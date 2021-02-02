#include "renderer.h"

#include "camera.h"
#include "shader.h"
#include "mesh.h"
#include "texture.h"
#include "prefab.h"
#include "material.h"
#include "utils.h"
#include "extra/hdre.h"


bool show_probes = false;



using namespace GTR;

Renderer::Renderer() {
	deferred_fbo = NULL;
	complete_fbo = NULL;
	ssao_fbo = NULL;
	irr_fbo = NULL;
	reflection_fbo = NULL;
	volumetric_fbo = new FBO();
	volumetric_fbo->create(Application::instance->window_width / 4, Application::instance->window_height / 4, 1, GL_RGBA);
	probes_texture = NULL;
	show_properties = false;
	degamma = true;
	pbr = true;
	show_ssao = false;
	apply_ssao = true;
	apply_volumetric = false;
	computeAmbientOcclusion = true;
	apply_environmentReflections = true;
	show_reflectionProbes = false;
	add_decal = false;
	apply_tonemapper = false;
	apply_glow = false;
	SHinterpolation = false;
	show_irradiance = false;
	noise = Texture::Get("data/textures/noise.png");

	points = GTR::generateSpherePoints(64, 1.0, true);

	skybox = CubemapFromHDRE("data/textures/panorama.hdre");
	decal_depth_texture = NULL;
	decal = Texture::Get("data/textures/crack.png");
	cube = new Mesh();
	cube->createCube();

	generateReflectionProbes();

	u_scale = 1.0f;
	u_average_lum = 1.0f;
	u_lumwhite2 = 1.0f;
	u_igamma = 2.2f;
}


void Renderer::renderInMenu() {
#ifndef SKIP_IMGUI
	ImGui::Checkbox("Show FBO's", &show_properties);
	ImGui::Checkbox("Degamma", &degamma);
	ImGui::Checkbox("PBR", &pbr);
	ImGui::Checkbox("Add decal", &add_decal);
	ImGui::Checkbox("Apply Volumetric in Directional", &apply_volumetric);

	if (ImGui::TreeNode("SSAO+")) {
		ImGui::Checkbox("Show SSAO", &show_ssao);
		ImGui::Checkbox("Apply SSAO", &apply_ssao);
		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Reflections")) {
		if (ImGui::Button("Compute Reflections")) {
			computeReflections(Scene::getInstance());
		}
		ImGui::Checkbox("Show Reflection Probes", &show_reflectionProbes);
		ImGui::Checkbox("Apply Environment Reflection", &apply_environmentReflections);
		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Irradiance")) {
		if (ImGui::Button("Compute Irradiance")) {
			computeIrradiance(Scene::getInstance());
		}
		if (ImGui::Button("Save irradiance")) {
			saveIrradiance();
			//saveIrradiance(Scene::getInstance()->sh1, Scene::getInstance()->shpos)
		}
		if (ImGui::Button("Read irradiance")) {
			readIrradiance();
			//saveIrradiance(Scene::getInstance()->sh1, Scene::getInstance()->shpos)
		}

		ImGui::Checkbox("Show Irradiance texture", &show_irradiance);
		ImGui::Checkbox("Show Probes", &show_probes);
		ImGui::Checkbox("SHinterpolation", &SHinterpolation);
		ImGui::Checkbox("Apply glow", &apply_glow);
		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Tonemapper")) {
		ImGui::Checkbox("Apply Tonemapper", &apply_tonemapper);
		ImGui::SliderFloat("Tonemapper Scale", &u_scale, 0.0f, 5.0f);
		ImGui::SliderFloat("Tonemapper Average Lum", &u_average_lum, 0.0f, 5.0f);
		ImGui::SliderFloat("Tonemapper Lumwhite", &u_lumwhite2, 0.0f, 5.0f);
		ImGui::SliderFloat("Tonemapper Gamma", &u_igamma, 0.0f, 5.0f);
		ImGui::TreePop();
	}

#endif
}

//renders all the prefab
void Renderer::renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera)
{
	//assign the model to the root node
	renderNode(model, &prefab->root, camera);
}

