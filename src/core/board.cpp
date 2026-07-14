// board.cpp — board-level constants that need a translation unit.
#include "board.h"

// All board constants are declared `const`/constexpr in board.h; this
// translation unit exists so future non-inline board helpers have a home
// and to anchor the symbols for any module that takes their address.