// Things that move: Minecraft particles, thrown ender pearls, primed TNT and explosions.
#pragma once
#include "common.h"
#include "world.h"

namespace fx
{
	void init();
	void update(); // simulate + draw (call once per frame after the camera is known)

	void block_break(const Cell &c, int item); // debris in the block's colours
	void portal_burst(const V3 &p, int count);
	void sweep(const V3 &p, const V3 &dir);
	void crit(const V3 &p);
	void throw_pearl(const V3 &from, const V3 &dir);
	// speed in m/s, damage in GTA health points; crit = fully drawn bow (crit particle trail)
	// owner: the ped that shot it (its own hits are ignored; 0 = the player)
	void shoot_arrow(const V3 &from, const V3 &dir, float speed, int damage, bool crit, int owner = 0);
	void flash_box(const V3 &centre, const V3 halfAxes[3], int alpha); // white overlay (creeper / TNT flash)
	void poof(const V3 &p); // Minecraft's death smoke
	void blood(const V3 &p, const V3 &dir); // a little burst of blood (red chips)
	void preload(); // request the DLC's TNT / chip / arrow models (Stage 2)
	void prime_tnt(const Cell &c, float fuseSeconds = 4.0f);
	void explode(const V3 &p, float power, bool fire = false);

	int pearl_count();
	int tnt_count();
	int particle_count();
	int arrow_count();
}
