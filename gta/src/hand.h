// The held item in 3D, like Minecraft's first-person hand. Built in Minecraft's view space with its own transforms
// (ItemInHandRenderer + the item models' firstperson_righthand display), then placed in front of GTA's camera.
// Stage 2: a real DLC prop (gtm_<block>_h / gtm_i_<sprite>), lit by GTA. Without the DLC: DRAW_POLY quads.
#pragma once
#include <string>

namespace hand
{
	enum Use
	{
		USE_NONE,
		USE_BOW,             // drawing a bow; progress = seconds held
		USE_CROSSBOW_LOAD,   // loading a crossbow; progress = 0..1
		USE_CROSSBOW_LOADED, // holding a loaded crossbow
	};
	// swing: 0..1 attack progress (1 = idle); equip: 0 = lowered, 1 = raised
	void draw(int item, float swing, float equip, Use use = USE_NONE, float progress = 0);
	void hide(); // removes the hand prop (third person, inventory open, Minecraft mode off)
	std::string sprite_for(int item, Use use, float progress); // which item model/texture to show
}
