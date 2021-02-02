#pragma once

#ifndef BASEENTITY
#define BASEENTITY

#include "framework.h"

enum {
	BASE_NODE,
	PREFAB,
	LIGHT
};

class BaseEntity
{
public:
	unsigned int id;
	Matrix44 model;
	bool visible;
	char type;
	bool hasMoved;

	BaseEntity();
	void setPosition(int x, int y, int z);
	virtual ~BaseEntity();
};

#endif // !BASEENTITY
