#pragma once
#include <string>

struct Config
{
	int toggleKey = 0x75; // F6
	int debugKey = 0x78;  // F9
	int guiScale = 0;     // 0 = auto, like Minecraft
	int maxCollisionProps = 350;
	float collisionRadius = 40.0f;
	float renderDistance = 64.0f;
	int polyBudget = 30000;
	float fullDetailDistance = 24.0f; // metres of full-resolution block textures
	int polyDetailNear = 16; // face grid NxN for the nearest blocks (1, 2, 4, 8 or 16)
	bool invincible = true;
	bool noWanted = false;
	bool gtaExplosionFx = false; // also show GTA's own explosion effect under the Minecraft particles
	bool startEnabled = true;
	float volume = 0.8f;
};

extern Config g_cfg;
extern std::string g_dataDir; // ...\GTA Modding\GrandTheftMinecraft\  (with trailing slash)

void config_load();
