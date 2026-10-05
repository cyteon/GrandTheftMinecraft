#include "steve.h"
#include "common.h"
#include "elytra.h"
#include "gui.h"
#include "hand.h"
#include "interact.h"
#include "items.h"
#include "rig.h"
#include <string>

namespace steve
{
	static rig::Instance s_rig{"steve"};
	static int s_held = 0;
	static Hash s_heldModel = 0;
	static int s_wing[2] = {};
	static Hash s_wingModel[2] = {};
	static float s_spread = 0; // elytra wings: 0 folded on the back, 1 spread for gliding

	bool available() { return rig::available("steve"); }

	void hide()
	{
		s_rig.hide();
		rig::drop(s_held);
		rig::drop(s_wing[0]);
		rig::drop(s_wing[1]);
	}

	// rotation of a Minecraft model part (xRot, yRot, zRot in radians, applied X then Y then Z like PartPose),
	// expressed in our part space (x = right, y = forward, z = up; Minecraft x = left, y = down, z = back)
	static void mc_rotation(float xr, float yr, float zr, V3 &cx, V3 &cy, V3 &cz)
	{
		auto rx = [](float a, const V3 &v) { return V3(v.x, v.y * std::cos(a) - v.z * std::sin(a), v.y * std::sin(a) + v.z * std::cos(a)); };
		auto ry = [](float a, const V3 &v) { return V3(v.x * std::cos(a) + v.z * std::sin(a), v.y, -v.x * std::sin(a) + v.z * std::cos(a)); };
		auto rz = [](float a, const V3 &v) { return V3(v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a), v.z); };
		auto toMc = [](const V3 &o) { return V3(-o.x, -o.z, -o.y); };   // ours -> Minecraft
		auto fromMc = [](const V3 &m) { return V3(-m.x, -m.z, -m.y); }; // Minecraft -> ours
		auto rot = [&](const V3 &o) { return fromMc(rx(xr, ry(yr, rz(zr, toMc(o))))); };
		cx = rot(V3(1, 0, 0)), cy = rot(V3(0, 1, 0)), cz = rot(V3(0, 0, 1));
	}

	// the elytra on Steve's back (ElytraModel.setupAnim: folded 15/15 degrees, spread to 20/90 while gliding)
	static void wings()
	{
		int bi = rig::part_index("steve", "body");
		if (!elytra::worn() || bi < 0 || bi >= (int)s_rig.frames.size())
		{
			rig::drop(s_wing[0]);
			rig::drop(s_wing[1]);
			return;
		}
		float want = 0;
		if (elytra::gliding())
		{
			V3 v = elytra::velocity();
			float f4 = 1.0f;
			if (v.z < 0 && v.len2() > 1e-4f)
				f4 = 1.0f - std::pow(-v.norm().z, 1.5f); // diving folds them back in
			want = f4;
		}
		s_spread += (want - s_spread) * std::min(1.0f, g.dt * 8.0f);
		float xr = s_spread * 0.34906584f + (1 - s_spread) * 0.2617994f;
		float zr = s_spread * -1.5707964f + (1 - s_spread) * -0.2617994f;
		const rig::Frame &b = s_rig.frames[bi];
		float s = rig::scale("steve");
		for (int w = 0; w < 2; w++)
		{
			// left wing: Minecraft pivot (5, 0, 2) = 5 px to the character's left, 2 px behind the back
			V3 pivot = b.pos + b.x * ((w == 0 ? -5 : 5) * s) + b.y * (-2 * s);
			V3 cx, cy, cz;
			mc_rotation(xr, 0, w == 0 ? zr : -zr, cx, cy, cz);
			V3 X = b.x * cx.x + b.y * cx.y + b.z * cx.z;
			V3 Y = b.x * cy.x + b.y * cy.y + b.z * cy.z;
			V3 Z = b.x * cz.x + b.y * cz.y + b.z * cz.z;
			rig::place(s_wing[w], s_wingModel[w], w == 0 ? "gtm_elytra_lwing" : "gtm_elytra_rwing", pivot, X, Y, Z);
		}
	}

	void update(bool show)
	{
		if (!show || !available())
		{
			hide();
			return;
		}
		rig::Pose p;
		p.look = g.camDir; // the head looks where you look
		float progress = 0;
		int use = interact::hand_use(progress);
		if (use != hand::USE_NONE) // aiming a bow / crossbow: both arms point where you look
			p.aim = g.camDir;
		float swing = gui::swing_progress();
		if (swing < 1.0f) // attack: the right arm swings forward
		{
			int ra = rig::part_index("steve", "rarm");
			if (ra >= 0 && ra < (int)s_rig.frames.size())
			{
				V3 hang = s_rig.frames[ra].z * -1.0f;
				float a = std::sin(swing * PI);
				p.rightArm = (hang * (1 - a) + g.camDir * a).norm();
			}
		}
		if (elytra::gliding()) // lying along the flight, head first
			p.bodyAlong = elytra::velocity().len2() > 1.0f ? elytra::velocity() : g.camDir;
		rig::pose(s_rig, g.ped, p);
		wings();

		// the held item in the right hand (arm frame: x right, y forward, z up the arm; the hand is 10 px down)
		const Slot &sl = g_hotbar[g_sel];
		int ra = rig::part_index("steve", "rarm");
		if (sl.empty() || ra < 0 || ra >= (int)s_rig.frames.size())
		{
			rig::drop(s_held);
			return;
		}
		const rig::Frame &f = s_rig.frames[ra];
		V3 handPos = f.pos - f.z * (10 * rig::scale("steve"));
		const Item &it = item(sl.item);
		if (it.held3d()) // item space is y-up, z towards the viewer: x -> right, y -> up the arm, z -> backwards
			rig::place(s_held, s_heldModel, "gtm_" + it.name + "_h", handPos + f.y * 0.12f, f.x, f.z, f.y * -1.0f);
		else
		{
			// the sprite's diagonal (where blades and bows point) along the hand's forward direction
			V3 ix = (f.y - f.z).norm(), iy = (f.y + f.z).norm(), iz = ix.cross(iy);
			if (it.name == "bow") // mirrored across that diagonal: the arc faces away from you, the string towards you
				std::swap(ix, iy), iz = ix.cross(iy);
			std::string sprite = hand::sprite_for(sl.item, (hand::Use)use, progress);
			rig::place(s_held, s_heldModel, "gtm_i_" + sprite + "_tp", handPos + f.y * (0.25f * 1.4142f * 0.7f), ix, iy, iz);
		}
	}
}
