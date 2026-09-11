#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <functional>
#include <iostream>
#include <numbers>
#include <algorithm>
#include <string>
#include <vk_mem_alloc.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/backends/imgui_impl_sdl3.h>

#define MAX_DIRECTIONAL_LIGHTS 8 

struct GFXPushConstants {
	VkDeviceAddress vertAddress;
	VkDeviceAddress materialAddress;
	VkDeviceAddress meshAddress;
	uint32_t materialIndex;
	uint32_t meshIndex;
};

struct DeletionQueue {
	std::vector<std::function<void()>> _queue;

	void push(std::function<void()> func) {
		_queue.push_back(func);
	}

	void flush() {
		for (auto it = _queue.rbegin(); it != _queue.rend(); it++) {
			(*it)();
		}

		_queue.clear();
	}
};

/*
* Color: 4 floats
* UV: 2 floats
* Normal: 3 floats
* Position: 3 floats
*/

// Packs into 12 floats -> 384 bytes if UVs separated.
struct Vertex{
	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 color;
};

struct DirectionalLight {
	glm::vec3 color;
	float intensity;
	glm::vec3 direction;
	float padding;
};

struct ViewUBO {
	glm::mat4x4 view;
	glm::mat4x4 proj;
	DirectionalLight lights[MAX_DIRECTIONAL_LIGHTS];
	uint32_t lightCount;
	float padding[3];
};


struct MeshUBO {
	glm::mat4x4 model;
	glm::mat4x4 objectToWorldDir;
};

struct ComputeUBO {
	glm::mat4x4 projInv;
	glm::mat4x4 viewInv;
	glm::ivec2 imageSize;
};

struct ComputePC {
	glm::int16 frameNumber;
	glm::int16 scratch;
};


struct MaterialGPU {
	glm::vec4 albedo{ 1.0 };
	float metallic{ 0.0 };
	float smoothness{ 0.5 };
	float refractiveIndex{ 1.5 };
	float transmissionFactor{ 0.0 };
	glm::vec3 emissiveColor{ 0.0 };
	float emissiveStrength{ 0.0 };
};
struct Material {

	MaterialGPU data{};
	std::string name;
	Material() {
		static int instanceCount = 0;
		name = std::to_string(instanceCount);
		instanceCount++;

	}
	void DrawMenu() {
		ImGui::ColorEdit4("Albedo", glm::value_ptr(data.albedo));
		ImGui::DragFloat("Metallic", &data.metallic, 0.1f);
		ImGui::DragFloat("Smoothness", &data.smoothness, 0.1f);
		ImGui::DragFloat("Refractive Index", &data.refractiveIndex, 0.1f);
		ImGui::DragFloat("Transmission Factor", &data.transmissionFactor, 0.1f);

		ImGui::ColorEdit3("Emission Color", glm::value_ptr(data.emissiveColor));
		ImGui::DragFloat("Emission Strength", &data.emissiveStrength, 0.1f);
	}
};


struct MeshPrimitive {
	uint32_t firstIndex;
	uint32_t count;
	int32_t vertexOffset;
	uint32_t materialIndex;
};

struct AllocatedBuffer {
	VkBuffer buffer;
	VmaAllocation alloc;
	VmaAllocationInfo allocInfo;
	std::optional<VkDeviceAddress> bufferAddress;
};

struct AllocatedMesh {
	AllocatedBuffer vertexBuffer;
	AllocatedBuffer indexBuffer;

	std::vector<MeshPrimitive> primitives;
};

struct AllocatedImage {
	VkImage image;
	VkImageView imageView;
	VkExtent2D extent;
	VkFormat format;
	VmaAllocation alloc;
};

class SceneObject {
public:
	std::string name;
	glm::vec3 transform{ 0.0 };
	glm::vec3 rotation{ 0.0 };
	glm::vec3 scale{ 1.0 };

	SceneObject() {
		static int instanceCount = 0;
		name = std::to_string(instanceCount);
		instanceCount++;
	}

	void DrawMenu() {
		ImGui::DragFloat3("Transform", glm::value_ptr(transform), 0.1f);
		ImGui::DragFloat3("Rotation", glm::value_ptr(rotation), 0.1f);
		ImGui::DragFloat3("Scale", glm::value_ptr(scale), 0.1f);
	}

	glm::mat4x4 getTransform() const {
		glm::quat quat{ glm::radians(rotation) };

		glm::mat4 translation = glm::translate(glm::mat4{ 1.0f }, transform);
		glm::mat4 rotation = glm::mat4_cast(quat);
		glm::mat4 scaleT = glm::scale(glm::mat4{ 1.0f }, scale);

		return translation * rotation * scaleT;
	}
};

