#include <math.h>
#include "algebra.hpp"
#include "illumination.hpp"

using namespace std;




double illuminationAngle(lunar::Vec3 lightDirection, Triangle t) {
    return abs(acos(vectorDotProduct(lightDirection, normalVectorTriangle(t))));
};

bool intersectsPolyhedron(lunar::Vec3 Q1, lunar::Vec3 Q2, vector<Triangle> polyhedron, int numTriangles){
    for (int i = 0; i < numTriangles; i++) {
        if (intersectsTriangle(Q1, Q2, polyhedron[i])) {
            return true;
        }
    }
    return false;
};

//We assume that polyhedron[0] is the face that the base is standing on
double illumination(lunar::Vec3 SunPosition, lunar::Vec3 MoonPosition, vector<Triangle> polyhedron, int numTriangles) {
    lunar::Vec3 lightDirection = normalize({SunPosition.x - MoonPosition.x, SunPosition.y - MoonPosition.y, SunPosition.z - MoonPosition.z});
    // Angle of incidence: angle between the direction to the Sun and the normal of the
    // face. The mesh normals point skyward (see lunar::Patch::triangles()).
    double angle = illuminationAngle(lightDirection, polyhedron[0]);
    // The illumination is proportional to the cosine of the angle of incidence:
    //   1 = light normal to the face (angle = 0), 0 = grazing light or shadow.
    // (It used to return the angle itself, in radians: ~1.55 for a Sun 1 deg above
    //  the face, which is more than "100 %".)
    double cosIncidence = cos(angle);
    if (cosIncidence <= 0.0) {
        return 0.0f; // The Sun is behind the face: the face shadows itself
    }
    // Elevate the MoonPosition slightly along the normal of the triangle (toward the sky) so
    // that the segment to the Sun does not intersect the very face the base is standing on.
    // (It used to be moved to the side opposite to the Sun, so the segment always crossed
    //  that face and everything came out as shadowed.)
    lunar::Vec3 elevatedMoonPosition = vectorAddition(MoonPosition, productByScalar(normalVectorTriangle(polyhedron[0]), 0.01f));
    if (intersectsPolyhedron(elevatedMoonPosition, SunPosition, polyhedron, numTriangles)) {
        return 0.0f; // Shadowed
    } else {
        return cosIncidence; // Illuminated: value in (0, 1]
    }
};

double communication(lunar::Vec3 EarthPosition, lunar::Vec3 MoonPosition, vector<Triangle> polyhedron, int numTriangles) {
    // Same small elevation as in illumination(), so that the segment to the Earth does not
    // intersect the very face the base is standing on (polyhedron[0])
    lunar::Vec3 elevatedMoonPosition = vectorAddition(MoonPosition, productByScalar(normalVectorTriangle(polyhedron[0]), 0.01f));
    if (intersectsPolyhedron(elevatedMoonPosition, EarthPosition, polyhedron, numTriangles)) {
        return 0.0f; // Blocked
    } else {
        return 1.0f; // Clear line of sight
    }
};