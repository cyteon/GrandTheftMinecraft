#include "mobs.h"
#include "audio.h"
#include "collision.h"
#include "fx.h"
#include "items.h"
#include "log.h"
#include "rig.h"
#include "world.h"
#include "wither.h"
#include <algorithm>
#include <vector>

namespace mobs
{
	enum Kind
	{
		HOSTILE, // hunts GTA's people
		PASSIVE, // wanders, panics when hurt
		NEUTRAL, // hostile only when provoked (spiders also at night)
		ALLY     // on the player's side: fights hostile mobs
	};
	struct Spec
	{
		const char *rig, *egg, *sound; // sound folder: mob/<sound>/
		const char *say, *hurt, *death; // file names in it (nullptr = none)
		int mcHealth;                   // Minecraft hit points
		float speed;                    // GTA move blend ratio (1 walk, 2 run)
		Kind kind;
	};
	static const Spec SPEC[NTYPES] = {
		{"zombie", "zombie_spawn_egg", "zombie", "say", "hurt", "death", 20, 1.3f, HOSTILE},
		{"skeleton", "skeleton_spawn_egg", "skeleton", "say", "hurt", "death", 20, 1.6f, HOSTILE},
		{"creeper", "creeper_spawn_egg", "creeper", "say", nullptr, "death", 20, 1.4f, HOSTILE},
		{"golem", "iron_golem_spawn_egg", "irongolem", nullptr, "damage", "death", 100, 1.0f, ALLY},
		{"pig", "pig_spawn_egg", "pig", "say", "say", "death", 10, 1.0f, PASSIVE},
		{"cow", "cow_spawn_egg", "cow", "say", "hurt", "hurt", 10, 1.0f, PASSIVE},
		{"sheep", "sheep_spawn_egg", "sheep", "say", "say", "say", 8, 1.0f, PASSIVE},
		{"chicken", "chicken_spawn_egg", "chicken", "say", "hurt", "hurt", 4, 1.0f, PASSIVE},
		{"spider", "spider_spawn_egg", "spider", "say", "say", "death", 16, 2.2f, NEUTRAL},
		{"enderman", "enderman_spawn_egg", "endermen", "idle", "hit", "death", 40, 1.3f, NEUTRAL},
		{"snow_golem", "snow_golem_spawn_egg", "snowgolem", nullptr, nullptr, nullptr, 4, 1.0f, ALLY},
	};

	struct Mob
	{
		Type type;
		int ped = 0;
		rig::Instance body;
		int target = 0;
		uint32_t nextThink = 0, nextAttack = 0, nextSay = 0, issued = 0;
		int lastTarget = -1, lastHealth = 0;
		float fuse = -1;        // creeper: seconds into its hiss (-1 = not primed)
		float slam = 0;         // golem: arm raise 0..1
		uint32_t deadSince = 0; // when it died (0 = alive)
		int held = 0;           // skeleton's bow / enderman's block
		Hash heldModel = 0;
		uint32_t panicUntil = 0;              // animals: running about after being hurt
		int angryAt = 0;                      // neutral mobs: who provoked them
		uint32_t angryUntil = 0;
		int carry = -1;                       // enderman: the block it holds
		uint32_t nextCarry = 0, nextTeleport = 0;
		bool hostileGroup = false;
	};
	static std::vector<Mob> s_mobs;
	static Hash s_hostile = 0, s_ally = 0, s_animal = 0;
	static Hash s_pedModel = 0;
	static const int MAX_MOBS = 40;

	struct Snowball
	{
		V3 p, v;
		float age = 0;
		int owner = 0, obj = 0;
		Hash model = 0;
	};
	static std::vector<Snowball> s_snowballs;

	int count() { return (int)s_mobs.size(); }

	int parts_live()
	{
		int n = 0;
		for (auto &m : s_mobs)
			n += m.body.live();
		return n;
	}

	bool is_mob(int ped)
	{
		for (auto &m : s_mobs)
			if (m.ped == ped)
				return true;
		return false;
	}

	bool is_undead(int ped)
	{
		for (auto &m : s_mobs)
			if (m.ped == ped)
				return m.type == ZOMBIE || m.type == SKELETON;
		return false;
	}

	int egg_type(const std::string &name)
	{
		for (int t = 0; t < NTYPES; t++)
			if (name == SPEC[t].egg)
				return t;
		return -1;
	}

