#include "wither.h"
#include "audio.h"
#include "collision.h"
#include "fx.h"
#include "log.h"
#include "mobs.h"
#include "render2d.h"
#include "rig.h"
#include "world.h"
#include <algorithm>
#include <vector>

namespace wither
{
	static const float PXM = 2.0f / 16.0f; // the Wither is drawn at 2x: one model pixel in metres
	static const int MAX_HP = 300;          // Minecraft's Wither health
	static const float CHARGE = 3.5f;       // seconds of spawn charging

	struct Skull
	{
		V3 p, v;
		float age = 0;
		int obj = 0;
		Hash model = 0;
	};
	struct Wither
	{
		int ped = 0;
		V3 pos, vel;            // "feet" of the model; velocity in blocks per tick
		float yaw = 0;          // body heading (radians, GTA convention)
		float age = 0, acc = 0; // seconds alive; tick accumulator
		uint32_t deadSince = 0;
		int target[3] = {};     // per head (0 = centre)
		uint32_t nextShot[3] = {}, nextRetarget = 0, nextIdle = 0;
		int lastHealth = 0;
		uint32_t shieldUntil = 0; // its own explosions can't hurt it (GTA applies their damage a frame or two later)
		bool shielded = true;
		int objs[4] = {};       // body, centre head, right head, left head
		Hash models[4] = {};
		V3 headDir[3];
	};
	static std::vector<Wither> s_withers;
	static std::vector<Skull> s_skulls;

	int count() { return (int)s_withers.size(); }
	bool is_wither(int ped)
	{
		for (auto &w : s_withers)
			if (w.ped == ped)
				return true;
		return false;
	}

	static bool alive(int p) { return p && DOES_ENTITY_EXIST(p) && !IS_PED_DEAD_OR_DYING(p, TRUE); }
	static int hp(const Wither &w) { return std::max(0, (GET_ENTITY_HEALTH(w.ped) - 100) / 10); }

	bool spawn(const V3 &at)
	{
		if (s_withers.size() >= 3 || !collision::dlc() || !IS_MODEL_VALID(GET_HASH_KEY("gtm_wither_body")))
			return false;
		Hash model = GET_HASH_KEY("a_m_y_skater_01");
		REQUEST_MODEL(model);
		for (int i = 0; i < 100 && !HAS_MODEL_LOADED(model); i++)
			WAIT(0);
		Wither w;
		w.pos = at + V3(0, 0, 0.2f);
		V3 c = w.pos + V3(0, 0, 2.2f); // the ped (its hit box) sits in the chest
		w.ped = CREATE_PED(26, model, c.x, c.y, c.z, 0, FALSE, TRUE);
		if (!w.ped)
			return false;
		SET_ENTITY_AS_MISSION_ENTITY(w.ped, TRUE, TRUE);
		SET_ENTITY_VISIBLE(w.ped, FALSE, FALSE);
		FREEZE_ENTITY_POSITION(w.ped, TRUE);
		SET_PED_RELATIONSHIP_GROUP_HASH(w.ped, GET_HASH_KEY("GTM_HOSTILE"));
		SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(w.ped, TRUE);
		REMOVE_ALL_PED_WEAPONS(w.ped, TRUE);
		DISABLE_PED_PAIN_AUDIO(w.ped, TRUE);
		STOP_PED_SPEAKING(w.ped, TRUE);
		SET_PED_CAN_RAGDOLL(w.ped, FALSE);
		SET_PED_SUFFERS_CRITICAL_HITS(w.ped, FALSE);
		SET_PED_MAX_HEALTH(w.ped, 100 + MAX_HP * 10);
		SET_ENTITY_HEALTH(w.ped, 100 + MAX_HP * 10, 0, 0);
		SET_ENTITY_INVINCIBLE(w.ped, TRUE, FALSE); // while charging
		w.lastHealth = GET_ENTITY_HEALTH(w.ped);
		w.yaw = deg2rad(GET_ENTITY_HEADING(g.ped)) + PI; // facing the player
		for (auto &d : w.headDir)
			d = V3(-std::sin(w.yaw), std::cos(w.yaw), 0);
		s_withers.push_back(w);
		audio::play_at("mob/wither/spawn", at, 4.0f, 1.0f, 200);
		logf("wither: spawned");
		return true;
	}

	// whom a head shoots at: people (not the player: creative), golems, other mobs; cars count via their drivers
	static int pick(const V3 &from, int notThis, bool nearest)
	{
		static int peds[1024];
		int n = shv::worldGetAllPeds(peds, 1024);
		std::vector<std::pair<float, int>> c;
		for (int i = 0; i < n; i++)
		{
			int p = peds[i];
			if (p == g.ped || IS_PED_A_PLAYER(p) || !alive(p) || is_wither(p) || p == notThis || mobs::is_undead(p))
				continue;
			float d = (V3(GET_ENTITY_COORDS(p, TRUE)) - from).len();
			if (d < 48)
				c.push_back({d, p});
		}
		if (c.empty())
			return 0;
		std::sort(c.begin(), c.end());
		if (nearest)
			return c[0].second;
		return c[rand() % std::min<size_t>(c.size(), 6)].second;
	}

