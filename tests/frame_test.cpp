#include "meshcore_frame.h"
#include <cassert>

int main() {
    mdm::Frame f;
    assert(mdm::parseFrame("150001aabbcc", f));
    assert(mdm::isDirectGroup(f, 0x01));
    const auto direct_key = f.fingerprint;
    assert(mdm::parseFrame("15011101aabbcc", f));
    assert(f.hops == 1 && !mdm::isDirectGroup(f, 0x01));
    assert(f.fingerprint == direct_key);
    assert(mdm::parseFrame("14000000000001aabbcc", f));
    assert(mdm::isDirectGroup(f, 0x01));
    assert(mdm::parseFrame("160001aabbcc", f));
    assert(!mdm::isDirectGroup(f, 0x01));
    assert(!mdm::parseFrame("150001aabbc", f));
    assert(!mdm::parseFrame("150101aabbcc", f));
    assert(!mdm::parseFrame("150001aabbcz", f));
}
