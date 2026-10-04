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
		int obj; // Stage 2: a real physics TNT block (0 = polygon cube)
	};
	struct Chip
	{
		int obj;
		float life;
	};
	struct Arrow
	{
		V3 p, v, dir;
		float age = 0;
		int obj = 0;
		bool stuck = false, crit = false;
		int damage = 0;
		uint64_t stuckCell = 0; // block it's stuck in (0 = GTA world)
		bool inBlock = false;
	};

	static std::vector<Sprite> s_sprites;
	static std::vector<Group> s_groups;
	static std::vector<Debris> s_debris;
	static std::vector<Pearl> s_pearls;
	static std::vector<Tnt> s_tnt;
	static std::vector<Chip> s_chips;
	static std::vector<Arrow> s_arrows;

	// ---- Stage 2 props ----
	static Hash model_if_loaded(const std::string &name)
	{
		Hash h = GET_HASH_KEY(name.c_str());
		if (!IS_MODEL_VALID(h))
			return 0;
		if (!HAS_MODEL_LOADED(h))
		{
			REQUEST_MODEL(h);
			return 0;
		}
		return h;
	}

	static void delete_obj(int &obj)
	{
		if (obj && DOES_ENTITY_EXIST(obj))
		{
			SET_ENTITY_AS_MISSION_ENTITY(obj, TRUE, TRUE);
			DELETE_OBJECT(&obj);
		}
		obj = 0;
	}

	static int spawn_physics(Hash model, const V3 &p, const V3 &vel)
	{
		int obj = CREATE_OBJECT(model, p.x, p.y, p.z, FALSE, TRUE, TRUE);
		if (!obj)
			return 0;
		SET_ENTITY_DYNAMIC(obj, TRUE);
		ACTIVATE_PHYSICS(obj);
		SET_ENTITY_VELOCITY(obj, vel.x, vel.y, vel.z);
		return obj;
	}

	void preload()
	{
		for (auto &it : g_items)
			if (it.block)
				model_if_loaded("gtm_" + it.name + "_p");
		model_if_loaded("gtm_tnt");
		model_if_loaded("gtm_arrow");
	}

	int arrow_count() { return (int)s_arrows.size(); }
	static int s_explosion[16], s_smoke[12], s_generic[8], s_sweep[8], s_flame[1], s_crit[1];
	static int s_pearlTex = -1;
	static int s_tntItem = -1;

	int pearl_count() { return (int)s_pearls.size(); }
	int tnt_count() { return (int)s_tnt.size(); }
	int particle_count() { return (int)(s_sprites.size() + s_debris.size() + s_chips.size()); }

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
		// Stage 2: little physics chips of the block that bounce off the ground
		if (Hash chip = model_if_loaded("gtm_" + i.name + "_p"))
		{
			for (int k = 0; k < 6 && s_chips.size() < 60; k++)
			{
				V3 p = mn + V3(frand(0.2f, 0.8f), frand(0.2f, 0.8f), frand(0.2f, 0.8f));
				V3 v = (p - (mn + V3(0.5f, 0.5f, 0.5f))) * 5.0f + V3(0, 0, frand(1.5f, 3.5f));
				int obj = spawn_physics(chip, p, v);
				if (!obj)
					continue;
				SET_ENTITY_ROTATION(obj, frand(0, 360), frand(0, 360), frand(0, 360), 2, TRUE);
				SET_ENTITY_NO_COLLISION_ENTITY(obj, g.ped, FALSE);
				s_chips.push_back({obj, frand(0.6f, 1.3f)});
			}
			return;
		}
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
		int obj = 0;
		// Stage 2: a real TNT block with physics; Minecraft gives primed TNT a little hop
		if (Hash h = model_if_loaded("gtm_tnt"))
		{
			float a = frand(0, 2 * PI);
			obj = spawn_physics(h, mn + V3(0.5f, 0.5f, 0.5f), V3(std::cos(a) * 0.4f, std::sin(a) * 0.4f, 4.0f));
		}
		s_tnt.push_back({mn, s_tntItem >= 0 ? s_tntItem : 0, fuse, fuse, obj});
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

	// a box (centre, three half-axes) drawn with DRAW_POLY: the white flash over a physics TNT block
	static void draw_box(const V3 &c, const V3 ax[3], uint8_t r, uint8_t g_, uint8_t b, uint8_t a)
	{
		for (int k = 0; k < 3; k++)
			for (int sgn = -1; sgn <= 1; sgn += 2)
			{
				V3 n = ax[k] * (float)sgn, fc = c + n;
				if ((g.camPos - fc).dot(n) <= 0)
					continue;
				const V3 &u = ax[(k + 1) % 3], &v = ax[(k + 2) % 3];
				V3 p0 = fc - u - v, p1 = fc + u - v, p2 = fc + u + v, p3 = fc - u + v;
				DRAW_POLY(p0.x, p0.y, p0.z, p1.x, p1.y, p1.z, p2.x, p2.y, p2.z, r, g_, b, a);
				DRAW_POLY(p0.x, p0.y, p0.z, p2.x, p2.y, p2.z, p3.x, p3.y, p3.z, r, g_, b, a);
			}
	}

	static void update_tnt(float dt)
	{
		for (size_t i = 0; i < s_tnt.size();)
		{
			Tnt &t = s_tnt[i];
			t.fuse -= dt;
			V3 centre = t.mn + V3(0.5f, 0.5f, 0.5f);
			if (t.obj && DOES_ENTITY_EXIST(t.obj))
				centre = GET_ENTITY_COORDS(t.obj, TRUE);
			if (t.fuse <= 0)
			{
				int obj = t.obj;
				s_tnt[i] = s_tnt.back();
				s_tnt.pop_back();
				delete_obj(obj);
				explode(centre, 4.0f);
				continue;
			}
			if (t.obj)
			{
				// Minecraft: white flash every 5 ticks, swells in the last 10 ticks
				int ticks = (int)(t.fuse * 20.0f);
				float grow = t.fuse < 0.5f ? std::pow(1.0f - t.fuse / 0.5f, 4.0f) * 0.3f : 0.0f;
				if ((ticks / 5) % 2 == 0 || grow > 0)
				{
					Vector3 a{}, b{}, up{}, pos{};
					GET_ENTITY_MATRIX(t.obj, &a, &b, &up, &pos);
					float h = 0.51f + grow * 0.5f;
					V3 ax[3] = {V3(a).norm() * h, V3(b).norm() * h, V3(up).norm() * h};
					draw_box(centre, ax, 255, 255, 255, (ticks / 5) % 2 == 0 ? 190 : 60);
				}
				i++;
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

	void shoot_arrow(const V3 &from, const V3 &dir, float speed, int damage, bool crit)
	{
		Arrow a;
		a.p = from + dir * 0.6f;
		a.v = dir * speed;
		a.dir = dir;
		a.damage = damage;
		a.crit = crit;
		if (Hash h = model_if_loaded("gtm_arrow"))
		{
			a.obj = CREATE_OBJECT_NO_OFFSET(h, a.p.x, a.p.y, a.p.z, FALSE, TRUE, FALSE, 0);
			if (a.obj)
			{
				FREEZE_ENTITY_POSITION(a.obj, TRUE);
				SET_ENTITY_COLLISION(a.obj, FALSE, FALSE);
			}
		}
		s_arrows.push_back(a);
	}

	static void place_arrow(Arrow &a)
	{
		if (!a.obj)
			return;
		V3 d = a.dir;
		float heading = std::atan2(-d.x, d.y) * 180.0f / PI;
		float pitch = std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y)) * 180.0f / PI;
		SET_ENTITY_COORDS_NO_OFFSET(a.obj, a.p.x, a.p.y, a.p.z, FALSE, FALSE, FALSE);
		SET_ENTITY_ROTATION(a.obj, pitch, 0, heading, 2, TRUE);
	}

	static void update_arrows(float dt)
	{
		static const Hash WEAPON_PISTOL = 0x1B06D571;
		int stuck = 0;
		for (auto &a : s_arrows)
			stuck += a.stuck;
		for (size_t i = 0; i < s_arrows.size();)
		{
			Arrow &a = s_arrows[i];
			a.age += dt;
			bool kill = false;
			if (a.stuck)
			{
				// Minecraft despawns stuck arrows after a minute; also drop them if their block went away
				if (a.age > 60.0f || stuck > 48 || (a.inBlock && !g_blocks.count(a.stuckCell)))
				{
					kill = true;
					stuck--;
				}
			}
			else
			{
				V3 next = a.p + a.v * dt;
				a.v.z -= 20.0f * dt; // 0.05 blocks/tick^2
				a.v *= std::pow(0.99f, dt * 20.0f);
				V3 seg = next - a.p;
				float len = seg.len();
				if (len > 1e-4f)
				{
					V3 dir = seg * (1.0f / len);
					a.dir = dir;
					GtaHit gh = gta_probe(a.p, next, g.ped, 1 | 2 | 4 | 8 | 16 | 256);
					if (gh.hit && collision::is_ours(gh.entity))
						gh.hit = false; // block props: the voxel ray is exact
					if (gh.hit && (gh.entity == a.obj || gh.entity == g.ped))
						gh.hit = false;
					VoxelHit vh = voxel_raycast(a.p, dir, len);
					int type = gh.hit && gh.entity && DOES_ENTITY_EXIST(gh.entity) ? GET_ENTITY_TYPE(gh.entity) : 0;
					if (vh.hit && (!gh.hit || vh.t <= gh.t))
					{
						a.p = vh.pos - dir * 0.25f; // tip buried in the block
						a.stuck = true;
						a.inBlock = true;
						a.stuckCell = cell_key(vh.cell);
						a.age = 0;
						audio::play_at("random/bowhit", vh.pos, 1.0f, frand(1.0f, 1.3f));
					}
					else if (gh.hit && (type == 1 || type == 2))
					{
						// GTA does the damage, blood and reactions: an invisible bullet along the arrow's path
						V3 from = gh.pos - dir * 0.4f, to = gh.pos + dir * 0.4f;
						SHOOT_SINGLE_BULLET_BETWEEN_COORDS(from.x, from.y, from.z, to.x, to.y, to.z, a.damage, TRUE,
						                                   WEAPON_PISTOL, g.ped, FALSE, TRUE, -1.0f);
						if (type == 1)
							crit(gh.pos);
						audio::play_at("random/bowhit", gh.pos, 1.0f, frand(1.0f, 1.3f));
						kill = true;
					}
					else if (gh.hit)
					{
						a.p = gh.pos - dir * 0.25f;
						a.stuck = true;
						a.age = 0;
						audio::play_at("random/bowhit", gh.pos, 1.0f, frand(1.0f, 1.3f));
					}
					else
						a.p = next;
				}
				if (a.age > 10.0f)
					kill = true;
				if (!kill && a.crit && !a.stuck && s_sprites.size() < 500)
				{
					Sprite sp;
					sp.p = a.p;
					sp.life = 0.4f;
					sp.size = 0.12f;
					sp.frames = s_crit;
					sp.nFrames = 1;
					add(sp);
				}
			}
			if (kill)
			{
				delete_obj(a.obj);
				s_arrows[i] = s_arrows.back();
				s_arrows.pop_back();
				continue;
			}
			if (a.obj)
				place_arrow(a);
			else
			{
				V3 tail = a.p - a.dir * 0.7f;
				DRAW_LINE(a.p.x, a.p.y, a.p.z, tail.x, tail.y, tail.z, 120, 90, 60, 255);
			}
			i++;
		}
	}

	static void update_chips(float dt)
	{
		for (size_t i = 0; i < s_chips.size();)
		{
			s_chips[i].life -= dt;
			if (s_chips[i].life <= 0)
			{
				delete_obj(s_chips[i].obj);
				s_chips[i] = s_chips.back();
				s_chips.pop_back();
				continue;
			}
			i++;
		}
	}

	void update()
	{
		float dt = std::min(g.dt, 0.1f);
		update_arrows(dt);
		update_chips(dt);
		update_tnt(dt);
		update_debris(dt);
		update_pearls(dt);
		update_sprites(dt);
	}
}
