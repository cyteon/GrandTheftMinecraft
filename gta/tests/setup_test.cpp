// Offline test of first-launch setup: mcassets (find/download Minecraft, build assets, textures.bin) and dlcpatch
// (fill dlc.rpf), on a fake game folder. Usage: setup_test <fake game dir>   (needs GrandTheftMinecraft\defs.txt,
// GrandTheftMinecraft\dlc_tex.txt and mods\update\x64\dlcpacks\gtm\dlc.rpf in it)
// Build it -static (like the ASI): otherwise Windows may load another toolchain's libstdc++-6.dll from PATH (Git's)
// and it crashes in the first std::ifstream - that was the old "flaky -O2 crash", never a bug in the mod.
#include "../src/config.h"
#include "../src/dlcpatch.h"
#include "../src/log.h"
#include "../src/mcassets.h"
#include <cstdio>
#include <windows.h>

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *e)
{
	std::fprintf(stderr, "CRASH code %08lx at %p (exe base %p)\n", e->ExceptionRecord->ExceptionCode,
	             e->ExceptionRecord->ExceptionAddress, (void *)GetModuleHandleA(nullptr));
	std::fflush(stderr);
	return EXCEPTION_EXECUTE_HANDLER;
}

int main(int argc, char **argv)
{
	SetUnhandledExceptionFilter(crash_filter);
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	std::string game = argc > 1 ? argv[1] : ".";
	if (game.back() != '\\')
		game += '\\';
	g_dataDir = game + "GrandTheftMinecraft\\";
	log_open(g_dataDir + "gtm.log");
	config_load();
	std::printf("data ready: %d, dlc ready: %d, pending: %d\n", mcassets::data_ready(), dlcpatch::dlc_ready(),
	            mcassets::textures_pending());
	if (mcassets::start_if_needed())
	{
		std::string last;
		while (mcassets::running())
		{
			std::string s = mcassets::status();
			if (s != last)
				std::printf("  %s\n", (last = s).c_str());
			Sleep(100);
		}
		std::printf("setup finished: failed=%d status=%s\n", mcassets::failed(), mcassets::status().c_str());
	}
	// what DllMain does at the next start
	bool patched = dlcpatch::apply_pending();
	std::printf("patched: %d, dlc ready: %d\n", patched, dlcpatch::dlc_ready());
	return 0;
}
