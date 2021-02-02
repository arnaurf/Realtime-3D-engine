#pragma once


#ifndef SCENE
#define SCENE

#include "framework.h"
#include "BaseEntity.h"
#include "Light.h"
#include "sphericalharmonics.h"



class Scene
{
	private:
		static Scene* instance;

		Scene();
		virtual ~Scene();

	public:
		static Scene* getInstance() {
			if (instance == NULL) {
				instance = new Scene();
			}
			return instance;
		}

		SphericalHarmonics sh1;
		Vector3 shposition;


public:
	std::vector<BaseEntity*> entities;
	std::vector<Light*> lights;
	float ambient_light;
	Vector4 background;

	void addEntity(BaseEntity* be);
	void addLight(Light* l);
	void createFloor(int size);
	//MY FUNCTIONS

};

#endif // !1