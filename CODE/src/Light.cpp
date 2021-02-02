#include "Light.h"
#include "Scene.h"
#include "mesh.h"
#include "shader.h"

Light::Light(Vector3 colour, Vector3 position, Vector2 rotation, LightType type, float intensity, int fbo_w, int fbo_h)
{
	this->type = LIGHT;

	if (Scene::getInstance()->lights.empty())
		this->id = 0;
	else
		this->id = Scene::getInstance()->lights.back()->id + 1;

	this->visible = true;
	this->color = colour;
	this->light_type = type;
	this->intensity = intensity;

	this->model.setIdentity();
	this->setPosition(position.x, position.y, position.z);
	this->model.rotate(DEG2RAD * rotation.x, Vector3(0, 1, 0));
	this->model.rotate(DEG2RAD * rotation.y, Vector3(1, 0, 0));
	this->yaw = rotation.x;
	this->pitch = rotation.y;
	this->shadow_fbo = nullptr;
	//this->shadow_fbo->create(fbo_w, fbo_h);
	this->camera = new Camera();
	this->shadow_bias = 0.02;
	this->spotCosineAngle = 0.0;
	this->cfar = 1000;
	this->cnear = 5;
	this->max_dist = 1000;
	this->frustrum = 1000;
	this->spotExponent = 3;

}

void Light::setColor(Vector3 c) {
	this->color = c;
}

Vector3 Light::getColor() {
	return this->color;
}

LightType Light::getType() {
	return this->light_type;
}

void Light::setIntensity(float i) {
	this->intensity = i;
}

float Light::getIntensity() {
	return this->intensity;
}

void Light::setMaxDist(float md) {
	this->max_dist = md;
}
float Light::getMaxDist() {
	return this->max_dist;
}

void Light::setDirection(Vector3 center, Vector3 up) {
	Vector3 pos = this->model.getTranslation();
	this->model.lookAt(pos, center, up);
}

Camera* Light::getCamera() {
	return this->camera;
}

void Light::setNearFar(float near_p, float far_p) {
	this->camera->far_plane = far_p;
	this->camera->near_plane = near_p;
	this->cfar = far_p;
	this->cnear = near_p;
}

void Light::setBias(float b) {
	this->shadow_bias = b;
}

void Light::setSpotAngle(float a) {
	this->spotCosineAngle = a;
}

float Light::getSpotAngle() {
	return this->spotCosineAngle;
}


Light::~Light()
{
}

void Light::setSpotExponent(float exp) {
	this->spotExponent = exp;
}

float Light::getSpotExponent() {
	return this->spotExponent;
}
void Light::renderinMenu() {
#ifndef SKIP_IMGUI
	char aux[20];
	sprintf(aux, "Light %i, type: %i", this->id, this->light_type);
	//bool is_active = (*current_light == this);

	if (ImGui::TreeNode(aux)) {
		ImGui::Checkbox("Visible", &visible);

		/*
		if (ImGui::Checkbox("Selected", &is_active)) {
			*current_light = this;
		}*/
		bool changed = false;
		//ImGuiMatrix44(this->model, "Model");
		ImGui::DragFloat("Bias", &shadow_bias, 0.001);
		changed |= ImGui::SliderFloat("spotAngle", &spotCosineAngle, 0.000, 1.000);
		changed |= ImGui::SliderFloat("Spot Exponent", &this->spotExponent, 0.000, 7.0);

		changed |= ImGui::SliderFloat("Intensity", &intensity, 0.0, 10);

		bool changed2;
		changed2 |= ImGui::Combo("Type", (int*)&this->light_type, "OMNI\0SPOT\0DIRECTIONAL", 3);
		//ImGui::DragFloat("Type", &this->light_type, 1.0, 0.0, 2.0);
		
		
		ImGui::ColorEdit3("Colorr", this->color.v, 0.05f);



		float matrixTranslation[3], matrixRotation[3], matrixScale[3];
		ImGuizmo::DecomposeMatrixToComponents(this->model.m, matrixTranslation, matrixRotation, matrixScale);
		ImGui::DragFloat3("Position l", matrixTranslation, 0.5f);
		if (this->light_type == DIRECTIONAL) {
			float frontVec[3];
			frontVec[0] = this->model.frontVector().x;
			frontVec[1] = this->model.frontVector().y;
			frontVec[2] = this->model.frontVector().z;
			ImGui::DragFloat3("Light Direction", frontVec, 0.5f);
		}
		ImGui::DragFloat3("Scale l", matrixScale, 0.2f);
		ImGui::DragFloat("yaw", &this->yaw, 0.2f);
		ImGui::DragFloat("pitch", &this->pitch, 0.2f);

		//ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, this->model.m);
		this->model.setTranslation(matrixTranslation[0], matrixTranslation[1], matrixTranslation[2]);
		this->model.rotate(DEG2RAD * this->yaw, Vector3(0, 1, 0));
		this->model.rotate(DEG2RAD * this->pitch, Vector3(1, 0, 0));
		this->model.scale(matrixScale[0], matrixScale[1], matrixScale[2]);

		
		/*
		if (this->light_type == Light::SPOT) {
			ImGui::DragFloat("Cosine Cutoff", &this->spotCosineCutoff, 0.005f);
			ImGui::DragFloat("Exponent", &this->spotExponent, 0.05f);
		}
		ImGui::DragFloat("bias", &this->u_shadow_bias, 0.001);
		ImGui::DragFloat("Max distance", &this->max_distance, 5.0);
		*/
		
		ImGui::DragFloat("near", &this->cnear, 0.5);
		ImGui::DragFloat("far", &this->cfar, 0.5);
		ImGui::DragFloat("frustrum", &this->frustrum, 1);
		
		ImGui::TreePop();
	}

#endif
}


void Light::renderSphere(Camera* camera) {

	Mesh* sphere = Mesh::Get("data/meshes/sphere.obj");
	Shader* shader = NULL;
	//Light* l = (Light*)(Scene::instance->entities.back());
	shader = Shader::Get("flat");
	Matrix44 m = this->model;
	m.scale(4, 4, 4);
	if (!shader)
		return;
	shader->enable();
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", m);
	shader->setUniform("u_color", Vector4(color.x, color.y, color.z, 1.0));

	sphere->render(GL_TRIANGLES);

	shader->disable();
}
