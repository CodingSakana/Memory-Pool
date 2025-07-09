#include <iostream>
#include <vector>
#include <random>
#include <thread>
#include <cassert>
#include <algorithm>

#include "MemoryPool.h"   // 统一接口
#include "PageCache.h"    // 直接验证 merge / freePages()

// -------------- 单线程：PageCache 相邻合并 -----------------
void test_pagecache_merge()
{
    using namespace mempool;
    auto& pc = PageCache::getInstance();

    // 确保初始空闲为 0（仅一次性测试，实际跑多轮可先记录 baseline）
    std::size_t base = pc.freePages();

    // 申请两段相邻的 4 页
    void* p1 = pc.allocateSpan(4);
    void* p2 = pc.allocateSpan(4);

    // 释放两段（先 p1 后 p2）
    pc.freeSpan(p1, 4);
    pc.freeSpan(p2, 4);

    // 理论上应合并为 8 页
    std::size_t after = pc.freePages();
    assert(after - base == 8 && "Merge failed!");

    std::cout << "[PASS] PageCache merge -> free pages +8\n";
}

// -------------- 单线程：超阈值自动回收 -----------------
void test_pagecache_release()
{
    using namespace mempool;
    auto& pc = PageCache::getInstance();
    std::size_t base = pc.freePages();

    // 连续 free 足够多页让空闲 > 阈值 (kReleaseThresholdPages = 16*1024)
    constexpr std::size_t pagesPerLoop = 1024;
    std::vector<void*> buffs;
    for (int i = 0; i < 20; ++i) {
        void* buf = pc.allocateSpan(pagesPerLoop);
        buffs.push_back(buf);
    }
    // 现在把全部 free 回去
    for (void* buf : buffs) pc.freeSpan(buf, pagesPerLoop);

    // PageCache 内部应触发回收，总空闲 <= 阈值
    std::size_t now = pc.freePages();
    assert(now - base <= PageCache::kReleaseThresholdPages &&
           "Release threshold not enforced!");
    std::cout << "[PASS] PageCache automatic release ✓\n";
}

// -------------- 多线程：ThreadCache allocate/deallocate -----------------
void worker(int id, std::mt19937& rng)
{
    using namespace mempool;
    std::uniform_int_distribution<std::size_t> dist(8, 4096);

    std::vector<void*> ptrs;
    ptrs.reserve(10'000);

    // 1. allocate
    for (int i = 0; i < 10'000; ++i) {
        std::size_t sz = dist(rng);
        ptrs.push_back(MemoryPool::allocate(sz));
    }

    std::shuffle(ptrs.begin(), ptrs.end(), rng);

    // 2. deallocate (无 size)
    for (void* p : ptrs) MemoryPool::deallocate(p);
}

void test_threadcache_mt()
{
    std::random_device rd;
    std::mt19937 rng(rd());

    std::thread t1(worker, 1, std::ref(rng));
    std::thread t2(worker, 2, std::ref(rng));
    std::thread t3(worker, 3, std::ref(rng));
    t1.join(); t2.join(); t3.join();

    std::cout << "[PASS] ThreadCache multi-thread smoke ✓\n";
}

// --------------------------- main ---------------------------
int main()
{
    test_pagecache_merge();
    test_pagecache_release();
    test_threadcache_mt();

    std::cout << "All basic tests passed!\n";
    return 0;
}
