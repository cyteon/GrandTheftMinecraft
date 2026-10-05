// Shaped blocks (slabs, stairs, walls, fences, gates, panes, carpets, torches, lanterns, plants, ladders, doors,
// trapdoors): which of the block pack's models stands for a block and how it's turned, from its own state and its
// neighbours, the way Minecraft's block states work. Model names and canonical poses come from tools/shapes.py:
// the block's front (or whoever placed it) towards +Y.
#pragma once
#include "common.h"
#include "world.h"
#include <string>

namespace shapes
{
	struct Look
	{
		std::string suffix; // model = "gtm_" + item name + suffix
		float yaw = 0;      // degrees anticlockwise
	};
	Look look(const Cell &c, const Block &b);

	// horizontal directions by index, anticlockwise from +Y: 0 +Y, 1 -X, 2 -Y, 3 +X; block facing (22.5 degree
	// steps) <-> direction index
	int dir_of_facing(int facing);
	int facing_of_dir(int dir);
	Cell step(const Cell &c, int dir);
}
