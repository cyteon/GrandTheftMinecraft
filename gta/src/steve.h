// Steve in third person: the player's GTA ped is hidden and Steve's six box parts (from the block pack) are placed
// every frame. Body and pivots follow the ped; each limb copies the direction of the matching GTA bones, so GTA's
// animations (walk, run, jump, ragdoll) drive Minecraft-style stiff limbs. The head follows the camera, the right
// arm swings on attacks, both arms aim while a bow or crossbow is drawn, and the held item sits in the right hand.
#pragma once

namespace steve
{
	bool available();      // block pack ready and Steve's models exist
	void update(bool show); // show = third person, on foot, Minecraft mode
	void hide();
}
