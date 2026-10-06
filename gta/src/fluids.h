// Water and lava, Minecraft's flowing fluids in the block grid: a source (bucket) spreads 7 blocks (water, every
// 0.25 s) or 3 (lava, every 1.5 s), falls down holes, heads for the nearest drop, dries up without its source, and
// two water sources make a third. Lava meeting water: obsidian (source) / cobblestone (flowing) / stone (water
// running onto it). GTA's ground and walls stop it. In the world: lava burns people and cars, water puts out fires,
// carries people along and slows cars.
// Block::state for fluids: bits 0-2 the flow level (0 = source), bit 3 falling.
#pragma once
#include "common.h"
#include "world.h"

namespace fluids
{
	enum : uint8_t
	{
		FALLING = 8
	};
	int kind(const Block *b); // 0 none, 1 water, 2 lava
	bool place(const Cell &c, int kind); // a source (bucket)
	void on_change(const Cell &c);       // a block appeared or vanished here: wake the fluids around it
	void update();
	void clear();
	int count();
}
