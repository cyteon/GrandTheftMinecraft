#include "redstone.h"
#include "audio.h"
#include "fx.h"
#include "items.h"
#include "shapes.h"
#include <unordered_set>
#include <vector>

namespace redstone
{
	static std::unordered_set<uint64_t> s_check;   // components to look at again
	static std::unordered_set<uint64_t> s_powered; // components that were powered last time (we act on changes)

	static Cell nb(const Cell &c, int f) { return {c.b, c.x + FACE_N[f][0], c.y + FACE_N[f][1], c.z + FACE_N[f][2]}; }

	// is this block a power source that's on?
	static bool source(const Block *b)
	{
		if (!b)
			return false;
		const Item &it = item(b->item);
		switch (it.shape)
		{
		case SH_LEVER:
		case SH_BUTTON:
		case SH_PLATE:
			return b->state & ST_OPEN;
		case SH_TORCH:
			return it.name == "redstone_torch";
		default:
			return it.name == "redstone_block";
		}
	}

	// the solid block a source powers through (Minecraft's "strong" power): a lever's or button's wall or floor, the
	// block under a plate
	static bool powers_through(const Cell &src, const Block *b, const Cell &solid)
	{
		const Item &it = item(b->item);
		if (it.shape != SH_LEVER && it.shape != SH_BUTTON && it.shape != SH_PLATE)
			return false;
		Cell on = src;
		if ((b->state & ST_WALL) && it.shape != SH_PLATE)
			on = shapes::step(src, (shapes::dir_of_facing(b->facing) + 2) % 4);
		else
			on.z--;
		return cell_key(on) == cell_key(solid);
	}

	static bool powered(const Cell &c)
	{
		for (int f = 0; f < 6; f++)
		{
			Cell n = nb(c, f);
			const Block *b = block_at(n);
			if (!b)
				continue;
			if (source(b))
				return true;
			if (!item(b->item).opaque())
				continue;
			for (int k = 0; k < 6; k++) // a source powering the solid block next to us
			{
				Cell m = nb(n, k);
				const Block *s = block_at(m);
				if (s && source(s) && powers_through(m, s, n))
					return true;
			}
		}
		return false;
	}

	static bool component(const Item &it)
	{
		return it.shape == SH_DOOR || it.shape == SH_TRAPDOOR || it.shape == SH_GATE || it.shape == SH_LAMP || it.tnt ||
		       it.name == "note_block";
	}

	void on_change(const Cell &c)
	{
		// a source two steps away (through a solid block) can reach a component: look at everything that close
		for (int dx = -2; dx <= 2; dx++)
			for (int dy = -2; dy <= 2; dy++)
				for (int dz = -2; dz <= 2; dz++)
					if (std::abs(dx) + std::abs(dy) + std::abs(dz) <= 2)
						s_check.insert(cell_key({c.b, c.x + dx, c.y + dy, c.z + dz}));
	}

	static void set_open(const Cell &c, const Block &b, bool open)
	{
		int st = open ? b.state | ST_OPEN : b.state & ~ST_OPEN;
		if (st != b.state)
			set_block_state(c, st);
	}

	static void act(const Cell &c, const Block &b, bool on)
	{
		const Item &it = item(b.item);
		V3 at = cell_center(c);
		if (it.tnt)
		{
			if (on)
				fx::prime_tnt(c);
		}
		else if (it.name == "note_block")
		{
			if (on)
				audio::play_at("note/harp", at, 1.0f, 1.0f);
		}
		else if (it.shape == SH_LAMP)
			set_open(c, b, on);
		else if (it.shape == SH_DOOR)
		{
			// both halves, powered from around either
			Cell o = c;
			o.z += b.state & ST_UPPER ? -1 : 1;
			const Block *ob = block_at(o);
			bool was = b.state & ST_OPEN;
			set_open(c, b, on);
			if (ob && ob->item == b.item)
			{
				set_open(o, *ob, on);
				s_powered.erase(cell_key(o));
				if (on)
					s_powered.insert(cell_key(o));
			}
			if (was != on)
			{
				bool metal = it.name.find("iron") != std::string::npos || it.name.find("copper") != std::string::npos;
				audio::play_at(metal ? "block/copper_door/toggle" : on ? "block/wooden_door/open" : "block/wooden_door/close", at);
			}
		}
		else
		{
			bool was = b.state & ST_OPEN;
			set_open(c, b, on);
			if (was != on)
				audio::play_at(it.shape == SH_GATE ? (on ? "block/fence_gate/open" : "block/fence_gate/close")
				                                   : (on ? "block/wooden_trapdoor/open" : "block/wooden_trapdoor/close"),
				               at);
		}
	}

	void update()
	{
		if (s_check.empty())
			return;
		std::vector<uint64_t> keys(s_check.begin(), s_check.end());
		s_check.clear();
		for (uint64_t k : keys)
		{
			auto it = g_blocks.find(k);
			if (it == g_blocks.end())
			{
				s_powered.erase(k);
				continue;
			}
			const Item &i = item(it->second.item);
			if (!component(i))
				continue;
			Cell c = key_cell(k);
			bool on = powered(c);
			if (i.shape == SH_DOOR) // a door is powered from around either half
			{
				Cell o = c;
				o.z += it->second.state & ST_UPPER ? -1 : 1;
				const Block *ob = block_at(o);
				on = on || (ob && ob->item == it->second.item && powered(o));
			}
			bool was = s_powered.count(k) != 0;
			if (on == was)
				continue;
			if (on)
				s_powered.insert(k);
			else
				s_powered.erase(k);
			Block copy = it->second;
			act(c, copy, on);
		}
	}

	void clear()
	{
		s_check.clear();
		s_powered.clear();
	}
}
