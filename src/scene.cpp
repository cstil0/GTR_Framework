#include "scene.h"
#include "utils.h"

#include "prefab.h"
#include "light.h"
#include "extra/cJSON.h"

#include <iostream>
#include <algorithm>
#include <vector>    

GTR::Scene* GTR::Scene::instance = NULL;

GTR::Scene::Scene()
{
	instance = this;
	
}

void GTR::Scene::clear()
{
	for (int i = 0; i < entities.size(); ++i)
	{
		BaseEntity* ent = entities[i];
		delete ent;
	}
	entities.resize(0);
}


void GTR::Scene::addEntity(BaseEntity* entity)
{
	entities.push_back(entity); entity->scene = this;
}

bool GTR::Scene::load(const char* filename)
{
	std::string content;

	this->filename = filename;
	std::cout << " + Reading scene JSON: " << filename << "..." << std::endl;

	if (!readFile(filename, content))
	{
		std::cout << "- ERROR: Scene file not found: " << filename << std::endl;
		return false;
	}

	//parse json string 
	cJSON* json = cJSON_Parse(content.c_str());
	if (!json)
	{
		std::cout << "ERROR: Scene JSON has errors: " << filename << std::endl;
		return false;
	}

	//read global properties
	// SE REPTE LA VARIABLE DENTRO DE LA FUNCIÓN POR QUÉ SI NO ENCUENTRA ESE VALOR EN EL JSON
	// LE ASIGNA EL QUE YA TENÍA -- ES UN PEQUEÑO HACK
	background_color = readJSONVector3(json, "background_color", background_color);
	ambient_light = readJSONVector3(json, "ambient_light", ambient_light );
	main_camera.eye = readJSONVector3(json, "camera_position", main_camera.eye);
	main_camera.center = readJSONVector3(json, "camera_target", main_camera.center);
	main_camera.fov = readJSONNumber(json, "camera_fov", main_camera.fov);

	//entities
	cJSON* entities_json = cJSON_GetObjectItemCaseSensitive(json, "entities");
	cJSON* entity_json;
	cJSON_ArrayForEach(entity_json, entities_json)
	{
		std::string type_str = cJSON_GetObjectItem(entity_json, "type")->valuestring;
		BaseEntity* ent = createEntity(type_str);
		if (!ent)
		{
			std::cout << " - ENTITY TYPE UNKNOWN: " << type_str << std::endl;
			//continue;
			ent = new BaseEntity();
		}

		addEntity(ent);


		// PROPIEDAES GENÉRICAS DE TODAS LAS ENTIDADES
		if (cJSON_GetObjectItem(entity_json, "name"))
		{
			ent->name = cJSON_GetObjectItem(entity_json, "name")->valuestring;
			stdlog(std::string(" + entity: ") + ent->name);
		}

		//read transform
		if (cJSON_GetObjectItem(entity_json, "position"))
		{
			ent->model.setIdentity();
			Vector3 position = readJSONVector3(entity_json, "position", Vector3());
			ent->model.translate(position.x, position.y, position.z);
		}

		if (cJSON_GetObjectItem(entity_json, "angle"))
		{
			float angle = cJSON_GetObjectItem(entity_json, "angle")->valuedouble;
			ent->model.rotate(angle * DEG2RAD, Vector3(0, 1, 0));
		}

		if (cJSON_GetObjectItem(entity_json, "rotation"))
		{
			Vector4 rotation = readJSONVector4(entity_json, "rotation");
			Quaternion q(rotation.x, rotation.y, rotation.z, rotation.w);
			Matrix44 R;
			q.toMatrix(R);
			ent->model = R * ent->model;
		}

		if (cJSON_GetObjectItem(entity_json, "target"))
		{
			Vector3 target = readJSONVector3(entity_json, "target", Vector3());
			Vector3 front = target - ent->model.getTranslation();
			ent->model.setFrontAndOrthonormalize(front);
		}

		if (cJSON_GetObjectItem(entity_json, "scale"))
		{
			Vector3 scale = readJSONVector3(entity_json, "scale", Vector3(1, 1, 1));
			ent->model.scale(scale.x, scale.y, scale.z);
		}

		ent->configure(entity_json);
	}

	//free memory
	cJSON_Delete(json);

	return true;
}

GTR::BaseEntity* GTR::Scene::createEntity(std::string type)
{
	if (type == "PREFAB")
		return new GTR::PrefabEntity();
	// SI ES UNA LIGHT PUES CREAMOS UN NODO LIGHT
    return NULL;
}

void GTR::Scene::addRenderCall_node(Node* node, Matrix44 curr_model, Matrix44 root_model) {
	// If the node doesn't have mesh or material we do not add it
	if (node->material || node->mesh) {
		RenderCall rc;
		Vector3 nodepos = curr_model.getTranslation();
		rc.mesh = node->mesh;
		rc.material = node->material;
		rc.model = curr_model;

		rc.distance_to_camera = nodepos.distance(main_camera.eye);
		// If the material is opaque add a distance factor to sort it at the end of the vector
		if (rc.material) {
			if (rc.material->alpha_mode == GTR::eAlphaMode::BLEND)
			{
				int dist_factor = 1000000;
				rc.distance_to_camera += dist_factor;
			}

		}
		render_calls.push_back(rc);
	}


	// Add also all the childrens from this node
	for (int j = 0; j < node->children.size(); ++j) {
		GTR::Node* curr_node = node->children[j];
		// Compute global matrix
		Matrix44 node_model = node->getGlobalMatrix(true) * root_model;
		addRenderCall_node(curr_node, node_model, root_model);
	}
}

