// GrandTheftMinecraft: Minecraft creative mode inside GTA V story mode (ScriptHookV ASI).
#include "audio.h"
#include "blockrender.h"
#include "collision.h"
#include "common.h"
#include "config.h"
#include "flight.h"
#include "fx.h"
#include "gui.h"
#include "hand.h"
#include "input.h"
#include "interact.h"
#include "items.h"
#include "log.h"
#include "dlcpatch.h"
#include "mcassets.h"
#include "version.h"
#include "render2d.h"
#include "world.h"
#include <cstdio>
#include <string>

Frame g;
bool g_mcMode = true;
bool g_debug = false;

static HMODULE s_module;
static bool s_calib = false;
static int s_stress = 0; // F11: DRAW_POLY stress test (triangles per frame), cycles 0 → 2k → 5k → 10k → 20k → 0
static bool s_worldReady = false;
static float s_fps = 60.0f;
static bool s_inited = false, s_online = false, s_setupNotified = false;
static bool s_pedHidden = false;

static void notify(const char *text)
{
	BEGIN_TEXT_COMMAND_THEFEED_POST("STRING");
	ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(text);
	END_TEXT_COMMAND_THEFEED_POST_TICKER(FALSE, FALSE);
}

static void update_frame()
{
	int w = 0, h = 0;
	GET_ACTUAL_SCREEN_RESOLUTION(&w, &h);
	if (w > 0 && h > 0)
		g.screenW = w, g.screenH = h;
	if (g_cfg.guiScale > 0)
		g.gui = g_cfg.guiScale;
	else
	{
		// Minecraft's auto scale: the largest that keeps at least 320x240 GUI pixels
		int s = 1;
		while (g.screenW / (s + 1) >= 320 && g.screenH / (s + 1) >= 240)
			s++;
		g.gui = s;
	}
	g.dt = GET_FRAME_TIME();
	if (g.dt <= 0 || g.dt > 0.5f)
		g.dt = 0.016f;
	s_fps += (1.0f / g.dt - s_fps) * 0.05f;
	g.now = (uint32_t)GET_GAME_TIMER();
	g.player = PLAYER_ID();
	g.ped = PLAYER_PED_ID();
	g.inVehicle = IS_PED_IN_ANY_VEHICLE(g.ped, FALSE);
	g.pedPos = GET_ENTITY_COORDS(g.ped, TRUE);
	g.camPos = GET_FINAL_RENDERED_CAM_COORD();
	g.camRot = GET_FINAL_RENDERED_CAM_ROT(2);
	g.camDir = rot_to_dir(g.camRot);
	g.camFov = GET_FINAL_RENDERED_CAM_FOV();
	// DRAW_POLY is unlit: dim it at night (full 7:00-19:00, 35% 21:00-5:00)
	float hr = GET_CLOCK_HOURS() + GET_CLOCK_MINUTES() / 60.0f;
	float day;
	if (hr >= 7 && hr <= 19)
		day = 1.0f;
	else if (hr >= 21 || hr <= 5)
		day = 0.35f;
	else if (hr < 7)
		day = 0.35f + 0.65f * (hr - 5) / 2.0f;
	else
		day = 1.0f - 0.65f * (hr - 19) / 2.0f;
	g.daylight = day;
}

static void disable_gta_controls()
{
	static const int CTRL[] = {24, 25, 257, 140, 141, 142, 143, 263, 264, // attack / aim / melee
	                           14, 15, 16, 17, 37, 261, 262,              // weapon wheel / next / prev
	                           157, 158, 159, 160, 161, 162, 163, 164, 165, // weapon slots 1-9
	                           44, 45, 47, 58,                             // cover, reload, detonate, throw
	                           51, 38, 46, 54, 241, 242};                  // E (context / pickup / talk), wheel
	for (int c : CTRL)
		DISABLE_CONTROL_ACTION(0, c, TRUE);
}

