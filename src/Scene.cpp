#include "Scene.h"
#include "prefab.h"
#include "mesh.h"
#include "PrefabEntity.h"

Scene* Scene::instance = NULL;


Scene::Scene()
{
}

void Scene::addEntity(BaseEntity* be)
{
	this->entities.push_back(be);
}

void Scene::addLight(Light* l) {
	this->lights.push_back(l);
}

void Scene::createFloor(int size) {
	GTR::Prefab* prefab_floor = new GTR::Prefab();
	GTR::Node aux = GTR::Node();
	Mesh* floorPlane = new Mesh(); floorPlane->createPlane(size);
	GTR::Material* mat = new GTR::Material();
	mat->color_texture = Texture::Get("data/textures/floor/Mud_Rocks_001_Color.tga");
	mat->normal_texture = Texture::Get("data/textures/floor/Mud_Rocks_001_normal.tga");
	mat->texture_rep = 10;
	mat->metallic_factor = 1;
	mat->roughness_factor = 1;
	mat->color = Vector4(1, 1, 1, 1);
	aux.material = mat;
	aux.mesh = floorPlane;
	Matrix44 model;
	aux.model = model;
	prefab_floor->root = aux;

	PrefabEntity* floor = new PrefabEntity(prefab_floor, model);

	addEntity(floor);
}

Scene::~Scene()
{
}
