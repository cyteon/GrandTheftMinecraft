#include "rig.h"
#include "collision.h"
#include "config.h"
#include "log.h"
#include <fstream>
#include <map>
#include <sstream>

namespace rig
{
	enum Kind
	{
		BODY,
		HEAD,
		LIMB,
		FWDARM,
		WING,  // posed by whoever wears it (elytra)
		SWING, // a leg turning about the vertical with the walk (spiders); b0 = which way (+1 / -1)
		WALK   // a leg swinging forward and back with the walk; b0 = half a stride later (1) or not, b1 = amplitude x 100
	};
	struct PartDef
	{
		std::string model, name;
		Kind kind;
		V3 pivot; // pixels: x right, y forward, z above the ground
		int b0, b1;
		bool right, arm;
	};
	struct RigDef
	{
		float scale = 1 / 16.0f;
		std::vector<PartDef> parts;
	};
	static std::map<std::string, RigDef> s_rigs;

	bool load()
	{
		std::ifstream in(g_dataDir + "rigs.txt");
		std::string line;
		while (std::getline(in, line))
		{
			if (line.empty() || line[0] == '#')
				continue;
			std::vector<std::string> f;
			std::stringstream ss(line);
			std::string t;
			while (std::getline(ss, t, ';'))
				f.push_back(t);
			if (f[0] == "rig" && f.size() >= 3)
				s_rigs[f[1]].scale = std::stof(f[2]);
			else if (f[0] == "part" && f.size() >= 9) // part;rig;model;kind;x;y;z;bone0;bone1
			{
				PartDef p;
				p.model = f[2];
				std::string prefix = "gtm_r_" + f[1] + "_"; // gtm_r_<rig>_<name>
				p.name = p.model.rfind(prefix, 0) == 0 ? p.model.substr(prefix.size()) : p.model;
				p.kind = f[3] == "body"   ? BODY
				         : f[3] == "head"   ? HEAD
				         : f[3] == "fwdarm" ? FWDARM
				         : f[3] == "wing"   ? WING
				         : f[3] == "swing"  ? SWING
				         : f[3] == "walk"   ? WALK
				                            : LIMB;
				p.pivot = V3(std::stof(f[4]), std::stof(f[5]), std::stof(f[6]));
				p.b0 = std::stoi(f[7]), p.b1 = std::stoi(f[8]);
				p.arm = p.name.find("arm") != std::string::npos;
				p.right = !p.name.empty() && p.name[0] == 'r';
				s_rigs[f[1]].parts.push_back(p);
			}
		}
		int parts = 0;
		for (auto &kv : s_rigs)
			parts += (int)kv.second.parts.size();
		logf("rigs: %d loaded, %d parts", (int)s_rigs.size(), parts);
		return !s_rigs.empty();
	}

	bool available(const std::string &r)
	{
		auto it = s_rigs.find(r);
		if (it == s_rigs.end() || it->second.parts.empty() || !collision::dlc())
			return false; // never "available" with nothing to show (the GTA ped would just vanish)
		for (auto &p : it->second.parts)
			if (!IS_MODEL_VALID(GET_HASH_KEY(p.model.c_str())))
				return false;
		return true;
	}

	float scale(const std::string &r)
	{
		auto it = s_rigs.find(r);
		return it == s_rigs.end() ? 1 / 16.0f : it->second.scale;
	}

	int part_index(const std::string &r, const char *part)
	{
		auto it = s_rigs.find(r);
		if (it == s_rigs.end())
			return -1;
		for (size_t i = 0; i < it->second.parts.size(); i++)
			if (it->second.parts[i].name == part)
				return (int)i;
		return -1;
	}

	void drop(int &obj)
	{
		if (obj && DOES_ENTITY_EXIST(obj))
		{
			SET_ENTITY_AS_MISSION_ENTITY(obj, TRUE, TRUE);
			DELETE_OBJECT(&obj);
		}
		obj = 0;
	}

	int Instance::live() const
	{
		int n = 0;
		for (int o : objs)
			n += o && DOES_ENTITY_EXIST(o);
		return n;
	}

	void Instance::hide()
	{
		for (int &o : objs)
			drop(o);
	}

	static void quat(const V3 &X, const V3 &Y, const V3 &Z, float &x, float &y, float &z, float &w)
	{
		// rotation whose columns are the world directions of the model's x, y and z axes
		float m[3][3] = {{X.x, Y.x, Z.x}, {X.y, Y.y, Z.y}, {X.z, Y.z, Z.z}};
		float tr = m[0][0] + m[1][1] + m[2][2];
		if (tr > 0)
		{
			float s = std::sqrt(tr + 1.0f) * 2;
			w = 0.25f * s, x = (m[2][1] - m[1][2]) / s, y = (m[0][2] - m[2][0]) / s, z = (m[1][0] - m[0][1]) / s;
		}
		else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
		{
			float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2;
			w = (m[2][1] - m[1][2]) / s, x = 0.25f * s, y = (m[0][1] + m[1][0]) / s, z = (m[0][2] + m[2][0]) / s;
		}
		else if (m[1][1] > m[2][2])
		{
			float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2;
			w = (m[0][2] - m[2][0]) / s, x = (m[0][1] + m[1][0]) / s, y = 0.25f * s, z = (m[1][2] + m[2][1]) / s;
		}
		else
		{
			float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2;
			w = (m[1][0] - m[0][1]) / s, x = (m[0][2] + m[2][0]) / s, y = (m[1][2] + m[2][1]) / s, z = 0.25f * s;
		}
	}

