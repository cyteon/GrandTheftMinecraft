// Redstone, direct power only (no wire yet): levers that are on, pressed buttons, pressure plates with something on
// them, redstone blocks and redstone torches power the blocks touching them; a lever or button also powers the
// solid block it's mounted on (a plate the block under it), which passes it to everything touching that block.
// Powered: doors, trapdoors and fence gates open (iron ones only this way), redstone lamps light, TNT ignites,
// note blocks play. Only switching on / off acts, so hand-opened doors stay as you left them.
#pragma once
#include "common.h"
#include "world.h"

namespace redstone
{
	void on_change(const Cell &c); // something changed here: look again at what it could power
	void update();
	void clear();
}
