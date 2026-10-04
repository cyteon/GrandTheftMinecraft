#include "render2d.h"
#include "common.h"
#include "config.h"
#include "log.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <windows.h>

namespace r2d
{
	static std::unordered_map<std::string, int> s_tex;
	static std::unordered_map<int, int> s_count; // instances used this frame per texture
	static int s_white = -1;
	static int s_glyph[256];
	static int s_adv[256];

	int tex(const std::string &rel)
	{
		auto it = s_tex.find(rel);
		if (it != s_tex.end())
			return it->second;
		std::string path = g_dataDir + rel;
		int id = -1;
		if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
			id = shv::createTexture(path.c_str());
		else
			logf("texture missing: %s", path.c_str());
		s_tex[rel] = id;
		return id;
	}

	bool init()
	{
		s_white = tex("gui/white.png");
		std::ifstream in(g_dataDir + "font/font.txt");
		for (int i = 0; i < 256; i++)
		{
			s_adv[i] = 6;
			in >> s_adv[i];
			s_glyph[i] = -1;
		}
		for (int c = 33; c < 127; c++)
			if (s_adv[c])
				s_glyph[c] = tex("font/" + std::to_string(c) + ".png");
		logf("r2d: white=%d, glyph A=%d", s_white, s_glyph['A']);
		return s_white >= 0;
	}

	void begin_frame() { s_count.clear(); }

	void draw(int id, float x, float y, float w, float h, uint32_t argb, int level, float rot)
	{
		if (id < 0 || w <= 0 || h <= 0)
			return;
		int &n = s_count[id];
		if (n >= 64)
			return;
		float W = (float)g.screenW, H = (float)g.screenH;
		float a = ((argb >> 24) & 255) / 255.0f, r = ((argb >> 16) & 255) / 255.0f, gg = ((argb >> 8) & 255) / 255.0f,
		      b = (argb & 255) / 255.0f;
		// SHV scales sizeY by the aspect factor: pass both sizes relative to the screen width.
		shv::drawTexture(id, n++, level, 60, w / W, h / W, 0.5f, 0.5f, (x + w * 0.5f) / W, (y + h * 0.5f) / H, rot,
		                 W / H, r, gg, b, a);
	}

	void rect(float x, float y, float w, float h, uint32_t argb, int level) { draw(s_white, x, y, w, h, argb, level); }

	float text_width(const std::string &s, float scale)
	{
		if (scale <= 0)
			scale = (float)g.gui;
		float w = 0;
		for (unsigned char c : s)
			w += s_adv[c] * scale;
		return w;
	}

	float text(float x, float y, const std::string &s, uint32_t rgb, bool shadow, int level, float scale, float alpha)
	{
		if (scale <= 0)
			scale = (float)g.gui;
		uint32_t a = (uint32_t)(clampf(alpha, 0, 1) * 255.0f) << 24;
		uint32_t sh = ((rgb & 0xFCFCFC) >> 2) | a;
		float cx = x;
		for (unsigned char c : s)
		{
			int id = s_glyph[c];
			if (id >= 0)
			{
				if (shadow)
					draw(id, cx + scale, y + scale, 8 * scale, 8 * scale, sh, level);
				draw(id, cx, y, 8 * scale, 8 * scale, rgb | a, level + 1);
			}
			cx += s_adv[c] * scale;
		}
		return cx - x;
	}
}
