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

	Matrix44 model;
	int type;
	float intensity;
	vec3 color;
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



