#pragma once
// 0 errors only; 1 normal communication diagnostics; 2 detailed receive diagnostics.
#ifndef RPW_LOG_LEVEL
#define RPW_LOG_LEVEL 1
#endif
#define LOG_ERROR(...) Serial.printf(__VA_ARGS__)
#if RPW_LOG_LEVEL >= 1
#define LOG_INFO(...) Serial.printf(__VA_ARGS__)
#else
#define LOG_INFO(...) do {} while(0)
#endif
#if RPW_LOG_LEVEL >= 2
#define LOG_DEBUG(...) Serial.printf(__VA_ARGS__)
#else
#define LOG_DEBUG(...) do {} while(0)
#endif