//renders a node of the prefab and its children
void Renderer::renderNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera)
{
	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true) * prefab_model;

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(node_model,node->mesh->box);
		
		//if bounding box is inside the camera frustum then the object is probably visible
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )
		{
			//render node mesh
			renderMeshWithMaterial( node_model, node->mesh, node->material, camera );
			//node->mesh->renderBounding(node_model, true);
		}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		renderNode(prefab_model, node->children[i], camera);
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;
	Texture* texture = NULL;

	texture = material->color_texture;
	//texture = material->emissive_texture;
	//texture = material->metallic_roughness_texture;
	//texture = material->normal_texture;
	//texture = material->occlusion_texture;
	if (texture == NULL)
		texture = Texture::getWhiteTexture(); //a 1x1 white texture


	//select if render both sides of the triangles
	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	//chose a shader
	shader = Shader::Get("forward");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	glDepthFunc(GL_LEQUAL);
	//glBlendFunc(GL_SRC_ALPHA, GL_ONE); //set blending mode to additive
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	float ambient = Scene::getInstance()->ambient_light;
	int it = 0;
	for (int i = 0; i < Scene::getInstance()->lights.size(); i++) {
		Light* light = Scene::getInstance()->lights[i];
		if (light->visible) {
			//first one do not use blending
			if (it == 0) {
				glDisable(GL_BLEND);
				shader->setUniform("u_ambient_light", ambient);			//aquest es canvia si volem afegir ambient
				if (material->emissive_texture) {
					shader->setUniform("u_emissive_factor", (float)material->emissive_factor.length());
					shader->setUniform("u_emissive_texture", material->emissive_texture, 3);
				}
				else if (material->emissive_factor.length() > 0) {
					shader->setUniform("u_emissive_factor", (float)material->emissive_factor.length());
					shader->setUniform("u_emissive_texture", Texture::getWhiteTexture(), 3);
				}else{
					shader->setUniform("u_emissive_factor", 0.0f);
					shader->setUniform("u_emissive_texture", Texture::getBlackTexture(), 3);
				}
			}
			else {
				glEnable(GL_BLEND);
				shader->setUniform("u_ambient_light", 0.0f);
				shader->setUniform("u_emissive_texture", Texture::getBlackTexture(), 3);
				shader->setUniform("u_emissive_factor", 0.0f);
			}
			/*
			if (i == 0 && material->alpha_mode != GTR::AlphaMode::BLEND) {
				glDisable(GL_BLEND);
				shader->setUniform("u_ambient_light", ambient);			//aquest es canvia si volem afegir ambient
				if (material->emissive_texture)
					shader->setUniform("u_emissive_texture", material->emissive_texture, 3);
				else
					shader->setUniform("u_emissive_texture", Texture::getBlackTexture(), 3);
			}
			else if (i == 0 && material->alpha_mode == GTR::AlphaMode::BLEND) {
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				shader->setUniform("u_ambient_light", ambient);			//aquest es canvia si volem afegir ambient
				if (material->emissive_texture)
					shader->setUniform("u_emissive_texture", material->emissive_texture, 3);
				else
					shader->setUniform("u_emissive_texture", Texture::getBlackTexture(), 3);
			}
			else {
				glEnable(GL_BLEND);
				shader->setUniform("u_ambient_light", 0.0f);
				shader->setUniform("u_emissive_texture", Texture::getBlackTexture(), 3);
			}*/




			//codigo relacionado con shadowmap
			if (light->shadow_fbo) {
				Texture* shadowmap = Scene::getInstance()->lights[i]->shadow_fbo->depth_texture;

				shader->setUniform("shadowmap", shadowmap, 8);
				shader->setUniform("gShadowMap", shadowmap, 9);
				shader->setUniform("gMapSize", Vector2(shadowmap->width, shadowmap->height));
				Matrix44 shadow_proj = light->getCamera()->viewprojection_matrix;

				shader->setUniform("u_shadow_viewproj", shadow_proj);
				shader->setUniform("u_shadow_bias", light->shadow_bias*0.1f);
			}
			assert(glGetError() == GL_NO_ERROR);
			//upload uniforms
			shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
			shader->setUniform("u_camera_position", camera->eye);
			shader->setUniform("u_model", model);

			shader->setUniform("pbr", pbr);
			shader->setUniform("degamma", degamma);			
			shader->setUniform("u_texture_rep", (float)material->texture_rep);

			shader->setUniform("u_color", material->color);
			if (texture)
				shader->setUniform("u_texture", texture, 0);

			if (material->metallic_roughness_texture)
				shader->setUniform("u_rough_metal_texture", material->metallic_roughness_texture, 1);
			else
				shader->setUniform("u_rough_metal_texture", Texture::getBlackTexture(), 1);

			



			assert(glGetError() == GL_NO_ERROR);
			//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
			shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::AlphaMode::MASK ? material->alpha_cutoff : 0);

			//pass all uniforms corresponding to each light
			shader->setUniform("u_light_color", light->getColor());

			shader->setUniform("u_light_intensity", light->getIntensity());

			if (light->getType() == DIRECTIONAL) {
				Vector3 light_direction = light->model.frontVector();
				shader->setUniform("u_light_vector", light_direction);
				shader->setUniform("u_light_type", 0.0f);
				assert(glGetError() == GL_NO_ERROR);
			}
			else {
				Vector3 light_pos = light->model.getTranslation();
				shader->setUniform("u_light_position", light_pos);
				shader->setUniform("u_light_maxdist", (float)light->getMaxDist());
				if (light->getType() == OMNI) {
					shader->setUniform("u_light_type", 1.0f);
				}
				else { //SPOT LIGHT
					shader->setUniform("u_light_type", 2.0f);
					Vector3 light_direction = light->model.frontVector();
					shader->setUniform("spotDirection", light_direction);
					shader->setUniform("spotCosineCutoff", Scene::getInstance()->lights[i]->getSpotAngle());
					shader->setUniform("spotExponent", light->getSpotExponent());
				}

			}

			mesh->render(GL_TRIANGLES);
			it++;
		}

	}

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glDepthFunc(GL_LESS);
}

void Renderer::renderForward(Scene* scene, Camera* camera) {
	
	
	
	

	//glFrontFace(GL_CW);
	glEnable(GL_DEPTH_TEST);
	for (int i = 0; i < scene->lights.size(); i++) {
		createShadowmap(scene->entities, scene->lights[i]);
	}
	glFrontFace(GL_CCW);
	//glDisable(GL_DEPTH_TEST);
	
	renderSceneForward(scene, camera);

}


void Renderer::renderSceneForward(Scene* scene, Camera* camera) {
	Scene* sc = Scene::getInstance();
	//set the clear color (the background color)

	//glClearColor(sc->background.x, sc->background.y, sc->background.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();
	
	renderSkybox(camera);

	glEnable(GL_DEPTH_TEST);
	for (int i = 0; i < scene->entities.size(); i++) {

		if (scene->entities.at(i)->type == PREFAB) {
			PrefabEntity* p = new PrefabEntity();
			p = (PrefabEntity*)scene->entities[i];
			renderPrefab(scene->entities.at(i)->model, p->getPrefab(), camera);
		}
	}
}

