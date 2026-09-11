#include "include/bvh.hpp"

namespace {
	constexpr uint32_t MAX_LEAF_TRIANGLES = 4;
	constexpr int SAH_BUCKET_COUNT = 12;			// Number of possible partition positions to check per axis
	constexpr float COST_TRAVERSAL = 1.0f;			// SAH Hyperparam, cost of traversing
	constexpr float COST_INTERSECT = 1.0f;			// SAH Hyperparam, cost of intersection

	struct AABB {
		glm::vec3 min{ FLT_MAX };
		glm::vec3 max{ FLT_MIN };

		// Grows the AABB with the inclusion of the input point
		void grow(const glm::vec3& p) {
			min = glm::min(min, p);
			max = glm::max(max, p);
		}

		// Grows the AABB by merging with the other AABB
		void grow(const AABB& aabb) {
			grow(aabb.min);
			grow(aabb.max);
		}

		float area() const {
			glm::vec3 extent = max - min;

			if (extent.x < 0 || extent.y < 0 || extent.z < 0) return 0.0f; 
			return 2.0f * (extent.x * extent.y + extent.x * extent.z + extent.y * extent.z);
		}
	};

	AABB triangleBounds(const BVHBuildTriangle& tri) {
		AABB box;
		box.grow(tri.v0);
		box.grow(tri.v1);
		box.grow(tri.v2);
		return box;
	}

	struct Bucket {
		AABB bounds;			// Bounds of this bucket
		uint32_t count = 0;		// Number of triangles in this bucket
	};
}

class BLASBuilder {
public:
	BLASBuilder(std::vector<BVHBuildTriangle>& tris, std::vector<BVHNode>& nodesOut)
		:triangles(tris), nodes(nodesOut) {}

	void build() {
		uint32_t triCount = (uint32_t)triangles.size();
		nodes.resize(triCount > 0 ? triCount * 2 : 1);

		nodesUsed = 1;

		BVHNode& root = nodes[0];
		root.leftFirst = 0;
		root.triCount = triCount;


		updateBounds(0);

		if (triCount > 0) subdivide(0);

		nodes.resize(nodesUsed);
	}

private:

	std::vector<BVHBuildTriangle>& triangles;
	std::vector<BVHNode>& nodes;
	uint32_t nodesUsed{ 0 };

	void updateBounds(uint32_t nodeIdx) {
		BVHNode& node = nodes[nodeIdx];
		uint32_t offset = node.leftFirst;

		AABB box;
		for (uint32_t i = 0; i < node.triCount; i++) {
			box.grow(triangleBounds(triangles[i + offset]));
		}

		node.aabbMin = box.min;
		node.aabbMax = box.max;
	}


	bool findBestSplit(uint32_t start, uint32_t count, int& outAxis, float& outSplitPos, float& outCost) {
		AABB centroidBounds;
		for (uint32_t i = start; i < start + count; i++) {
			centroidBounds.grow(triangles[i].Centroid());
		}

		glm::vec3 extent = centroidBounds.max - centroidBounds.min;

		float bestCost = FLT_MAX;
		float bestPos = 0.0f;
		int bestAxis = -1;

		for (auto axis = 0; axis < 3; axis++) {
			if (extent[axis] < 0.0001f) continue;		// All centroids line up here, possible with flat regions of a mesh. Ignore.

			Bucket buckets[SAH_BUCKET_COUNT];
			float binScale = SAH_BUCKET_COUNT / extent[axis];	// Length in object space of each bucket split

			for (uint32_t i = 0; i < count; i++) {
				const auto& tri = triangles[start + i];
				float c = tri.Centroid()[axis];
				int binIdx = std::min(SAH_BUCKET_COUNT - 1,
					(int)((c - centroidBounds.min[axis]) * binScale));
				buckets[binIdx].count++;
				buckets[binIdx].bounds.grow(triangleBounds(tri));
			}


			// Treat each bucket as its own cuboid, find the number of triangles within them, then
			// cumulatively merge them to get counts per split

			AABB leftBoxes[SAH_BUCKET_COUNT - 1];
			uint32_t leftCounts[SAH_BUCKET_COUNT - 1];
			AABB leftAccum;
			uint32_t leftCountAccum{ 0 };

			for (auto i = 0; i < SAH_BUCKET_COUNT - 1; i++) {
				leftAccum.grow(buckets[i].bounds);
				leftCountAccum += buckets[i].count;

				leftBoxes[i] = leftAccum;
				leftCounts[i] = leftCountAccum;
			}

			AABB rightBoxes[SAH_BUCKET_COUNT - 1];
			uint32_t rightCounts[SAH_BUCKET_COUNT - 1];
			AABB rightAccum;
			uint32_t rightCountAccum{ 0 };

			for (auto i = SAH_BUCKET_COUNT - 2; i >= 0; i--) {
				rightAccum.grow(buckets[i+1].bounds);
				rightCountAccum += buckets[i+1].count;

				rightBoxes[i] = rightAccum;
				rightCounts[i] = rightCountAccum;
			}

			for (int i = 0; i < SAH_BUCKET_COUNT; i++) {
				if (leftCounts[i] == 0 || rightCounts[i] == 0) continue;		// Don't make an empty partition

				float cost = COST_TRAVERSAL + (leftBoxes[i].area() * leftCounts[i] + rightBoxes[i].area() + rightCounts[i]) * COST_INTERSECT;

				if (cost < bestCost) {
					bestCost = cost;
					bestAxis = axis;
					bestPos = centroidBounds.min[axis] + (i + 1) / binScale;
				}
			}
		}

		if (bestAxis == -1) return false;

		outAxis = bestAxis;
		outSplitPos = bestPos;
		outCost = bestCost;

		return true;
		}

