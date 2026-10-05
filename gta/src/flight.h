// Creative flight: double-tap Space. The ped is frozen and moved by hand (velocity-driven movement triggers GTA's
// falling animations). Space up, Ctrl down, Sprint faster; landing on something ends it.
#pragma once

namespace flight
{
	void update(bool allowInput);
	bool active();
	void stop();

	// ladders: forward / jump climbs, nothing slides down, crouch holds on
	void climb_update(bool allowInput);
	void climb_stop();
	bool climbing();
}
