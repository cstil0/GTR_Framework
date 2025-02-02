#include "renderer.h"

#include "application.h"
#include "camera.h"
#include "shader.h"
#include "mesh.h"
#include "texture.h"
#include "fbo.h"
#include "prefab.h"
#include "material.h"
#include "utils.h"
#include "scene.h"
#include "extra/hdre.h"

#include <iostream>
#include <algorithm>
#include <vector>  


using namespace GTR;


GTR::Renderer::Renderer()
{
	show_shadowmap = false;
	debug_shadowmap = 7;
	debug_texture = eTextureType::COMPLETE;
	fbo = NULL;
	shadowmap = NULL;
	max_lights = 10;
}

// --- Rendercalls manager functions ---

// Generate the rendercalls vector by iterating through the entities vector
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

// Recursive function to add a rendercall node with its children
void GTR::Renderer::addRenderCall_node(GTR::Scene* scene, Camera* camera, Node* node, Matrix44 curr_model, Matrix44 root_model) {
	RenderCall rc;
	// If the node doesn't have mesh or material do not add it
	if (node->material && node->mesh) {
		Vector3 nodepos = curr_model.getTranslation();
		rc.mesh = node->mesh;
		rc.material = node->material;
		rc.model = curr_model;
		rc.distance_to_camera = nodepos.distance(scene->main_camera.eye);
		rc.world_bounding = transformBoundingBox(curr_model, node->mesh->box);
		// If the material is opaque add a distance factor to sort it at the end of the vector
		if (rc.material->alpha_mode == GTR::eAlphaMode::BLEND)
		{
			int dist_factor = 1000000;
			rc.distance_to_camera += dist_factor;
		}
		render_calls.push_back(rc);
	}


	// Add also all the childrens of this node
	for (int j = 0; j < node->children.size(); ++j) {
		GTR::Node* curr_node = node->children[j];
		// Compute global matrix
		Matrix44 node_model = node->getGlobalMatrix(true) * root_model;
		addRenderCall_node(scene, camera, curr_node, node_model, root_model);
	}
}

// Sort rendercalls by distance
void GTR::Renderer::sortRenderCalls() {
	std::sort(render_calls.begin(), render_calls.end(), compare_distances);
}


// --- Shadowmap functions ---

// generate the shadowmap given a light
void GTR::Renderer::generateShadowmap(LightEntity* light)
{
	// only spot and directional lights cast shadows
	if (light->light_type != LightEntity::eTypeOfLight::SPOT && light->light_type != LightEntity::eTypeOfLight::DIRECTIONAL)
		return;
	if (!light->cast_shadows) {
		// if a created light is no longer casts shadows delete it
		if (light->fbo) {
			delete light->fbo;
			light->fbo = NULL;
			light->shadowmap = NULL;
		}
		return;
	}

	if (!light->fbo) {
		light->shadowmap = new Texture();
		light->fbo = new FBO();
		// We only need to store the depth buffer
		light->fbo->setDepthOnly(2048, 2048);
		// take the texture from the fbo and store it in another variable
		light->shadowmap = light->fbo->depth_texture;
	}
	// Create a new camera from light
	if (!light->light_camera)
		light->light_camera = new Camera();

	// Guardamos la camara anterior para no perderla
	Camera* view_camera = Camera::current;
	// activate fbo to start painting in it and not in the screen
	light->fbo->bind();
	Camera* light_camera = light->light_camera;

	if (light->light_type == LightEntity::eTypeOfLight::SPOT) {
		// set the perspective matrix for the light
		light_camera->setPerspective(light->cone_angle, 1.0, 0.1, light->max_distance);
		// locate and rotate the camera according to the light position, forward direction and up vector
		light_camera->lookAt(light->model.getTranslation(), light->model * Vector3(0, 0, -1), light->model.rotateVector(Vector3(0, 1, 0)));
	}
	else if (light->light_type == LightEntity::eTypeOfLight::DIRECTIONAL) {
		// tried to make the light follow the camera to reach all parts of the scene, but this also makes the light rotate, so it is not working
		//vec3 light_cam_pos = vec3(view_camera->eye.x, view_camera->eye.y, view_camera->eye.z);
		Application* app = Application::instance;
		float halfarea = light->area_size / 2;
		float aspect = Application::instance->window_width / (float)Application::instance->window_height;
		// set orthographic matrix for the light since all rays are parallel
		light_camera->setOrthographic(-halfarea, halfarea, -halfarea * aspect, halfarea * aspect, 0.1, light->max_distance);
		// locate and rotate the camera
		// Now, define center using the target vector since it corresponds to a point where the light is pointing
		light_camera->lookAt(light->model.getTranslation(), light->target, light->model.rotateVector(Vector3(0, 1, 0)));
	}

	light_camera->enable();
	// clear depth buffer to avoid ghosting artifacts
	glClear(GL_DEPTH_BUFFER_BIT);

	// paint all rendercalls
	for (int i = 0; i < render_calls.size(); i++) {
		RenderCall& rc = render_calls[i];
		// transparent materials do not cast shadows
		if (rc.material->alpha_mode == eAlphaMode::BLEND)
			continue;
		// render if the node is inside the frustum of the new camera
		if (light_camera->testBoxInFrustum(rc.world_bounding.center, rc.world_bounding.halfsize)) {
			renderFlatMesh(rc.model, rc.mesh, rc.material, light_camera);
		}
	}

	// go back to default system
	light->fbo->unbind();
	view_camera->enable();
}

