#pragma once
#include <math.h>
#include "algebra.hpp"
#include <vector>

using namespace std;

double illuminationAngle(lunar::Vec3 lightDirection, Triangle t);

bool intersectsPolyhedron(lunar::Vec3 Q1, lunar::Vec3 Q2, vector<Triangle> polyhedron, int numTriangles);

double illumination(lunar::Vec3 SunPosition, lunar::Vec3 MoonPosition, vector<Triangle> polyhedron, int numTriangles);

double communication(lunar::Vec3 EarthPosition, lunar::Vec3 MoonPosition, vector<Triangle> polyhedron, int numTriangles);