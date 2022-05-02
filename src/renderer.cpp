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
	shadowmap2show = 3;
	texture2show = 0;
	fbo = NULL;
	shadowmap = NULL;
}

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
		if (camera->testBoxInFrustum(rc.world_bounding.center, rc.world_bounding.halfsize))
			renderMeshWithMaterial(rc.model, rc.mesh, rc.material, camera);
			//}
		//}
	}
	if (show_shadowmap) 
		showShadowmap(lights[shadowmap2show]);
}

void Renderer::showShadowmap(LightEntity* light) {
	if (!light->shadowmap)
		return;

	// USAMOS UN SHADER QUE ENTIENDE QUE ESTAMOS USANDO UNA TEXTURA QUE TIENE PROFUNDIDAD Y NO COLOR, SINO NO VEREMOS NADA
	// LO QUE HACE ESTE SHADER ES COGER LA TEXTURA Y DESLINEALIZARLA, Y PARA ELLO NECESITA SABER EL NEAR Y EL FAR DE LA CAMARA
	Shader* depth_shader = Shader::getDefaultShader("depth");
	depth_shader->enable();
	depth_shader->setUniform("u_camera_nearfar", Vector2(light->light_camera->near_plane, light->light_camera->far_plane));
	glViewport(0, 0, 256, 256);
	light->shadowmap->toViewport(depth_shader);
	glViewport(0, 0, Application::instance->window_width, Application::instance->window_height);
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
	Texture* normal_texture = NULL;
	Texture* emissive_texture = NULL;
	Texture* occlusion_texture = NULL;
	Texture* met_rough_texture = NULL;

	texture = material->color_texture.texture;
	normal_texture = material->normal_texture.texture;
	emissive_texture = material->emissive_texture.texture;
	met_rough_texture = material->metallic_roughness_texture.texture;
	occlusion_texture = material->occlusion_texture.texture;

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
	Scene* scene = Scene::instance;
	// ES UNA BUENA IDEA HACER ESTE IF DOS VECES...?
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
	shader->setUniform("u_model", model );
	float t = getTime();
	shader->setUniform("u_time", t );

	shader->setUniform("u_color", material->color);
	// Textures
	if(texture)
		shader->setUniform("u_texture", texture, 0);
	if (emissive_texture)
		shader->setUniform("u_emissive_texture", emissive_texture, 1);
	else
		shader->setUniform("u_emissive_texture", Texture::getBlackTexture(), 1);
	if (occlusion_texture)
		shader->setUniform("u_occlusion_texture", occlusion_texture, 2);
	else
		shader->setUniform("u_occlusion_texture", Texture::getWhiteTexture(), 2);
	if (met_rough_texture)
		shader->setUniform("u_met_rough_texture", met_rough_texture, 3);
	else
		shader->setUniform("u_met_rough_texture", Texture::getWhiteTexture(), 3);

	if (normal_texture)
		//shader->setUniform("u_normal_texture", normal_texture, 4);
		shader->setUniform("u_normal_texture", normal_texture, 4);
	else
		shader->setUniform("u_normal_texture", Texture::getWhiteTexture(), 4);
	//else
	//	shader->setUniform("u_met_rough_texture", Texture::getWhiteTexture(), 3);

	shader->setUniform("u_texture2show", texture2show);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);


	// Light parameters
	// -- Single Pass --
	if (scene->typeOfRender == Scene::eRenderPipeline::SINGLEPASS) {
		// Define some variables to store lights information
		std::vector<int> lights_type;
		std::vector<Vector3> lights_position;
		std::vector<Vector3> lights_color;
		std::vector<float> lights_max_distance;
		std::vector<float> lights_cone_cos;
		std::vector<float> lights_cone_exp;
		std::vector<Vector3> lights_direction;
		std::vector<int> lights_cast_shadows;
		std::vector<Texture*> lights_shadowmap;
		std::vector<Matrix44> lights_shadowmap_vpm;
		std::vector<float> lights_shadow_bias;

		// Iterate and store the information
		// ENLLOC DE CREAR UNA LIGHT BUIDA S'HAURIA DE PROBAR A FER UN IF AL SHADER PER SI LA PROPIETAT EXISTEIX
		// I FER PUSHBACKS NÓMÉS SI LA LIGHT TE AQUELLA PROPIETAT
		// PERO POTSER EN REALITAT ES MENYS EFICIENT
		for (int i = 0; i < 10; i++) {
			LightEntity* light;
			if (i < lights.size() && lights[i]->visible)
				light = lights[i];
			else
				light = new LightEntity();

			lights_type.push_back(light->light_type);
			lights_position.push_back(light->model.getTranslation());
			lights_color.push_back(light->color * light->intensity);
			lights_max_distance.push_back(light->max_distance);
			lights_cone_cos.push_back((float)cos(light->cone_angle * DEG2RAD));
			lights_cone_exp.push_back(light->cone_exp);
			lights_direction.push_back(light->model.rotateVector(Vector3(0.0, 0.0, -1.0)));

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

		lights_type.clear();
		lights_position.clear();
		lights_color.clear();
		lights_max_distance.clear();
	}

	// -- Multi Pass --
	else if (scene->typeOfRender == Scene::eRenderPipeline::MULTIPASS) {
		// Valor por defecto
		//glDepthFunc(GL_LESS);
		// PINTAMOS SI EL VALOR QUE HAY EN EL DEPTH BUFFER ES MENOR O IGUAL AL QUE QUEREMOS PINTAR. ASÍ NO TENEMOS PROBLEMA CON PINTAR VARIAS VECES LA MESH
		// Draw if the pixel in the depth buffer has less or equal distance from the one drawn
		glDepthFunc(GL_LEQUAL);

		// Do a linear interpolation btw the pixel * 1 and what we want to draw * 1
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		// Disable blending for the first light
		//glDisable(GL_BLEND);

		Vector3 ambient_light = scene->ambient_light;
		// To know if there is any light visible
		bool any_visible= false;

		for (int i = 0; i < lights.size(); ++i) {
			LightEntity* light = lights[i];

			if (!lights[i]->visible)
				continue;

			// There is at least one visible light
			any_visible = true;

			// Pass to the shader
			shader->setUniform("u_light_type", light->light_type);
			shader->setUniform("u_ambient_light", ambient_light);
			shader->setUniform("u_light_position", light->model.getTranslation());
			shader->setUniform("u_light_color", light->color*light->intensity);
			shader->setUniform("u_light_max_distance", light->max_distance);

			// Use the cosine to compare it directly to NdotL
			shader->setUniform("u_light_cone_cos", (float)cos(light->cone_angle*DEG2RAD));
			shader->setUniform("u_light_cone_exp", light->cone_exp);
			shader->setUniform("u_light_direction", light->model.rotateVector(Vector3(0.0, 0.0, -1.0)));

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

			// Reset ambient light to add it only once
			ambient_light = vec3(0.0, 0.0, 0.0);
		}

		// If no light is visible, pass only the ambient light to the shader
		if (any_visible == false) {
			shader->setUniform("u_ambient_light", ambient_light);
		}
	}


	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glDepthFunc(GL_LESS);
}

