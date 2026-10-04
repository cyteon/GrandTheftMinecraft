#include "steve.h"
#include "collision.h"
#include "common.h"
#include "gui.h"
#include "hand.h"
#include "interact.h"
#include "items.h"
#include <algorithm>
#include <string>

namespace steve
{
	static const float PX = 0.9375f / 16; // one skin pixel (Minecraft draws players at 15/16 scale)

	enum Part
	{
		HEAD,
		BODY,
		RARM,
		LARM,
		RLEG,
		LLEG,
		HELD,
		NPARTS
	};
	static const char *MODEL[6] = {"gtm_steve_head", "gtm_steve_body", "gtm_steve_rarm",
	                               "gtm_steve_larm", "gtm_steve_rleg", "gtm_steve_lleg"};
	static int s_obj[NPARTS] = {};
	static Hash s_model[NPARTS] = {};

	bool available()
	{
		if (!collision::dlc())
			return false;
		for (const char *m : MODEL)
			if (!IS_MODEL_VALID(GET_HASH_KEY(m)))
				return false;
		return true;
	}

	static void drop(int i)
	{
		if (s_obj[i] && DOES_ENTITY_EXIST(s_obj[i]))
		{
			SET_ENTITY_AS_MISSION_ENTITY(s_obj[i], TRUE, TRUE);
			DELETE_OBJECT(&s_obj[i]);
		}
		s_obj[i] = 0;
		s_model[i] = 0;
	}

	void hide()
	{
		for (int i = 0; i < NPARTS; i++)
			drop(i);
	}

