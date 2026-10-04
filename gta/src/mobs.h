// Minecraft mobs in GTA: each is an invisible GTA ped (GTA does the walking, pathing, collision and takes the
// damage: cops really shoot them) wearing a Minecraft rig. Hostile mobs hunt GTA's people; the iron golem is on the
// player's side and fights hostile mobs, cops and gangs.
//   zombie   walks up and hits          skeleton  keeps its distance and shoots arrows
//   creeper  walks up, hisses, explodes  golem     slams and throws its target into the air
#pragma once
#include "common.h"
#include <string>

namespace mobs
{
	enum Type
	{
		ZOMBIE,
		SKELETON,
		CREEPER,
		GOLEM,
		NTYPES
	};
	void init();
	void update();
	void clear();
	int count();
	int parts_live(); // diagnostics: rig parts that exist (6 per mob expected)
	bool spawn(Type t, const V3 &feet, float heading);
	int egg_type(const std::string &itemName); // "zombie_spawn_egg" -> ZOMBIE, -1 if not an egg
	bool is_mob(int ped);
}
