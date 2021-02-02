#pragma once
#include "prefab.h"
#include "Scene.h"
#include "PrefabEntity.h"
#include "fbo.h"
#include "application.h"
#include "sphericalharmonics.h"

//forward declarations
class Camera;

namespace GTR {

	class Prefab;
	class Material;
	
	struct sProbe {
		Vector3 index;
		Vector3 pos;
		SphericalHarmonics sh;
	};

	struct sReflectionProbe {
		Vector3 pos;
		Texture* cubemap = NULL;
	};
	struct sIrrHeader {
		Vector3 start;
		Vector3 end;
		Vector3 delta;
		Vector3 dims;
		int num_probes;
	};

	std::vector<Vector3> generateSpherePoints(int num, float radius, bool hemi);
	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{
	private:
		FBO* deferred_fbo, * complete_fbo, * ssao_fbo, * volumetric_fbo, * reflection_fbo, * aux, *aux2;
		bool show_properties, degamma, pbr, show_ssao, computeAmbientOcclusion, 
			apply_ssao, apply_volumetric, apply_environmentReflections, 
			show_reflectionProbes, add_decal, apply_tonemapper, apply_glow, SHinterpolation,
			show_irradiance;
		Texture* skybox, *decal_depth_texture, *decal, *noise;
		std::vector<Vector3> points;
		std::vector<sProbe> probes;
		std::vector<sReflectionProbe*> reflection_probes;
		Vector3 start_pos;
		Vector3 end_pos;
		Vector3 dim;
		Vector3 delta;
		Mesh* cube;

		float u_scale, u_average_lum, u_lumwhite2, u_igamma;
	public:
		FBO *irr_fbo;
		Texture* probes_texture;

		//add here your functions
		Renderer();

		void renderInMenu();

		//Shadowmap creation
		void createShadowmap(std::vector<BaseEntity*> ent, Light* l);
		void checkRendering(PrefabEntity* p, Shader* s, Light* l, GTR::Node* n);

		//Irradiance
		void computeIrradiance(Scene* scene);
		void renderProbe(Vector3 pos, float size, float* coeffs, Camera* camera, Texture* depth);

		//Forward
		void renderForward(Scene* scene, Camera* camera); //Forward pipeline
		void renderSceneForward(Scene* scene, Camera* camera); //Forward scene render, only render prefabs

		//Deferred
		void renderSceneInDeferred(Scene* scene, Camera* camera);
		void renderMeshinDeferred(std::vector<BaseEntity*> scene, Camera* camera);

		void renderPrefabDeferred(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);
		void renderNodeDeferred(const Matrix44& prefab_model, GTR::Node* node, Camera* camera);
		void renderMeshWithMaterialDeferred(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

		//Reflections
		void computeReflections(Scene* scene);
		void generateReflectionProbes();
		void renderReflectionProbe(Vector3 pos, float size, Texture* cubemap, Camera* camera);
		void renderSkybox(Camera* camera);

		void saveIrradiance();
		bool readIrradiance();
		//--------------------------------------------------------------------
	
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
	};

	Texture* CubemapFromHDRE(const char* filename);

};