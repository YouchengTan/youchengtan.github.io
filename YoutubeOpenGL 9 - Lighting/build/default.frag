#version 330 core

// Outputs colors in RGBA
out vec4 FragColor;


// Imports the texture coordinates from the Vertex Shader
in vec2 texCoord;
// Imports the normal from the Vertex Shader
in vec3 Normal;
// Imports the current position from the Vertex Shader
in vec3 crntPos;

// Gets the Texture Unit from the main function
uniform sampler2D tex0;
// Gets the color of the light from the main function
uniform vec4 lightColor;
// Gets the position of the light from the main function
uniform vec3 lightPos;
// Gets the position of the camera from the main function
uniform vec3 camPos;

uniform vec4  jellyTint;

void main()
{
	// inputs
	vec3 N = normalize(Normal);
    vec3 L = normalize(lightPos - crntPos);
    vec3 V = normalize(camPos   - crntPos);
    vec3 H = normalize(L + V);

	// jellyish lighting generating
	float ambient = 0.18f;

	// soften the denominator
	const float kWrap = 0.7; // 0..1 (higher = softer)
    float ndl     = dot(N, L);
    float diffuse = max((ndl + kWrap) / (1.0 + kWrap), 0.0);

	// Blong phong
	const float shininess    = 48.0;
    const float specStrength = 0.25;
    float spec = pow(max(dot(N, H), 0.0), shininess) * specStrength;

	// gummy light
	const float rimStrength = 0.5;
    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.0) * rimStrength;

	// back-scattering
	const float sssStrength = 0.25;
    float back = max(dot(-N, L), 0.0) * sssStrength;

	// base color
	vec4 base = texture(tex0, texCoord) * jellyTint;

	vec3 lit = base.rgb * (ambient + diffuse) + spec  + rim  * base.rgb + back * jellyTint.rgb;
	lit *= lightColor.rgb;
	FragColor = vec4(lit, base.a);
}