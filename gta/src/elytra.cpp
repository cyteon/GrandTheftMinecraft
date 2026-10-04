#include "elytra.h"
#include "audio.h"
#include "collision.h"
#include "config.h"
#include "flight.h"
#include "fx.h"
#include "gui.h"
#include "log.h"
#include "world.h"

namespace elytra
{
	static bool s_worn = false, s_on = false;
	static V3 s_v;             // blocks (metres) per tick
	static V3 s_prev, s_cur;   // positions at the last two ticks (rendered in between)
	static float s_acc = 0;    // time since the last tick
	static int s_boostTicks = 0;

	bool worn() { return s_worn; }
	bool gliding() { return s_on; }
	V3 velocity() { return s_v * (20.0f * g_cfg.elytraSpeed); }

	void toggle_worn()
	{
		s_worn = !s_worn;
		if (!s_worn)
			stop();
		gui::toast(s_worn ? "Elytra equipped" : "Elytra removed");
		audio::play("random/pop", nullptr, 0.5f, s_worn ? 1.2f : 0.9f);
	}

	void stop()
	{
		if (!s_on)
			return;
		s_on = false;
		s_boostTicks = 0;
		FREEZE_ENTITY_POSITION(g.ped, FALSE);
		V3 v = s_v * 20.0f;
		SET_ENTITY_VELOCITY(g.ped, v.x, v.y, std::max(v.z, -5.0f));
	}

	void boost()
	{
		if (!s_on)
			return;
		// FireworkRocketEntity: lifetime 10 * (flight duration 1 + 1) + rand(6) + rand(7) ticks
		s_boostTicks = 20 + rand() % 6 + rand() % 7;
		audio::play_at("fireworks/launch", g.pedPos, 1.0f, 1.0f, 32);
	}

	static void start()
	{
		s_on = true;
		V3 v = GET_ENTITY_VELOCITY(g.ped);
		s_v = v * (1.0f / 20.0f);
		s_prev = s_cur = g.pedPos;
		s_acc = 0;
		CLEAR_PED_TASKS_IMMEDIATELY(g.ped);
		FREEZE_ENTITY_POSITION(g.ped, TRUE);
		audio::play_at("item/elytra/elytra_loop", g.pedPos, 0.5f, 1.0f, 16);
	}

	// one Minecraft tick of fall flying (LivingEntity.travel), look = where the camera points
	static void tick()
	{
		V3 look = g.camDir;
		float pitch = -std::asin(clampf(look.z, -1, 1)); // Minecraft pitch: positive = looking down
		float horiz = std::sqrt(look.x * look.x + look.y * look.y);
		float speedH = std::sqrt(s_v.x * s_v.x + s_v.y * s_v.y);
		float f3 = std::cos(pitch);
		f3 = f3 * f3; // * min(1, |look| / 0.4) with |look| = 1
		const float gravity = 0.08f;
		s_v.z += gravity * (-1.0f + f3 * 0.75f);
		if (s_v.z < 0 && horiz > 0)
		{
			float lift = s_v.z * -0.1f * f3;
			s_v.x += look.x * lift / horiz;
			s_v.y += look.y * lift / horiz;
			s_v.z += lift;
		}
		if (pitch < 0 && horiz > 0)
		{
			float climb = speedH * -std::sin(pitch) * 0.04f;
			s_v.x -= look.x * climb / horiz;
			s_v.y -= look.y * climb / horiz;
			s_v.z += climb * 3.2f;
		}
		if (horiz > 0)
		{
			s_v.x += (look.x / horiz * speedH - s_v.x) * 0.1f;
			s_v.y += (look.y / horiz * speedH - s_v.y) * 0.1f;
		}
		s_v.x *= 0.99f, s_v.y *= 0.99f, s_v.z *= 0.98f;
		if (s_boostTicks > 0) // firework boost
		{
			s_boostTicks--;
			s_v.x += look.x * 0.1f + (look.x * 1.5f - s_v.x) * 0.5f;
			s_v.y += look.y * 0.1f + (look.y * 1.5f - s_v.y) * 0.5f;
			s_v.z += look.z * 0.1f + (look.z * 1.5f - s_v.z) * 0.5f;
			fx::firework_trail(s_cur - V3(0, 0, 0.8f));
		}
	}

