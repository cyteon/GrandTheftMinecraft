// The held item in 3D, like Minecraft's first-person hand: a textured cube for blocks, the sprite extruded one
// pixel thick for items. Built in Minecraft's view space with its own transforms (ItemInHandRenderer + the item
// models' firstperson_righthand display), then placed in front of GTA's camera with DRAW_POLY.
#pragma once

namespace hand
{
	// swing: 0..1 attack progress (1 = idle); equip: 0 = lowered, 1 = raised
	void draw(int item, float swing, float equip);
}
