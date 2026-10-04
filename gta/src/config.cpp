#include "config.h"
#include "log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <windows.h>

Config g_cfg;
std::string g_dataDir;

static const char *DEFAULT_INI =
	"; GrandTheftMinecraft settings (edit while the game is closed)\n"
	"[General]\n"
	"; Virtual-key codes: F6 = 0x75, F9 = 0x78\n"
	"ToggleKey=0x75\n"
	"DebugKey=0x78\n"
	"StartEnabled=1\n"
	"; 0 = automatic like Minecraft (4 at 1080p), else 1..6\n"
	"GuiScale=0\n"
	"Invincible=1\n"
	"NoWanted=0\n"
	"Volume=0.8\n"
	"; Minecraft 1.21.11 is found automatically (official launcher, Prism, CurseForge, Modrinth) or fetched from\n"
	"; Mojang's servers. To use a specific install: MinecraftJar=C:\\path\\1.21.11.jar, MinecraftAssets=its assets folder\n"
	"MinecraftJar=\n"
	"MinecraftAssets=\n"
	"[Blocks]\n"
	"RenderDistance=64\n"
	"PolyBudget=30000\n"
	"; blocks closer than this (metres) get full texture detail; it halves each time the distance doubles\n"
	"FullDetailDistance=24\n"
	"; texture detail of the nearest blocks: 1, 2, 4, 8 or 16 (16 = full Minecraft resolution, default)\n"
	"PolyDetailNear=16\n"
	"; Stage 2 (DLC installed): blocks within PropRadius become real textured props, nearest first, at most\n"
	"; MaxBlockProps (GTA gets unstable past ~1500 script objects). Farther blocks use the polygon renderer.\n"
	"MaxBlockProps=900\n"
	"PropRadius=150\n"
	"; without the DLC: invisible collision crates\n"
	"MaxCollisionProps=350\n"
	"CollisionRadius=40\n"
	"GtaExplosionFx=0\n";

static int parse_int(const char *v) { return (int)std::strtol(v, nullptr, 0); }

void config_load()
{
	std::string path = g_dataDir + "config.ini";
	std::ifstream in(path);
	if (!in)
	{
		std::ofstream out(path);
		out << DEFAULT_INI;
		logf("wrote default %s", path.c_str());
		return;
	}
	std::string line;
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == ';' || line[0] == '[')
			continue;
		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		std::string k = line.substr(0, eq), v = line.substr(eq + 1);
		const char *s = v.c_str();
		if (k == "ToggleKey") g_cfg.toggleKey = parse_int(s);
		else if (k == "DebugKey") g_cfg.debugKey = parse_int(s);
		else if (k == "StartEnabled") g_cfg.startEnabled = parse_int(s) != 0;
		else if (k == "GuiScale") g_cfg.guiScale = parse_int(s);
		else if (k == "Invincible") g_cfg.invincible = parse_int(s) != 0;
		else if (k == "NoWanted") g_cfg.noWanted = parse_int(s) != 0;
		else if (k == "MinecraftJar") g_cfg.minecraftJar = v;
		else if (k == "MinecraftAssets") g_cfg.minecraftAssets = v;
		else if (k == "Volume") g_cfg.volume = (float)std::atof(s);
		else if (k == "RenderDistance") g_cfg.renderDistance = (float)std::atof(s);
		else if (k == "FullDetailDistance") g_cfg.fullDetailDistance = (float)std::atof(s);
		else if (k == "PolyBudget") g_cfg.polyBudget = parse_int(s);
		else if (k == "PolyDetailNear") g_cfg.polyDetailNear = parse_int(s);
		else if (k == "MaxBlockProps") g_cfg.maxBlockProps = parse_int(s);
		else if (k == "PropRadius") g_cfg.propRadius = (float)std::atof(s);
		else if (k == "MaxCollisionProps") g_cfg.maxCollisionProps = parse_int(s);
		else if (k == "CollisionRadius") g_cfg.collisionRadius = (float)std::atof(s);
		else if (k == "GtaExplosionFx") g_cfg.gtaExplosionFx = parse_int(s) != 0;
	}
	int d = g_cfg.polyDetailNear;
	g_cfg.polyDetailNear = d >= 16 ? 16 : d >= 8 ? 8 : d >= 4 ? 4 : d >= 2 ? 2 : 1;
	logf("config: toggle=0x%X gui=%d props=%d dist=%.0f budget=%d detail=%d", g_cfg.toggleKey, g_cfg.guiScale,
	     g_cfg.maxCollisionProps, g_cfg.renderDistance, g_cfg.polyBudget, g_cfg.polyDetailNear);
}
