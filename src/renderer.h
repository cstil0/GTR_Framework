#pragma once
#include "prefab.h"
#include <string>


//forward declarations
class Camera;

namespace GTR {

	class Prefab;
	class Material;
	
	class RenderCall {
	public:
		Mesh* mesh;
		Material* material;
		Matrix44 model;
		// A LOS QUE SEAN OPACOS LES SUMAMOS UN FACTOR MUY GRANDE PARA QUE SE QUEDEN AL FINAL
		float distance_to_camera;
		BoundingBox world_bounding;


		RenderCall() {}
		virtual ~RenderCall() {}
	};

	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{

	public:
		// Save only the visible nodes sorted by distance to the camera
		// NOT SAVING A POINTER SINCE THEN IT CANNOT BE SORTED CORRECTLY
		std::vector<RenderCall> render_calls;
		std::vector<LightEntity*> lights;

		FBO* fbo;
		Texture* shadowmap;

		bool show_shadowmap;
		int shadowmap2show;

		Renderer();

		//std::vector<LightEntity*> lights; // PARA TENER PRECALCULADAS LAS LUCES Y NO TENER QUE HACERLO EN CADA RENDER, PERO NO HE ENTENDIDO POR QUÉ

		//add here your functions
		void renderScene_RenderCalls(GTR::Scene* scene, Camera* camera);

		void showShadowmap(LightEntity* light);

		//renders several elements of the scene
		void renderScene(GTR::Scene* scene, Camera* camera);
	
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

		void renderFlatMesh(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
		void generateShadowmap(LightEntity* light);
		void assignShadowmap(LightEntity* light);

		// Functions to manage the rendercalls vector
		void createRenderCalls(GTR::Scene* scene, Camera* camera);
		void sortRenderCalls();
		void addRenderCall_node(GTR::Scene* scene, Camera* camera, Node* node, Matrix44 curr_model, Matrix44 parent_model);
		//void addRenderCall_light(LightEntity* node);

		static bool compare_distances(const RenderCall rc1, const RenderCall rc2) { return (rc1.distance_to_camera < rc2.distance_to_camera); }
	
		//void generateShadowmap(LightEntity* light);
	};

	Texture* CubemapFromHDRE(const char* filename);

};