#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <functional>

#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

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

struct MeshPrimitive {
	uint32_t firstIndex;
	uint32_t count;
	int32_t vertexOffset;
};

struct AllocatedBuffer {
	VkBuffer buffer;
	VmaAllocation alloc;
	VmaAllocationInfo allocInfo;
};

struct AllocatedMesh {
	AllocatedBuffer vertexBuffer;
	AllocatedBuffer indexBuffer;

	std::vector<MeshPrimitive> primitives;
};