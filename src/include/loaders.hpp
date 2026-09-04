#pragma once
#include "types.hpp"
#include <vulkan/vulkan.h>
#include <iostream>
#include <fstream>
#include <optional>
#include "ray_tracer.hpp"

namespace loaders {
	VkShaderModule load_shader(const char* filepath, VkDevice device);

	// Loads GLTF meshes into a list of AllocatedMesh
	// Each mesh gets a unique AllocatedMesh struct, which lists out the primitives inside.

	std::optional<std::vector<AllocatedMesh>> load_gltf_meshes(const char* filepath, RayTracer* rayTracer);
}
