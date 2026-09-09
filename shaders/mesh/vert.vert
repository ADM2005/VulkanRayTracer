#version 450
#extension GL_EXT_buffer_reference : require

layout(location = 0) out vec3 vertexColor;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec3 worldPos;

struct DirectionalLight{
	vec3 color;
	float intensity;
	vec3 direction;
	float padding;
};

layout(set=0, binding=0) uniform viewUBO
{
    mat4 view;
    mat4 proj;
    DirectionalLight[8] lights;
    int lightCount;
    float padding[3];
} vUBO;

struct Vertex{
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
};

struct Material{
	vec4 albedo;
	float metallic;
	float smoothness;
	float refractiveIndex;
	float transmissionFactor;
	vec3 emissiveColor;
	float emissiveStrength;
};

struct Object{
    mat4 model;
    mat4 objectToWorldDir;
};

layout(std430, buffer_reference, buffer_reference_align = 16) readonly buffer VertexBuf {
    Vertex vertices[];
};

layout(std430, buffer_reference, buffer_reference_align = 16) readonly buffer MaterialBuf {
    Material materials[];
};

layout(std430, buffer_reference, buffer_reference_align = 16) readonly buffer ObjectBuf {
    Object objects[];
};


layout(std430, push_constant) uniform Pc {
    VertexBuf vertexAddr;
    MaterialBuf materialAddr;
    ObjectBuf objectAddr;
    uint materialIndex;
    uint objectIndex;
} pc;


void main(){
    Vertex vert = pc.vertexAddr.vertices[gl_VertexIndex];
    Material mat = pc.materialAddr.materials[pc.materialIndex];
    Object obj = pc.objectAddr.objects[pc.objectIndex];

    gl_Position = vUBO.proj * vUBO.view * obj.model * vec4(vert.position, 1.0);
    vertexColor = vert.color.rgb;
    worldPos = vec3(obj.model * vec4(vert.position,  1.0));
    normal = normalize(obj.objectToWorldDir * vec4(vert.normal,0.0)).xyz;
}