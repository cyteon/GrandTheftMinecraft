// Keyboard edges from ScriptHookV's keyboard handler (runs on the window thread), consumed by the script thread.
#pragma once
#include <windows.h>

namespace input
{
	void on_key(DWORD key, bool down);
	void begin_frame();  // move queued presses into this frame's set
	bool pressed(int vk); // went down since the last frame
	bool held(int vk);
	bool mouse_pressed(int vk); // VK_LBUTTON/VK_RBUTTON/VK_MBUTTON edge (only while GTA has focus)
	bool mouse_held(int vk);
}