	static void quat(const V3 &X, const V3 &Y, const V3 &Z, float &x, float &y, float &z, float &w)
	{
		// rotation whose columns are the world directions of the model's x, y and z axes
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

	// place part i (model `name`) at pos with model axes X, Y, Z (world directions)
	static void place(int i, const std::string &name, const V3 &pos, const V3 &X, const V3 &Y, const V3 &Z)
	{
		Hash h = GET_HASH_KEY(name.c_str());
		if (s_obj[i] && (s_model[i] != h || !DOES_ENTITY_EXIST(s_obj[i])))
			drop(i);
		if (!s_obj[i])
		{
			if (!IS_MODEL_VALID(h))
				return;
			if (!HAS_MODEL_LOADED(h))
			{
				REQUEST_MODEL(h);
				return;
			}
			s_obj[i] = CREATE_OBJECT_NO_OFFSET(h, pos.x, pos.y, pos.z, FALSE, TRUE, FALSE, 0);
			if (!s_obj[i])
				return;
			s_model[i] = h;
			FREEZE_ENTITY_POSITION(s_obj[i], TRUE);
			SET_ENTITY_COLLISION(s_obj[i], FALSE, FALSE);
			SET_ENTITY_CAN_BE_DAMAGED(s_obj[i], FALSE);
		}
		float qx, qy, qz, qw;
		quat(X, Y, Z, qx, qy, qz, qw);
		SET_ENTITY_COORDS_NO_OFFSET(s_obj[i], pos.x, pos.y, pos.z, FALSE, FALSE, FALSE);
		SET_ENTITY_QUATERNION(s_obj[i], qx, qy, qz, qw);
	}

	static V3 bone(int id) { return GET_PED_BONE_COORDS(g.ped, id, 0, 0, 0); }

	// a limb hanging along d: model z = up the limb, y = forward (kept close to the body's forward), x = right
	static void limb_axes(const V3 &d, const V3 &fwd, V3 &X, V3 &Y, V3 &Z)
	{
		Z = (d * -1.0f).norm();
		Y = fwd - Z * fwd.dot(Z);
		if (Y.len2() < 1e-4f)
			Y = V3(0, 0, 1) - Z * Z.z;
		Y = Y.norm();
		X = Y.cross(Z);
	}

	static V3 toward(const V3 &a, const V3 &b, float t) { return (a * (1 - t) + b * t).norm(); }

	void update(bool show)
	{
		if (!show || !available())
		{
			hide();
			return;
		}
		// body: upright and facing the ped's heading, unless ragdolling (then it follows the spine)
		float h = deg2rad(GET_ENTITY_HEADING(g.ped));
		V3 U(0, 0, 1), F(-std::sin(h), std::cos(h), 0);
		V3 pelvis = bone(11816), neckB = bone(39317);
		if (IS_PED_RAGDOLL(g.ped))
		{
			U = (neckB - pelvis).norm();
			F = (F - U * F.dot(U)).norm();
		}
		V3 R = F.cross(U);
		V3 hip = g.pedPos - U * 0.28f; // the ped's root sits ~1 m above its feet; Steve's hips are 12 px up
		V3 neck = hip + U * (12 * PX);

		// head looks where the camera looks (yaw limited relative to the body, like Minecraft)
		V3 look = g.camDir;
		float yawBody = std::atan2(-F.x, F.y), yawLook = std::atan2(-look.x, look.y);
		float dy = std::remainder(yawLook - yawBody, 2 * PI);
		dy = clampf(dy, deg2rad(-75), deg2rad(75));
		float pitch = clampf(std::asin(clampf(look.z, -1, 1)), deg2rad(-80), deg2rad(80));
		float yh = yawBody + dy;
		V3 Fh(-std::sin(yh) * std::cos(pitch), std::cos(yh) * std::cos(pitch), std::sin(pitch));
		V3 Rh = Fh.cross(V3(0, 0, 1)).norm(), Uh = Rh.cross(Fh);

		// limbs copy the GTA skeleton's directions
		V3 dRA = (bone(57005) - bone(40269)).norm(), dLA = (bone(18905) - bone(45509)).norm();
		V3 dRL = (bone(52301) - bone(51826)).norm(), dLL = (bone(14201) - bone(58271)).norm();
		float progress = 0;
		int use = interact::hand_use(progress);
		if (use != hand::USE_NONE) // aiming a bow / crossbow: both arms point where you look
			dRA = dLA = look;
		float swing = gui::swing_progress();
		if (swing < 1.0f) // attack: the right arm swings forward
			dRA = toward(dRA, look, std::sin(swing * PI));

		V3 X, Y, Z;
		place(HEAD, MODEL[HEAD], neck, Rh, Fh, Uh);
		place(BODY, MODEL[BODY], neck, R, F, U);
		V3 rs = neck + R * (5 * PX) - U * (2 * PX), ls = neck - R * (5 * PX) - U * (2 * PX);
		limb_axes(dRA, F, X, Y, Z);
		place(RARM, MODEL[RARM], rs, X, Y, Z);
		V3 handPos = rs - Z * (10 * PX), handX = X, handY = Y, handZ = Z;
		limb_axes(dLA, F, X, Y, Z);
		place(LARM, MODEL[LARM], ls, X, Y, Z);
		limb_axes(dRL, F, X, Y, Z);
		place(RLEG, MODEL[RLEG], hip + R * (1.9f * PX), X, Y, Z);
		limb_axes(dLL, F, X, Y, Z);
		place(LLEG, MODEL[LLEG], hip - R * (1.9f * PX), X, Y, Z);

		// the held item in the right hand (hand frame: X right, Y forward, Z up the arm)
		const Slot &sl = g_hotbar[g_sel];
		if (sl.empty())
		{
			drop(HELD);
			return;
		}
		const Item &it = item(sl.item);
		if (it.block)
		{
			// item space is y-up, z towards the viewer: x -> right, y -> up the arm, z -> backwards
			place(HELD, "gtm_" + it.name + "_h", handPos + handY * 0.12f, handX, handZ, handY * -1.0f);
		}
		else
		{
			// the sprite's diagonal (where blades and bows point) along the hand's forward direction
			V3 ix = (handY - handZ).norm(), iy = (handY + handZ).norm(), iz = ix.cross(iy);
			std::string sprite = hand::sprite_for(sl.item, (hand::Use)use, progress);
			place(HELD, "gtm_i_" + sprite + "_tp", handPos + handY * (0.25f * 1.4142f * 0.7f), ix, iy, iz);
		}
	}
}
