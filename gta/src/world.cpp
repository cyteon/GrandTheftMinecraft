#include "world.h"
#include "config.h"
#include "items.h"
#include "log.h"
#include "shapes.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

std::vector<Build> g_builds;
std::unordered_map<uint64_t, Block> g_blocks;
int g_worldVersion = 0;
static uint32_t s_dirtySince = 0;
static bool s_dirty = false;

const int FACE_N[6][3] = {{0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}};

// 12 bits build | 18 bits x | 18 bits y | 16 bits z
uint64_t cell_key(const Cell &c)
{
	return ((uint64_t)(c.b & 0xFFF) << 52) | ((uint64_t)((c.x + 131072) & 0x3FFFF) << 34) |
	       ((uint64_t)((c.y + 131072) & 0x3FFFF) << 16) | (uint64_t)((c.z + 32768) & 0xFFFF);
}

Cell key_cell(uint64_t k)
{
	Cell c;
	c.b = (int)(k >> 52);
	c.x = (int)((k >> 34) & 0x3FFFF) - 131072;
	c.y = (int)((k >> 16) & 0x3FFFF) - 131072;
	c.z = (int)(k & 0xFFFF) - 32768;
	return c;
}

V3 cell_min(const Cell &c) { return {(float)c.x, (float)c.y, (float)c.z + g_builds[c.b].zOff}; }

const Block *block_at(const Cell &c)
{
	if (c.b < 0 || c.b >= (int)g_builds.size())
		return nullptr;
	auto it = g_blocks.find(cell_key(c));
	return it == g_blocks.end() ? nullptr : &it->second;
}

static Cell neighbour(const Cell &c, int f) { return {c.b, c.x + FACE_N[f][0], c.y + FACE_N[f][1], c.z + FACE_N[f][2]}; }

// A face is hidden by an opaque neighbour, or by a neighbour of the same see-through block (glass on glass).
static bool hides(const Block *n, int self)
{
	if (!n)
		return false;
	const Item &ni = item(n->item);
	return ni.opaque() || (n->item == self && ni.alpha);
}

static void refresh(const Cell &c)
{
	auto it = g_blocks.find(cell_key(c));
	if (it == g_blocks.end())
		return;
	uint8_t m = 0;
	for (int f = 0; f < 6; f++)
		if (!hides(block_at(neighbour(c, f)), it->second.item))
			m |= 1 << f;
	it->second.exposed = m;
}

static void touched(const Cell &c)
{
	refresh(c);
	for (int f = 0; f < 6; f++)
		refresh(neighbour(c, f));
	g_worldVersion++;
	s_dirty = true;
	s_dirtySince = GetTickCount();
}

bool place_block(const Cell &c, int itemId, int facing, int state)
{
	if (c.b < 0 || c.b >= (int)g_builds.size() || itemId < 0 || !item(itemId).block)
		return false;
	uint64_t k = cell_key(c);
	bool existed = g_blocks.count(k) != 0;
	g_blocks[k].item = (uint16_t)itemId;
	g_blocks[k].facing = (uint8_t)(facing & 15);
	g_blocks[k].state = (uint8_t)state;
	Build &b = g_builds[c.b];
	if (!existed)
		b.count++;
	b.minX = std::min(b.minX, (float)c.x), b.maxX = std::max(b.maxX, (float)c.x);
	b.minY = std::min(b.minY, (float)c.y), b.maxY = std::max(b.maxY, (float)c.y);
	touched(c);
	return true;
}

bool set_block_state(const Cell &c, int state)
{
	auto it = g_blocks.find(cell_key(c));
	if (it == g_blocks.end())
		return false;
	it->second.state = (uint8_t)state;
	touched(c);
	return true;
}

static void remove_dependents(const Cell &c, const Block &old);

bool remove_block(const Cell &c)
{
	auto it = g_blocks.find(cell_key(c));
	if (it == g_blocks.end())
		return false;
	Block old = it->second;
	g_blocks.erase(it);
	g_builds[c.b].count--;
	touched(c);
	static int depth = 0;
	if (depth < 32)
	{
		depth++;
		remove_dependents(c, old);
		depth--;
	}
	return true;
}

