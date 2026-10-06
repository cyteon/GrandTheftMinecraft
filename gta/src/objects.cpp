#include "objects.h"
#include "collision.h"
#include "config.h"
#include "log.h"
#include "mobs.h"
#include "fx.h"
#include <algorithm>
#include <unordered_map>

namespace objects
{
	static std::unordered_map<int, Kind> s_live;
	static int s_count[NKINDS] = {};
	static int s_refused[NKINDS] = {};
	static const int LIMIT[NKINDS] = {100000, 480, 160, 100000}; // blocks: whatever the total leaves

	int live(Kind k) { return s_count[k]; }
	int live_total() { return (int)s_live.size(); }

	int room(Kind k)
	{
		int total = std::max(0, g_cfg.maxModObjects - live_total());
		return std::max(0, std::min(total, LIMIT[k] - s_count[k]));
	}

	int create(Kind k, Hash model, const V3 &p, bool physics)
	{
		if (k != HAND && room(k) <= 0)
		{
			s_refused[k]++;
			return 0;
		}
		int obj = physics ? CREATE_OBJECT(model, p.x, p.y, p.z, FALSE, TRUE, TRUE)
		                  : CREATE_OBJECT_NO_OFFSET(model, p.x, p.y, p.z, FALSE, TRUE, FALSE, 0);
		if (obj)
		{
			auto it = s_live.find(obj);
			if (it != s_live.end()) // a handle GTA reused after deleting it itself
				s_count[it->second]--;
			s_live[obj] = k;
			s_count[k]++;
		}
		return obj;
	}

	void destroy(int &obj)
	{
		if (!obj)
			return;
		auto it = s_live.find(obj);
		if (it != s_live.end())
		{
			s_count[it->second]--;
			s_live.erase(it);
		}
		if (DOES_ENTITY_EXIST(obj))
		{
			SET_ENTITY_AS_MISSION_ENTITY(obj, TRUE, TRUE);
			DELETE_OBJECT(&obj);
		}
		obj = 0;
	}

	void update()
	{
		static uint32_t nextPurge = 0, nextLog = 0;
		if (g.now >= nextPurge)
		{
			nextPurge = g.now + 2000;
			for (auto it = s_live.begin(); it != s_live.end();)
				if (!DOES_ENTITY_EXIST(it->first))
				{
					s_count[it->second]--;
					it = s_live.erase(it);
				}
				else
					++it;
		}
		if (g.now >= nextLog)
		{
			nextLog = g.now + 10000;
			logf("objects: %d/%d (blocks %d, rigs %d, fx %d, hand %d; refused %d/%d/%d) mobs %d arrows %d particles %d "
			     "probes %u (pending %u)",
			     live_total(), g_cfg.maxModObjects, s_count[BLOCK], s_count[RIG], s_count[FX], s_count[HAND],
			     s_refused[BLOCK], s_refused[RIG], s_refused[FX], mobs::count(), fx::arrow_count(), fx::particle_count(),
			     g_probeCount, g_probePending);
			g_probeCount = 0;
		}
	}
}