	void place(int &obj, Hash &model, const std::string &name, const V3 &pos, const V3 &X, const V3 &Y, const V3 &Z)
	{
		Hash h = GET_HASH_KEY(name.c_str());
		if (obj && (model != h || !DOES_ENTITY_EXIST(obj)))
			drop(obj);
		if (!obj)
		{
			if (!IS_MODEL_VALID(h))
				return;
			if (!HAS_MODEL_LOADED(h))
			{
				REQUEST_MODEL(h);
				return;
			}
			obj = CREATE_OBJECT_NO_OFFSET(h, pos.x, pos.y, pos.z, FALSE, TRUE, FALSE, 0);
			if (!obj)
				return;
			model = h;
			FREEZE_ENTITY_POSITION(obj, TRUE);
			SET_ENTITY_COLLISION(obj, FALSE, FALSE);
			SET_ENTITY_CAN_BE_DAMAGED(obj, FALSE);
		}
		float qx, qy, qz, qw;
		quat(X, Y, Z, qx, qy, qz, qw);
		SET_ENTITY_COORDS_NO_OFFSET(obj, pos.x, pos.y, pos.z, FALSE, FALSE, FALSE);
		SET_ENTITY_QUATERNION(obj, qx, qy, qz, qw);
	}

	// a limb along d (pointing away from its pivot): model z = back up the limb, y = forward-ish, x = right
	static void limb_axes(const V3 &d, const V3 &fwd, V3 &X, V3 &Y, V3 &Z)
	{
		Z = (d * -1.0f).norm();
		Y = fwd - Z * fwd.dot(Z);
		if (Y.len2() < 1e-4f)
			Y = V3(0, 0, 1) - Z * Z.z;
		Y = Y.norm();
		X = Y.cross(Z);
	}

