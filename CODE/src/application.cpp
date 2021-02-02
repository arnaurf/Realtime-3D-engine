#include "application.h"
#include "utils.h"
#include "mesh.h"
#include "texture.h"
 
#include "fbo.h"
#include "shader.h"
#include "input.h"
#include "includes.h"
#include "prefab.h"
#include "gltf_loader.h"
#include "renderer.h"
#include "PrefabEntity.h"
#include "BaseEntity.h"
#include "Light.h"
#include "Scene.h"

#include <time.h> 

#include <cmath>
#include <string>
#include <cstdio>

Application* Application::instance = nullptr;
Vector4 bg_color(0.5, 0.5, 0.5, 1.0);
Camera* camera = nullptr;

Mesh floor_plane;
GTR::Material floor_material;

PrefabEntity* prefab = nullptr;
Light* white_light = nullptr, * red_light = nullptr, * blue_light = nullptr, *green_light = nullptr;

GTR::Renderer* renderer = nullptr;
FBO* fbo = nullptr;
Texture* texture = nullptr;

float cam_speed = 10;
bool show_fbo;
Mesh* sphere;

Application::Application(int window_width, int window_height, SDL_Window* window)
{
	this->window_width = window_width;
	this->window_height = window_height;
	this->window = window;
	instance = this;
	must_exit = false;
	render_debug = true;
	render_gui = true;
	show_fbo = false;

	render_wireframe = false;

	fps = 0;
	frame = 0;
	t = 0.0f;
	elapsed_time = 0.0f;
	mouse_locked = false;
	//loads and compiles several shaders from one single file
    //change to "data/shader_atlas_osx.txt" if you are in XCODE
	if(!Shader::LoadAtlas("data/shader_atlas.txt"))
        exit(1);
    checkGLErrors();

	// Create camera
	camera = new Camera();
	camera->lookAt(Vector3(176.f, 106.0f, -358.f), Vector3(170.f, -3.632f, 660.f), Vector3(0.f, 1.f, 0.f)); //posicio per testear volumetric
	//camera->lookAt(Vector3(755.f, 199.0f, 22.f), Vector3(0.f, 0.0f, 0.f), Vector3(0.f, 1.f, 0.f));
	camera->setPerspective( 45.f, window_width/(float)window_height, 1.0f, 10000.f);


	renderer = new GTR::Renderer();

	Scene::getInstance()->createFloor(1000);
	GTR::Prefab* prefab_house = GTR::Prefab::Get("data/prefabs/brutalism/scene.gltf");
	Matrix44 model;
	model.setTranslation(0, 20, 0);
	//model.rotate(45*DEG2RAD, Vector3(0,1,0));
	model.scale(100, 100, 100);
	Scene::getInstance()->addEntity(new PrefabEntity(prefab_house, model));

	GTR::Prefab* prefab_car = GTR::Prefab::Get("data/prefabs/gmc/scene.gltf");
	GTR::Node* aa = prefab_car->root.children.at(2);
	prefab_car->root.children.pop_back(); //delete floor plane
	Matrix44 model1;
	model1.setTranslation(450, 0, -50);
	model1.rotate(45*DEG2RAD, Vector3(0,1,0));
	Scene::getInstance()->addEntity(new PrefabEntity(prefab_car, model1));

	sphere = Mesh::Get("data/meshes/sphere.obj");
	//Create some lights
	green_light = new Light(Vector3(1, 1, 1), Vector3(100, 75, -700), Vector2(130, -50), DIRECTIONAL, 4.0, window_width, window_height);
	//blue_light->model.setFrontAndOrthonormalize(Vector3(0, -1, 0));
	Scene::getInstance()->addLight(green_light);

	red_light = new Light(Vector3(0.4, 1, 0.4), Vector3(-45, 343, 344), Vector2(-120, -45), SPOT, 2, window_width, window_height);
	//red_light->model.setFrontAndOrthonormalize(Vector3(1, -1, 0));
	//red_light->getCamera()->aspect = 1.0;
	//red_light->getCamera()->lookAt(red_light->model.getTranslation(), red_light->model.frontVector(), Vector3(0, 1, 0));
	//red_light->getCamera()->setPerspective(100.0, red_light->getCamera()->aspect, 1.0, 400.0);
	red_light->setSpotAngle(0.8);
	red_light->setSpotExponent(5);
	Scene::getInstance()->addLight(red_light);
	


	Scene::getInstance()->shposition = Vector3(0, 100, 0);
	Scene::getInstance()->background = Vector4(174.0f/255.0f, 245.0f / 255.0f, 255.0f / 255.0f, 1.0);

	/*
	srand(time(NULL));

	for (int i = 0; i < 20; i++) {
		blue_light = new Light(Vector3(0, 0, 1), Vector3(rand() % 2000 -1000, 100, rand() % 2000 -1000), Vector2(0, 0), OMNI, 10, window_width, window_height);
		blue_light->setMaxDist(200);
		Scene::getInstance()->addLight(blue_light);
	}
	*/

	Scene::getInstance()->ambient_light = 0.01f;
	
	/*
	floor_plane.createPlane(1000);
	floor_material.color_texture = Texture::Get("data/textures/floor/Mud_Rocks_001_Color.tga");
	floor_material.normal_texture = Texture::Get("data/textures/floor/Mud_Rocks_001_normal.tga");
	floor_material.metallic_roughness_texture = Texture::getBlackTexture();//Texture::Get("data/textures/floor/Mud_Rocks_001_roughness.tga");
	floor_material.texture_rep = 10;
	*/

	rendertype = DEFERRED;

	//hide the cursor
	SDL_ShowCursor(!mouse_locked); //hide or show the mouse
}

