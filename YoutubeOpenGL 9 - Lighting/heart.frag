#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform float uAge, uLife, uSeed, uTime;
uniform vec3  uTint;

// Classic implicit heart; < 0 is inside
float heartEq(vec2 p){
    p *= vec2(1.05, 1.20);
    p.y -= 0.10;
    float x2 = p.x*p.x, y2 = p.y*p.y;
    float k  = x2 + y2 - 1.0;
    return k*k*k - x2*p.y*y2; // (x^2+y^2-1)^3 - x^2 y^3
}

void main(){
    // map to [-1,1]^2
    vec2 p = vUV*2.0 - 1.0;

    // gentle spin so silhouettes vary
    float ang = uSeed*6.28318 + 0.6*uTime;
    mat2 R = mat2(cos(ang), -sin(ang), sin(ang), cos(ang));
    p = R * p;

    float h = heartEq(p);

    // Screen-space anti-aliased edge (fixes the “circle” look)
    float edge = max(0.001, 1.5 * length(fwidth(p)));
    float alpha = 1.0 - smoothstep(0.0, edge, h);   // 1 inside, 0 outside

    // Fade with age
    float age01 = clamp(uAge/uLife, 0.0, 1.0);
    alpha *= (1.0 - age01);

    if (alpha < 0.02) discard;

    // slight highlight
    vec3 col = mix(uTint, vec3(1.0), 0.15);
    FragColor = vec4(col, alpha);
}
