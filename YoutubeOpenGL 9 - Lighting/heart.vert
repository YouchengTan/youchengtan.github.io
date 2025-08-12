#version 330 core
layout (location=0) in vec2 aPos;   // quad in [-1,1]^2
uniform mat4 camMatrix;
uniform vec3 uCenter, uRight, uUp;
uniform float uSize;
out vec2 vUV;
void main(){
    vec3 worldPos = uCenter + uRight*(aPos.x*uSize) + uUp*(aPos.y*uSize);
    vUV = aPos*0.5 + 0.5;
    gl_Position = camMatrix * vec4(worldPos, 1.0);
}
