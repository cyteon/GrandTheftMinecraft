// Shared math and per-frame game state.
#pragma once
#include "natives_gen.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>

// Natives by bare name (qualify the rare name that exists in two namespaces).
using namespace APPS; using namespace AUDIO; using namespace BRAIN; using namespace CAMERA; using namespace CLOCK; using namespace CUTSCENE; using namespace DATAFILE; using namespace DECORATOR; using namespace DLC; using namespace ENTITY; using namespace EVENT; using namespace EXTRAMETADATA; using namespace FIRE; using namespace GRAPHICS; using namespace GTA; using namespace HUD; using namespace INTERIOR; using namespace ITEMSETS; using namespace LOBBY; using namespace LOCALIZATION; using namespace MISC; using namespace MONEY; using namespace NETSHOPPING; using namespace NETWORK; using namespace OBJECT; using namespace PAD; using namespace PATH; using namespace PED; using namespace PHYSICS; using namespace PLAYER; using namespace RECORDING; using namespace REPLAY; using namespace SAVEMIGRATION; using namespace SCRIPT; using namespace SECURITY; using namespace SHAPETEST; using namespace SOCIALCLUB; using namespace STATS; using namespace STREAMING; using namespace TASK; using namespace VEHICLE; using namespace WATER; using namespace WEAPON; using namespace ZONE;

struct V3
{
	float x = 0, y = 0, z = 0;
	V3() = default;
	V3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
	V3(const Vector3 &v) : x(v.x), y(v.y), z(v.z) {}
	V3 operator+(const V3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
	V3 operator-(const V3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
	V3 operator*(float s) const { return {x * s, y * s, z * s}; }
	V3 &operator+=(const V3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
	V3 &operator-=(const V3 &o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
	V3 &operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
	float dot(const V3 &o) const { return x * o.x + y * o.y + z * o.z; }
	V3 cross(const V3 &o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
	float len() const { return std::sqrt(dot(*this)); }
	float len2() const { return dot(*this); }
	V3 norm() const { float l = len(); return l > 1e-6f ? *this * (1.0f / l) : V3{}; }
};

constexpr float PI = 3.14159265358979f;
inline float deg2rad(float d) { return d * PI / 180.0f; }
inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
inline float frand() { return (float)rand() / (float)RAND_MAX; }
inline float frand(float a, float b) { return a + (b - a) * frand(); }

// GTA rotation (order 2) → unit forward vector.
inline V3 rot_to_dir(const V3 &rot)
{
	float p = deg2rad(rot.x), h = deg2rad(rot.z);
	return {-std::sin(h) * std::cos(p), std::cos(h) * std::cos(p), std::sin(p)};
}

struct Frame
{
	int screenW = 1920, screenH = 1080;
	int gui = 4;         // Minecraft GUI scale
	float dt = 0.016f;   // seconds
	uint32_t now = 0;    // ms, GetTickCount-like (game timer)
	int ped = 0, player = 0;
	bool inVehicle = false;
	V3 camPos, camRot, camDir;
	float camFov = 50.0f;
	V3 pedPos;
	float daylight = 1.0f; // multiplier for unlit DRAW_POLY colours
};
extern Frame g;
extern bool g_mcMode;   // Minecraft mode on (F6)
extern bool g_debug;    // F9 overlay
