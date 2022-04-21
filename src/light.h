#pragma once
#include "utils.h"
#include "scene.h"
#include <string>


class Light {
public: 
	enum eTypeOfLight {
		NONE,
		POINT,
		SPOT,
		DIRECTIONAL
	};

	std::string name;
	bool visible;

	// ME ESTOY LIANDO DE SI LAS VARIABLES VAN AQUÍ O EN SCENE
	Matrix44 model;
	int type;
	float intensity;
	vec3 color;
	// PARA LA ATENUACIÓN, HAY QUE TENERLO EN CUENTA EN EL SHADER
	float max_distance;
	float cone_angle;
	float cone_exp;
	float area_size;

	Light();
	virtual ~Light();
	static Light* Get(const char* filename);
	void registerLight(std::string name);
	void renderInMenu();
};

class PointLight : public Light {
	PointLight();
	virtual ~PointLight();
};

class SpotLight : public Light {
	SpotLight();
	virtual ~SpotLight();
};

class DirectionalLight : public Light {
	DirectionalLight();
	virtual ~DirectionalLight();
};



