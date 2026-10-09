#include "crashlog.h"
#include "log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace crashlog
{
	static char s_frame[512];
	static char s_notes[16][192];
	static DWORD s_noteTimes[16];
	static volatile LONG s_noteNext = 0;
	static LPTOP_LEVEL_EXCEPTION_FILTER s_prev = nullptr;
	static volatile LONG s_reports = 0;
	static DWORD s_mainThread = 0;
	static void *s_lastAddr[4] = {};

	void frame(const char *fmt, ...)
	{
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(s_frame, sizeof s_frame, fmt, ap);
		va_end(ap);
	}

	void note(const char *fmt, ...)
	{
		LONG i = InterlockedIncrement(&s_noteNext) - 1;
		char *dst = s_notes[i & 15];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(dst, sizeof s_notes[0], fmt, ap);
		va_end(ap);
		s_noteTimes[i & 15] = GetTickCount();
	}

	// written straight to the log file (no CRT locks: the crashing thread may not get them)
	static HANDLE s_out = INVALID_HANDLE_VALUE;
	static void out(const char *fmt, ...)
	{
		char buf[600];
		va_list ap;
		va_start(ap, fmt);
		int n = std::vsnprintf(buf, sizeof buf - 2, fmt, ap);
		va_end(ap);
		if (n < 0)
			return;
		n = n > (int)sizeof buf - 3 ? (int)sizeof buf - 3 : n;
		buf[n++] = '\r', buf[n++] = '\n';
		DWORD w;
		if (s_out != INVALID_HANDLE_VALUE)
			WriteFile(s_out, buf, n, &w, nullptr);
	}

	static void where(DWORD64 addr, char *dst, size_t size)
	{
		HMODULE m = nullptr;
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		                       (LPCSTR)addr, &m) && m)
		{
			char path[MAX_PATH] = {};
			GetModuleFileNameA(m, path, MAX_PATH);
			const char *base = std::strrchr(path, '\\');
			std::snprintf(dst, size, "%s+0x%llx", base ? base + 1 : path, (unsigned long long)(addr - (DWORD64)m));
		}
		else
			std::snprintf(dst, size, "0x%llx", (unsigned long long)addr);
	}

	static void report(EXCEPTION_POINTERS *e, const char *kind)
	{
		s_out = CreateFileA(log_path(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
		                    FILE_ATTRIBUTE_NORMAL, nullptr);
		if (s_out == INVALID_HANDLE_VALUE)
			return;
		EXCEPTION_RECORD *r = e->ExceptionRecord;
		CONTEXT *c = e->ContextRecord;
		char at[160];
		where((DWORD64)r->ExceptionAddress, at, sizeof at);
		SYSTEMTIME t;
		GetLocalTime(&t);
		out("[%02d:%02d:%02d.%03d] ===== CRASH (%s) =====", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, kind);
		out("exception 0x%08lX at %s, thread %lu%s", r->ExceptionCode, at, GetCurrentThreadId(),
		    GetCurrentThreadId() == s_mainThread ? " (the game's main / script thread)" : " (a GTA worker thread)");
		if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
			out("  %s address 0x%llx", r->ExceptionInformation[0] == 0 ? "reading" : r->ExceptionInformation[0] == 1 ? "writing" : "executing",
			    (unsigned long long)r->ExceptionInformation[1]);
		out("  rax %llx rbx %llx rcx %llx rdx %llx rsi %llx rdi %llx", c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi);
		out("  r8 %llx r9 %llx r10 %llx r11 %llx rsp %llx rbp %llx", c->R8, c->R9, c->R10, c->R11, c->Rsp, c->Rbp);
		// GTA's pool allocator crash (GTA5.exe+0x1387d00, rcx = the pool): its capacity and slot size name the pool
		// (gameconfig.xml PoolSize); layout: storage, capacity, free count, slot size, free list head
		for (DWORD64 p : {c->Rcx, c->Rbx})
			if (p && !IsBadReadPtr((void *)p, 0x30))
			{
				const DWORD64 *q = (const DWORD64 *)p;
				out("  [%llx] storage %llx capacity %llu free %llu slot %llu head %llx flags %llx", (unsigned long long)p,
				    q[0], q[1] & 0xffffffff, q[2] & 0xffffffff, q[3] & 0xffffffff, q[4], q[5]);
			}
		// the call stack, unwound with the modules' own unwind data
		CONTEXT ctx = *c;
		out("  stack:");
		for (int i = 0; i < 24 && ctx.Rip; i++)
		{
			char w[160];
			where(ctx.Rip, w, sizeof w);
			out("    %2d %s", i, w);
			DWORD64 imageBase = 0;
			PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
			if (!fn)
			{
				// a leaf function: the return address is on top of the stack
				if (ctx.Rsp == 0 || IsBadReadPtr((void *)ctx.Rsp, 8))
					break;
				ctx.Rip = *(DWORD64 *)ctx.Rsp;
				ctx.Rsp += 8;
				continue;
			}
			void *handlerData = nullptr;
			DWORD64 establisher = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
		}
		out("  mod state: %s", s_frame[0] ? s_frame : "(none yet)");
		LONG last = s_noteNext;
		DWORD now = GetTickCount();
		for (LONG i = last - 16; i < last; i++)
			if (i >= 0 && s_notes[i & 15][0])
				out("  %5.1f s ago: %s", (now - s_noteTimes[i & 15]) / 1000.0, s_notes[i & 15]);
		out("===== end of crash report =====");
		CloseHandle(s_out);
		s_out = INVALID_HANDLE_VALUE;
	}

	static LONG WINAPI unhandled(EXCEPTION_POINTERS *e)
	{
		if (InterlockedIncrement(&s_reports) <= 6)
			report(e, "unhandled");
		return s_prev ? s_prev(e) : EXCEPTION_CONTINUE_SEARCH;
	}

	static bool fatal_code(DWORD code)
	{
		return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
		       code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_STACK_OVERFLOW ||
		       code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == 0xC0000374 /* heap corruption */ ||
		       code == 0xC0000409 /* fail fast */;
	}

	static LONG WINAPI first_chance(EXCEPTION_POINTERS *e)
	{
		DWORD code = e->ExceptionRecord->ExceptionCode;
		if (!fatal_code(code))
			return EXCEPTION_CONTINUE_SEARCH;
		void *addr = e->ExceptionRecord->ExceptionAddress;
		for (void *a : s_lastAddr) // the same spot again: already reported
			if (a == addr)
				return EXCEPTION_CONTINUE_SEARCH;
		LONG n = InterlockedIncrement(&s_reports);
		if (n <= 4)
		{
			s_lastAddr[(n - 1) & 3] = addr;
			report(e, "first chance - GTA may still handle it");
		}
		return EXCEPTION_CONTINUE_SEARCH;
	}

	void install()
	{
		if (!s_mainThread)
		{
			s_mainThread = GetCurrentThreadId();
			AddVectoredExceptionHandler(1, first_chance);
		}
		LPTOP_LEVEL_EXCEPTION_FILTER cur = SetUnhandledExceptionFilter(unhandled);
		if (cur != unhandled)
			s_prev = cur; // keep whoever was there (GTA's own crash handler) in the chain
	}
}
