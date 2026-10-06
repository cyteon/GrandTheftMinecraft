// Falling blocks (sand, gravel, concrete powder, anvils): with nothing under them they drop with Minecraft's
// gravity (0.04 blocks/tick^2, 2 % drag a tick) as a moving prop and land in the first free cell; anvils hurt the
// people they land on and dent cars, concrete powder that lands by water hardens into concrete.
#pragma once
#include "common.h"
#include "world.h"

namespace falling
{
	void on_change(const Cell &c); // something changed here: check this block and the one above
	void update();
	void clear();
	int count();
}