void Renderer::createShadowmap(std::vector<BaseEntity*> ent, Light* l) {
	if (l->getType() == DIRECTIONAL || l->getType() == SPOT) {
		Shader* shader = NULL;
		shader = Shader::Get("shadow");
		shader->enable();

		if (!l->shadow_fbo) {
			l->shadow_fbo = new FBO();
			//l->shadow_fbo->setDepthOnly(1024, 1024);
			l->shadow_fbo->create(1024, 1024, 1);
		}

		l->shadow_fbo->bind();
		//glColorMask(false, false, false, false);
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

		Camera* main_camera = Camera::current;
		Camera* cameraL = new Camera();
		Vector3 at = l->model.frontVector();
		Vector3 up = l->model.rotateVector(Vector3(0, 1, 0));
		if (l->getType() == SPOT) {
			cameraL->setPerspective(acos(l->getSpotAngle()) * RAD2DEG + 40, 1, l->cnear, l->getMaxDist());
			Vector3 pos = l->model.getTranslation();
			cameraL->lookAt(pos, pos + at, up);
		}
		else {
			cameraL->setOrthographic(-l->frustrum, l->frustrum, -l->frustrum, l->frustrum, l->cnear, l->cfar); //to change
			//Vector3 pos = Vector3(-500, 600, 500);
			//Vector3 pos = Vector3(Camera::current->eye.x, 0, Camera::current->eye.z) - (at * 100);
			Vector3 pos = Vector3(0, 0, 0) - (at * 500.0);
			//float step = l->shadow_fbo->depth_texture->height / (2 * l->frustrum);
			/*pos.x = floor(pos.x / step) * step;
			pos.y = floor(pos.y / step) * step;
			pos.z = floor(pos.z / step) * step;*/

			cameraL->lookAt(pos, pos + at, up);
		}

		l->camera = cameraL;
		l->camera->enable();
		
		for (int i = 0; i < ent.size(); i++) {
			if (ent[i]->type == PREFAB) {
				PrefabEntity* p = new PrefabEntity();
				p = (PrefabEntity*)ent[i];
				checkRendering(p, shader, l, &p->getPrefab()->root);
			}
		}

		l->shadow_fbo->unbind();
		glColorMask(true, true, true, true);
		shader->disable();
		main_camera->enable();
	}

}

void Renderer::checkRendering(PrefabEntity* p, Shader* s, Light* l, GTR::Node* n) {
	if (n->children.size() != 0) {
		for (int i = 0; i < n->children.size(); i++) {
			checkRendering(p, s, l, n->children[i]);
		}
	}
	else {
		if (n->material->alpha_mode == GTR::AlphaMode::NO_ALPHA) {
			assert(glGetError() == GL_NO_ERROR);
			s->setUniform("u_viewprojection", l->getCamera()->viewprojection_matrix);
			s->setUniform("u_model", n->getGlobalMatrix(true) * p->model);
			if(n->material->color_texture)
				s->setUniform("u_texture", n->material->color_texture, 1);
			else
				s->setUniform("u_texture", Texture::getWhiteTexture(), 1);

			assert(glGetError() == GL_NO_ERROR);
			n->mesh->render(GL_TRIANGLES);

		}
	}
}

void Renderer::renderSceneInDeferred(Scene* scene, Camera* camera) {

	//glFrontFace(GL_CW);
	for (int i = 0; i < scene->lights.size(); i++) {
		createShadowmap(scene->entities, scene->lights[i]);
		//scene->lights[i]->renderSphere(camera);
	}
	glFrontFace(GL_CCW);


	renderMeshinDeferred(scene->entities, camera);
	
}

