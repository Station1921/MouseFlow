#!/usr/bin/env bash
set -e
export PATH="/c/Users/Admin/.workbuddy/binaries/mingw64/bin:$PATH"
BUILD="C:/Users/Admin/.workbuddy/mfbuild"
cd "$BUILD"
FLAGS="-O2 -s -std=c++20 -DNDEBUG -D_WIN32_WINNT=0x0A00 -Wall"
for f in main config effects particles renderer hook ui; do
  echo "-- compiling $f.cpp"
  g++.exe $FLAGS -c "$f.cpp" -o "$f.o"
done
echo "-- windres"
windres.exe mf.rc -O coff -o mf.res
echo "-- link"
g++.exe -o MouseFlow.exe main.o config.o effects.o particles.o renderer.o hook.o ui.o mf.res \
  -static-libgcc -static-libstdc++ \
  -ld3d11 -ldxgi -ld2d1 -ldcomp -ldwrite -ldxguid -lole32 -ldwmapi \
  -lshell32 -luser32 -lgdi32 -lshlwapi -luxtheme -lcomctl32 \
  -lcomdlg32 -ladvapi32 -lkernel32 -lmsimg32 -lgdiplus
echo "-- done"
ls -la MouseFlow.exe
