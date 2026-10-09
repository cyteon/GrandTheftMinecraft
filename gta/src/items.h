// Item and block registry, loaded from items.txt (written by tools/extract_mc.py).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Rgba
{
	uint8_t r, g, b, a;
};

enum Face
{
	F_TOP,
	F_SIDE,
	F_BOTTOM
};

// block shapes (tools/shapes.py, gta/src/shapes.cpp)
enum Shape : uint8_t
{
	SH_CUBE,
	SH_SLAB,
	SH_STAIRS,
	SH_WALL,
	SH_FENCE,
	SH_GATE,
	SH_PANE,
	SH_CARPET,
	SH_TORCH,
	SH_LANTERN,
	SH_CROSS,
	SH_LADDER,
	SH_DOOR,
	SH_TRAPDOOR,
	SH_BED,
	SH_CHEST,
	SH_SIGN,
	SH_BANNER,
	SH_BUTTON,
	SH_LEVER,
	SH_PLATE,
	SH_RAIL,
	SH_ANVIL,
	SH_ENCHANTING,
	SH_BREWING,
	SH_CAULDRON,
	SH_CAMPFIRE,
	SH_POT,
	SH_LAMP
};

struct Item
{
	std::string name, display, tab, sound;
	bool block = false;
	bool alpha = false, light = false, gravity = false, tnt = false, cutout = false;
	bool oriented = false; // has a front that turns towards you when placed (furnace, carved pumpkin...)
	bool skull = false; // a half-size head on the floor of its cell, not a full cube
	Shape shape = SH_CUBE;
	std::string sprite; // a block held and shown flat as this sprite (plants, torches, doors...); "" = 3D
	int maxStack = 64;
	int icon = -2; // drawTexture id (-2 = not loaded yet: see item_icon)
	// Face colours per level of detail: lod[k] holds an n x n grid (n = 1 << k) for top/side/bottom.
	std::vector<Rgba> lod[5][3];
	int tris[5][3] = {}; // triangles the renderer emits per face at each lod (runs merged, clear texels skipped)
	bool opaque() const { return block && !alpha && !cutout && !skull && (shape == SH_CUBE || shape == SH_LAMP); }
	bool held3d() const { return block && sprite.empty(); }
	// our own movement checks (flight, gliding, making room) let you through these
	bool passable() const
	{
		return shape == SH_TORCH || shape == SH_CROSS || shape == SH_LANTERN || shape == SH_CARPET || shape == SH_LADDER ||
		       shape == SH_SIGN || shape == SH_BANNER || shape == SH_BUTTON || shape == SH_LEVER || shape == SH_PLATE ||
		       shape == SH_RAIL;
	}
};

extern std::vector<Item> g_items;
bool items_load();
int item_find(const std::string &name); // -1 if unknown
inline const Item &item(int id) { return g_items[id]; }
int item_icon(int id); // the icon's texture, loaded on first use (-1 if missing)

// Detail n (1, 2, 4, 8, 16) → lod index.
inline int lod_index(int n) { return n >= 16 ? 4 : n >= 8 ? 3 : n >= 4 ? 2 : n >= 2 ? 1 : 0; }