	void init()
	{
		ADD_RELATIONSHIP_GROUP("GTM_HOSTILE", &s_hostile);
		ADD_RELATIONSHIP_GROUP("GTM_GOLEM", &s_ally);
		ADD_RELATIONSHIP_GROUP("GTM_ANIMAL", &s_animal);
		// 0 companion, 1 respect, 3 neutral, 4 dislike, 5 hate. Creative players are left alone by hostile mobs.
		static const char *PEOPLE[] = {"CIVMALE", "CIVFEMALE", "COP", "SECURITY_GUARD", "PRIVATE_SECURITY", "FIREMAN",
		                               "MEDIC", "ARMY", "GANG_1", "GANG_2", "GANG_9", "GANG_10", "AMBIENT_GANG_LOST",
		                               "AMBIENT_GANG_MEXICAN", "AMBIENT_GANG_FAMILY", "AMBIENT_GANG_BALLAS",
		                               "AMBIENT_GANG_MARABUNTE", "AMBIENT_GANG_CULT", "AMBIENT_GANG_SALVA",
		                               "AMBIENT_GANG_WEICHENG", "AMBIENT_GANG_HILLBILLY", "DEALER", "HATES_PLAYER"};
		for (const char *n : PEOPLE)
		{
			Hash h = GET_HASH_KEY(n);
			SET_RELATIONSHIP_BETWEEN_GROUPS(5, s_hostile, h);
			SET_RELATIONSHIP_BETWEEN_GROUPS(5, h, s_hostile);
			SET_RELATIONSHIP_BETWEEN_GROUPS(3, s_animal, h);
			SET_RELATIONSHIP_BETWEEN_GROUPS(3, h, s_animal);
		}
		static const char *FOES[] = {"COP", "ARMY", "GANG_1", "GANG_2", "GANG_9", "GANG_10", "AMBIENT_GANG_LOST",
		                             "AMBIENT_GANG_MEXICAN", "AMBIENT_GANG_FAMILY", "AMBIENT_GANG_BALLAS",
		                             "AMBIENT_GANG_MARABUNTE", "AMBIENT_GANG_SALVA", "AMBIENT_GANG_WEICHENG"};
		for (const char *n : FOES)
		{
			Hash h = GET_HASH_KEY(n);
			SET_RELATIONSHIP_BETWEEN_GROUPS(5, s_ally, h);
			SET_RELATIONSHIP_BETWEEN_GROUPS(5, h, s_ally);
		}
		Hash player = GET_HASH_KEY("PLAYER");
		SET_RELATIONSHIP_BETWEEN_GROUPS(3, s_hostile, player);
		SET_RELATIONSHIP_BETWEEN_GROUPS(3, player, s_hostile);
		SET_RELATIONSHIP_BETWEEN_GROUPS(0, s_ally, player);
		SET_RELATIONSHIP_BETWEEN_GROUPS(0, player, s_ally);
		SET_RELATIONSHIP_BETWEEN_GROUPS(3, s_animal, player);
		SET_RELATIONSHIP_BETWEEN_GROUPS(3, player, s_animal);
		SET_RELATIONSHIP_BETWEEN_GROUPS(5, s_ally, s_hostile);
		SET_RELATIONSHIP_BETWEEN_GROUPS(5, s_hostile, s_ally);
		SET_RELATIONSHIP_BETWEEN_GROUPS(3, s_animal, s_hostile);
		SET_RELATIONSHIP_BETWEEN_GROUPS(3, s_hostile, s_animal);
		SET_RELATIONSHIP_BETWEEN_GROUPS(3, s_animal, s_ally);
		SET_RELATIONSHIP_BETWEEN_GROUPS(3, s_ally, s_animal);
		s_pedModel = GET_HASH_KEY("a_m_y_skater_01");
	}

	static void say(Mob &m, const char *what, float vol = 1.0f)
	{
		if (!what)
			return;
		V3 p = GET_ENTITY_COORDS(m.ped, TRUE);
		float pitch = m.type == CHICKEN ? frand(1.1f, 1.4f) : frand(0.8f, 1.2f);
		audio::play_at(std::string("mob/") + SPEC[m.type].sound + "/" + what, p, vol, pitch, 16);
	}

	static bool night()
	{
		int h = GET_CLOCK_HOURS();
		return h >= 20 || h < 6;
	}