void Renderer::renderMeshinDeferred(std::vector<BaseEntity*> entities, Camera* camera) {
	int height = Application::instance->window_height;
	int width = Application::instance->window_width;

	if (!deferred_fbo) {
		deferred_fbo = new FBO();
		deferred_fbo->create(width, height, 4, GL_RGBA, GL_HALF_FLOAT, true);
		complete_fbo = new FBO();
		complete_fbo->create(width, height, 2, GL_RGB, GL_FLOAT);
		ssao_fbo = new FBO();
		ssao_fbo->create(width, height, 1, GL_RGBA);
		aux = new FBO(); 
		aux->create(width, height, 2, GL_RGB, GL_FLOAT);
		aux2 = new FBO();
		aux2->create(width, height, 1, GL_RGB, GL_FLOAT);
	}

	deferred_fbo->bind();

	Scene* sc = Scene::getInstance();

	deferred_fbo->enableSingleBuffer(0);

	//glClearColor(0.2313, 0.1882, 0.4941, 1.0);
	glClearColor(sc->background.x, sc->background.y, sc->background.z, 1.0);
	
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	deferred_fbo->enableSingleBuffer(1);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	deferred_fbo->enableSingleBuffer(2);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	deferred_fbo->enableSingleBuffer(3);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	deferred_fbo->enableAllBuffers();

	
	
	//Render entities
	for (int i = 0; i < entities.size(); i++) {
		if (entities.at(i)->type == PREFAB) {
			PrefabEntity* p = new PrefabEntity();
			p = (PrefabEntity*)entities[i];
			renderPrefabDeferred(entities[i]->model, p->getPrefab(), camera);
		}
	}
	

	


	deferred_fbo->unbind();

		
	Matrix44 inv_viewprojection = camera->viewprojection_matrix;
	inv_viewprojection.inverse();

	//DECALS
	if (add_decal) {

		if (!decal_depth_texture) {
			decal_depth_texture = new Texture(deferred_fbo->depth_texture->width, deferred_fbo->depth_texture->height, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, false);
		}
		deferred_fbo->depth_texture->copyTo(decal_depth_texture);

		deferred_fbo->bind();
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glDepthMask(false);

		Matrix44 m, invm;
		m.setTranslation(260, 130, -200);
		m.scale(10, 80, 80);
		m.rotate(90 * DEG2RAD, Vector3(0, 0, 1));
		invm = m;
		invm.inverse();

		Shader* shader = Shader::Get("decals");
		shader->enable();
		shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
		shader->setUniform("u_inverse_viewprojection", inv_viewprojection);
		shader->setUniform("u_iRes", Vector2(1.0f / deferred_fbo->width, 1.0f / deferred_fbo->height));
		shader->setUniform("u_model", m);
		shader->setUniform("u_invmodel", invm);
		shader->setUniform("u_texture", decal, 0);
		shader->setUniform("u_depth_texture", decal_depth_texture, 1);
		shader->setUniform("u_camera_pos", camera->eye);
		
		cube->render(GL_TRIANGLES);
		shader->disable();
		glDepthMask(true);
		deferred_fbo->unbind();
		
	}

	glDisable(GL_DEPTH_TEST);

	Mesh* quad = Mesh::getQuad();

	//AMBIENT OCCLUSION
	if (computeAmbientOcclusion) {
		ssao_fbo->bind();
		Shader* shader = Shader::Get("occlusion");
		shader->enable();
		shader->setUniform("u_iRes", Vector2(1.0 / deferred_fbo->depth_texture->width, 1.0 / deferred_fbo->depth_texture->height));
		shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
		shader->setUniform("u_inverse_viewprojection", inv_viewprojection);
		shader->setUniform("u_normal_texture", deferred_fbo->color_textures[1], 0);
		shader->setUniform("u_depth_texture", deferred_fbo->depth_texture, 1);
		shader->setUniform("u_camera_pos", camera->eye);
		shader->setUniform3Array("u_points", (float*)&points[0], points.size());

		quad->render(GL_TRIANGLES);
		shader->disable();
		ssao_fbo->unbind();
	}
	
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	if (show_properties) { //Pintar Frame Buffers
		glViewport(0, height * 0.5, width * 0.5, height * 0.5);
		deferred_fbo->color_textures[0]->toViewport();

		glViewport(width * 0.5, height * 0.5, width * 0.5, height * 0.5);
		deferred_fbo->color_textures[1]->toViewport();
		glViewport(width * 0.5, 0, width * 0.5, height * 0.5);
		deferred_fbo->color_textures[2]->toViewport();
		/*Shader* s = Shader::Get("depth");
		s->enable();
		glViewport(0, 0, width * 0.5, height * 0.5);
		deferred_fbo->depth_texture->toViewport(s);
		s->disable();*/
		glViewport(0, 0, width * 0.5, height * 0.5);
		deferred_fbo->color_textures[3]->toViewport();

		glViewport(0, 0, width, height);
	}
	else if (show_irradiance && probes_texture) {

		probes_texture->toViewport();

	}
	else { //Pintar escena amb iluminació
		complete_fbo->bind();
		//glClearColor(sc->background.x, sc->background.y, sc->background.z, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);

		renderSkybox(camera);

		glClearColor(0.0f, 0.0f, 0.0f, 1.0);
		glDisable(GL_DEPTH_TEST);

		//Apliquem deferred
		Shader* shader1 = Shader::Get("deferred");
		shader1->enable();

		glDisable(GL_BLEND);

		//===================== SCENE ILUMINATION FOR SPOT AND POINT LIGHTS =========================//
		float amb = Scene::getInstance()->ambient_light;
		Texture* irr = deferred_fbo->color_textures[3];
		Scene* scene = Scene::getInstance();
		for (std::vector<Light*>::iterator it = scene->lights.begin(); it < scene->lights.end(); it++) {
			Light* light = (*it);

			if (light->visible) {
				if (light->getType() == SPOT || light->getType() == DIRECTIONAL) {
					shader1->setUniform("degamma", degamma);
					shader1->setUniform("pbr", pbr);
					shader1->setUniform("ssao", apply_ssao);
					shader1->setUniform("u_light_maxdist", light->getMaxDist());
					shader1->setUniform("u_color_texture", deferred_fbo->color_textures[0], 0);
					shader1->setUniform("u_normal_texture", deferred_fbo->color_textures[1], 1);
					shader1->setUniform("u_extra_texture", deferred_fbo->color_textures[2], 2);
					shader1->setUniform("u_irradiance_texture", irr, 3);
					irr = Texture::getBlackTexture();

					shader1->setUniform("u_depth_texture", deferred_fbo->depth_texture, 4);
					shader1->setUniform("u_ambient_texture", ssao_fbo->color_textures[0], 5);

					shader1->setUniform("u_inverse_viewprojection", inv_viewprojection);
					Vector3 light_pos = light->model.getTranslation();
					shader1->setUniform("u_light_position", light_pos);
					shader1->setUniform("u_light_color", light->getColor());
					shader1->setUniform("u_light_intensity", light->getIntensity());

					shader1->setUniform1("u_ambient_light", amb);
					amb = 0.0f;


					shader1->setUniform("u_iRes", Vector2(1.0 / deferred_fbo->width, 1.0 / deferred_fbo->height));
					shader1->setUniform("u_shadow_viewproj", light->getCamera()->viewprojection_matrix);
					shader1->setUniform("u_shadow_bias", light->shadow_bias * 0.1f);
					shader1->setUniform("u_camera_pos", camera->eye);
					if (light->shadow_fbo) {
						shader1->setUniform("shadow_map", light->shadow_fbo->depth_texture, 8);

						shader1->setUniform("gShadowMap", light->shadow_fbo->depth_texture, 8);
						shader1->setUniform("gMapSize", Vector2(light->shadow_fbo->depth_texture->width, light->shadow_fbo->depth_texture->height));
					}

					if (light->getType() == SPOT) {
						shader1->setUniform("u_light_type", (float)2.0);
						Vector3 light_direction = light->model.frontVector();
						shader1->setUniform("spotDirection", light_direction);
						shader1->setUniform("spotCosineCutoff", light->getSpotAngle());
						shader1->setUniform("spotExponent", (float)3.0);
					}
					else if (light->getType() == DIRECTIONAL) {
						shader1->setUniform("u_light_type", (float)0.0);
						Vector3 at = light->model.frontVector();
						shader1->setUniform("u_light_vector", at);

					}
					quad->render(GL_TRIANGLES);
					glEnable(GL_BLEND);
				}
			}
		}
		shader1->disable();

		//===================== SCENE ILUMINATION FOR DIRECTIONAL LIGHTS =========================//
		glEnable(GL_CULL_FACE);
		Shader* shader2 = Shader::Get("deferred_ws");
		Mesh* sphere = Mesh::Get("data/meshes/sphere.obj");
		shader2->enable();

		for (std::vector<Light*>::iterator it = scene->lights.begin(); it < scene->lights.end(); it++) {
			Light* light = (*it);

			if (light->visible) {
				if (light->getType() == OMNI) {

					shader2->setUniform("u_viewprojection", camera->viewprojection_matrix);
					Matrix44 ma;
					Vector3 pos = light->model.getTranslation();
					ma.setTranslation(pos.x, pos.y, pos.z);
					ma.scale(light->getMaxDist(), light->getMaxDist(), light->getMaxDist());

					shader2->setUniform("u_model", ma);

					shader2->setUniform("degamma", degamma);
					shader2->setUniform("pbr", pbr);

					shader2->setUniform("u_light_maxdist", light->getMaxDist());
					shader2->setUniform("u_color_texture", deferred_fbo->color_textures[0], 0);
					shader2->setUniform("u_normal_texture", deferred_fbo->color_textures[1], 1);
					shader2->setUniform("u_extra_texture", deferred_fbo->color_textures[2], 2);
					shader2->setUniform("u_depth_texture", deferred_fbo->depth_texture, 3);
					shader2->setUniform("u_ambient_texture", ssao_fbo->color_textures[0], 4);

					shader2->setUniform("u_inverse_viewprojection", inv_viewprojection);
					Vector3 light_pos = light->model.getTranslation();
					shader2->setUniform("u_light_position", light_pos);
					shader2->setUniform("u_light_color", light->getColor());
					shader2->setUniform("u_light_intensity", light->getIntensity());
					shader2->setUniform("u_ambient_light", 0.0f);

					shader2->setUniform("u_iRes", Vector2(1.0 / deferred_fbo->width, 1.0 / deferred_fbo->height));
					shader2->setUniform("u_shadow_viewproj", light->getCamera()->viewprojection_matrix);
					shader2->setUniform("u_shadow_bias", light->shadow_bias);
					shader2->setUniform("u_camera_pos", camera->eye);
					shader2->setUniform("u_light_type", (float)1.0);

					glDisable(GL_DEPTH_TEST);
					glEnable(GL_CULL_FACE);
					glFrontFace(GL_CW);
					sphere->render(GL_TRIANGLES);
					glFrontFace(GL_CCW);
					glDisable(GL_CULL_FACE);
				}
			}
		}
		shader2->disable();

		//complete_fbo->unbind();

		//glDisable(GL_CULL_FACE);
		//glDisable(GL_BLEND);
		//glDepthFunc(GL_LESS);



		//======================== POST ============================//

		//=============== REFLECTIONS ==============//
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		if (apply_environmentReflections && skybox) { 

			Mesh* quadr = Mesh::getQuad();

			Shader* shader = Shader::Get("deferred_reflections");
			shader->enable();
			shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
			shader->setUniform("u_inverse_viewprojection", inv_viewprojection);
			shader->setUniform("u_camera_pos", camera->eye);
			shader->setUniform("u_iRes", Vector2(1.0 / deferred_fbo->width, 1.0 / deferred_fbo->height));
			shader->setUniform("u_color_texture", deferred_fbo->color_textures[0], 0);
			shader->setUniform("u_normal_texture", deferred_fbo->color_textures[1], 1);
			shader->setUniform("u_extra_texture", deferred_fbo->color_textures[2], 2);
			shader->setUniform("u_depth_texture", deferred_fbo->depth_texture, 3);


			Texture* nearestTexture = skybox;

			for (int i = 0; i < reflection_probes.size(); i++) {
				sReflectionProbe* aux = reflection_probes[i];
				float dist = camera->eye.distance(reflection_probes[i]->pos);
				if (dist < 105 && aux->cubemap) {
					nearestTexture = aux->cubemap;
				}
			}

			shader->setUniform("u_environment_texture", nearestTexture, 8);

			quadr->render(GL_TRIANGLES);
			shader->disable();
		}

		//============= AMBIENT OCLUSION ===============//
		if (show_ssao) {
			//glViewport(0, 0, width, height);
			ssao_fbo->color_textures[0]->toViewport();
		}
		//else {
		//	glViewport(0, 0, width, height);

		

		//la light[0] es la directional (llum del sol)
		if (scene->lights[0] && scene->lights[0]->shadow_fbo && apply_volumetric) {
			glDisable(GL_BLEND);

			noise->bind();
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

			volumetric_fbo->bind();
			Shader* shader = Shader::Get("volumetric");
			shader->enable();
			Mesh* quadv = Mesh::getQuad();
			shader->setUniform("u_iRes", Vector2(1.0 / volumetric_fbo->width, 1.0 / volumetric_fbo->height));
			shader->setUniform("u_inverse_viewprojection", inv_viewprojection);
			shader->setUniform("u_camera_pos", camera->eye);
			shader->setUniform("u_depth_texture", deferred_fbo->depth_texture, 3);
			//Dades només de la directional light
			shader->setUniform("u_light_color", scene->lights[0]->getColor());
			Vector3 at = scene->lights[0]->model.frontVector();
			shader->setUniform("u_light_vector", at);

			shader->setUniform("u_shadow_viewproj", scene->lights[0]->getCamera()->viewprojection_matrix);
			shader->setUniform("u_shadow_bias", scene->lights[0]->shadow_bias);
			shader->setUniform("shadow_map", scene->lights[0]->shadow_fbo->depth_texture, 8);

			shader->setUniform("u_random_vector", Vector3(random(), random(), random()));
			shader->setUniform("u_noise_texture", noise, 5);

			quadv->render(GL_TRIANGLES);
			shader->disable();
			volumetric_fbo->unbind();

			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			complete_fbo->bind();
			volumetric_fbo->color_textures[0]->toViewport();
			glDisable(GL_BLEND);

		}


		if (apply_glow) {
			complete_fbo->color_textures[1]->copyTo(aux->color_textures[0]);
			glBlendFunc(GL_ONE, GL_ONE);
			for (int i = 0; i < 10; i++) {

				aux2->bind();
				aux2->enableAllBuffers();
				aux->color_textures[0]->toViewport(Shader::Get("bloom"));
				aux2->unbind();

				aux->bind();
				aux->enableSingleBuffer(0);
				aux2->color_textures[0]->toViewport(Shader::Get("bloom2"));
				aux->unbind();
			}
			complete_fbo->bind();
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_COLOR, GL_ONE);
			//glBlendFunc(GL_ONE, GL_ONE);

			aux->color_textures[0]->toViewport();
			complete_fbo->unbind();
			glDisable(GL_BLEND);
		}


		//======================== TONE MAPPING ==================//
		complete_fbo->unbind();
		if (apply_tonemapper) {
			Shader* tonemapper = Shader::Get("tonemapper");
			tonemapper->enable();
			tonemapper->setUniform("u_scale", u_scale);
			tonemapper->setUniform("u_average_lum", u_average_lum);
			tonemapper->setUniform("u_lumwhite2", u_lumwhite2 * u_lumwhite2);
			tonemapper->setUniform("u_igamma", 1.0f / u_igamma);
			complete_fbo->color_textures[0]->toViewport(tonemapper);
		}
		else {
			complete_fbo->color_textures[0]->toViewport();
		}



		if (show_reflectionProbes) {
			glClear(GL_DEPTH_BUFFER_BIT);
			glEnable(GL_DEPTH_TEST);
			for (int i = 0; i < reflection_probes.size(); i++)
				renderReflectionProbe(reflection_probes[i]->pos, 4, reflection_probes[i]->cubemap, camera);
		}

		if (show_probes) {
			glClear(GL_DEPTH_BUFFER_BIT);
			glEnable(GL_DEPTH_TEST);
			for (int i = 0; i < probes.size(); i++)
				renderProbe(probes[i].pos, 4, (float*)&probes[i].sh, camera, deferred_fbo->depth_texture);
		}

		//glDisable(GL_DEPTH_TEST);

	}
}

