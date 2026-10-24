@echo off
echo =======================================================
echo Compiling Butterflow: Universal Control for Windows
echo Toolchain: MinGW-w64 G++ (C++17, Optimized -O3)
echo =======================================================

g++ -std=c++17 -O3 ^
    -Iinclude ^
    src/main.cpp ^
    src/edge_overlay.cpp ^
    src/edge_physics.cpp ^
    src/input_capturer.cpp ^
    src/input_injector.cpp ^
    src/network_manager.cpp ^
    -o butterflow.exe ^
    -lws2_32 -lgdiplus -lgdi32 -ldwmapi ^
    -static-libgcc -static-libstdc++

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] butterflow.exe built successfully!
) else (
    echo [FAILED] Compilation failed.
)