//void GTR::Scene::addRenderCall_light(LightEntity* node) {
//	RenderCall rc;
//	Vector3 nodepos = curr_node->model.getTranslation();
//	rc.mesh = curr_node->mesh;
//	rc.material = curr_node->material;
//	rc.model = curr_node->model;
//	rc.distance_to_camera = nodepos.distance(main_camera.eye);
//	render_calls.push_back(&rc);
//}

// AIXÒ S'HA DE FER AL RENDERER!
void GTR::Scene::createRenderCalls()
{
	// PER SI FEM LO DE REPETIR LA FUNCIÓ A CADA UPDATE
	render_calls.clear();

	// Iterate the entities vector to save each node
	for (int i = 0; i < entities.size(); ++i)
	{
		BaseEntity* ent = entities[i];

		// Save only the visible nodes
		if (!ent->visible)
			continue;

		// If prefab iterate the nodes
		if (ent->entity_type == PREFAB)
		{
			PrefabEntity* pent = (GTR::PrefabEntity*)ent;
			// First take the root node
			if (pent->prefab) {
				GTR::Node* curr_node = &pent->prefab->root;
				// Compute global matrix
				Matrix44 node_model = curr_node->getGlobalMatrix(true) * ent->model;
				// Pass the global matrix for this node and the root one to compute the global matrix for the rest of children
				addRenderCall_node(curr_node, node_model, ent->model);
			}
		}
	}
}

void GTR::Scene::sortRenderCalls(){
	std::vector<RenderCall> rc_test;
	RenderCall rc1;
	rc1.distance_to_camera = 10;
	rc_test.push_back(rc1);

	RenderCall rc2;
	rc2.distance_to_camera = 1;
	rc_test.push_back(rc2);

	RenderCall rc3;
	rc3.distance_to_camera = 5;
	rc_test.push_back(rc3);

	RenderCall rc4;
	rc4.distance_to_camera = -3;
	rc_test.push_back(rc4);

	RenderCall rc5;
	rc5.distance_to_camera = 20;
	rc_test.push_back(rc5);

	std::sort(render_calls.begin(), render_calls.end(), compare_distances);
}


void GTR::BaseEntity::renderInMenu()
{
#ifndef SKIP_IMGUI
	ImGui::Text("Name: %s", name.c_str()); // Edit 3 floats representing a color
	ImGui::Checkbox("Visible", &visible); // Edit 3 floats representing a color
	//Model edit
	ImGuiMatrix44(model, "Model");
#endif
}

GTR::PrefabEntity::PrefabEntity()
{
	entity_type = PREFAB;
	prefab = NULL;
}

void GTR::PrefabEntity::configure(cJSON* json)
{
	if (cJSON_GetObjectItem(json, "filename"))
	{
		filename = cJSON_GetObjectItem(json, "filename")->valuestring;
		prefab = GTR::Prefab::Get( (std::string("data/") + filename).c_str());
	}
}

void GTR::PrefabEntity::renderInMenu()
{
	BaseEntity::renderInMenu();

#ifndef SKIP_IMGUI
	ImGui::Text("filename: %s", filename.c_str()); // Edit 3 floats representing a color
	if (prefab && ImGui::TreeNode(prefab, "Prefab Info"))
	{
		prefab->root.renderInMenu();
		ImGui::TreePop();
	}
#endif
}

//GTR::LightEntity::LightEntity()
//{
//	entity_type = LIGHT;
//	light = NULL;
//	color.set(1, 1, 1);
//	intensity = 1;
//}
//
//void GTR::LightEntity::renderInMenu() {
//	//First render the base menu
//	BaseEntity::renderInMenu();
//#ifndef SKIP_IMGUI
//	ImGui::Text("filename: %s", filename.c_str()); // Edit 3 floats representing a color
//	if (light && ImGui::TreeNode(light, "Light Info"))
//	{
//		//light->renderInMenu();
//		ImGui::TreePop();
//	}
//
//	// AQUÍ HA FET ALGO QUE DIU QUE ERA PER DEBUGAR AMB UN SWITCH --- NO M'HE ENTERAT BÉ
//#endif
//
//}

//void GTR::LightEntity::configure(cJSON* json)
//{
//	if (cJSON_GetObjectItem(json, "filename"))
//	{
//		// Read parameters
//		color = readJSONVector3(json, 'color', color);
//		// FALTEN LA RESTA DE PARÀMETRES
//		std::string str = readJSONString(json, 'light_type', '');
//		if (str == 'POINT')
//			light_type = eLightType::POINT;
//	}
//}