static void stress_test()
{
	if (!s_stress)
		return;
	// a flat sheet of tiny triangles 4 m in front of the player
	V3 base = g.pedPos + V3(g.camDir.x, g.camDir.y, 0).norm() * 4.0f;
	int side = (int)std::sqrt((float)s_stress / 2.0f);
	float cell = 3.0f / side;
	for (int j = 0; j < side; j++)
		for (int i = 0; i < side; i++)
		{
			V3 p(base.x - 1.5f + i * cell, base.y, base.z - 0.5f + j * cell);
			int r = (i * 7) & 255, gg = (j * 5) & 255;
			DRAW_POLY(p.x, p.y, p.z, p.x + cell, p.y, p.z, p.x + cell, p.y, p.z + cell, r, gg, 128, 255);
			DRAW_POLY(p.x, p.y, p.z, p.x + cell, p.y, p.z + cell, p.x, p.y, p.z + cell, r, gg, 128, 255);
		}
}

static void calibration()
{
	if (!s_calib)
		return;
	int t = r2d::tex("gui/calib.png");
	// 16x16 checker (pre-scaled x4 = 64 px) drawn at 64 px and 256 px: both must look square and crisp
	r2d::draw(t, 100, 100, 64, 64, 0xFFFFFFFF, r2d::L_DEBUG);
	r2d::draw(t, 200, 100, 256, 256, 0xFFFFFFFF, r2d::L_DEBUG);
	r2d::rect(100, 400, 200, 2, 0xFFFF0000, r2d::L_DEBUG); // 200x2 px red line
	char buf[96];
	std::snprintf(buf, sizeof buf, "calib %dx%d gui %d", g.screenW, g.screenH, g.gui);
	r2d::text(100, 420, buf, 0xFFFFFF, true, r2d::L_DEBUG);
}

static std::string debug_text()
{
	char buf[1024];
	V3 sz = collision::model_size();
	std::snprintf(buf, sizeof buf,
	              "GrandTheftMinecraft " GTM_VERSION "  %.0f fps\n"
	              "Mode: %s%s%s\n"
	              "XYZ: %.2f / %.2f / %.2f\n"
	              "Blocks: %d in %d builds, drawn %d, faces %d, polys %d / %d\n"
	              "Collision props: %d / %d (%s %.2fx%.2fx%.2f)\n"
	              "Pearls %d  arrows %d  TNT %d  particles %d\n"
	              "%s\n"
	              "Screen %dx%d gui %d fov %.1f  stress %d",
	              s_fps, g_mcMode ? "Minecraft" : "GTA", flight::active() ? " (flying)" : "",
	              g.inVehicle ? " (vehicle)" : "", g.pedPos.x, g.pedPos.y, g.pedPos.z, (int)g_blocks.size(),
	              (int)g_builds.size(), blockrender::blocksDrawn, blockrender::facesThisFrame,
	              blockrender::polysThisFrame, g_cfg.polyBudget, collision::count(), g_cfg.maxCollisionProps,
	              collision::model_name(), sz.x, sz.y, sz.z, fx::pearl_count(), fx::arrow_count(), fx::tnt_count(), fx::particle_count(),
	              interact::describe().c_str(), g.screenW, g.screenH, g.gui, g.camFov, s_stress);
	return buf;
}

// GTA's own text (the Minecraft font isn't built until setup has run)
static void status_text(const std::string &msg, float y)
{
	SET_TEXT_FONT(0);
	SET_TEXT_SCALE(0.0f, 0.38f);
	SET_TEXT_COLOUR(255, 255, 255, 230);
	SET_TEXT_OUTLINE();
	BEGIN_TEXT_COMMAND_DISPLAY_TEXT("STRING");
	ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(("GrandTheftMinecraft: " + msg).c_str());
	END_TEXT_COMMAND_DISPLAY_TEXT(0.012f, y, 0);
}

// Never anything in GTA Online: if a session starts, everything Minecraft goes away until story mode.
static bool online_guard()
{
	bool online = NETWORK_IS_SESSION_STARTED() || NETWORK_IS_GAME_IN_PROGRESS();
	if (online && !s_online)
	{
		logf("GTA Online session detected: GrandTheftMinecraft disabled");
		collision::clear();
		hand::hide();
		flight::stop();
	}
	s_online = online;
	return online;
}

