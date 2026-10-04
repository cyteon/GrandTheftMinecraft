#include "gui.h"
#include "hand.h"
#include "interact.h"
#include "common.h"
#include "input.h"
#include "items.h"
#include "audio.h"
#include "render2d.h"
#include <algorithm>
#include <vector>

Slot g_hotbar[9];
int g_sel = 0;
bool g_invOpen = false;

namespace gui
{
	using namespace r2d;

	static int t_cursor, t_hotbar, t_sel, t_cross, t_tabItems, t_scroller, t_scrollerOff;
	static int t_tabSel[8], t_tabUn[8];
	static std::string s_nameText;
	static uint32_t s_nameUntil = 0;
	static float s_swing = 1.0f;  // 0..1 progress, 1 = idle
	static float s_equip = 1.0f;  // 0 = lowered, 1 = raised
	static int s_handItem = -1;

	struct Tab
	{
		const char *key, *title, *icon;
	};
	static const Tab TABS[] = {
		{"building", "Building Blocks", "bricks"},
		{"colored", "Colored Blocks", "cyan_wool"},
		{"redstone", "Redstone Blocks", "tnt"},
		{"combat", "Combat", "diamond_sword"},
		{"tools", "Tools & Utilities", "flint_and_steel"},
		{"spawn", "Spawn Eggs", "creeper_spawn_egg"},
	};
	static const int NTABS = sizeof(TABS) / sizeof(TABS[0]);
	static int s_tab = 0;
	static int s_scroll = 0;
	static Slot s_carried;
	static std::vector<int> s_tabItems;

	static void stack_of(Slot &s, int it)
	{
		s.item = it;
		s.count = it >= 0 ? item(it).maxStack : 0;
	}

	void init()
	{
		t_cursor = tex("gui/cursor.png");
		t_hotbar = tex("gui/hotbar.png");
		t_sel = tex("gui/hotbar_selection.png");
		t_cross = tex("gui/crosshair.png");
		t_tabItems = tex("gui/tab_items.png");
		t_scroller = tex("gui/scroller.png");
		t_scrollerOff = tex("gui/scroller_disabled.png");
		for (int i = 1; i <= 7; i++)
		{
			t_tabSel[i] = tex("gui/tab_top_selected_" + std::to_string(i) + ".png");
			t_tabUn[i] = tex("gui/tab_top_unselected_" + std::to_string(i) + ".png");
		}
		const char *defaults[9] = {"grass_block", "dirt", "stone", "oak_planks", "glass",
		                           "tnt", "flint_and_steel", "ender_pearl", "diamond_sword"};
		for (int i = 0; i < 9; i++)
			stack_of(g_hotbar[i], item_find(defaults[i]));
		select(0);
	}

	void toast(const std::string &msg)
	{
		s_nameText = msg;
		s_nameUntil = g.now + 2000;
	}

	void select(int slot)
	{
		g_sel = (slot % 9 + 9) % 9;
		const Slot &s = g_hotbar[g_sel];
		if (!s.empty())
			toast(item(s.item).display);
		else
			s_nameUntil = 0;
	}

	void set_selected_item(int it)
	{
		for (int i = 0; i < 9; i++)
			if (g_hotbar[i].item == it)
			{
				select(i);
				return;
			}
		stack_of(g_hotbar[g_sel], it);
		select(g_sel);
	}

	void swing() { s_swing = 0.0f; }
	float swing_progress() { return s_swing; }

	static void draw_item(const Slot &s, float x, float y, float sz, int level)
	{
		if (s.empty())
			return;
		draw(item(s.item).icon, x, y, sz, sz, 0xFFFFFFFF, level);
		if (s.count > 1)
		{
			std::string n = std::to_string(s.count);
			float sc = sz / 16.0f;
			float w = text_width(n, sc);
			text(x + 17 * sc - w, y + 9 * sc, n, 0xFFFFFF, true, level + 2, sc);
		}
	}

