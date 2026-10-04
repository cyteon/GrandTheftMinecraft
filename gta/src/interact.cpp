#include "interact.h"
#include "audio.h"
#include "blockrender.h"
#include "fx.h"
#include "gui.h"
#include "hand.h"
#include "input.h"
#include "items.h"
#include "log.h"
#include <cstdio>

namespace interact
{
	Target g_target;
	static uint32_t s_nextBreak = 0, s_nextUse = 0, s_nextPearl = 0;
	static bool s_using = false;      // right button held on a bow / crossbow
	static uint32_t s_useStart = 0;
	static int s_useSlot = -1;
	static int s_loadSounds = 0;      // crossbow loading sounds played (start, middle)
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


	// ---- making room: cars and people standing where a block goes get pushed out instead of being launched by
	// the collision prop that appears inside them ----
	struct Obb
	{
		V3 c, ax[3];
		float h[3];
	};

	static bool entity_obb(int e, Obb &o)
	{
		Vector3 a{}, b{}, up{}, pos{}, mn{}, mx{};
		GET_ENTITY_MATRIX(e, &a, &b, &up, &pos);
		GET_MODEL_DIMENSIONS(GET_ENTITY_MODEL(e), &mn, &mx);
		V3 fwd = GET_ENTITY_FORWARD_VECTOR(e);
		V3 A(a), B(b);
		// the DB's out-parameter order is disputed: take whichever axis matches the entity's forward as Y
		V3 right = std::fabs(A.dot(fwd)) > std::fabs(B.dot(fwd)) ? B : A;
		V3 forward = std::fabs(A.dot(fwd)) > std::fabs(B.dot(fwd)) ? A : B;
		o.ax[0] = right.norm(), o.ax[1] = forward.norm(), o.ax[2] = V3(up).norm();
		V3 lc((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);
		o.c = V3(pos) + o.ax[0] * lc.x + o.ax[1] * lc.y + o.ax[2] * lc.z;
		o.h[0] = (mx.x - mn.x) * 0.5f, o.h[1] = (mx.y - mn.y) * 0.5f, o.h[2] = (mx.z - mn.z) * 0.5f;
		return o.h[0] > 0.01f && o.h[1] > 0.01f;
	}

	// separating-axis test of a box against an axis-aligned cell (shrunk a little so touching isn't overlapping)
	static bool obb_hits_cell(const Obb &o, const V3 &mn)
	{
		const float m = 0.02f;
		V3 cc = mn + V3(0.5f, 0.5f, 0.5f), d = o.c - cc;
		const V3 W[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
		auto sep = [&](const V3 &axis) {
			float ro = 0, rc = (0.5f - m) * (std::fabs(axis.x) + std::fabs(axis.y) + std::fabs(axis.z));
			for (int i = 0; i < 3; i++)
				ro += o.h[i] * std::fabs(o.ax[i].dot(axis));
			return std::fabs(d.dot(axis)) > ro + rc;
		};
		for (int i = 0; i < 3; i++)
			if (sep(W[i]) || sep(o.ax[i]))
				return false;
		return true;
	}

	static bool obb_hits_blocks(const Obb &o)
	{
		float r = std::sqrt(o.h[0] * o.h[0] + o.h[1] * o.h[1] + o.h[2] * o.h[2]);
		for (int b = 0; b < (int)g_builds.size(); b++)
		{
			const Build &bd = g_builds[b];
			if (bd.count <= 0)
				continue;
			int x0 = (int)std::floor(o.c.x - r), x1 = (int)std::floor(o.c.x + r);
			int y0 = (int)std::floor(o.c.y - r), y1 = (int)std::floor(o.c.y + r);
			int z0 = (int)std::floor(o.c.z - r - bd.zOff), z1 = (int)std::floor(o.c.z + r - bd.zOff);
			for (int x = x0; x <= x1; x++)
				for (int y = y0; y <= y1; y++)
					for (int z = z0; z <= z1; z++)
						if (g_blocks.count(cell_key({b, x, y, z})) && obb_hits_cell(o, cell_min({b, x, y, z})))
							return true;
		}
		return false;
	}

	// Push every car and ped overlapping the new block's cell (already placed) to the nearest free spot.
	static void make_room(const Cell &c)
	{
		V3 mn = cell_min(c), cc = mn + V3(0.5f, 0.5f, 0.5f);
		static int ents[512];
		for (int pass = 0; pass < 2; pass++)
		{
			int n = pass == 0 ? shv::worldGetAllVehicles(ents, 512) : shv::worldGetAllPeds(ents, 512);
			for (int i = 0; i < n; i++)
			{
				int e = ents[i];
				if (e == g.ped || !DOES_ENTITY_EXIST(e))
					continue;
				V3 ep = GET_ENTITY_COORDS(e, TRUE);
				if ((ep - cc).len2() > 12 * 12)
					continue;
				Obb o;
				if (!entity_obb(e, o) || !obb_hits_cell(o, mn))
					continue;
				// try directions (away from the block first, then around it), shortest free move wins
				V3 away = V3(o.c.x - cc.x, o.c.y - cc.y, 0);
				float base = away.len2() > 1e-4f ? std::atan2(away.y, away.x) : 0.0f;
				bool moved = false;
				for (float dist = 0.25f; dist <= 6.0f && !moved; dist += 0.25f)
					for (int k = 0; k < 8 && !moved; k++)
					{
						float ang = base + (k % 2 ? 1 : -1) * ((k + 1) / 2) * (PI / 4);
						V3 off(std::cos(ang) * dist, std::sin(ang) * dist, 0);
						Obb t = o;
						t.c += off;
						if (obb_hits_blocks(t))
							continue;
						// don't shove it into a wall either
						if (gta_probe(o.c, t.c + off.norm() * 0.5f, e, 1 | 16).hit)
							continue;
						V3 np = ep + off;
						SET_ENTITY_COORDS_NO_OFFSET(e, np.x, np.y, np.z, FALSE, FALSE, FALSE);
						if (pass == 0)
							SET_VEHICLE_ON_GROUND_PROPERLY(e, 5.0f);
						moved = true;
					}
				if (!moved) // boxed in: lift it on top of the block instead
				{
					float lift = (mn.z + 1.0f) - (o.c.z - o.h[2]) + 0.05f;
					SET_ENTITY_COORDS_NO_OFFSET(e, ep.x, ep.y, ep.z + lift, FALSE, FALSE, FALSE);
				}
			}
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
		make_room(c);
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

	void cancel_use() { s_using = false; }

	int hand_use(float &progress)
	{
		const Slot &sl = g_hotbar[g_sel];
		progress = 0;
		if (sl.empty())
			return hand::USE_NONE;
		const std::string &n = item(sl.item).name;
		if (n == "crossbow" && sl.loaded)
			return hand::USE_CROSSBOW_LOADED;
		if (!s_using || s_useSlot != g_sel)
			return hand::USE_NONE;
		float held = (g.now - s_useStart) / 1000.0f;
		if (n == "bow")
		{
			progress = held;
			return hand::USE_BOW;
		}
		if (n == "crossbow")
		{
			progress = std::min(1.0f, held / 1.25f);
			return hand::USE_CROSSBOW_LOAD;
		}
		return hand::USE_NONE;
	}

	bool holding_ranged()
	{
		const Slot &sl = g_hotbar[g_sel];
		if (sl.empty())
			return false;
		const std::string &n = item(sl.item).name;
		return n == "bow" || n == "crossbow";
	}

	// Arrows leave from the player's head (not the camera, which is behind you in third person and in vehicles)
	// towards whatever the crosshair is on.
	static void aim(V3 &from, V3 &dir)
	{
		V3 target = g.camPos + g.camDir * 300.0f;
		GtaHit h = gta_probe_self(g.camPos, target);
		if (h.hit)
			target = h.pos;
		VoxelHit vh = voxel_raycast(g.camPos, g.camDir, 300.0f);
		if (vh.hit && (!h.hit || vh.t < (h.pos - g.camPos).len()))
			target = vh.pos;
		bool fp = !g.inVehicle && GET_FOLLOW_PED_CAM_VIEW_MODE() == 4;
		from = fp ? g.camPos : V3(GET_PED_BONE_COORDS(g.ped, 31086, 0, 0, 0)) + V3(0, 0, 0.1f);
		dir = (target - from).norm();
		if (dir.dot(g.camDir) < 0.2f) // target behind the head (very close walls): just use the camera's direction
			dir = g.camDir;
	}

	// Bow: hold to draw, release to shoot (Minecraft's power curve). Crossbow: hold 1.25 s to load, release, then
	// click to fire. Returns true if the held item is a bow/crossbow (so the generic right-click use is skipped).
	static bool bows(bool allowInput)
	{
		Slot &sl = g_hotbar[g_sel];
		const Item *h = sl.empty() ? nullptr : &item(sl.item);
		bool bow = h && h->name == "bow", xbow = h && h->name == "crossbow";
		if (!bow && !xbow)
		{
			s_using = false;
			return false;
		}
		if (s_using && s_useSlot != g_sel)
			s_using = false;
		bool held = allowInput && input::mouse_held(VK_RBUTTON);
		bool pressed = allowInput && input::mouse_pressed(VK_RBUTTON);
		float t = (g.now - s_useStart) / 1000.0f;
		if (xbow && sl.loaded)
		{
			if (pressed)
			{
				gui::swing();
				sl.loaded = false;
				V3 from, dir;
				aim(from, dir);
				fx::shoot_arrow(from, dir, 63.0f, 70, false); // 3.15 blocks/tick
				audio::play_at("item/crossbow/shoot", g.camPos, 1.0f, frand(0.9f, 1.1f));
				s_using = false;
				s_nextUse = g.now + 300; // don't start loading again from the same click
			}
			return true;
		}
		if (!s_using)
		{
			if (pressed && g.now >= s_nextUse)
			{
				s_using = true;
				s_useStart = g.now;
				s_useSlot = g_sel;
				s_loadSounds = 0;
			}
			return true;
		}
		if (xbow)
		{
			float p = t / 1.25f;
			if (s_loadSounds == 0 && p >= 0.2f)
				audio::play_at("item/crossbow/loading_start", g.camPos, 0.5f), s_loadSounds = 1;
			if (s_loadSounds == 1 && p >= 0.5f)
				audio::play_at("item/crossbow/loading_middle", g.camPos, 0.5f), s_loadSounds = 2;
			if (s_loadSounds == 2 && p >= 1.0f)
				audio::play_at("item/crossbow/loading_end", g.camPos, 0.5f), s_loadSounds = 3;
			if (!held)
			{
				if (p >= 1.0f)
					sl.loaded = true;
				s_using = false;
			}
			return true;
		}
		// bow
		if (!held)
		{
			s_using = false;
			float f = std::min(t, 1.0f);
			float power = std::min(1.0f, (f * f + f * 2.0f) / 3.0f);
			if (power < 0.1f)
				return true;
			int dmg = (int)std::ceil(power * 3.0f * 2.0f); // Minecraft: ceil(speed * base damage 2)
			bool crit = power >= 1.0f;
			if (crit)
				dmg += rand() % (dmg / 2 + 2);
			gui::swing();
			V3 from, dir;
			aim(from, dir);
			fx::shoot_arrow(from, dir, power * 60.0f, dmg * 10, crit);
			audio::play_at("random/bow", g.camPos, 1.0f, 1.0f / frand(1.2f, 1.6f) + power * 0.5f);
		}
		return true;
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
		bool bowHeld = bows(true);
		if (!bowHeld && (input::mouse_pressed(VK_RBUTTON) || (input::mouse_held(VK_RBUTTON) && g.now >= s_nextUse)))
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
