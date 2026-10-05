#include "flight.h"
#include "collision.h"
#include "common.h"
#include "elytra.h"
#include "log.h"
#include "world.h"
#include "items.h"
#include "shapes.h"

namespace flight
{
	static bool s_on = false;
	static uint32_t s_lastJump = 0;
	static V3 s_vel;

	bool active() { return s_on; }

	void stop()
	{
		if (!s_on)
			return;
		s_on = false;
		FREEZE_ENTITY_POSITION(g.ped, FALSE);
		SET_ENTITY_VELOCITY(g.ped, s_vel.x, s_vel.y, std::min(s_vel.z, 0.0f));
	}

	static void start()
	{
		s_on = true;
		s_vel = V3(0, 0, 0);
		CLEAR_PED_TASKS_IMMEDIATELY(g.ped);
		FREEZE_ENTITY_POSITION(g.ped, TRUE);
	}

	// Would moving the player's body from `from` to `to` hit GTA's world or one of our blocks?
	static bool blocked(const V3 &from, const V3 &to)
	{
		const float offs[3] = {-0.9f, 0.0f, 0.7f};
		V3 d = to - from;
		float len = d.len();
		if (len > 1e-4f)
		{
			V3 dir = d * (1.0f / len);
			for (float o : offs)
			{
				V3 a = from + V3(0, 0, o), b = to + V3(0, 0, o) + dir * 0.3f;
				if (gta_probe(a, b, g.ped, 1 | 2 | 16).hit)
					return true;
			}
		}
		return boxes_hit_blocks(V3(to.x - 0.3f, to.y - 0.3f, to.z - 0.98f), V3(to.x + 0.3f, to.y + 0.3f, to.z + 0.85f));
	}

	void update(bool allowInput)
	{
		// allowInput = false (the inventory is open): keep flying, hovering in place
		if (g.inVehicle || IS_ENTITY_DEAD(g.ped, FALSE) || IS_PED_RAGDOLL(g.ped) || elytra::gliding())
		{
			stop();
			return;
		}
		if (allowInput && (IS_CONTROL_JUST_PRESSED(0, 22) || (s_on && IS_DISABLED_CONTROL_JUST_PRESSED(0, 22))))
		{
			if (g.now - s_lastJump < 300)
			{
				if (s_on)
					stop();
				else
					start();
				s_lastJump = 0;
			}
			else
				s_lastJump = g.now;
		}
		if (!s_on)
			return;
		DISABLE_CONTROL_ACTION(0, 22, TRUE); // jump = ascend
		DISABLE_CONTROL_ACTION(0, 36, TRUE); // duck/stealth = descend
		float h = deg2rad(g.camRot.z);
		V3 fwd(-std::sin(h), std::cos(h), 0), right(std::cos(h), std::sin(h), 0);
		float lr = allowInput ? GET_DISABLED_CONTROL_NORMAL(0, 30) : 0, ud = allowInput ? GET_DISABLED_CONTROL_NORMAL(0, 31) : 0;
		bool sprint = allowInput && IS_DISABLED_CONTROL_PRESSED(0, 21);
		float speed = sprint ? 21.6f : 10.9f; // Minecraft creative flight, blocks/s
		V3 want = fwd * -ud + right * lr;
		if (want.len2() > 1)
			want = want.norm();
		want *= speed;
		if (allowInput && IS_DISABLED_CONTROL_PRESSED(0, 22))
			want.z += 7.5f;
		if (allowInput && IS_DISABLED_CONTROL_PRESSED(0, 36))
			want.z -= 7.5f;
		s_vel += (want - s_vel) * std::min(1.0f, g.dt * 8.0f);

		V3 np = g.pedPos;
		V3 move = s_vel * std::min(g.dt, 0.1f);
		// horizontal, axis by axis so we slide along walls
		V3 tryX(np.x + move.x, np.y, np.z);
		if (std::fabs(move.x) > 1e-5f && !blocked(np, tryX))
			np = tryX;
		else
			s_vel.x = 0;
		V3 tryY(np.x, np.y + move.y, np.z);
		if (std::fabs(move.y) > 1e-5f && !blocked(np, tryY))
			np = tryY;
		else
			s_vel.y = 0;
		if (move.z > 0)
		{
			GtaHit up = gta_probe(np + V3(0, 0, 0.7f), np + V3(0, 0, 0.9f + move.z), g.ped, 1 | 2 | 16);
			if (!up.hit && !boxes_hit_blocks(V3(np.x - 0.3f, np.y - 0.3f, np.z + move.z - 0.98f),
			                                  V3(np.x + 0.3f, np.y + 0.3f, np.z + move.z + 0.85f)))
				np.z += move.z;
			else
				s_vel.z = 0;
		}
		else if (move.z < 0)
		{
			GtaHit dn = gta_probe(np - V3(0, 0, 0.9f), np - V3(0, 0, 1.05f - move.z), g.ped, 1 | 2 | 16);
			bool blk = boxes_hit_blocks(V3(np.x - 0.3f, np.y - 0.3f, np.z + move.z - 1.0f),
			                            V3(np.x + 0.3f, np.y + 0.3f, np.z + move.z + 0.85f));
			if (dn.hit || blk)
			{
				// touched down: Minecraft ends flight on landing
				if (dn.hit)
					np.z = dn.pos.z + 1.0f;
				SET_ENTITY_COORDS_NO_OFFSET(g.ped, np.x, np.y, np.z, FALSE, FALSE, FALSE);
				s_vel = V3(0, 0, 0);
				stop();
				return;
			}
			np.z += move.z;
		}
		SET_ENTITY_COORDS_NO_OFFSET(g.ped, np.x, np.y, np.z, FALSE, FALSE, FALSE);
		SET_ENTITY_HEADING(g.ped, g.camRot.z);
	}

