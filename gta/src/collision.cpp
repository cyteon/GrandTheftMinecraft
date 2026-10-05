#include "collision.h"
#include "config.h"
#include "dlcpatch.h"
#include "items.h"
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
	Hash mat = 0;
	GET_SHAPE_TEST_RESULT_INCLUDING_MATERIAL(handle, &hit, &end, &nrm, &mat, &ent);
	if (hit)
	{
		h.hit = true;
		h.pos = end;
		h.normal = V3(nrm).norm();
		h.entity = ent;
		h.material = mat;
		h.t = (h.pos - a).len();
	}
	return h;
}

GtaHit gta_probe_self(const V3 &a, const V3 &b, int flags, int alsoIgnore)
{
	int veh = g.inVehicle ? PED::GET_VEHICLE_PED_IS_IN(g.ped, FALSE) : 0;
	V3 from = a, dir = (b - a).norm();
	float total = (b - a).len();
	for (int i = 0; i < 4; i++)
	{
		GtaHit h = gta_probe(from, b, g.ped, flags);
		if (!h.hit || (h.entity != g.ped && (!veh || h.entity != veh) && (!alsoIgnore || h.entity != alsoIgnore)))
		{
			if (h.hit)
				h.t = (h.pos - a).len();
			return h;
		}
		from = h.pos + dir * 0.05f; // inside our own car: step past the hit and look again
		if ((from - a).len() >= total)
			break;
	}
	return GtaHit{};
}

namespace collision
{
	// Stage 2: the DLC's textured gtm_<block> props (visible, lit, exact 1 m collision).
	// Fallback (no DLC): invisible stock crates for collision only, Stage 1 polygons for looks.
	static bool s_dlc = false;
	static std::vector<Hash> s_itemModel; // per item id, 0 = none
	static Hash s_model = 0;              // fallback crate
	static const char *s_name = "none";
	static V3 s_min, s_max;
	struct Prop
	{
		int obj;
		uint16_t item;
		uint8_t facing;
	};
	static std::unordered_map<uint64_t, Prop> s_props; // cell key -> object
	static std::unordered_set<int> s_handles;
	static int s_lastVersion = -1;
	static uint32_t s_nextUpdate = 0;

	static const char *CANDIDATES[] = {"prop_box_wood01a", "prop_box_wood02a", "prop_mb_crate_01a", "prop_ld_crate_01"};

	static void init_fallback()
	{
		float bestScore = 1e9f;
		for (const char *name : CANDIDATES)
		{
			Hash h = GET_HASH_KEY(name);
			if (!IS_MODEL_VALID(h))
				continue;
			REQUEST_MODEL(h);
			for (int i = 0; i < 200 && !HAS_MODEL_LOADED(h); i++)
				WAIT(0);
			Vector3 mn{}, mx{};
			GET_MODEL_DIMENSIONS(h, &mn, &mx);
			V3 sz = V3(mx) - V3(mn);
			// prefer ~1 m in every axis; overshoot is worse than a gap (it blocks where there is no block)
			auto pen = [](float v) { return v > 1.02f ? (v - 1.0f) * 3.0f : 1.0f - v; };
			float score = pen(sz.x) + pen(sz.y) + pen(sz.z);
			if (HAS_MODEL_LOADED(h) && score < bestScore)
			{
				bestScore = score;
				s_model = h;
				s_name = name;
				s_min = mn;
				s_max = mx;
			}
		}
		logf("collision: fallback crate %s", s_name);
	}

	void init()
	{
		s_itemModel.assign(g_items.size(), 0);
		int found = 0;
		for (size_t i = 0; i < g_items.size(); i++)
		{
			if (!g_items[i].block)
				continue;
			Hash h = GET_HASH_KEY(("gtm_" + g_items[i].name).c_str());
			if (IS_MODEL_VALID(h))
			{
				s_itemModel[i] = h;
				found++;
			}
		}
		s_dlc = found > 0 && dlcpatch::dlc_ready();
		if (found > 0 && !s_dlc)
			logf("blocks: DLC loaded but its textures aren't filled yet (restart GTA once after setup)");
		if (s_dlc)
		{
			s_name = "gtm_* (DLC)";
			s_min = V3(-0.5f, -0.5f, -0.5f), s_max = V3(0.5f, 0.5f, 0.5f);
			logf("blocks: DLC pack found, %d block models", found);
		}
		else
		{
			logf("blocks: DLC pack NOT found (gtm_* models invalid) - using Stage 1 polygons + invisible crates");
			init_fallback();
		}
	}