	bool spawn(Type t, const V3 &feet, float heading)
	{
		if (s_mobs.size() >= MAX_MOBS || !rig::available(SPEC[t].rig))
			return false;
		REQUEST_MODEL(s_pedModel);
		for (int i = 0; i < 100 && !HAS_MODEL_LOADED(s_pedModel); i++)
			WAIT(0);
		int ped = CREATE_PED(26, s_pedModel, feet.x, feet.y, feet.z + 1.0f, heading, FALSE, TRUE);
		if (!ped)
			return false;
		const Spec &sp = SPEC[t];
		SET_ENTITY_AS_MISSION_ENTITY(ped, TRUE, TRUE);
		SET_ENTITY_VISIBLE(ped, FALSE, FALSE); // the rig is what you see; the ped still collides and takes hits
		SET_PED_RELATIONSHIP_GROUP_HASH(ped, sp.kind == HOSTILE ? s_hostile : sp.kind == ALLY ? s_ally : s_animal);
		SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, TRUE); // our AI decides, not GTA's panic reactions
		REMOVE_ALL_PED_WEAPONS(ped, TRUE);
		DISABLE_PED_PAIN_AUDIO(ped, TRUE);
		STOP_PED_SPEAKING(ped, TRUE);
		SET_PED_FLEE_ATTRIBUTES(ped, 0, FALSE);
		SET_PED_COMBAT_ATTRIBUTES(ped, 46, TRUE); // always fight
		SET_PED_SUFFERS_CRITICAL_HITS(ped, FALSE);
		int hp = 100 + sp.mcHealth * 10; // GTA peds die below 100
		SET_PED_MAX_HEALTH(ped, hp);
		SET_ENTITY_HEALTH(ped, hp, 0, 0);
		Mob m;
		m.type = t;
		m.ped = ped;
		m.body.rig = sp.rig;
		m.lastHealth = hp;
		m.nextSay = g.now + (uint32_t)frand(2000, 8000);
		m.nextCarry = g.now + (uint32_t)frand(10000, 25000);
		s_mobs.push_back(m);
		logf("mob: spawned %s", sp.rig);
		return true;
	}

	static bool alive(int ped) { return ped && DOES_ENTITY_EXIST(ped) && !IS_PED_DEAD_OR_DYING(ped, TRUE); }

	static const Mob *mob_of(int ped)
	{
		for (auto &o : s_mobs)
			if (o.ped == ped)
				return &o;
		return nullptr;
	}

	static bool is_cop_or_gang(int p)
	{
		static std::vector<Hash> foes;
		if (foes.empty())
			for (const char *n : {"COP", "ARMY", "GANG_1", "GANG_2", "GANG_9", "GANG_10", "AMBIENT_GANG_LOST",
			                      "AMBIENT_GANG_MEXICAN", "AMBIENT_GANG_FAMILY", "AMBIENT_GANG_BALLAS",
			                      "AMBIENT_GANG_MARABUNTE", "AMBIENT_GANG_SALVA", "AMBIENT_GANG_WEICHENG"})
				foes.push_back(GET_HASH_KEY(n));
		Hash grp = GET_PED_RELATIONSHIP_GROUP_HASH(p);
		int type = GET_PED_TYPE(p);
		return type == 6 || type == 27 || type == 29 || std::find(foes.begin(), foes.end(), grp) != foes.end();
	}

	static bool hunting(const Mob &m) // currently out for GTA's people
	{
		const Spec &sp = SPEC[m.type];
		if (sp.kind == HOSTILE)
			return true;
		if (sp.kind != NEUTRAL)
			return false;
		return (m.type == SPIDER && night()) || g.now < m.angryUntil;
	}

	// whom this mob goes after: hostile mobs hunt people (and golems); golems hunt hostile mobs first, then (iron
	// golems) cops and gang members; animals nobody
	static int pick_target(const Mob &m, const V3 &at)
	{
		const Spec &sp = SPEC[m.type];
		if (sp.kind == PASSIVE)
			return 0;
		if (sp.kind == NEUTRAL && g.now < m.angryUntil && alive(m.angryAt))
			return m.angryAt; // whoever provoked it
		bool hostile = sp.kind != ALLY;
		if (hostile && !hunting(m))
			return 0;
		static int peds[1024];
		int n = shv::worldGetAllPeds(peds, 1024);
		int best = 0, bestRank = 99;
		float range = m.type == SNOW_GOLEM ? 16.0f : hostile ? 40.0f : 30.0f;
		float bestD = range * range;
		for (int i = 0; i < n; i++)
		{
			int p = peds[i];
			if (p == m.ped || p == g.ped || IS_PED_A_PLAYER(p) || !alive(p))
				continue;
			const Mob *other = mob_of(p);
			bool hostileMob = (other && hunting(*other)) || wither::is_wither(p);
			bool golem = other && (other->type == GOLEM || other->type == SNOW_GOLEM);
			int rank; // lower is preferred
			if (hostile)
			{
				if (other && !golem)
					continue; // monsters don't fight each other or the animals
				rank = 0;
			}
			else
			{
				if (hostileMob)
					rank = 0;
				else if (m.type == GOLEM && !other && is_cop_or_gang(p))
					rank = 1;
				else
					continue;
			}
			if (m.type != SKELETON && m.type != SNOW_GOLEM && IS_PED_IN_ANY_VEHICLE(p, FALSE))
				continue; // can't reach people in cars
			float d = (V3(GET_ENTITY_COORDS(p, TRUE)) - at).len2();
			if (d > range * range)
				continue;
			if (rank < bestRank || (rank == bestRank && d < bestD))
				bestRank = rank, bestD = d, best = p;
		}
		return best;
	}

	static void hit(Mob &m, int victim, int damage, float knock, float lift)
	{
		V3 a = GET_ENTITY_COORDS(m.ped, TRUE), b = GET_ENTITY_COORDS(victim, TRUE);
		V3 dir = V3(b.x - a.x, b.y - a.y, 0).norm();
		SET_PED_TO_RAGDOLL(victim, lift > 0 ? 2500 : 600, lift > 0 ? 2500 : 600, 0, FALSE, FALSE, FALSE);
		APPLY_DAMAGE_TO_PED(victim, damage, FALSE, 0, 0xA2719263);
		V3 v = dir * knock + V3(0, 0, lift > 0 ? lift : 1.0f);
		SET_ENTITY_VELOCITY(victim, v.x, v.y, v.z);
		audio::play_at("damage/hit", b, 1.0f, frand(0.9f, 1.1f));
	}

	static void go_to(Mob &m, int target, float speed)
	{
		if (m.lastTarget == target && g.now < m.issued + 2000)
			return;
		TASK_GO_TO_ENTITY(m.ped, target, -1, 1.2f, speed, 0, 0);
		m.lastTarget = target;
		m.issued = g.now;
	}

	static void stop_moving(Mob &m)
	{
		if (m.lastTarget != -2)
		{
			CLEAR_PED_TASKS(m.ped);
			m.lastTarget = -2;
		}
	}

	static void wander(Mob &m)
	{
		if (m.lastTarget != -3)
		{
			TASK_WANDER_STANDARD(m.ped, 10.0f, 10);
			m.lastTarget = -3;
		}
	}

	static void face(Mob &m, const V3 &at)
	{
		V3 p = GET_ENTITY_COORDS(m.ped, TRUE);
		SET_ENTITY_HEADING(m.ped, std::atan2(-(at.x - p.x), at.y - p.y) * 180.0f / PI);
	}

	// the enderman's block: put it down on something solid near its feet (Minecraft's EndermanLeaveBlockGoal)
	static bool put_down(Mob &m, const V3 &feet)
	{
		int b = build_for_point(feet);
		if (b < 0 || m.carry < 0)
			return false;
		for (int tries = 0; tries < 8; tries++)
		{
			V3 p = feet + V3(frand(-2, 2), frand(-2, 2), 0.5f);
			Cell c = world_to_cell(b, p);
			for (int dz = 1; dz >= -1; dz--)
			{
				Cell t = c, below = c;
				t.z += dz, below.z += dz - 1;
				if (block_at(t))
					continue;
				const Block *u = block_at(below);
				V3 mn = cell_min(t);
				bool ground = u ? item(u->item).opaque() :
				                  gta_probe(mn + V3(0.5f, 0.5f, 0.1f), mn + V3(0.5f, 0.5f, -0.2f), m.ped, 1).hit;
				if (!ground)
					continue;
				place_block(t, m.carry, 0, 0);
				m.carry = -1;
				return true;
			}
		}
		return false;
	}

	static void teleport(Mob &m, const V3 &from, const V3 *toward)
	{
		for (int tries = 0; tries < 6; tries++)
		{
			V3 aim = toward ? *toward : from;
			float r = toward ? 4.0f : 24.0f;
			float x = aim.x + frand(-r, r), y = aim.y + frand(-r, r), gz = 0;
			if (!GET_GROUND_Z_FOR_3D_COORD(x, y, from.z + 40.0f, &gz, FALSE, FALSE) || std::fabs(gz - from.z) > 30)
				continue;
			fx::portal_burst(from + V3(0, 0, 1), 32);
			SET_ENTITY_COORDS(m.ped, x, y, gz + 1.0f, FALSE, FALSE, FALSE, FALSE);
			fx::portal_burst(V3(x, y, gz + 1), 32);
			audio::play_at("mob/endermen/portal", V3(x, y, gz + 1), 1.0f, 1.0f, 24);
			m.lastTarget = -1;
			return;
		}
	}

	static void remove(size_t i)
	{
		Mob &m = s_mobs[i];
		m.body.hide();
		rig::drop(m.held);
		if (m.ped && DOES_ENTITY_EXIST(m.ped))
		{
			SET_ENTITY_AS_MISSION_ENTITY(m.ped, TRUE, TRUE);
			DELETE_PED(&m.ped);
		}
		s_mobs.erase(s_mobs.begin() + i);
	}

	void clear()
	{
		while (!s_mobs.empty())
			remove(s_mobs.size() - 1);
		for (auto &s : s_snowballs)
			rig::drop(s.obj);
		s_snowballs.clear();
	}

	// who just hurt this mob: the nearest person around (not the player: creative, nor other mobs)
	static int attacker_of(const Mob &m, const V3 &pos)
	{
		static int peds[512];
		int n = shv::worldGetAllPeds(peds, 512), best = 0;
		float bd = 30 * 30;
		for (int i = 0; i < n; i++)
		{
			int p = peds[i];
			if (p == m.ped || IS_PED_A_PLAYER(p) || !alive(p) || mob_of(p))
				continue;
			float d = (V3(GET_ENTITY_COORDS(p, TRUE)) - pos).len2();
			if (d < bd)
				bd = d, best = p;
		}
		return best;
	}

	static void hurt(Mob &m, const V3 &pos)
	{
		say(m, SPEC[m.type].hurt);
		switch (SPEC[m.type].kind)
		{
		case PASSIVE: // Minecraft's PanicGoal: run about for a while
			m.panicUntil = g.now + 5000;
			TASK_SMART_FLEE_COORD(m.ped, pos.x + frand(-1, 1), pos.y + frand(-1, 1), pos.z, 25.0f, 5000, FALSE, FALSE);
			m.lastTarget = -5;
			break;
		case NEUTRAL:
			m.angryAt = attacker_of(m, pos);
			m.angryUntil = g.now + 30000;
			if (m.type == ENDERMAN) // teleports away from harm, and drops what it carries
			{
				if (m.carry >= 0)
					put_down(m, pos - V3(0, 0, 0.98f));
				teleport(m, pos, nullptr);
				audio::play_at("mob/endermen/scream", pos, 1.0f, 1.0f, 24);
			}
			break;
		default:
			break;
		}
		bool hostileNow = hunting(m);
		if (SPEC[m.type].kind == NEUTRAL && hostileNow != m.hostileGroup)
		{
			SET_PED_RELATIONSHIP_GROUP_HASH(m.ped, hostileNow ? s_hostile : s_animal);
			m.hostileGroup = hostileNow;
		}
	}

	static void throw_snowball(Mob &m, const V3 &from, const V3 &at)
	{
		// Minecraft's snow golem: 1.6 blocks/tick, aimed a little high for the drop
		float speed = 32.0f, t = (at - from).len() / speed;
		V3 aim = at + V3(0, 0, 0.5f * 12.0f * t * t);
		Snowball s;
		s.p = from;
		s.v = (aim - from).norm() * speed;
		s.owner = m.ped;
		s_snowballs.push_back(s);
		audio::play_at("random/bow", from, 0.6f, frand(0.4f, 0.6f));
	}

	static void update_snowballs(float dt)
	{
		for (size_t i = 0; i < s_snowballs.size();)
		{
			Snowball &s = s_snowballs[i];
			s.age += dt;
			s.v.z -= 12.0f * dt; // 0.03 blocks/tick^2
			V3 next = s.p + s.v * dt;
			GtaHit h = gta_probe(s.p, next, s.owner, 1 | 2 | 4 | 8 | 16);
			bool done = s.age > 4.0f || (h.hit && !collision::is_ours(h.entity));
			VoxelHit vh = voxel_raycast(s.p, (next - s.p).norm(), (next - s.p).len(), true);
			if (vh.hit)
				done = true;
			if (h.hit && h.entity && DOES_ENTITY_EXIST(h.entity) && GET_ENTITY_TYPE(h.entity) == 1)
			{
				// a knock, no damage (Minecraft's snowballs only hurt blazes)
				V3 kb = V3(s.v.x, s.v.y, 0).norm() * 4.0f + V3(0, 0, 2.0f);
				SET_PED_TO_RAGDOLL(h.entity, 600, 600, 0, FALSE, FALSE, FALSE);
				SET_ENTITY_VELOCITY(h.entity, kb.x, kb.y, kb.z);
			}
			if (done)
			{
				fx::poof(h.hit ? h.pos : vh.hit ? vh.pos : s.p);
				rig::drop(s.obj);
				s_snowballs[i] = s_snowballs.back();
				s_snowballs.pop_back();
				continue;
			}
			s.p = next;
			V3 F = (g.camPos - s.p).norm(), R = V3(0, 0, 1).cross(F).norm(), U = F.cross(R);
			rig::place(s.obj, s.model, "gtm_i_snowball", s.p, R, U, F);
			i++;
		}
	}

	static void think(Mob &m, const V3 &pos)
	{
		const Spec &sp = SPEC[m.type];
		if (g.now >= m.nextThink)
		{
			m.nextThink = g.now + 700;
			if (!alive(m.target) || (V3(GET_ENTITY_COORDS(m.target, TRUE)) - pos).len() > 60 ||
			    (sp.kind == NEUTRAL && !hunting(m)))
				m.target = pick_target(m, pos);
			bool hostileNow = hunting(m);
			if (sp.kind == NEUTRAL && hostileNow != m.hostileGroup) // cops shoot at hunting spiders, not at sleepy ones
			{
				SET_PED_RELATIONSHIP_GROUP_HASH(m.ped, hostileNow ? s_hostile : s_animal);
				m.hostileGroup = hostileNow;
			}
		}
		// endermen pick up blocks and put them down elsewhere
		if (m.type == ENDERMAN && !alive(m.target) && g.now >= m.nextCarry)
		{
			m.nextCarry = g.now + (uint32_t)frand(15000, 35000);
			V3 feet = pos - V3(0, 0, 0.98f);
			if (m.carry >= 0)
				put_down(m, feet);
			else
				for (auto &kv : g_blocks)
				{
					Cell c = key_cell(kv.first);
					const Item &it = item(kv.second.item);
					if (!it.opaque() || it.name == "bedrock" || it.name == "obsidian" ||
					    (cell_center(c) - feet).len2() > 5.5f * 5.5f)
						continue;
					Cell up = c;
					up.z++;
					if (block_at(up))
						continue; // only blocks with nothing on them
					m.carry = kv.second.item;
					remove_block(c);
					break;
				}
		}
		if (m.type == ENDERMAN && g.now >= m.nextTeleport && rand() % 400 == 0) // now and then, for no reason
		{
			m.nextTeleport = g.now + 20000;
			teleport(m, pos, nullptr);
		}
		if (g.now < m.panicUntil)
			return;
		if (!alive(m.target))
		{
			m.target = 0;
			wander(m);
			m.fuse = -1;
			return;
		}
		V3 tp = GET_ENTITY_COORDS(m.target, TRUE);
		float d = (tp - pos).len();
		switch (m.type)
		{
		case ZOMBIE:
			if (d > 1.6f)
				go_to(m, m.target, sp.speed);
			else if (g.now >= m.nextAttack)
			{
				face(m, tp);
				hit(m, m.target, 30, 3.0f, 0); // Minecraft zombie: 3 damage
				m.nextAttack = g.now + 1000;
			}
			break;
		case SKELETON:
			if (d > 15.0f)
				go_to(m, m.target, sp.speed);
			else if (d < 5.0f && m.lastTarget != -4) // too close: back off
			{
				TASK_SMART_FLEE_PED(m.ped, m.target, 9.0f, 2500, FALSE, FALSE);
				m.lastTarget = -4;
				m.issued = g.now;
			}
			else
			{
				if (m.lastTarget == -4 && g.now < m.issued + 2500)
					break;
				stop_moving(m);
				face(m, tp);
				if (g.now >= m.nextAttack && HAS_ENTITY_CLEAR_LOS_TO_ENTITY(m.ped, m.target, 17))
				{
					// Minecraft skeleton: arrow at 1.6 blocks/tick, aimed a little high for the drop
					V3 from = V3(GET_PED_BONE_COORDS(m.ped, 31086, 0, 0, 0)) + V3(0, 0, 0.1f);
					V3 at = V3(GET_PED_BONE_COORDS(m.target, 24818, 0, 0, 0));
					float speed = 32.0f, t = (at - from).len() / speed;
					at.z += 0.5f * 20.0f * t * t;
					V3 dir = (at - from).norm();
					dir = (dir + V3(frand(-1, 1), frand(-1, 1), frand(-1, 1)) * 0.02f).norm();
					fx::shoot_arrow(from + dir * 0.5f, dir, speed, 40, false, m.ped);
					audio::play_at("random/bow", from, 1.0f, frand(0.8f, 1.0f));
					m.nextAttack = g.now + (uint32_t)frand(1500, 2400);
				}
			}
			break;
		case CREEPER:
			if (m.fuse < 0)
			{
				if (d > 2.6f)
					go_to(m, m.target, sp.speed);
				else
				{
					stop_moving(m);
					m.fuse = 0;
					audio::play_at("random/fuse", pos, 1.0f, 0.5f, 24);
				}
			}
			else if (d > 7.0f) // the target got away: Minecraft creepers calm down
				m.fuse = -1;
			break;
		case GOLEM:
			if (d > 2.4f)
				go_to(m, m.target, sp.speed);
			else if (g.now >= m.nextAttack)
			{
				face(m, tp);
				m.slam = 1.0f;
				// Minecraft iron golem: 7-21 damage and a throw into the air
				hit(m, m.target, (int)frand(70, 210), 2.0f, 9.0f);
				say(m, "throw");
				m.nextAttack = g.now + 1250;
			}
			break;
		case SPIDER:
			if (d > 1.6f)
			{
				go_to(m, m.target, sp.speed);
				// Minecraft's LeapAtTargetGoal: a jump at its prey from a few blocks off
				if (d < 5.0f && g.now >= m.nextAttack)
				{
					V3 dir = V3(tp.x - pos.x, tp.y - pos.y, 0).norm();
					SET_ENTITY_VELOCITY(m.ped, dir.x * 5.0f, dir.y * 5.0f, 4.0f);
					m.nextAttack = g.now + 1200;
				}
			}
			else if (g.now >= m.nextAttack)
			{
				face(m, tp);
				hit(m, m.target, 20, 2.0f, 0); // Minecraft spider: 2 damage
				m.nextAttack = g.now + 1000;
			}
			break;
		case ENDERMAN:
			if (d > 20.0f && g.now >= m.nextTeleport) // far away: teleport closer
			{
				m.nextTeleport = g.now + 5000;
				teleport(m, pos, &tp);
			}
			else if (d > 2.0f)
				go_to(m, m.target, 2.0f);
			else if (g.now >= m.nextAttack)
			{
				face(m, tp);
				hit(m, m.target, 70, 3.0f, 0); // Minecraft enderman: 7 damage
				m.nextAttack = g.now + 1200;
			}
			break;
		case SNOW_GOLEM:
			if (d > 10.0f)
				go_to(m, m.target, sp.speed);
			else
			{
				stop_moving(m);
				face(m, tp);
				if (g.now >= m.nextAttack && HAS_ENTITY_CLEAR_LOS_TO_ENTITY(m.ped, m.target, 17))
				{
					V3 from = pos + V3(0, 0, 0.6f);
					throw_snowball(m, from, V3(GET_PED_BONE_COORDS(m.target, 24818, 0, 0, 0)));
					m.nextAttack = g.now + 1000; // Minecraft: once a second
				}
			}
			break;
		default:
			break;
		}
	}

	void update()
	{
		float dt = std::min(g.dt, 0.1f);
		update_snowballs(dt);
		for (size_t i = 0; i < s_mobs.size();)
		{
			Mob &m = s_mobs[i];
			if (!DOES_ENTITY_EXIST(m.ped))
			{
				remove(i);
				continue;
			}
			V3 pos = GET_ENTITY_COORDS(m.ped, TRUE);
			if ((pos - g.pedPos).len() > 250.0f) // far away: gone, like Minecraft despawning
			{
				remove(i);
				continue;
			}
			bool dead = IS_PED_DEAD_OR_DYING(m.ped, TRUE);
			if (dead && !m.deadSince)
			{
				m.deadSince = g.now;
				say(m, SPEC[m.type].death);
				if (m.carry >= 0) // Minecraft: an enderman drops its block
					put_down(m, pos - V3(0, 0, 0.98f));
			}
			if (m.deadSince && g.now - m.deadSince > 1200)
			{
				fx::poof(pos);
				remove(i);
				continue;
			}
			if (!dead)
			{
				int hp = GET_ENTITY_HEALTH(m.ped);
				if (hp < m.lastHealth - 5)
					hurt(m, pos);
				m.lastHealth = hp;
				if (g.now >= m.nextSay && SPEC[m.type].say)
				{
					say(m, SPEC[m.type].say, 0.8f);
					m.nextSay = g.now + (uint32_t)frand(6000, 16000);
				}
				think(m, pos);
				// creeper: hiss, flash white, explode after 1.5 s (30 ticks)
				if (m.type == CREEPER && m.fuse >= 0)
				{
					m.fuse += dt;
					if (m.fuse >= 1.5f)
					{
						fx::explode(pos, 3.0f);
						remove(i);
						continue;
					}
				}
			}
			m.slam = std::max(0.0f, m.slam - dt * 3.0f);
			// pose the rig
			rig::Pose p;
			if (alive(m.target))
				p.look = (V3(GET_PED_BONE_COORDS(m.target, 31086, 0, 0, 0)) - V3(GET_PED_BONE_COORDS(m.ped, 31086, 0, 0, 0))).norm();
			if (m.type == SKELETON && alive(m.target) && m.lastTarget == -2)
				p.aim = p.look; // drawing the bow
			float hd = deg2rad(GET_ENTITY_HEADING(m.ped));
			V3 F(-std::sin(hd), std::cos(hd), 0), U(0, 0, 1), R = F.cross(U);
			if (m.type == ENDERMAN && m.carry >= 0)
				p.aim = (F * 0.9f - U * 0.45f).norm(); // holding the block out in front
			p.swingBoth = std::sin(m.slam * PI * 0.5f);
			rig::pose(m.body, m.ped, p);
			// creeper flash: white boxes over the head and body every 5 ticks while primed
			if (m.type == CREEPER && m.fuse >= 0 && ((int)(m.fuse * 20) / 5) % 2 == 0)
			{
				float s = rig::scale("creeper");
				for (const char *part : {"head", "body"})
				{
					int pi = rig::part_index("creeper", part);
					if (pi < 0 || pi >= (int)m.body.frames.size())
						continue;
					const rig::Frame &f = m.body.frames[pi];
					bool head = part[0] == 'h';
					V3 c = f.pos + f.z * ((head ? 4 : -6) * s);
					V3 ax[3] = {f.x * ((head ? 4.1f : 4.1f) * s), f.y * ((head ? 4.1f : 2.1f) * s), f.z * ((head ? 4.1f : 6.1f) * s)};
					fx::flash_box(c, ax, 200);
				}
			}
			// skeleton's bow in its right hand
			if (m.type == SKELETON)
			{
				int ra = rig::part_index("skeleton", "rarm");
				if (ra >= 0 && ra < (int)m.body.frames.size() && !dead)
				{
					const rig::Frame &f = m.body.frames[ra];
					V3 hand = f.pos - f.z * (10 * rig::scale("skeleton"));
					// mirrored across the sprite's diagonal so the bow's arc faces away from the skeleton
					V3 ix = (f.y + f.z).norm(), iy = (f.y - f.z).norm(), iz = ix.cross(iy);
					rig::place(m.held, m.heldModel, "gtm_i_bow_tp", hand + f.y * 0.12f, ix, iy, iz);
				}
				else
					rig::drop(m.held);
			}
			// the enderman's block, held out in front of its chest
			if (m.type == ENDERMAN)
			{
				int bi = rig::part_index("enderman", "body");
				if (m.carry >= 0 && !dead && bi >= 0 && bi < (int)m.body.frames.size())
				{
					V3 at = m.body.frames[bi].pos + F * 0.55f - U * 0.55f;
					rig::place(m.held, m.heldModel, "gtm_" + item(m.carry).name + "_h", at, R, U, F * -1.0f);
				}
				else
					rig::drop(m.held);
			}
			i++;
		}
	}
}
