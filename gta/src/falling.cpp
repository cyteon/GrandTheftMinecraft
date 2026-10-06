#include "falling.h"
#include "audio.h"
#include "blockrender.h"
#include "collision.h"
#include "fluids.h"
#include "items.h"
#include "rig.h"
#include "shapes.h"
#include <unordered_set>
#include <vector>

namespace falling
{
	struct Faller
	{
		int item;
		uint8_t facing;
		int build;
		V3 pos;        // bottom centre
		float vz = 0;  // m/s
		float startZ;
		int obj = 0;
		Hash model = 0;
	};
	static std::vector<Faller> s_fallers;
	static std::unordered_set<uint64_t> s_check;

	int count() { return (int)s_fallers.size(); }

	void on_change(const Cell &c)
	{
		s_check.insert(cell_key(c));
		Cell up = c;
		up.z++;
		s_check.insert(cell_key(up));
	}

	// is there something to stand on under this cell? (our solid blocks, else GTA's world)
	static bool supported(const Cell &c)
	{
		Cell below = c;
		below.z--;
		if (const Block *b = block_at(below))
			return !item(b->item).passable();
		V3 mn = cell_min(c);
		GtaHit h = gta_probe(mn + V3(0.5f, 0.5f, 0.1f), mn + V3(0.5f, 0.5f, -0.1f), g.ped, 1 | 16);
		return h.hit && !collision::is_ours(h.entity);
	}

	static void land(Faller &f)
	{
		// the cell the block comes to rest in: the one its bottom is in
		const Build &bd = g_builds[f.build];
		Cell c = {f.build, (int)std::floor(f.pos.x), (int)std::floor(f.pos.y), (int)std::floor(f.pos.z - bd.zOff + 0.02f)};
		for (int i = 0; i < 4; i++) // something solid already there: one up
		{
			const Block *b = block_at(c);
			if (!b || item(b->item).passable())
				break;
			c.z++;
		}
		if (block_at(c))
			remove_block(c); // plants, torches, water: replaced
		const Item &it = item(f.item);
		int placed = f.item;
		// concrete powder that lands in or by water hardens
		size_t p = it.name.find("_concrete_powder");
		if (p != std::string::npos)
			for (int k = 0; k < 6; k++)
			{
				Cell n = c;
				n.x += FACE_N[k][0], n.y += FACE_N[k][1], n.z += FACE_N[k][2];
				if (fluids::kind(block_at(n)) == 1)
				{
					int to = item_find(it.name.substr(0, p) + "_concrete");
					if (to >= 0)
						placed = to;
					break;
				}
			}
		place_block(c, placed, f.facing, 0);
		V3 at = cell_center(c);
		bool anvil = it.shape == SH_ANVIL;
		if (anvil)
		{
			audio::play_at("random/anvil_land", at, 0.6f, frand(0.9f, 1.1f));
			// Minecraft: 2 damage a block fallen beyond the first, up to 40 (x10 for GTA's health)
			float dist = f.startZ - f.pos.z;
			int dmg = (int)std::min(400.0f, std::max(0.0f, dist - 1) * 20);
			static int ents[512];
			int np = shv::worldGetAllPeds(ents, 512);
			for (int i = 0; i < np && dmg > 0; i++)
			{
				V3 pp = GET_ENTITY_COORDS(ents[i], TRUE);
				if (std::fabs(pp.x - at.x) < 0.8f && std::fabs(pp.y - at.y) < 0.8f && pp.z > at.z - 1.5f && pp.z < at.z + 1.5f)
					APPLY_DAMAGE_TO_PED(ents[i], dmg, FALSE, 0, 0xA2719263 /* unarmed */);
			}
			int nv = shv::worldGetAllVehicles(ents, 512);
			for (int i = 0; i < nv && dist > 1; i++)
			{
				V3 vp = GET_ENTITY_COORDS(ents[i], TRUE);
				if ((V3(vp.x, vp.y, 0) - V3(at.x, at.y, 0)).len() < 2.5f && std::fabs(vp.z - at.z) < 2.0f)
				{
					V3 off = GET_OFFSET_FROM_ENTITY_GIVEN_WORLD_COORDS(ents[i], at.x, at.y, at.z + 0.5f);
					SET_VEHICLE_DAMAGE(ents[i], off.x, off.y, off.z, std::min(400.0f, dist * 40), 1.2f, TRUE);
					SET_VEHICLE_BODY_HEALTH(ents[i], std::max(0.0f, GET_VEHICLE_BODY_HEALTH(ents[i]) - dist * 20));
				}
			}
		}
		else
			audio::block_sound(item(placed).sound, at, false);
	}

	void update()
	{
		// blocks that may have lost their support
		if (!s_check.empty())
		{
			std::vector<uint64_t> keys(s_check.begin(), s_check.end());
			s_check.clear();
			for (uint64_t k : keys)
			{
				auto it = g_blocks.find(k);
				if (it == g_blocks.end() || !item(it->second.item).gravity || s_fallers.size() >= 64)
					continue;
				Cell c = key_cell(k);
				if (supported(c))
					continue;
				Faller f;
				f.item = it->second.item;
				f.facing = it->second.facing;
				f.build = c.b;
				f.pos = cell_min(c) + V3(0.5f, 0.5f, 0);
				f.startZ = f.pos.z;
				remove_block(c);
				s_fallers.push_back(f);
			}
		}
		float dt = std::min(g.dt, 0.1f);
		for (size_t i = 0; i < s_fallers.size();)
		{
			Faller &f = s_fallers[i];
			// Minecraft's FallingBlockEntity: 0.04 blocks/tick^2 down, then 2 % drag a tick
			f.vz = f.vz * std::pow(0.98f, dt * 20) - 16.0f * dt;
			float dz = f.vz * dt;
			V3 next = f.pos + V3(0, 0, dz);
			bool landed = false;
			// our blocks under it
			const Build &bd = g_builds[f.build];
			Cell under = {f.build, (int)std::floor(next.x), (int)std::floor(next.y), (int)std::floor(next.z - bd.zOff)};
			if (const Block *b = block_at(under))
				if (!item(b->item).passable() && cell_min(under).z + 1 > next.z)
				{
					f.pos.z = cell_min(under).z + 1;
					landed = true;
				}
			if (!landed) // GTA's ground (people and cars don't stop it, as in Minecraft)
			{
				GtaHit h = gta_probe(f.pos + V3(0, 0, 0.05f), next, g.ped, 1 | 16);
				if (h.hit && !collision::is_ours(h.entity))
				{
					f.pos.z = h.pos.z;
					landed = true;
				}
			}
			if (landed || f.pos.z < f.startZ - 200)
			{
				rig::drop(f.obj);
				if (landed)
					land(f);
				s_fallers[i] = s_fallers.back();
				s_fallers.pop_back();
				continue;
			}
			f.pos = next;
			// draw it: the block's own prop, else polygons
			const Item &it = item(f.item);
			float yaw = deg2rad(f.facing * 22.5f);
			V3 X(std::cos(yaw), std::sin(yaw), 0), Y(-std::sin(yaw), std::cos(yaw), 0), Z(0, 0, 1);
			if (collision::dlc())
				rig::place(f.obj, f.model, "gtm_" + it.name, f.pos + V3(0, 0, 0.5f), X, Y, Z);
			else
				blockrender::draw_cube(f.pos - V3(0.5f, 0.5f, 0), f.item, 0, 0);
			i++;
		}
	}

	void clear()
	{
		for (auto &f : s_fallers)
			rig::drop(f.obj);
		s_fallers.clear();
		s_check.clear();
	}
}
