#include "flight.h"
#include "collision.h"
#include "common.h"
#include "elytra.h"
#include "log.h"
#include "world.h"

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
		if (!allowInput || g.inVehicle || IS_ENTITY_DEAD(g.ped, FALSE) || IS_PED_RAGDOLL(g.ped) || elytra::gliding())
		{
			stop();
			return;
		}
		if (IS_CONTROL_JUST_PRESSED(0, 22) || (s_on && IS_DISABLED_CONTROL_JUST_PRESSED(0, 22)))
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
		float lr = GET_DISABLED_CONTROL_NORMAL(0, 30), ud = GET_DISABLED_CONTROL_NORMAL(0, 31);
		bool sprint = IS_DISABLED_CONTROL_PRESSED(0, 21);
		float speed = sprint ? 21.6f : 10.9f; // Minecraft creative flight, blocks/s
		V3 want = fwd * -ud + right * lr;
		if (want.len2() > 1)
			want = want.norm();
		want *= speed;
		if (IS_DISABLED_CONTROL_PRESSED(0, 22))
			want.z += 7.5f;
		if (IS_DISABLED_CONTROL_PRESSED(0, 36))
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
}
