#include "fx.h"
#include "audio.h"
#include "blockrender.h"
#include "collision.h"
#include "config.h"
#include "items.h"
#include "log.h"
#include "render2d.h"
#include <algorithm>
#include <string>
#include <vector>

namespace fx
{
	// ---- sprite particles (Minecraft particle textures as screen-projected billboards) ----
	struct Sprite
	{
		V3 p, v;
		float age = 0, life = 1, size = 0.5f;
		const int *frames = nullptr;
		int nFrames = 1;
		bool animByAge = true; // false: frame = fixed index (frames[0])
		uint32_t rgb = 0xFFFFFF;
		float gravity = 0, drag = 0.96f;
		int group = -1;
	};
	struct Group
	{
		V3 center;
		bool visible = true;
		uint32_t nextCheck = 0;
		int live = 0;
	};
	struct Debris
	{
		V3 p, v;
		float life, size, floorZ;
		Rgba c;
	};
	struct Pearl
	{
		V3 p, v;
		float age = 0;
	};
	struct Tnt
	{
		V3 mn;
		int item;
		float fuse, total;
	};

	static std::vector<Sprite> s_sprites;
	static std::vector<Group> s_groups;
	static std::vector<Debris> s_debris;
	static std::vector<Pearl> s_pearls;
	static std::vector<Tnt> s_tnt;
	static int s_explosion[16], s_smoke[12], s_generic[8], s_sweep[8], s_flame[1], s_crit[1];
	static int s_pearlTex = -1;
	static int s_tntItem = -1;

	int pearl_count() { return (int)s_pearls.size(); }
	int tnt_count() { return (int)s_tnt.size(); }
	int particle_count() { return (int)(s_sprites.size() + s_debris.size()); }

	void init()
	{
		for (int i = 0; i < 16; i++)
			s_explosion[i] = r2d::tex("particles/explosion_" + std::to_string(i) + ".png");
		for (int i = 0; i < 12; i++)
			s_smoke[i] = r2d::tex("particles/big_smoke_" + std::to_string(i) + ".png");
		for (int i = 0; i < 8; i++)
			s_generic[i] = r2d::tex("particles/generic_" + std::to_string(i) + ".png");
		for (int i = 0; i < 8; i++)
			s_sweep[i] = r2d::tex("particles/sweep_" + std::to_string(i) + ".png");
		s_flame[0] = r2d::tex("particles/flame.png");
		s_crit[0] = r2d::tex("particles/critical_hit.png");
		s_pearlTex = r2d::tex("items/ender_pearl.png");
		s_tntItem = item_find("tnt");
	}

	static int new_group(const V3 &c)
	{
		for (int i = 0; i < (int)s_groups.size(); i++)
			if (s_groups[i].live <= 0)
			{
				s_groups[i] = {c, true, 0, 0};
				return i;
			}
		s_groups.push_back({c, true, 0, 0});
		return (int)s_groups.size() - 1;
	}

	static void add(const Sprite &s)
	{
		if (s_sprites.size() < 600)
			s_sprites.push_back(s);
	}

	// ---- spawners ----
	void block_break(const Cell &c, int it)
	{
		const Item &i = item(it);
		V3 mn = cell_min(c);
		const auto &grid = i.lod[2][F_SIDE];
		if (grid.empty())
			return;
		for (int k = 0; k < 24 && s_debris.size() < 800; k++)
		{
			Debris d;
			d.p = mn + V3(frand(0.1f, 0.9f), frand(0.1f, 0.9f), frand(0.1f, 0.9f));
			d.v = (d.p - (mn + V3(0.5f, 0.5f, 0.5f))) * 4.0f + V3(0, 0, frand(1.0f, 3.0f));
			d.life = frand(0.4f, 1.0f);
			d.size = frand(0.06f, 0.12f);
			d.floorZ = mn.z;
			d.c = grid[rand() % grid.size()];
			if (d.c.a < 16)
				d.c = grid[0];
			d.c.a = 255;
			s_debris.push_back(d);
		}
	}

