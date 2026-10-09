// Crash reports in gtm.log: when GTA goes down, the exception, where it happened (module + offset), the registers, the
// call stack and what the mod was doing just before (a per-frame state line and recent notes). GTA's own crash
// handler still runs afterwards (Rockstar's dump in %LOCALAPPDATA%\Rockstar Games\GTAV\CrashLogs).
// Fatal-looking first-chance exceptions inside GTA, ScriptHookV or the mod are reported too (at most a few a session):
// GTA's worker threads may crash in ways that never reach the unhandled-exception filter.
#pragma once

namespace crashlog
{
	void install();                    // call at script start, and now and then (stays first in line)
	void frame(const char *fmt, ...);  // what the mod is doing this frame (kept, printed in a report)
	void note(const char *fmt, ...);   // an event worth remembering (last 16 kept)
}
