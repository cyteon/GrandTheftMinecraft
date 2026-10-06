#include "signs.h"
#include "collision.h"
#include "items.h"
#include "render2d.h"
#include <algorithm>
#include <vector>

namespace signs
{
	static bool s_editing = false;
	static uint64_t s_key = 0;

	static const float PX = 0.010416667f * 0.6666667f; // Minecraft's sign text: metres per font pixel
	static const float MAX_W = 90;                      // font pixels per line

	bool editing() { return s_editing; }

	void edit(const Cell &c)
	{
		s_key = cell_key(c);
		s_editing = true;
		std::string cur = g_signText.count(s_key) ? g_signText[s_key] : "";
		for (char &ch : cur)
			if (ch == '\n')
				ch = '|';
		DISPLAY_ONSCREEN_KEYBOARD(0, "FMMC_KEY_TIP8", "", cur.c_str(), "", "", "", 90);
	}

	// Minecraft's four lines: '|' breaks a line, words wrap at the sign's width
	static std::string wrap(const std::string &in)
	{
		std::vector<std::string> lines(1);
		std::string word;
		auto flush = [&]() {
			if (word.empty())
				return;
			std::string tryLine = lines.back().empty() ? word : lines.back() + " " + word;
			if (r2d::text_width(tryLine, 1) > MAX_W && !lines.back().empty())
				lines.push_back(word);
			else
				lines.back() = tryLine;
			word.clear();
		};
		for (char ch : in)
		{
			if (ch == '|')
				flush(), lines.push_back("");
			else if (ch == ' ')
				flush();
			else
				word += ch;
		}
		flush();
		if (lines.size() > 4)
			lines.resize(4);
		std::string out;
		for (size_t i = 0; i < lines.size(); i++)
			out += (i ? "\n" : "") + lines[i];
		return out;
	}

	void update()
	{
		if (!s_editing)
			return;
		int st = UPDATE_ONSCREEN_KEYBOARD();
		if (st == 0)
			return; // still typing
		s_editing = false;
		if (st != 1)
			return; // cancelled
		const char *r = GET_ONSCREEN_KEYBOARD_RESULT();
		std::string t = wrap(r ? r : "");
		auto it = g_blocks.find(s_key);
		if (it == g_blocks.end() || item(it->second.item).shape != SH_SIGN)
			return;
		if (t.empty())
			g_signText.erase(s_key);
		else
			g_signText[s_key] = t;
		world_mark_dirty();
	}

	void draw()
	{
		struct Near
		{
			float d;
			uint64_t key;
		};
		static std::vector<Near> closeby;
		closeby.clear();
		for (auto &kv : g_signText)
		{
			float d = (cell_center(key_cell(kv.first)) - g.camPos).len();
			if (d < 16.0f)
				closeby.push_back({d, kv.first});
		}
		std::sort(closeby.begin(), closeby.end(), [](const Near &a, const Near &b) { return a.d < b.d; });
		if (closeby.size() > 8)
			closeby.resize(8);
		// each glyph texture can be drawn 64 times a frame (render2d); leave the HUD and tooltips 24 of them
		int used[256] = {};
		for (auto &n : closeby)
		{
			auto bit = g_blocks.find(n.key);
			if (bit == g_blocks.end() || item(bit->second.item).shape != SH_SIGN)
				continue;
			const Block &b = bit->second;
			Cell c = key_cell(n.key);
			float yaw = deg2rad(b.facing * 22.5f);
			V3 F(-std::sin(yaw), std::cos(yaw), 0), R(-F.y, F.x, 0), U(0, 0, 1);
			bool wall = b.state & ST_WALL;
			// the board's front face, at its centre (tools/make_dlc_src.py sign_model)
			V3 P = cell_center(c) + F * ((wall ? -0.4375f : 0.0f) + 0.0417f + 0.002f) + U * (wall ? 0.0208f : 0.3333f);
			V3 toCam = g.camPos - P;
			if (toCam.dot(F) <= 0.05f)
				continue; // looking at its back
			float dist = toCam.len();
			V3 dir = toCam * (-1.0f / dist);
			GtaHit h = gta_probe(g.camPos, P - dir * 0.05f, g.ped, 1 | 2 | 16);
			if (h.hit && !collision::is_ours(h.entity))
				continue;
			VoxelHit vh = voxel_raycast(g.camPos, dir, dist - 0.1f, true);
			if (vh.hit && cell_key(vh.cell) != n.key)
				continue;
			const std::string &text = g_signText[n.key];
			size_t start = 0;
			for (int k = 0; k < 4 && start <= text.size(); k++)
			{
				size_t nl = text.find('\n', start);
				std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
				start = nl == std::string::npos ? text.size() + 1 : nl + 1;
				if (line.empty())
					continue;
				bool fits = true;
				for (unsigned char ch : line)
					fits = fits && used[ch] < 40;
				if (!fits)
					continue;
				for (unsigned char ch : line)
					used[ch]++;
				V3 top = P + U * ((20 - k * 10) * PX); // Minecraft: lines 10 font px apart, centred on the board
				float sx, sy, rx, ry;
				if (!GET_SCREEN_COORD_FROM_WORLD_COORD(top.x, top.y, top.z, &sx, &sy))
					continue;
				V3 nxt = top + R * PX;
				if (!GET_SCREEN_COORD_FROM_WORLD_COORD(nxt.x, nxt.y, nxt.z, &rx, &ry))
					continue;
				float scale = std::sqrt((rx - sx) * (rx - sx) * g.screenW * g.screenW + (ry - sy) * (ry - sy) * g.screenH * g.screenH);
				if (scale < 0.4f)
					continue;
				float w = r2d::text_width(line, scale);
				r2d::text(sx * g.screenW - w / 2, sy * g.screenH, line, 0x000000, false, r2d::L_WORLD, scale);
			}
		}
	}
}