	void pose(Instance &inst, int ped, const Pose &p)
	{
		auto it = s_rigs.find(inst.rig);
		if (it == s_rigs.end())
			return;
		const RigDef &def = it->second;
		inst.objs.resize(def.parts.size(), 0);
		inst.frames.resize(def.parts.size());
		float h = deg2rad(GET_ENTITY_HEADING(ped));
		V3 U(0, 0, 1), F(-std::sin(h), std::cos(h), 0);
		V3 pedPos = GET_ENTITY_COORDS(ped, TRUE);
		if (p.bodyAlong.len2() > 0.5f) // gliding: head first along the flight, chest towards the ground
		{
			U = p.bodyAlong.norm();
			V3 down(0, 0, -1);
			F = down - U * down.dot(U);
			F = F.len2() > 1e-4f ? F.norm() : V3(-std::sin(h), std::cos(h), 0);
		}
		else if (IS_PED_RAGDOLL(ped)) // lying down: follow the spine
		{
			V3 pelvis = GET_PED_BONE_COORDS(ped, 11816, 0, 0, 0), neck = GET_PED_BONE_COORDS(ped, 39317, 0, 0, 0);
			U = (neck - pelvis).norm();
			F = (F - U * F.dot(U)).norm();
		}
		V3 R = F.cross(U);
		// the ped's root sits above its feet by its model's depth below the origin (~1 m for people, less for animals)
		static std::map<Hash, float> rootH;
		Hash mdl = GET_ENTITY_MODEL(ped);
		if (!rootH.count(mdl))
		{
			Vector3 mn{}, mx{};
			GET_MODEL_DIMENSIONS(mdl, &mn, &mx);
			rootH[mdl] = clampf(-mn.z, 0.15f, 1.2f);
		}
		V3 feet = pedPos - U * (IS_PED_HUMAN(ped) ? 0.98f : rootH[mdl]);
		// Minecraft's walk cycle: limbSwing grows with distance moved, limbSwingAmount with speed (to 1 at a run)
		float dtp = inst.lastPose ? std::min(0.1f, (g.now - inst.lastPose) / 1000.0f) : 0;
		inst.lastPose = g.now;
		V3 vel = GET_ENTITY_VELOCITY(ped);
		float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
		inst.walkAmount += (std::min(1.0f, speed / 5.0f) - inst.walkAmount) * std::min(1.0f, dtp * 8);
		inst.walkPhase += 20.0f * inst.walkAmount * dtp;
		float s = def.scale;

		// head direction: given (clamped to 75 degrees either side of the body, like Minecraft) or straight ahead
		V3 look = p.look.len2() > 0.5f ? p.look : F;
		float yawBody = std::atan2(-F.x, F.y), yawLook = std::atan2(-look.x, look.y);
		float dy = clampf(std::remainder(yawLook - yawBody, 2 * PI), deg2rad(-75), deg2rad(75));
		float pitch = clampf(std::asin(clampf(look.z, -1, 1)), deg2rad(-80), deg2rad(80));
		float yh = yawBody + dy;
		V3 Fh(-std::sin(yh) * std::cos(pitch), std::cos(yh) * std::cos(pitch), std::sin(pitch));
		if (p.bodyAlong.len2() > 0.5f) // gliding: the body-relative limit makes no sense lying down
			Fh = look.norm();
		V3 Rh = Fh.cross(V3(0, 0, 1)).norm(), Uh = Rh.cross(Fh);
		float t = g.now / 1000.0f;

		for (size_t i = 0; i < def.parts.size(); i++)
		{
			const PartDef &pd = def.parts[i];
			V3 pos = feet + R * (pd.pivot.x * s) + F * (pd.pivot.y * s) + U * (pd.pivot.z * s);
			V3 X = R, Y = F, Z = U;
			if (pd.kind == HEAD)
				X = Rh, Y = Fh, Z = Uh;
			else if (pd.kind == SWING) // spider legs: forward and back about the vertical, alternately
			{
				float a = std::cos(inst.walkPhase * 0.6662f * 2) * 0.4f * inst.walkAmount * (float)pd.b0;
				X = R * std::cos(a) + F * std::sin(a);
				Y = F * std::cos(a) - R * std::sin(a);
			}
			else if (pd.kind == WALK) // QuadrupedModel: xRot = cos(limbSwing * 0.6662 (+ pi)) * 1.4 * limbSwingAmount
			{
				float a = std::cos(inst.walkPhase * 0.6662f + (pd.b0 ? PI : 0)) * (pd.b1 / 100.0f) * inst.walkAmount;
				V3 d = (U * -std::cos(a) + F * std::sin(a)).norm();
				limb_axes(d, F, X, Y, Z);
			}
			else if (pd.kind == LIMB || pd.kind == FWDARM)
			{
				V3 d;
				if (p.bodyAlong.len2() > 0.5f) // gliding: limbs trail behind, arms a little apart
					d = (U * -1.0f + R * (pd.arm ? (pd.right ? 0.12f : -0.12f) : (pd.right ? 0.03f : -0.03f))).norm();
				else if (pd.kind == FWDARM) // straight out in front, swaying a little
					d = (F + U * (0.08f * std::sin(t * 2.0f + (pd.right ? 0 : 1.7f)))).norm();
				else
					d = (V3(GET_PED_BONE_COORDS(ped, pd.b1, 0, 0, 0)) - V3(GET_PED_BONE_COORDS(ped, pd.b0, 0, 0, 0))).norm();
				bool gliding = p.bodyAlong.len2() > 0.5f;
				if (!gliding && pd.arm && p.aim.len2() > 0.5f)
					d = p.aim;
				else if (!gliding && pd.arm && pd.right && p.rightArm.len2() > 0.5f)
					d = p.rightArm;
				if (pd.arm && p.swingBoth > 0) // raise both arms forward (and up) for a slam
					d = (d * (1 - p.swingBoth) + (F + U * 0.6f).norm() * p.swingBoth).norm();
				limb_axes(d, F, X, Y, Z);
			}
			int &obj = inst.objs[i];
			Hash m = obj && DOES_ENTITY_EXIST(obj) ? GET_ENTITY_MODEL(obj) : 0; // what it is now (place() swaps if needed)
			place(obj, m, pd.model, pos, X, Y, Z);
			inst.frames[i] = {pos, X, Y, Z};
		}
		// diagnostics: what each part looks like right after the first pose and 5 s later
		if (!inst.firstPose)
			inst.firstPose = g.now;
		if (inst.logged < 2 && g.now - inst.firstPose >= (inst.logged ? 5000u : 0u))
		{
			inst.logged++;
			V3 pp = GET_ENTITY_COORDS(ped, TRUE);
			logf("rig %s (ped %d at %.1f %.1f %.1f, visible %d) after %u ms:", inst.rig.c_str(), ped, pp.x, pp.y, pp.z,
			     (int)IS_ENTITY_VISIBLE(ped), g.now - inst.firstPose);
			for (size_t i = 0; i < def.parts.size(); i++)
			{
				Hash h = GET_HASH_KEY(def.parts[i].model.c_str());
				int o = inst.objs[i];
				V3 op = o && DOES_ENTITY_EXIST(o) ? V3(GET_ENTITY_COORDS(o, FALSE)) : V3();
				logf("  %s: valid %d loaded %d obj %d exists %d visible %d alpha %d at %.2f %.2f %.2f", def.parts[i].model.c_str(),
				     (int)IS_MODEL_VALID(h), (int)HAS_MODEL_LOADED(h), o, o ? (int)DOES_ENTITY_EXIST(o) : 0,
				     o && DOES_ENTITY_EXIST(o) ? (int)IS_ENTITY_VISIBLE(o) : 0,
				     o && DOES_ENTITY_EXIST(o) ? GET_ENTITY_ALPHA(o) : -1, op.x, op.y, op.z);
			}
		}
	}
}
