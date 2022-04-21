#ifndef SCENE_H
#define SCENE_H

#include "framework.h"
#include "camera.h"
#include "material.h"
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

	enum eLightType {
		DIRECTIONAL,
		POINT,
		SPOTLIGHT
	};

	class Scene;
	class Prefab;
	class Light;
	class Node;

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
		// VIRTUAL NOS INDICA QUE ESTA FUNCI�N CAMBIAR� DEPENDIENDO DE LA HERENCIA
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
	//	Vector3 color;
	//	float intensity;
	//	// PARA LA ATENUACI�N -- HAY QUE TENERLO EN CUENTA EN EL SHADER
	//	// ES MEJOR UNA ATENUACI�N QUADR�TICA YA QUE ES M�S NATURAL QUE UNA LINEAL POR LA FORMA DE LA CURVA
	//	float max_distance;

	//	LightEntity();
	//	virtual void renderInMenu();
	//	virtual void configure(cJSON* json);
	//};

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

		void clear();
		void addEntity(BaseEntity* entity);

		bool load(const char* filename);
		BaseEntity* createEntity(std::string type);

	};

};

#endif