	// does moving from a to b hit GTA's world or our blocks? returns the hit point and normal
	static bool hits(const V3 &a, const V3 &b, V3 &at, V3 &n)
	{
		V3 d = b - a;
		float len = d.len();
		if (len < 1e-4f)
			return false;
		V3 dir = d * (1.0f / len);
		float pad = dir.z < -0.9f ? 0.0f : 0.4f; // the head sticks out ahead of the ped's centre (not below it)
		GtaHit h = gta_probe_self(a, b + dir * pad, 1 | 2 | 16);
		VoxelHit vh = voxel_raycast(a, dir, len + pad);
		if (vh.hit && (!h.hit || vh.t <= h.t))
		{
			at = vh.pos;
			n = V3((float)FACE_N[vh.face][0], (float)FACE_N[vh.face][1], (float)FACE_N[vh.face][2]);
			return true;
		}
		if (h.hit)
		{
			at = h.pos, n = h.normal;
			return true;
		}
		return false;
	}

	void update(bool allowInput)
	{
		if (!allowInput || !s_worn || g.inVehicle || IS_ENTITY_DEAD(g.ped, FALSE))
		{
			stop();
			return;
		}
		if (!s_on)
		{
			// Minecraft: press jump while falling with an elytra on
			V3 v = GET_ENTITY_VELOCITY(g.ped);
			bool falling = IS_PED_FALLING(g.ped) || (IS_ENTITY_IN_AIR(g.ped) && v.z < -2.0f);
			if (falling && !flight::active() && (IS_CONTROL_JUST_PRESSED(0, 22) || IS_DISABLED_CONTROL_JUST_PRESSED(0, 22)))
				start();
			return;
		}
		if (IS_ENTITY_IN_WATER(g.ped) || IS_PED_RAGDOLL(g.ped))
		{
			stop();
			return;
		}
		DISABLE_CONTROL_ACTION(0, 22, TRUE);
		s_acc += std::min(g.dt, 0.1f);
		while (s_acc >= 0.05f)
		{
			s_acc -= 0.05f;
			tick();
			V3 next = s_cur + s_v * g_cfg.elytraSpeed;
			V3 at, n;
			// lying down, the body reaches ~0.35 m below the ped's centre: check the path, and just under the body
			if (hits(s_cur, next, at, n) || (s_v.z < 0 && hits(next, next - V3(0, 0, 0.35f), at, n)))
			{
				if (n.z > 0.5f) // touched down: Minecraft stops fall flying on the ground
				{
					s_cur = s_prev = V3(at.x, at.y, at.z + 1.0f);
					SET_ENTITY_COORDS_NO_OFFSET(g.ped, s_cur.x, s_cur.y, s_cur.z, FALSE, FALSE, FALSE);
					s_v = V3(s_v.x * 0.3f, s_v.y * 0.3f, 0);
				}
				else // into a wall: stop dead and drop
				{
					s_v = V3(0, 0, 0);
					s_cur = s_prev;
				}
				stop();
				return;
			}
			s_prev = s_cur;
			s_cur = next;
		}
		float a = s_acc / 0.05f;
		V3 p = s_prev + (s_cur - s_prev) * a;
		SET_ENTITY_COORDS_NO_OFFSET(g.ped, p.x, p.y, p.z, FALSE, FALSE, FALSE);
		if (s_v.x * s_v.x + s_v.y * s_v.y > 1e-4f)
			SET_ENTITY_HEADING(g.ped, std::atan2(-s_v.x, s_v.y) * 180.0f / PI);
	}
}
