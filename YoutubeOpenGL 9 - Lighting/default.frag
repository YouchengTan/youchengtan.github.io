#version 330 core

// Output
out vec4 FragColor;

// From vertex shader
in vec2 texCoord;
in vec3 Normal;
in vec3 crntPos;

// Scene uniforms
uniform sampler2D tex0;
uniform vec4  lightColor;
uniform vec3  lightPos;
uniform vec3  camPos;

// Material uniforms
uniform vec4  jellyTint;
uniform float jellyWrap;       // 0..1 (higher = softer wrap diffuse)
uniform float jellySpecular;   // spec intensity
uniform float jellyShininess;  // spec exponent

// Fog uniforms
uniform float fogAmount;       // 0..1 overall fog intensity
uniform vec3  fogColor;        // fog color (vec3)
uniform float fogStart;        // distance where fog starts
uniform float fogEnd;          // distance where fog is full

uniform int isShadowPass;

void main()
{
    if (isShadowPass == 1) {
        FragColor = vec4(0.0, 0.0, 0.0, 0.40);
        return;
    }
    // Inputs
    vec3 N = normalize(Normal);
    vec3 L = normalize(lightPos - crntPos);
    vec3 V = normalize(camPos   - crntPos);
    vec3 H = normalize(L + V);

    // Base color
    vec4 base = texture(tex0, texCoord) * jellyTint;

    // Lighting terms
    float ambient = 0.18;

    // Wrap diffuse (uses uniform)
    float ndl = dot(N, L);
    float diffuse = max((ndl + jellyWrap) / (1.0 + jellyWrap), 0.0);

    // Specular (uses uniforms)
    float spec = pow(max(dot(N, H), 0.0), jellyShininess) * jellySpecular;

    // Rim & back-scattering (kept as tasteful constants)
    float rimStrength = 0.5;
    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.0) * rimStrength;

    float sssStrength = 0.25;
    float back = max(dot(-N, L), 0.0) * sssStrength;

    // Combine lighting
    vec3 lit = base.rgb * (ambient + diffuse) + spec + rim * base.rgb + back * jellyTint.rgb;
    lit *= lightColor.rgb;

    // Linear fog
    float d = length(camPos - crntPos);
    float t = clamp((d - fogStart) / max(fogEnd - fogStart, 0.0001), 0.0, 1.0);
    t *= fogAmount;

    vec3 fogged = mix(lit, fogColor, t);
    FragColor = vec4(fogged, base.a);
}
