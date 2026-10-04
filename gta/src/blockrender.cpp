#include "blockrender.h"
#include "config.h"
#include "items.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace blockrender
{
	int polysThisFrame = 0, facesThisFrame = 0, blocksDrawn = 0;

	struct FaceDef
	{
		V3 o, u, v; // top-left corner as seen from outside, right and down axes (unit cube)
		float shade;
		int tex; // F_TOP / F_SIDE / F_BOTTOM
	};
	// Same order as FACE_N: +Z, -Z, +Y, -Y, +X, -X
	static const FaceDef FACES[6] = {
		{{0, 1, 1}, {1, 0, 0}, {0, -1, 0}, 1.0f, F_TOP},
		{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, 0.5f, F_BOTTOM},
		{{1, 1, 1}, {-1, 0, 0}, {0, 0, -1}, 0.8f, F_SIDE},
		{{0, 0, 1}, {1, 0, 0}, {0, 0, -1}, 0.8f, F_SIDE},
		{{1, 0, 1}, {0, 1, 0}, {0, 0, -1}, 0.6f, F_SIDE},
		{{0, 1, 1}, {0, -1, 0}, {0, 0, -1}, 0.6f, F_SIDE},
	};

	static inline void tri(const V3 &a, const V3 &b, const V3 &c, int r, int g_, int b_, int al)
	{
		GRAPHICS::DRAW_POLY(a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z, r, g_, b_, al);
	}

	// size = edge length (1 for blocks); flash lerps colours to white
	static void draw_face(const V3 &mn, float size, int f, const Item &it, int n, float flash)
	{
		const FaceDef &fd = FACES[f];
		const auto &grid = it.lod[lod_index(n)][fd.tex];
		if (grid.empty())
			return;
		float shade = fd.shade * g.daylight;
		V3 o = mn + fd.o * size, du = fd.u * (size / n), dv = fd.v * (size / n);
		// nudge outward a hair so faces don't z-fight with the collision prop or GTA ground
		V3 nrm((float)FACE_N[f][0], (float)FACE_N[f][1], (float)FACE_N[f][2]);
		o += nrm * 0.002f;
		auto shaded = [&](const Rgba &c, int &r, int &gg, int &b, int &al) {
			float fr = c.r * shade, fg = c.g * shade, fb = c.b * shade;
			if (flash > 0)
				fr += (255 - fr) * flash, fg += (255 - fg) * flash, fb += (255 - fb) * flash;
			r = (int)fr, gg = (int)fg, b = (int)fb;
			al = (it.alpha || it.cutout) ? (c.a < 255 && it.cutout ? 255 : c.a) : 255;
		};
		for (int j = 0; j < n; j++)
		{
			// merge runs of identical texels in a row into one quad
			int i = 0;
			while (i < n)
			{
				const Rgba &c = grid[j * n + i];
				int e = i + 1;
				while (e < n && std::memcmp(&grid[j * n + e], &c, sizeof(Rgba)) == 0)
					e++;
				if (c.a >= 16)
				{
					int r, gg, b, al;
					shaded(c, r, gg, b, al);
					V3 p00 = o + du * (float)i + dv * (float)j;
					V3 p10 = o + du * (float)e + dv * (float)j;
					V3 p01 = p00 + dv, p11 = p10 + dv;
					tri(p00, p10, p11, r, gg, b, al);
					tri(p00, p11, p01, r, gg, b, al);
					polysThisFrame += 2;
				}
				i = e;
			}
		}
		facesThisFrame++;
	}

	static int detail_for(float d)
	{
		int n = g_cfg.polyDetailNear;
		if (d > 6)
			n = std::min(n, 8);
		if (d > 14)
			n = std::min(n, 4);
		if (d > 28)
			n = std::min(n, 2);
		if (d > 48)
			n = 1;
		return n;
	}

	struct Cand
	{
		float d;
		uint64_t key;
	};
	static std::vector<Cand> s_cands;
	static std::vector<std::pair<uint64_t, int>> s_plan; // key, detail

	void draw_blocks()
	{
		polysThisFrame = facesThisFrame = blocksDrawn = 0;
		GRAPHICS::SET_BACKFACECULLING(FALSE);
		float maxD = g_cfg.renderDistance;
		float aspect = (float)g.screenW / (float)g.screenH;
		float tanHalf = std::tan(deg2rad(g.camFov * 0.5f));
		float halfDiag = std::atan(tanHalf * std::sqrt(1.0f + aspect * aspect));
		s_cands.clear();
		for (auto &kv : g_blocks)
		{
			if (!kv.second.exposed)
				continue;
			Cell c = key_cell(kv.first);
			V3 v = cell_center(c) - g.camPos;
			float d = v.len();
			if (d > maxD)
				continue;
			if (d > 1.5f)
			{
				float cosA = v.dot(g.camDir) / d;
				float margin = std::asin(std::min(1.0f, 0.9f / d));
				if (cosA < std::cos(std::min(PI, halfDiag + margin)))
					continue;
			}
			s_cands.push_back({d, kv.first});
		}
		std::sort(s_cands.begin(), s_cands.end(), [](const Cand &a, const Cand &b) { return a.d < b.d; });
		// pick each block's detail nearest-first (the budget goes to what's close)...
		int budget = g_cfg.polyBudget, planned = 0;
		s_plan.clear();
		std::vector<std::pair<int, V3>> lights;
		for (auto &cd : s_cands)
		{
			const Block &bk = g_blocks[cd.key];
			int faces = 0;
			for (int f = 0; f < 6; f++)
				if (bk.exposed & (1 << f))
					faces++;
			faces = std::min(faces, 3); // at most three faces of a cube face the camera
			int n = detail_for(cd.d);
			while (n > 1 && planned + faces * 2 * n * n > budget)
				n >>= 1;
			if (planned + faces * 2 > budget)
				break;
			planned += faces * 2 * n * n;
			s_plan.push_back({cd.key, n});
			if (item(bk.item).light && lights.size() < 24)
				lights.push_back({0, cell_center(key_cell(cd.key))});
		}
		// ...then draw far to near: DRAW_POLY is depth-tested against GTA's world but not against other polys,
		// so the painter's order is what makes near blocks cover far ones
		for (auto p = s_plan.rbegin(); p != s_plan.rend(); ++p)
		{
			Cell c = key_cell(p->first);
			const Block &bk = g_blocks[p->first];
			const Item &it = item(bk.item);
			V3 mn = cell_min(c);
			for (int f = 0; f < 6; f++)
			{
				if (!(bk.exposed & (1 << f)))
					continue;
				// face plane must face the camera
				V3 pn((float)FACE_N[f][0], (float)FACE_N[f][1], (float)FACE_N[f][2]);
				V3 pc = mn + V3(0.5f, 0.5f, 0.5f) + pn * 0.5f;
				if ((g.camPos - pc).dot(pn) <= 0)
					continue;
				draw_face(mn, 1.0f, f, it, p->second, 0);
			}
			blocksDrawn++;
		}
		for (auto &l : lights)
			GRAPHICS::DRAW_LIGHT_WITH_RANGE(l.second.x, l.second.y, l.second.z, 255, 214, 150, 9.0f, 2.5f);
	}

	void draw_cube(const V3 &mn0, int itemId, float flash, float grow)
	{
		const Item &it = item(itemId);
		float s = 1.0f + grow;
		V3 mn = mn0 - V3(grow * 0.5f, grow * 0.5f, grow * 0.5f);
		float d = (mn0 + V3(0.5f, 0.5f, 0.5f) - g.camPos).len();
		int n = detail_for(d);
		for (int f = 0; f < 6; f++)
		{
			V3 pn((float)FACE_N[f][0], (float)FACE_N[f][1], (float)FACE_N[f][2]);
			V3 pc = mn + V3(s, s, s) * 0.5f + pn * (s * 0.5f);
			if ((g.camPos - pc).dot(pn) <= 0)
				continue;
			draw_face(mn, s, f, it, n, flash);
		}
	}

	void draw_outline(const Cell &c)
	{
		V3 a = cell_min(c) - V3(0.003f, 0.003f, 0.003f);
		V3 b = a + V3(1.006f, 1.006f, 1.006f);
		V3 p[8];
		for (int i = 0; i < 8; i++)
			p[i] = {(i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z};
		static const int E[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
		for (auto &e : E)
			GRAPHICS::DRAW_LINE(p[e[0]].x, p[e[0]].y, p[e[0]].z, p[e[1]].x, p[e[1]].y, p[e[1]].z, 0, 0, 0, 160);
	}

	void draw_quad_billboard(const V3 &p, float size, uint8_t r, uint8_t g_, uint8_t b, uint8_t a)
	{
		V3 right = g.camDir.cross(V3(0, 0, 1)).norm();
		if (right.len2() < 0.5f)
			right = V3(1, 0, 0);
		V3 up = right.cross(g.camDir).norm();
		V3 h = right * (size * 0.5f), v = up * (size * 0.5f);
		V3 p00 = p - h - v, p10 = p + h - v, p11 = p + h + v, p01 = p - h + v;
		float s = g.daylight;
		tri(p00, p10, p11, (int)(r * s), (int)(g_ * s), (int)(b * s), a);
		tri(p00, p11, p01, (int)(r * s), (int)(g_ * s), (int)(b * s), a);
		polysThisFrame += 2;
	}
}
