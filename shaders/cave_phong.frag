#version 330 core

const int maximum_point_lights = 8;
const int rock_material = 0;
const int wood_material = 1;
const int untextured_material = 2;
const int basalt_material = 3;
const int lava_material = 4;
const int wet_rock_material = 5;
const int shallow_water_material = 6;
const int deep_water_material = 7;
const int soil_material = 8;
const int bark_material = 9;
const int aether_material = 10;
const int mist_material = 11;
const int water_marble_material = 12;
const float minimum_weight_sum = 0.000001;

struct PointLight {
    vec3 position;
    vec3 color;
    float intensity;
    float attenuation_constant;
    float attenuation_linear;
    float attenuation_quadratic;
    float range_metres;
};

in vec3 world_position;
in vec3 world_normal;
in vec2 texture_coordinates;

uniform vec3 u_camera_position;
uniform vec3 u_albedo;
uniform vec3 u_material_ambient;
uniform vec3 u_material_diffuse;
uniform vec3 u_material_specular;
uniform vec3 u_material_emission;
uniform float u_alpha;
uniform float u_material_shininess;
uniform float u_texture_scale;
uniform float u_triplanar_sharpness;
uniform float u_time_seconds;
uniform int u_material_kind;
uniform sampler2D u_rock_texture;
uniform sampler2D u_wood_texture;
uniform int u_point_light_count;
uniform PointLight u_point_lights[maximum_point_lights];
uniform vec3 u_fog_color;
uniform float u_fog_start;
uniform float u_fog_end;

out vec4 fragment_color;

float rock_noise(vec3 normal)
{
    vec3 weights = pow(max(abs(normal), vec3(minimum_weight_sum)),
        vec3(u_triplanar_sharpness));
    weights /= max(weights.x + weights.y + weights.z, minimum_weight_sum);
    float x_projection = texture(u_rock_texture, world_position.yz * u_texture_scale).r;
    float y_projection = texture(u_rock_texture, world_position.xz * u_texture_scale).r;
    float z_projection = texture(u_rock_texture, world_position.xy * u_texture_scale).r;
    return dot(vec3(x_projection, y_projection, z_projection), weights);
}

vec3 surface_color(vec3 normal)
{
    float noise = rock_noise(normal);
    if (u_material_kind == rock_material) {
        return u_albedo * mix(0.54, 1.16, noise);
    }
    if (u_material_kind == untextured_material) {
        return u_albedo;
    }
    if (u_material_kind == basalt_material) {
        float pulse = 0.5 + 0.5 * sin(u_time_seconds * 1.4
            + world_position.x * 1.2 + world_position.z * 1.5);
        float cracks = smoothstep(0.69, 0.86, noise + pulse * 0.10);
        return mix(u_albedo * mix(0.22, 0.58, noise),
            vec3(1.0, 0.12, 0.006), cracks);
    }
    if (u_material_kind == lava_material) {
        float flow = 0.5 + 0.5 * sin(world_position.x * 1.8
            + world_position.z * 1.35 + u_time_seconds * 1.7);
        return mix(vec3(0.42, 0.012, 0.001), vec3(1.0, 0.32, 0.008),
            clamp(noise * 0.62 + flow * 0.55, 0.0, 1.0));
    }
    if (u_material_kind == wet_rock_material) {
        return u_albedo * mix(0.30, 0.78, noise);
    }
    if (u_material_kind == water_marble_material) {
        float broad_vein = abs(sin(world_position.x * 0.34
            + world_position.y * 0.18 + world_position.z * 0.27
            + noise * 4.8));
        float fine_vein = abs(sin(world_position.x * 0.91
            - world_position.z * 0.73 + noise * 8.0));
        float veins = smoothstep(0.82, 0.97,
            max(broad_vein, fine_vein * 0.88));
        vec3 cool_stone = u_albedo * mix(0.70, 1.08, noise);
        vec3 pale_vein = vec3(0.78, 0.91, 0.98);
        return mix(cool_stone, pale_vein, veins * 0.72);
    }
    if (u_material_kind == shallow_water_material
        || u_material_kind == deep_water_material) {
        float ripple = 0.5 + 0.5 * sin((texture_coordinates.x
            + texture_coordinates.y) * 24.0 + u_time_seconds * 1.3
            + noise * 4.0);
        float depth_tint = u_material_kind == deep_water_material ? 0.42 : 0.78;
        return u_albedo * mix(depth_tint, 1.12, ripple);
    }
    if (u_material_kind == soil_material) {
        float mineral = smoothstep(0.72, 0.90, noise);
        return mix(u_albedo * mix(0.45, 0.92, noise),
            vec3(0.48, 0.58, 0.42), mineral * 0.55);
    }
    if (u_material_kind == aether_material) {
        float pulse = 0.72 + 0.28 * sin(
            u_time_seconds * 1.6 + world_position.y * 1.3);
        return u_albedo * mix(0.62, 1.24, noise) * pulse;
    }
    if (u_material_kind == mist_material) {
        float drift = 0.72 + 0.28 * sin(world_position.x * 0.35
            + world_position.z * 0.28 + u_time_seconds * 0.22);
        return u_albedo * drift;
    }
    vec2 grain_coordinates = texture_coordinates * vec2(u_texture_scale, 1.0);
    vec3 grain = texture(u_wood_texture, grain_coordinates).rgb;
    if (u_material_kind == bark_material) {
        grain *= 0.72 + 0.28 * sin(world_position.y * 2.5 + noise * 3.0);
    }
    return u_albedo * grain;
}

void main()
{
    vec3 normal = normalize(world_normal);
    vec3 view_direction = normalize(u_camera_position - world_position);
    vec3 albedo = surface_color(normal);
    vec3 linear_color = u_material_ambient * albedo + u_material_emission;

    for (int index = 0; index < u_point_light_count; ++index) {
        PointLight light = u_point_lights[index];
        vec3 to_light = light.position - world_position;
        float distance_squared = dot(to_light, to_light);
        if (distance_squared >= light.range_metres * light.range_metres) {
            continue;
        }
        float distance_metres = sqrt(distance_squared);
        vec3 light_direction = distance_metres > minimum_weight_sum
            ? to_light / distance_metres : normal;
        float diffuse_amount = max(dot(normal, light_direction), 0.0);
        vec3 reflected_direction = reflect(-light_direction, normal);
        float specular_amount = diffuse_amount > 0.0
            ? pow(max(dot(view_direction, reflected_direction), 0.0), u_material_shininess)
            : 0.0;
        float attenuation_denominator = light.attenuation_constant
            + light.attenuation_linear * distance_metres
            + light.attenuation_quadratic * distance_metres * distance_metres;
        float local_cutoff = 1.0 - smoothstep(
            light.range_metres * 0.72, light.range_metres, distance_metres);
        float attenuation = local_cutoff / max(attenuation_denominator, minimum_weight_sum);
        vec3 diffuse = u_material_diffuse * albedo * diffuse_amount;
        vec3 specular = u_material_specular * specular_amount;
        linear_color += (diffuse + specular) * light.color * light.intensity * attenuation;
    }

    float camera_distance = length(u_camera_position - world_position);
    float fog = clamp((camera_distance - u_fog_start) / max(u_fog_end - u_fog_start,
        minimum_weight_sum), 0.0, 1.0);
    linear_color = mix(linear_color, u_fog_color, fog);
    float alpha = clamp(u_alpha, 0.0, 1.0);
    fragment_color = vec4(max(linear_color, vec3(0.0)) * alpha, alpha);
}
