#include <cassert>
#include "../original_navigation.h"
using namespace OriginalNavigation;
int main() {
    assert(!ReturnToFace(0, false, false, 4999999));
    assert(ReturnToFace(0, false, false, 5000000));
    assert(!ReturnToFace(0, true, false, 6000000));
    assert(!ReturnToFace(0, false, true, 6000000));
    for (int app = 1; app <= 13; app++)
        assert(!ReturnToFace(app, false, false, 60000000));
    assert(HeaderParent(12) == 2 && HeaderParent(13) == 2);
    assert(HeaderParent(2) == 1 && HeaderParent(1) == 0);
    assert(SwipePage(5, true) == 0 && SwipePage(0, false) == 5);
    for (int p = 0; p < 6; p++)
        assert(SwipePage(SwipePage(p, true), false) == p);
}
