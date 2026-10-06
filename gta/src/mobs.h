// Minecraft mobs in GTA: each is an invisible GTA ped (GTA does the walking, pathing, collision and takes the
// damage: cops really shoot them) wearing a Minecraft rig.
//   zombie    walks up and hits               skeleton  keeps its distance and shoots arrows
//   creeper   walks up, hisses, explodes      golem     slams and throws its target into the air (player's side)
//   pig, cow, sheep, chicken  wander and panic when hurt
//   spider    hostile at night: leaps and bites; in daylight only if hurt
//   enderman  wanders, carries your blocks off; hurt, it teleports away and comes back for whoever hit it
//   snow golem  throws snowballs at hostile mobs (player's side)
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
		PIG,
		COW,
		SHEEP,
		CHICKEN,
		SPIDER,
		ENDERMAN,
		SNOW_GOLEM,
		NTYPES
	};
	void init();
	void update();
	void clear();
	int count();
	int parts_live(); // diagnostics: rig parts that exist
	bool spawn(Type t, const V3 &feet, float heading);
	int egg_type(const std::string &itemName); // "zombie_spawn_egg" -> ZOMBIE, -1 if not an egg
	bool is_mob(int ped);
	bool is_undead(int ped); // zombies and skeletons (the Wither spares them, like Minecraft)
	bool shear(int ped);     // shears on a sheep: its wool comes off (and grows back)
}
