#!/usr/bin/env bash
# Builds build/GrandTheftMinecraft.asi with mingw-w64 (Windows: scoop install mingw; Linux: x86_64-w64-mingw32-g++).
set -euo pipefail
cd "$(dirname "$0")"
CXX="${CXX:-}"
CC="${CC:-}"
if [ -z "$CXX" ]; then
	if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then CXX=x86_64-w64-mingw32-g++; CC=x86_64-w64-mingw32-gcc; else CXX=g++; CC=gcc; fi
fi
CC="${CC:-gcc}"
PY="${PY:-$(command -v python3 || command -v python)}"
[ -f gta/src/natives_gen.h ] || "$PY" tools/gen_natives.py
mkdir -p build/obj
OBJS=()
pids=()
for src in gta/src/*.cpp gta/vendor/miniz.c; do
	base="$(basename "$src")"
	obj="build/obj/${base%.*}.o"
	OBJS+=("$obj")
	# the big single-file libraries only rebuild when they changed
	if [ "$obj" -nt "$src" ] && { [ "$base" = audio.cpp ] || [ "$base" = miniz.c ] || [ "$base" = mcassets.cpp ]; } && [ "$obj" -nt gta/src/common.h ]; then continue; fi
	if [ "${src##*.}" = c ]; then
		"$CC" -O2 -c "$src" -o "$obj" &
	else
		"$CXX" -std=c++20 -O2 -Wall -Wno-unused-function -Wno-missing-braces -Igta/src -c "$src" -o "$obj" &
	fi
	pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
"$CXX" -shared -Wl,--exclude-all-symbols -static -static-libgcc -static-libstdc++ -s -o build/GrandTheftMinecraft.asi \
	"${OBJS[@]}" -lole32 -lwinmm -lwinhttp -lbcrypt
ls -la build/GrandTheftMinecraft.asi
