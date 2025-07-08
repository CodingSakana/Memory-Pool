#include "MemoryPool.h"
#include <vector>
#include <thread>
#include <cassert>
using namespace mempool;

int main()
{
    constexpr size_t N = 100'000;
    std::vector<void*> ptrs;
    ptrs.reserve(N);

    /* 单线程 allocate / free */
    for (size_t i = 0; i < N; ++i)
        ptrs.push_back(MemoryPool::allocate(24));
    for (void* p : ptrs)
        MemoryPool::deallocate(p);

    /* 多线程烟测（4×CPU）*/
    auto worker = []{
        for (int i = 0; i < 25'000; ++i)
            MemoryPool::deallocate(MemoryPool::allocate(64));
    };
    std::thread t1(worker), t2(worker), t3(worker), t4(worker);
    t1.join(); t2.join(); t3.join(); t4.join();

    return 0;
}
