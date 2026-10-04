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

struct Item
{
	std::string name, display, tab, sound;
	bool block = false;
	bool alpha = false, light = false, gravity = false, tnt = false, cutout = false;
	int maxStack = 64;
	int icon = -1; // drawTexture id
	// Face colours per level of detail: lod[k] holds an n x n grid (n = 1 << k) for top/side/bottom.
	std::vector<Rgba> lod[5][3];
	bool opaque() const { return block && !alpha && !cutout; }
};

extern std::vector<Item> g_items;
bool items_load();
int item_find(const std::string &name); // -1 if unknown
inline const Item &item(int id) { return g_items[id]; }

// Detail n (1, 2, 4, 8, 16) → lod index.
inline int lod_index(int n) { return n >= 16 ? 4 : n >= 8 ? 3 : n >= 4 ? 2 : n >= 2 ? 1 : 0; }
