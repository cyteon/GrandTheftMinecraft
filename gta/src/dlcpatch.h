// Fills the shipped block pack (dlc.rpf, blank placeholder textures) with the player's own Minecraft textures.
// The pixels come from textures.bin (written by mcassets); the patch runs from DllMain at the next game start,
// before GTA mounts the DLC, and rewrites gtm_tex.ytd in place (it is the last data in the archive by design).
#pragma once
#include <string>

namespace dlcpatch
{
	std::string find_dlc_rpf(); // mods\update\x64\dlcpacks\gtm\dlc.rpf (or the same under the game folder), "" if none
	bool dlc_ready();           // dlc.rpf already carries real textures (dlc_ready.txt matches its size and time)
	bool apply_pending();       // textures.bin -> dlc.rpf; true if it patched
}
