// Block collision for GTA: invisible, frozen stock props at block cells near the player (nearest first, capped,
// because GTA crashes around ~1500 script objects). Also the GTA line-of-sight probe used everywhere.
#pragma once
#include "common.h"

struct GtaHit
{
	bool hit = false;
	V3 pos, normal;
	int entity = 0;
	float t = 0; // distance from the start
};

// Synchronous LOS probe (world, vehicles, peds, objects, foliage). Hits on our own block props are reported with
// entity set; callers use collision::is_ours() to tell them apart.
GtaHit gta_probe(const V3 &a, const V3 &b, int ignoreEntity, int flags = 1 | 2 | 4 | 8 | 16 | 256);

namespace collision
{
	void init(); // picks the stock prop closest to a 1 m cube (logged)
	void update();
	void clear(); // delete all props
	bool is_ours(int entity);
	int count();
	const char *model_name();
	V3 model_size();
}
