#include "input.h"
#include <atomic>
#include <mutex>
#include <vector>

namespace input
{
	static std::mutex s_mx;
	static std::vector<int> s_queue;
	static bool s_now[256], s_held[256];
	static bool s_mouseWas[8];
	static bool s_mouseNow[8];

	void on_key(DWORD key, bool down)
	{
		if (key >= 256)
			return;
		std::lock_guard<std::mutex> l(s_mx);
		s_held[key] = down;
		if (down)
			s_queue.push_back((int)key);
	}

	static bool focused()
	{
		HWND fg = GetForegroundWindow();
		DWORD pid = 0;
		GetWindowThreadProcessId(fg, &pid);
		return pid == GetCurrentProcessId();
	}

	void begin_frame()
	{
		{
			std::lock_guard<std::mutex> l(s_mx);
			for (auto &b : s_now)
				b = false;
			for (int k : s_queue)
				s_now[k] = true;
			s_queue.clear();
		}
		bool f = focused();
		const int vks[3] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON};
		for (int i = 0; i < 3; i++)
		{
			bool d = f && (GetAsyncKeyState(vks[i]) & 0x8000);
			s_mouseNow[i] = d && !s_mouseWas[i];
			s_mouseWas[i] = d;
		}
	}

	bool pressed(int vk) { return vk >= 0 && vk < 256 && s_now[vk]; }
	bool held(int vk) { return vk >= 0 && vk < 256 && s_held[vk]; }
	static int mi(int vk) { return vk == VK_LBUTTON ? 0 : vk == VK_RBUTTON ? 1 : 2; }
	bool mouse_pressed(int vk) { return s_mouseNow[mi(vk)]; }
	bool mouse_held(int vk) { return s_mouseWas[mi(vk)]; }
}