//what to do when the image has to be draw
void Application::render(void)
{
	//be sure no errors present in opengl before start
	checkGLErrors();
	
    
	//set the camera as default (used by some functions in the framework)
	camera->enable();

	//set default flags
	glDisable(GL_BLEND);
    
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	if(render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	else
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);




	
	//lets render something
	if (rendertype == FORWARD) {
		renderer->renderForward(Scene::getInstance(), camera);
	}
	else if (rendertype == DEFERRED) {
		renderer->renderSceneInDeferred(Scene::getInstance(), camera);
	}

    //glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    //render anything in the gui after this
	#ifndef _DEBUG
	if (rendertype == FORWARD) {
		if (Scene::getInstance()->lights.size() != 0) {
			for (int i = 0; i < Scene::getInstance()->lights.size(); i++) {
				Light* l = Scene::getInstance()->lights[i];
				glViewport((i * 250) + (i * 10), 0, 250, 250);
				FBO* fbo;
				fbo = l->shadow_fbo;
				if (Scene::getInstance()->lights[i]->getType() == DIRECTIONAL) {
					fbo->depth_texture->toViewport();
				}
				else if (Scene::getInstance()->lights[i]->getType() == SPOT) {
					Shader* s = Shader::Get("depth");
					s->setUniform("u_camera_nearfar", Vector2(l->cnear, l->cfar));
					s->setUniform("u_alpha_cutoff", 0.0f);
					s->setUniform("u_color", Vector4(1.0, 1.0, 1.0, 1.0));
					fbo->depth_texture->toViewport(s);
				}
			}

		}
	}
	else {
		

		if (show_fbo) {
			Light* light = Scene::getInstance()->lights[0];
			if (light->shadow_fbo) {
				glDisable(GL_DEPTH_TEST);
				glViewport(0, 0, 250, 250);
				Shader* sh = Shader::Get("depth");
				sh->enable();
				sh->setUniform("u_camera_nearfar", Vector2(light->cnear, light->cfar));
				sh->setUniform("u_alpha_cutoff", 0.0f);
				sh->setUniform("u_color", Vector4(1.0, 1.0, 1.0, 1.0));
				light->shadow_fbo->depth_texture->toViewport(sh);
				sh->disable();
			}
			if (renderer->irr_fbo) {


				glViewport(250, 0, 250, 250);
				renderer->irr_fbo->color_textures[0]->toViewport();






				/*
				Shader* sh = Shader::Get("depth");
				sh->enable();
				sh->setUniform("u_camera_nearfar", Vector2(light->cnear, light->cfar));
				sh->setUniform("u_alpha_cutoff", 0.0f);
				sh->setUniform("u_color", Vector4(1.0, 1.0, 1.0, 1.0));
				renderer->irr_fbo->depth_texture->toViewport(sh);*/
			}
		}
	}
	glEnable(GL_DEPTH_TEST);
	
	#endif
}

