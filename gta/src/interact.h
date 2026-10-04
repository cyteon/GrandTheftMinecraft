// What the crosshair points at, and the mouse buttons: break / attack (LMB), place / use (RMB), pick block (MMB).
#pragma once
#include "collision.h"
#include "world.h"
#include <string>

namespace interact
{
	struct Target
	{
		enum Kind
		{
			NONE,
			BLOCK,  // one of our blocks
			GROUND, // GTA geometry
			PED,
			VEHICLE,
			OBJECT
		} kind = NONE;
		VoxelHit vh;
		GtaHit gh;
	};
	extern Target g_target;
	void update(bool allowInput);
	std::string describe();
}
