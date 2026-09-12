#ifndef RT_TYPES_GLSL
#define RT_TYPES_GLSL

#extension GL_EXT_buffer_reference2 : require

const float EPSILON = 1e-6;

struct Vertex {
    vec3  position;
    float uv_x;
    vec3  normal;
    float uv_y;
    vec4  color;
};

struct Triangle{
    Vertex v1;
    Vertex v2;
    Vertex v3;
};

struct BVHNode {
    vec3 aabbMin;
    uint leftFirst;   // internal: index of left child (right = left+1)
                      // leaf:     index of first triangle/instance in range
    vec3 aabbMax;
    uint triCount;    // 0 => internal node, >0 => leaf with triCount entries
};

struct BVHTriRef {
    uint indexBufferOffset; // absolute offset into the mesh's index buffer
    int  vertexOffset;      // added to each fetched index before indexing Vertex[]
    uint materialIndex;     // absolute index into the global material buffer
};

struct Material {
    vec4  albedo;
    float metallic;
    float smoothness;
    float refractiveIndex;
    float transmissionFactor;
    vec3  emissiveColor;
    float emissiveStrength;
};

struct TLASInstance {
    mat4 worldToObject;
    mat4 objectToWorld;
    vec3 aabbMin;
    uint blasIndex;
    vec3 aabbMax;
    uint padding;
};


layout(buffer_reference, std430, buffer_reference_align = 4) buffer BVHTriRefBuffer {
    BVHTriRef data[];
};

layout(buffer_reference, std430, buffer_reference_align = 16) buffer BVHNodeBuffer {
    BVHNode data[];
};

layout(buffer_reference, std430, buffer_reference_align = 16) buffer VertexBuffer {
    Vertex data[];
};

layout(buffer_reference, std430, buffer_reference_align = 4) buffer IndexBuffer {
    uint data[];
};

struct BLASGPU {
    BVHNodeBuffer   nodes;
    BVHTriRefBuffer triRefs;
    VertexBuffer    vertices;
    IndexBuffer     indices;
};

layout(buffer_reference, std430, buffer_reference_align = 16) buffer TLASInstanceBuffer {
    TLASInstance data[];
};

layout(buffer_reference, std430, buffer_reference_align = 8) buffer BLASTableBuffer {
    BLASGPU data[];
};

layout(buffer_reference, std430, buffer_reference_align = 16) buffer MaterialBuffer {
    Material data[];
};


layout(push_constant, std430) uniform PC {
    TLASInstanceBuffer tlasInstances;   // TLASInstance[], scanned linearly (no TLAS tree yet)
    BLASTableBuffer    blasTable;       // BLASGPU[]
    MaterialBuffer      materials;       // global material buffer
    uint tlasInstanceCount;             // number of valid entries in tlasInstances
    uint frameNumber;                   // used for randomisation
    uint sampleCount;                   // determines how to use result in accumulation
    uint maxBounces;
    uint resetAccumulation;             // non-zero if accumulation buffer reset this frame
};

#endif // RT_TYPES_GLSL