	static V3 head_pos(const Wither &w, int i)
	{
		// model pivots (Minecraft): centre (0,0,0), right (-8,4,0), left (10,4,0); the model origin is 24 px up
		V3 F(-std::sin(w.yaw), std::cos(w.yaw), 0), R(std::cos(w.yaw), std::sin(w.yaw), 0);
		V3 o = w.pos + V3(0, 0, 24 * PXM);
		if (i == 1)
			return o + R * (8 * PXM) - V3(0, 0, 4 * PXM);
		if (i == 2)
			return o - R * (10 * PXM) - V3(0, 0, 4 * PXM);
		return o;
	}

	static void shoot(Wither &w, int i, int target)
	{
		V3 from = head_pos(w, i);
		V3 at = V3(GET_ENTITY_COORDS(target, TRUE));
		Skull s;
		V3 dir = (at - from).norm();
		dir = (dir + V3(frand(-1, 1), frand(-1, 1), frand(-1, 1)) * 0.03f).norm();
		s.p = from + dir * 0.8f;
		s.v = dir * 18.0f;
		s_skulls.push_back(s);
		audio::play_at("mob/wither/shoot", from, 1.5f, frand(0.8f, 1.2f), 64);
	}

	static void quat(const V3 &X, const V3 &Y, const V3 &Z, float &x, float &y, float &z, float &w)
	{
		float m[3][3] = {{X.x, Y.x, Z.x}, {X.y, Y.y, Z.y}, {X.z, Y.z, Z.z}};
		float tr = m[0][0] + m[1][1] + m[2][2];
		if (tr > 0)
		{
			float s = std::sqrt(tr + 1.0f) * 2;
			w = 0.25f * s, x = (m[2][1] - m[1][2]) / s, y = (m[0][2] - m[2][0]) / s, z = (m[1][0] - m[0][1]) / s;
		}
		else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
		{
			float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2;
			w = (m[2][1] - m[1][2]) / s, x = 0.25f * s, y = (m[0][1] + m[1][0]) / s, z = (m[0][2] + m[2][0]) / s;
		}
		else if (m[1][1] > m[2][2])
		{
			float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2;
			w = (m[0][2] - m[2][0]) / s, x = (m[0][1] + m[1][0]) / s, y = 0.25f * s, z = (m[1][2] + m[2][1]) / s;
		}
		else
		{
			float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2;
			w = (m[1][0] - m[0][1]) / s, x = (m[0][2] + m[2][0]) / s, y = (m[1][2] + m[2][1]) / s, z = 0.25f * s;
		}
	}

	static void draw(Wither &w, float shake)
	{
		V3 F(-std::sin(w.yaw), std::cos(w.yaw), 0), U(0, 0, 1);
		V3 R = F.cross(U);
		V3 jitter = shake > 0 ? V3(frand(-1, 1), frand(-1, 1), frand(-1, 1)) * shake : V3();
		V3 o = w.pos + V3(0, 0, 24 * PXM) + jitter;
		rig::place(w.objs[0], w.models[0], "gtm_wither_body", o, R, F, U);
		for (int i = 0; i < 3; i++)
		{
			V3 hf = w.headDir[i].len2() > 0.5f ? w.headDir[i].norm() : F;
			V3 hr = hf.cross(V3(0, 0, 1));
			hr = hr.len2() > 1e-4f ? hr.norm() : R;
			V3 hu = hr.cross(hf);
			rig::place(w.objs[1 + i], w.models[1 + i], i == 0 ? "gtm_wither_head" : "gtm_wither_shead", head_pos(w, i) + jitter,
			           hr, hf, hu);
		}
	}

	static void remove(size_t i)
	{
		Wither &w = s_withers[i];
		for (int k = 0; k < 4; k++)
			rig::drop(w.objs[k]);
		if (w.ped && DOES_ENTITY_EXIST(w.ped))
		{
			SET_ENTITY_AS_MISSION_ENTITY(w.ped, TRUE, TRUE);
			DELETE_PED(&w.ped);
		}
		s_withers.erase(s_withers.begin() + i);
	}

	void clear()
	{
		while (!s_withers.empty())
			remove(s_withers.size() - 1);
		for (auto &s : s_skulls)
			rig::drop(s.obj);
		s_skulls.clear();
	}

