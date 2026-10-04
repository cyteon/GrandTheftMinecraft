// Stage-1 block rendering: each exposed, camera-facing face is an n x n grid of DRAW_POLY quads coloured from the
// real texture (n by distance), with Minecraft's face shading and a day/night factor (DRAW_POLY is unlit).
#pragma once
#include "common.h"
#include "world.h"

namespace blockrender
{
	extern int polysThisFrame, facesThisFrame, blocksDrawn;
	void draw_blocks();
	// A free-standing cube (primed TNT). flash 0..1 lerps to white; grow scales about the centre.
	void draw_cube(const V3 &mn, int item, float flash, float grow);
	void draw_outline(const Cell &c);
	void draw_quad_billboard(const V3 &p, float size, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
}
