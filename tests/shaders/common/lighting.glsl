// Shared lighting helpers.
vec3 lambert(vec3 n, vec3 l, vec3 albedo) {
    return albedo * max(dot(normalize(n), normalize(l)), 0.0);
}