	// one Minecraft tick of the Wither's flying (WitherBoss.aiStep)
	static void tick(Wither &w)
	{
		bool powered = hp(w) <= MAX_HP / 2; // second phase: stays at its target's height
		V3 v(w.vel.x, w.vel.y, w.vel.z * 0.6f);
		int t = w.target[0];
		if (alive(t))
		{
			V3 tp = GET_ENTITY_COORDS(t, TRUE);
			float vz = v.z;
			if (w.pos.z < tp.z || (!powered && w.pos.z < tp.z + 5.0f))
			{
				vz = std::max(0.0f, vz);
				vz += 0.3f - vz * 0.6f;
			}
			v.z = vz;
			V3 d(tp.x - w.pos.x, tp.y - w.pos.y, 0);
			if (d.len2() > 9.0f)
			{
				V3 n = d.norm();
				v.x += n.x * 0.3f - v.x * 0.6f;
				v.y += n.y * 0.3f - v.y * 0.6f;
			}
		}
		else
			v.z += (std::sin(w.age) * 0.02f - v.z) * 0.1f; // idle bobbing
		if (v.x * v.x + v.y * v.y > 0.05f)
			w.yaw = std::atan2(-v.x, v.y);
		w.vel = v;
		w.pos += w.vel;
		w.vel.x *= 0.91f, w.vel.y *= 0.91f, w.vel.z *= 0.98f; // air drag (no gravity)
		// don't sink into the ground or our blocks
		float gz = 0;
		V3 probeFrom = w.pos + V3(0, 0, 3.0f);
		GtaHit h = gta_probe(probeFrom, w.pos - V3(0, 0, 0.5f), w.ped, 1 | 16);
		if (h.hit && !collision::is_ours(h.entity))
			gz = h.pos.z;
		if (h.hit && w.pos.z < gz + 0.5f)
			w.pos.z = gz + 0.5f, w.vel.z = std::max(w.vel.z, 0.0f);
	}