void Renderer::renderPrefabDeferred(const Matrix44& model, GTR::Prefab* prefab, Camera* camera){
	renderNodeDeferred(model, &prefab->root, camera);
}

void Renderer::renderNodeDeferred(const Matrix44& prefab_model, GTR::Node* node, Camera* camera) {
	if (!node->visible)
		return;

	Matrix44 node_model = node->getGlobalMatrix(true) * prefab_model;

	if (node->mesh && node->material){
		BoundingBox world_bounding = transformBoundingBox(node_model, node->mesh->box);

		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize)){
			renderMeshWithMaterialDeferred(node_model, node->mesh, node->material, camera);
		}
	}
	for (int i = 0; i < node->children.size(); ++i)
		renderNodeDeferred(prefab_model, node->children[i], camera);
}

void Renderer::renderMeshWithMaterialDeferred(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera) {
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);
	Shader* shader = NULL;
	Texture* texture = NULL;

	texture = material->color_texture;
	//texture = material->emissive_texture;
	//texture = material->metallic_roughness_texture;
	//texture = material->normal_texture;
	//texture = material->occlusion_texture;
	if (texture == NULL)
		texture = Texture::getWhiteTexture();

	//select the blending
	glDisable(GL_BLEND);

	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	shader = Shader::Get("multi");

	assert(glGetError() == GL_NO_ERROR);

	if (!shader)
		return;
	shader->enable();

	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	assert(glGetError() == GL_NO_ERROR);
	shader->setUniform("u_model", model);
	assert(glGetError() == GL_NO_ERROR);

	//ROUGHNESS-METALLIC
	if (material->metallic_roughness_texture)
		shader->setUniform("u_material_map", material->metallic_roughness_texture, 1);
	else
		shader->setUniform("u_material_map", Texture::getBlackTexture(), 1);

	//NORMAL MAP
	if (material->normal_texture) {
		shader->setUniform("u_normal_map", material->normal_texture, 2);
		float normal = 1.0;
		shader->setUniform("hasNormalmap", normal);
	}
	else {
		float normal = 0.0;
		shader->setUniform("hasNormalmap", normal);
	}


	shader->setUniform("SHinterp", SHinterpolation);

	//EMISSIVE
	if (material->emissive_texture) {
		shader->setUniform("u_emissive_texture", material->emissive_texture, 3);
		shader->setUniform("u_emissive_factor", Vector3(0,0,0) );
	}
	else{
		shader->setUniform("u_emissive_texture", Texture::getBlackTexture(), 3);
	shader->setUniform("u_emissive_factor", material->emissive_factor);
	}


	///////////////////IRADIANCE
	
	if (probes_texture)
		shader->setUniform("u_probes_texture", probes_texture, 4);
	else
		shader->setUniform("u_probes_texture", Texture::getBlackTexture(), 4);
	shader->setUniform("u_irr_start", start_pos);
	shader->setUniform("u_irr_end", end_pos);
	shader->setUniform("u_irr_delta", delta);
	shader->setUniform("u_irr_dims", dim);
	shader->setUniform("u_num_probes", dim.x*dim.y*dim.z);
	shader->setUniform("u_irr_normal_distance", 0.0f);



	shader->setUniform1("u_texture_rep", (float)material->texture_rep);


	shader->setUniform("u_color", material->color);
	if (texture)
		shader->setUniform("u_texture", texture, 0);

	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::AlphaMode::MASK ? material->alpha_cutoff : 0);
	shader->setUniform("degamma", degamma);

	mesh->render(GL_TRIANGLES);

	shader->disable();
	glDisable(GL_BLEND);
}