	void portal_burst(const V3 &p, int count)
	{
		int grp = new_group(p);
		for (int k = 0; k < count; k++)
		{
			Sprite s;
			s.p = p + V3(frand(-0.6f, 0.6f), frand(-0.6f, 0.6f), frand(-1.0f, 1.0f));
			s.v = V3(frand(-1, 1), frand(-1, 1), frand(-1, 1)) * 1.5f;
			s.life = frand(0.6f, 1.6f);
			s.size = frand(0.1f, 0.25f);
			s.frames = s_generic;
			s.nFrames = 8;
			// Minecraft portal particles: purple with a random brightness
			float f = frand(0.4f, 1.0f);
			s.rgb = ((uint32_t)(0.9f * f * 255) << 16) | ((uint32_t)(0.3f * f * 255) << 8) | (uint32_t)(f * 255);
			s.drag = 0.9f;
			s.group = grp;
			add(s);
		}
	}

	void sweep(const V3 &p, const V3 &dir)
	{
		Sprite s;
		s.p = p + dir * 1.2f;
		s.life = 0.3f;
		s.size = 1.4f;
		s.frames = s_sweep;
		s.nFrames = 8;
		s.drag = 1;
		s.group = new_group(s.p);
		add(s);
	}

	void crit(const V3 &p)
	{
		int grp = new_group(p);
		for (int k = 0; k < 10; k++)
		{
			Sprite s;
			s.p = p + V3(frand(-0.3f, 0.3f), frand(-0.3f, 0.3f), frand(-0.3f, 0.5f));
			s.v = V3(frand(-2, 2), frand(-2, 2), frand(0, 3));
			s.life = frand(0.3f, 0.6f);
			s.size = 0.15f;
			s.frames = s_crit;
			s.nFrames = 1;
			s.gravity = 6;
			s.group = grp;
			add(s);
		}
	}

	void throw_pearl(const V3 &from, const V3 &dir)
	{
		Pearl p;
		p.p = from + dir * 0.6f;
		p.v = dir * 30.0f; // 1.5 blocks/tick
		s_pearls.push_back(p);
		audio::play_at("random/bow", from, 0.5f, frand(0.33f, 0.5f));
	}

	void prime_tnt(const Cell &c, float fuse)
	{
		V3 mn = cell_min(c);
		remove_block(c);
		s_tnt.push_back({mn, s_tntItem >= 0 ? s_tntItem : 0, fuse, fuse});
		audio::play_at("random/fuse", mn + V3(0.5f, 0.5f, 0.5f), 1.0f, 1.0f, 24);
	}

	void explode(const V3 &p, float power, bool fire)
	{
		// GTA damage: peds, cars, physics props
		ADD_EXPLOSION(p.x, p.y, p.z, 2 /* sticky bomb */, 1.0f, FALSE, g_cfg.gtaExplosionFx ? FALSE : TRUE, 1.0f,
		              FALSE);
		if (fire)
			START_SCRIPT_FIRE(p.x, p.y, p.z, 10, FALSE);
		audio::play_at("random/explode", p, 4.0f, frand(0.56f, 0.84f), 64);

		// blocks: remove those within reach with a ragged edge, chain-prime TNT
		std::vector<std::pair<Cell, int>> hit;
		float r = power * 1.1f;
		for (auto &kv : g_blocks)
		{
			Cell c = key_cell(kv.first);
			float d = (cell_center(c) - p).len();
			if (d > r)
				continue;
			const Item &it = item(kv.second.item);
			if (it.name == "obsidian" || it.name == "bedrock")
				continue;
			if (d > power * frand(0.6f, 1.1f))
				continue;
			hit.push_back({c, kv.second.item});
		}
		int debris = 0;
		for (auto &h : hit)
		{
			if (item(h.second).tnt)
			{
				prime_tnt(h.first, frand(0.5f, 1.5f));
				continue;
			}
			if (debris++ < 12)
				block_break(h.first, h.second);
			remove_block(h.first);
		}

		// Minecraft explosion emitter: big animated puffs around the centre + smoke
		int grp = new_group(p);
		for (int k = 0; k < 16; k++)
		{
			Sprite s;
			s.p = p + V3(frand(-1, 1), frand(-1, 1), frand(-1, 1)) * power * 0.5f;
			s.life = frand(0.4f, 0.9f);
			s.size = frand(1.5f, 2.6f);
			s.frames = s_explosion;
			s.nFrames = 16;
			float gray = frand(0.6f, 1.0f);
			uint32_t v = (uint32_t)(gray * 255);
			s.rgb = (v << 16) | (v << 8) | v;
			s.drag = 1;
			s.group = grp;
			add(s);
		}
		for (int k = 0; k < 24; k++)
		{
			Sprite s;
			s.p = p + V3(frand(-1, 1), frand(-1, 1), frand(-0.5f, 1)) * power * 0.4f;
			s.v = V3(frand(-1, 1), frand(-1, 1), frand(0.5f, 2.0f)) * 1.5f;
			s.life = frand(1.0f, 2.5f);
			s.size = frand(0.6f, 1.2f);
			s.frames = s_smoke;
			s.nFrames = 12;
			float gray = frand(0.3f, 0.6f);
			uint32_t v = (uint32_t)(gray * 255);
			s.rgb = (v << 16) | (v << 8) | v;
			s.drag = 0.97f;
			s.group = grp;
			add(s);
		}
	}

