#pragma once

#ifndef PREFABENTITY
#define PREFABENTITY


#include "framework.h"
#include "BaseEntity.h"
#include "prefab.h"

#include "shader.h"


class PrefabEntity : public BaseEntity
{
private:
	GTR::Prefab* prefab;
public:
	PrefabEntity();
	PrefabEntity(GTR::Prefab* p, Matrix44 model);
	void setPrefab(GTR::Prefab* p);
	GTR::Prefab* getPrefab();
	void setPos(Vector3 position);
	void renderinMenu();
	virtual ~PrefabEntity();
};

#endif