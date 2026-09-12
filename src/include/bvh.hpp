#pragma once
#include "include/types.hpp"

// CPU side code for building BVH for triangles as well as creating lists of BVH instances
namespace bvh {
	BVHBuildResult buildBLAS(const std::vector<Vertex>& vertexBuffer, const std::vector<uint32_t>& indexBuffer, const std::vector<MeshPrimitive>& primitives);

	std::vector<TLASInstance> createTLASInstances(const std::vector<TLASBuildInput>& tlasData);
}