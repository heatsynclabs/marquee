// The robot effect: see robot.cpp.
#pragma once
#include "matrix.h"

namespace Robot {
extern const uint32_t kCycleMs;  // one full walk, fly, walk routine
// elapsed: ms since this item started; ms: free-running clock for blinks.
void render(Matrix& m, uint32_t elapsed, uint32_t ms);
}  // namespace Robot
