## Lunar Topography Data Setup (LOLA DEM)

Due to its size (~3.3 GB), the NASA LOLA Digital Elevation Model (`ldem_87s_5mpp.tif`) is not bundled inside the repository or the Docker image. 

### 1. Place the DEM file in `datos/`
Download `ldem_87s_5mpp.tif` and place it inside the local `datos/` folder of the project:

```text
Kepler-LunarBooking-EN/
├── datos/
│   ├── sun_earth_ephemerids.txt
│   └── ldem_87s_5mpp.tif   <-- Place the DEM file here
├── Dockerfile
└── docker-compose.yml
