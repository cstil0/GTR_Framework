#include "renderer.h"

#include "camera.h"
#include "shader.h"
#include "mesh.h"
#include "texture.h"
#include "prefab.h"
#include "material.h"
#include "utils.h"
#include "scene.h"
#include "extra/hdre.h"

#include <iostream>
#include <algorithm>
#include <vector>  


using namespace GTR;

void Renderer::renderScene(GTR::Scene* scene, Camera* camera)
{
	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	//render entities
	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;

		//is a prefab!
		if (ent->entity_type == PREFAB)
		{
			PrefabEntity* pent = (GTR::PrefabEntity*)ent;
			if(pent->prefab)
				renderPrefab(ent->model, pent->prefab, camera);
		}
	}
}

// To render the scene according to the render_calls vector
// L'ESCENA NO S'HAURIA DE PASSAR
void Renderer::renderScene_RenderCalls(GTR::Scene* scene, Camera* camera){
	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	// Create the lights vector
	lights.clear();
	for (int i = 0; i < scene->entities.size(); i++){
		BaseEntity* ent = scene->entities[i];
		if (ent->entity_type == GTR::eEntityType::LIGHT) {
			LightEntity* light = (LightEntity*)ent;
			lights.push_back(light);
		}
	}

	// Create the vector of nodes
	createRenderCalls(scene, camera);

	// Sort the objects by distance to the camera
	sortRenderCalls();

	//render rendercalls
	for (int i = 0; i < render_calls.size(); ++i) {
		// Instead of rendering the entities vector, render the render_calls vector
		RenderCall rc = render_calls[i];

		//does this node have a mesh? then we must render it
		//if (rc.mesh && rc.material)
		//{
			//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		//BoundingBox world_bounding = transformBoundingBox(rc.model, rc.mesh->box);

			//if bounding box is inside the camera frustum then the object is probably visible
			//if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize))
			//{
				////compute global matrix
				//Matrix44 node_model = node->getGlobalMatrix(true) * prefab_model;

		renderMeshWithMaterial(rc.model, rc.mesh, rc.material, camera);
			//}
		//}
	}
}

//renders all the prefab
void Renderer::renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera)
{
	assert(prefab && "PREFAB IS NULL");
	//assign the model to the root node
	renderNode(model, &prefab->root, camera);
}

//renders a node of the prefab and its children
void Renderer::renderNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera)
{
	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true) * prefab_model;

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(node_model,node->mesh->box);
		
		//if bounding box is inside the camera frustum then the object is probably visible
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )
		{
			//render node mesh
			renderMeshWithMaterial( node_model, node->mesh, node->material, camera );
			//node->mesh->renderBounding(node_model, true);
		}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		renderNode(prefab_model, node->children[i], camera);
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
    assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;
	Texture* texture = NULL;

	texture = material->color_texture.texture;
	//texture = material->emissive_texture;
	//texture = material->metallic_roughness_texture;
	//texture = material->normal_texture;
	//texture = material->occlusion_texture;
	if (texture == NULL)
		texture = Texture::getWhiteTexture(); //a 1x1 white texture

	//select the blending
	if (material->alpha_mode == GTR::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if(material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR);

	//chose a shader
	shader = Shader::Get("light");

    assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();


	// HI HA DUES MANERES DE COMPUTAR VARIES LLUMS:
	// SINGLE PASS -- ENVIEM LA INFO DE TOTES LES LLUMS I AL SHADER FEM UN FOR PER RECORRER TOTES LES 
	// LLUMS I ANAR SUMANT. EL QUE PASSA �S QUE S'HA DE DEFINIR UNA VARIABLE AMB EL M�XIM DE LIGHTS
	// MULTIPASS -- M�S EFICIENT: PRINTEM L'OBJECTE TANTES VEGADES COM LLUMS TENIM. EL PROBLEMA �S QUE LLAVORS EL Z BUFFER DONA
	// PROBLEMES DE MANERA QUE PRINTEM SI EL ZBUFFER T� UN OBJECTE A UNA DIST�NCIA <= I FEM BLENDING NOM�S A LA PRIMERA ITERACI�
	// (EL FOR ARA EST� AL RENDERER, NO AL SHADER)
	
	// Valor por defecto
	glDepthFunc(GL_LESS);
	// PINTAMOS SI EL VALOR QUE HAY EN EL DEPTH BUFFER ES MENOR O IGUAL AL QUE QUEREMOS PINTAR. AS� NO TENEMOS PROBLEMA CON PINTAR VARIAS VECES LA MESH
	glDepthFunc(GL_LEQUAL);
	//glBlendFunc(GL_LEQUAL, GL_ONE); // LO SEGUNDO ES PARA HACER UNA INTERPOLACI�N, pero no he acabado de entender por qu� este tipo

	// LA LUZ AMBIENTE SOLO DEBER�A SUMARSE UNA VEZ!! POR LO TANTO DESPU�S DE LA PRIMERA ITERACI�N LA PONEMOS A CERO
	//for (int i = 0; i < lights.size(); ++i) {
		//// DESACTIVAMOS EL BLEND PARA LA PRIMERA LUZ -- PORR???
		//if (i == 0) {
		//	glDisable(GL_BLEND);
		//}
		//else
		//	glEnable(GL_BLEND);
		//// PASSEM ELS UNIFORMS DE LA LIGHT[I]
	//}

	//upload uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model );
	float t = getTime();
	shader->setUniform("u_time", t );

	shader->setUniform("u_color", material->color);
	if(texture)
		shader->setUniform("u_texture", texture, 0);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);


	// Light parameters
	Scene* scene = Scene::instance;
	shader->setUniform("u_ambient_light", scene->ambient_light);
	shader->setUniform("u_light_position", lights[0]->model.getTranslation());
	shader->setUniform("u_light_color", lights[0]->color);
	shader->setUniform("u_intensity", lights[0]->intensity);
	shader->setUniform("u_max_distance", lights[0]->max_distance);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
}

