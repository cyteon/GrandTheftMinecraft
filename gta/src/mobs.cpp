#include "mobs.h"
#include "audio.h"
#include "collision.h"
#include "fx.h"
#include "log.h"
#include "rig.h"
#include "world.h"
#include <algorithm>
#include <vector>

namespace mobs
{
	struct Spec
	{
		const char *rig, *egg, *sound; // sound folder: mob/<sound>/
		int mcHealth;                  // Minecraft hit points
		float speed;                   // GTA move blend ratio (1 walk, 2 run)
		bool hostile;
	};
	static const Spec SPEC[NTYPES] = {
		{"zombie", "zombie_spawn_egg", "zombie", 20, 1.3f, true},
		{"skeleton", "skeleton_spawn_egg", "skeleton", 20, 1.6f, true},
		{"creeper", "creeper_spawn_egg", "creeper", 20, 1.4f, true},
		{"golem", "iron_golem_spawn_egg", "irongolem", 100, 1.0f, false},
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
		int held = 0;           // skeleton's bow
		Hash heldModel = 0;
	};
	static std::vector<Mob> s_mobs;
	static Hash s_hostile = 0, s_ally = 0;
	static Hash s_pedModel = 0;
	static const int MAX_MOBS = 40;

	int count() { return (int)s_mobs.size(); }

	bool is_mob(int ped)
	{
		for (auto &m : s_mobs)
			if (m.ped == ped)
				return true;
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
		SET_RELATIONSHIP_BETWEEN_GROUPS(5, s_ally, s_hostile);
		SET_RELATIONSHIP_BETWEEN_GROUPS(5, s_hostile, s_ally);
		s_pedModel = GET_HASH_KEY("a_m_y_skater_01");
	}

	static void say(Mob &m, const char *what, float vol = 1.0f)
	{
		V3 p = GET_ENTITY_COORDS(m.ped, TRUE);
		audio::play_at(std::string("mob/") + SPEC[m.type].sound + "/" + what, p, vol, frand(0.8f, 1.2f), 16);
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
		SET_ENTITY_AS_MISSION_ENTITY(ped, TRUE, TRUE);
		SET_ENTITY_VISIBLE(ped, FALSE, FALSE); // the rig is what you see; the ped still collides and takes hits
		SET_PED_RELATIONSHIP_GROUP_HASH(ped, SPEC[t].hostile ? s_hostile : s_ally);
		SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, TRUE); // our AI decides, not GTA's panic reactions
		REMOVE_ALL_PED_WEAPONS(ped, TRUE);
		DISABLE_PED_PAIN_AUDIO(ped, TRUE);
		STOP_PED_SPEAKING(ped, TRUE);
		SET_PED_FLEE_ATTRIBUTES(ped, 0, FALSE);
		SET_PED_COMBAT_ATTRIBUTES(ped, 46, TRUE); // always fight
		SET_PED_SUFFERS_CRITICAL_HITS(ped, FALSE);
		int hp = 100 + SPEC[t].mcHealth * 10; // GTA peds die below 100
		SET_PED_MAX_HEALTH(ped, hp);
		SET_ENTITY_HEALTH(ped, hp, 0, 0);
		Mob m;
		m.type = t;
		m.ped = ped;
		m.body.rig = SPEC[t].rig;
		m.lastHealth = hp;
		m.nextSay = g.now + (uint32_t)frand(2000, 8000);
		s_mobs.push_back(m);
		logf("mob: spawned %s", SPEC[t].rig);
		return true;
	}

	static bool alive(int ped) { return ped && DOES_ENTITY_EXIST(ped) && !IS_PED_DEAD_OR_DYING(ped, TRUE); }

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

	// whom this mob goes after: hostile mobs hunt people (and golems); golems hunt hostile mobs first, then cops
	// and gang members
	static int pick_target(const Mob &m, const V3 &at)
	{
		static int peds[1024];
		int n = shv::worldGetAllPeds(peds, 1024);
		bool hostile = SPEC[m.type].hostile;
		int best = 0, bestRank = 99;
		float bestD = (hostile ? 40.0f : 30.0f) * (hostile ? 40.0f : 30.0f);
		for (int i = 0; i < n; i++)
		{
			int p = peds[i];
			if (p == m.ped || p == g.ped || IS_PED_A_PLAYER(p) || !alive(p))
				continue;
			int mobType = -1;
			for (auto &o : s_mobs)
				if (o.ped == p)
					mobType = o.type;
			bool isHostileMob = mobType >= 0 && SPEC[mobType].hostile;
			int rank; // lower is preferred
			if (hostile)
			{
				if (isHostileMob)
					continue; // monsters don't fight each other
				rank = 0;
			}
			else
			{
				if (isHostileMob)
					rank = 0;
				else if (mobType < 0 && is_cop_or_gang(p))
					rank = 1;
				else
					continue;
			}
			if (m.type != SKELETON && IS_PED_IN_ANY_VEHICLE(p, FALSE))
				continue; // can't reach people in cars
			float d = (V3(GET_ENTITY_COORDS(p, TRUE)) - at).len2();
			if (rank < bestRank || (rank == bestRank && d < bestD))
			{
				if (d > (hostile ? 1600.0f : 900.0f))
					continue;
				bestRank = rank, bestD = d, best = p;
			}
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

	static void face(Mob &m, const V3 &at)
	{
		V3 p = GET_ENTITY_COORDS(m.ped, TRUE);
		SET_ENTITY_HEADING(m.ped, std::atan2(-(at.x - p.x), at.y - p.y) * 180.0f / PI);
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
	}

	static void think(Mob &m, const V3 &pos)
	{
		const Spec &sp = SPEC[m.type];
		if (g.now >= m.nextThink)
		{
			m.nextThink = g.now + 700;
			if (!alive(m.target) || (V3(GET_ENTITY_COORDS(m.target, TRUE)) - pos).len() > 60)
				m.target = pick_target(m, pos);
		}
		if (!alive(m.target))
		{
			m.target = 0;
			if (m.lastTarget != -3)
			{
				TASK_WANDER_STANDARD(m.ped, 10.0f, 10);
				m.lastTarget = -3;
			}
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
		default:
			break;
		}
	}

	void update()
	{
		float dt = std::min(g.dt, 0.1f);
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
				say(m, "death");
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
					say(m, m.type == GOLEM ? "damage" : "hurt");
				m.lastHealth = hp;
				if (g.now >= m.nextSay && m.type != GOLEM)
				{
					say(m, "say", 0.8f);
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
					V3 ix = (f.y - f.z).norm(), iy = (f.y + f.z).norm(), iz = ix.cross(iy);
					rig::place(m.held, m.heldModel, "gtm_i_bow_tp", hand + f.y * 0.12f, ix, iy, iz);
				}
				else
					rig::drop(m.held);
			}
			i++;
		}
	}
}