	static void draw_hand()
	{
		const Slot &s = g_hotbar[g_sel];
		int it = s.empty() ? -1 : s.item;
		if (it != s_handItem)
		{
			s_handItem = it;
			s_equip = 0.0f;
		}
		s_equip = std::min(1.0f, s_equip + g.dt * 6.0f);
		s_swing = std::min(1.0f, s_swing + g.dt / 0.3f);
		if (it < 0)
		{
			hand::hide();
			return;
		}
		float progress = 0;
		int use = interact::hand_use(progress);
		hand::draw(it, s_swing, s_equip, (hand::Use)use, progress);
	}

	void draw_hud(bool interactive)
	{
		float s = (float)g.gui, W = (float)g.screenW, H = (float)g.screenH;
		if (interactive && !g_invOpen && GET_FOLLOW_PED_CAM_VIEW_MODE() == 4)
			draw_hand();
		else
			hand::hide();
		// crosshair
		if (interactive && !g_invOpen)
			draw(t_cross, std::floor(W / 2 - 7.5f * s), std::floor(H / 2 - 7.5f * s), 15 * s, 15 * s, 0xE6FFFFFF,
			     L_HUD);
		// hotbar
		float hx = std::floor(W / 2 - 91 * s), hy = H - 22 * s;
		draw(t_hotbar, hx, hy, 182 * s, 22 * s, 0xFFFFFFFF, L_HUD);
		draw(t_sel, hx - 1 * s + g_sel * 20 * s, hy - 1 * s, 24 * s, 23 * s, 0xFFFFFFFF, L_HUD + 1);
		for (int i = 0; i < 9; i++)
			draw_item(g_hotbar[i], hx + (3 + i * 20) * s, hy + 3 * s, 16 * s, L_HUD + 2);
		// item name (creative: 45 GUI px from the bottom), fading out over the last 0.5 s
		if (g.now < s_nameUntil && !g_invOpen)
		{
			float left = (s_nameUntil - g.now) / 500.0f;
			float w = text_width(s_nameText);
			text(std::floor(W / 2 - w / 2), H - 45 * s, s_nameText, 0xFFFFFF, true, L_HUD_TOP, 0, std::min(1.0f, left));
		}
	}

	// ---- creative inventory ----
	static void rebuild_tab()
	{
		s_tabItems.clear();
		for (int i = 0; i < (int)g_items.size(); i++)
			if (g_items[i].tab == TABS[s_tab].key)
				s_tabItems.push_back(i);
		s_scroll = 0;
	}

	static int max_scroll() { return std::max(0, ((int)s_tabItems.size() + 8) / 9 - 5); }

	struct Layout
	{
		float s, left, top;
	};
	static Layout layout()
	{
		Layout l;
		l.s = (float)g.gui;
		l.left = std::floor(g.screenW / 2 - 195 * l.s / 2);
		l.top = std::floor(g.screenH / 2 - 136 * l.s / 2);
		return l;
	}

	static bool in(float mx, float my, float x, float y, float w, float h) { return mx >= x && my >= y && mx < x + w && my < y + h; }

	// hovered: grid index (0..44) → 100 + n, hotbar slot → n (0..8), tab → 200 + t, else -1
	static int hover(const Layout &l, float mx, float my)
	{
		float s = l.s;
		for (int r = 0; r < 5; r++)
			for (int c = 0; c < 9; c++)
				if (in(mx, my, l.left + (9 + c * 18 - 1) * s, l.top + (18 + r * 18 - 1) * s, 18 * s, 18 * s))
					return 100 + r * 9 + c;
		for (int c = 0; c < 9; c++)
			if (in(mx, my, l.left + (9 + c * 18 - 1) * s, l.top + (112 - 1) * s, 18 * s, 18 * s))
				return c;
		for (int t = 0; t < NTABS; t++)
			if (in(mx, my, l.left + t * 27 * s, l.top - 28 * s, 26 * s, 30 * s))
				return 200 + t;
		return -1;
	}

	static int grid_item(int h)
	{
		int idx = (h - 100) + s_scroll * 9;
		return idx >= 0 && idx < (int)s_tabItems.size() ? s_tabItems[idx] : -1;
	}

	static float s_mx, s_my;