	// ---- ladders: in a ladder's block, forward or jump climbs, nothing slides down slowly, crouch holds on (Minecraft) ----
	static bool s_climb = false;

	static const Block *ladder_at(const V3 &p)
	{
		int b = build_for_point(p);
		if (b < 0)
			return nullptr;
		const Block *k = block_at(world_to_cell(b, p));
		return k && item(k->item).shape == SH_LADDER ? k : nullptr;
	}

	void climb_stop()
	{
		if (!s_climb)
			return;
		s_climb = false;
		FREEZE_ENTITY_POSITION(g.ped, FALSE);
	}

	bool climbing() { return s_climb; }

	void climb_update(bool allowInput)
	{
		if (s_on || elytra::gliding() || g.inVehicle || IS_ENTITY_DEAD(g.ped, FALSE) || IS_PED_RAGDOLL(g.ped))
		{
			climb_stop();
			return;
		}
		V3 feet = g.pedPos - V3(0, 0, 0.9f);
		const Block *lad = ladder_at(feet + V3(0, 0, 0.1f));
		if (!lad)
			lad = ladder_at(g.pedPos);
		float ud = allowInput ? GET_DISABLED_CONTROL_NORMAL(0, 31) : 0; // < 0 = forward
		bool up = allowInput && (ud < -0.3f || IS_DISABLED_CONTROL_PRESSED(0, 22));
		bool hold = allowInput && IS_DISABLED_CONTROL_PRESSED(0, 36);
		bool away = allowInput && ud > 0.3f;
		if (!lad || away)
		{
			if (s_climb && lad == nullptr && up)
			{
				// off the top: step onto the block the ladder leans on
				climb_stop();
				SET_ENTITY_VELOCITY(g.ped, g.camDir.x * 2.0f, g.camDir.y * 2.0f, 3.0f);
				return;
			}
			climb_stop();
			return;
		}
		if (!s_climb)
		{
			GtaHit dn = gta_probe(g.pedPos, g.pedPos - V3(0, 0, 1.15f), g.ped, 1 | 2 | 16);
			bool grounded = dn.hit || boxes_hit_blocks(feet - V3(0.25f, 0.25f, 0.15f), feet + V3(0.25f, 0.25f, 0.0f));
			if (grounded && !up)
				return; // standing at the foot of a ladder
			s_climb = true;
			CLEAR_PED_TASKS_IMMEDIATELY(g.ped);
			FREEZE_ENTITY_POSITION(g.ped, TRUE);
		}
		DISABLE_CONTROL_ACTION(0, 22, TRUE);
		DISABLE_CONTROL_ACTION(0, 36, TRUE);
		float vz = up ? 2.35f : hold ? 0.0f : -2.35f; // Minecraft: 0.2 blocks a tick up, 0.15 down
		V3 p = g.pedPos;
		float dz = vz * std::min(g.dt, 0.1f);
		if (dz < 0)
		{
			GtaHit dn = gta_probe(p, p - V3(0, 0, 1.0f - dz), g.ped, 1 | 2 | 16);
			if (dn.hit || boxes_hit_blocks(V3(p.x - 0.25f, p.y - 0.25f, p.z - 1.0f + dz), V3(p.x + 0.25f, p.y + 0.25f, p.z - 0.9f)))
			{
				climb_stop(); // feet on the ground
				return;
			}
		}
		else if (dz > 0 && gta_probe(p + V3(0, 0, 0.7f), p + V3(0, 0, 0.9f + dz), g.ped, 1 | 2 | 16).hit)
			dz = 0; // head against a ceiling
		SET_ENTITY_COORDS_NO_OFFSET(g.ped, p.x, p.y, p.z + dz, FALSE, FALSE, FALSE);
		// face the ladder
		float h = (shapes::dir_of_facing(lad->facing) * 90.0f) + 180.0f;
		SET_ENTITY_HEADING(g.ped, h);
	}
}
