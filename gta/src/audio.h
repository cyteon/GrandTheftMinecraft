// Minecraft sounds (WAVs converted from the user's own Minecraft assets) through miniaudio, positioned in 3D.
#pragma once
#include "common.h"
#include <string>

namespace audio
{
	bool init();
	void shutdown();
	void update(); // listener = GTA camera; frees finished voices
	// name like "dig/stone" picks a random numbered variant if "dig/stone1.wav".. exist; pos = nullptr → 2D (UI)
	void play(const std::string &name, const V3 *pos = nullptr, float volume = 1.0f, float pitch = 1.0f,
	          float maxDist = 16.0f);
	inline void play_at(const std::string &name, const V3 &pos, float volume = 1.0f, float pitch = 1.0f,
	                    float maxDist = 16.0f)
	{
		play(name, &pos, volume, pitch, maxDist);
	}
	// The block's sound group ("stone", "grass", "glass"...) for breaking and placing.
	void block_sound(const std::string &group, const V3 &pos, bool breaking);
}
