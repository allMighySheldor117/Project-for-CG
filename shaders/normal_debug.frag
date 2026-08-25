#version 330 core

in vec3 normal_color;

uniform vec3 u_albedo;

out vec4 fragment_color;

void main()
{
    vec3 debug_color = mix(u_albedo, normal_color, 0.38);
    fragment_color = vec4(debug_color, 1.0);
}