// to show the shadowmap for debugging purposes
void Renderer::showShadowmap(LightEntity* light) {
	if (!light->shadowmap)
		return;

	Shader* depth_shader = Shader::getDefaultShader("depth");
	depth_shader->enable();
	// set uniforms to delinearize shadowmap texture
	depth_shader->setUniform("u_camera_nearfar", Vector2(light->light_camera->near_plane, light->light_camera->far_plane));
	glViewport(0, 0, 256, 256);
	light->shadowmap->toViewport(depth_shader);
	glViewport(0, 0, Application::instance->window_width, Application::instance->window_height);
}


// --- Render functions ---

// render all entities of the scene
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


// To render the scene according to the rendercalls vector
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

	// Generate shadowmaps
	for (int i = 0; i < lights.size(); i++) {
		LightEntity* light = lights[i];
		if (light->cast_shadows) {
			generateShadowmap(light);
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

		// if rendercall has mesh and material, render it
		if (rc.mesh && rc.material) {
			// test if node inside the frustum of the camera
			if (camera->testBoxInFrustum(rc.world_bounding.center, rc.world_bounding.halfsize))
				renderMeshWithMaterial(rc.model, rc.mesh, rc.material, camera);
		}
	}
	// show shadowmap if activated
	if (show_shadowmap) 
		showShadowmap(lights[debug_shadowmap]);
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

	Shader* shader = NULL;

	//select if render both sides of the triangles
	if(material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR);

	//chose a shader
	Scene* scene = Scene::instance;
	if (scene->typeOfRender == Scene::eRenderPipeline::SINGLEPASS)
		shader = Shader::Get("single_pass");
	else if (scene->typeOfRender == Scene::eRenderPipeline::MULTIPASS)
		shader = Shader::Get("multi_pass");

    assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);
	float t = getTime();
	shader->setUniform("u_time", t );

	shader->setUniform("u_color", material->color);
	// pass textures to the shader
	setTextures(material, shader);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	// pass light parameters
	if (scene->typeOfRender == Scene::eRenderPipeline::SINGLEPASS) {
		setSinglepass_parameters(material, shader, mesh);
	}
	else if (scene->typeOfRender == Scene::eRenderPipeline::MULTIPASS) {
		setMultipassParameters(material, shader, mesh);
	}

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glDepthFunc(GL_LESS);
}

