// Offline check of the 3D hand: fakes ScriptHookV's native calls, records DRAW_POLY triangles to stdout.
// Camera at the origin looking down +Y (GTA heading 0), FOV 50. Usage: hand_test <item> <swing> <equip>
#include "../src/common.h"
#include "../src/config.h"
#include "../src/hand.h"
#include "../src/items.h"
#include <cstdio>
#include <string>
#include <vector>

Frame g;
bool g_mcMode = true, g_debug = false;
namespace r2d { int tex(const std::string &) { return -1; } }

static uint64_t s_hash;
static std::vector<uint64_t> s_args;
static uint64_t s_ret[4];
namespace shv
{
	void (*nativeInit)(uint64_t) = [](uint64_t h) { s_hash = h; s_args.clear(); };
	void (*nativePush64)(uint64_t) = [](uint64_t v) { s_args.push_back(v); };
	uint64_t *(*nativeCall)() = []() -> uint64_t * {
		auto f = [](int i) { float x; std::memcpy(&x, &s_args[i], 4); return x; };
		std::memset(s_ret, 0, sizeof s_ret);
		if (s_hash == 0x65019750A0324133ULL) { float v = 50; std::memcpy(s_ret, &v, 4); }
		if (s_hash == 0xAC26716048436851ULL)
			std::printf("%f %f %f %f %f %f %f %f %f %d %d %d\n", f(0), f(1), f(2), f(3), f(4), f(5), f(6), f(7), f(8),
			            (int)s_args[9], (int)s_args[10], (int)s_args[11]);
		return s_ret;
	};
}

int main(int argc, char **argv)
{
	g_dataDir = "build/GrandTheftMinecraft/";
	g.daylight = 1;
	items_load();
	hand::draw(item_find(argc > 1 ? argv[1] : "diamond_sword"), argc > 2 ? (float)atof(argv[2]) : 1.0f,
	           argc > 3 ? (float)atof(argv[3]) : 1.0f);
}