void Application::update(double seconds_elapsed)
{
	float speed = seconds_elapsed * cam_speed; //the speed is defined by the seconds_elapsed so it goes constant
	float orbit_speed = seconds_elapsed * 0.5;
	
	//async input to move the camera around
	if (Input::isKeyPressed(SDL_SCANCODE_LSHIFT)) speed *= 10; //move faster with left shift
	if (Input::isKeyPressed(SDL_SCANCODE_W) || Input::isKeyPressed(SDL_SCANCODE_UP)) camera->move(Vector3(0.0f, 0.0f, 10.0f) * speed);
	if (Input::isKeyPressed(SDL_SCANCODE_S) || Input::isKeyPressed(SDL_SCANCODE_DOWN)) camera->move(Vector3(0.0f, 0.0f,-10.0f) * speed);
	if (Input::isKeyPressed(SDL_SCANCODE_A) || Input::isKeyPressed(SDL_SCANCODE_LEFT)) camera->move(Vector3(10.0f, 0.0f, 0.0f) * speed);
	if (Input::isKeyPressed(SDL_SCANCODE_D) || Input::isKeyPressed(SDL_SCANCODE_RIGHT)) camera->move(Vector3(-10.0f, 0.0f, 0.0f) * speed);

	//mouse input to rotate the cam
	#ifndef SKIP_IMGUI
	if (!ImGuizmo::IsUsing())
	#endif
	{
		if (mouse_locked || Input::mouse_state & SDL_BUTTON(SDL_BUTTON_RIGHT)) //move in first person view
		{
			camera->rotate(-Input::mouse_delta.x * orbit_speed * 0.5, Vector3(0, 1, 0));
			Vector3 right = camera->getLocalVector(Vector3(1, 0, 0));
			camera->rotate(-Input::mouse_delta.y * orbit_speed * 0.5, right);
		}
		else //orbit around center
		{
			bool mouse_blocked = false;
			#ifndef SKIP_IMGUI
						mouse_blocked = ImGui::IsAnyWindowHovered() || ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive();
			#endif
			if (Input::mouse_state & SDL_BUTTON(SDL_BUTTON_LEFT) && !mouse_blocked) //is left button pressed?
			{
				camera->orbit(-Input::mouse_delta.x * orbit_speed, Input::mouse_delta.y * orbit_speed);
			}
		}
	}
	
	//move up or down the camera using Q and E
	if (Input::isKeyPressed(SDL_SCANCODE_Q)) camera->moveGlobal(Vector3(0.0f, -10.0f, 0.0f) * speed);
	if (Input::isKeyPressed(SDL_SCANCODE_E)) camera->moveGlobal(Vector3(0.0f, 10.0f, 0.0f) * speed);

	//to navigate with the mouse fixed in the middle
	SDL_ShowCursor(!mouse_locked);
	#ifndef SKIP_IMGUI
		ImGui::SetMouseCursor(mouse_locked ? ImGuiMouseCursor_None : ImGuiMouseCursor_Arrow);
	#endif
	if (mouse_locked)
	{
		Input::centerMouse();
		//ImGui::SetCursorPos(ImVec2(Input::mouse_position.x, Input::mouse_position.y));
	}
}

