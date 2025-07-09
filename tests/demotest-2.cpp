#include "MemoryPool.h"
#include <vector>

int main() {
    std::vector<void*> addrList;
    addrList.emplace_back(mempool::MemoryPool::allocate(8 * 1024));
    for (int i = 5; i > 0; --i) {
        addrList.emplace_back(mempool::MemoryPool::allocate(i * 8));
    }
    return 0;
}