// to pass the textures to the shader
void Renderer::setTextures(GTR::Material* material, Shader* shader) {
	Texture* texture = NULL;
	Texture* normal_texture = NULL;
	Texture* emissive_texture = NULL;
	Texture* occlusion_texture = NULL;
	Texture* met_rough_texture = NULL;

	// save textures
	texture = material->color_texture.texture;
	normal_texture = material->normal_texture.texture;
	emissive_texture = material->emissive_texture.texture;
	met_rough_texture = material->metallic_roughness_texture.texture;
	occlusion_texture = material->occlusion_texture.texture;

	if (texture == NULL)
		texture = Texture::getWhiteTexture(); //a 1x1 white texture

	// pass textures
	if (texture)
		shader->setUniform("u_texture", texture, 0);

	if (emissive_texture)
		shader->setUniform("u_emissive_texture", emissive_texture, 1);
	// black texture will not add additional light
	else
		shader->setUniform("u_emissive_texture", Texture::getBlackTexture(), 1);
	
	if (occlusion_texture)
		shader->setUniform("u_occlusion_texture", occlusion_texture, 2);
	// white texture will take into account all light, namely no occlusion
	else
		shader->setUniform("u_occlusion_texture", Texture::getWhiteTexture(), 2);
	
	if (met_rough_texture)
		shader->setUniform("u_met_rough_texture", met_rough_texture, 3);
	else
		shader->setUniform("u_met_rough_texture", Texture::getWhiteTexture(), 3);

	if (normal_texture) {
		shader->setUniform("u_normal_texture", normal_texture, 4);
		shader->setUniform("u_normal_text_bool", 1);
	}
	// if the material do not have normal texture as the floor, we set the boolean to false to avoid artifacts
	else
		shader->setUniform("u_normal_text_bool", 0);

	shader->setUniform("u_texture2show", debug_texture);
}

void Renderer::setSinglepass_parameters(GTR::Material* material, Shader* shader, Mesh* mesh) {
	//select the blending
	if (material->alpha_mode == GTR::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);

	// Define some variables to store lights information
	std::vector<int> lights_type;
	std::vector<Vector3> lights_position;
	std::vector<Vector3> lights_color;
	std::vector<float> lights_max_distance;
	// spot and directional parameters
	std::vector<float> lights_cone_cos;
	std::vector<float> lights_cone_exp;
	std::vector<Vector3> lights_direction;
	// shadows parameters
	std::vector<int> lights_cast_shadows;
	// didn't make it to pass multiple textures to the shader, so shadows are not working in singlepass
	std::vector<Texture*> lights_shadowmap;
	std::vector<Matrix44> lights_shadowmap_vpm;
	std::vector<float> lights_shadow_bias;


	// Iterate and store the information
	for (int i = 0; i < max_lights; i++) {
		LightEntity* light;
		// if the light is not created, pass an empty light to the shader to avoid taking trash information
		if (i < lights.size() && lights[i]->visible)
			light = lights[i];
		else
			light = new LightEntity();

		// add the information to the vectors
		lights_type.push_back(light->light_type);
		lights_position.push_back(light->model.getTranslation());
		lights_color.push_back(light->color * light->intensity);
		lights_max_distance.push_back(light->max_distance);
		lights_cone_cos.push_back((float)cos(light->cone_angle * DEG2RAD));
		lights_cone_exp.push_back(light->cone_exp);

		if (light->light_type == LightEntity::eTypeOfLight::SPOT)
			// take the forward vector of the light
			lights_direction.push_back(light->model.rotateVector(Vector3(0.0, 0.0, -1.0)));
		else if (light->light_type == LightEntity::eTypeOfLight::DIRECTIONAL)
			// get direction of the light taking the target point
			lights_direction.push_back((light->model.getTranslation() - light->target));
		else {
			lights_direction.push_back(vec3(0.0, 0.0, 0.0));
		}

		if (light->cast_shadows) {
			lights_cast_shadows.push_back(1);
			lights_shadowmap.push_back(light->shadowmap);
			lights_shadowmap_vpm.push_back(light->light_camera->viewprojection_matrix);
			lights_shadow_bias.push_back(light->shadow_bias);
		}
		else {
			lights_cast_shadows.push_back(0);
		}
	}

	// Pass to the shader
	Scene* scene = Scene::instance;

	shader->setUniform("u_lights_type", lights_type);
	shader->setUniform("u_ambient_light", scene->ambient_light);
	shader->setUniform("u_lights_position", lights_position);
	shader->setUniform("u_lights_color", lights_color);
	shader->setUniform("u_lights_max_distance", lights_max_distance);

	// Use the cosine to compare it directly to NdotL
	shader->setUniform("u_lights_cone_cos", lights_cone_cos);
	shader->setUniform("u_lights_cone_exp", lights_cone_exp);
	shader->setUniform("u_lights_direction", lights_direction);

	shader->setUniform("u_lights_cast_shadows", lights_cast_shadows);
	shader->setUniform("u_light_shadowmap", lights_shadowmap, 8);
	shader->setUniform("u_light_shadowmap_vpm", lights_shadowmap_vpm);
	shader->setUniform("u_light_shadow_bias", lights_shadow_bias);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	// clear all vectors
	lights_type.clear();
	lights_position.clear();
	lights_color.clear();
	lights_max_distance.clear();
	lights_cone_cos.clear();
	lights_cone_exp.clear();
	lights_direction.clear();
	lights_cast_shadows.clear();
	lights_shadowmap.clear();
	lights_shadowmap_vpm.clear();
	lights_shadow_bias.clear();
}