//IRRADIANCE FUNCTIONS
void Renderer::computeIrradiance(Scene* scene) {
	
	if(!irr_fbo){
		irr_fbo = new FBO();
		irr_fbo->create(64, 64, 1, GL_RGB, GL_FLOAT);
	}

	this->start_pos.set(-125, 11, -330);
	this->end_pos.set(300, 230, 120);
	//this->dim.set(14, 5, 15);
	this->dim.set(8, 6, 10);

	this->delta = (end_pos - start_pos);

	delta.x /= (dim.x - 1);
	delta.y /= (dim.y - 1);
	delta.z /= (dim.z - 1);

	probes.clear();
	for (int z = 0; z < dim.z; ++z)
		for (int y = 0; y < dim.y; ++y)
			for (int x = 0; x < dim.x; ++x)
			{
				sProbe p;
				p.index.set(x, y, z);
				p.pos = start_pos + delta * Vector3(x, y, z);
				probes.push_back(p);
			}
	/*sProbe p;
	p.pos.set(0, 50, 0);
	probes.push_back(p);*/

	Camera c;
	c.setPerspective(90, 1, 0.1, 1000);
	FloatImage images[6];
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	Scene * sc = Scene::getInstance();
	for (int iP = 0; iP < probes.size(); ++iP) {

		sProbe& p = probes[iP];
		for (int i = 0; i < 6; i++) {
							
			//glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

			Vector3 eyecamera = p.pos;
			Vector3 centercamera = p.pos + cubemapFaceNormals[i][2];
			Vector3 upcamera = cubemapFaceNormals[i][1];
			c.lookAt(eyecamera, centercamera, upcamera);
			c.enable();

			irr_fbo->bind();
			renderSceneForward(Scene::getInstance(), &c);
			irr_fbo->unbind();

			images[i].fromTexture(irr_fbo->color_textures[0]);

		}
		p.sh = computeSH(images);

	}


	if (!probes_texture) {
		probes_texture = new Texture(
			9, //9 coefficients per probe
			probes.size(), //as many rows as probes
			GL_RGB, //3 channels per coefficient
			GL_FLOAT); //they require a high range
	}
		//we must create the color information for the texture. because every SH are 27 floats in the RGB,RGB,... order, we can create an array of SphericalHarmonics and use it as pixels of the texture
	SphericalHarmonics* sh_data = NULL;
	sh_data = new SphericalHarmonics[dim.x * dim.y * dim.z];

	for (int iP = 0; iP < probes.size(); iP++) {
		sProbe& p = probes[iP];
		int index = floor(p.index.x + p.index.y * dim.x + p.index.z * (dim.x*dim.y));
		sh_data[index] = p.sh;
	}

	probes_texture->upload(GL_RGB, GL_FLOAT, false, (uint8*)sh_data);

	probes_texture->bind();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	//always free memory after allocating it!!!
	delete[] sh_data;

}

