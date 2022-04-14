#include "light.h"

Light::Light(){
	intensity = 1.0f;
	type = eTypeOfLight::NONE;
}

Light::~Light() {

}

Light* Light::Get(const char* filename)
{
	//assert(filename);
	//std::map<std::string, Prefab*>::iterator it = sPrefabsLoaded.find(filename);
	//if (it != sPrefabsLoaded.end())
	//	return it->second;

	Light* light = nullptr;
	{
		if (!light)
			// Carreguem esfera
			//light = loadGLTF(filename);
		if (!light) {
			std::cout << "[ERROR]: Prefab not found" << std::endl;
			return NULL;
		}
	}

	std::string name = filename;
	light->registerLight(name);
	return light;
}

void Light::registerLight(std::string name)
{
	this->name = name;
	// NO SÉ QUE ES AIXÒ:)
	//sPrefabsLoaded[name] = this;
}

void Light::renderInMenu() {
#ifndef SKIP_IMGUI
	ImGui::Text("Name: %s", name.c_str()); // Edit 3 floats representing a color

	//Model edit
	ImGuiMatrix44(model, "Model");
	
	// Color edit

	//
	//int type;
	//float intensity;
	//vec3 color;
	//float max_distance;
	//float cone_angle;
	//float cone_exp;
	//float area_size;

#endif
}

PointLight::PointLight() {
	intensity = 1.0f;
	type = eTypeOfLight::POINT;
	color.set(1.0f, 1.0f, 1.0f);
	max_distance = 1.0f;
	cone_angle = 1.0f;
	cone_exp = 1.0f;
	area_size = 1.0f;
}

PointLight::~PointLight() {
}

SpotLight::SpotLight() {
	intensity = 1.0f;
	type = eTypeOfLight::SPOT;
	color.set(1.0f, 1.0f, 1.0f);
	max_distance = 1.0f;
	cone_angle = 1.0f;
	cone_exp = 1.0f;
	area_size = 1.0f;
}

SpotLight::~SpotLight() {

}

DirectionalLight::DirectionalLight() {
	intensity = 1.0f;
	type = eTypeOfLight::DIRECTIONAL;
	color.set(1.0f, 1.0f, 1.0f);
	max_distance = 1.0f;
	cone_angle = 1.0f;
	cone_exp = 1.0f;
	area_size = 1.0f;
}

DirectionalLight::~DirectionalLight() {

}