static void tick()
{
	if (online_guard())
		return;
	update_frame();
	input::begin_frame();
	r2d::begin_frame();

	bool playing = !IS_PAUSE_MENU_ACTIVE() && !IS_SCREEN_FADED_OUT() && !IS_CUTSCENE_ACTIVE() &&
	               !IS_PLAYER_SWITCH_IN_PROGRESS();
	if (!s_worldReady && playing && DOES_ENTITY_EXIST(g.ped))
	{
		collision::init();
		if (collision::dlc())
			fx::preload();
		s_worldReady = true;
		if (g_mcMode)
			SET_FOLLOW_PED_CAM_VIEW_MODE(4);
	}

	if (input::pressed(g_cfg.toggleKey))
	{
		g_mcMode = !g_mcMode;
		flight::stop();
		g_invOpen = false;
		if (g_mcMode)
			SET_FOLLOW_PED_CAM_VIEW_MODE(4);
		notify(g_mcMode ? "~g~Minecraft mode ON~s~ (F6)" : "~r~Minecraft mode OFF~s~ (F6)");
		logf("mode %s", g_mcMode ? "on" : "off");
	}
	if (input::pressed(g_cfg.debugKey))
		g_debug = !g_debug;
	if (input::pressed(VK_F10))
		s_calib = !s_calib;
	if (input::pressed(VK_F11))
	{
		static const int steps[] = {0, 2000, 5000, 10000, 20000};
		int i = 0;
		while (i < 5 && steps[i] != s_stress)
			i++;
		s_stress = steps[(i + 1) % 5];
		logf("stress test: %d triangles", s_stress);
	}

	bool alive = !IS_ENTITY_DEAD(g.ped, FALSE);
	bool onFoot = g_mcMode && alive && !g.inVehicle && playing;
	if (g_mcMode && alive)
	{
		if (g_cfg.invincible)
			SET_PLAYER_INVINCIBLE(g.player, TRUE);
		if (g_cfg.noWanted)
		{
			SET_MAX_WANTED_LEVEL(0);
			CLEAR_PLAYER_WANTED_LEVEL(g.player);
		}
	}
	if (onFoot)
	{
		HIDE_HUD_AND_RADAR_THIS_FRAME();
		INVALIDATE_IDLE_CAM();
		disable_gta_controls();
		SET_CURRENT_PED_WEAPON(g.ped, 0xA2719263 /* unarmed */, TRUE);
		// Minecraft has no body in first person: hide GTA's arms (door reaches, phone calls...)
		bool fp = GET_FOLLOW_PED_CAM_VIEW_MODE() == 4;
		if (fp != s_pedHidden)
		{
			SET_ENTITY_VISIBLE(g.ped, !fp, FALSE);
			s_pedHidden = fp;
		}
		if (fp)
			SET_ENTITY_LOCALLY_INVISIBLE(g.ped);
		if (input::pressed('E'))
		{
			g_invOpen = !g_invOpen;
			if (!g_invOpen)
				gui::close_inventory_reset();
		}
		if (g_invOpen && input::pressed(VK_ESCAPE))
		{
			g_invOpen = false;
			gui::close_inventory_reset();
		}
	}
	else if (s_pedHidden)
	{
		SET_ENTITY_VISIBLE(g.ped, TRUE, FALSE);
		s_pedHidden = false;
	}
	if (!onFoot && g_invOpen)
	{
		g_invOpen = false;
		gui::close_inventory_reset();
	}

	if (g_mcMode && playing && !g_invOpen && alive)
	{
		// hotbar: 1-9 and the mouse wheel (also in vehicles; it's only the GUI)
		for (int k = 0; k < 9; k++)
			if (input::pressed('1' + k))
				gui::select(k);
		if (onFoot)
		{
			if (IS_DISABLED_CONTROL_JUST_PRESSED(0, 14) || IS_DISABLED_CONTROL_JUST_PRESSED(0, 242))
				gui::select(g_sel + 1);
			else if (IS_DISABLED_CONTROL_JUST_PRESSED(0, 15) || IS_DISABLED_CONTROL_JUST_PRESSED(0, 241))
				gui::select(g_sel - 1);
		}
	}

	if (g_invOpen)
	{
		DISABLE_ALL_CONTROL_ACTIONS(0);
		DISABLE_CONTROL_ACTION(2, 199, TRUE);
		DISABLE_CONTROL_ACTION(2, 200, TRUE);
		SET_MOUSE_CURSOR_THIS_FRAME();
		gui::update_inventory();
	}

	blockrender::draw_blocks();
	if (onFoot)
	{
		interact::update(!g_invOpen);
		flight::update(!g_invOpen);
	}
	else
	{
		interact::g_target = interact::Target{};
		flight::stop();
	}
	fx::update();
	stress_test();
	if (s_worldReady)
		collision::update();
	audio::update();

	if (!(g_mcMode && playing))
		hand::hide();
	if (g_mcMode && playing)
	{
		gui::draw_hud(onFoot);
		if (g_invOpen)
			gui::draw_inventory();
	}
	if (g_debug)
		gui::draw_debug(debug_text());
	calibration();
	world_save_if_dirty();
}

