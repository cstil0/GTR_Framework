#ifndef SCENE_H
#define SCENE_H

#include "framework.h"
#include "camera.h"
#include <string>

//forward declaration
class cJSON; 


//our namespace
namespace GTR {



	enum eEntityType {
		NONE = 0,
		PREFAB = 1,
		LIGHT = 2,
		CAMERA = 3,
		REFLECTION_PROBE = 4,
		DECALL = 5
	};

	class Scene;
	class Prefab;
	class Light;

	//represents one element of the scene (could be lights, prefabs, cameras, etc)
	class BaseEntity
	{
	public:
		Scene* scene;
		std::string name;
		eEntityType entity_type;
		Matrix44 model;
		bool visible;
		BaseEntity() { entity_type = NONE; visible = true; }
		virtual ~BaseEntity() {}
		virtual void renderInMenu();
		virtual void configure(cJSON* json) {}
	};

	//represents one prefab in the scene
	class PrefabEntity : public GTR::BaseEntity
	{
	public:
		std::string filename;
		Prefab* prefab;
		
		PrefabEntity();
		virtual void renderInMenu();
		virtual void configure(cJSON* json);
	};

	//class LightEntity : public GTR::BaseEntity {
	//public:
	//	std::string filename;
	//	Light* light;

	//	LightEntity();
	//	virtual void renderInMenu();
	//	virtual void configure(cJSON* json);
	//};

	class RenderCall {
	public:
		Mesh* mesh;
		Material* material;
		Matrix44 model;
		int entity_type;
		float distance_to_camera;

		RenderCall() {}
		virtual ~RenderCall() {}
	};

	//contains all entities of the scene
	class Scene
	{
	public:
		static Scene* instance;

		Vector3 background_color;
		Vector3 ambient_light;
		Camera main_camera;

		Scene();

		std::string filename;
		// Esto es un contenedor --> esto tiene objetos de la clase base de entity, pero podemos estar guardando
		// cualquier objeto hijo de esta clase. Para saber de qu� tipo es cada uno, tenemos un enum dentro de la clase
		// que nos lo chiva y de esta forma podemos hacer un cast y acceder a propiedades espec�ficas de los hijos
		std::vector<BaseEntity*> entities;
		// Save only the visible nodes sorted by distance to the camera
		std::vector<RenderCall*> render_calls;

		void clear();
		void addEntity(BaseEntity* entity);

		bool load(const char* filename);
		BaseEntity* createEntity(std::string type);
		void createRenderCalls();
		void sortRenderCalls();
	};

};

#endif