#include "collision.h"
#include "config.h"
#include "log.h"
#include "world.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

GtaHit gta_probe(const V3 &a, const V3 &b, int ignoreEntity, int flags)
{
	GtaHit h;
	int handle = START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE(a.x, a.y, a.z, b.x, b.y, b.z, flags, ignoreEntity, 7);
	BOOL hit = FALSE;
	Vector3 end{}, nrm{};
	int ent = 0;
	GET_SHAPE_TEST_RESULT(handle, &hit, &end, &nrm, &ent);
	if (hit)
	{
		h.hit = true;
		h.pos = end;
		h.normal = V3(nrm).norm();
		h.entity = ent;
		h.t = (h.pos - a).len();
	}
	return h;
}

namespace collision
{
	static Hash s_model = 0;
	static const char *s_name = "none";
	static V3 s_min, s_max;
	static std::unordered_map<uint64_t, int> s_props; // cell key → object
	static std::unordered_set<int> s_handles;
	static int s_lastVersion = -1;
	static uint32_t s_nextUpdate = 0;

	static const char *CANDIDATES[] = {"stt_prop_stunt_bblock_sml1", "prop_box_wood02a", "prop_box_wood01a",
	                                   "prop_mb_crate_01a", "prop_ld_crate_01", "prop_cs_cardbox_01"};

	void init()
	{
		float bestScore = 1e9f;
		for (const char *name : CANDIDATES)
		{
			Hash h = GET_HASH_KEY(name);
			if (!IS_MODEL_VALID(h))
			{
				logf("collision: %s not valid", name);
				continue;
			}
			REQUEST_MODEL(h);
			for (int i = 0; i < 200 && !HAS_MODEL_LOADED(h); i++)
				WAIT(0);
			Vector3 mn{}, mx{};
			GET_MODEL_DIMENSIONS(h, &mn, &mx);
			V3 sz = V3(mx) - V3(mn);
			// prefer ~1 m in every axis; overshoot is worse than a gap (it blocks where there is no block)
			auto pen = [](float v) { return v > 1.02f ? (v - 1.0f) * 3.0f : 1.0f - v; };
			float score = pen(sz.x) + pen(sz.y) + pen(sz.z);
			logf("collision: %s loaded=%d size %.3f x %.3f x %.3f score %.3f", name, (int)HAS_MODEL_LOADED(h), sz.x,
			     sz.y, sz.z, score);
			if (HAS_MODEL_LOADED(h) && score < bestScore)
			{
				bestScore = score;
				s_model = h;
				s_name = name;
				s_min = mn;
				s_max = mx;
			}
		}
		logf("collision: using %s", s_name);
	}

	bool is_ours(int e) { return e && s_handles.count(e); }
	int count() { return (int)s_props.size(); }
	const char *model_name() { return s_name; }
	V3 model_size() { return s_max - s_min; }

	static void destroy(int obj)
	{
		s_handles.erase(obj);
		if (DOES_ENTITY_EXIST(obj))
		{
			SET_ENTITY_AS_MISSION_ENTITY(obj, TRUE, TRUE);
			DELETE_OBJECT(&obj);
		}
	}

	void clear()
	{
		for (auto &kv : s_props)
			destroy(kv.second);
		s_props.clear();
		s_handles.clear();
	}

	void update()
	{
		if (!s_model)
			return;
		bool changed = s_lastVersion != g_worldVersion;
		if (!changed && g.now < s_nextUpdate)
			return;
		s_nextUpdate = g.now + 250;
		s_lastVersion = g_worldVersion;

		float r = g_cfg.collisionRadius;
		std::vector<std::pair<float, uint64_t>> want;
		for (auto &kv : g_blocks)
		{
			if (!kv.second.exposed)
				continue;
			V3 c = cell_center(key_cell(kv.first));
			float d = (c - g.pedPos).len2();
			if (d < r * r)
				want.push_back({d, kv.first});
		}
		std::sort(want.begin(), want.end());
		if ((int)want.size() > g_cfg.maxCollisionProps)
			want.resize(g_cfg.maxCollisionProps);
		std::unordered_set<uint64_t> keep;
		for (auto &w : want)
			keep.insert(w.second);
		for (auto it = s_props.begin(); it != s_props.end();)
		{
			if (!keep.count(it->first) || !g_blocks.count(it->first))
			{
				destroy(it->second);
				it = s_props.erase(it);
			}
			else
				++it;
		}
		int created = 0;
		for (auto &w : want)
		{
			if (s_props.count(w.second))
				continue;
			if (created >= 24)
			{
				s_nextUpdate = g.now; // finish next frame
				s_lastVersion = -1;
				break;
			}
			Cell c = key_cell(w.second);
			V3 mn = cell_min(c);
			float x = mn.x + 0.5f - (s_min.x + s_max.x) * 0.5f;
			float y = mn.y + 0.5f - (s_min.y + s_max.y) * 0.5f;
			float z = mn.z + 1.0f - s_max.z; // top flush with the cell top
			int obj = CREATE_OBJECT_NO_OFFSET(s_model, x, y, z, FALSE, TRUE, FALSE, 0);
			if (!obj)
				continue;
			SET_ENTITY_ROTATION(obj, 0, 0, 0, 2, TRUE);
			SET_ENTITY_COORDS_NO_OFFSET(obj, x, y, z, FALSE, FALSE, FALSE);
			FREEZE_ENTITY_POSITION(obj, TRUE);
			SET_ENTITY_VISIBLE(obj, FALSE, FALSE);
			SET_ENTITY_COLLISION(obj, TRUE, FALSE);
			SET_ENTITY_CAN_BE_DAMAGED(obj, FALSE);
			SET_ENTITY_INVINCIBLE(obj, TRUE, FALSE);
			s_props[w.second] = obj;
			s_handles.insert(obj);
			created++;
		}
	}
}
