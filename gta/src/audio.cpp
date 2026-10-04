#define STB_VORBIS_HEADER_ONLY
#include "../vendor/stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_FLAC
#define MA_NO_MP3
#include "../vendor/miniaudio.h"

#include "audio.h"
#include "config.h"
#include "log.h"
#include <unordered_map>
#include <vector>
#include <windows.h>

namespace audio
{
	static ma_engine s_engine;
	static bool s_ok = false;
	static std::vector<ma_sound *> s_voices;
	static std::unordered_map<std::string, int> s_variants; // name → number of numbered variants (0 = plain)

	static bool exists(const std::string &p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

	bool init()
	{
		ma_engine_config cfg = ma_engine_config_init();
		cfg.listenerCount = 1;
		if (ma_engine_init(&cfg, &s_engine) != MA_SUCCESS)
		{
			logf("audio: ma_engine_init failed");
			return false;
		}
		ma_engine_set_volume(&s_engine, g_cfg.volume);
		ma_engine_listener_set_world_up(&s_engine, 0, 0, 0, 1);
		s_ok = true;
		logf("audio: ok");
		return true;
	}

	void shutdown()
	{
		if (!s_ok)
			return;
		for (auto *v : s_voices)
		{
			ma_sound_uninit(v);
			delete v;
		}
		s_voices.clear();
		ma_engine_uninit(&s_engine);
		s_ok = false;
	}

	void update()
	{
		if (!s_ok)
			return;
		ma_engine_listener_set_position(&s_engine, 0, g.camPos.x, g.camPos.y, g.camPos.z);
		ma_engine_listener_set_direction(&s_engine, 0, g.camDir.x, g.camDir.y, g.camDir.z);
		for (size_t i = 0; i < s_voices.size();)
		{
			if (ma_sound_at_end(s_voices[i]) || !ma_sound_is_playing(s_voices[i]))
			{
				ma_sound_uninit(s_voices[i]);
				delete s_voices[i];
				s_voices[i] = s_voices.back();
				s_voices.pop_back();
			}
			else
				i++;
		}
	}

	static std::string resolve(const std::string &name)
	{
		auto it = s_variants.find(name);
		int n;
		if (it == s_variants.end())
		{
			n = 0;
			while (n < 8 && (exists(g_dataDir + "sounds/" + name + std::to_string(n + 1) + ".ogg") ||
			                 exists(g_dataDir + "sounds/" + name + std::to_string(n + 1) + ".wav")))
				n++;
			s_variants[name] = n;
		}
		else
			n = it->second;
		std::string base = g_dataDir + "sounds/" + name + (n ? std::to_string(1 + rand() % n) : std::string());
		return exists(base + ".ogg") ? base + ".ogg" : base + ".wav";
	}

	void play(const std::string &name, const V3 *pos, float volume, float pitch, float maxDist)
	{
		if (!s_ok || s_voices.size() > 48)
			return;
		std::string path = resolve(name);
		auto *s = new ma_sound;
		ma_uint32 flags = MA_SOUND_FLAG_DECODE | (pos ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION);
		if (ma_sound_init_from_file(&s_engine, path.c_str(), flags, nullptr, nullptr, s) != MA_SUCCESS)
		{
			delete s;
			static int warned = 0;
			if (warned++ < 10)
				logf("audio: cannot load %s", path.c_str());
			return;
		}
		ma_sound_set_volume(s, volume);
		ma_sound_set_pitch(s, pitch);
		if (pos)
		{
			ma_sound_set_position(s, pos->x, pos->y, pos->z);
			ma_sound_set_attenuation_model(s, ma_attenuation_model_linear);
			ma_sound_set_min_distance(s, 1.0f);
			ma_sound_set_max_distance(s, maxDist);
		}
		ma_sound_start(s);
		s_voices.push_back(s);
	}

	void block_sound(const std::string &group, const V3 &pos, bool breaking)
	{
		if (group == "glass" && breaking)
			play_at("random/glass", pos, 1.0f, frand(0.8f, 1.0f));
		else
			play_at("dig/" + (group == "glass" ? std::string("stone") : group), pos, 1.0f, 0.8f);
	}
}

// stb_vorbis implementation last: it defines short macros (L, C, R) that would clash with our headers
#undef STB_VORBIS_HEADER_ONLY
#include "../vendor/stb_vorbis.c"
