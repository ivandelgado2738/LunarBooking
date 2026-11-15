#include <string>
#include "algebra.hpp"
#include "illumination.hpp"
#include "polyhedron.hpp"
#include <stdio.h>
#include <vector>
#include "lunar_dem.hpp"


#define nyears 11
#define ndaysperyear 365
#define nrowsperday 96
//#define MOON_RADIUS 1737.4000

using namespace std;

vector<Triangle> trianglesFromPatch(const lunar::Patch& patch) {
    vector<Triangle> polyhedron;
    vector<lunar::Triangle> triangles = patch.triangles();
    polyhedron.reserve(triangles.size());
    for (const lunar::Triangle& triangle : triangles) {
        polyhedron.push_back({
            patch.xyz[triangle.a],
            patch.xyz[triangle.b],
            patch.xyz[triangle.c]
        });
    }
    return polyhedron;
};

int main(){
    double lon, lat;
    FILE *f = fopen("sun_earth_ephemerids.txt", "r");
    if(f == NULL){
        printf("Error opening file sun_earth_ephemerids.txt\n");
        return 1;
    };
    FILE *res = fopen("results.txt", "w");
    if(res == NULL){
        printf("Error opening file results.txt\n");
        return 1;
    };
    lunar::Dem dem("ldem_87s_5mpp.tif");

    lunar::Patch patch = dem.region_lat_lon(lat, lon, 1000.0, 1);

    lunar::Vec3 MoonPosition = patch.point(patch.rows/2, patch.columns/2);

    vector<Triangle> triangles = trianglesFromPatch(patch);

    //AQUÍ HAY QUE HACER QUE triangles [0] SEA LA CARA EN LA QUE ESTAMOS

    int triangleCount = triangles.size();

    for(int i = 0; i < nyears * ndaysperyear * nrowsperday; i++){
        double time;

        char * date;

        lunar::Vec3 SunPosition, EarthPosition;

        fscanf(f, "%lf %s %lf %lf %lf %lf %lf %lf", &time, &date,
               &EarthPosition.x, &EarthPosition.y, &EarthPosition.z,
               &SunPosition.x, &SunPosition.y, &SunPosition.z
               );
            
            EarthPosition = productByScalar(EarthPosition, 1000.0f);
            SunPosition = productByScalar(SunPosition, 1000.0f); //We write the coordinates in meters

            double illuminationValue = illumination(SunPosition, MoonPosition, triangles, triangleCount);
            double communicationValue = communication(EarthPosition, MoonPosition, triangles, triangleCount);

            fprintf(res, "%.2f\t%.6f\t%.1f\n", time, illuminationValue, communicationValue);
        
    };

    fclose(f);
    fclose(res);

    /*FILE *f = fopen("sun_earth_ephemerids.txt", "r");
    FILE *g = fopen("moon_position.txt", "r");
    FILE *h = fopen("moon_polyhedron.txt", "r");
    FILE *res = fopen("results.txt", "w");

    int i;
    int Nsides;
    Triangle polyhedron = readPolyhedron(h);
    double latitude, longitude, altitude;
    readPosition(g, latitude, longitude);
    altitude = readAltitude(g, latitude, longitude);
    lunar::Vec3 MoonPosition = latitudeLongitudeAltitudeToCartesian(latitude, longitude, altitude);
    // Position in Moon, cartesian

    fprintf(res, "Time (s)\tRelative Illumination\tCommunication (1=clear, 0=blocked)\n");

    for(i = 0; i < nyears * ndaysperyear * nrowsperday; i++){
        double time;

        char * date;

        lunar::Vec3 SunPosition, EarthPosition;

        fscanf(f, "%lf %s %lf %lf %lf %lf %lf %lf", &time, &date,
               &EarthPosition.x, &EarthPosition.y, &EarthPosition.z,
               &SunPosition.x, &SunPosition.y, &SunPosition.z
               );

        double illuminationValue = illumination(SunPosition, MoonPosition, polyhedron, Nsides);
        double communicationValue = communication(EarthPosition, MoonPosition, polyhedron, Nsides);

        fprintf(res, "%.2f\t%.6f\t%.1f\n", time, illuminationValue, communicationValue);
    };

    fclose(h);
    fclose(g);
    fclose(f);
    fclose(res);*/
}