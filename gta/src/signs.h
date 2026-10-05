// Signs: writing on them (GTA's on-screen keyboard; '|' starts a new line, long text wraps onto Minecraft's four
// lines) and drawing their text in the world (projected each frame, Minecraft font, hidden when not facing you or
// behind something).
#pragma once
#include "common.h"
#include "world.h"

namespace signs
{
	void edit(const Cell &c); // open the keyboard for this sign
	bool editing();
	void update(); // polls the keyboard
	void draw();   // text on the signs near the camera
}
