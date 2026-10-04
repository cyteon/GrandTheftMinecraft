#pragma once
#include <string>

struct Config
{
	int toggleKey = 0x75; // F6
	int debugKey = 0x78;  // F9
	int guiScale = 0;     // 0 = auto, like Minecraft
	int maxCollisionProps = 350;
	float collisionRadius = 40.0f;
	int maxBlockProps = 900; // DLC props (visible blocks); the rest fall back to polygons
	float propRadius = 150.0f;
	float renderDistance = 64.0f;
	int polyBudget = 30000;
	float fullDetailDistance = 24.0f; // metres of full-resolution block textures
	int polyDetailNear = 16; // face grid NxN for the nearest blocks (1, 2, 4, 8 or 16)
	bool invincible = true;
	bool noWanted = false;
	bool gtaExplosionFx = false; // also show GTA's own explosion effect under the Minecraft particles
	bool startEnabled = true;
	float volume = 0.8f;
	float elytraSpeed = 1.6f; // elytra glide speed vs Minecraft's (1 = vanilla)
	bool playAsSteve = true;  // third person: show Steve instead of the GTA character
	std::string skinFile;     // optional 64x64 Minecraft skin (classic/wide arms) instead of Steve's
	std::string minecraftJar;    // optional: a 1.21.11 client jar to use instead of searching / downloading
	std::string minecraftAssets; // optional: that launcher's assets folder (indexes\, objects\)
};

extern Config g_cfg;
extern std::string g_dataDir; // ...\GTA Modding\GrandTheftMinecraft\  (with trailing slash)

void config_load();
