// Minecraft HUD (hotbar, crosshair, held item, item name), creative inventory (E) and the F9 debug overlay.
#pragma once
#include <string>

struct Slot
{
	int item = -1;
	int count = 0;
	bool loaded = false; // crossbow: an arrow is loaded
	bool empty() const { return item < 0 || count <= 0; }
};

extern Slot g_hotbar[9];
extern int g_sel;
extern bool g_invOpen;

namespace gui
{
	void init();
	void select(int slot);
	void set_selected_item(int item); // pick block: select it if it's in the hotbar, else put it in this slot
	void swing();                     // hand swing animation (click)
	void update_inventory();          // input for the open inventory (call before drawing)
	void draw_hud(bool interactive);
	void draw_inventory();
	void close_inventory_reset(); // drops the stack on the cursor
	void draw_debug(const std::string &extra);
	void toast(const std::string &msg); // short message above the hotbar (uses the item-name slot)
}
