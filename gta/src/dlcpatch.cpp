#include "dlcpatch.h"
#include "config.h"
#include "log.h"

#include "../vendor/miniz.h"

#include <windows.h>
#include <io.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace dlcpatch
{
	static std::string game_dir()
	{
		// g_dataDir = <game>\GrandTheftMinecraft\  ->  <game>\  .
		std::string d = g_dataDir;
		if (!d.empty() && (d.back() == '\\' || d.back() == '/'))
			d.pop_back();
		return d.substr(0, d.find_last_of("\\/") + 1);
	}

	static bool exists(const std::string &p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

	std::string find_dlc_rpf()
	{
		std::string g = game_dir();
		for (const char *rel : {"mods\\update\\x64\\dlcpacks\\gtm\\dlc.rpf", "update\\x64\\dlcpacks\\gtm\\dlc.rpf"})
			if (exists(g + rel))
				return g + rel;
		return "";
	}

	static std::string stamp(const std::string &path)
	{
		WIN32_FILE_ATTRIBUTE_DATA a{};
		if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &a))
			return "";
		char buf[96];
		std::snprintf(buf, sizeof buf, "%lu:%lu:%lu:%lu", a.nFileSizeHigh, a.nFileSizeLow, a.ftLastWriteTime.dwHighDateTime,
		              a.ftLastWriteTime.dwLowDateTime);
		return buf;
	}

	bool dlc_ready()
	{
		std::string rpf = find_dlc_rpf();
		if (rpf.empty())
			return false;
		std::ifstream in(g_dataDir + "dlc_ready.txt");
		std::string s;
		std::getline(in, s);
		return !s.empty() && s == stamp(rpf);
	}

	// ---- RPF7 (OPEN, unencrypted) ----
	struct Rpf
	{
		uint32_t count = 0, namesLen = 0, enc = 0;
		std::vector<uint8_t> entries; // count * 16
		std::vector<char> names;
	};

	static bool read_rpf(FILE *f, long long base, Rpf &r)
	{
		uint32_t hdr[4];
		_fseeki64(f, base, SEEK_SET);
		if (fread(hdr, 4, 4, f) != 4 || hdr[0] != 0x52504637) // 'RPF7'
			return false;
		r.count = hdr[1], r.namesLen = hdr[2], r.enc = hdr[3];
		if (r.enc != 0x4E45504F && r.enc != 0) // OPEN or NONE only (our own archives)
			return false;
		r.entries.resize((size_t)r.count * 16);
		r.names.resize(r.namesLen);
		return fread(r.entries.data(), 1, r.entries.size(), f) == r.entries.size() &&
		       fread(r.names.data(), 1, r.names.size(), f) == r.names.size();
	}

	static std::string entry_name(const Rpf &r, int i)
	{
		const uint8_t *e = &r.entries[(size_t)i * 16];
		uint32_t off;
		uint32_t w1;
		std::memcpy(&w1, e + 4, 4);
		if (w1 == 0x7FFFFF00) // directory: 32-bit name offset
			std::memcpy(&off, e, 4);
		else
			off = (uint32_t)(e[0] | (e[1] << 8));
		return off < r.names.size() ? std::string(&r.names[off]) : std::string();
	}

	static bool is_dir(const Rpf &r, int i)
	{
		uint32_t w1;
		std::memcpy(&w1, &r.entries[(size_t)i * 16 + 4], 4);
		return w1 == 0x7FFFFF00;
	}

	// walk "a/b/c" from the root directory (entry 0); returns the entry index or -1
	static int find_path(const Rpf &r, const std::vector<std::string> &parts)
	{
		int dir = 0;
		for (size_t p = 0; p < parts.size(); p++)
		{
			uint32_t first, cnt;
			std::memcpy(&first, &r.entries[(size_t)dir * 16 + 8], 4);
			std::memcpy(&cnt, &r.entries[(size_t)dir * 16 + 12], 4);
			int found = -1;
			for (uint32_t i = first; i < first + cnt && i < r.count; i++)
				if (_stricmp(entry_name(r, (int)i).c_str(), parts[p].c_str()) == 0)
				{
					found = (int)i;
					break;
				}
			if (found < 0)
				return -1;
			if (p + 1 < parts.size() && !is_dir(r, found))
				return -1;
			dir = found;
		}
		return dir;
	}

	static uint32_t file_offset_blocks(const Rpf &r, int i) // resource and binary entries
	{
		const uint8_t *e = &r.entries[(size_t)i * 16];
		return (uint32_t)(e[5] | (e[6] << 8) | ((e[7] & 0x7F) << 16));
	}

	// RSC7 page sizes from the entry flags (CodeWalker's RpfResourceFileEntry.GetSizeFromFlags)
	static uint32_t size_from_flags(uint32_t f)
	{
		uint32_t s0 = ((f >> 27) & 1) << 0, s1 = ((f >> 26) & 1) << 1, s2 = ((f >> 25) & 1) << 2, s3 = ((f >> 24) & 1) << 3;
		uint32_t s4 = ((f >> 17) & 0x7F) << 4, s5 = ((f >> 11) & 0x3F) << 5, s6 = ((f >> 7) & 0xF) << 6;
		uint32_t s7 = ((f >> 5) & 3) << 7, s8 = ((f >> 4) & 1) << 8;
		return (0x200u << (f & 0xF)) * (s0 + s1 + s2 + s3 + s4 + s5 + s6 + s7 + s8);
	}

	bool apply_pending()
	{
		std::string binPath = g_dataDir + "textures.bin";
		std::string rpfPath = find_dlc_rpf();
		if (!exists(binPath) || rpfPath.empty())
			return false;
		logf("dlcpatch: filling %s", rpfPath.c_str());
		std::vector<uint8_t> bin;
		{
			std::ifstream in(binPath, std::ios::binary);
			bin.assign(std::istreambuf_iterator<char>(in), {});
		}
		if (bin.size() < 8 || std::memcmp(bin.data(), "GTMTX1", 6) != 0)
		{
			logf("dlcpatch: textures.bin is damaged; deleting it (it will be rebuilt)");
			DeleteFileA(binPath.c_str());
			return false;
		}
		FILE *f = std::fopen(rpfPath.c_str(), "r+b");
		if (!f)
		{
			logf("dlcpatch: can't open dlc.rpf for writing (is GTA already running?)");
			return false;
		}
		bool ok = false;
		do
		{
			Rpf root, props;
			if (!read_rpf(f, 0, root))
			{
				logf("dlcpatch: dlc.rpf is not an OPEN RPF7");
				break;
			}
			int pi = find_path(root, {"x64", "levels", "gta5", "props", "gtm_blocks.rpf"});
			if (pi < 0)
			{
				logf("dlcpatch: gtm_blocks.rpf not found in dlc.rpf");
				break;
			}
			long long propsBase = (long long)file_offset_blocks(root, pi) * 512;
			if (!read_rpf(f, propsBase, props))
			{
				logf("dlcpatch: gtm_blocks.rpf unreadable");
				break;
			}
			int ti = find_path(props, {"gtm_tex.ytd"});
			if (ti < 0)
			{
				logf("dlcpatch: gtm_tex.ytd not found");
				break;
			}
			uint8_t *te = &props.entries[(size_t)ti * 16];
			uint32_t ytdSize = (uint32_t)(te[2] | (te[3] << 8) | (te[4] << 16));
			long long ytdPos = propsBase + (long long)file_offset_blocks(props, ti) * 512;
			uint32_t sysFlags, gfxFlags;
			std::memcpy(&sysFlags, te + 8, 4);
			std::memcpy(&gfxFlags, te + 12, 4);
			std::vector<uint8_t> ytd(ytdSize);
			_fseeki64(f, ytdPos, SEEK_SET);
			if (fread(ytd.data(), 1, ytdSize, f) != ytdSize || std::memcmp(ytd.data(), "RSC7", 4) != 0)
			{
				logf("dlcpatch: gtm_tex.ytd header bad");
				break;
			}
			size_t bodyLen = 0;
			void *body = tinfl_decompress_mem_to_heap(ytd.data() + 16, ytd.size() - 16, &bodyLen, 0);
			size_t expect = size_from_flags(sysFlags) + size_from_flags(gfxFlags);
			if (!body || bodyLen != expect)
			{
				logf("dlcpatch: ytd body %u bytes, expected %u", (unsigned)bodyLen, (unsigned)expect);
				mz_free(body);
				break;
			}
			// write every texture's pixels at its recorded offset
			size_t p = 8;
			int n = 0;
			bool bad = false;
			while (p + 8 <= bin.size())
			{
				uint32_t off, size;
				std::memcpy(&off, &bin[p], 4);
				std::memcpy(&size, &bin[p + 4], 4);
				p += 8;
				if (p + size > bin.size() || (size_t)off + size > bodyLen)
				{
					bad = true;
					break;
				}
				std::memcpy((uint8_t *)body + off, &bin[p], size);
				p += size;
				n++;
			}
			if (bad || !n)
			{
				logf("dlcpatch: textures.bin doesn't fit this dlc.rpf (wrong version?); deleting it");
				mz_free(body);
				std::fclose(f);
				f = nullptr;
				DeleteFileA(binPath.c_str());
				return false;
			}
			size_t packedLen = 0;
			void *packed = tdefl_compress_mem_to_heap(body, bodyLen, &packedLen,
			                                          tdefl_create_comp_flags_from_zip_params(6, -15, MZ_DEFAULT_STRATEGY));
			mz_free(body);
			if (!packed || packedLen + 16 >= 0xFFFFFF)
			{
				logf("dlcpatch: compression failed");
				mz_free(packed);
				break;
			}
			uint32_t newSize = (uint32_t)(packedLen + 16);
			// the ytd is the last data: rewrite it, pad to 512, then fix the two sizes and trim the file
			_fseeki64(f, ytdPos + 16, SEEK_SET);
			fwrite(packed, 1, packedLen, f);
			mz_free(packed);
			uint32_t padded = (newSize + 511) & ~511u;
			std::vector<uint8_t> zeros(padded - newSize, 0);
			if (!zeros.empty())
				fwrite(zeros.data(), 1, zeros.size(), f);
			te[2] = newSize & 0xFF, te[3] = (newSize >> 8) & 0xFF, te[4] = (newSize >> 16) & 0xFF;
			_fseeki64(f, propsBase + 16 + (long long)ti * 16, SEEK_SET);
			fwrite(te, 1, 16, f);
			uint32_t propsSize = (uint32_t)(ytdPos + padded - propsBase);
			uint8_t *pe = &root.entries[(size_t)pi * 16];
			std::memcpy(pe + 8, &propsSize, 4); // binary entry: uncompressed size (stored, FileSize stays 0)
			_fseeki64(f, 16 + (long long)pi * 16, SEEK_SET);
			fwrite(pe, 1, 16, f);
			std::fflush(f);
			long long end = ytdPos + padded;
			_chsize_s(_fileno(f), end);
			ok = true;
			logf("dlcpatch: wrote %d textures, gtm_tex.ytd %u -> %u bytes", n, ytdSize, newSize);
		} while (false);
		if (f)
			std::fclose(f);
		if (ok)
		{
			std::string st = stamp(rpfPath) + "\n";
			std::ofstream(g_dataDir + "dlc_ready.txt") << st;
			DeleteFileA(binPath.c_str());
		}
		return ok;
	}
}