static void init_all()
{
	if (!r2d::init())
		logf("GUI textures missing in %s", g_dataDir.c_str());
	if (!items_load())
		logf("no items.txt: setup hasn't finished");
	gui::init();
	fx::init();
	world_load();
	audio::init();
	s_inited = true;
	logf("ready: %d items, mode %s", (int)g_items.size(), g_mcMode ? "on" : "off");
}

static void script_main()
{
	logf("script start (GrandTheftMinecraft " GTM_VERSION ")");
	g_mcMode = g_cfg.startEnabled;
	if (mcassets::data_ready())
		init_all();
	mcassets::start_if_needed();
	while (true)
	{
		if (!s_inited)
		{
			if (mcassets::data_ready())
				init_all();
			else if (!online_guard())
				status_text(mcassets::status(), 0.012f); // first launch: building from Minecraft
		}
		if (s_inited)
		{
			tick();
			if (mcassets::running() && !s_online)
				status_text(mcassets::status(), 0.012f);
		}
		if (mcassets::finished() && !s_setupNotified)
		{
			s_setupNotified = true;
			std::string msg = "~g~GrandTheftMinecraft~s~: " + mcassets::status();
			notify(msg.c_str());
		}
		if (mcassets::failed() && !s_setupNotified)
		{
			s_setupNotified = true;
			std::string msg = "~r~GrandTheftMinecraft~s~: " + mcassets::status();
			notify(msg.c_str());
		}
		WAIT(0);
	}
}

static void on_keyboard(DWORD key, WORD, BYTE, BOOL, BOOL, BOOL wasDownBefore, BOOL isUpNow)
{
	if (isUpNow)
		input::on_key(key, false);
	else if (!wasDownBefore)
		input::on_key(key, true);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		s_module = module;
		char path[MAX_PATH];
		GetModuleFileNameA(module, path, MAX_PATH);
		std::string dir(path);
		dir = dir.substr(0, dir.find_last_of("\\/") + 1);
		g_dataDir = dir + "GrandTheftMinecraft\\";
		CreateDirectoryA(g_dataDir.c_str(), nullptr);
		log_open(g_dataDir + "gtm.log");
		logf("GrandTheftMinecraft " GTM_VERSION " loaded from %s", path);
		config_load();
		// before GTA mounts DLC packs: fill the block pack with the textures setup built last session
		if (dlcpatch::apply_pending())
			logf("block pack textured");
		// first launch: start building right away (a thread; it only runs once DllMain returns) so the block pack
		// can be filled before GTA loads it, a few seconds from now
		mcassets::start_if_needed();
		if (!shv::load())
		{
			logf("ScriptHookV binding failed; not starting");
			return TRUE;
		}
		shv::scriptRegister(module, script_main);
		shv::keyboardHandlerRegister(on_keyboard);
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		if (shv::scriptUnregister)
		{
			shv::scriptUnregister(module);
			shv::keyboardHandlerUnregister(on_keyboard);
		}
	}
	return TRUE;
}
