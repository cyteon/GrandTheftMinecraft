// Elytra: right-click the elytra item to put it on / take it off; press Space while falling to glide. The glide is
// Minecraft's own fall-flying physics (LivingEntity.travel: lift from looking down, climbing trades speed for
// height, drag), run at 20 ticks a second and interpolated. Firework rockets used while gliding boost you towards
// where you look (FireworkRocketEntity's attached boost).
#pragma once
#include "common.h"

namespace elytra
{
	bool worn();
	void toggle_worn();
	bool gliding();
	V3 velocity();   // m/s while gliding
	void boost();    // a firework rocket while gliding
	void update(bool allowInput);
	void stop();
}