	void update_inventory()
	{
		if (s_tabItems.empty())
			rebuild_tab();
		s_mx = GET_DISABLED_CONTROL_NORMAL(0, 239) * g.screenW;
		s_my = GET_DISABLED_CONTROL_NORMAL(0, 240) * g.screenH;
		Layout l = layout();
		int h = hover(l, s_mx, s_my);
		bool lmb = input::mouse_pressed(VK_LBUTTON), rmb = input::mouse_pressed(VK_RBUTTON);
		// wheel scrolls the grid
		if (IS_DISABLED_CONTROL_JUST_PRESSED(0, 241) || IS_DISABLED_CONTROL_JUST_PRESSED(0, 15))
			s_scroll = std::max(0, s_scroll - 1);
		if (IS_DISABLED_CONTROL_JUST_PRESSED(0, 242) || IS_DISABLED_CONTROL_JUST_PRESSED(0, 14))
			s_scroll = std::min(max_scroll(), s_scroll + 1);
		// number keys put the hovered item straight into that hotbar slot
		for (int k = 0; k < 9; k++)
			if (input::pressed('1' + k))
			{
				if (h >= 100 && h < 200)
				{
					int it = grid_item(h);
					if (it >= 0)
						stack_of(g_hotbar[k], it);
					else
						g_hotbar[k] = Slot{};
				}
				else if (h >= 0 && h < 9)
					std::swap(g_hotbar[k], g_hotbar[h]);
			}
		if (lmb || rmb)
		{
			if (h >= 200)
			{
				s_tab = h - 200;
				rebuild_tab();
				audio::play("random/click", nullptr, 0.25f);
			}
			else if (h >= 100)
			{
				int it = grid_item(h);
				if (!s_carried.empty())
					s_carried = Slot{}; // dropping onto the creative grid deletes it
				else if (it >= 0)
				{
					stack_of(s_carried, it);
					if (rmb)
						s_carried.count = 1;
				}
			}
			else if (h >= 0)
			{
				if (rmb && s_carried.empty() && !g_hotbar[h].empty())
				{
					s_carried = g_hotbar[h];
					s_carried.count = (s_carried.count + 1) / 2;
				}
				else
					std::swap(s_carried, g_hotbar[h]);
			}
			else if (!in(s_mx, s_my, l.left, l.top, 195 * l.s, 136 * l.s))
				s_carried = Slot{}; // thrown out of the window
		}
	}

