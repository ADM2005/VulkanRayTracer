#include "include/loaders.hpp"
#include <iostream>
#include <filesystem>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <functional>
#include <stack>

#include "include/bvh.hpp"

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


AllocatedMesh allocateMesh(const fastgltf::Mesh& mesh, fastgltf::Asset& asset, RayTracer* rt) {
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

		auto* colorIt = it->findAttribute("COLOR_0");
		if (colorIt != it->attributes.end()) {
			auto& accessor = asset.accessors[colorIt->accessorIndex];
			fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, accessor,
				[&](glm::vec4 color, size_t idx) {
					globalVertices[vertOffset + idx].color = color;
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

	size_t vertSize = sizeof(Vertex) * globalVertices.size();
	size_t indexSize = sizeof(uint32_t) * globalIndices.size();
	// Staging buffer
	VkBufferCreateInfo stagingInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	stagingInfo.size = vertSize + indexSize;
	stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo stagingAllocCreateInfo{};
	stagingAllocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	stagingAllocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
	stagingAllocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	VkBuffer stagingBuffer;
	VmaAllocation stagingAllocation;
	VmaAllocationInfo stagingAllocInfo;

	if (vmaCreateBuffer(rt->allocator, &stagingInfo, &stagingAllocCreateInfo, &stagingBuffer, &stagingAllocation, &stagingAllocInfo)
		!= VK_SUCCESS) {
		throw std::runtime_error("failed to create staging buffer!");
	}

	// Create vertex and index buffers
	VkBufferCreateInfo vertInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	vertInfo.size = vertSize;
	vertInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
		| VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

	VkBufferCreateInfo idxInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	idxInfo.size = indexSize;
	idxInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	VmaAllocationCreateInfo allocCreateInfo{}; // Same for both
	allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	if (vmaCreateBuffer(rt->allocator, &vertInfo, &allocCreateInfo, &allocMesh.vertexBuffer.buffer, 
		&allocMesh.vertexBuffer.alloc, &allocMesh.vertexBuffer.allocInfo)
		!= VK_SUCCESS) {
		throw std::runtime_error("failed to create vertex buffer!");
	}

	if (vmaCreateBuffer(rt->allocator, &idxInfo, &allocCreateInfo, &allocMesh.indexBuffer.buffer,
		&allocMesh.indexBuffer.alloc, &allocMesh.indexBuffer.allocInfo)
		!= VK_SUCCESS) {
		throw std::runtime_error("failed to create index buffer!");
	}

	void* startPtr = stagingAllocInfo.pMappedData;
	memcpy(startPtr, globalVertices.data(), vertSize);
	memcpy((char*)startPtr + vertSize, globalIndices.data(), indexSize);

	rt->immediate_submit([&](VkCommandBuffer cmd) {
		VkBufferCopy vertCopy{};
		vertCopy.srcOffset = 0;
		vertCopy.dstOffset = 0;
		vertCopy.size = vertSize;

		vkCmdCopyBuffer(cmd, stagingBuffer, allocMesh.vertexBuffer.buffer, 1, &vertCopy);

		VkBufferCopy idxCopy{};
		idxCopy.srcOffset = vertSize;
		idxCopy.dstOffset = 0;
		idxCopy.size = indexSize;

		vkCmdCopyBuffer(cmd, stagingBuffer, allocMesh.indexBuffer.buffer, 1, &idxCopy);
	});


	vmaDestroyBuffer(rt->allocator, stagingBuffer, stagingAllocation);

	BVHBuildResult blas = bvh::buildBLAS(globalVertices, globalIndices, allocMesh.primitives);
	rt->uploadBLAS(blas);


	rt->deletionQueue.push([=]() {
		vmaDestroyBuffer(rt->allocator, allocMesh.vertexBuffer.buffer, allocMesh.vertexBuffer.alloc);
		vmaDestroyBuffer(rt->allocator, allocMesh.indexBuffer.buffer, allocMesh.indexBuffer.alloc);
		});


	return allocMesh;
}


std::optional<std::vector<AllocatedMesh>> loaders::load_gltf_meshes(const char* filepath, RayTracer* rt) {
	std::filesystem::path fp{ filepath };

	fastgltf::Parser parser{};

	auto gltfFile = fastgltf::MappedGltfFile::FromPath(filepath);

	if (!bool(gltfFile)) {
		std::cout << "FILE NOT FOUND" << '\n';
		return {};
	}

	std::cout << fp.parent_path() << std::endl;
	std::cout << fp << std::endl;

	constexpr auto gltfOptions = fastgltf::Options::GenerateMeshIndices | fastgltf::Options::LoadExternalBuffers
		| fastgltf::Options::DecomposeNodeMatrices;

	auto asset_ret = parser.loadGltf(gltfFile.get(), fp.parent_path(), gltfOptions);

	if (!bool(asset_ret)) {
		std::cout << "ASSET NOT FOUND" << '\n';
		std::cout << std::strerror((int)asset_ret.error()) << '\n';
		return {};
	}

	auto asset = std::move(asset_ret.get());

	std::vector<AllocatedMesh> allocatedMeshes;

	for (const auto& mesh : asset.meshes) {
		std::cout << "ALLOCATING MESH: " << mesh.name << '\n';

		AllocatedMesh allocMesh = allocateMesh(mesh, asset, rt);
		std::cout << "ALLOACTED!" << mesh.name << '\n';

		VkBufferDeviceAddressInfo bda { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		bda.buffer = allocMesh.vertexBuffer.buffer;

		allocMesh.vertexBuffer.bufferAddress = vkGetBufferDeviceAddress(rt->_device, &bda);
		allocatedMeshes.push_back(allocateMesh(mesh, asset, rt));

	}

	return allocatedMeshes;
}

void loaders::load_scene(const char* filepath, RayTracer* rt, Scene& scene) {
	std::filesystem::path fp{ filepath };

	fastgltf::Parser parser{fastgltf::Extensions::KHR_lights_punctual};

	auto gltfFile = fastgltf::MappedGltfFile::FromPath(filepath);

	if (!bool(gltfFile)) {
		std::cout << "FILE NOT FOUND" << '\n';
		return;
	}

	std::cout << fp.parent_path() << std::endl;
	std::cout << fp << std::endl;

	constexpr auto gltfOptions = fastgltf::Options::GenerateMeshIndices | fastgltf::Options::LoadExternalBuffers;
	auto asset_ret = parser.loadGltf(gltfFile.get(), fp.parent_path(), gltfOptions);
	if (!bool(asset_ret)) {
		std::cout << std::filesystem::current_path() << std::endl;

		std::cout << "ASSET NOT FOUND" << '\n';
		std::cout << std::strerror((int)asset_ret.error()) << '\n';
		return;
	}

	auto asset = std::move(asset_ret.get());

	std::vector<Material> materials;
	std::vector<AllocatedMesh> meshes;


	for (const auto& material : asset.materials) {
		Material mat{};
		mat.data.albedo = glm::make_vec4(material.pbrData.baseColorFactor.data());
		mat.data.metallic = material.pbrData.metallicFactor;
		mat.data.smoothness = 1.0f - material.pbrData.roughnessFactor;
		mat.data.refractiveIndex = material.ior;
		mat.data.transmissionFactor = material.transmission ? material.transmission->transmissionFactor : 0.0f;
		mat.data.emissiveStrength = material.emissiveStrength;
		mat.data.emissiveColor = glm::make_vec3(material.emissiveFactor.data());
		mat.name = material.name;
		materials.push_back(mat);
	}

	uint32_t materialOffset = scene.materials.size();

	for (const auto& mesh : asset.meshes) {
		std::cout << "ALLOCATING MESH: " << mesh.name << '\n';
		AllocatedMesh allocMesh = allocateMesh(mesh, asset, rt);

		for (auto i = 0; i < mesh.primitives.size(); i++) {
			if (mesh.primitives[i].materialIndex.has_value()) {
				allocMesh.primitives[i].materialIndex = mesh.primitives[i].materialIndex.value() + materialOffset;
			}
			else {
				allocMesh.primitives[i].materialIndex = 0;
			}
		}
		VkBufferDeviceAddressInfo bda{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		bda.buffer = allocMesh.vertexBuffer.buffer;

		allocMesh.vertexBuffer.bufferAddress = vkGetBufferDeviceAddress(rt->_device, &bda);

		meshes.push_back(allocMesh);


		// Create BLAS structure too

		std::cout << "ALLOACTED!" << mesh.name << '\n';
	}

	
	std::vector<LightObject> lightObjects;

	std::vector <CameraObject> cameras;

	std::vector<MeshObject> meshObjects;


	// Tree traversal over all nodes
	std::vector<bool> isChild(asset.nodes.size(), false);

	for (const auto& node : asset.nodes) {
		for (auto child : node.children) {
			isChild[child] = true;
		}
	}

	std::stack<std::pair<glm::mat4x4, size_t>> dfs{};
	for (auto i = 0; i < asset.nodes.size(); i++) {
		if (!isChild[i]) {
			dfs.push({ glm::mat4x4{1}, i});
		}
	}

	while (!dfs.empty()) {
		auto [t, i] = dfs.top();
		dfs.pop();

		auto node = asset.nodes[i];

		auto local = fastgltf::getLocalTransformMatrix(node);
		glm::mat4x4 localGLM = glm::make_mat4x4(local.data());

		auto transform = t * localGLM;

		glm::vec3 pos{transform[3][0], transform[3][1], transform[3][2]};
		glm::vec3 scale{glm::length(transform[0]), glm::length(transform[1]), glm::length(transform[2])};


		glm::mat3x3 rot { transform[0] / scale[0], transform[1] / scale[1], transform[2] / scale[2] };

		glm::quat quaternion = glm::quat_cast(rot);
		glm::vec3 angles = glm::degrees(glm::eulerAngles(quaternion));

		if (node.meshIndex.has_value()) {
			// Push a mesh object
			MeshObject obj{};
			obj.transform = pos;
			obj.rotation = angles;
			obj.scale = scale;
			obj.mesh = meshes[node.meshIndex.value()];
			obj.name = node.name;
			meshObjects.push_back(obj);
		}
		if (node.lightIndex.has_value()) {
			// Push a light 
			auto light = asset.lights[node.lightIndex.value()];
			if (light.type == fastgltf::LightType::Directional) {
				LightObject obj{};
				obj.transform = pos;
				obj.rotation = angles;
				obj.scale = scale;
				obj.color = glm::make_vec3(light.color.data());
				obj.intensity = light.intensity;
				obj.name = light.name;
				lightObjects.push_back(obj);
			}
		}

		if (node.cameraIndex.has_value()) {
			auto camera = asset.cameras[node.cameraIndex.value()];
			if (std::holds_alternative<fastgltf::Camera::Perspective>(camera.camera)) {
				auto cam = std::get<fastgltf::Camera::Perspective>(camera.camera);
				CameraObject obj{};
				obj.transform = pos;
				obj.rotation = angles;
				obj.scale = scale;
				obj.fov = glm::degrees(cam.yfov);

				obj.far = cam.zfar.has_value() ? cam.zfar.value() : 1000.0f;
				obj.near = cam.znear;
				obj.name = camera.name;
				cameras.push_back(obj);
			}
		}

		for (auto child : node.children) {
			dfs.push({ transform, child });
		}
	}

	if (lightObjects.size() == 0) {
		LightObject defaultLight{};
		defaultLight.color = glm::vec3(1, 1, 1);
		defaultLight.intensity = 1.0f;
		defaultLight.rotation = glm::vec3(0, 0, 0);
		lightObjects.push_back(defaultLight);
	}

	scene.meshes.insert(scene.meshes.end(), meshes.begin(), meshes.end());
	scene.materials.insert(scene.materials.end(), materials.begin(), materials.end());
	scene.objects.insert(scene.objects.end(), meshObjects.begin(), meshObjects.end());
	scene.lights.insert(scene.lights.end(), lightObjects.begin(), lightObjects.end());
	if (!scene.assignedCamera) {
		if (cameras.size() > 0) {
			scene.camera = cameras[0];
			scene.assignedCamera = true;
		}
	}
}