	bool dlc() { return s_dlc; }
	bool is_ours(int e) { return e && s_handles.count(e); }
	int count() { return (int)s_props.size(); }
	const char *model_name() { return s_name; }
	V3 model_size() { return s_max - s_min; }

	bool has_prop(uint64_t key)
	{
		if (!s_dlc)
			return false;
		auto it = s_props.find(key);
		return it != s_props.end() && it->second.obj;
	}

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
			destroy(kv.second.obj);
		s_props.clear();
		s_handles.clear();
	}

	void update()
	{
		if (!s_dlc && !s_model)
			return;
		bool changed = s_lastVersion != g_worldVersion;
		if (!changed && g.now < s_nextUpdate)
			return;
		s_nextUpdate = g.now + 250;
		s_lastVersion = g_worldVersion;

		float r = s_dlc ? g_cfg.propRadius : g_cfg.collisionRadius;
		int cap = s_dlc ? g_cfg.maxBlockProps : g_cfg.maxCollisionProps;
		std::vector<std::pair<float, uint64_t>> want;
		for (auto &kv : g_blocks)
		{
			if (!kv.second.exposed)
				continue;
			V3 c = cell_center(key_cell(kv.first));
			// visible props follow the camera (what you see), collision-only crates follow the player
			float d = (c - (s_dlc ? g.camPos : g.pedPos)).len2();
			if (d < r * r)
				want.push_back({d, kv.first});
		}
		std::sort(want.begin(), want.end());
		if ((int)want.size() > cap)
			want.resize(cap);
		std::unordered_set<uint64_t> keep;
		for (auto &w : want)
			keep.insert(w.second);
		for (auto it = s_props.begin(); it != s_props.end();)
		{
			auto bk = g_blocks.find(it->first);
			if (!keep.count(it->first) || bk == g_blocks.end() || (s_dlc && (bk->second.item != it->second.item || bk->second.facing != it->second.facing)))
			{
				destroy(it->second.obj);
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
			if (created >= 32)
			{
				s_nextUpdate = g.now; // finish next frame
				s_lastVersion = -1;
				break;
			}
			const Block &bk = g_blocks[w.second];
			Cell c = key_cell(w.second);
			V3 mn = cell_min(c);
			Hash model = s_dlc ? s_itemModel[bk.item] : s_model;
			if (!model)
				continue; // no prop for this block: the polygon renderer draws it
			if (!HAS_MODEL_LOADED(model))
			{
				REQUEST_MODEL(model);
				s_nextUpdate = g.now;
				s_lastVersion = -1;
				continue;
			}
			float x, y, z;
			if (s_dlc)
				x = mn.x + 0.5f, y = mn.y + 0.5f, z = mn.z + 0.5f; // models are centred
			else
			{
				x = mn.x + 0.5f - (s_min.x + s_max.x) * 0.5f;
				y = mn.y + 0.5f - (s_min.y + s_max.y) * 0.5f;
				z = mn.z + 1.0f - s_max.z; // top flush with the cell top
			}
			int obj = CREATE_OBJECT_NO_OFFSET(model, x, y, z, FALSE, TRUE, FALSE, 0);
			if (!obj)
				continue;
			SET_ENTITY_ROTATION(obj, 0, 0, s_dlc ? bk.facing * 22.5f : 0, 2, TRUE);
			SET_ENTITY_COORDS_NO_OFFSET(obj, x, y, z, FALSE, FALSE, FALSE);
			FREEZE_ENTITY_POSITION(obj, TRUE);
			SET_ENTITY_VISIBLE(obj, s_dlc, FALSE);
			SET_ENTITY_COLLISION(obj, TRUE, FALSE);
			SET_ENTITY_CAN_BE_DAMAGED(obj, FALSE);
			SET_ENTITY_INVINCIBLE(obj, TRUE, FALSE);
			if (s_dlc)
				SET_ENTITY_LOD_DIST(obj, (int)(g_cfg.propRadius + 50));
			s_props[w.second] = {obj, bk.item, bk.facing};
			s_handles.insert(obj);
			created++;
		}
	}
}
