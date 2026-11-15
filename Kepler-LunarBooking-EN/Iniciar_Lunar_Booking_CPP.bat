@echo off
title NASA LunarBooking - Servidor C++17
cd /d "%~dp0"
echo ============================================================================
echo   NASA LUNARBOOKING - MOTOR DE BUSQUEDA LUNAR EN C++
echo ============================================================================
echo.
if exist "servidor_lunar_booking.exe" goto RUN_SERVER
if not exist "C:\msys64\ucrt64\bin\g++.exe" goto NO_COMPILER
set "PATH=C:\msys64\ucrt64\bin;%PATH%"
echo [1/2] Compilando servidor_lunar_booking.cpp ...
"C:\msys64\ucrt64\bin\g++.exe" -std=c++17 -O2 -static -static-libgcc -static-libstdc++ servidor_lunar_booking.cpp motor_real.cpp fisica/algebra.cpp fisica/maximum_average.cpp -o servidor_lunar_booking.exe -lws2_32 -lshell32
:RUN_SERVER
echo [OK] Abriendo NASA LunarBooking en tu navegador...
start "" "%~dp0index.html"
servidor_lunar_booking.exe --no-browser
goto END
:NO_COMPILER
echo [AVISO] Abriendo interfaz web directamente...
start "" "%~dp0index.html"
:END
