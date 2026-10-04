#include "interact.h"
#include "audio.h"
#include "blockrender.h"
#include "fx.h"
#include "gui.h"
#include "input.h"
#include "items.h"
#include "log.h"
#include <cstdio>

namespace interact
{
	Target g_target;
	static uint32_t s_nextBreak = 0, s_nextUse = 0, s_nextPearl = 0;
	static const Hash WEAPON_UNARMED = 0xA2719263;

	static void find_target()
	{
		Target t;
		V3 head = GET_PED_BONE_COORDS(g.ped, 31086 /* SKEL_Head */, 0, 0, 0);
		float reach = 5.0f + (g.camPos - head).len();
		VoxelHit vh = voxel_raycast(g.camPos, g.camDir, reach);
		GtaHit gh = gta_probe(g.camPos, g.camPos + g.camDir * reach, g.ped);
		if (gh.hit && collision::is_ours(gh.entity))
			gh.hit = false; // our own invisible collision prop: the voxel ray is exact
		if (vh.hit && (!gh.hit || vh.t <= gh.t + 0.01f))
		{
			t.kind = Target::BLOCK;
			t.vh = vh;
		}
		else if (gh.hit)
		{
			t.gh = gh;
			int type = gh.entity && DOES_ENTITY_EXIST(gh.entity) ? GET_ENTITY_TYPE(gh.entity) : 0;
			t.kind = type == 1 ? Target::PED : type == 2 ? Target::VEHICLE : type == 3 ? Target::OBJECT : Target::GROUND;
		}
		g_target = t;
	}

	static const Item *held()
	{
		const Slot &s = g_hotbar[g_sel];
		return s.empty() ? nullptr : &item(s.item);
	}

	static void attack()
	{
		gui::swing();
		const Item *h = held();
		bool sword = h && h->name == "diamond_sword";
		Target &t = g_target;
		if (t.kind == Target::PED)
		{
			int ped = t.gh.entity;
			int dmg = sword ? 70 : 10; // Minecraft damage x10 (GTA peds have ~100 effective health)
			SET_PED_TO_RAGDOLL(ped, 1200, 1200, 0, FALSE, FALSE, FALSE);
			APPLY_DAMAGE_TO_PED(ped, dmg, FALSE, 0, WEAPON_UNARMED);
			V3 kb = V3(g.camDir.x, g.camDir.y, 0).norm() * (sword ? 14.0f : 8.0f) + V3(0, 0, 4.0f);
			APPLY_FORCE_TO_ENTITY(ped, 1, kb.x, kb.y, kb.z, 0, 0, 0, 0, FALSE, TRUE, TRUE, FALSE, TRUE);
			audio::play_at(sword ? "entity/player/attack/strong" : "damage/hit", t.gh.pos, 1.0f, 1.0f);
			fx::crit(t.gh.pos);
		}
		else if (t.kind == Target::VEHICLE)
		{
			int veh = t.gh.entity;
			V3 v = GET_ENTITY_VELOCITY(veh);
			V3 kb = V3(g.camDir.x, g.camDir.y, 0).norm() * (sword ? 10.0f : 3.0f) + V3(0, 0, sword ? 4.0f : 1.0f);
			SET_ENTITY_VELOCITY(veh, v.x + kb.x, v.y + kb.y, v.z + kb.z);
			audio::play_at(sword ? "entity/player/attack/knockback" : "damage/hit", t.gh.pos, 1.0f, 1.0f);
		}
		else if (t.kind == Target::BLOCK)
		{
			const Block *b = block_at(t.vh.cell);
			if (!b)
				return;
			int it = b->item;
			fx::block_break(t.vh.cell, it);
			audio::block_sound(item(it).sound, cell_center(t.vh.cell), true);
			remove_block(t.vh.cell);
		}
	}

	static bool overlaps_player(const V3 &mn)
	{
		V3 p = g.pedPos;
		V3 a(p.x - 0.3f, p.y - 0.3f, p.z - 0.98f), b(p.x + 0.3f, p.y + 0.3f, p.z + 0.85f);
		return mn.x < b.x && mn.x + 1 > a.x && mn.y < b.y && mn.y + 1 > a.y && mn.z < b.z && mn.z + 1 > a.z;
	}

