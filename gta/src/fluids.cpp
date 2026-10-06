#include "fluids.h"
#include "audio.h"
#include "collision.h"
#include "config.h"
#include "flight.h"
#include "items.h"
#include "shapes.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fluids
{
	static int s_water = -2, s_lava = -2, s_obsidian = -1, s_cobble = -1, s_stone = -1;
	static std::unordered_set<uint64_t> s_active[3]; // per kind: cells to look at on the next tick
	static uint32_t s_next[3] = {};
	static std::unordered_map<uint64_t, uint8_t> s_gta; // cell -> 1 free, 2 GTA geometry in it
	static int s_probes = 0;                          // probe budget left this tick
	static const int MAX_CELLS = 3000;
	static int s_count = 0;
	static bool s_lookOnly = false; // searching ahead: no hardening

	static void ids()
	{
		if (s_water != -2)
			return;
		s_water = item_find("water"), s_lava = item_find("lava");
		s_obsidian = item_find("obsidian"), s_cobble = item_find("cobblestone"), s_stone = item_find("stone");
	}

	int kind(const Block *b)
	{
		ids();
		if (!b)
			return 0;
		return b->item == s_water ? 1 : b->item == s_lava ? 2 : 0;
	}
	static int item_of(int k) { return k == 1 ? s_water : s_lava; }
	static int level(const Block *b) { return b->state & 7; }
	static bool falls(const Block *b) { return b->state & FALLING; }
	static bool source(const Block *b) { return !(b->state & 7) && !(b->state & FALLING); }
	int count() { return s_count; }

	// GTA's own world in a cell: a vertical probe through its middle (cached; the city doesn't move)
	static int gta_cell(const Cell &c)
	{
		uint64_t k = cell_key(c);
		auto it = s_gta.find(k);
		if (it != s_gta.end())
			return it->second;
		if (s_probes <= 0)
			return 0; // unknown: try again next tick
		s_probes--;
		V3 mn = cell_min(c);
		GtaHit h = gta_probe(mn + V3(0.5f, 0.5f, 0.98f), mn + V3(0.5f, 0.5f, 0.02f), g.ped, 1 | 16);
		uint8_t v = h.hit && !collision::is_ours(h.entity) ? 2 : 1;
		s_gta[k] = v;
		return v;
	}

	void on_change(const Cell &c)
	{
		ids();
		for (int f = -1; f < 6; f++)
		{
			Cell n = c;
			if (f >= 0)
				n.x += FACE_N[f][0], n.y += FACE_N[f][1], n.z += FACE_N[f][2];
			int k = kind(block_at(n));
			if (k)
				s_active[k].insert(cell_key(n));
		}
	}

	static void set(const Cell &c, int k, int lvl, bool fall)
	{
		const Block *b = block_at(c);
		if (!b)
			s_count++;
		place_block(c, item_of(k), 0, (lvl & 7) | (fall ? FALLING : 0));
	}

	static void remove(const Cell &c)
	{
		remove_block(c);
		s_count = std::max(0, s_count - 1);
	}

	// lava touching water hardens (Minecraft's LavaFluid / LiquidBlock rules)
	static bool harden(const Cell &c, const Block *lava, bool fromAbove)
	{
		int to = fromAbove ? s_stone : source(lava) ? s_obsidian : s_cobble;
		if (to < 0)
			return false;
		place_block(c, to, 0, 0);
		s_count = std::max(0, s_count - 1);
		audio::play_at("random/fizz", cell_center(c), 0.5f, frand(2.2f, 2.8f));
		return true;
	}

	// can fluid of kind k (at flow level lvl) run into n? Washes away torches, plants and the like.
	static bool can_enter(const Cell &n, int k, int lvl, bool down)
	{
		const Block *b = block_at(n);
		if (b)
		{
			int nk = kind(b);
			if (nk == k)
				return !source(b) && (down ? !falls(b) : (falls(b) ? false : level(b) > lvl));
			if (nk && k == 1 && nk == 2 && !s_lookOnly) // water running into lava
			{
				harden(n, b, down);
				return false;
			}
			if (nk)
				return false; // lava into water: the lava hardens on its own tick
			const Item &it = item(b->item);
			return it.passable();
		}
		if (s_count >= MAX_CELLS)
			return false;
		return gta_cell(n) == 1;
	}

	static void flow_into(const Cell &n, int k, int lvl, bool fall)
	{
		const Block *b = block_at(n);
		if (b && !kind(b))
			remove_block(n); // washed away
		set(n, k, lvl, fall);
	}

	// Minecraft's FlowingFluid.getSpread: the directions leading to the nearest drop (within 4 for water, 2 for
	// lava), or all open directions if there's none
	static int spread_dirs(const Cell &c, int k, int lvl)
	{
		int reach = k == 1 ? 4 : 2, best = 1000, mask = 0;
		for (int d = 0; d < 4; d++)
		{
			Cell n = shapes::step(c, d);
			if (!can_enter(n, k, lvl, false))
				continue;
			// breadth-first over open cells for a hole
			int dist = 1000;
			std::vector<std::pair<Cell, int>> q = {{n, 0}};
			std::unordered_set<uint64_t> seen = {cell_key(c), cell_key(n)};
			for (size_t i = 0; i < q.size() && dist == 1000; i++)
			{
				Cell cur = q[i].first;
				Cell below = cur;
				below.z--;
				s_lookOnly = true;
				bool hole = can_enter(below, k, 0, true);
				s_lookOnly = false;
				if (hole)
				{
					dist = q[i].second;
					break;
				}
				if (q[i].second >= reach)
					continue;
				for (int e = 0; e < 4; e++)
				{
					Cell m = shapes::step(cur, e);
					if (seen.insert(cell_key(m)).second && !block_at(m) && gta_cell(m) == 1)
						q.push_back({m, q[i].second + 1});
				}
			}
			if (dist < best)
				best = dist, mask = 1 << d;
			else if (dist == best)
				mask |= 1 << d;
		}
		return mask;
	}

	static void tick_cell(const Cell &c, int k)
	{
		const Block *b = block_at(c);
		if (kind(b) != k)
			return;
		int drop = k == 1 ? 1 : 2;
		// lava next to water (not below it) hardens
		if (k == 2)
			for (int f = 0; f < 6; f++)
			{
				if (f == 1)
					continue;
				Cell n = c;
				n.x += FACE_N[f][0], n.y += FACE_N[f][1], n.z += FACE_N[f][2];
				if (kind(block_at(n)) == 1)
				{
					harden(c, b, false);
					return;
				}
			}
		// water hardens concrete powder it touches
		if (k == 1)
			for (int f = 0; f < 6; f++)
			{
				Cell n = c;
				n.x += FACE_N[f][0], n.y += FACE_N[f][1], n.z += FACE_N[f][2];
				const Block *nb = block_at(n);
				if (!nb)
					continue;
				const std::string &nm = item(nb->item).name;
				size_t p = nm.find("_concrete_powder");
				if (p != std::string::npos)
				{
					int to = item_find(nm.substr(0, p) + "_concrete");
					if (to >= 0)
						place_block(n, to, 0, 0);
				}
			}
		Cell up = c, below = c;
		up.z++, below.z--;
		// a flowing cell takes its level from what feeds it, or dries up
		if (!source(b))
		{
			int lvl = 8, sources = 0;
			bool fall = kind(block_at(up)) == k;
			if (!fall)
				for (int d = 0; d < 4; d++)
				{
					const Block *nb = block_at(shapes::step(c, d));
					if (kind(nb) != k)
						continue;
					if (source(nb))
						sources++;
					lvl = std::min(lvl, (source(nb) || falls(nb)) ? drop : level(nb) + drop);
				}
			if (k == 1 && sources >= 2)
			{
				const Block *bb = block_at(below);
				bool floor = bb ? (kind(bb) == 1 ? source(bb) : !item(bb->item).passable()) : gta_cell(below) == 2;
				if (floor)
				{
					set(c, k, 0, false); // infinite water
					s_active[k].insert(cell_key(c));
					return;
				}
			}
			if (fall)
				lvl = 0;
			if (!fall && lvl > 7)
			{
				remove(c);
				return;
			}
			if (lvl != level(b) || fall != falls(b))
			{
				set(c, k, lvl, fall);
				b = block_at(c);
			}
		}
		// down first; a source or a cell on a floor also spreads sideways
		if (can_enter(below, k, 0, true))
		{
			flow_into(below, k, 0, true);
			return;
		}
		int next = falls(b) ? drop : level(b) + drop;
		if (next > 7)
			return;
		int dirs = spread_dirs(c, k, next);
		for (int d = 0; d < 4; d++)
			if (dirs & (1 << d))
			{
				Cell n = shapes::step(c, d);
				if (can_enter(n, k, next, false))
					flow_into(n, k, next, false);
			}
	}

	bool place(const Cell &c, int k)
	{
		ids();
		if (item_of(k) < 0)
			return false;
		const Block *b = block_at(c);
		if (b && !kind(b) && !item(b->item).passable())
			return false;
		if (b && !kind(b))
			remove_block(c);
		set(c, k, 0, false);
		s_active[k].insert(cell_key(c));
		return true;
	}

	void clear()
	{
		for (auto &a : s_active)
			a.clear();
		s_gta.clear();
	}

	// people and cars in fluids
	static void effects()
	{
		static uint32_t next = 0;
		if (g.now < next)
			return;
		next = g.now + 250;
		if (!s_count && kind(nullptr) == 0)
		{
			bool any = false;
			for (auto &kv : g_blocks)
				if (kind(&kv.second))
				{
					any = true;
					break;
				}
			if (!any)
				return;
		}
		static int ents[512];
		auto in_fluid = [](const V3 &p, int &k, Cell &cell) {
			int b = build_for_point(p);
			if (b < 0)
				return false;
			cell = world_to_cell(b, p);
			const Block *bk = block_at(cell);
			k = kind(bk);
			return k != 0;
		};
		// the flow at a cell: towards neighbours that are lower (Minecraft's FlowingFluid.getFlow, simplified)
		auto flow = [](const Cell &c) {
			const Block *b = block_at(c);
			V3 f(0, 0, 0);
			if (!b)
				return f;
			if (falls(b))
				return V3(0, 0, -1);
			for (int d = 0; d < 4; d++)
			{
				Cell n = shapes::step(c, d);
				const Block *nb = block_at(n);
				int diff = nb && kind(nb) == kind(b) ? (falls(nb) ? 0 : level(nb) - level(b)) : 0;
				if (!nb)
				{
					Cell nd = n;
					nd.z--;
					if (kind(block_at(nd)) == kind(b))
						diff = 8;
				}
				V3 dir((float)(d == 3) - (float)(d == 1), (float)(d == 0) - (float)(d == 2), 0);
				f += dir * (float)std::max(0, diff);
			}
			return f.len2() > 0 ? f.norm() : f;
		};
		int np = shv::worldGetAllPeds(ents, 512);
		for (int i = 0; i < np; i++)
		{
			int ped = ents[i];
			if (!DOES_ENTITY_EXIST(ped) || IS_ENTITY_DEAD(ped, FALSE))
				continue;
			V3 p = GET_ENTITY_COORDS(ped, TRUE);
			if ((p - g.pedPos).len2() > 60 * 60)
				continue;
			int k;
			Cell c;
			bool me = ped == g.ped;
			if (!in_fluid(p - V3(0, 0, 0.8f), k, c) && !in_fluid(p, k, c))
				continue;
			if (k == 2)
			{
				if (me && (g_cfg.invincible || flight::active()))
					continue;
				if (!IS_ENTITY_ON_FIRE(ped))
					START_ENTITY_FIRE(ped);
				APPLY_DAMAGE_TO_PED(ped, 20, FALSE, 0, 0xA2719263 /* unarmed */); // Minecraft: 4 a second
			}
			else
			{
				if (IS_ENTITY_ON_FIRE(ped))
					STOP_ENTITY_FIRE(ped);
				if (me && (flight::active() || flight::climbing()))
					continue;
				V3 f = flow(c);
				if (f.len2() > 0)
				{
					V3 v = GET_ENTITY_VELOCITY(ped);
					SET_ENTITY_VELOCITY(ped, v.x + f.x * 1.2f, v.y + f.y * 1.2f, v.z + std::min(0.0f, f.z));
				}
			}
		}
		int nv = shv::worldGetAllVehicles(ents, 512);
		for (int i = 0; i < nv; i++)
		{
			int veh = ents[i];
			if (!DOES_ENTITY_EXIST(veh))
				continue;
			V3 p = GET_ENTITY_COORDS(veh, TRUE);
			if ((p - g.pedPos).len2() > 60 * 60)
				continue;
			int k;
			Cell c;
			if (!in_fluid(p - V3(0, 0, 0.4f), k, c))
				continue;
			if (k == 2)
			{
				if (!IS_ENTITY_ON_FIRE(veh))
					START_ENTITY_FIRE(veh);
				SET_VEHICLE_ENGINE_HEALTH(veh, std::max(-1000.0f, GET_VEHICLE_ENGINE_HEALTH(veh) - 60));
			}
			else
			{
				if (IS_ENTITY_ON_FIRE(veh))
					STOP_ENTITY_FIRE(veh);
				V3 v = GET_ENTITY_VELOCITY(veh), f = flow(c);
				SET_ENTITY_VELOCITY(veh, v.x * 0.85f + f.x, v.y * 0.85f + f.y, v.z);
			}
		}
		// water puts out GTA fires in it; lava pops now and then
		static uint32_t nextFx = 0;
		if (g.now >= nextFx)
		{
			nextFx = g.now + 1000;
			int fires = 0, pops = 0;
			for (auto &kv : g_blocks)
			{
				int k = kind(&kv.second);
				if (!k)
					continue;
				V3 cc = cell_center(key_cell(kv.first));
				if ((cc - g.pedPos).len2() > 40 * 40)
					continue;
				if (k == 1 && fires++ < 64)
					STOP_FIRE_IN_RANGE(cc.x, cc.y, cc.z, 1.2f);
				else if (k == 2 && source(&kv.second) && pops < 2 && rand() % 40 == 0)
				{
					pops++;
					audio::play_at("liquid/lavapop", cc, 0.4f, frand(0.9f, 1.05f));
				}
			}
		}
	}

	void update()
	{
		ids();
		if (s_water < 0)
			return;
		for (int k = 1; k <= 2; k++)
		{
			if (g.now < s_next[k] || s_active[k].empty())
				continue;
			s_next[k] = g.now + (k == 1 ? 250 : 1500);
			s_probes = 200;
			std::vector<uint64_t> now(s_active[k].begin(), s_active[k].end());
			s_active[k].clear();
			for (uint64_t key : now)
			{
				Cell c = key_cell(key);
				size_t before = s_active[k].size();
				tick_cell(c, k);
				if (s_probes <= 0 && s_active[k].size() == before && kind(block_at(c)) == k)
					s_active[k].insert(key); // ran out of probes: look again
			}
		}
		effects();
	}
}