static void remove_dependents(const Cell &c, const Block &old)
{
	const Item &oi = item(old.item);
	if (oi.shape == SH_DOOR) // the other half
	{
		Cell o = c;
		o.z += old.state & ST_UPPER ? -1 : 1;
		const Block *b = block_at(o);
		if (b && b->item == old.item)
			remove_block(o);
	}
	Cell up = c, dn = c;
	up.z++, dn.z--;
	if (const Block *b = block_at(up))
	{
		const Item &i = item(b->item);
		bool wall = b->state & ST_WALL;
		if (i.shape == SH_CROSS || i.shape == SH_CARPET || ((i.shape == SH_TORCH || i.shape == SH_LANTERN) && !wall) ||
		    (i.shape == SH_DOOR && !(b->state & ST_UPPER)))
			remove_block(up);
	}
	if (const Block *b = block_at(dn))
		if (item(b->item).shape == SH_LANTERN && (b->state & ST_WALL))
			remove_block(dn);
	for (int d = 0; d < 4; d++) // wall torches and ladders whose wall this was (they face away from it)
	{
		Cell s = shapes::step(c, d);
		const Block *b = block_at(s);
		if (!b)
			continue;
		const Item &i = item(b->item);
		if (((i.shape == SH_TORCH && (b->state & ST_WALL)) || i.shape == SH_LADDER) &&
		    shapes::dir_of_facing(b->facing) == d)
			remove_block(s);
	}
}

int build_for_point(const V3 &p)
{
	int best = -1;
	float bestD = 48.0f;
	for (int i = 0; i < (int)g_builds.size(); i++)
	{
		const Build &b = g_builds[i];
		if (b.count <= 0)
			continue;
		float dx = std::max({b.minX - p.x, 0.0f, p.x - (b.maxX + 1)});
		float dy = std::max({b.minY - p.y, 0.0f, p.y - (b.maxY + 1)});
		float d = std::sqrt(dx * dx + dy * dy);
		if (d < bestD)
			bestD = d, best = i;
	}
	if (best >= 0)
		return best;
	// reuse an empty slot before growing (keys hold 12 bits of build id)
	for (int i = 0; i < (int)g_builds.size(); i++)
		if (g_builds[i].count <= 0)
		{
			g_builds[i] = Build{};
			g_builds[i].zOff = p.z - std::floor(p.z);
			return i;
		}
	if (g_builds.size() >= 4095)
		return -1;
	Build b;
	b.zOff = p.z - std::floor(p.z);
	g_builds.push_back(b);
	logf("new build %d at (%.1f, %.1f, %.2f) zOff=%.3f", (int)g_builds.size() - 1, p.x, p.y, p.z, b.zOff);
	return (int)g_builds.size() - 1;
}

Cell world_to_cell(int b, const V3 &p)
{
	return {b, (int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z - g_builds[b].zOff)};
}

VoxelHit voxel_raycast(const V3 &from, const V3 &dir, float maxDist, bool solidOnly)
{
	VoxelHit best;
	best.t = maxDist;
	for (int b = 0; b < (int)g_builds.size(); b++)
	{
		const Build &bd = g_builds[b];
		if (bd.count <= 0)
			continue;
		// quick reject: ray's horizontal reach vs build bounds
		float rx0 = std::min(from.x, from.x + dir.x * maxDist), rx1 = std::max(from.x, from.x + dir.x * maxDist);
		float ry0 = std::min(from.y, from.y + dir.y * maxDist), ry1 = std::max(from.y, from.y + dir.y * maxDist);
		if (rx1 < bd.minX - 1 || rx0 > bd.maxX + 2 || ry1 < bd.minY - 1 || ry0 > bd.maxY + 2)
			continue;
		// Amanatides-Woo DDA in build space
		V3 o(from.x, from.y, from.z - bd.zOff);
		int x = (int)std::floor(o.x), y = (int)std::floor(o.y), z = (int)std::floor(o.z);
		int sx = dir.x > 0 ? 1 : -1, sy = dir.y > 0 ? 1 : -1, sz = dir.z > 0 ? 1 : -1;
		auto tmax = [](float o_, float d, int c, int s) {
			if (std::fabs(d) < 1e-9f)
				return 1e30f;
			float next = s > 0 ? (float)(c + 1) : (float)c;
			return (next - o_) / d;
		};
		float tx = tmax(o.x, dir.x, x, sx), ty = tmax(o.y, dir.y, y, sy), tz = tmax(o.z, dir.z, z, sz);
		float dx = std::fabs(dir.x) < 1e-9f ? 1e30f : std::fabs(1.0f / dir.x);
		float dy = std::fabs(dir.y) < 1e-9f ? 1e30f : std::fabs(1.0f / dir.y);
		float dz = std::fabs(dir.z) < 1e-9f ? 1e30f : std::fabs(1.0f / dir.z);
		float t = 0;
		int face = -1;
		while (t <= best.t)
		{
			if (face >= 0)
			{
				auto it = g_blocks.find(cell_key({b, x, y, z}));
				if (it != g_blocks.end() && !(solidOnly && item(it->second.item).passable()))
				{
					best.hit = true;
					best.cell = {b, x, y, z};
					best.face = face;
					best.t = t;
					best.pos = from + dir * t;
					break;
				}
			}
			else if (g_blocks.count(cell_key({b, x, y, z})) &&
			         !(solidOnly && item(g_blocks[cell_key({b, x, y, z})].item).passable())) // started inside a block
			{
				best.hit = true;
				best.cell = {b, x, y, z};
				best.face = 0;
				best.t = 0;
				best.pos = from;
				break;
			}
			if (tx < ty && tx < tz)
				t = tx, x += sx, tx += dx, face = sx > 0 ? 5 : 4;
			else if (ty < tz)
				t = ty, y += sy, ty += dy, face = sy > 0 ? 3 : 2;
			else
				t = tz, z += sz, tz += dz, face = sz > 0 ? 1 : 0;
			if (face < 0)
				face = 0;
		}
	}
	return best;
}

