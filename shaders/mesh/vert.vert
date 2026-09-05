#version 450
#extension GL_EXT_buffer_reference : require

layout(location = 0) out vec3 vertexColor;

layout(set=0, binding=0) uniform viewUBO
{
    mat4 view;
    mat4 proj;
} vUBO;

layout(set=1, binding=0) uniform meshUBO
{
    mat4 model;
} mUBO;

struct Vertex{
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
};

layout(std430, buffer_reference, buffer_reference_align = 16) readonly buffer VertexBuf {
    Vertex vertices[];
};

layout(std430, push_constant) uniform Pc {
    VertexBuf ptr;
} pc;


void main(){
    Vertex vert = pc.ptr.vertices[gl_VertexIndex];
    gl_Position = vUBO.proj * vUBO.view * mUBO.model * vec4(vert.position, 1.0);
    //gl_Position = vec4(vert.position, 1.0);
    vertexColor = vert.normal;
}