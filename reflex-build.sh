#!/usr/bin/env bash
set -e

# echo "[reflex] Regenerating CMake build..."
# cmake -B build/debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON

echo "[reflex] Building LivePostSvc..."
cmake --build build/debug --target LivePostSvc

echo "[reflex] Build complete."
