// Placed blocks. X/Y sit on the global integer grid; Z is offset per "build" by a fraction so blocks stand flush
// on GTA ground at whatever height it has. A block is (build, x, y, z) → item id.
#pragma once
#include <string>
#include "common.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

struct Build
{
	float zOff = 0;
	int count = 0;
	float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f; // horizontal bounds (cells)
};

struct Cell
{
	int b = 0, x = 0, y = 0, z = 0;
	bool operator==(const Cell &o) const { return b == o.b && x == o.x && y == o.y && z == o.z; }
};

// Block::state bits
enum : uint8_t
{
	ST_TOP = 1,     // slab / stairs / trapdoor in the upper half
	ST_DOUBLE = 2,  // two slabs
	ST_OPEN = 4,    // door / trapdoor / gate
	ST_WALL = 8,    // torch on a wall, lantern hanging
	ST_HINGE_R = 16, // door hinged on the right
	ST_UPPER = 32,  // a door's upper half
};

struct Block
{
	uint16_t item = 0;
	uint8_t facing = 0;  // 0..15, 22.5 degree steps anticlockwise; 0 = front towards +Y
	uint8_t state = 0;   // ST_* bits
	uint8_t exposed = 0; // bit per face: 0 +Z, 1 -Z, 2 +Y, 3 -Y, 4 +X, 5 -X
};

extern std::vector<Build> g_builds;
extern std::unordered_map<uint64_t, Block> g_blocks;
extern int g_worldVersion; // bumps on every change (collision and caches resync)

uint64_t cell_key(const Cell &c);
Cell key_cell(uint64_t k);
V3 cell_min(const Cell &c); // world-space min corner
inline V3 cell_center(const Cell &c) { return cell_min(c) + V3(0.5f, 0.5f, 0.5f); }

const Block *block_at(const Cell &c);
bool place_block(const Cell &c, int item, int facing = 0, int state = 0);
bool set_block_state(const Cell &c, int state); // e.g. a door opening

// what some blocks hold: a sign's text (lines split by '\n'), a chest's 27 slots
struct StoredSlot
{
	int item = -1, count = 0;
};
extern std::unordered_map<uint64_t, std::string> g_signText;
extern std::unordered_map<uint64_t, std::vector<StoredSlot>> g_chestItems;
void world_mark_dirty();
// also removes what depended on it (Minecraft pops them): the other half of a door, plants / carpets / torches on
// top, torches and ladders on its sides, a lantern hanging under it
bool remove_block(const Cell &c);

// A build for a block placed against GTA geometry at point p: joins one within 48 m, else makes one.
int build_for_point(const V3 &p);

// Cell coordinates in build b containing world point p.
Cell world_to_cell(int b, const V3 &p);

extern const int FACE_N[6][3];

struct VoxelHit
{
	bool hit = false;
	Cell cell;
	int face = 0; // face entered through (normal = FACE_N[face])
	float t = 0;  // distance along the ray
	V3 pos;
};
// solidOnly: fly through torches, plants, carpets, ladders (projectiles, gliding)
// fluids are skipped unless hitFluids (an empty bucket)
VoxelHit voxel_raycast(const V3 &from, const V3 &dir, float maxDist, bool solidOnly = false, bool hitFluids = false);

// Does any block overlap the axis-aligned box?
bool boxes_hit_blocks(const V3 &mn, const V3 &mx);

void world_load();
void world_save_if_dirty(); // debounced ~2 s after the last change
