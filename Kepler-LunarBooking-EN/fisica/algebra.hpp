#pragma once
#include <math.h>
#include <vector>
#include "lunar_dem.hpp"

struct Triangle {
    lunar::Vec3 v1, v2, v3;
};

struct Matrix3x3 {
    double m[3][3];
};

struct Matrix4x4 {
    double m[4][4];
};

double vectorNorm(lunar::Vec3 v);

double vectorDotProduct(lunar::Vec3 v1, lunar::Vec3 v2);

lunar::Vec3 vectorAddition(lunar::Vec3 v1, lunar::Vec3 v2);

lunar::Vec3 vectorSubstraction(lunar::Vec3 v1, lunar::Vec3 v2);

lunar::Vec3 productByScalar(lunar::Vec3 v, double scalar);

lunar::Vec3 vectorCrossProduct(lunar::Vec3 v1, lunar::Vec3 v2);

lunar::Vec3 normalize(lunar::Vec3 v);


double determinant3x3(Matrix3x3 m);

double determinant4x4(Matrix4x4 m);

lunar::Vec3 normalVectorTriangle(Triangle t);

int sign(double f);

double signedTetrahedronVolume (lunar::Vec3 v, Triangle t);

bool intersectsTriangle(lunar::Vec3 Q1, lunar::Vec3 Q2, Triangle t);

lunar::Vec3 cartesianToSpherical(lunar::Vec3 v);

lunar::Vec3 sphericalToCartesian(lunar::Vec3 v);

lunar::Vec3 latitudeLongitudeAltitudeToCartesian(double latitude, double longitude, double altitude);