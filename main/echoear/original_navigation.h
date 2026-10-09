#pragma once
#include <cstdint>
namespace OriginalNavigation {
constexpr int HeaderParent(int page) {
    if (page == 1 || page == 7 || page == 8)
        return 0;
    if (page == 12 || page == 13)
        return 2;
    return 1;
}
constexpr bool ReturnToFace(int page, bool quick_open, bool pressed, int64_t idle_us) {
    return page == 0 && !quick_open && !pressed && idle_us >= 5000000;
}
constexpr int SwipePage(int page, bool left) { return (page + (left ? 1 : 5)) % 6; }
}  // namespace OriginalNavigation
