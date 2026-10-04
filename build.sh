#!/usr/bin/env bash
# Builds build/GrandTheftMinecraft.asi with mingw-w64 (Windows: scoop install mingw; Linux: x86_64-w64-mingw32-g++).
# Then run tools/extract_mc.py once to make build/GrandTheftMinecraft/ (the data folder).
set -euo pipefail
cd "$(dirname "$0")"
CXX="${CXX:-}"
if [ -z "$CXX" ]; then
	if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then CXX=x86_64-w64-mingw32-g++; else CXX=g++; fi
fi
PY="${PY:-$(command -v python3 || command -v python)}"
[ -f gta/src/natives_gen.h ] || "$PY" tools/gen_natives.py
mkdir -p build/obj
OBJS=()
for src in gta/src/*.cpp; do
	obj="build/obj/$(basename "${src%.cpp}").o"
	OBJS+=("$obj")
	# audio.cpp holds miniaudio (4 MB): only rebuild it when it changed
	if [ "$obj" -nt "$src" ] && [ "$obj" -nt gta/src/common.h ] && [ "$src" = gta/src/audio.cpp ]; then continue; fi
	"$CXX" -std=c++20 -O2 -Wall -Wno-unused-function -Wno-missing-braces -Igta/src -c "$src" -o "$obj" &
done
wait
"$CXX" -shared -Wl,--exclude-all-symbols -static -static-libgcc -static-libstdc++ -s -o build/GrandTheftMinecraft.asi "${OBJS[@]}" -lole32 -lwinmm
ls -la build/GrandTheftMinecraft.asi
