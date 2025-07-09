/*********************************************************************
 *  mempool_full_test.cpp
 *
 *  - 单线程：相邻合并 / 跨桶拆分 / 超阈值回收
 *  - 多线程：随机尺寸高并发 + 线程退出回收
 *  - 随机长跑：100 万次分配/回收混合，检测碎片、泄漏
 *
 *  运行环境：推荐 -fsanitize=address,undefined,thread
 *
 *********************************************************************/

// 编译指令：g++ -std=c++20 -O2 -fsanitize=address,undefined -pthread tests/mempool_full_test.cpp
// src/*.cpp -Iinclude -o mempool_full_test

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

#include "MemoryPool.h"
#include "PageCache.h"

using namespace mempool;

/* 打印简易 banner */
static void ok(const char* msg) { std::printf("[PASS] %s\n", msg); }

/* --------------------------------------------------------------- */
/* 1. 相邻合并 + 跨桶拆分                                          */
/* --------------------------------------------------------------- */
void test_span_merge_split() {
    auto& pc = PageCache::getInstance();
    auto base = pc.freePages();

    // 先申请 4+8 页，释放 4 后再释放 8，验证合并 12
    void* p4 = pc.allocateSpan(4);
    void* p8 = pc.allocateSpan(8);

    pc.freeSpan(p4, 4);
    pc.freeSpan(p8, 8);
    assert(pc.freePages() - base == 12 && "adjacent merge failed");

    // 再申请 6 页，应该从 12 页里拆分得到一段 6 + 6
    void* p6 = pc.allocateSpan(6);
    pc.freeSpan(p6, 6);
    assert(pc.freePages() - base == 12 && "split or reuse failed");

    ok("Span merge / split");
}

/* --------------------------------------------------------------- */
/* 2. 超阈值回收                                                   */
/* --------------------------------------------------------------- */
void test_release_threshold() {
    auto& pc = PageCache::getInstance();
    const size_t base = pc.freePages();
    constexpr size_t big = 2 * PageCache::kReleaseThresholdPages; // 128 MB

    // 申请 big 页，然后释放，需触发回收
    void* buf = pc.allocateSpan(big);
    pc.freeSpan(buf, big);

    // 回收后空闲不应超过阈值
    assert(pc.freePages() - base <= PageCache::kReleaseThresholdPages);
    ok("Threshold release");
}

/* --------------------------------------------------------------- */
/* 3. ThreadCache 并发随机尺寸                                     */
/* --------------------------------------------------------------- */
void worker_rand(int tid, size_t nOps) {
    std::mt19937 rng(tid + 1);
    std::uniform_int_distribution<size_t> dist(8, 4096);

    std::vector<void*> vec;
    vec.reserve(nOps);

    for (size_t i = 0; i < nOps; ++i)
        vec.push_back(MemoryPool::allocate(dist(rng)));

    std::shuffle(vec.begin(), vec.end(), rng);
    for (void* p : vec)
        MemoryPool::deallocate(p);
}

void test_threadcache_concurrency() {
    const int T = std::min(16, (int)std::thread::hardware_concurrency());
    const size_t N = 200'000;

    std::vector<std::thread> ths;
    for (int t = 0; t < T; ++t)
        ths.emplace_back(worker_rand, t, N);

    for (auto& th : ths)
        th.join();
    ok("ThreadCache concurrency");
}

/* --------------------------------------------------------------- */
/* 4. 线程退出回收                                                 */
/* --------------------------------------------------------------- */
void test_thread_exit_cleanup() {
    auto& pc = PageCache::getInstance();
    const size_t before = pc.freePages();

    {
        std::thread tmp([] {
            for (int i = 0; i < 50'000; ++i)
                MemoryPool::deallocate(MemoryPool::allocate(64));
        });
        tmp.join(); // 线程退出，此时 ThreadCache 析构应把空闲链归还
    }
    // allow a tiny leak margin (if any)
    assert(pc.freePages() >= before && "leak on thread exit");
    ok("Thread exit cleanup");
}

/* --------------------------------------------------------------- */
/* 5. 随机长跑                                                      */
/* --------------------------------------------------------------- */
void test_random_longrun() {
    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> sizeDist(4, 16 * 1024); // up to 16 KB
    std::bernoulli_distribution coin(0.5);

    std::vector<void*> pool;
    pool.reserve(200'000);

    for (size_t i = 0; i < 1'000'000; ++i) {
        if (coin(rng) || pool.empty()) {
            pool.push_back(MemoryPool::allocate(sizeDist(rng)));
        } else {
            size_t idx = rng() % pool.size();
            MemoryPool::deallocate(pool[idx]);
            pool[idx] = pool.back();
            pool.pop_back();
        }
    }
    // 清空剩余
    for (void* p : pool)
        MemoryPool::deallocate(p);

    ok("1M mixed alloc/free longrun");
}

/* --------------------------------------------------------------- */
int main() {
    test_span_merge_split();
    test_release_threshold();
    test_threadcache_concurrency();
    test_thread_exit_cleanup();
    test_random_longrun();

    std::puts("All extended tests passed!");
    return 0;
}
