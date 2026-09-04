#include "include/loaders.hpp"
#include <iostream>
#include <filesystem>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <functional>

VkShaderModule loaders::load_shader(const char* filepath, VkDevice device) {

	std::ifstream file(filepath, std::ifstream::binary);
	if (!file) {
		throw std::runtime_error("filed to open file!");
	}

	file.seekg(0, file.end);
	int size_in_bytes = file.tellg();
	file.seekg(0, file.beg);

	char* buffer = new char[size_in_bytes];
	file.read(buffer, size_in_bytes);

	file.close();
	uint32_t* data = reinterpret_cast<uint32_t*>(buffer);

	VkShaderModuleCreateInfo shaderModuleCreateInfo{ .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	shaderModuleCreateInfo.codeSize = size_in_bytes;
	shaderModuleCreateInfo.pCode = data;

	VkShaderModule shaderModule;

	if (vkCreateShaderModule(device, &shaderModuleCreateInfo, nullptr, &shaderModule)
		!= VK_SUCCESS) {
		throw std::runtime_error("failed to create shader module");
	}

	return shaderModule;
}

std::optional<std::vector<AllocatedMesh>> loaders::load_gltf_meshes(const char* filepath, RayTracer* rayTracer) {
	std::filesystem::path fp{ filepath };

	fastgltf::Parser parser{};

	auto gltfFile = fastgltf::MappedGltfFile::FromPath(filepath);

	if (!bool(gltfFile)) {
		return {};
	}

	constexpr auto gltfOptions = fastgltf::Options::GenerateMeshIndices;
	auto asset_ret = parser.loadGltf(gltfFile.get(), fp.parent_path(), gltfOptions);

	if (!bool(asset_ret)) {
		return {};
	}

	auto asset = std::move(asset_ret.get());

	std::vector<AllocatedMesh> allocatedMeshes;

	for (const auto& mesh : asset.meshes) {
	}

	return {};
}

AllocatedMesh allocateMesh(fastgltf::Mesh mesh, fastgltf::Asset& asset) {
	AllocatedMesh allocMesh{};

	std::vector<Vertex> globalVertices{};
	std::vector<uint32_t> globalIndices{};


	int vertPosIdx = 0;
	for (auto it = mesh.primitives.begin(); it != mesh.primitives.end(); it++) {
		int vertOffset = globalVertices.size();

		MeshPrimitive primitive{};
		primitive.vertexOffset = vertOffset;

		auto* posIt = it->findAttribute("POSITION");
		if (posIt == it->attributes.end()) {
			throw std::runtime_error("failed to find position attribute for primitive!");
		}

		auto& posAccessor = asset.accessors[posIt->accessorIndex];
		fastgltf::iterateAccessor<glm::vec3>(asset, posAccessor,
			[&](glm::vec3 pos) {
				Vertex vert{};
				vert.position = pos;

				globalVertices.push_back(vert);
			});

		auto* uvIt = it->findAttribute("TEXCOORD0");
		if (uvIt != it->attributes.end()) {
			auto& accessor = asset.accessors[uvIt->accessorIndex];
			fastgltf::iterateAccessorWithIndex<glm::vec2>(asset, accessor,
				[&](glm::vec2 uv, std::size_t idx) {
					globalVertices[vertOffset + idx].uv_x = uv.x;
					globalVertices[vertOffset + idx].uv_y = uv.y;
				});
		}

		auto* normalIt = it->findAttribute("NORMAL");
		if (normalIt != it->attributes.end()) {
			auto& accessor = asset.accessors[normalIt->accessorIndex];
			fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, accessor,
				[&](glm::vec3 normal, size_t idx) {
					globalVertices[vertOffset + idx].normal = normal;
				});
		}

		if (!it->indicesAccessor.has_value()) {
			throw std::runtime_error("mesh has no indices accessor. Is GenerateMeshIndices enabled?");
		}

		auto& idxAccessor = (it->indicesAccessor).value();

		primitive.firstIndex = globalIndices.size();

		fastgltf::iterateAccessor<uint32_t>(asset, asset.accessors[idxAccessor], [&](uint32_t idx) {
			globalIndices.push_back(idx);
		});

		primitive.count = globalIndices.size() - primitive.firstIndex;

		allocMesh.primitives.push_back(primitive);
	}

	// Allocate mesh and index buffers onto the gpu.

	// Staging buffer
	return allocMesh;
	
}