bool boxes_hit_blocks(const V3 &mn, const V3 &mx)
{
	for (int b = 0; b < (int)g_builds.size(); b++)
	{
		const Build &bd = g_builds[b];
		if (bd.count <= 0 || mx.x < bd.minX || mn.x > bd.maxX + 1 || mx.y < bd.minY || mn.y > bd.maxY + 1)
			continue;
		int x0 = (int)std::floor(mn.x), x1 = (int)std::floor(mx.x);
		int y0 = (int)std::floor(mn.y), y1 = (int)std::floor(mx.y);
		int z0 = (int)std::floor(mn.z - bd.zOff), z1 = (int)std::floor(mx.z - bd.zOff);
		for (int x = x0; x <= x1; x++)
			for (int y = y0; y <= y1; y++)
				for (int z = z0; z <= z1; z++)
				{
					auto it = g_blocks.find(cell_key({b, x, y, z}));
					if (it != g_blocks.end() && !item(it->second.item).passable())
						return true;
				}
	}
	return false;
}

void world_load()
{
	std::ifstream in(g_dataDir + "world.txt");
	if (!in)
		return;
	std::string line;
	std::vector<Cell> cells;
	while (std::getline(in, line))
	{
		std::istringstream ss(line);
		char kind;
		ss >> kind;
		if (kind == 'B')
		{
			int id;
			Build b;
			ss >> id >> b.zOff;
			if (id >= 0 && id < 4095)
			{
				if ((int)g_builds.size() <= id)
					g_builds.resize(id + 1);
				g_builds[id] = b;
			}
		}
		else if (kind == 'K')
		{
			Cell c;
			std::string name;
			int facing = 0, state = 0;
			ss >> c.b >> c.x >> c.y >> c.z >> name >> facing >> state;
			int it = item_find(name);
			if (it < 0 || c.b < 0 || c.b >= (int)g_builds.size())
				continue;
			g_blocks[cell_key(c)].item = (uint16_t)it;
			g_blocks[cell_key(c)].facing = (uint8_t)(facing & 15);
			g_blocks[cell_key(c)].state = (uint8_t)state;
			Build &b = g_builds[c.b];
			b.count++;
			b.minX = std::min(b.minX, (float)c.x), b.maxX = std::max(b.maxX, (float)c.x);
			b.minY = std::min(b.minY, (float)c.y), b.maxY = std::max(b.maxY, (float)c.y);
			cells.push_back(c);
		}
	}
	for (auto &c : cells)
		refresh(c);
	g_worldVersion++;
	logf("world: %d builds, %d blocks loaded", (int)g_builds.size(), (int)g_blocks.size());
}

void world_save_if_dirty()
{
	if (!s_dirty || GetTickCount() - s_dirtySince < 2000)
		return;
	s_dirty = false;
	std::string tmp = g_dataDir + "world.txt.tmp", dst = g_dataDir + "world.txt";
	FILE *f = std::fopen(tmp.c_str(), "w");
	if (!f)
		return;
	std::fprintf(f, "# GrandTheftMinecraft world: B <build> <zOff>, K <build> <x> <y> <z> <item> [facing 0-15 [state]]\n");
	for (int i = 0; i < (int)g_builds.size(); i++)
		if (g_builds[i].count > 0)
			std::fprintf(f, "B %d %.4f\n", i, g_builds[i].zOff);
	for (auto &kv : g_blocks)
	{
		Cell c = key_cell(kv.first);
		if (kv.second.facing || kv.second.state)
			std::fprintf(f, "K %d %d %d %d %s %d %d\n", c.b, c.x, c.y, c.z, item(kv.second.item).name.c_str(),
			             kv.second.facing, kv.second.state);
		else
			std::fprintf(f, "K %d %d %d %d %s\n", c.b, c.x, c.y, c.z, item(kv.second.item).name.c_str());
	}
	std::fclose(f);
	MoveFileExA(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING);
}