void Renderer::renderProbe(Vector3 pos, float size, float* coeffs, Camera* camera, Texture* depth) {

	Shader* shader = Shader::Get("probe");
	Mesh* mesh = Mesh::Get("data/meshes/sphere.obj");

	//glEnable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	//glEnable(GL_DEPTH_TEST);
	
	Matrix44 model;
	model.setTranslation(pos.x, pos.y, pos.z);
	model.scale(size, size, size);

	shader->enable();
	shader->setUniform("u_iRes", Vector2((float)1 / depth->width, (float)1 / depth->height));
	shader->setUniform("u_depth_texture", depth, 1);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_pos", camera->eye);
	shader->setUniform("u_model", model);
	shader->setUniform3Array("u_coeffs", coeffs, 9);

	mesh->render(GL_TRIANGLES);
	shader->disable();
}


//AMBIENT OCCLUSION FUNCTIONS
std::vector<Vector3> GTR::generateSpherePoints(int num,	float radius, bool hemi){
	std::vector<Vector3> points;
	points.resize(num);
	for (int i = 0; i < num; i += 3)
	{
		Vector3& p = points[i];
		float u = random();
		float v = random();
		float theta = u * 2.0 * PI;
		float phi = acos(2.0 * v - 1.0);
		float r = cbrt(random() * 0.9 + 0.1) * radius;
		float sinTheta = sin(theta);
		float cosTheta = cos(theta);
		float sinPhi = sin(phi);
		float cosPhi = cos(phi);
		p.x = r * sinPhi * cosTheta;
		p.y = r * sinPhi * sinTheta;
		p.z = r * cosPhi;
		if (hemi && p.z < 0)
			p.z *= -1.0;
	}
	return points;
}


//REFLECTIONS FUNCTIONS
Texture* GTR::CubemapFromHDRE(const char* filename)
{
	HDRE* hdre = new HDRE();
	if (!hdre->load(filename))
	{
		delete hdre;
		return NULL;
	}

	Texture* texture = new Texture();
	texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFaces(0), hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_FLOAT);
	for (int i = 1; i < 6; ++i)
		texture->uploadCubemap(texture->format, texture->type, false, (Uint8**)hdre->getFaces(i), GL_RGBA32F, i);
	return texture;
}