void Application::renderDebugGizmo()
{
	if (!prefab)
		return;

	//example of matrix we want to edit, change this to the matrix of your entity
	Matrix44& matrix = Scene::getInstance()->entities[0]->model;

	#ifndef SKIP_IMGUI

	static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
	static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::WORLD);
	if (ImGui::IsKeyPressed(90))
		mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
	if (ImGui::IsKeyPressed(69))
		mCurrentGizmoOperation = ImGuizmo::ROTATE;
	if (ImGui::IsKeyPressed(82)) // r Key
		mCurrentGizmoOperation = ImGuizmo::SCALE;
	if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE))
		mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
	ImGui::SameLine();
	if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE))
		mCurrentGizmoOperation = ImGuizmo::ROTATE;
	ImGui::SameLine();
	if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE))
		mCurrentGizmoOperation = ImGuizmo::SCALE;
	float matrixTranslation[3], matrixRotation[3], matrixScale[3];
	ImGuizmo::DecomposeMatrixToComponents(matrix.m, matrixTranslation, matrixRotation, matrixScale);
	ImGui::InputFloat3("Tr", matrixTranslation, 3);
	ImGui::InputFloat3("Rt", matrixRotation, 3);
	ImGui::InputFloat3("Sc", matrixScale, 3);
	ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, matrix.m);

	if (mCurrentGizmoOperation != ImGuizmo::SCALE)
	{
		if (ImGui::RadioButton("Local", mCurrentGizmoMode == ImGuizmo::LOCAL))
			mCurrentGizmoMode = ImGuizmo::LOCAL;
		ImGui::SameLine();
		if (ImGui::RadioButton("World", mCurrentGizmoMode == ImGuizmo::WORLD))
			mCurrentGizmoMode = ImGuizmo::WORLD;
	}
	static bool useSnap(false);
	if (ImGui::IsKeyPressed(83))
		useSnap = !useSnap;
	ImGui::Checkbox("", &useSnap);
	ImGui::SameLine();
	static Vector3 snap;
	switch (mCurrentGizmoOperation)
	{
	case ImGuizmo::TRANSLATE:
		//snap = config.mSnapTranslation;
		ImGui::InputFloat3("Snap", &snap.x);
		break;
	case ImGuizmo::ROTATE:
		//snap = config.mSnapRotation;
		ImGui::InputFloat("Angle Snap", &snap.x);
		break;
	case ImGuizmo::SCALE:
		//snap = config.mSnapScale;
		ImGui::InputFloat("Scale Snap", &snap.x);
		break;
	}
	ImGuiIO& io = ImGui::GetIO();
	ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
	ImGuizmo::Manipulate(camera->view_matrix.m, camera->projection_matrix.m, mCurrentGizmoOperation, mCurrentGizmoMode, matrix.m, NULL, useSnap ? &snap.x : NULL);
	#endif
}