Texture* GTR::CubemapFromHDRE(const char* filename)
{
	HDRE* hdre = HDRE::Get(filename);
	if (!hdre)
		return NULL;

	Texture* texture = new Texture();
	if (hdre->getFacesf(0))
	{
		texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesf(0),
			hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_FLOAT);
		for (int i = 1; i < hdre->levels; ++i)
			texture->uploadCubemap(texture->format, texture->type, false,
				(Uint8**)hdre->getFacesf(i), GL_RGBA32F, i);
	}
	else
		if (hdre->getFacesh(0))
		{
			texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesh(0),
				hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_HALF_FLOAT);
			for (int i = 1; i < hdre->levels; ++i)
				texture->uploadCubemap(texture->format, texture->type, false,
					(Uint8**)hdre->getFacesh(i), GL_RGBA16F, i);
		}
	return texture;
}

void GTR::Renderer::addRenderCall_node(GTR::Scene* scene, Camera* camera, Node* node, Matrix44 curr_model, Matrix44 root_model) {
	RenderCall rc;
	// If the node doesn't have mesh or material we do not add it

	if (node->material && node->mesh) {
		BoundingBox world_bounding;
		// If the node is parent it will not have any mesh but we want to add it
		world_bounding = transformBoundingBox(curr_model, node->mesh->box);
		// Add only if inside the frustum of the camera
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize)) {
			Vector3 nodepos = curr_model.getTranslation();
			rc.mesh = node->mesh;
			rc.material = node->material;
			rc.model = curr_model;
			rc.distance_to_camera = nodepos.distance(scene->main_camera.eye);
			// If the material is opaque add a distance factor to sort it at the end of the vector
			if (rc.material->alpha_mode == GTR::eAlphaMode::BLEND)
			{
				int dist_factor = 1000000;
				rc.distance_to_camera += dist_factor;
			}
			render_calls.push_back(rc);
		}
	}



	// Add also all the childrens from this node
	for (int j = 0; j < node->children.size(); ++j) {
		GTR::Node* curr_node = node->children[j];
		// Compute global matrix
		Matrix44 node_model = node->getGlobalMatrix(true) * root_model;
		addRenderCall_node(scene, camera, curr_node, node_model, root_model);
	}
}

void GTR::Renderer::createRenderCalls(GTR::Scene* scene, Camera* camera)
{
	render_calls.clear();

	// Iterate the entities vector to save each node
	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];

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
				Matrix44 node_model = curr_node->getGlobalMatrix(true) * ent->model;
				// Pass the global matrix for this node and the root one to compute the global matrix for the rest of children
				addRenderCall_node(scene, camera, curr_node, node_model, ent->model);
			}
		}
	}
}

void GTR::Renderer::sortRenderCalls() {
	std::sort(render_calls.begin(), render_calls.end(), compare_distances);
}