	// ---- simulation + drawing ----
	static bool project(const V3 &p, float &sx, float &sy)
	{
		float x = 0, y = 0;
		if (!GET_SCREEN_COORD_FROM_WORLD_COORD(p.x, p.y, p.z, &x, &y))
			return false;
		sx = x * g.screenW;
		sy = y * g.screenH;
		return true;
	}

	static float px_per_metre(float dist)
	{
		float tanHalf = std::tan(deg2rad(g.camFov * 0.5f));
		return (g.screenH * 0.5f) / (std::max(dist, 0.05f) * tanHalf);
	}

	static void update_pearls(float dt)
	{
		for (size_t i = 0; i < s_pearls.size();)
		{
			Pearl &pl = s_pearls[i];
			pl.age += dt;
			V3 next = pl.p + pl.v * dt;
			pl.v.z -= 12.0f * dt; // 0.03 blocks/tick^2
			pl.v *= std::pow(0.99f, dt * 20.0f);
			V3 seg = next - pl.p;
			float segLen = seg.len();
			bool landed = false;
			V3 land, nrm(0, 0, 1);
			if (segLen > 1e-4f)
			{
				V3 dir = seg * (1.0f / segLen);
				GtaHit gh = gta_probe(pl.p, next, g.ped);
				VoxelHit vh = voxel_raycast(pl.p, dir, segLen);
				if (vh.hit && (!gh.hit || vh.t <= gh.t))
				{
					landed = true;
					land = vh.pos;
					nrm = V3((float)FACE_N[vh.face][0], (float)FACE_N[vh.face][1], (float)FACE_N[vh.face][2]);
				}
				else if (gh.hit)
				{
					landed = true;
					land = gh.pos;
					nrm = gh.normal;
				}
			}
			if (landed || pl.age > 12.0f)
			{
				if (landed)
				{
					V3 to = land + nrm * 0.5f;
					if (nrm.z > 0.5f)
						to.z = land.z + 1.0f; // ped origin is ~1 m above its feet
					else if (nrm.z < -0.5f)
						to.z = land.z - 1.2f;
					int ent = g.inVehicle ? PED::GET_VEHICLE_PED_IS_IN(g.ped, FALSE) : g.ped;
					SET_ENTITY_COORDS(ent, to.x, to.y, to.z, FALSE, FALSE, FALSE, FALSE);
					portal_burst(land, 32);
					portal_burst(to, 32);
					audio::play_at("mob/endermen/portal", to, 1.0f, 1.0f);
				}
				s_pearls[i] = s_pearls.back();
				s_pearls.pop_back();
				continue;
			}
			pl.p = next;
			// trail + sprite
			float sx, sy;
			float d = (pl.p - g.camPos).len();
			if (project(pl.p, sx, sy))
			{
				float sz = 0.25f * px_per_metre(d);
				r2d::draw(s_pearlTex, sx - sz * 0.5f, sy - sz * 0.5f, sz, sz, 0xFFFFFFFF, r2d::L_WORLD);
			}
			i++;
		}
	}

