#include "log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>

static FILE *g_log = nullptr;
static char g_path[MAX_PATH] = "GrandTheftMinecraft.log";

void log_open(const std::string &path)
{
	if (!g_log)
	{
		g_log = std::fopen(path.c_str(), "w");
		std::snprintf(g_path, sizeof g_path, "%s", path.c_str());
	}
}

const char *log_path() { return g_path; }

void logf(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	SYSTEMTIME t;
	GetLocalTime(&t);
	if (!g_log)
		g_log = std::fopen("GrandTheftMinecraft.log", "w");
	if (g_log)
	{
		std::fprintf(g_log, "[%02d:%02d:%02d.%03d] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, buf);
		std::fflush(g_log);
	}
}
