#pragma once

#include "framework.h"
#include "BaseEntity.h"
#include "fbo.h"
#include "camera.h"
#include "utils.h"

enum LightType{
	OMNI,
	SPOT,
	DIRECTIONAL
};

class Light : public BaseEntity
{
private:
	Vector3 color;
	float intensity;
	LightType light_type;
	float spotCosineAngle;
	float spotExponent;
	float yaw;
	float pitch;
	float max_dist;

public:
	float cfar;
	float cnear;
	float frustrum;
	
	Camera* camera;
	Light(Vector3 colour, Vector3 position, Vector2 rotation, LightType type, float intensity, int fbo_w, int fbo_h);
	void setColor(Vector3 c);
	Vector3 getColor();
	LightType getType();
	void setIntensity(float i);
	float getIntensity();
	void setDirection(Vector3 center, Vector3 up);
	Camera* getCamera();
	void setNearFar(float near_p, float far_p);
	void setBias(float b);
	void setSpotAngle(float a);
	float getSpotAngle();
	void setSpotExponent(float exp);
	float getSpotExponent();
	void setMaxDist(float md);
	float getMaxDist();

	void renderinMenu();
	void Light::renderSphere(Camera* camera);

	FBO* shadow_fbo;
	float shadow_bias;
	virtual ~Light();
};

