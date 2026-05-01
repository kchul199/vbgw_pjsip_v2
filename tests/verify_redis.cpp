#include "../src/engine/CapacityManager.h"
#include <iostream>

int main() {
    auto& cm = CapacityManager::getInstance();
    if (!cm.init("tcp://127.0.0.1:6379")) return 1;
    auto l1 = cm.leaseSlot("test", 10);
    if (l1.success) cm.releaseSlot("test", l1.slot_id);
    return 0;
}