	void subdivide(uint32_t nodeIdx) {
		BVHNode& node = nodes[nodeIdx];
		if (node.triCount <= MAX_LEAF_TRIANGLES) {
			return;
		}

		uint32_t start = node.leftFirst;
		uint32_t count = node.triCount;

		int axis;
		float splitPos, splitCost;

		float leafCost = count * COST_INTERSECT;
		bool found = findBestSplit(start, count, axis, splitPos, splitCost);

		if (!found || splitCost >= leafCost) {
			return;
		}

		auto midIt = std::partition(
			triangles.begin() + start,
			triangles.begin() + start + count,
			[axis, splitPos](const BVHBuildTriangle& t) {
				return t.Centroid()[axis] < splitPos;
			}
		);

		uint32_t leftCount = (uint32_t)(midIt - (triangles.begin() + start));

		// Ensure no degenerate partitions, shouldn't be possible but good to fall back

		// Split into two by number of triangles along the selected axis

		if (leftCount == 0 || leftCount == count) {
			leftCount = count / 2;
			std::nth_element(
				triangles.begin() + start,
				triangles.begin() + start + leftCount,
				triangles.begin() + start + count,
				[axis](const BVHBuildTriangle& a, const BVHBuildTriangle& b) {
					return a.Centroid()[axis] < b.Centroid()[axis];
				}
			);
		}

		uint32_t leftIdx = nodesUsed++;
		uint32_t rightIdx = nodesUsed++;

		nodes[leftIdx].leftFirst = start;
		nodes[leftIdx].triCount = leftCount;

		nodes[rightIdx].leftFirst = start + leftCount;
		nodes[rightIdx].triCount = count - leftCount;

		updateBounds(rightIdx);
		updateBounds(leftIdx);

		nodes[nodeIdx].leftFirst = leftIdx;
		nodes[nodeIdx].triCount = 0;			// Indicate no longer a leaf

		subdivide(leftIdx);
		subdivide(rightIdx);

	}

};

BVHBuildResult bvh::buildBLAS(const std::vector<Vertex>& vertexBuffer, const std::vector<uint32_t>& indexBuffer, 
	const std::vector<MeshPrimitive>& primitives) {
	
	size_t triCount = indexBuffer.size() / 3;
	std::vector<BVHTriRef> triangleReferences(triCount);

	uint32_t triRefIndex{ 0 };
	for (const auto& prim : primitives) {
		uint32_t materialIndex = prim.materialIndex;
		uint32_t triRangeStart = prim.firstIndex;
		int32_t offset = prim.vertexOffset;
		uint32_t indexCount = prim.count;

		for (uint32_t i = 0; i < indexCount; i += 3) {
			BVHTriRef ref{};
			ref.indexBufferOffset = triRangeStart + i;
			ref.materialIndex = materialIndex;
			ref.vertexOffset = offset;
			triangleReferences[triRefIndex++] = ref;
		}
	}

	std::vector<BVHBuildTriangle> buildTriangles(triCount);		// CPU only for building the BVH
	uint32_t buildTriangleIndex{ 0 };
	for (const auto& ref : triangleReferences) {
		auto offset = ref.vertexOffset;
		auto idx = ref.indexBufferOffset;
		
		auto i0 = indexBuffer[idx] + offset;
		auto i1 = indexBuffer[idx + 1] + offset;
		auto i2 = indexBuffer[idx + 2] + offset;

		glm::vec3 v0 = vertexBuffer[i0].position;
		glm::vec3 v1 = vertexBuffer[i1].position;
		glm::vec3 v2 = vertexBuffer[i2].position;

		buildTriangles[buildTriangleIndex++] = { v0, v1, v2, ref };
	}

	BVHBuildResult result;
	BLASBuilder builder(buildTriangles, result.nodes);
	builder.build();

	result.triangles.resize(buildTriangles.size());
	for (size_t i = 0; i < buildTriangles.size(); i++) {
		result.triangles[i] = buildTriangles[i].ref;
	}

	return result;
}