	void draw_inventory()
	{
		Layout l = layout();
		float s = l.s;
		// dim the world like Minecraft's screen background
		rect(0, 0, (float)g.screenW, (float)g.screenH, 0x80101010, L_INV_BACK);
		for (int t = 0; t < NTABS; t++)
			if (t != s_tab)
				draw(t_tabUn[t + 1], l.left + t * 27 * s, l.top - 28 * s, 26 * s, 32 * s, 0xFFFFFFFF, L_INV_BACK + 1);
		draw(t_tabItems, l.left, l.top, 195 * s, 136 * s, 0xFFFFFFFF, L_INV);
		draw(t_tabSel[s_tab + 1], l.left + s_tab * 27 * s, l.top - 28 * s, 26 * s, 32 * s, 0xFFFFFFFF, L_INV + 1);
		for (int t = 0; t < NTABS; t++)
		{
			int it = item_find(TABS[t].icon);
			if (it >= 0)
				draw(item(it).icon, l.left + (t * 27 + 5) * s, l.top + (-28 + 9) * s, 16 * s, 16 * s, 0xFFFFFFFF,
				     L_INV_ITEM);
		}
		text(l.left + 8 * s, l.top + 6 * s, TABS[s_tab].title, 0x404040, false, L_INV_ITEM);
		// grid
		for (int r = 0; r < 5; r++)
			for (int c = 0; c < 9; c++)
			{
				int idx = (s_scroll + r) * 9 + c;
				if (idx >= (int)s_tabItems.size())
					continue;
				Slot tmp;
				stack_of(tmp, s_tabItems[idx]);
				tmp.count = 1;
				draw_item(tmp, l.left + (9 + c * 18) * s, l.top + (18 + r * 18) * s, 16 * s, L_INV_ITEM);
			}
		for (int c = 0; c < 9; c++)
			draw_item(g_hotbar[c], l.left + (9 + c * 18) * s, l.top + 112 * s, 16 * s, L_INV_ITEM);
		// scroller
		int ms = max_scroll();
		float sy = ms ? (float)s_scroll / ms : 0.0f;
		draw(ms ? t_scroller : t_scrollerOff, l.left + 175 * s, l.top + (18 + sy * (112 - 15)) * s, 12 * s, 15 * s,
		     0xFFFFFFFF, L_INV_ITEM);
		// hover highlight + tooltip
		int h = hover(l, s_mx, s_my);
		if (h >= 0 && h < 200)
		{
			float hx, hy;
			if (h >= 100)
				hx = l.left + (9 + ((h - 100) % 9) * 18) * s, hy = l.top + (18 + ((h - 100) / 9) * 18) * s;
			else
				hx = l.left + (9 + h * 18) * s, hy = l.top + 112 * s;
			rect(hx, hy, 16 * s, 16 * s, 0x80FFFFFF, L_INV_TOP);
			int it = h >= 100 ? grid_item(h) : g_hotbar[h].item;
			if (it >= 0 && s_carried.empty())
			{
				const std::string &name = item(it).display;
				float tw = text_width(name);
				float tx = s_mx + 12 * s, ty = s_my - 12 * s;
				rect(tx - 3 * s, ty - 4 * s, tw + 6 * s, 16 * s, 0xF0100010, L_TOOLTIP);
				rect(tx - 2 * s, ty - 3 * s, tw + 4 * s, 1 * s, 0x805000FF, L_TOOLTIP + 1);
				rect(tx - 2 * s, ty + 10 * s, tw + 4 * s, 1 * s, 0x8028007F, L_TOOLTIP + 1);
				rect(tx - 3 * s, ty - 3 * s, 1 * s, 14 * s, 0x805000FF, L_TOOLTIP + 1);
				rect(tx + tw + 2 * s, ty - 3 * s, 1 * s, 14 * s, 0x805000FF, L_TOOLTIP + 1);
				text(tx, ty, name, 0xFFFFFF, true, L_TOOLTIP_TEXT);
			}
		}
		else if (h >= 200)
		{
			const char *name = TABS[h - 200].title;
			float tw = text_width(name);
			float tx = s_mx + 12 * s, ty = s_my - 12 * s;
			rect(tx - 3 * s, ty - 4 * s, tw + 6 * s, 16 * s, 0xF0100010, L_TOOLTIP);
			text(tx, ty, name, 0xFFFFFF, true, L_TOOLTIP_TEXT);
		}
		if (!s_carried.empty())
			draw_item(s_carried, s_mx - 8 * s, s_my - 8 * s, 16 * s, L_TOOLTIP_TEXT + 3);
		// our overlay covers GTA's own cursor, so draw one (12x19 px art, 2x at 1080p)
		float cs = std::max(1.0f, s * 0.5f);
		draw(t_cursor, s_mx, s_my, 12 * cs, 19 * cs, 0xFFFFFFFF, L_TOOLTIP_TEXT + 6);
	}

	void close_inventory_reset() { s_carried = Slot{}; }

	void draw_debug(const std::string &extra)
	{
		float ds = std::max(1.0f, std::floor(g.gui * 0.5f)); // font pixel size: half the GUI scale, like F3
		float y = 2 * ds;
		size_t start = 0;
		while (start < extra.size())
		{
			size_t nl = extra.find('\n', start);
			if (nl == std::string::npos)
				nl = extra.size();
			std::string line = extra.substr(start, nl - start);
			float w = text_width(line, ds);
			rect(2 * ds, y - ds, w + 2 * ds, 9 * ds, 0x90505050, L_DEBUG);
			text(3 * ds, y, line, 0xE0E0E0, true, L_DEBUG + 1, ds);
			y += 9 * ds;
			start = nl + 1;
		}
	}
}
