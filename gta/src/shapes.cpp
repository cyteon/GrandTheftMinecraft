#include "shapes.h"
#include "items.h"
#include "config.h"
#include "log.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace shapes
{
	static const int DX[4] = {0, -1, 0, 1}, DY[4] = {1, 0, -1, 0};
	static const int BIT[4] = {1, 8, 4, 2}; // direction -> connection mask bit (1 +Y, 2 +X, 4 -Y, 8 -X)

	int dir_of_facing(int facing) { return ((facing + 2) / 4) & 3; }
	int facing_of_dir(int dir) { return (dir & 3) * 4; }
	Cell step(const Cell &c, int dir) { return {c.b, c.x + DX[dir & 3], c.y + DY[dir & 3], c.z}; }

	// a mask turned 90 degrees anticlockwise (+Y -> -X -> -Y -> +X)
	static int rot1(int m) { return (m & 1 ? 8 : 0) | (m & 2 ? 1 : 0) | (m & 4 ? 2 : 0) | (m & 8 ? 4 : 0); }

	static bool nether_fence(const Item &i) { return i.name.rfind("nether_brick", 0) == 0; }

	// does a fence / wall / pane at c reach towards its neighbour in direction d?
	static bool connects(const Item &self, const Cell &c, int d)
	{
		const Block *nb = block_at(step(c, d));
		if (!nb)
			return false;
		const Item &n = item(nb->item);
		if (n.shape == SH_CUBE)
			return !n.skull; // a full block's side
		if (n.shape == SH_GATE) // a gate in line with us
			return dir_of_facing(nb->facing) % 2 != d % 2;
		switch (self.shape)
		{
		case SH_FENCE:
			return n.shape == SH_FENCE && nether_fence(n) == nether_fence(self);
		case SH_WALL:
			return n.shape == SH_WALL || n.shape == SH_PANE;
		case SH_PANE:
			return n.shape == SH_PANE || n.shape == SH_WALL;
		default:
			return false;
		}
	}

	static Look connected(const Item &it, const Cell &c)
	{
		int mask = 0;
		for (int d = 0; d < 4; d++)
			if (connects(it, c, d))
				mask |= BIT[d];
		if (it.shape == SH_WALL && (mask == 5 || mask == 10))
		{
			Cell up = c;
			up.z++;
			if (!block_at(up)) // a straight run with nothing on top: no post (Minecraft)
				return {"_ns_np", mask == 5 ? 0.0f : 90.0f};
		}
		static const struct
		{
			const char *s;
			int m;
		} CV[] = {{"_post", 0}, {"_n", 1}, {"_ns", 5}, {"_ne", 3}, {"_nes", 7}, {"_nesw", 15}};
		for (auto &v : CV)
		{
			int m = v.m;
			for (int k = 0; k < 4; k++, m = rot1(m))
				if (m == mask)
					return {v.s, k * 90.0f};
		}
		return {"_post", 0};
	}

	// Minecraft's StairBlock.getStairsShape, with directions as indices (tall side = away from whoever placed it)
	static std::string stair_shape(const Cell &c, const Block &b)
	{
		int td = (dir_of_facing(b.facing) + 2) % 4, half = b.state & ST_TOP;
		auto stairs_at = [&](const Cell &n, int &ntd) {
			const Block *o = block_at(n);
			if (!o || item(o->item).shape != SH_STAIRS || (o->state & ST_TOP) != half)
				return false;
			ntd = (dir_of_facing(o->facing) + 2) % 4;
			return true;
		};
		auto can_take = [&](int d) {
			int ntd;
			return !(stairs_at(step(c, d), ntd) && ntd == td);
		};
		int d;
		if (stairs_at(step(c, td), d) && d % 2 != td % 2 && can_take((d + 2) % 4))
			return d == (td + 1) % 4 ? "_ol" : "_or";
		if (stairs_at(step(c, (td + 2) % 4), d) && d % 2 != td % 2 && can_take(d))
			return d == (td + 1) % 4 ? "_il" : "_ir";
		return "_s";
	}

	// rails join the rails beside them: straight along a line, or (plain rails only) a curve where two meet at a corner
	static Look rail_look(const Item &it, const Cell &c)
	{
		int mask = 0;
		for (int d = 0; d < 4; d++)
		{
			const Block *nb = block_at(step(c, d));
			if (nb && item(nb->item).shape == SH_RAIL)
				mask |= BIT[d];
		}
		if (it.name == "rail")
		{
			int m = 6; // the curve model joins -Y and +X
			for (int k = 0; k < 4; k++, m = rot1(m))
				if ((mask & m) == m && (mask & ~m) == 0)
					return {"_corner", k * 90.0f};
			// three or four neighbours: a corner if two of them make one (Minecraft prefers south / east)
			m = 6;
			if (mask != 5 && mask != 10 && (mask & 5) != 5 && (mask & 10) != 10)
				for (int k = 0; k < 4; k++, m = rot1(m))
					if ((mask & m) == m)
						return {"_corner", k * 90.0f};
		}
		return {"_ns", (mask & 10) && !(mask & 5) ? 90.0f : 0.0f};
	}

	// shapes.txt from the block pack's generator: model -> boxes (block pixels, the model's own orientation)
	static std::unordered_map<std::string, std::vector<Box>> s_outlines;
	static bool s_loaded = false;

	static void load_outlines()
	{
		s_loaded = true;
		std::ifstream in(g_dataDir + "shapes.txt");
		std::string line;
		while (std::getline(in, line))
		{
			if (line.empty() || line[0] == '#')
				continue;
			size_t semi = line.find(';');
			if (semi == std::string::npos)
				continue;
			std::vector<Box> boxes;
			std::stringstream ss(line.substr(semi + 1));
			std::string part;
			while (std::getline(ss, part, '|'))
			{
				float v[6] = {};
				if (std::sscanf(part.c_str(), "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6)
					boxes.push_back({V3(v[0], v[1], v[2]) * (1 / 16.0f), V3(v[3], v[4], v[5]) * (1 / 16.0f)});
			}
			s_outlines[line.substr(0, semi)] = boxes;
		}
		logf("shapes: %d outline shapes", (int)s_outlines.size());
	}

	int outline(const Cell &c, const Block &b, Box *out)
	{
		const Item &it = item(b.item);
		if (it.shape == SH_CUBE && !it.skull)
		{
			out[0] = {V3(0, 0, 0), V3(1, 1, 1)};
			return 1;
		}
		if (!s_loaded)
			load_outlines();
		Look lk = look(c, b);
		auto f = s_outlines.find("gtm_" + it.name + lk.suffix);
		if (f == s_outlines.end() || f->second.empty())
		{
			out[0] = {V3(0, 0, 0), V3(1, 1, 1)};
			return 1;
		}
		// turned about the block's vertical centre line like its prop (the bounds of the turned box)
		float a = deg2rad(lk.yaw), ca = std::cos(a), sa = std::sin(a);
		int n = 0;
		for (const Box &bx : f->second)
		{
			if (n >= 16)
				break;
			V3 lo(1e9f, 1e9f, bx.lo.z), hi(-1e9f, -1e9f, bx.hi.z);
			for (int k = 0; k < 4; k++)
			{
				float x = (k & 1 ? bx.hi.x : bx.lo.x) - 0.5f, y = (k & 2 ? bx.hi.y : bx.lo.y) - 0.5f;
				float rx = x * ca - y * sa + 0.5f, ry = x * sa + y * ca + 0.5f;
				lo.x = std::min(lo.x, rx), lo.y = std::min(lo.y, ry), hi.x = std::max(hi.x, rx), hi.y = std::max(hi.y, ry);
			}
			out[n++] = {lo, hi};
		}
		return n;
	}

	Look look(const Cell &c, const Block &b)
	{
		const Item &it = item(b.item);
		float yaw = b.facing * 22.5f;
		bool top = b.state & ST_TOP, open = b.state & ST_OPEN, wall = b.state & ST_WALL;
		switch (it.shape)
		{
		case SH_SLAB:
			return {b.state & ST_DOUBLE ? "_double" : top ? "_top" : "_bottom", 0};
		case SH_STAIRS:
			return {stair_shape(c, b) + (top ? "_t" : ""), yaw};
		case SH_WALL:
		case SH_FENCE:
		case SH_PANE:
			return connected(it, c);
		case SH_GATE:
			return {open ? "_open" : "_closed", yaw};
		case SH_TORCH:
			return {wall ? "_wall" : "", wall ? yaw : 0};
		case SH_LANTERN:
			return {wall ? "_hanging" : "", 0};
		case SH_LADDER:
			return {"", yaw};
		case SH_DOOR:
			return {std::string(b.state & ST_UPPER ? "_upper" : "_lower") +
			            (open ? (b.state & ST_HINGE_R ? "_open_r" : "_open") : ""),
			        yaw};
		case SH_TRAPDOOR:
			return {open ? "_open" : top ? "_top" : "_bottom", yaw};
		case SH_CARPET:
		case SH_CROSS:
			return {"", 0};
		case SH_BED:
			return {b.state & ST_UPPER ? "_head" : "_foot", yaw};
		case SH_CHEST:
			return {open ? "_open" : "", yaw};
		case SH_SIGN:
		case SH_BANNER:
			return {wall ? "_wall" : "", yaw};
		case SH_BUTTON:
		case SH_LEVER:
			return {std::string(wall ? "_wall" : "_floor") + (open ? "_on" : ""), yaw};
		case SH_PLATE:
			return {open ? "_down" : "", 0};
		case SH_RAIL:
			return rail_look(it, c);
		case SH_ANVIL:
		case SH_CAMPFIRE:
			return {"", yaw};
		case SH_LAMP:
			return {open ? "_on" : "", 0};
		default:
			return {"", (it.oriented || it.skull) ? yaw : 0};
		}
	}
}
