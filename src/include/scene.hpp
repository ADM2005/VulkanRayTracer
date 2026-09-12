#pragma once

#include "types.hpp"
#include "ray_tracer.hpp"

#include <unordered_set>


class Scene {
public:
	std::vector<AllocatedMesh> meshes{};
	std::vector<Material> materials{};
	std::vector<MeshObject> objects{};
	std::vector<LightObject> lights{};
	CameraObject camera{};
	bool assignedCamera{ false };


	Scene() {
		CameraObject defaultCamera{};
		defaultCamera.transform = glm::vec3(0, 0, 5);
		defaultCamera.rotation = glm::vec3(0, 0, 0);
		defaultCamera.scale = glm::vec3(1, 1, 1);
		defaultCamera.fov = 70.0f;
		defaultCamera.near = 0.01f;
		defaultCamera.far = 1000.0f;
	}
};