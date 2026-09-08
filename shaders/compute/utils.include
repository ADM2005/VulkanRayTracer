struct Ray{
    vec3 origin;
    vec3 direction;
};

bool intersectTriangle(Ray ray, vec3 v0, vec3 v1, vec3 v2, out float t, out float u, out float v) {
    const float EPSILON = 0.0000001;
    
    vec3 edge1 = v1 - v0;
    vec3 edge2 = v2 - v0;
    
    // Begin calculating determinant - also used to calculate barycentric 'u' coordinate
    vec3 pvec = cross(ray.direction, edge2);
    float det = dot(edge1, pvec);
    
    // If determinant is near zero, ray lies in plane of triangle (or is parallel)
    // For single-sided culling, use: if (det < EPSILON) return false;
    if (abs(det) < EPSILON) {
        return false;
    }
    
    float invDet = 1.0 / det;
    
    // Calculate distance from v0 to ray origin
    vec3 tvec = ray.origin - v0;
    
    // Calculate u parameter and test bound
    u = dot(tvec, pvec) * invDet;
    if (u < 0.0 || u > 1.0) {
        return false;
    }
    
    // Prepare to test v parameter
    vec3 qvec = cross(tvec, edge1);
    
    // Calculate v parameter and test bound
    v = dot(ray.direction, qvec) * invDet;
    if (v < 0.0 || u + v > 1.0) {
        return false;
    }
    
    // Calculate t, the distance along the ray to the intersection point
    t = dot(edge2, qvec) * invDet;
    
    // If t is positive, the ray hit the triangle in the forward direction
    return t > EPSILON;
}