//called to render the GUI from
void Application::renderDebugGUI(void)
{
#ifndef SKIP_IMGUI //to block this code from compiling if we want

	//System stats
	
	ImGui::Text(getGPUStats().c_str());					   // Display some text (you can use a format strings too)
	ImGui::ColorEdit4("BG color", Scene::getInstance()->background.v);

	if (ImGui::TreeNode("Render options")) {
		ImGui::Combo("Render Type", &rendertype, "FORWARD\0DEFFERRED", 2);
		ImGui::Checkbox("Show Light maps", &show_fbo);
		ImGui::Checkbox("Wireframe", &render_wireframe);
		renderer->renderInMenu();
		ImGui::TreePop();
	}

		//add info to the debug panel about the camera
	if (ImGui::TreeNode(camera, "Camera")) {
		camera->renderInMenu();
		ImGui::TreePop();
	}


	// RENDER PREFABS AND ENTITIES INFO
	if (ImGui::TreeNode("Entities")) {
		
		for (std::vector<BaseEntity*>::iterator it = Scene::getInstance()->entities.begin(); it < Scene::getInstance()->entities.end(); it++) {
			if ((*it)->type == PREFAB)
				((PrefabEntity*)(*it))->renderinMenu();
		}
		ImGui::TreePop();
	}

	if (ImGui::TreeNode("Lights")) {
		ImGui::DragFloat("Ambient Light", &Scene::getInstance()->ambient_light, 0.005f, 0.0f, 2.0f);

		for (std::vector<Light*>::iterator it = Scene::getInstance()->lights.begin(); it < Scene::getInstance()->lights.end(); it++) {
			(*it)->renderinMenu();
		}
		ImGui::TreePop();
	}
	/*
	for (int i = 0; i < Scene::getInstance()->lights.size(); i++) {
		if (ImGui::TreeNode(Scene::getInstance()->lights[i]->getCamera(), "CameraLight")) {
			Scene::getInstance()->lights[i]->getCamera()->renderInMenu();
			ImGui::TreePop();
		}
	}
	//example to show prefab info: first param must be unique!
	for (int i = 0; i < Scene::getInstance()->lights.size(); i++) {
		if (ImGui::TreeNode(Scene::getInstance()->lights[i], "Light")) {
			Scene::getInstance()->lights[i]->renderInMenu();
			ImGui::TreePop();
		}
	}

	for (int i = 0; i < Scene::getInstance()->entities.size(); i++) {
		if (Scene::getInstance()->entities[i]->type == PREFAB) {
			PrefabEntity* p = new PrefabEntity();
			p = (PrefabEntity*)Scene::getInstance()->entities[i];
			if (ImGui::TreeNode(p->getPrefab(), "Prefab")) {
				p->getPrefab()->root.renderInMenu();
				ImGui::TreePop();
			}
		}
	}
	*/
#endif
}

//Keyboard event handler (sync input)
void Application::onKeyDown( SDL_KeyboardEvent event )
{
	switch(event.keysym.sym)
	{
		case SDLK_ESCAPE: must_exit = true; break; //ESC key, kill the app
		case SDLK_F1: render_debug = !render_debug; break;
		case SDLK_f: camera->center.set(0, 0, 0); camera->updateViewMatrix(); break;
		case SDLK_F5: Shader::ReloadAll(); break;
	}
}

void Application::onKeyUp(SDL_KeyboardEvent event)
{
}

void Application::onGamepadButtonDown(SDL_JoyButtonEvent event)
{

}

void Application::onGamepadButtonUp(SDL_JoyButtonEvent event)
{

}

void Application::onMouseButtonDown( SDL_MouseButtonEvent event )
{
	if (event.button == SDL_BUTTON_MIDDLE) //middle mouse
	{
		//Input::centerMouse();
		mouse_locked = !mouse_locked;
		SDL_ShowCursor(!mouse_locked);
	}
}

void Application::onMouseButtonUp(SDL_MouseButtonEvent event)
{
}

void Application::onMouseWheel(SDL_MouseWheelEvent event)
{
	bool mouse_blocked = false;

	#ifndef SKIP_IMGUI
		ImGuiIO& io = ImGui::GetIO();
		if(!mouse_locked)
		switch (event.type)
		{
			case SDL_MOUSEWHEEL:
			{
				if (event.x > 0) io.MouseWheelH += 1;
				if (event.x < 0) io.MouseWheelH -= 1;
				if (event.y > 0) io.MouseWheel += 1;
				if (event.y < 0) io.MouseWheel -= 1;
			}
		}
		mouse_blocked = ImGui::IsAnyWindowHovered();
	#endif

	if (!mouse_blocked && event.y)
	{
		if (mouse_locked)
			cam_speed *= 1 + (event.y * 0.1);
		else
			camera->changeDistance(event.y * 0.5);
	}
}

void Application::onResize(int width, int height)
{
    std::cout << "window resized: " << width << "," << height << std::endl;
	glViewport( 0,0, width, height );
	camera->aspect =  width / (float)height;
	window_width = width;
	window_height = height;
}

