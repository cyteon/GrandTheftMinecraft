#include "steve.h"
#include "common.h"
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

	bool available() { return rig::available("steve"); }

	void hide()
	{
		s_rig.hide();
		rig::drop(s_held);
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
		rig::pose(s_rig, g.ped, p);

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
		if (it.block) // item space is y-up, z towards the viewer: x -> right, y -> up the arm, z -> backwards
			rig::place(s_held, s_heldModel, "gtm_" + it.name + "_h", handPos + f.y * 0.12f, f.x, f.z, f.y * -1.0f);
		else
		{
			// the sprite's diagonal (where blades and bows point) along the hand's forward direction
			V3 ix = (f.y - f.z).norm(), iy = (f.y + f.z).norm(), iz = ix.cross(iy);
			std::string sprite = hand::sprite_for(sl.item, (hand::Use)use, progress);
			rig::place(s_held, s_heldModel, "gtm_i_" + sprite + "_tp", handPos + f.y * (0.25f * 1.4142f * 0.7f), ix, iy, iz);
		}
	}
}
