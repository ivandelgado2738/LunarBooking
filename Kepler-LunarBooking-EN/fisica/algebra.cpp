#include <math.h>
#include "algebra.hpp"

double vectorNorm(lunar::Vec3 v) {
    return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
};

double vectorDotProduct(lunar::Vec3 v1, lunar::Vec3 v2) {
    return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
};

lunar::Vec3 vectorAddition(lunar::Vec3 v1, lunar::Vec3 v2) {
    return {v1.x + v2.x, v1.y + v2.y, v1.z + v2.z};
};


lunar::Vec3 vectorSubstraction(lunar::Vec3 v1, lunar::Vec3 v2) {
    return {v1.x - v2.x, v1.y - v2.y, v1.z - v2.z};
};

lunar::Vec3 productByScalar(lunar::Vec3 v, double scalar) {
    return {v.x * scalar, v.y * scalar, v.z * scalar};
};

lunar::Vec3 vectorCrossProduct(lunar::Vec3 v1, lunar::Vec3 v2) {
    return {v1.y * v2.z - v1.z * v2.y,
            v1.z * v2.x - v1.x * v2.z,
            v1.x * v2.y - v1.y * v2.x};
};

lunar::Vec3 normalize(lunar::Vec3 v) {
    double norm = vectorNorm(v);
    return {v.x / norm, v.y / norm, v.z / norm};
};


double determinant3x3(Matrix3x3 m) {
    return m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) -
           m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]) +
           m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
};

double determinant4x4(Matrix4x4 m) {
    double det = 0.0f;
    for (int i = 0; i < 4; ++i) {
        Matrix3x3 subMatrix;
        for (int j = 1; j < 4; ++j) {
            int colIndex = 0;
            for (int k = 0; k < 4; ++k) {
                if (k == i) continue;
                subMatrix.m[j - 1][colIndex] = m.m[j][k];
                colIndex++;
            }
        }
        det += (i % 2 == 0 ? 1 : -1) * m.m[0][i] * determinant3x3(subMatrix);
    }
    return det;
};

lunar::Vec3 normalVectorTriangle(Triangle t) {
    lunar::Vec3 edge1 = {t.v2.x - t.v1.x, t.v2.y - t.v1.y, t.v2.z - t.v1.z};
    lunar::Vec3 edge2 = {t.v3.x - t.v1.x, t.v3.y - t.v1.y, t.v3.z - t.v1.z};
    lunar::Vec3 normal = vectorCrossProduct(edge1, edge2);
    return normalize(normal);
};

int sign(double f){
    if(f < 0) return -1;
    if(f > 0) return 1;
    return 0;   
};

double signedTetrahedronVolume (lunar::Vec3 v, Triangle t){
    // The volume does not change if the four points are translated together, so the
    // coordinates are taken relative to t.v1. With the absolute MOON_ME coordinates
    // (~1.7e6 m) the rounding error of the determinant is larger than the volume of a
    // tetrahedron a few meters wide, and its sign came out at random.
    lunar::Vec3 o = t.v1;
    Matrix4x4 m;
    m.m[0][0] = v.x - o.x; m.m[0][1] = v.y - o.y; m.m[0][2] = v.z - o.z; m.m[0][3] = 1.0f;
    m.m[1][0] = 0.0f; m.m[1][1] = 0.0f; m.m[1][2] = 0.0f; m.m[1][3] = 1.0f;
    m.m[2][0] = t.v2.x - o.x; m.m[2][1] = t.v2.y - o.y; m.m[2][2] = t.v2.z - o.z; m.m[2][3] = 1.0f;
    m.m[3][0] = t.v3.x - o.x; m.m[3][1] = t.v3.y - o.y; m.m[3][2] = t.v3.z - o.z; m.m[3][3] = 1.0f;

    return determinant4x4(m) / 6.0f;
};

bool intersectsTriangle(lunar::Vec3 Q1, lunar::Vec3 Q2,Triangle t) {
    int i,j,k;
    // The segment can only intersect the triangle if Q1 and Q2 lie on opposite sides of
    // its plane. Without this check the three tests below detect the intersection with the
    // whole straight line through Q1 and Q2, also behind Q1 (i.e. under the ground).
    if (sign(signedTetrahedronVolume(Q1, t)) * sign(signedTetrahedronVolume(Q2, t)) > 0) return false;
    Triangle t1, t2, t3;
    t1 = {t.v1, Q1, t.v2};
    t2 = {t.v3, t.v2, Q1};
    t3 = {t.v1, t.v3, Q1};
    i = sign(signedTetrahedronVolume(Q2, t1));
    j = sign(signedTetrahedronVolume(Q2, t2));
    k = sign(signedTetrahedronVolume(Q2, t3));
    if(i == j && j == k && k == 1) return true;
    else if(i == j && j == k && k == -1) return true;
    else return false;
};

lunar::Vec3 cartesianToSpherical(lunar::Vec3 v) {
    double r = vectorNorm(v);
    double theta = acos(v.z / r); // polar angle (rad)
    double phi = atan2(v.y, v.x); // azimuthal angle (rad)
    return {r, theta, phi};
};

lunar::Vec3 sphericalToCartesian(lunar::Vec3 v) {
    double r = v.x;
    double theta = v.y; // polar angle (rad)
    double phi = v.z; // azimuthal angle (rad)
    return {r * sin(theta) * cos(phi), r * sin(theta) * sin(phi), r * cos(theta)};
};

lunar::Vec3 latitudeLongitudeAltitudeToCartesian(double latitude, double longitude, double altitude) {
    double latRad = latitude * M_PI / 180.0f;
    double lonRad = longitude * M_PI / 180.0f;
    double r = 1737.4f + altitude; // Moon's radius in km + altitude
    return {r * cos(latRad) * cos(lonRad), r * cos(latRad) * sin(lonRad), r * sin(latRad)};
};