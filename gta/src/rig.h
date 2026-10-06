// Minecraft entity rigs (Steve, mobs) posed from a GTA ped. Parts come from the block pack (gtm_<rig>_<part>);
// their pivots and posing come from rigs.txt (written by tools/make_dlc_src.py from Minecraft's model layouts).
// Each frame every part is placed as a frozen, non-colliding object: the body stands upright facing the ped's
// heading (or follows the spine while ragdolling), the head looks at a given direction, limbs copy the direction of
// the matching GTA bones, "fwdarm" limbs are held straight out (zombies).
#pragma once
#include "common.h"
#include <string>
#include <vector>

namespace rig
{
	struct Frame // where a part ended up (for held items, overlays)
	{
		V3 pos, x, y, z; // pivot and the part's right / forward / up axes
	};

	struct Pose
	{
		V3 look;            // head direction (world); zero = straight ahead
		V3 aim;             // non-zero: both arms point this way (bow, crossbow)
		V3 rightArm;        // non-zero: the right arm points this way (attack swing)
		float swingBoth = 0; // 0..1: both arms raised forward (iron golem attack)
		V3 bodyAlong;        // non-zero: the body lies along this direction, head first (elytra glide)
	};

	struct Instance
	{
		std::string rig;
		std::vector<int> objs;
		std::vector<Frame> frames; // per part, after pose()
		float walkPhase = 0, walkAmount = 0; // Minecraft's limbSwing / limbSwingAmount, from how fast it moves
		uint32_t lastPose = 0;
		uint32_t firstPose = 0;    // diagnostics
		int logged = 0;
		void hide();
		int live() const;          // parts that currently exist
	};

	bool load();                       // rigs.txt from the data folder
	bool available(const std::string &rig); // block pack ready and the rig's models exist
	float scale(const std::string &rig);
	int part_index(const std::string &rig, const char *part); // e.g. "rarm"; -1 if none
	void pose(Instance &inst, int ped, const Pose &p);
	// place one prop with the given axes (shared by held items)
	void place(int &obj, Hash &model, const std::string &name, const V3 &pos, const V3 &X, const V3 &Y, const V3 &Z);
	void drop(int &obj);
}
