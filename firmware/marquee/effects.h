// Built-in animations. Each one renders a single frame for time `ms`.
#pragma once
#include <Arduino.h>
#include "matrix.h"

namespace Effects {
int         count();
const char* name(int id);
int         find(const char* name);           // -1 when unknown
void        render(int id, Matrix& m, uint32_t ms, uint32_t color);
}  // namespace Effects
