// ScriptHookV, bound at runtime. Its exports are MSVC-mangled C++ names, which mingw can't link against
// directly, so shv::load() resolves them with GetProcAddress. invoke<R>(hash, args...) calls a native.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <type_traits>

typedef int BOOL_;
typedef uint32_t Hash;
typedef int Any;

// Natives return and take vectors padded to 8 bytes per component.
struct Vector3
{
	alignas(8) float x;
	alignas(8) float y;
	alignas(8) float z;
};
static_assert(sizeof(Vector3) == 24, "Vector3 must match the game's layout");

namespace shv
{
	typedef void (*KeyboardHandler)(DWORD, WORD, BYTE, BOOL, BOOL, BOOL, BOOL);

	extern int (*createTexture)(const char *file);
	extern void (*drawTexture)(int id, int index, int level, int time, float sizeX, float sizeY, float centerX,
	                           float centerY, float posX, float posY, float rotation, float screenHeightScaleFactor,
	                           float r, float g, float b, float a);
	extern void (*scriptWait)(DWORD ms);
	extern void (*scriptRegister)(HMODULE module, void (*main)());
	extern void (*scriptUnregister)(HMODULE module);
	extern void (*keyboardHandlerRegister)(KeyboardHandler handler);
	extern void (*keyboardHandlerUnregister)(KeyboardHandler handler);
	extern void (*nativeInit)(uint64_t hash);
	extern void (*nativePush64)(uint64_t value);
	extern uint64_t *(*nativeCall)();
	extern int (*worldGetAllPeds)(int *arr, int size);
	extern int (*worldGetAllVehicles)(int *arr, int size);
	extern int (*worldGetAllObjects)(int *arr, int size);

	// Resolves every export; false (and a log line) if ScriptHookV.dll is missing or incompatible.
	bool load();

	template <typename T>
	inline void push(T value)
	{
		static_assert(sizeof(T) <= 8, "native arguments are at most 64 bits");
		uint64_t v = 0;
		std::memcpy(&v, &value, sizeof(T));
		nativePush64(v);
	}
}

template <typename R, typename... Args>
inline R invoke(uint64_t hash, Args... args)
{
	shv::nativeInit(hash);
	(shv::push(args), ...);
	if constexpr (std::is_void_v<R>)
		shv::nativeCall();
	else
		return *reinterpret_cast<R *>(shv::nativeCall());
}

inline void WAIT(DWORD ms) { shv::scriptWait(ms); }
