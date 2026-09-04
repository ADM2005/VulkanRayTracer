#version 450

layout(location = 0) out vec3 vertexColor;

const vec3 vertices[3] = { vec3(0,0,0), vec3(0,1,0), vec3(1,0,0) };
const vec3 colors[3] = { vec3(1,0,0), vec3(0,1,0), vec3(0,0,1) };

void main(){
    gl_Position = vec4(vertices[gl_VertexIndex], 1.0);
    vertexColor = colors[gl_VertexIndex];
}