#include "MemoryPool.h"
#include <thread>
#include <vector>
#include <random>

int main() {
    constexpr size_t kThreads = 1;
    constexpr size_t kOps     = 1;

    std::vector<std::thread> vt;
    std::atomic<size_t> failCnt{0};

    for (size_t t = 0; t < kThreads; ++t) {
        vt.emplace_back([&] {
            std::mt19937_64 rng{std::random_device{}()};
            std::uniform_int_distribution<size_t> dist(1, 2048); // 1~2 KB

            std::vector<void*> buf;
            buf.reserve(1024);

            for (size_t i = 0; i < kOps; ++i) {
                if (buf.empty() || dist(rng) & 1) {   // 50% malloc
                    size_t sz = dist(rng);
                    void* p = mempool::MemoryPool::allocate(sz);
                    if (!p) failCnt++;
                    else    buf.push_back(p);
                } else {                              // 50% free
                    size_t idx = rng() % buf.size();
                    mempool::MemoryPool::deallocate(buf[idx]);
                    buf.erase(buf.begin() + idx);
                }
            }
            // 清理剩余
            for (void* p : buf) mempool::MemoryPool::deallocate(p);
        });
    }
    for (auto& th : vt) th.join();

    printf("All threads joined, fails=%zu\n", failCnt.load());
    return 0;
}
