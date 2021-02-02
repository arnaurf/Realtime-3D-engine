#include "PrefabEntity.h"

#include "Scene.h"

PrefabEntity::PrefabEntity()
{
	this->type = PREFAB;
	this->visible = true;
	this->model.setIdentity();
	if (Scene::getInstance()->entities.empty())
		this->id = 0;
	else
		this->id = Scene::getInstance()->entities.back()->id + 1;

}

PrefabEntity::PrefabEntity(GTR::Prefab *p, Matrix44 model)
{
	this->type = PREFAB;
	this->visible = true;
	this->model.setIdentity();
	this->model = model;
	if (Scene::getInstance()->entities.empty())
		this->id = 0;
	else
		this->id = Scene::getInstance()->entities.back()->id + 1;

	setPrefab(p);
}

void PrefabEntity::setPrefab(GTR::Prefab* p)
{
	this->prefab = p;
}

GTR::Prefab* PrefabEntity::getPrefab() {
	return this->prefab;
}

void PrefabEntity::setPos(Vector3 position) {
	this->setPosition(position.x, position.y, position.z);
}

PrefabEntity::~PrefabEntity()
{
}

void PrefabEntity::renderinMenu() {
#ifndef SKIP_IMGUI
	char aux[20];
	sprintf(aux, "Prefab %i", this->id);
	if (ImGui::TreeNode(aux)) {
		float matrixTranslation[3], matrixRotation[3], matrixScale[3];
		ImGuizmo::DecomposeMatrixToComponents(this->model.m, matrixTranslation, matrixRotation, matrixScale);
		ImGui::DragFloat3("Position l", matrixTranslation, 0.5f);
		ImGui::DragFloat3("Rotation l", matrixRotation, 0.5f);
		ImGui::DragFloat3("Scale l", matrixScale, 0.2f);
		ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, this->model.m);

		this->prefab->root.renderInMenu();
		ImGui::TreePop();
	}

#endif
}