// The Wither: a flying three-headed boss (Minecraft's WitherBoss, simplified). It's an invisible, frozen GTA ped
// moved by the mod (so GTA's bullets, explosions and our weapons can hit it, and cops fight it) wearing the Wither's
// body and heads from the block pack.
//   spawning: 3.5 s of charging (invulnerable, flashing), then a power-7 explosion
//   flying: hovers ~5 m above its target and closes in (Minecraft's movement code), heads track targets
//   attacking: wither skulls from all three heads that explode on impact and break blocks
//   below half health: flies at its target's height and fires faster (Minecraft's second phase)
//   death: shudders, then a big explosion
#pragma once
#include "common.h"

namespace wither
{
	bool spawn(const V3 &at);
	void update();
	void draw_boss_bar(); // the purple boss bar at the top of the screen
	void clear();
	int count();
	bool is_wither(int ped);
}
