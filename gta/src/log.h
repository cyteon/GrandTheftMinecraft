#pragma once
#include <string>

// gtm.log next to the data files; flushed every line so a crash keeps the tail.
void log_open(const std::string &path);
void logf(const char *fmt, ...);
