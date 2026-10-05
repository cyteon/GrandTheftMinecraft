#include "mcassets.h"
#include "config.h"
#include "dlcpatch.h"
#include "log.h"
#include "version.h"

#include "../vendor/miniz.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../vendor/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../vendor/stb_image_write.h"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace mcassets
{
	// ---- the pinned Minecraft version (content-addressed URLs and SHA1s from Mojang's version manifest) ----
	static const char *MC_VERSION = "1.21.11";
	static const char *JAR_URL = "https://piston-data.mojang.com/v1/objects/ba2df812c2d12e0219c489c4cd9a5e1f0760f5bd/client.jar";
	static const char *JAR_SHA1 = "ba2df812c2d12e0219c489c4cd9a5e1f0760f5bd";
	static const char *INDEX_ID = "29";
	static const char *INDEX_URL = "https://piston-meta.mojang.com/v1/packages/4043c34fdd5d06b2ac1f6f46c5b91a4ac16e3452/29.json";
	static const char *INDEX_SHA1 = "4043c34fdd5d06b2ac1f6f46c5b91a4ac16e3452";
	static const char *OBJECTS_URL = "https://resources.download.minecraft.net/";

	static std::mutex s_mx;
	static std::string s_status = "idle";
	static std::atomic<bool> s_running{false}, s_finished{false}, s_failed{false};

	static void set_status(const std::string &st, bool log = true)
	{
		{
			std::lock_guard<std::mutex> l(s_mx);
			s_status = st;
		}
		if (log)
			logf("setup: %s", st.c_str());
	}

	std::string status()
	{
		std::lock_guard<std::mutex> l(s_mx);
		return s_status;
	}
	bool running() { return s_running; }
	bool finished() { return s_finished; }
	bool failed() { return s_failed; }

	static bool exists(const std::string &p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
	static std::string env(const char *n)
	{
		char buf[MAX_PATH];
		DWORD r = GetEnvironmentVariableA(n, buf, MAX_PATH);
		return r && r < MAX_PATH ? std::string(buf) : std::string();
	}
	static void mkdirs(const std::string &path)
	{
		for (size_t i = 3; i < path.size(); i++)
			if (path[i] == '\\' || path[i] == '/')
				CreateDirectoryA(path.substr(0, i).c_str(), nullptr);
		CreateDirectoryA(path.c_str(), nullptr);
	}
	static bool read_file(const std::string &p, std::vector<uint8_t> &out)
	{
		std::ifstream f(p, std::ios::binary);
		if (!f)
			return false;
		out.assign(std::istreambuf_iterator<char>(f), {});
		return true;
	}
	static bool write_file(const std::string &p, const void *data, size_t n)
	{
		size_t slash = p.find_last_of("\\/");
		if (slash != std::string::npos)
			mkdirs(p.substr(0, slash));
		std::ofstream f(p, std::ios::binary);
		f.write((const char *)data, n);
		return (bool)f;
	}

	// ---- SHA1 (Windows CNG) ----
	static std::string sha1_hex(const uint8_t *data, size_t n)
	{
		BCRYPT_ALG_HANDLE alg = nullptr;
		BCRYPT_HASH_HANDLE h = nullptr;
		uint8_t digest[20] = {};
		std::string hex;
		if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) == 0)
		{
			if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0)
			{
				BCryptHashData(h, (PUCHAR)data, (ULONG)n, 0);
				BCryptFinishHash(h, digest, 20, 0);
				BCryptDestroyHash(h);
			}
			BCryptCloseAlgorithmProvider(alg, 0);
		}
		char b[3];
		for (uint8_t c : digest)
			std::snprintf(b, sizeof b, "%02x", c), hex += b;
		return hex;
	}
	static bool file_has_sha1(const std::string &p, const char *sha1, std::vector<uint8_t> *keep = nullptr)
	{
		std::vector<uint8_t> d;
		if (!read_file(p, d))
			return false;
		bool ok = sha1_hex(d.data(), d.size()) == sha1;
		if (ok && keep)
			keep->swap(d);
		return ok;
	}

	// ---- HTTPS download (WinHTTP) ----
	static bool http_get(const std::string &url, std::vector<uint8_t> &out, const std::string &label)
	{
		std::wstring wurl(url.begin(), url.end());
		URL_COMPONENTS uc{};
		uc.dwStructSize = sizeof uc;
		wchar_t host[256], path[1024];
		uc.lpszHostName = host, uc.dwHostNameLength = 256;
		uc.lpszUrlPath = path, uc.dwUrlPathLength = 1024;
		if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
			return false;
		HINTERNET ses = WinHttpOpen(L"GrandTheftMinecraft/" GTM_VERSION_W, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!ses)
			return false;
		bool ok = false;
		HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
		HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", path, nullptr, WINHTTP_NO_REFERER,
		                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
		                    : nullptr;
		if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
		    WinHttpReceiveResponse(req, nullptr))
		{
			DWORD code = 0, len = sizeof code, total = 0, tlen = sizeof total;
			WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &len, nullptr);
			WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &total, &tlen,
			                    nullptr);
			if (code == 200)
			{
				out.clear();
				DWORD avail = 0, lastPct = 999;
				ok = true;
				while (WinHttpQueryDataAvailable(req, &avail) && avail)
				{
					size_t at = out.size();
					out.resize(at + avail);
					DWORD got = 0;
					if (!WinHttpReadData(req, out.data() + at, avail, &got))
					{
						ok = false;
						break;
					}
					out.resize(at + got);
					if (total && !label.empty())
					{
						DWORD pct = (DWORD)(out.size() * 100 / total);
						if (pct / 10 != lastPct / 10)
						{
							lastPct = pct;
							set_status(label + " " + std::to_string(pct) + "%");
						}
					}
				}
			}
			else
				logf("setup: HTTP %lu for %s", code, url.c_str());
		}
		if (req)
			WinHttpCloseHandle(req);
		if (con)
			WinHttpCloseHandle(con);
		WinHttpCloseHandle(ses);
		return ok;
	}

	// ---- finding the player's Minecraft ----
	struct Source
	{
		std::string jar;               // verified client jar
		std::vector<std::string> assetDirs; // launcher asset folders (indexes/, objects/)
	};

	static Source find_local()
	{
		Source s;
		std::string appdata = env("APPDATA"), home = env("USERPROFILE");
		std::vector<std::pair<std::string, std::string>> cands; // jar, assets dir
		if (!g_cfg.minecraftJar.empty())
			cands.push_back({g_cfg.minecraftJar, g_cfg.minecraftAssets});
		cands.push_back({appdata + "\\.minecraft\\versions\\" + MC_VERSION + "\\" + MC_VERSION + ".jar",
		                 appdata + "\\.minecraft\\assets"});
		cands.push_back({appdata + "\\PrismLauncher\\libraries\\com\\mojang\\minecraft\\" + MC_VERSION + "\\minecraft-" +
		                     MC_VERSION + "-client.jar",
		                 appdata + "\\PrismLauncher\\assets"});
		cands.push_back({appdata + "\\PolyMC\\libraries\\com\\mojang\\minecraft\\" + MC_VERSION + "\\minecraft-" +
		                     MC_VERSION + "-client.jar",
		                 appdata + "\\PolyMC\\assets"});
		cands.push_back({home + "\\curseforge\\minecraft\\Install\\versions\\" + MC_VERSION + "\\" + MC_VERSION + ".jar",
		                 home + "\\curseforge\\minecraft\\Install\\assets"});
		cands.push_back({appdata + "\\ModrinthApp\\meta\\versions\\" + MC_VERSION + "\\" + MC_VERSION + ".jar",
		                 appdata + "\\ModrinthApp\\meta"});
		for (auto &c : cands)
		{
			if (!c.second.empty() && exists(c.second))
				s.assetDirs.push_back(c.second);
			if (s.jar.empty() && exists(c.first))
			{
				if (file_has_sha1(c.first, JAR_SHA1))
				{
					s.jar = c.first;
					logf("setup: using local Minecraft %s jar %s", MC_VERSION, c.first.c_str());
				}
				else
					logf("setup: %s is not the official %s client jar (SHA1 mismatch), skipping", c.first.c_str(),
					     MC_VERSION);
			}
		}
		return s;
	}

	// ---- images ----
	struct Img
	{
		int w = 0, h = 0;
		std::vector<uint8_t> px; // RGBA
		uint8_t *at(int x, int y) { return &px[(size_t)(y * w + x) * 4]; }
		const uint8_t *at(int x, int y) const { return &px[(size_t)(y * w + x) * 4]; }
	};

	static mz_zip_archive s_zip;
	static bool s_zipOpen = false;

	static bool jar_read(const std::string &name, std::vector<uint8_t> &out)
	{
		size_t n = 0;
		void *p = mz_zip_reader_extract_file_to_heap(&s_zip, name.c_str(), &n, 0);
		if (!p)
			return false;
		out.assign((uint8_t *)p, (uint8_t *)p + n);
		mz_free(p);
		return true;
	}

	static bool tex(const std::string &rel, Img &img)
	{
		std::vector<uint8_t> d;
		if (!jar_read("assets/minecraft/textures/" + rel, d))
		{
			logf("setup: missing texture %s", rel.c_str());
			return false;
		}
		int c;
		uint8_t *p = stbi_load_from_memory(d.data(), (int)d.size(), &img.w, &img.h, &c, 4);
		if (!p)
			return false;
		img.px.assign(p, p + (size_t)img.w * img.h * 4);
		stbi_image_free(p);
		return true;
	}

	static Img crop(const Img &s, int x0, int y0, int w, int h)
	{
		Img o;
		o.w = w, o.h = h;
		o.px.resize((size_t)w * h * 4);
		for (int y = 0; y < h; y++)
			std::memcpy(o.at(0, y), s.at(x0, y0 + y), (size_t)w * 4);
		return o;
	}

	static Img block_tex(const std::string &name)
	{
		Img i;
		if (!tex("block/" + name + ".png", i))
		{
			i.w = i.h = 16;
			i.px.assign(16 * 16 * 4, 255); // white stand-in
			return i;
		}
		if (i.h > i.w) // animated strip: first frame
			i = crop(i, 0, 0, i.w, i.w);
		return i;
	}

	static Img resize_nn(const Img &s, int w, int h)
	{
		Img o;
		o.w = w, o.h = h;
		o.px.resize((size_t)w * h * 4);
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
				std::memcpy(o.at(x, y), s.at(x * s.w / w, y * s.h / h), 4);
		return o;
	}
	static Img up(const Img &s, int k = 4) { return resize_nn(s, s.w * k, s.h * k); }

	static Img mul(const Img &s, float r, float g, float b)
	{
		Img o = s;
		for (size_t i = 0; i < o.px.size(); i += 4)
		{
			o.px[i] = (uint8_t)std::min(255.0f, o.px[i] * r);
			o.px[i + 1] = (uint8_t)std::min(255.0f, o.px[i + 1] * g);
			o.px[i + 2] = (uint8_t)std::min(255.0f, o.px[i + 2] * b);
		}
		return o;
	}
	static Img tint(const Img &s, uint32_t rgb)
	{
		return mul(s, ((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f);
	}

	static bool save_png(const std::string &rel, const Img &i)
	{
		std::string p = g_dataDir + rel;
		size_t slash = p.find_last_of("\\/");
		mkdirs(p.substr(0, slash));
		return stbi_write_png(p.c_str(), i.w, i.h, 4, i.px.data(), i.w * 4) != 0;
	}

	// Minecraft-style inventory block icon, nearest-neighbour sampled from three 16x16 faces (64 px).
	static Img iso_icon(const Img &top, const Img &left, const Img &right)
	{
		const int S = 64;
		Img o;
		o.w = o.h = S;
		o.px.assign(S * S * 4, 0);
		struct F
		{
			const Img *t;
			float ox, oy, ux, uy, vx, vy;
		};
		const F faces[3] = {{&top, 3, 17, 29, -15, 29, 15}, {&left, 3, 17, 29, 15, 0, 30}, {&right, 32, 32, 29, -15, 0, 30}};
		for (const F &f : faces)
		{
			float det = f.ux * f.vy - f.vx * f.uy;
			int n = f.t->w;
			for (int y = 0; y < S; y++)
				for (int x = 0; x < S; x++)
				{
					float px = x + 0.5f - f.ox, py = y + 0.5f - f.oy;
					float u = (px * f.vy - py * f.vx) / det, v = (py * f.ux - px * f.uy) / det;
					if (u < -1e-6f || u >= 1 || v < -1e-6f || v >= 1)
						continue;
					const uint8_t *c = f.t->at(std::min(n - 1, (int)(u * n)), std::min(n - 1, (int)(v * n)));
					if (c[3])
						std::memcpy(o.at(x, y), c, 4);
				}
		}
		return o;
	}

	// ---- definitions (data/defs.txt, shipped with the mod) ----
	struct BlockDef
	{
		std::string name, display, tab, flags, sound, top, side, bottom, front; // front: only flag o
	};
	struct ItemDef
	{
		std::string name, display, tab, flags, sprite;
	};
	static std::vector<BlockDef> s_blocks;
	static std::vector<std::string> s_icons; // tab icons that aren't items
	static std::vector<ItemDef> s_items;

	static std::vector<std::string> split(const std::string &s, char d)
	{
		std::vector<std::string> out;
		std::string cur;
		for (char c : s)
		{
			if (c == d)
				out.push_back(cur), cur.clear();
			else if (c != '\r')
				cur += c;
		}
		out.push_back(cur);
		return out;
	}

	static bool load_defs()
	{
		std::ifstream in(g_dataDir + "defs.txt");
		if (!in)
			return false;
		std::string line;
		while (std::getline(in, line))
		{
			if (line.empty() || line[0] == '#')
				continue;
			auto f = split(line, ';');
			if (f[0] == "block" && f.size() >= 9)
			{
				BlockDef b{f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f.size() > 9 ? f[9] : ""};
				if (b.side.empty())
					b.side = b.top;
				if (b.bottom.empty())
					b.bottom = b.top;
				s_blocks.push_back(b);
			}
			else if (f[0] == "item" && f.size() >= 6)
				s_items.push_back({f[1], f[2], f[3], f[4], f[5]});
			else if (f[0] == "icon" && f.size() >= 2)
				s_icons.push_back(f[1]);
		}
		return !s_blocks.empty();
	}

	static const uint32_t GRASS_TINT = 0x91BD59, FOLIAGE_TINT = 0x77AB2F;

	static void block_faces(const BlockDef &b, Img &t, Img &s, Img &bt)
	{
		if (b.flags.find('k') != std::string::npos) // mob skull: the head of entity/skeleton/<top>.png (64x32)
		{
			Img e;
			if (!tex("entity/skeleton/" + b.top + ".png", e) || e.w < 32 || e.h < 16)
				e.w = 64, e.h = 32, e.px.assign(64 * 32 * 4, 255);
			int k = e.w / 64;
			t = resize_nn(crop(e, 8 * k, 0, 8 * k, 8 * k), 16, 16);
			s = resize_nn(crop(e, 8 * k, 8 * k, 8 * k, 8 * k), 16, 16);
			bt = resize_nn(crop(e, 16 * k, 0, 8 * k, 8 * k), 16, 16);
			return;
		}
		t = block_tex(b.top), s = block_tex(b.side), bt = block_tex(b.bottom);
		if (b.flags.find('r') != std::string::npos)
			t = tint(t, GRASS_TINT);
		if (b.flags.find('f') != std::string::npos)
			t = tint(t, FOLIAGE_TINT), s = tint(s, FOLIAGE_TINT), bt = tint(bt, FOLIAGE_TINT);
		t = resize_nn(t, 16, 16), s = resize_nn(s, 16, 16), bt = resize_nn(bt, 16, 16);
	}

	static std::string hexface(const Img &i)
	{
		Img s = resize_nn(i, 16, 16);
		std::string h;
		h.reserve(2048);
		char b[3];
		for (uint8_t c : s.px)
			std::snprintf(b, sizeof b, "%02x", c), h += b;
		return h;
	}

	// ---- the outputs ----
	static bool build_gui()
	{
		set_status("Building the Minecraft HUD...");
		const char *GUI[][2] = {{"hotbar", "gui/sprites/hud/hotbar.png"},
		                        {"hotbar_selection", "gui/sprites/hud/hotbar_selection.png"},
		                        {"crosshair", "gui/sprites/hud/crosshair.png"},
		                        {"scroller", "gui/sprites/container/creative_inventory/scroller.png"},
		                        {"scroller_disabled", "gui/sprites/container/creative_inventory/scroller_disabled.png"},
		                        {"boss_bar_bg", "gui/sprites/boss_bar/purple_background.png"},
		                        {"boss_bar_fg", "gui/sprites/boss_bar/purple_progress.png"}};
		for (auto &g : GUI)
		{
			Img i;
			if (tex(g[1], i))
				save_png(std::string("gui/") + g[0] + ".png", up(i));
		}
		for (int k = 1; k <= 7; k++)
			for (const char *st : {"selected", "unselected"})
			{
				Img i;
				for (const char *row : {"top", "bottom"})
				{
					std::string n = std::string("tab_") + row + "_" + st + "_" + std::to_string(k);
					if (tex("gui/sprites/container/creative_inventory/" + n + ".png", i))
						save_png("gui/" + n + ".png", up(i));
				}
			}
		Img tab;
		if (tex("gui/container/creative_inventory/tab_items.png", tab))
			save_png("gui/tab_items.png", up(crop(tab, 0, 0, 195, 136)));
		// our own: a white pixel, the calibration checker, the mouse cursor
		Img w;
		w.w = w.h = 4;
		w.px.assign(64, 255);
		save_png("gui/white.png", w);
		Img chk;
		chk.w = chk.h = 64;
		chk.px.resize(64 * 64 * 4);
		for (int y = 0; y < 64; y++)
			for (int x = 0; x < 64; x++)
			{
				bool a = ((x / 4) + (y / 4)) % 2;
				uint8_t *p = chk.at(x, y);
				p[0] = 255, p[1] = a ? 255 : 0, p[2] = 255, p[3] = 255;
			}
		save_png("gui/calib.png", chk);
		static const char *ARROW[19] = {"X...........", "XX..........", "XWX.........", "XWWX........", "XWWWX.......",
		                                "XWWWWX......", "XWWWWWX.....", "XWWWWWWX....", "XWWWWWWWX...", "XWWWWWWWWX..",
		                                "XWWWWWWWWWX.", "XWWWWWWXXXXX", "XWWWXWWX....", "XWWXXWWX....", "XWX..XWWX...",
		                                "XX...XWWX...", "X.....XWWX..", "......XWWX..", ".......XX..."};
		Img cur;
		cur.w = 12, cur.h = 19;
		cur.px.assign(12 * 19 * 4, 0);
		for (int y = 0; y < 19; y++)
			for (int x = 0; x < 12; x++)
			{
				uint8_t *p = cur.at(x, y);
				if (ARROW[y][x] == 'X')
					p[3] = 255;
				else if (ARROW[y][x] == 'W')
					p[0] = p[1] = p[2] = p[3] = 255;
			}
		save_png("gui/cursor.png", up(cur));
		return true;
	}

	static bool build_font()
	{
		set_status("Building the Minecraft font...");
		Img a;
		if (!tex("font/ascii.png", a))
			return false;
		int cell = a.w / 16;
		std::string widths;
		for (int c = 0; c < 256; c++)
		{
			Img g = crop(a, (c % 16) * cell, (c / 16) * cell, cell, cell);
			int right = -1;
			for (int x = 0; x < cell; x++)
				for (int y = 0; y < cell; y++)
					if (g.at(x, y)[3])
						right = std::max(right, x);
			int adv = c == 32 ? 4 : right < 0 ? 0 : right + 2;
			if (c > 32 && c < 127 && adv)
				save_png("font/" + std::to_string(c) + ".png", up(g));
			widths += std::to_string(adv) + (c < 255 ? " " : "\n");
		}
		write_file(g_dataDir + "font\\font.txt", widths.data(), widths.size());
		return true;
	}

	static bool build_items()
	{
		set_status("Building block and item icons...");
		std::string txt = "# name;Display Name;kind;tab;flags;sound;top 16x16 RGBA hex;side;bottom\n";
		for (auto &b : s_blocks)
		{
			Img t, s, bt;
			block_faces(b, t, s, bt);
			Img fr = b.front.empty() ? s : resize_nn(block_tex(b.front), 16, 16); // Minecraft shows the front on the left
			save_png("items/" + b.name + ".png", iso_icon(t, mul(fr, 0.8f, 0.8f, 0.8f), mul(s, 0.6f, 0.6f, 0.6f)));
			txt += b.name + ";" + b.display + ";block;" + b.tab + ";" + b.flags + ";" + b.sound + ";" + hexface(t) +
			       ";" + hexface(s) + ";" + hexface(bt) + "\n";
		}
		for (auto &it : s_items)
		{
			Img spr;
			if (!tex("item/" + it.sprite + ".png", spr))
				continue;
			save_png("items/" + it.name + ".png", up(spr));
			txt += it.name + ";" + it.display + ";item;" + it.tab + ";" + it.flags + ";;" + hexface(spr) + ";;\n";
		}
		for (auto &n : s_icons)
		{
			Img spr;
			if (tex("item/" + n + ".png", spr))
				save_png("items/" + n + ".png", up(spr));
		}
		return write_file(g_dataDir + "items.txt", txt.data(), txt.size());
	}

	static bool build_particles()
	{
		set_status("Building particles...");
		std::vector<std::string> names;
		for (int i = 0; i < 16; i++)
			names.push_back("explosion_" + std::to_string(i));
		for (int i = 0; i < 12; i++)
			names.push_back("big_smoke_" + std::to_string(i));
		for (int i = 0; i < 8; i++)
			names.push_back("generic_" + std::to_string(i)), names.push_back("sweep_" + std::to_string(i)),
				names.push_back("spark_" + std::to_string(i));
		names.push_back("flame"), names.push_back("critical_hit");
		for (auto &n : names)
		{
			Img i;
			if (tex("particle/" + n + ".png", i))
				save_png("particles/" + n + ".png", up(i));
		}
		return true;
	}

	// ---- sounds: Minecraft's .ogg files (played directly through stb_vorbis) ----
	static const std::pair<const char *, int> SOUNDS[] = {
		{"dig/stone", 4}, {"dig/grass", 4}, {"dig/wood", 4}, {"dig/gravel", 4}, {"dig/sand", 4}, {"dig/cloth", 4},
		{"dig/snow", 4}, {"random/glass", 3}, {"random/explode", 4}, {"random/fuse", 0}, {"random/bow", 0},
		{"mob/endermen/portal", 0}, {"fire/ignite", 0}, {"random/click", 0}, {"random/pop", 0}, {"damage/hit", 3},
		{"random/orb", 0}, {"random/bowhit", 4}, {"entity/player/attack/strong", 1}, {"entity/player/attack/knockback", 1},
		{"entity/player/attack/sweep", 1}, {"item/crossbow/loading_start", 0}, {"item/crossbow/loading_middle", 4},
		{"item/crossbow/loading_end", 0}, {"item/crossbow/shoot", 3},
		{"mob/zombie/say", 3}, {"mob/zombie/hurt", 2}, {"mob/zombie/death", 0},
		{"mob/skeleton/say", 3}, {"mob/skeleton/hurt", 4}, {"mob/skeleton/death", 0},
		{"mob/creeper/say", 4}, {"mob/creeper/death", 0},
		{"mob/irongolem/hit", 4}, {"mob/irongolem/damage", 2}, {"mob/irongolem/death", 0},
		{"mob/irongolem/throw", 0}, {"fireworks/launch", 1}, {"fireworks/blast", 1}, {"fireworks/twinkle", 1},
		{"item/elytra/elytra_loop", 0}, {"mob/wither/spawn", 0}, {"mob/wither/shoot", 0},
		{"mob/wither/idle", 4}, {"mob/wither/hurt", 4}, {"mob/wither/death", 0}};

	static bool build_sounds(const Source &src)
	{
		set_status("Getting the asset index...");
		std::vector<uint8_t> idx;
		bool have = false;
		for (auto &d : src.assetDirs)
			if (file_has_sha1(d + "\\indexes\\" + INDEX_ID + ".json", INDEX_SHA1, &idx))
			{
				have = true;
				break;
			}
		std::string cached = g_dataDir + "cache\\" + INDEX_ID + ".json";
		if (!have && file_has_sha1(cached, INDEX_SHA1, &idx))
			have = true;
		if (!have)
		{
			if (!http_get(INDEX_URL, idx, "") || sha1_hex(idx.data(), idx.size()) != INDEX_SHA1)
			{
				logf("setup: could not get the asset index; sounds skipped");
				return false;
			}
			write_file(cached, idx.data(), idx.size());
		}
		std::string json(idx.begin(), idx.end());
		std::vector<std::string> names;
		for (auto &s : SOUNDS)
		{
			if (!s.second)
				names.push_back(s.first);
			for (int i = 1; i <= s.second; i++)
				names.push_back(std::string(s.first) + std::to_string(i));
		}
		int n = 0, got = 0;
		for (auto &name : names)
		{
			set_status("Getting sounds " + std::to_string(++n) + "/" + std::to_string(names.size()) + "...", false);
			std::string key = "\"minecraft/sounds/" + name + ".ogg\"";
			size_t at = json.find(key);
			size_t hp = at == std::string::npos ? at : json.find("\"hash\"", at);
			size_t q = hp == std::string::npos ? hp : json.find('"', json.find(':', hp));
			if (q == std::string::npos)
			{
				logf("setup: sound %s not in the index", name.c_str());
				continue;
			}
			std::string hash = json.substr(q + 1, 40);
			std::string rel = hash.substr(0, 2) + "\\" + hash;
			std::vector<uint8_t> data;
			bool ok = false;
			for (auto &d : src.assetDirs)
				if (file_has_sha1(d + "\\objects\\" + rel, hash.c_str(), &data))
				{
					ok = true;
					break;
				}
			if (!ok && http_get(std::string(OBJECTS_URL) + hash.substr(0, 2) + "/" + hash, data, "") &&
			    sha1_hex(data.data(), data.size()) == hash)
				ok = true;
			if (ok)
			{
				std::string out = g_dataDir + "sounds\\" + name + ".ogg";
				std::replace(out.begin(), out.end(), '/', '\\');
				write_file(out, data.data(), data.size());
				got++;
			}
		}
		logf("setup: %d/%d sounds", got, (int)names.size());
		return got > 0;
	}

	// ---- the block pack's textures (recipes from dlc_tex.txt, written next to the DLC by gtmpack) ----
	static void append_mips(const Img &base, std::vector<uint8_t> &out)
	{
		// box-filtered chain down to 1x1, stored as BGRA (D3DFMT_A8R8G8B8)
		int w = base.w, h = base.h;
		std::vector<float> cur(base.px.begin(), base.px.end());
		while (true)
		{
			for (int i = 0; i < w * h; i++)
			{
				out.push_back((uint8_t)std::clamp(cur[i * 4 + 2], 0.0f, 255.0f));
				out.push_back((uint8_t)std::clamp(cur[i * 4 + 1], 0.0f, 255.0f));
				out.push_back((uint8_t)std::clamp(cur[i * 4 + 0], 0.0f, 255.0f));
				out.push_back((uint8_t)std::clamp(cur[i * 4 + 3], 0.0f, 255.0f));
			}
			if (w == 1 && h == 1)
				break;
			int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
			std::vector<float> nx((size_t)nw * nh * 4);
			for (int y = 0; y < nh; y++)
				for (int x = 0; x < nw; x++)
					for (int c = 0; c < 4; c++)
					{
						int x0 = std::min(w - 1, x * 2), x1 = std::min(w - 1, x * 2 + (w > 1));
						int y0 = std::min(h - 1, y * 2), y1 = std::min(h - 1, y * 2 + (h > 1));
						nx[(y * nw + x) * 4 + c] = (cur[(y0 * w + x0) * 4 + c] + cur[(y0 * w + x1) * 4 + c] +
						                            cur[(y1 * w + x0) * 4 + c] + cur[(y1 * w + x1) * 4 + c]) * 0.25f;
					}
			cur.swap(nx), w = nw, h = nh;
		}
	}

	static std::string skin_tag();

	static bool build_textures(const std::string &layoutPath)
	{
		set_status("Building block textures...");
		std::ifstream in(layoutPath);
		if (!in)
		{
			logf("setup: %s missing; the block pack can't be textured", layoutPath.c_str());
			return false;
		}
		std::map<std::string, const BlockDef *> byName;
		for (auto &b : s_blocks)
			byName[b.name] = &b;
		std::vector<uint8_t> bin = {'G', 'T', 'M', 'T', 'X', '1', 0, 0};
		std::string line;
		int count = 0;
		while (std::getline(in, line))
		{
			auto f = split(line, ';');
			if (f[0] != "tex" || f.size() < 8)
				continue;
			uint32_t off = (uint32_t)std::stoul(f[2]), size = (uint32_t)std::stoul(f[3]);
			int w = std::stoi(f[4]), h = std::stoi(f[5]);
			auto r = split(f[7], ' ');
			Img base;
			if (r[0] == "sheet" && r.size() > 1 && byName.count(r[1]))
			{
				const BlockDef &b = *byName[r[1]];
				Img t, s, bt;
				block_faces(b, t, s, bt);
				Img sheet;
				sheet.w = 64, sheet.h = 16;
				sheet.px.assign(64 * 16 * 4, 0);
				Img fr = b.front.empty() ? s : resize_nn(block_tex(b.front), 16, 16);
				const Img *faces[4] = {&t, &s, &bt, &fr};
				bool opaque = b.flags.find('a') == std::string::npos && b.flags.find('c') == std::string::npos;
				for (int k = 0; k < 4; k++)
					for (int y = 0; y < 16; y++)
						for (int x = 0; x < 16; x++)
						{
							uint8_t *p = sheet.at(k * 16 + x, y);
							std::memcpy(p, faces[k]->at(x, y), 4);
							if (opaque)
								p[3] = 255;
						}
				base = resize_nn(sheet, w, h);
			}
			else if (r[0] == "sprite" && r.size() > 1)
			{
				Img spr;
				if (!tex("item/" + r[1] + ".png", spr))
					spr.w = spr.h = 16, spr.px.assign(16 * 16 * 4, 0);
				base = resize_nn(resize_nn(spr, 16, 16), w, h);
			}
			else if (r[0] == "skin")
			{
				Img sk;
				bool own = false;
				if (!g_cfg.skinFile.empty())
				{
					std::vector<uint8_t> d;
					int c;
					if (read_file(g_cfg.skinFile, d))
						if (uint8_t *px = stbi_load_from_memory(d.data(), (int)d.size(), &sk.w, &sk.h, &c, 4))
						{
							sk.px.assign(px, px + (size_t)sk.w * sk.h * 4);
							stbi_image_free(px);
							own = sk.w == 64 && sk.h == 64;
						}
					if (!own)
						logf("setup: SkinFile %s isn't a 64x64 PNG skin; using Steve", g_cfg.skinFile.c_str());
				}
				if (!own && !tex("entity/player/wide/steve.png", sk))
					sk.w = sk.h = 64, sk.px.assign(64 * 64 * 4, 255);
				base = resize_nn(sk, w, h);
			}
			else if (r[0] == "entity" && r.size() > 1) // a mob's texture: entity/zombie/zombie.png ...
			{
				Img e;
				if (!tex(r[1], e))
					e.w = 64, e.h = 64, e.px.assign(64 * 64 * 4, 255);
				base = resize_nn(e, w, h);
			}
			else if (r[0] == "arrow")
			{
				Img a;
				if (!tex("entity/projectiles/arrow.png", a))
					a.w = a.h = 32, a.px.assign(32 * 32 * 4, 0);
				base = resize_nn(a, w, h);
			}
			else
				continue;
			std::vector<uint8_t> px;
			append_mips(base, px);
			if (px.size() != size)
			{
				logf("setup: %s size %u != layout %u", f[1].c_str(), (unsigned)px.size(), size);
				return false;
			}
			bin.insert(bin.end(), (uint8_t *)&off, (uint8_t *)&off + 4);
			bin.insert(bin.end(), (uint8_t *)&size, (uint8_t *)&size + 4);
			bin.insert(bin.end(), px.begin(), px.end());
			count++;
		}
		std::string tmp = g_dataDir + "textures.bin.tmp";
		if (!write_file(tmp, bin.data(), bin.size()))
			return false;
		MoveFileExA(tmp.c_str(), (g_dataDir + "textures.bin").c_str(), MOVEFILE_REPLACE_EXISTING);
		std::string tag = skin_tag() + "\n";
		write_file(g_dataDir + "skin.txt", tag.data(), tag.size());
		logf("setup: textures.bin with %d textures (%u KB)", count, (unsigned)(bin.size() / 1024));
		return count > 0;
	}

	// ---- state ----
	// rebuilt when the mod, Minecraft's version or the block/item list (defs.txt) changes
	static std::string ok_tag()
	{
		std::vector<uint8_t> d;
		read_file(g_dataDir + "defs.txt", d);
		uint32_t h = 2166136261u; // FNV-1a
		for (uint8_t c : d)
			if (c != 13) // CRLF and LF copies hash the same
				h = (h ^ c) * 16777619u;
		char hex[9];
		std::snprintf(hex, sizeof hex, "%08x", h);
		return std::string(GTM_VERSION) + " " + MC_VERSION + " " + hex;
	}

	bool data_ready()
	{
		std::ifstream in(g_dataDir + "assets.ok");
		std::string tag;
		std::getline(in, tag);
		return tag == ok_tag() && exists(g_dataDir + "items.txt");
	}

	bool textures_pending() { return exists(g_dataDir + "textures.bin"); }

	// which skin the block pack's textures were built with (SkinFile path + its modification time)
	static std::string skin_tag()
	{
		if (g_cfg.skinFile.empty())
			return "steve";
		WIN32_FILE_ATTRIBUTE_DATA a{};
		if (!GetFileAttributesExA(g_cfg.skinFile.c_str(), GetFileExInfoStandard, &a))
			return "missing " + g_cfg.skinFile;
		return g_cfg.skinFile + " " + std::to_string(a.ftLastWriteTime.dwLowDateTime);
	}

	static bool skin_changed()
	{
		std::ifstream in(g_dataDir + "skin.txt");
		std::string tag;
		std::getline(in, tag);
		return tag != skin_tag();
	}

	static void run(bool needData, bool needTextures)
	{
		bool ok = true;
		if (!load_defs())
		{
			set_status("defs.txt is missing - reinstall the mod");
			s_failed = true;
			s_running = false;
			return;
		}
		set_status("Looking for Minecraft " + std::string(MC_VERSION) + "...");
		Source src = find_local();
		if (src.jar.empty())
		{
			std::string cached = g_dataDir + "cache\\client-" + MC_VERSION + ".jar";
			if (file_has_sha1(cached, JAR_SHA1))
				src.jar = cached;
			else
			{
				std::vector<uint8_t> jar;
				if (!http_get(JAR_URL, jar, std::string("Downloading Minecraft ") + MC_VERSION + " from Mojang") ||
				    sha1_hex(jar.data(), jar.size()) != JAR_SHA1)
				{
					set_status("Couldn't get Minecraft " + std::string(MC_VERSION) +
					           " (no local install, download failed). See gtm.log.");
					s_failed = true;
					s_running = false;
					return;
				}
				write_file(cached, jar.data(), jar.size());
				src.jar = cached;
				logf("setup: downloaded and verified the %s client jar", MC_VERSION);
			}
		}
		std::memset(&s_zip, 0, sizeof s_zip);
		if (!mz_zip_reader_init_file(&s_zip, src.jar.c_str(), 0))
		{
			set_status("Can't open " + src.jar);
			s_failed = true;
			s_running = false;
			return;
		}
		s_zipOpen = true;
		// the block pack first: GTA opens it a few seconds after the process starts, and if the textures are in by
		// then no restart is needed (dlcpatch refuses to touch it once GTA has it open)
		if (needTextures)
		{
			std::string layout = g_dataDir + "dlc_tex.txt";
			if (!build_textures(layout))
				logf("setup: block textures not built; blocks stay as Stage 1 polygons");
			else if (dlcpatch::apply_pending())
				logf("setup: block pack textured before GTA loaded it - no restart needed");
		}
		if (needData)
		{
			ok &= build_gui();
			ok &= build_font();
			ok &= build_items();
			ok &= build_particles();
			build_sounds(src); // optional: the mod works silently without them
		}
		mz_zip_reader_end(&s_zip);
		s_zipOpen = false;
		if (needData && ok)
		{
			std::string tag = ok_tag() + "\n";
			write_file(g_dataDir + "assets.ok", tag.data(), tag.size());
		}
		if (!ok)
		{
			set_status("Setup failed - see GrandTheftMinecraft\\gtm.log");
			s_failed = true;
		}
		else
			set_status(textures_pending() ? "Done! Restart GTA once to load the textured blocks."
			                              : "Done!");
		s_finished = true;
		s_running = false;
	}

	bool start_if_needed()
	{
		if (s_running)
			return true;
		bool needData = !data_ready();
		bool needTextures = !dlcpatch::find_dlc_rpf().empty() && !textures_pending() &&
		                    (!dlcpatch::dlc_ready() || skin_changed());
		if (!needData && !needTextures)
			return false;
		s_running = true;
		s_finished = false;
		s_failed = false;
		std::thread(run, needData, needTextures).detach();
		return true;
	}
}
