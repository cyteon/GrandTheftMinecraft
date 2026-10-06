// Block props. Stage 2: the DLC's textured 1 m cubes (visible, lit, they collide). Fallback: invisible,
// frozen stock props at block cells near the player (nearest first, capped,
// because GTA crashes around ~1500 script objects). Also the GTA line-of-sight probe used everywhere.
#pragma once
#include "common.h"

struct GtaHit
{
	bool hit = false;
	V3 pos, normal;
	int entity = 0;
	float t = 0;       // distance from the start
	Hash material = 0; // surface material (materials.dat name hash, e.g. CAR_GLASS_WEAK)
};

// Synchronous LOS probe (world, vehicles, peds, objects, foliage). Hits on our own block props are reported with
// entity set; callers use collision::is_ours() to tell them apart.
GtaHit gta_probe(const V3 &a, const V3 &b, int ignoreEntity, int flags = 1 | 2 | 4 | 8 | 16 | 256);
extern unsigned g_probeCount, g_probePending; // diagnostics: probes run, results that weren't ready

// Like gta_probe, but passes through the player's ped and the vehicle they're in (re-probing past them).
GtaHit gta_probe_self(const V3 &a, const V3 &b, int flags = 1 | 2 | 4 | 8 | 16 | 256, int alsoIgnore = 0);

namespace collision
{
	void init(); // finds the DLC block models (gtm_*), else picks a stock crate for collision only
	bool dlc();  // real textured block props available
	bool has_prop(uint64_t cellKey); // a visible DLC prop currently stands for this block
	void update();
	void clear(); // delete all props
	bool is_ours(int entity);
	int count();
	const char *model_name();
	V3 model_size();
}