class MeshObject : public SceneObject {
public:
	std::optional<AllocatedMesh> mesh;
};

class CameraObject : public SceneObject {
public:
	enum CameraType {
		PERSPECTIVE,
		OTHER
	};

	glm::vec3 transform{ 0, 0, 5};
	glm::vec3 rotation{ 0.0 };
	glm::vec3 scale{ 1.0 };

	float fov = 70.0f;
	float near = 0.01f;
	float far = 1000.0f;

	void DrawMenu() {
		ImGui::DragFloat3("Transform", glm::value_ptr(transform), 0.1f);
		ImGui::DragFloat3("Rotation", glm::value_ptr(rotation), 0.1f);

		ImGui::DragFloat("Fov", &fov, 0.1f);
		ImGui::DragFloat("Near", &near, 0.1f);
		ImGui::DragFloat("Far", &far, 0.1f);
	}

	std::pair<glm::mat4, glm::mat4> getViewMatrices(float aspect) {
		glm::quat cameraRotation = glm::quat(glm::radians(rotation));

		glm::mat4 invRotation =
			glm::mat4_cast(glm::inverse(cameraRotation));

		glm::mat4 invTranslation =
			glm::translate(glm::mat4(1.0f), -transform);

		glm::mat4 view =
			invRotation * invTranslation;
		
		glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, far, near); // far/near reversed for reverse-Z
		return { view, proj };
	}
};

class LightObject : public SceneObject {
public:
	glm::vec3 color;
	float intensity;

	void DrawMenu() {
		ImGui::DragFloat3("Rotation", glm::value_ptr(rotation), 0.1f);
		ImGui::ColorEdit3("Color", glm::value_ptr(color));
		ImGui::DragFloat("Intensity", &intensity, 0.1f);
	}
};

struct alignas(16) BVHNode {
	glm::vec3 aabbMin;
	uint32_t  leftFirst;   // internal: index of left child (right = left+1)
	// leaf:     index of first triangle in the index list
	glm::vec3 aabbMax;
	uint32_t  triCount;    // 0 => internal node, >0 => leaf with triCount triangles
};

struct BVHTriRef {
	uint32_t indexBufferOffset; // Index of first vertex in triangle triple (triIndex * 3) for the primitive
	int32_t vertexOffset; // Offset into the vertex buffer (identical to that of the primitive)
	uint32_t materialIndex; // Indexes the global material buffer directly.
};

struct BLASGPU {
	VkDeviceAddress nodeBufferAddress; // BVHNode[] 
	VkDeviceAddress triBufferAddress; // BVHTriRef[]
	VkDeviceAddress vertexBufferAddress; // Same as vertex buffer for mesh
	VkDeviceAddress indexBufferAddress; // Same as index buffer for mesh
};

struct TLASBuildInput {
	glm::mat4x4 model;
	glm::vec3 blasAABBMin;
	glm::vec3 blasAABBMax;
	uint32_t blasIndex;
};

struct alignas(16) TLASInstance {
	glm::mat4 worldToObject;
	glm::mat4 objectToWorld;
	glm::vec3 worldAABBMin;
	uint32_t blasIndex;
	glm::vec3 worldAABBMax;
	uint32_t padding;
};

struct RayTracePC {
	VkDeviceAddress tlasInstanceAddress;	// TLASInstance[], indexed by leaf BVHNodes
	VkDeviceAddress blasTable;				// BLASGPU[]
	VkDeviceAddress materialAddress;		// global material buffer
	uint32_t tlasInstanceCount;				// Number of tlas instances
	uint32_t frameNumber;					// Use for randomisation
	uint32_t sampleCount;					// Determines how to use result in accumulation
	uint32_t maxBounces;					
	uint32_t resetAccumulation;				// Not zero if accumulation buffer reset 
											// When this is active, ignore history image and set directly
};

struct BVHBuildTriangle {
	glm::vec3 v0, v1, v2;				// AABB and centroid checking
	BVHTriRef ref;						// Actual data into the built BVH

	glm::vec3 Centroid() const {

		return (float)(1.0f/3.0f) * (v0 + v1 + v2);
	}
};

struct BVHBuildResult {
	std::vector<BVHNode> nodes;
	std::vector<BVHTriRef> triangles;
};

struct TLASBuildResult {
	std::vector<BVHNode> nodes;
	std::vector<uint32_t> indices;
};