#!/usr/bin/env bash
set -e
export PATH="/c/Users/Admin/.workbuddy/binaries/mingw64/bin:$PATH"
# 2026-08-04 重写：分层窗口 + 软件渲染，不再依赖 DirectX/DComp。
# 源文件直接在 mfbuild 目录下就地编译（源已是 ASCII 路径，避免 ld 中文路径问题）。
BUILD="C:/Users/Admin/.workbuddy/mfbuild"
cd "$BUILD"
FLAGS="-O2 -s -std=c++20 -DNDEBUG -D_WIN32_WINNT=0x0A00 -Wall"
for f in main config effects particles renderer hook ui; do
  echo "-- compile $f"
  g++.exe $FLAGS -c "$f.cpp" -o "$f.o"
done
echo "-- windres"
windres.exe mf.rc -O coff -o mf.res
echo "-- link"
# -static : 把 libgcc/libstdc++/winpthread 静态链接，使 exe 单文件零 MinGW 依赖。
# -mwindows : PE 子系统设为 GUI（2），静默启动不弹控制台。
# -s : 删除调试符号。
# 新架构只用系统 GDI/User32/ComCtl32 等，不再链接 d3d11/d2d1/dcomp/dwrite/dxguid。
g++.exe -o MouseFlow.exe main.o config.o effects.o particles.o renderer.o hook.o ui.o mf.res \
  -static -mwindows -s \
  -lole32 -ldwmapi \
  -lshell32 -luser32 -lgdi32 -lshlwapi -luxtheme -lcomctl32 \
  -lcomdlg32 -ladvapi32 -lkernel32 -lmsimg32 -lgdiplus
echo "-- done"
ls -la MouseFlow.exe
