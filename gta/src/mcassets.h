// First-launch setup: builds everything Minecraft-looking from the player's own Minecraft 1.21.11 (a local launcher
// install, or the official files fetched from Mojang's servers and checked against Mojang's SHA1s). Nothing of
// Mojang's ships with the mod. Runs on a background thread; the game keeps running.
//
// Produces in GrandTheftMinecraft\: gui/, font/, items/, particles/, sounds/ (.ogg), items.txt, assets.ok, and
// textures.bin (the block pack's pixels; dlcpatch writes them into dlc.rpf at the next game start).
#pragma once
#include <string>

namespace mcassets
{
	bool data_ready();      // GUI/items/sounds built (assets.ok matches this version)
	bool start_if_needed(); // starts the builder thread when data or the block pack's textures are missing
	bool running();
	bool finished();        // the builder finished this session
	bool failed();
	bool textures_pending(); // textures.bin waiting for the next game start
	std::string status();   // human-readable progress / error
}
