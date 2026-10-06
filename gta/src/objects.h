// Every GTA object the mod creates goes through here, so the total stays inside a budget: GTA's object pools are
// fixed, and running one dry crashes the game later on one of its own threads. Kinds have their own limits; block
// props get what's left, up to MaxBlockProps. Also logs what's alive every 10 seconds (gtm.log), for crash hunting.
#pragma once
#include "common.h"

namespace objects
{
	enum Kind
	{
		BLOCK, // block props (collision.cpp)
		RIG,   // mob / Steve body parts, held items on rigs, falling blocks, snowballs (rig.cpp)
		FX,    // arrows, debris chips, firework rockets (fx.cpp)
		HAND,  // the first-person held item (always allowed)
		NKINDS
	};
	// 0 when over budget (or GTA refused); physics: a dynamic object GTA simulates (debris)
	int create(Kind k, Hash model, const V3 &p, bool physics = false);
	void destroy(int &obj); // deletes it and forgets it (0 is fine)
	int live(Kind k);
	int live_total();
	int room(Kind k); // how many more of this kind may be made now
	void update();    // drops entries GTA deleted itself; periodic log
}
