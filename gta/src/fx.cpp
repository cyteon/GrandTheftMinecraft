#include "fx.h"
#include "objects.h"
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
	struct Rocket
	{
		V3 p, v;
		float age = 0, life = 1.2f;
		int obj = 0;
		Hash model = 0;
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
		int stuckIn = 0;        // ped or vehicle it's stuck in (attached; follows them)
		int owner = 0;          // who shot it (a skeleton); 0 = the player
		int inside = 0;         // vehicle it went into through a window (probes ignore it; occupants are checked)
		float insideLeft = 0;   // metres of flight left inside that vehicle
	};

	static std::vector<Sprite> s_sprites;
	static std::vector<Group> s_groups;
	static std::vector<Debris> s_debris;
	static std::vector<Pearl> s_pearls;
	static std::vector<Tnt> s_tnt;
	static std::vector<Chip> s_chips;
	static std::vector<Rocket> s_rockets;
	static std::vector<Arrow> s_arrows;

	// ---- Stage 2 props ----
	static Hash model_if_loaded(const std::string &name)
	{
		if (!collision::dlc()) // block pack missing or its textures not filled yet
			return 0;
		Hash h = joaat(name);
		if (!IS_MODEL_VALID(h))
			return 0;
		if (!HAS_MODEL_LOADED(h))
		{
			REQUEST_MODEL(h);
			return 0;
		}
		return h;
	}

	static void delete_obj(int &obj) { objects::destroy(obj); }

	static int spawn_physics(Hash model, const V3 &p, const V3 &vel)
	{
		int obj = objects::create(objects::FX, model, p, true);
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
	static int s_explosion[16], s_smoke[12], s_generic[8], s_sweep[8], s_flame[1], s_crit[1], s_spark[8];
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
		for (int i = 0; i < 8; i++)
			s_spark[i] = r2d::tex("particles/spark_" + std::to_string(i) + ".png");
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
				VoxelHit vh = voxel_raycast(pl.p, dir, segLen, true);
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

	void flash_box(const V3 &c, const V3 ax[3], int alpha) { draw_box(c, ax, 255, 255, 255, (uint8_t)alpha); }

	void poof(const V3 &p)
	{
		int grp = new_group(p);
		for (int k = 0; k < 20; k++)
		{
			Sprite s;
			s.p = p + V3(frand(-0.5f, 0.5f), frand(-0.5f, 0.5f), frand(-0.9f, 0.6f));
			s.v = V3(frand(-1, 1), frand(-1, 1), frand(0.2f, 1.2f)) * 0.8f;
			s.life = frand(0.5f, 1.0f);
			s.size = frand(0.25f, 0.5f);
			s.frames = s_generic;
			s.nFrames = 8;
			uint32_t v = (uint32_t)frand(200, 255);
			s.rgb = (v << 16) | (v << 8) | v;
			s.drag = 0.92f;
			s.group = grp;
			add(s);
		}
	}

	// Minecraft firework sparks: spark_7 -> spark_0 as they age, in the star's colour
	static void spark(const V3 &p, const V3 &v, uint32_t rgb, float life, float size, float gravity, int group)
	{
		Sprite s;
		s.p = p;
		s.v = v;
		s.life = life;
		s.size = size;
		s.frames = s_spark;
		s.nFrames = 8;
		s.rgb = rgb;
		s.gravity = gravity;
		s.drag = 0.91f;
		s.group = group;
		add(s);
	}

	void firework_trail(const V3 &p)
	{
		int grp = new_group(p);
		for (int k = 0; k < 2; k++)
			spark(p + V3(frand(-0.15f, 0.15f), frand(-0.15f, 0.15f), frand(-0.15f, 0.15f)),
			      V3(frand(-0.5f, 0.5f), frand(-0.5f, 0.5f), frand(-0.5f, 0.2f)), 0xFFFFFF, frand(0.3f, 0.6f), 0.15f, 0,
			      grp);
	}

	void launch_firework(const V3 &p)
	{
		Rocket r;
		r.p = p + V3(0, 0, 0.3f);
		// FireworkRocketEntity: a little random sideways drift, accelerating upward (x1.15 / tick)
		r.v = V3(frand(-0.02f, 0.02f), frand(-0.02f, 0.02f), 0.05f) * 20.0f;
		r.life = (20 + rand() % 6 + rand() % 7) / 20.0f;
		s_rockets.push_back(r);
		audio::play_at("fireworks/launch", p, 1.0f, 1.0f, 48);
	}

	static void burst(const V3 &p)
	{
		static const uint32_t COLOURS[] = {0xB3312C, 0x3B511A, 0x253192, 0x7B2FBE, 0x287697, 0xD88198,
		                                   0x41CD34, 0xDECF2A, 0x6689D3, 0xC354CD, 0xEB8844, 0xF0F0F0};
		uint32_t c1 = COLOURS[rand() % 12], c2 = COLOURS[rand() % 12];
		int grp = new_group(p);
		for (int k = 0; k < 90; k++) // Minecraft's small ball: sparks on a sphere
		{
			float a = frand(0, 2 * PI), z = frand(-1, 1), rr = std::sqrt(1 - z * z);
			V3 d(std::cos(a) * rr, std::sin(a) * rr, z);
			spark(p, d * frand(4.0f, 6.0f), k % 2 ? c1 : c2, frand(0.9f, 1.6f), 0.3f, 1.5f, grp);
		}
		audio::play_at("fireworks/blast", p, 3.0f, 1.0f, 96);
		audio::play_at("fireworks/twinkle", p, 2.0f, 1.0f, 96);
		DRAW_LIGHT_WITH_RANGE(p.x, p.y, p.z, (c1 >> 16) & 255, (c1 >> 8) & 255, c1 & 255, 25.0f, 8.0f);
	}

	static void update_rockets(float dt)
	{
		for (size_t i = 0; i < s_rockets.size();)
		{
			Rocket &r = s_rockets[i];
			r.age += dt;
			r.v.x *= std::pow(1.15f, dt * 20.0f), r.v.y *= std::pow(1.15f, dt * 20.0f);
			r.v.z = r.v.z * std::pow(1.15f, dt * 20.0f) + 0.04f * 20.0f * dt * 20.0f;
			V3 next = r.p + r.v * dt;
			bool blocked = gta_probe_self(r.p, next, 1 | 16).hit;
			if (r.age >= r.life || blocked)
			{
				burst(r.p);
				int o = r.obj;
				delete_obj(o);
				s_rockets[i] = s_rockets.back();
				s_rockets.pop_back();
				continue;
			}
			r.p = next;
			firework_trail(r.p - r.v.norm() * 0.3f);
			// the rocket itself: the item model pointing along its flight
			V3 d = r.v.norm(), side = d.cross(V3(1, 0, 0));
			if (side.len2() < 1e-3f)
				side = d.cross(V3(0, 1, 0));
			side = side.norm();
			V3 up = side.cross(d);
			V3 ix = (d - up).norm(), iy = (d + up).norm(), iz = ix.cross(iy);
			if (collision::dlc())
			{
				Hash h = joaat("gtm_i_firework_rocket");
				if (!r.obj && IS_MODEL_VALID(h))
				{
					if (HAS_MODEL_LOADED(h))
					{
						r.obj = objects::create(objects::FX, h, r.p);
						if (r.obj)
						{
							FREEZE_ENTITY_POSITION(r.obj, TRUE);
							SET_ENTITY_COLLISION(r.obj, FALSE, FALSE);
						}
					}
					else
						REQUEST_MODEL(h);
				}
				if (r.obj)
				{
					float qm[3][3] = {{ix.x, iy.x, iz.x}, {ix.y, iy.y, iz.y}, {ix.z, iy.z, iz.z}};
					float tr = qm[0][0] + qm[1][1] + qm[2][2];
					float qw = std::sqrt(std::max(0.0f, 1 + tr)) / 2;
					float qx = std::copysign(std::sqrt(std::max(0.0f, 1 + qm[0][0] - qm[1][1] - qm[2][2])) / 2, qm[2][1] - qm[1][2]);
					float qy = std::copysign(std::sqrt(std::max(0.0f, 1 - qm[0][0] + qm[1][1] - qm[2][2])) / 2, qm[0][2] - qm[2][0]);
					float qz = std::copysign(std::sqrt(std::max(0.0f, 1 - qm[0][0] - qm[1][1] + qm[2][2])) / 2, qm[1][0] - qm[0][1]);
					SET_ENTITY_COORDS_NO_OFFSET(r.obj, r.p.x, r.p.y, r.p.z, FALSE, FALSE, FALSE);
					SET_ENTITY_QUATERNION(r.obj, qx, qy, qz, qw);
				}
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

	void shoot_arrow(const V3 &from, const V3 &dir, float speed, int damage, bool crit, int owner)
	{
		Arrow a;
		a.owner = owner;
		a.p = from + dir * 0.6f;
		a.v = dir * speed;
		a.dir = dir;
		a.damage = damage;
		a.crit = crit;
		if (Hash h = model_if_loaded("gtm_arrow"))
		{
			a.obj = objects::create(objects::FX, h, a.p);
			if (a.obj)
			{
				FREEZE_ENTITY_POSITION(a.obj, TRUE);
				SET_ENTITY_COLLISION(a.obj, FALSE, FALSE);
			}
		}
		s_arrows.push_back(a);
	}

	// The bone frame measured from the game itself (no Euler conventions): its origin and where its local x, y, z
	// axes point in the world.
	static void bone_frame(int ped, int boneId, V3 &o, V3 &x, V3 &y, V3 &z)
	{
		o = GET_PED_BONE_COORDS(ped, boneId, 0, 0, 0);
		x = (V3(GET_PED_BONE_COORDS(ped, boneId, 1, 0, 0)) - o).norm();
		y = (V3(GET_PED_BONE_COORDS(ped, boneId, 0, 1, 0)) - o).norm();
		z = (V3(GET_PED_BONE_COORDS(ped, boneId, 0, 0, 1)) - o).norm();
	}

	// Attach the arrow at a.p (pointing a.dir) to `ent` so it rides along: peds by their nearest bone, vehicles by
	// their body. The local direction gives the same pitch/heading form SET_ENTITY_ROTATION uses.
	static void stick_arrow(Arrow &a, int ent, bool ped)
	{
		if (!a.obj)
			return;
		// at most a few arrows hang on one person or car (each is an attachment in GTA's pools)
		int on = 0;
		for (auto &o : s_arrows)
			on += o.stuck && o.stuckIn == ent && o.obj;
		if (on >= 6)
		{
			delete_obj(a.obj);
			return;
		}
		int boneIndex = 0;
		V3 o, x, y, z;
		if (ped)
		{
			static const int BONES[] = {31086, 39317, 24818, 24817, 23553, 11816, 40269, 28252, 57005, 45509,
			                            61163, 18905, 51826, 36864, 52301, 58271, 63931, 14201};
			int best = BONES[0];
			float bestD = 1e9f;
			for (int id : BONES)
			{
				float d = (V3(GET_PED_BONE_COORDS(ent, id, 0, 0, 0)) - (a.p + a.dir * 0.3f)).len2();
				if (d < bestD)
					bestD = d, best = id;
			}
			boneIndex = GET_PED_BONE_INDEX(ent, best);
			bone_frame(ent, best, o, x, y, z);
		}
		else
		{
			o = GET_ENTITY_COORDS(ent, FALSE);
			x = (V3(GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(ent, 1, 0, 0)) - o).norm();
			y = (V3(GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(ent, 0, 1, 0)) - o).norm();
			z = (V3(GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(ent, 0, 0, 1)) - o).norm();
		}
		V3 rel = a.p - o;
		V3 off(rel.dot(x), rel.dot(y), rel.dot(z));
		V3 d(a.dir.dot(x), a.dir.dot(y), a.dir.dot(z));
		float heading = std::atan2(-d.x, d.y) * 180.0f / PI;
		float pitch = std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y)) * 180.0f / PI;
		FREEZE_ENTITY_POSITION(a.obj, FALSE);
		ATTACH_ENTITY_TO_ENTITY(a.obj, ent, boneIndex, off.x, off.y, off.z, pitch, 0, heading, FALSE, FALSE, FALSE,
		                        FALSE, 2, TRUE, 0);
		a.stuckIn = ent;
	}

	// ---- arrows vs vehicles ----
	static Hash mh(const char *n) { return GET_HASH_KEY(n); }

	static bool glass_passes(Hash m) // windows an arrow goes through (bulletproof / opaque ones stop it)
	{
		static Hash GL[5] = {};
		if (!GL[0])
		{
			GL[0] = mh("CAR_GLASS_WEAK"), GL[1] = mh("CAR_GLASS_MEDIUM"), GL[2] = mh("CAR_GLASS_STRONG");
			GL[3] = mh("GLASS_SHOOT_THROUGH"), GL[4] = mh("PERSPEX");
		}
		for (Hash h : GL)
			if (m == h)
				return true;
		return false;
	}

	static float seg_dist2(const V3 &p, const V3 &a, const V3 &b, float &t)
	{
		V3 ab = b - a;
		float l2 = ab.len2();
		t = l2 > 1e-6f ? clampf((p - a).dot(ab) / l2, 0, 1) : 0;
		return (a + ab * t - p).len2();
	}

	// the occupant of `veh` whose head or torso the segment a-b passes closest to (within reach), or 0
	static int occupant_on_path(int veh, const V3 &a, const V3 &b, V3 &where)
	{
		int best = 0;
		float bestT = 2;
		int seats = GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS(veh);
		for (int seat = -1; seat < seats; seat++)
		{
			int ped = GET_PED_IN_VEHICLE_SEAT(veh, seat, FALSE);
			if (!ped || ped == g.ped || !DOES_ENTITY_EXIST(ped))
				continue;
			static const int B[] = {31086, 39317, 24818, 24817, 11816, 40269, 45509};
			static const float R[] = {0.22f, 0.20f, 0.32f, 0.32f, 0.30f, 0.16f, 0.16f};
			for (int i = 0; i < 7; i++)
			{
				V3 bp = GET_PED_BONE_COORDS(ped, B[i], 0, 0, 0);
				float t;
				if (seg_dist2(bp, a, b, t) < R[i] * R[i] && t < bestT)
					bestT = t, best = ped, where = a + (b - a) * t;
			}
		}
		return best;
	}

	static int nearest_vehicle_bone(int veh, const V3 &p, const char *const *names, int n, float maxD)
	{
		int best = -1;
		float bestD = maxD * maxD;
		for (int i = 0; i < n; i++)
		{
			int bi = GET_ENTITY_BONE_INDEX_BY_NAME(veh, names[i]);
			if (bi < 0)
				continue;
			float d = (V3(GET_WORLD_POSITION_OF_ENTITY_BONE(veh, bi)) - p).len2();
			if (d < bestD)
				bestD = d, best = i;
		}
		return best;
	}

	static const char *WINDOWS[] = {"window_lf", "window_rf", "window_lr", "window_rr",
	                                "window_lm", "window_rm", "windscreen", "windscreen_r"};

	// Car windows, decided in the car's own coordinates. GTA's car collision mostly has no glass: a ray through a
	// window flies on through the cabin and hits the far door from inside. So instead of trusting the hit point, find
	// where the arrow enters (and leaves) the car's box: above the window line (the lowest window bone, less a margin)
	// on a side, front or back face it's glass, belonging to the horizontally nearest window bone; lower down, or on
	// the roof / underside, it's bodywork.
	struct CarSpan
	{
		float tin, tout;    // metres along the arrow's path
		int inAxis, outAxis; // which face of the box: 0 sides, 1 front/back, 2 roof/underside
		V3 inL, outL;       // entry / exit points in the car's coordinates
	};

	static bool car_span(int veh, const V3 &from, const V3 &dir, float len, CarSpan &cs)
	{
		Vector3 mn{}, mx{};
		GET_MODEL_DIMENSIONS(GET_ENTITY_MODEL(veh), &mn, &mx);
		V3 to = from + dir * len;
		V3 la = GET_OFFSET_FROM_ENTITY_GIVEN_WORLD_COORDS(veh, from.x, from.y, from.z);
		V3 lb = GET_OFFSET_FROM_ENTITY_GIVEN_WORLD_COORDS(veh, to.x, to.y, to.z);
		V3 ld = (lb - la) * (1.0f / len);
		float lo[3] = {mn.x, mn.y, mn.z}, hi[3] = {mx.x, mx.y, mx.z}, o[3] = {la.x, la.y, la.z}, d[3] = {ld.x, ld.y, ld.z};
		cs.tin = -1e9f, cs.tout = 1e9f, cs.inAxis = cs.outAxis = 0;
		for (int k = 0; k < 3; k++)
		{
			if (std::fabs(d[k]) < 1e-6f)
			{
				if (o[k] < lo[k] || o[k] > hi[k])
					return false;
				continue;
			}
			float t0 = (lo[k] - o[k]) / d[k], t1 = (hi[k] - o[k]) / d[k];
			if (t0 > t1)
				std::swap(t0, t1);
			if (t0 > cs.tin)
				cs.tin = t0, cs.inAxis = k;
			if (t1 < cs.tout)
				cs.tout = t1, cs.outAxis = k;
		}
		if (cs.tin > cs.tout || cs.tout < 0)
			return false;
		cs.inL = la + ld * cs.tin, cs.outL = la + ld * cs.tout;
		return true;
	}

	// the window at a point on the car's box (car coordinates) crossed through face `axis`, or -1 for bodywork
	static int window_at_local(int veh, const V3 &lp, int axis, float *beltOut = nullptr)
	{
		float belt = 1e9f;
		int best = -1;
		float bestD = 1e9f;
		for (int i = 0; i < 8; i++)
		{
			int bi = GET_ENTITY_BONE_INDEX_BY_NAME(veh, WINDOWS[i]);
			if (bi < 0)
				continue;
			V3 w = GET_WORLD_POSITION_OF_ENTITY_BONE(veh, bi);
			V3 wl = GET_OFFSET_FROM_ENTITY_GIVEN_WORLD_COORDS(veh, w.x, w.y, w.z);
			belt = std::min(belt, wl.z);
			float dd = (wl.x - lp.x) * (wl.x - lp.x) + (wl.y - lp.y) * (wl.y - lp.y);
			if (dd < bestD)
				bestD = dd, best = i;
		}
		if (beltOut)
			*beltOut = belt;
		if (axis == 2 || best < 0 || lp.z < belt - 0.15f)
			return -1; // roof / underside, no windows, or below the window line
		return best;
	}

	static V3 car_to_world(int veh, const V3 &l) { return GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(veh, l.x, l.y, l.z); }

	void blood(const V3 &p, const V3 &dir)
	{
		for (int k = 0; k < 10 && s_debris.size() < 800; k++)
		{
			Debris d;
			d.p = p;
			d.v = dir * frand(-1.5f, 0.5f) + V3(frand(-1, 1), frand(-1, 1), frand(0.5f, 2.0f));
			d.life = frand(0.3f, 0.7f);
			d.size = frand(0.03f, 0.06f);
			d.floorZ = p.z - 2.0f;
			d.c = {(uint8_t)frand(110, 160), 8, 8, 255};
			s_debris.push_back(d);
		}
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
				if (a.age > 60.0f || stuck > 48 || (a.inBlock && !g_blocks.count(a.stuckCell)) ||
				    (a.stuckIn && !DOES_ENTITY_EXIST(a.stuckIn)))
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
					if (a.inside && !DOES_ENTITY_EXIST(a.inside))
						a.inside = 0;
					// through the shooter, their own car, and a car the arrow has already flown into
					GtaHit gh = gta_probe_self(a.p, next, 1 | 2 | 4 | 8 | 16 | 256, a.inside ? a.inside : a.owner);
					if (gh.hit && a.owner && gh.entity == a.owner)
						gh.hit = false;
					if (gh.hit && collision::is_ours(gh.entity))
						gh.hit = false; // block props: the voxel ray is exact
					if (gh.hit && gh.entity == a.obj)
						gh.hit = false;
					VoxelHit vh = voxel_raycast(a.p, dir, len, true);
					int type = gh.hit && gh.entity && DOES_ENTITY_EXIST(gh.entity) ? GET_ENTITY_TYPE(gh.entity) : 0;
					// cars: where the arrow meets the car's box decides glass or bodywork (see car_span)
					int victim = 0;
					V3 victimAt;
					bool smashed = false;
					if (a.inside && DOES_ENTITY_EXIST(a.inside))
					{
						// flying through the cabin: people on the path, then out through the far side
						CarSpan cs;
						float reach = len;
						if (car_span(a.inside, a.p, dir, len, cs) && cs.tout < len)
							reach = std::max(0.0f, cs.tout);
						victim = occupant_on_path(a.inside, a.p, a.p + dir * reach, victimAt);
						if (!victim && reach < len && !(vh.hit && vh.t < reach))
						{
							int veh = a.inside;
							V3 out = car_to_world(veh, cs.outL);
							int w = window_at_local(veh, cs.outL, cs.outAxis);
							if (w >= 0) // out through a window: smash it and fly on
							{
								SMASH_VEHICLE_WINDOW(veh, w);
								audio::play_at("random/glass", out, 0.6f, frand(1.2f, 1.5f));
								a.inside = 0;
							}
							else // into the far door / roof from inside: stuck there
							{
								gh.hit = true, gh.entity = veh, gh.pos = out, gh.t = reach, type = 2;
							}
						}
					}
					int window = -1;
					V3 entry;
					if (gh.hit && type == 2 && gh.entity != a.inside)
					{
						// An arrow covers ~1 m a frame and the window openings have no collision, so the last frame may
						// already have carried it into the box (even the cabin): trace the box from 4 m back along the path.
						CarSpan cs;
						const float BACK = 4.0f;
						V3 from = a.p - dir * BACK;
						bool spanOk = car_span(gh.entity, from, dir, BACK + gh.t + 4.0f, cs) && cs.tin >= 0;
						float belt = 0;
						if (spanOk)
						{
							window = window_at_local(gh.entity, cs.inL, cs.inAxis, &belt);
							if (window >= 0)
							{
								entry = car_to_world(gh.entity, cs.inL);
								// whoever sits between the window and where this frame ends (or the far side)
								float upTo = std::min(BACK + len, cs.tout);
								victim = occupant_on_path(gh.entity, entry, from + dir * std::max(upTo, cs.tin), victimAt);
								SMASH_VEHICLE_WINDOW(gh.entity, window);
								audio::play_at("random/glass", entry, 0.6f, frand(1.2f, 1.5f));
								smashed = true;
								a.inside = gh.entity;
								a.insideLeft = cs.tout - cs.tin + 0.5f;
							}
						}
						static int logged = 0;
						if (logged++ < 40)
							logf("arrow hit vehicle: span %d tin %.2f axis %d entry (%.2f %.2f %.2f) belt %.2f -> window %d, victim %d",
							     spanOk ? 1 : 0, cs.tin - BACK, cs.inAxis, cs.inL.x, cs.inL.y, cs.inL.z, belt, window, victim);
						if (!victim && window < 0) // riders, open cars
							victim = occupant_on_path(gh.entity, a.p, gh.pos + dir * 0.3f, victimAt);
					}
					if (victim && (!vh.hit || (victimAt - a.p).len() < vh.t))
					{
						gh.hit = true;
						gh.entity = victim;
						gh.pos = victimAt;
						gh.t = (victimAt - a.p).len();
						type = 1;
					}
					if (vh.hit && (!gh.hit || vh.t <= gh.t))
					{
						a.p = vh.pos - dir * 0.25f; // tip buried in the block
						a.stuck = true;
						a.inBlock = true;
						a.stuckCell = cell_key(vh.cell);
						a.age = 0;
						audio::play_at("random/bowhit", vh.pos, 1.0f, frand(1.0f, 1.3f));
					}
					// a car window it just smashed: on into the cabin (next frame checks who's on the path)
					else if (smashed && type != 1)
						a.p = (entry - a.p).dot(dir) > 0 ? entry + dir * 0.05f : a.p; // on into the cabin from the window
					// shop windows and other shoot-through glass just let it through
					else if (gh.hit && type != 2 && glass_passes(gh.material))
					{
						audio::play_at("random/glass", gh.pos, 0.6f, frand(1.2f, 1.5f));
						a.p = gh.pos + dir * 0.05f;
					}
					else if (gh.hit && type == 1)
					{
						// a person: damage, a wound where it hit, a knock, and the arrow stays in them
						int ped = gh.entity;
						a.p = gh.pos - dir * 0.2f; // tip in the body
						a.stuck = true;
						a.age = 0;
						stick_arrow(a, ped, true);
						int hp = GET_ENTITY_HEALTH(ped);
						APPLY_DAMAGE_TO_PED(ped, a.damage, FALSE, 0, 0xA2719263 /* unarmed */);
						int bone = 0;
						if (a.obj && IS_ENTITY_ATTACHED(a.obj))
						{
							// the same bone the arrow is attached to gets the wound decal
							float best = 1e9f;
							for (int id : {31086, 24818, 11816, 40269, 45509, 51826, 58271, 36864, 63931})
							{
								float d = (V3(GET_PED_BONE_COORDS(ped, id, 0, 0, 0)) - gh.pos).len2();
								if (d < best)
									best = d, bone = GET_PED_BONE_INDEX(ped, id);
							}
						}
						APPLY_PED_BLOOD(ped, bone, 0, 0, 0, "BulletSmall");
						blood(gh.pos, dir);
						if (hp - a.damage > 100) // still standing: Minecraft's knock-back
						{
							SET_PED_TO_RAGDOLL(ped, 400, 400, 0, FALSE, FALSE, FALSE);
							V3 kb = V3(dir.x, dir.y, 0).norm() * 4.0f + V3(0, 0, 1.0f);
							APPLY_FORCE_TO_ENTITY(ped, 1, kb.x, kb.y, kb.z, 0, 0, 0, 0, FALSE, TRUE, TRUE, FALSE, TRUE);
						}
						if (a.crit)
							crit(gh.pos);
						audio::play_at("random/bowhit", gh.pos, 1.0f, frand(1.0f, 1.3f));
					}
					else if (gh.hit && type == 2)
					{
						// a vehicle: no bullet hole, the arrow sticks in the bodywork (or a tyre, which bursts) and rides along
						int veh = gh.entity;
						static const char *WHEEL[] = {"wheel_lf", "wheel_rf", "wheel_lm1", "wheel_rm1", "wheel_lr", "wheel_rr"};
						int wh = nearest_vehicle_bone(veh, gh.pos, WHEEL, 6, 0.45f);
						if (wh >= 0)
							SET_VEHICLE_TYRE_BURST(veh, wh, FALSE, 1000.0f);
						a.p = gh.pos - dir * 0.25f;
						a.stuck = true;
						a.age = 0;
						stick_arrow(a, veh, false);
						float body = GET_VEHICLE_BODY_HEALTH(veh);
						SET_VEHICLE_BODY_HEALTH(veh, std::max(0.0f, body - a.damage * 0.5f));
						audio::play_at("random/bowhit", gh.pos, 1.0f, frand(1.0f, 1.3f));
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
					if (a.inside && !a.stuck && (a.insideLeft -= len) <= 0)
						a.inside = 0; // out the other side
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
			if (a.obj && !a.stuckIn)
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
		update_rockets(dt);
		update_chips(dt);
		update_tnt(dt);
		update_debris(dt);
		update_pearls(dt);
		update_sprites(dt);
	}
}