	void update()
	{
		float dt = std::min(g.dt, 0.1f);
		for (size_t i = 0; i < s_withers.size();)
		{
			Wither &w = s_withers[i];
			if (!DOES_ENTITY_EXIST(w.ped) || (w.pos - g.pedPos).len() > 400.0f)
			{
				remove(i);
				continue;
			}
			w.age += dt;
			// spawning: charge (flashing, invulnerable), then blow up
			if (w.age < CHARGE)
			{
				w.yaw += dt * 2.5f;
				draw(w, 0.02f);
				if (((int)(w.age * 20) / 4) % 2 == 0)
				{
					V3 c = w.pos + V3(0, 0, 24 * PXM);
					V3 ax[3] = {V3(1.4f, 0, 0), V3(0, 0.5f, 0), V3(0, 0, 1.6f)};
					fx::flash_box(c - V3(0, 0, 0.8f), ax, 120);
				}
				SET_ENTITY_COORDS_NO_OFFSET(w.ped, w.pos.x, w.pos.y, w.pos.z + 2.2f, FALSE, FALSE, FALSE);
				i++;
				continue;
			}
			if (w.age - dt < CHARGE) // the spawn explosion (Minecraft: power 7)
			{
				fx::explode(w.pos + V3(0, 0, 1.5f), 7.0f);
				w.shieldUntil = g.now + 1000;
			}
			bool shield = g.now < w.shieldUntil;
			if (shield != w.shielded)
			{
				SET_ENTITY_INVINCIBLE(w.ped, shield, FALSE);
				w.shielded = shield;
				w.lastHealth = GET_ENTITY_HEALTH(w.ped);
			}
			bool dead = IS_PED_DEAD_OR_DYING(w.ped, TRUE) || hp(w) <= 0;
			if (dead && !w.deadSince)
			{
				w.deadSince = g.now;
				audio::play_at("mob/wither/death", w.pos, 4.0f, 1.0f, 300);
			}
			if (w.deadSince)
			{
				// Minecraft's death: shudder and puff smoke, then go out with a bang
				draw(w, 0.08f);
				if (rand() % 3 == 0)
					fx::poof(w.pos + V3(frand(-1, 1), frand(-1, 1), frand(1, 3)));
				if (g.now - w.deadSince > 2500)
				{
					fx::explode(w.pos + V3(0, 0, 1.5f), 4.0f);
					remove(i);
					continue;
				}
				i++;
				continue;
			}
			int health = GET_ENTITY_HEALTH(w.ped);
			if (health < w.lastHealth - 5)
				audio::play_at("mob/wither/hurt", w.pos, 2.0f, frand(0.8f, 1.1f), 64);
			w.lastHealth = health;
			if (g.now >= w.nextIdle)
			{
				audio::play_at("mob/wither/idle", w.pos, 2.0f, frand(0.8f, 1.1f), 96);
				w.nextIdle = g.now + (uint32_t)frand(6000, 12000);
			}
			// targets: the centre head picks the nearest, the side heads random ones nearby
			if (g.now >= w.nextRetarget)
			{
				w.nextRetarget = g.now + 1500;
				if (!alive(w.target[0]))
					w.target[0] = pick(w.pos, 0, true);
				for (int k = 1; k < 3; k++)
					if (!alive(w.target[k]) || rand() % 4 == 0)
						w.target[k] = pick(w.pos, 0, false);
			}
			w.acc += dt;
			while (w.acc >= 0.05f)
			{
				w.acc -= 0.05f;
				tick(w);
			}
			// heads turn towards their targets and shoot
			bool powered = hp(w) <= MAX_HP / 2;
			for (int k = 0; k < 3; k++)
			{
				int t = w.target[k];
				if (!alive(t))
				{
					w.headDir[k] = V3(-std::sin(w.yaw), std::cos(w.yaw), 0);
					continue;
				}
				V3 want = (V3(GET_ENTITY_COORDS(t, TRUE)) - head_pos(w, k)).norm();
				w.headDir[k] = (w.headDir[k] + (want - w.headDir[k]) * std::min(1.0f, dt * 6.0f)).norm();
				if (g.now >= w.nextShot[k] && want.dot(w.headDir[k]) > 0.9f)
				{
					shoot(w, k, t);
					float base = k == 0 ? 2000.0f : 3000.0f;
					w.nextShot[k] = g.now + (uint32_t)(frand(base * 0.7f, base * 1.3f) * (powered ? 0.6f : 1.0f));
				}
			}
			SET_ENTITY_COORDS_NO_OFFSET(w.ped, w.pos.x, w.pos.y, w.pos.z + 2.2f, FALSE, FALSE, FALSE);
			draw(w, 0);
			i++;
		}

		// wither skulls: straight, explode on whatever they touch (power 1: they break a block or two)
		for (size_t i = 0; i < s_skulls.size();)
		{
			Skull &s = s_skulls[i];
			s.age += dt;
			V3 next = s.p + s.v * dt;
			int self = 0;
			for (auto &w : s_withers)
				if ((w.pos - s.p).len() < 4)
					self = w.ped;
			GtaHit h = gta_probe(s.p, next, self ? self : g.ped, 1 | 2 | 4 | 8 | 16);
			if (h.hit && h.entity == g.ped)
				h.hit = false;
			VoxelHit vh = voxel_raycast(s.p, (next - s.p).norm(), (next - s.p).len(), true);
			if (h.hit || vh.hit || s.age > 6.0f)
			{
				V3 at = vh.hit && (!h.hit || vh.t <= h.t) ? vh.pos : h.hit ? h.pos : s.p;
				fx::explode(at, 1.0f);
				for (auto &w : s_withers) // a skull going off next to its owner
					if (!w.deadSince && (w.pos + V3(0, 0, 2.2f) - at).len() < 8)
						w.shieldUntil = g.now + 600;
				rig::drop(s.obj);
				s_skulls[i] = s_skulls.back();
				s_skulls.pop_back();
				continue;
			}
			s.p = next;
			float spin = s.age * 6.0f;
			V3 f = s.v.norm(), r = f.cross(V3(0, 0, 1));
			r = r.len2() > 1e-4f ? r.norm() : V3(1, 0, 0);
			V3 u = r.cross(f);
			V3 rr = r * std::cos(spin) + u * std::sin(spin), uu = u * std::cos(spin) - r * std::sin(spin);
			rig::place(s.obj, s.model, "gtm_wither_skull", s.p, rr, f, uu);
			i++;
		}
	}

	void draw_boss_bar()
	{
		// the nearest Wither within 100 m (Minecraft: bars stack; one is enough here)
		const Wither *w = nullptr;
		float best = 100.0f;
		for (auto &x : s_withers)
		{
			float d = (x.pos - g.pedPos).len();
			if (d < best)
				best = d, w = &x;
		}
		if (!w)
			return;
		float frac = w->age < CHARGE ? clampf(w->age / CHARGE, 0, 1) : hp(*w) / (float)MAX_HP;
		float s = (float)g.gui, W = (float)g.screenW;
		float x = std::floor(W / 2 - 91 * s), y = 12 * s;
		r2d::draw(r2d::tex("gui/boss_bar_bg.png"), x, y, 182 * s, 5 * s, 0xFFFFFFFF, r2d::L_HUD);
		if (frac > 0)
			r2d::draw(r2d::tex("gui/boss_bar_fg.png"), x, y, std::max(1.0f, 182 * s * frac), 5 * s, 0xFFFFFFFF, r2d::L_HUD + 1);
		std::string name = "Wither";
		float tw = r2d::text_width(name);
		r2d::text(std::floor(W / 2 - tw / 2), y - 9 * s, name, 0xFFFFFF, true, r2d::L_HUD + 2);
	}
}
