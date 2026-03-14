#!/bin/bash
set -e

echo "=== RPI_MediaServer Build Script ==="

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
echo "Building..."
make -j$(nproc)

echo "Build complete! Binary: ./build/RPI_MediaServer"