#include "items.h"
#include "config.h"
#include "log.h"
#include "render2d.h"
#include <cstring>
#include <fstream>
#include <sstream>

std::vector<Item> g_items;

static std::vector<std::string> split(const std::string &s, char d)
{
	std::vector<std::string> out;
	std::stringstream ss(s);
	std::string t;
	while (std::getline(ss, t, d))
		out.push_back(t);
	if (!s.empty() && s.back() == d)
		out.push_back("");
	return out;
}

static int hexv(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

static void build_lods(Item &it, int face, const std::string &hex)
{
	if (hex.size() < 16 * 16 * 8)
		return;
	Rgba full[256];
	for (int i = 0; i < 256; i++)
	{
		const char *p = hex.c_str() + i * 8;
		full[i] = {(uint8_t)(hexv(p[0]) * 16 + hexv(p[1])), (uint8_t)(hexv(p[2]) * 16 + hexv(p[3])),
		           (uint8_t)(hexv(p[4]) * 16 + hexv(p[5])), (uint8_t)(hexv(p[6]) * 16 + hexv(p[7]))};
	}
	for (int k = 0; k < 5; k++)
	{
		int n = 1 << k, cell = 16 / n;
		auto &out = it.lod[k][face];
		out.resize(n * n);
		for (int gy = 0; gy < n; gy++)
			for (int gx = 0; gx < n; gx++)
			{
				// average colour over opaque texels; alpha = coverage (so leaves and glass thin out)
				float r = 0, g = 0, b = 0, a = 0;
				int cnt = 0;
				for (int y = gy * cell; y < (gy + 1) * cell; y++)
					for (int x = gx * cell; x < (gx + 1) * cell; x++)
					{
						const Rgba &c = full[y * 16 + x];
						a += c.a;
						if (c.a)
						{
							r += c.r, g += c.g, b += c.b;
							cnt++;
						}
					}
				int tot = cell * cell;
				Rgba o{0, 0, 0, 0};
				if (cnt)
					o = {(uint8_t)(r / cnt), (uint8_t)(g / cnt), (uint8_t)(b / cnt), (uint8_t)(a / tot)};
				out[gy * n + gx] = o;
			}
		int t = 0;
		for (int y = 0; y < n; y++)
			for (int x = 0; x < n;)
			{
				int e = x + 1;
				while (e < n && std::memcmp(&out[y * n + e], &out[y * n + x], sizeof(Rgba)) == 0)
					e++;
				if (out[y * n + x].a >= 16)
					t += 2;
				x = e;
			}
		it.tris[k][face] = t;
	}
}

bool items_load()
{
	std::ifstream in(g_dataDir + "items.txt");
	if (!in)
	{
		logf("items.txt missing in %s", g_dataDir.c_str());
		return false;
	}
	std::string line;
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == '#')
			continue;
		auto f = split(line, ';');
		if (f.size() < 6)
			continue;
		Item it;
		it.name = f[0];
		it.display = f[1];
		it.block = f[2] == "block";
		it.tab = f[3];
		const std::string &fl = f[4];
		it.alpha = fl.find('a') != std::string::npos;
		it.light = fl.find('l') != std::string::npos;
		it.gravity = fl.find('g') != std::string::npos;
		it.tnt = fl.find('t') != std::string::npos;
		it.cutout = fl.find('c') != std::string::npos;
		it.skull = fl.find('k') != std::string::npos;
		it.oriented = fl.find('o') != std::string::npos;
		it.maxStack = fl.find('s') != std::string::npos ? 1 : fl.find('p') != std::string::npos ? 16 : 64;
		it.sound = f[5];
		if (f.size() >= 9)
			for (int face = 0; face < 3; face++)
				build_lods(it, face, f[6 + face]);
		if (f.size() >= 11)
		{
			static const char *SHAPES[] = {"", "slab", "stairs", "wall", "fence", "gate", "pane", "carpet", "torch",
			                               "lantern", "cross", "ladder", "door", "trapdoor"};
			for (int k = 1; k < (int)(sizeof(SHAPES) / sizeof(SHAPES[0])); k++)
				if (f[9] == SHAPES[k])
					it.shape = (Shape)k;
			it.sprite = f[10];
		}
		it.icon = r2d::tex("items/" + it.name + ".png");
		g_items.push_back(std::move(it));
	}
	logf("items: %d loaded", (int)g_items.size());
	return !g_items.empty();
}

int item_find(const std::string &name)
{
	for (size_t i = 0; i < g_items.size(); i++)
		if (g_items[i].name == name)
			return (int)i;
	return -1;
}