	static bool place(int itemId)
	{
		Target &t = g_target;
		Cell c;
		if (t.kind == Target::BLOCK)
		{
			const int *n = FACE_N[t.vh.face];
			c = {t.vh.cell.b, t.vh.cell.x + n[0], t.vh.cell.y + n[1], t.vh.cell.z + n[2]};
		}
		else if (t.kind == Target::GROUND || t.kind == Target::OBJECT)
		{
			int b = build_for_point(t.gh.pos);
			if (b < 0)
				return false;
			V3 p = t.gh.pos, n = t.gh.normal;
			if (n.z > 0.7f) // floor: stand on it (sink rather than float when the build's grid is offset)
				c = {b, (int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z - g_builds[b].zOff + 0.05f)};
			else
				c = world_to_cell(b, p + n * 0.5f);
		}
		else
			return false;
		if (block_at(c) || overlaps_player(cell_min(c)))
			return false;
		if (!place_block(c, itemId))
			return false;
		audio::block_sound(item(itemId).sound, cell_center(c), false);
		return true;
	}

	static void use()
	{
		const Item *h = held();
		if (!h)
			return;
		Target &t = g_target;
		if (h->name == "ender_pearl")
		{
			if (g.now < s_nextPearl)
				return;
			s_nextPearl = g.now + 1000; // Minecraft's 20-tick cooldown
			gui::swing();
			fx::throw_pearl(g.camPos, g.camDir);
		}
		else if (h->name == "flint_and_steel")
		{
			gui::swing();
			if (t.kind == Target::BLOCK)
			{
				const Block *b = block_at(t.vh.cell);
				if (b && item(b->item).tnt)
				{
					fx::prime_tnt(t.vh.cell);
					return;
				}
				const int *n = FACE_N[t.vh.face];
				V3 p = cell_center(t.vh.cell) + V3((float)n[0], (float)n[1], (float)n[2]) * 0.6f;
				START_SCRIPT_FIRE(p.x, p.y, p.z, 5, FALSE);
				audio::play_at("fire/ignite", p, 1.0f, frand(0.8f, 1.2f));
			}
			else if (t.kind != Target::NONE)
			{
				if (t.kind == Target::PED || t.kind == Target::VEHICLE)
					START_ENTITY_FIRE(t.gh.entity);
				else
					START_SCRIPT_FIRE(t.gh.pos.x, t.gh.pos.y, t.gh.pos.z, 5, FALSE);
				audio::play_at("fire/ignite", t.gh.pos, 1.0f, frand(0.8f, 1.2f));
			}
		}
		else if (h->block)
		{
			if (place(g_hotbar[g_sel].item))
				gui::swing();
		}
	}

	void update(bool allowInput)
	{
		find_target();
		if (g_target.kind == Target::BLOCK)
			blockrender::draw_outline(g_target.vh.cell);
		if (!allowInput)
			return;
		// creative: instant break; holding repeats every 5 ticks, use every 4 ticks
		if (input::mouse_pressed(VK_LBUTTON) || (input::mouse_held(VK_LBUTTON) && g.now >= s_nextBreak))
		{
			s_nextBreak = g.now + 250;
			attack();
		}
		if (input::mouse_pressed(VK_RBUTTON) || (input::mouse_held(VK_RBUTTON) && g.now >= s_nextUse))
		{
			s_nextUse = g.now + 200;
			use();
		}
		if (input::mouse_pressed(VK_MBUTTON) && g_target.kind == Target::BLOCK)
		{
			const Block *b = block_at(g_target.vh.cell);
			if (b)
				gui::set_selected_item(b->item);
		}
	}

	std::string describe()
	{
		char buf[160];
		const Target &t = g_target;
		if (t.kind == Target::NONE)
			return "Target: none";
		if (t.kind == Target::BLOCK)
		{
			const Block *b = block_at(t.vh.cell);
			std::snprintf(buf, sizeof buf, "Target: %s b%d (%d, %d, %d) face %d", b ? item(b->item).name.c_str() : "?",
			              t.vh.cell.b, t.vh.cell.x, t.vh.cell.y, t.vh.cell.z, t.vh.face);
			return buf;
		}
		const char *k = t.kind == Target::PED ? "ped" : t.kind == Target::VEHICLE ? "vehicle" : t.kind == Target::OBJECT ? "object" : "ground";
		std::snprintf(buf, sizeof buf, "Target: %s at %.2f %.2f %.2f n %.2f %.2f %.2f", k, t.gh.pos.x, t.gh.pos.y,
		              t.gh.pos.z, t.gh.normal.x, t.gh.normal.y, t.gh.normal.z);
		return buf;
	}
}
