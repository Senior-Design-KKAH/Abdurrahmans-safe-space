#pragma once
#include <stdint.h>
#include "logmel.h"

constexpr unsigned windowCount = 10;
constexpr unsigned samplesPerWindow = LOGMEL_WINDOW_SAMPLES;

bool captureAndScore(float& mean);
bool initializeModel();
bool scoreWindow(const int16_t* pcm, float& score);