	static void update_tnt(float dt)
	{
		for (size_t i = 0; i < s_tnt.size();)
		{
			Tnt &t = s_tnt[i];
			t.fuse -= dt;
			if (t.fuse <= 0)
			{
				V3 c = t.mn + V3(0.5f, 0.5f, 0.5f);
				s_tnt[i] = s_tnt.back();
				s_tnt.pop_back();
				explode(c, 4.0f);
				continue;
			}
			// Minecraft: white flash every 5 ticks, swells in the last 10 ticks
			int ticks = (int)(t.fuse * 20.0f);
			float flash = (ticks / 5) % 2 == 0 ? 0.8f : 0.0f;
			float grow = 0;
			if (t.fuse < 0.5f)
			{
				float f = 1.0f - t.fuse / 0.5f;
				grow = f * f * f * f * 0.3f;
			}
			blockrender::draw_cube(t.mn, t.item, flash, grow);
			i++;
		}
	}

	static void update_debris(float dt)
	{
		for (size_t i = 0; i < s_debris.size();)
		{
			Debris &d = s_debris[i];
			d.life -= dt;
			if (d.life <= 0)
			{
				s_debris[i] = s_debris.back();
				s_debris.pop_back();
				continue;
			}
			d.v.z -= 16.0f * dt;
			d.v.x *= 0.98f, d.v.y *= 0.98f;
			d.p += d.v * dt;
			if (d.p.z < d.floorZ + d.size * 0.5f)
			{
				d.p.z = d.floorZ + d.size * 0.5f;
				d.v = V3(d.v.x * 0.6f, d.v.y * 0.6f, 0);
			}
			blockrender::draw_quad_billboard(d.p, d.size, d.c.r, d.c.g, d.c.b, 255);
			i++;
		}
	}

	static void update_sprites(float dt)
	{
		// one occlusion probe per effect group, refreshed a few times a second
		for (auto &gr : s_groups)
			gr.live = 0;
		for (auto &s : s_sprites)
			if (s.group >= 0)
				s_groups[s.group].live++;
		for (auto &gr : s_groups)
		{
			if (gr.live <= 0 || g.now < gr.nextCheck)
				continue;
			gr.nextCheck = g.now + 150;
			V3 to = gr.center;
			V3 d = to - g.camPos;
			float len = d.len();
			if (len < 1.5f)
			{
				gr.visible = true;
				continue;
			}
			// stop short so the effect's own surface (ground, car) doesn't count as an occluder
			GtaHit h = gta_probe(g.camPos, g.camPos + d * ((len - 1.5f) / len), g.ped, 1 | 16);
			gr.visible = !h.hit || collision::is_ours(h.entity);
		}
		// far → near so near particles draw over far ones (same level, later call wins)
		std::sort(s_sprites.begin(), s_sprites.end(), [](const Sprite &a, const Sprite &b) {
			return (a.p - g.camPos).len2() > (b.p - g.camPos).len2();
		});
		for (size_t i = 0; i < s_sprites.size();)
		{
			Sprite &s = s_sprites[i];
			s.age += dt;
			if (s.age >= s.life)
			{
				s_sprites[i] = s_sprites.back();
				s_sprites.pop_back();
				continue;
			}
			s.v.z -= s.gravity * dt;
			s.v *= std::pow(s.drag, dt * 20.0f);
			s.p += s.v * dt;
			i++;
			if (s.group >= 0 && !s_groups[s.group].visible)
				continue;
			float sx, sy;
			if (!project(s.p, sx, sy))
				continue;
			float d = (s.p - g.camPos).len();
			if (d < 0.3f)
				continue;
			int frame = s.nFrames > 1 ? std::min(s.nFrames - 1, (int)(s.age / s.life * s.nFrames)) : 0;
			// Minecraft's generic/smoke sprites play backwards (big → small)
			if (s.frames == s_generic || s.frames == s_smoke)
				frame = s.nFrames - 1 - frame;
			float sz = s.size * px_per_metre(d);
			if (sz > g.screenH * 2.0f)
				continue;
			r2d::draw(s.frames[frame], sx - sz * 0.5f, sy - sz * 0.5f, sz, sz, 0xFF000000 | s.rgb, r2d::L_WORLD);
		}
	}

	void update()
	{
		float dt = std::min(g.dt, 0.1f);
		update_tnt(dt);
		update_debris(dt);
		update_pearls(dt);
		update_sprites(dt);
	}
}
