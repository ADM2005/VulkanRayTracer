#version 450
#extension GL_EXT_buffer_reference : require

layout(location = 0) in vec3 vertexColor;
layout (location = 0) out vec4 fragColor;

layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 worldPos;

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

vec3 blinnPhong(Material mat, DirectionalLight light, vec3 normal, vec3 viewDir){

    vec3 incomingLight = light.color * light.intensity;

    float shininess = pow(2.0, mat.smoothness * 10.0 + 1.0);

    vec3 diffuseColor = mat.albedo.rgb * (1.0 - mat.metallic);
    vec3 specularColor = mix(vec3(0.04), mat.albedo.rgb, mat.metallic);


    vec3 n = normal;
    vec3 l = -light.direction;
    vec3 v = -viewDir;
    vec3 h = normalize(l + v);
    
    float diffuseFactor = max(0, dot(n, l));
    vec3 diffuseReflection = diffuseColor * diffuseFactor * incomingLight;

    float specularFactor = max(0, dot(n, h));
    float specPower = pow(specularFactor, shininess);
    float norm = (shininess + 8.0) / (8.0 * 3.14159);

    vec3 specularReflection = specularColor * specPower * norm * diffuseFactor * incomingLight;

    return diffuseReflection + specularReflection;
}

void main(){
    Material mat = pc.materialAddr.materials[pc.materialIndex];
    DirectionalLight[8] lights = vUBO.lights;
    int lightCount = vUBO.lightCount;
    vec3 albedo = mat.albedo.rgb;

    mat3 rot = mat3(vUBO.view);
    vec3 d = vUBO.view[3].xyz;

    vec3 cameraPos = -d * rot;

    vec3 view = normalize(worldPos - cameraPos);    
    vec3 n = normalize(normal);

    vec3 color = vec3(0.0);
    for(int i = 0; i < lightCount; i++){
        DirectionalLight light = lights[i];

        color += blinnPhong(mat, light, n, view);
    }
    color += 0.2 * albedo; // ambient
    fragColor = vec4(color, 1.0);    
}