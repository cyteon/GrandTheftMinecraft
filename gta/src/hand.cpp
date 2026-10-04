#include "hand.h"
#include "common.h"
#include "collision.h"
#include "items.h"
#include <algorithm>
#include <string>
#include <cstring>
#include <vector>

namespace hand
{
	// Affine transform in Minecraft view space (x right, y up, z towards the viewer).
	struct Mat
	{
		float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
		V3 t;
		V3 apply(const V3 &p) const
		{
			return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + t.x,
			        m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + t.y,
			        m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + t.z};
		}
		V3 dir(const V3 &p) const
		{
			return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z, m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z,
			        m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z};
		}
		// this = this * o (o applied first, like PoseStack)
		void mul(const Mat &o)
		{
			Mat r;
			for (int i = 0; i < 3; i++)
				for (int j = 0; j < 3; j++)
					r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
			r.t = apply(o.t);
			*this = r;
		}
		void translate(float x, float y, float z)
		{
			Mat o;
			o.t = {x, y, z};
			mul(o);
		}
		void scale(float s)
		{
			Mat o;
			o.m[0][0] = o.m[1][1] = o.m[2][2] = s;
			mul(o);
		}
		void rot(int axis, float deg)
		{
			float c = std::cos(deg2rad(deg)), s = std::sin(deg2rad(deg));
			Mat o;
			int a = (axis + 1) % 3, b = (axis + 2) % 3;
			o.m[a][a] = c, o.m[a][b] = -s, o.m[b][a] = s, o.m[b][b] = c;
			mul(o);
		}
	};

	struct Quad
	{
		V3 p[4]; // view space
		int r, g, b, a;
		float depth;
	};
	static std::vector<Quad> s_quads;

	// Minecraft's in-hand lighting, roughly: lit from above and from the front
	static float light(const V3 &n)
	{
		V3 l1 = V3(0.2f, 1.0f, 0.7f).norm(), l2 = V3(-0.2f, 1.0f, -0.7f).norm();
		float d = std::max(0.0f, n.dot(l1)) * 0.6f + std::max(0.0f, n.dot(l2)) * 0.6f;
		return std::min(1.0f, 0.4f + d);
	}

	// quad given in model space [0,1]^3, normal in model space
	static void add_quad(const Mat &M, const V3 &a, const V3 &b, const V3 &c, const V3 &d, const V3 &nrm, const Rgba &col)
	{
		Quad q;
		q.p[0] = M.apply(a), q.p[1] = M.apply(b), q.p[2] = M.apply(c), q.p[3] = M.apply(d);
		V3 n = M.dir(nrm).norm();
		V3 center = (q.p[0] + q.p[1] + q.p[2] + q.p[3]) * 0.25f;
		if (n.dot(center * -1.0f) <= 0) // back-facing (camera at the origin)
			return;
		float l = light(n) * g.daylight;
		q.r = (int)(col.r * l), q.g = (int)(col.g * l), q.b = (int)(col.b * l), q.a = 255;
		q.depth = center.len2();
		s_quads.push_back(q);
	}

	static void build_block(const Mat &M, const Item &it)
	{
		// faces: up (+y) = top texture, down = bottom, the four sides = side; texture rows run top → bottom
		struct F
		{
			V3 o, u, v, n;
			int tex;
		};
		const F faces[6] = {
			{{0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, F_TOP},     // up
			{{0, 0, 1}, {1, 0, 0}, {0, 0, -1}, {0, -1, 0}, F_BOTTOM}, // down
			{{0, 1, 1}, {1, 0, 0}, {0, -1, 0}, {0, 0, 1}, F_SIDE},   // south (+z)
			{{1, 1, 0}, {-1, 0, 0}, {0, -1, 0}, {0, 0, -1}, F_SIDE}, // north
			{{1, 1, 1}, {0, 0, -1}, {0, -1, 0}, {1, 0, 0}, F_SIDE},  // east
			{{0, 1, 0}, {0, 0, 1}, {0, -1, 0}, {-1, 0, 0}, F_SIDE},  // west
		};
		for (const F &f : faces)
		{
			const auto &grid = it.lod[4][f.tex];
			if (grid.empty())
				continue;
			V3 du = f.u * (1.0f / 16), dv = f.v * (1.0f / 16);
			for (int j = 0; j < 16; j++)
			{
				int i = 0;
				while (i < 16)
				{
					const Rgba &c = grid[j * 16 + i];
					int e = i + 1;
					while (e < 16 && std::memcmp(&grid[j * 16 + e], &c, sizeof(Rgba)) == 0)
						e++;
					if (c.a >= 16)
					{
						V3 p00 = f.o + du * (float)i + dv * (float)j, p10 = f.o + du * (float)e + dv * (float)j;
						add_quad(M, p00, p10, p10 + dv, p00 + dv, f.n, c);
					}
					i = e;
				}
			}
		}
	}

	static void build_sprite(const Mat &M, const Item &it)
	{
		const auto &px = it.lod[4][F_TOP]; // item sprites are stored in the top slot
		if (px.empty())
			return;
		const float z0 = 7.5f / 16, z1 = 8.5f / 16, s = 1.0f / 16;
		auto opaque = [&](int x, int y) { return x >= 0 && y >= 0 && x < 16 && y < 16 && px[y * 16 + x].a >= 128; };
		for (int y = 0; y < 16; y++)
		{
			float top = 1.0f - y * s, bot = top - s;
			int x = 0;
			while (x < 16)
			{
				const Rgba &c = px[y * 16 + x];
				int e = x + 1;
				while (e < 16 && std::memcmp(&px[y * 16 + e], &c, sizeof(Rgba)) == 0)
					e++;
				if (c.a >= 128)
				{
					float l = x * s, r = e * s;
					add_quad(M, {l, bot, z1}, {r, bot, z1}, {r, top, z1}, {l, top, z1}, {0, 0, 1}, c);  // front
					add_quad(M, {r, bot, z0}, {l, bot, z0}, {l, top, z0}, {r, top, z0}, {0, 0, -1}, c); // back
				}
				x = e;
			}
			// edges: a side face wherever an opaque pixel borders a transparent one
			for (int xx = 0; xx < 16; xx++)
			{
				if (!opaque(xx, y))
					continue;
				const Rgba &c = px[y * 16 + xx];
				float l = xx * s, r = l + s;
				if (!opaque(xx, y - 1))
					add_quad(M, {l, top, z1}, {r, top, z1}, {r, top, z0}, {l, top, z0}, {0, 1, 0}, c);
				if (!opaque(xx, y + 1))
					add_quad(M, {l, bot, z0}, {r, bot, z0}, {r, bot, z1}, {l, bot, z1}, {0, -1, 0}, c);
				if (!opaque(xx - 1, y))
					add_quad(M, {l, bot, z0}, {l, bot, z1}, {l, top, z1}, {l, top, z0}, {-1, 0, 0}, c);
				if (!opaque(xx + 1, y))
					add_quad(M, {r, bot, z1}, {r, bot, z0}, {r, top, z0}, {r, top, z1}, {1, 0, 0}, c);
			}
		}
	}

	std::string sprite_for(int itemId, Use use, float progress)
	{
		const Item &it = item(itemId);
		if (it.name == "bow" && use == USE_BOW) // Minecraft's "pull" thresholds (pull = seconds drawn)
			return progress >= 0.9f ? "bow_pulling_2" : progress >= 0.65f ? "bow_pulling_1" : "bow_pulling_0";
		if (it.name == "crossbow")
		{
			if (use == USE_CROSSBOW_LOADED)
				return "crossbow_arrow";
			if (use == USE_CROSSBOW_LOAD)
				return progress >= 1.0f ? "crossbow_pulling_2" : progress >= 0.58f ? "crossbow_pulling_1" : "crossbow_pulling_0";
			return "crossbow_standby";
		}
		return it.name;
	}

	// ---- the real prop (Stage 2) ----
	static int s_obj = 0;
	static Hash s_objModel = 0;

	void hide()
	{
		if (s_obj && DOES_ENTITY_EXIST(s_obj))
		{
			SET_ENTITY_AS_MISSION_ENTITY(s_obj, TRUE, TRUE);
			DELETE_OBJECT(&s_obj);
		}
		s_obj = 0;
		s_objModel = 0;
	}

	static void quat_from(const float m[3][3], float &x, float &y, float &z, float &w)
	{
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

	static const float HAND_SIZE = 0.25f; // size of the _h / gtm_i_ models (tools/make_dlc_src.py)

	// true if the prop was placed (else the caller draws polygons)
	static bool draw_prop(const Item &it, const std::string &sprite, const Mat &Mc, const V3 &cam, const V3 &R,
	                      const V3 &U, const V3 &F)
	{
		if (!collision::dlc()) // block pack missing or its textures not filled yet
			return false;
		std::string name = it.block ? "gtm_" + it.name + "_h" : "gtm_i_" + sprite;
		Hash model = GET_HASH_KEY(name.c_str());
		if (!IS_MODEL_VALID(model))
			return false;
		if (!HAS_MODEL_LOADED(model))
		{
			REQUEST_MODEL(model);
			return false;
		}
		// Mc maps the centred unit model into view space with a uniform scale s; our model is HAND_SIZE big, so
		// scale the whole placement about the eye by HAND_SIZE / s: same picture, no entity scaling needed
		V3 c0(Mc.m[0][0], Mc.m[1][0], Mc.m[2][0]);
		float s = c0.len();
		if (s < 1e-5f)
			return false;
		float lambda = HAND_SIZE / s;
		V3 t = Mc.t * lambda;
		V3 pos = cam + R * t.x + U * t.y - F * t.z;
		// world rotation = [R U -F] * (Mc / s)
		const V3 B[3] = {R, U, F * -1.0f};
		float W[3][3];
		for (int col = 0; col < 3; col++)
		{
			V3 vcol(Mc.m[0][col] / s, Mc.m[1][col] / s, Mc.m[2][col] / s);
			V3 wcol = B[0] * vcol.x + B[1] * vcol.y + B[2] * vcol.z;
			W[0][col] = wcol.x, W[1][col] = wcol.y, W[2][col] = wcol.z;
		}
		if (s_obj && (s_objModel != model || !DOES_ENTITY_EXIST(s_obj)))
			hide();
		if (!s_obj)
		{
			s_obj = CREATE_OBJECT_NO_OFFSET(model, pos.x, pos.y, pos.z, FALSE, TRUE, FALSE, 0);
			if (!s_obj)
				return false;
			s_objModel = model;
			FREEZE_ENTITY_POSITION(s_obj, TRUE);
			SET_ENTITY_COLLISION(s_obj, FALSE, FALSE);
			SET_ENTITY_CAN_BE_DAMAGED(s_obj, FALSE);
		}
		float qx, qy, qz, qw;
		quat_from(W, qx, qy, qz, qw);
		SET_ENTITY_COORDS_NO_OFFSET(s_obj, pos.x, pos.y, pos.z, FALSE, FALSE, FALSE);
		SET_ENTITY_QUATERNION(s_obj, qx, qy, qz, qw);
		return true;
	}

	void draw(int itemId, float swing, float equip, Use use, float progress)
	{
		if (itemId < 0)
		{
			hide();
			return;
		}
		const Item &it = item(itemId);
		// the camera being prepared this frame (the final-rendered one lags a frame and makes the hand swim)
		V3 cam = GET_GAMEPLAY_CAM_COORD();
		V3 rot = GET_GAMEPLAY_CAM_ROT(2);
		float fov = GET_GAMEPLAY_CAM_FOV();
		V3 F = rot_to_dir(rot);
		V3 R = F.cross(V3(0, 0, 1)).norm();
		V3 U = R.cross(F);
		// Minecraft lays the hand out for a 70 degree FOV; scale x/y so it covers the same part of GTA's screen
		float k = std::tan(deg2rad(fov * 0.5f)) / std::tan(deg2rad(35.0f));

		Mat M;
		float sq = std::sqrt(swing);
		if (use == USE_BOW || use == USE_CROSSBOW_LOAD)
		{
			// ItemInHandRenderer: drawing a bow / loading a crossbow (right hand)
			bool bow = use == USE_BOW;
			M.translate(0.56f * k, (-0.52f - (1.0f - equip) * 0.6f) * k, -0.72f);
			if (bow)
				M.translate(-0.2785682f * k, 0.18344387f * k, 0.15731531f);
			else
				M.translate(-0.4785682f * k, -0.094387f * k, 0.05731531f);
			M.rot(0, bow ? -13.935f : -11.935f);
			M.rot(1, bow ? 35.3f : 65.3f);
			M.rot(2, -9.785f);
			float f;
			if (bow)
			{
				f = progress; // seconds drawn
				f = (f * f + f * 2.0f) / 3.0f;
			}
			else
				f = progress;
			f = std::min(f, 1.0f);
			if (f > 0.1f) // the trembling pull
				M.translate(0, std::sin((progress * 20.0f - 0.1f) * 1.3f) * (f - 0.1f) * 0.004f * k, 0);
			M.translate(0, 0, f * 0.04f);
			M.rot(1, -45.0f);
			M.scale(k);
		}
		else
		{
			// ItemInHandRenderer.renderArmWithItem: swing offset, arm transform, attack transform
			M.translate(-0.4f * std::sin(sq * PI) * k, 0.2f * std::sin(sq * PI * 2) * k, -0.2f * std::sin(swing * PI));
			M.translate(0.56f * k, (-0.52f - (1.0f - equip) * 0.6f) * k, -0.72f);
			if (use == USE_CROSSBOW_LOADED)
			{
				M.translate(-0.641864f * k, 0, 0);
				M.rot(1, 10.0f);
			}
			float f = std::sin(swing * swing * PI), f1 = std::sin(sq * PI);
			M.rot(1, 45.0f + f * -20.0f);
			M.rot(2, f1 * -20.0f);
			M.rot(0, f1 * -80.0f);
			M.rot(1, -45.0f);
			M.scale(k);
		}
		// the model's firstperson_righthand display transform
		if (it.block)
		{
			M.rot(1, 45.0f);
			M.scale(0.40f);
		}
		else if (it.name == "crossbow")
		{
			M.translate(1.13f / 16, 3.2f / 16, 1.13f / 16);
			M.rot(0, -90.0f);
			M.rot(1, 0.0f);
			M.rot(2, -55.0f);
			M.scale(0.68f);
		}
		else
		{
			M.translate(1.13f / 16, 3.2f / 16, 1.13f / 16);
			M.rot(1, -90.0f);
			M.rot(2, 25.0f);
			M.scale(0.68f);
		}
		if (draw_prop(it, sprite_for(itemId, use, progress), M, cam, R, U, F))
			return;
		hide();
		M.translate(-0.5f, -0.5f, -0.5f);

		s_quads.clear();
		if (it.block)
			build_block(M, it);
		else
			build_sprite(M, it);
		// DRAW_POLY doesn't depth-test against itself: painter's order, far to near
		std::sort(s_quads.begin(), s_quads.end(), [](const Quad &a, const Quad &b) { return a.depth > b.depth; });
		for (const Quad &q : s_quads)
		{
			V3 w[4];
			for (int i = 0; i < 4; i++)
				w[i] = cam + R * q.p[i].x + U * q.p[i].y - F * q.p[i].z;
			DRAW_POLY(w[0].x, w[0].y, w[0].z, w[1].x, w[1].y, w[1].z, w[2].x, w[2].y, w[2].z, q.r, q.g, q.b, q.a);
			DRAW_POLY(w[0].x, w[0].y, w[0].z, w[2].x, w[2].y, w[2].z, w[3].x, w[3].y, w[3].z, q.r, q.g, q.b, q.a);
		}
	}
}