// PARA GUARDAR EL FBO CON EL DEPTH BUFFER
// NO RENDERIZAMOS TEXTURAS NI LUCES
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

	// NO NECESITAMOS BLENDING
	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);

	mesh->render(GL_TRIANGLES);
	//disable shader
	shader->disable();

}

void GTR::Renderer::generateShadowmap(LightEntity* light)
{
	if (light->light_type != LightEntity::eTypeOfLight::SPOT && light->light_type != LightEntity::eTypeOfLight::DIRECTIONAL)
		return;

	// EN LUGAR DE USAR MUCHAS TEXTURAS DEBERÍAMOS USAR UN ATLAS Y UNA FUNCIÓN QUE ASIGNE UN TROZO
	// DE ESA TEXTURA A CADA LUZ, SINO PARA MUCHAS LUCES SON MUCHAS TEXTURAS Y ES MUY COSTOSO.
	if (!light->cast_shadows) {
		// SI TENEMOS UNA LUZ CREADA QUE YA NO CASTEA SOMBRAS LO BORRAMOS DE MEMORIA
		if (light->fbo) {
			// EN REALIDAD ESTE DELETE YA SE ENCARGA DE LIMPIAR LAS TEXTURAS QUE TENGA DENTRO
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
		// Create a 1024x1024 depth texture
		// TEXTURA QUE TIENE UN COMPONENTE DE COLOR CON LA DISTANCIA DE CADA PIXEL
		// MEJOR PARA LA GPU SI ES POTENCIA DE 2
		light->fbo->setDepthOnly(2048, 2048);
		// SACAMOS LA TEXTURA DEL FBO Y LO GUARDAMOS EN UNA TEXTURA A PARTE
		light->shadowmap = light->fbo->depth_texture;
	}
	if (!light->light_camera)
		light->light_camera = new Camera();

	// Guardamos la camara anterior para no perderla
	Camera* view_camera = Camera::current;
	// ACTIVAMOS EL FBO -- A PARTIR DE AHORA YA NO EMPEZAMOS A PINTAR EN LA PANTALLA SINO EN LA TEXUTRA
	light->fbo->bind();
	Camera* light_camera = light->light_camera;


	if (light->light_type == LightEntity::eTypeOfLight::SPOT) {
		// DEFINIMOS LA PERSPECTIVA DE LA CAMARA (primero para la spot)
		// FOV -  ES EL ANGULO DE VISTA DE LA CAMARA, EN ESTE CASO EL CONE ANGLE DEL SPOTLIGHT
		// ASPECT-1 POR QUE LA TEXTURA ES CUADRADA
		// NEAR I FAR ENS SERVEIXEN PER FER LES COMPUTACIONS DE MANERA PRECISA, JA QUE TOT EL QUE HI HA DINS EL FRUSTUM QUEDA NORMALITZAT ENTRE 0 I 1
		// NEAR PLANE - 0.1 DISTANCIA VISIBLE MÉS PROPERA A LA CAMERA -- MÁS CERCA DE 0.1 ES RARO
		// FAR PLANE - DISTANCIA VISIBLE MÉS LLUNYANA A LA CÀMERA
		light_camera->setPerspective(light->cone_angle, 1.0, 0.1, light->max_distance);
		//DEFINIMOS LA POSICIÓN, DIRECCIÓN Y UP DE LA CAMARA
		// CREAMOS UNA CAMARA QUE ESTÉ EN LA POSICIÓN DE LA LIGHT Y MIRE HACIA DELANTE TENIENDO EN CUENTA SU MODEL
		// COGEMOS EL VECTOR DE LA LIGHT QUE MIRA HACIA ARRIBA (UP CAMERA)-> LO QUE ESTAMOS HACIENDO ES MULTIPLICAR EL VECTOR
		// POR LA MATRIZ PERO OMITIENDO LA TRASLACIÓN. SOLO "ROTA" EL VECTOR, NO LO LLEGA A TRASLADAR
		light_camera->lookAt(light->model.getTranslation(), light->model * Vector3(0, 0, -1), light->model.rotateVector(Vector3(0, 1, 0)));
	}
	else if (light->light_type == LightEntity::eTypeOfLight::DIRECTIONAL) {
		// AGAFEM LA MATEIXA Z DE LA LLUM I TRASLLADEM LA X I LA Y EN FUNCIÓ DE LA CÀMERA
		vec3 light_cam_pos = vec3(view_camera->eye.x, view_camera->eye.y, view_camera->eye.z);
		Application* app = Application::instance;

		float halfarea = light->area_size / 2;
		float aspect = Application::instance->window_width / (float)Application::instance->window_height;
		light_camera->setOrthographic(-halfarea, halfarea, -halfarea * aspect, halfarea * aspect, 0.1, light->max_distance);
		//light_camera->setOrthographic(-app->window_width/2, app->window_width/2, -app->window_height/2, app->window_height/2, 0.01, 1000);

		//light->model.setTranslation(light_cam_pos.x, light_cam_pos.y, light_cam_pos.z);
		light_camera->lookAt(light->model.getTranslation(), light->model.rotateVector(Vector3(0, 0, -1)), light->model.rotateVector(Vector3(0, 1, 0)));
		//light_camera->lookAt(light_cam_pos, light->model.rotateVector(Vector3(0, 0, -1)), light->model.rotateVector(Vector3(0, 1, 0)));
	}
	// EMPEZAMOS A PINTAR TODA LA ESCENA EN LA TEXTURA
	// PRIMERO BORRAMOS TODA LA ESCENA
	// BORRAMOS SOLO EL BUFFER DE DEPTH, YA QUE EL DE COLOR NO NOS INTERESA PARA NADA
	// SI NO LO BORRASEMOS NOS QUUEDARÍA UN GHOSTING DE LO QUE HABÍAMOS PINTADO ANTERIORMENTE
	// ESTE ENABLE NOS PONE ESTA CÁMARA COMO CURRENT, PERO ESO REALMENTE NO NOS INTERESA, NOS INTERESA EL TEMA DEL FRUSTUM
	light_camera->enable();
	glClear(GL_DEPTH_BUFFER_BIT);


	// PINTAMOS TODOS LOS RENDERCALLS
	for (int i = 0; i < render_calls.size(); i++) {
		RenderCall& rc = render_calls[i];
		// SI EL MATERIAL ES TRANSPARENTE NO LO PONEMOS EN EL SHADOWMAP YA QUE NO CASTEA SOMBRAS
		if (rc.material->alpha_mode == eAlphaMode::BLEND)
			continue;
		if (light_camera->testBoxInFrustum(rc.world_bounding.center, rc.world_bounding.halfsize))
			renderFlatMesh(rc.model, rc.mesh, rc.material, light_camera);
	}

	// VOLVEMOS A DEJAR EL SISTEMA COMO NOS LO ENCONTRAMOS AL PRINCIPIO PARA NO LIARLA
	// DEJAMOS DE PINTAR EN EL FBO
	light->fbo->unbind();

	// VOLVEMOS A ACTIVAR LA CÁMARA ANTERIOR
	view_camera->enable();

	//// Guardamos la camara anterior para no perderla
	//Camera* view_camera = Camera::current;

	//for (int i = 0; i < lights.size(); i++) {
	//	LightEntity* light = lights[i];

	//	if (light->light_type != LightEntity::eTypeOfLight::SPOT && light->light_type != LightEntity::eTypeOfLight::DIRECTIONAL)
	//		continue;

	//	// EN LUGAR DE USAR MUCHAS TEXTURAS DEBERÍAMOS USAR UN ATLAS Y UNA FUNCIÓN QUE ASIGNE UN TROZO
	//	// DE ESA TEXTURA A CADA LUZ, SINO PARA MUCHAS LUCES SON MUCHAS TEXTURAS Y ES MUY COSTOSO.
	//	//if (!light->cast_shadows) {
	//	//	// SI TENEMOS UNA LUZ CREADA QUE YA NO CASTEA SOMBRAS LO BORRAMOS DE MEMORIA
	//	//	if (light->fbo) {
	//	//		// EN REALIDAD ESTE DELETE YA SE ENCARGA DE LIMPIAR LAS TEXTURAS QUE TENGA DENTRO
	//	//		delete light->fbo;
	//	//		light->fbo = NULL;
	//	//		light->shadowmap = NULL;
	//	//	}
	//		//continue;
	////}

	//	if (fbo) {
	//		shadowmap = new Texture();
	//		fbo = new FBO();
	//		// We only need to store the depth buffer
	//		// Create a 1024x1024 depth texture
	//		// TEXTURA QUE TIENE UN COMPONENTE DE COLOR CON LA DISTANCIA DE CADA PIXEL
	//		// MEJOR PARA LA GPU SI ES POTENCIA DE 2

	//		fbo->setDepthOnly(1024 * lights.size() / 2, 1024 * lights.size() / 2);
	//		// SACAMOS LA TEXTURA DEL FBO Y LO GUARDAMOS EN UNA TEXTURA A PARTE
	//		shadowmap = fbo->depth_texture;
	//		// ACTIVAMOS EL FBO -- A PARTIR DE AHORA YA NO EMPEZAMOS A PINTAR EN LA PANTALLA SINO EN LA TEXUTRA
	//		fbo->bind();
	//	}
	//	if (!light->light_camera)
	//		light->light_camera = new Camera();

	//	Camera* light_camera = light->light_camera;


	//	if (light->light_type == LightEntity::eTypeOfLight::SPOT) {
	//		// DEFINIMOS LA PERSPECTIVA DE LA CAMARA (primero para la spot)
	//		// FOV -  ES EL ANGULO DE VISTA DE LA CAMARA, EN ESTE CASO EL CONE ANGLE DEL SPOTLIGHT
	//		// ASPECT-1 POR QUE LA TEXTURA ES CUADRADA
	//		// NEAR I FAR ENS SERVEIXEN PER FER LES COMPUTACIONS DE MANERA PRECISA, JA QUE TOT EL QUE HI HA DINS EL FRUSTUM QUEDA NORMALITZAT ENTRE 0 I 1
	//		// NEAR PLANE - 0.1 DISTANCIA VISIBLE MÉS PROPERA A LA CAMERA -- MÁS CERCA DE 0.1 ES RARO
	//		// FAR PLANE - DISTANCIA VISIBLE MÉS LLUNYANA A LA CÀMERA
	//		light_camera->setPerspective(light->cone_angle, 1.0, 0.1, light->max_distance);
	//		//DEFINIMOS LA POSICIÓN, DIRECCIÓN Y UP DE LA CAMARA
	//		// CREAMOS UNA CAMARA QUE ESTÉ EN LA POSICIÓN DE LA LIGHT Y MIRE HACIA DELANTE TENIENDO EN CUENTA SU MODEL
	//		// COGEMOS EL VECTOR DE LA LIGHT QUE MIRA HACIA ARRIBA (UP CAMERA)-> LO QUE ESTAMOS HACIENDO ES MULTIPLICAR EL VECTOR
	//		// POR LA MATRIZ PERO OMITIENDO LA TRASLACIÓN. SOLO "ROTA" EL VECTOR, NO LO LLEGA A TRASLADAR
	//		light_camera->lookAt(light->model.getTranslation(), light->model * Vector3(0, 0, -1), light->model.rotateVector(Vector3(0, 1, 0)));
	//	}
	//	else if (light->light_type == LightEntity::eTypeOfLight::DIRECTIONAL) {
	//		// AGAFEM LA MATEIXA Z DE LA LLUM I TRASLLADEM LA X I LA Y EN FUNCIÓ DE LA CÀMERA
	//		vec3 light_cam_pos = vec3(view_camera->eye.x, view_camera->eye.y, view_camera->eye.z);
	//		Application* app = Application::instance;

	//		float halfarea = light->area_size / 2;
	//		float aspect = Application::instance->window_width / (float)Application::instance->window_height;
	//		light_camera->setOrthographic(-halfarea, halfarea, -halfarea * aspect, halfarea * aspect, 0.1, light->max_distance);
	//		//light_camera->setOrthographic(-app->window_width/2, app->window_width/2, -app->window_height/2, app->window_height/2, 0.01, 1000);

	//		//light->model.setTranslation(light_cam_pos.x, light_cam_pos.y, light_cam_pos.z);
	//		light_camera->lookAt(light->model.getTranslation(), light->model.rotateVector(Vector3(0, 0, 1)), light->model.rotateVector(Vector3(0, 1, 0)));
	//		//light_camera->lookAt(light_cam_pos, light->model.rotateVector(Vector3(0, 0, -1)), light->model.rotateVector(Vector3(0, 1, 0)));
	//	}
	//	// EMPEZAMOS A PINTAR TODA LA ESCENA EN LA TEXTURA
	//	// PRIMERO BORRAMOS TODA LA ESCENA
	//	// BORRAMOS SOLO EL BUFFER DE DEPTH, YA QUE EL DE COLOR NO NOS INTERESA PARA NADA
	//	// SI NO LO BORRASEMOS NOS QUUEDARÍA UN GHOSTING DE LO QUE HABÍAMOS PINTADO ANTERIORMENTE
	//	// ESTE ENABLE NOS PONE ESTA CÁMARA COMO CURRENT, PERO ESO REALMENTE NO NOS INTERESA, NOS INTERESA EL TEMA DEL FRUSTUM
	//	light_camera->enable();
	//	glClear(GL_DEPTH_BUFFER_BIT);

	//	// PINTAMOS TODOS LOS RENDERCALLS
	//	for (int i = 0; i < render_calls.size(); i++) {
	//		RenderCall& rc = render_calls[i];
	//		// SI EL MATERIAL ES TRANSPARENTE NO LO PONEMOS EN EL SHADOWMAP YA QUE NO CASTEA SOMBRAS
	//		if (rc.material->alpha_mode == eAlphaMode::BLEND)
	//			continue;

	//		renderFlatMesh(rc.model, rc.mesh, rc.material, light_camera);
	//	}
	//}

	//// VOLVEMOS A DEJAR EL SISTEMA COMO NOS LO ENCONTRAMOS AL PRINCIPIO PARA NO LIARLA
	//// DEJAMOS DE PINTAR EN EL FBO
	//fbo->unbind();

	//// VOLVEMOS A ACTIVAR LA CÁMARA ANTERIOR
	//view_camera->enable();
}

//void GTR::Renderer::assignShadowmap(LightEntity* light)
//{
//
//}

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