#include "BaseEntity.h"



BaseEntity::BaseEntity()
{

	
	this->visible = true;
	this->type = BASE_NODE;
}

void BaseEntity::setPosition(int x, int y, int z) {
	this->model.translate(x, y, z);
}

BaseEntity::~BaseEntity()
{
}
