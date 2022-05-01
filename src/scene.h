#ifndef SCENE_H
#define SCENE_H

#include "framework.h"
#include "camera.h"
#include "material.h"
#include <string>

//forward declaration
class cJSON; 
// EXISTE ESTA CLASE PERO NO TE DIGO COMO ES POR AHORA
// COMO ES UN PUNTERO NO HACE FALTA INCLUIR LA CLASE ENTERA, SOLO DECIR QUE EXISTE PERO NO COMO ES
// LO NORMAL ES QUE EN EL HEADER, SI SOLO USAMOS UN PUNTERO HACEMOS FORWARD DECLARATION
// EN ESTE CASO EL HEADER NO LO NECESITA, A NO SER QUE NECESITEMOS GUARDAR ESPACIO EN MEMORIA PARA ESE OBJETO
class FBO;
class Texture;


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
	//class Light;
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
		// VIRTUAL NOS INDICA QUE ESTA FUNCIÓN CAMBIARÁ DEPENDIENDO DE LA HERENCIA
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

	class LightEntity : public GTR::BaseEntity {
	public:
		enum eTypeOfLight {
			NONE,
			POINT,
			SPOT,
			DIRECTIONAL
		};

		std::string filename;
		//Light* light;
		Vector3 color;
		float intensity;
		int light_type;
		bool cast_shadows;
		float shadow_bias;

		FBO* fbo;
		Texture* shadowmap;
		Camera* light_camera;

		// PARA LA ATENUACIÓN -- HAY QUE TENERLO EN CUENTA EN EL SHADER
		// ES MEJOR UNA ATENUACIÓN QUADRÁTICA YA QUE ES MÁS NATURAL QUE UNA LINEAL POR LA FORMA DE LA CURVA
		float max_distance;
		float cone_angle;
		float cone_exp;
		float area_size;

		// SON PUNTEROS POR QUE SI UNA LUZ NO CREA SOMBRAS ASI NO PERDEMOS ESPACIO EN MEMORIA
		//FBO* fbo;
		//bool cast_shadows;

		LightEntity();
		virtual void renderInMenu();
		virtual void configure(cJSON* json);
	};

	//contains all entities of the scene
	class Scene
	{
	public:
		enum eRenderPipeline {
			SINGLEPASS,
			MULTIPASS
		};

		static Scene* instance;

		Vector3 background_color;
		Vector3 ambient_light;
		Camera main_camera;

		int typeOfRender;

		Scene();

		std::string filename;
		// Esto es un contenedor --> esto tiene objetos de la clase base de entity, pero podemos estar guardando
		// cualquier objeto hijo de esta clase. Para saber de qué tipo es cada uno, tenemos un enum dentro de la clase
		// que nos lo chiva y de esta forma podemos hacer un cast y acceder a propiedades específicas de los hijos
		std::vector<BaseEntity*> entities;

		void clear();
		void addEntity(BaseEntity* entity);

		bool load(const char* filename);
		BaseEntity* createEntity(std::string type);

	};

};

#endif