void Renderer::setMultipassParameters(GTR::Material* material, Shader* shader, Mesh* mesh) {
	// paint if value is less or equal to the one in the depth buffer
	glDepthFunc(GL_LEQUAL);

	// do a linear interpolation adding the pixel painted with the current one
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	Scene* scene = Scene::instance;

	Vector3 ambient_light = scene->ambient_light;

	// To know if there is any light visible
	bool any_visible = false;
	// to know if we are in first iteration of a visible light, since the first light can be disabled and therefore blending will not be activated for transparent materials
	bool is_first = true;

	for (int i = 0; i < lights.size(); ++i) {
		LightEntity* light = lights[i];

		if (!lights[i]->visible)
			continue;

		if (is_first) {
			// select the blending
			if (material->alpha_mode == GTR::eAlphaMode::BLEND)
			{
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}
			else
				glDisable(GL_BLEND);
		}

		// if a texture is selected in imgui, render just one time
		else if (debug_texture != eTextureType::COMPLETE)
			continue;

		// There is at least one visible light
		any_visible = true;
		// we already passed first light
		is_first = false;

		// Pass to the shader
		shader->setUniform("u_light_type", light->light_type);
		shader->setUniform("u_ambient_light", ambient_light);
		shader->setUniform("u_light_position", light->model.getTranslation());
		shader->setUniform("u_light_color", light->color * light->intensity);
		shader->setUniform("u_light_max_distance", light->max_distance);

		// Use the cosine to compare it directly to NdotL
		shader->setUniform("u_light_cone_cos", (float)cos(light->cone_angle * DEG2RAD));
		shader->setUniform("u_light_cone_exp", light->cone_exp);

		if (light->light_type == LightEntity::eTypeOfLight::SPOT)
			shader->setUniform("u_light_direction", light->model.rotateVector(Vector3(0.0, 0.0, -1.0)));
		else if (light->light_type == LightEntity::eTypeOfLight::DIRECTIONAL)
			shader->setUniform("u_light_direction", (light->model.getTranslation() - light->target));
		
		if (light->shadowmap && light->cast_shadows) {
			shader->setUniform("u_light_cast_shadows", 1);
			shader->setUniform("u_light_shadowmap", light->shadowmap, 8);
			shader->setUniform("u_light_shadowmap_vpm", light->light_camera->viewprojection_matrix);
			shader->setUniform("u_light_shadow_bias", light->shadow_bias);
		}
		else
			shader->setUniform("u_light_cast_shadows", 0);

		//do the draw call that renders the mesh into the screen
		mesh->render(GL_TRIANGLES);

		// Activate blending again for the rest of lights to do the interpolation
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);


		// Reset ambient light to add it only once
		ambient_light = vec3(0.0, 0.0, 0.0);
	}

	// If no light is visible, pass only the ambient light to the shader
	if (any_visible == false) {
		shader->setUniform("u_ambient_light", ambient_light);
	}
}

// to save fbo with depth buffer
void Renderer::renderFlatMesh(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera) {
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;

	//select if render both sides of the triangles
	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	//chose a shader
	Scene* scene = Scene::instance;
	shader = Shader::Get("flat");


	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_model", model);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	// don't need blending
	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);

	mesh->render(GL_TRIANGLES);
	//disable shader
	shader->disable();
}

void GTR::Renderer::renderInMenu() {
	ImGui::Checkbox("Show Shadowmap", &show_shadowmap);
	ImGui::Combo("Shadowmaps", &debug_shadowmap, "SPOT1\0SPOT2\0POINT1\0POINT2\0POINT3\0POINT4\0POINT5\0DIRECTIONAL");
	ImGui::Combo("Textures", &debug_texture, "COMPLETE\0NORMAL\0OCCLUSION\0EMISSIVE");
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


