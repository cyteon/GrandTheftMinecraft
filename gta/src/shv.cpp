#include "shv.h"
#include "log.h"

namespace shv
{
	int (*createTexture)(const char *) = nullptr;
	void (*drawTexture)(int, int, int, int, float, float, float, float, float, float, float, float, float, float,
	                    float, float) = nullptr;
	void (*scriptWait)(DWORD) = nullptr;
	void (*scriptRegister)(HMODULE, void (*)()) = nullptr;
	void (*scriptUnregister)(HMODULE) = nullptr;
	void (*keyboardHandlerRegister)(KeyboardHandler) = nullptr;
	void (*keyboardHandlerUnregister)(KeyboardHandler) = nullptr;
	void (*nativeInit)(uint64_t) = nullptr;
	void (*nativePush64)(uint64_t) = nullptr;
	uint64_t *(*nativeCall)() = nullptr;
	int (*worldGetAllPeds)(int *, int) = nullptr;
	int (*worldGetAllVehicles)(int *, int) = nullptr;
	int (*worldGetAllObjects)(int *, int) = nullptr;

	template <typename F>
	static bool bind(HMODULE dll, F &fn, const char *mangled)
	{
		fn = reinterpret_cast<F>(GetProcAddress(dll, mangled));
		if (!fn)
			logf("ScriptHookV export missing: %s", mangled);
		return fn != nullptr;
	}

	bool load()
	{
		HMODULE dll = GetModuleHandleA("ScriptHookV.dll");
		if (!dll)
			dll = LoadLibraryA("ScriptHookV.dll");
		if (!dll)
		{
			logf("ScriptHookV.dll not found next to GTA5.exe");
			return false;
		}
		bool ok = true;
		ok &= bind(dll, createTexture, "?createTexture@@YAHPEBD@Z");
		ok &= bind(dll, drawTexture, "?drawTexture@@YAXHHHHMMMMMMMMMMMM@Z");
		ok &= bind(dll, scriptWait, "?scriptWait@@YAXK@Z");
		ok &= bind(dll, scriptRegister, "?scriptRegister@@YAXPEAUHINSTANCE__@@P6AXXZ@Z");
		ok &= bind(dll, scriptUnregister, "?scriptUnregister@@YAXPEAUHINSTANCE__@@@Z");
		ok &= bind(dll, keyboardHandlerRegister, "?keyboardHandlerRegister@@YAXP6AXKGEHHHH@Z@Z");
		ok &= bind(dll, keyboardHandlerUnregister, "?keyboardHandlerUnregister@@YAXP6AXKGEHHHH@Z@Z");
		ok &= bind(dll, nativeInit, "?nativeInit@@YAX_K@Z");
		ok &= bind(dll, nativePush64, "?nativePush64@@YAX_K@Z");
		ok &= bind(dll, nativeCall, "?nativeCall@@YAPEA_KXZ");
		ok &= bind(dll, worldGetAllPeds, "?worldGetAllPeds@@YAHPEAHH@Z");
		ok &= bind(dll, worldGetAllVehicles, "?worldGetAllVehicles@@YAHPEAHH@Z");
		ok &= bind(dll, worldGetAllObjects, "?worldGetAllObjects@@YAHPEAHH@Z");
		return ok;
	}
}
