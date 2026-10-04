// Screen-space drawing with ScriptHookV's drawTexture, in pixels. Minecraft's GUI pixels are `g.gui` screen
// pixels each; the PNGs are pre-scaled x4 with nearest-neighbour so GUI scale 4 (1080p) draws them 1:1.
#pragma once
#include <cstdint>
#include <string>

namespace r2d
{
	enum Level
	{
		L_WORLD = 0, // particles, pearls (behind the GUI)
		L_HAND = 10,
		L_HUD = 20,
		L_HUD_TOP = 25,
		L_INV_BACK = 40,
		L_INV = 45,
		L_INV_ITEM = 50,
		L_INV_TOP = 55,
		L_TOOLTIP = 60,
		L_TOOLTIP_TEXT = 65,
		L_DEBUG = 80,
	};

	bool init();      // loads font metrics and the shared textures
	void begin_frame(); // resets the per-texture instance counters (64 per texture per frame)
	int tex(const std::string &rel); // createTexture(dataDir + rel), cached; -1 if the file is missing

	// Draw a texture into a pixel rectangle (top-left x, y). argb = 0xAARRGGBB tint. rot in turns (0..1).
	void draw(int tex, float x, float y, float w, float h, uint32_t argb = 0xFFFFFFFF, int level = L_HUD,
	          float rot = 0.0f);
	void rect(float x, float y, float w, float h, uint32_t argb, int level);

	// Minecraft font. Positions and sizes in screen pixels; scale = screen pixels per font pixel.
	float text(float x, float y, const std::string &s, uint32_t rgb = 0xFFFFFF, bool shadow = true, int level = L_HUD,
	           float scale = 0, float alpha = 1.0f);
	float text_width(const std::string &s, float scale = 0);
}