void Renderer::generateReflectionProbes() {
	sReflectionProbe* probe1 = new sReflectionProbe;
	probe1->pos.set(171, 100, -222);
	probe1->cubemap = new Texture();
	probe1->cubemap->createCubemap(512, 512,	NULL,GL_RGB, GL_UNSIGNED_INT, false);
	reflection_probes.push_back(probe1);

	sReflectionProbe* probe2 = new sReflectionProbe;
	probe2->pos.set(171, 100, 10);
	probe2->cubemap = new Texture();
	probe2->cubemap->createCubemap(512, 512,	NULL,GL_RGB, GL_UNSIGNED_INT, false);
	reflection_probes.push_back(probe2);

}

void Renderer::computeReflections(Scene* scene) {
	if (!reflection_fbo) {
		reflection_fbo = new FBO();
	}

	Camera c;
	c.setPerspective(90, 1, 0.1, 1000);
	glEnable(GL_DEPTH_TEST);
	for (int j = 0; j < reflection_probes.size(); j++) {

		sReflectionProbe *probe = reflection_probes.at(j);
		for (int i = 0; i < 6; ++i)
		{
			//assign cubemap face to FBO
			reflection_fbo->setTexture(probe->cubemap, i);

			//bind FBO
			reflection_fbo->bind();

			//render view
			Vector3 eye = probe->pos;
			Vector3 center = probe->pos + cubemapFaceNormals[i][2];
			Vector3 up = cubemapFaceNormals[i][1];
			c.lookAt(eye, center, up);
			c.enable();
			renderSceneForward(Scene::getInstance(), &c);
			reflection_fbo->unbind();
		}
		probe->cubemap->generateMipmaps();
	}

}

void Renderer::renderReflectionProbe(Vector3 pos, float size, Texture* cubemap, Camera* camera) {
	glEnable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	
	
	Shader* shader = Shader::Get("reflectionprobe");
	Mesh* mesh = Mesh::Get("data/meshes/sphere.obj");

	
	Matrix44 model;
	model.setTranslation(pos.x, pos.y, pos.z);
	model.scale(size, size, size);

	Matrix44 inv_viewprojection = camera->viewprojection_matrix;
	inv_viewprojection.inverse();

	shader->enable();
	shader->setUniform("u_inverse_viewprojection", inv_viewprojection);
	shader->setUniform("u_iRes", Vector2((float)1 / deferred_fbo->width, (float)1 / deferred_fbo->height));
	shader->setUniform("u_normal_texture", deferred_fbo->color_textures[1], 1);
	shader->setUniform("u_extra_texture", deferred_fbo->color_textures[2], 2);
	shader->setUniform("u_depth_texture", deferred_fbo->depth_texture, 3);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_pos", camera->eye);
	shader->setUniform("u_model", model);
	shader->setUniform("u_environment_texture", cubemap, 8);

	mesh->render(GL_TRIANGLES);
	shader->disable();

}

void Renderer::renderSkybox(Camera *camera) {

	//Add Skybox
	if (skybox) {
		Shader* shader = Shader::Get("skybox");
		Mesh* mesh = Mesh::Get("data/meshes/sphere.obj");
		glDisable(GL_CULL_FACE);
		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		Matrix44 model;
		model.setTranslation(camera->eye.x, camera->eye.y, camera->eye.z);
		model.scale(10, 10, 10);
		shader->enable();
		shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
		shader->setUniform("u_camera_pos", camera->eye);
		shader->setUniform("u_model", model);
		shader->setUniform("u_texture", skybox, 0);
		mesh->render(GL_TRIANGLES);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		shader->disable();
	}
}

void Renderer::saveIrradiance() {
	sIrrHeader header;
	header.start = start_pos;
	header.end = end_pos;
	header.dims = dim;
	header.delta = delta;
	header.num_probes = dim.x * dim.y * dim.z;


	FILE* f = fopen("data/irradiance.bin", "wb");
	fwrite(&header, sizeof(header), 1, f);
	fwrite(&(probes[0]), sizeof(sProbe), probes.size(), f);
	fclose(f);

}

bool Renderer::readIrradiance() {
	FILE* f = fopen("data/irradiance.bin", "rb");
	if (!f)
		return false;

	//read header
	sIrrHeader header;
	fread(&header, sizeof(header), 1, f);

	//copy info from header to our local vars
	start_pos = header.start;
	end_pos = header.end;
	dim = header.dims;
	delta = header.delta;
	int num_probes = header.num_probes;

	//allocate space for the probes
	probes.resize(num_probes);


	//read from disk directly to our probes container in memory
	fread(&probes[0], sizeof(sProbe), probes.size(), f);
	fclose(f);

	if (!probes_texture) {
		probes_texture = new Texture(
			9, //9 coefficients per probe
			probes.size(), //as many rows as probes
			GL_RGB, //3 channels per coefficient
			GL_FLOAT); //they require a high range
	}
	//we must create the color information for the texture. because every SH are 27 floats in the RGB,RGB,... order, we can create an array of SphericalHarmonics and use it as pixels of the texture
	SphericalHarmonics* sh_data = NULL;
	sh_data = new SphericalHarmonics[dim.x * dim.y * dim.z];

	for (int iP = 0; iP < probes.size(); iP++) {
		sProbe& p = probes[iP];
		int index = floor(p.index.x + p.index.y * dim.x + p.index.z * (dim.x * dim.y));
		sh_data[index] = p.sh;
	}

	probes_texture->upload(GL_RGB, GL_FLOAT, false, (uint8*)sh_data);

	probes_texture->bind();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	//always free memory after allocating it!!!
	delete[] sh_data;

	//this->probes_texture->Get("data/irradiance.tga");

}