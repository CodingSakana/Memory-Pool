/*************************************************************
 *  bench_pool_vs_new.cpp
 *  比较 MemoryPool vs 普通 new/delete（pmr::new_delete_resource）
 *************************************************************/

/*
g++ -std=c++20 -O3 -pthread tests/bench_pool_vs_new.cpp src/*.cpp -Iinclude -o bench && ./bench
*/
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory_resource>
#include <thread>
#include <vector>
#include <random>
#include <atomic>
#include <sys/resource.h>
#include <unistd.h>

#include "MemoryPool.h"

// ─────────────── util: 读取 VmRSS (kB) ───────────────
long rss_kb() {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char buf[256];
    long rss = -1;
    while (fgets(buf, sizeof(buf), f)) {
        if (sscanf(buf, "VmRSS: %ld kB", &rss) == 1) break;
    }
    fclose(f);
    return rss;
}

// ─────────────── benchmark core ───────────────
struct Result { double ns_per_op; long peak_kb; };

template <typename AllocFn, typename FreeFn>
Result run_bench(std::size_t iterations,
                 AllocFn alloc,
                 FreeFn  dealloc,
                 int threads = 1)
{
    // record initial RSS
    long rss0 = rss_kb();

    auto start = std::chrono::high_resolution_clock::now();

    std::atomic<int> ready{0};
    std::vector<std::thread> ths;
    for (int t = 0; t < threads; ++t) {
        ths.emplace_back([&, t] {
            ready.fetch_add(1);
            while (ready.load() < threads) /* spin */;

            std::mt19937 rng(t+1);
            std::uniform_int_distribution<int> dist(8, 256);

            for (std::size_t i = 0; i < iterations; ++i) {
                std::size_t sz = dist(rng);
                void* p = alloc(sz);
                dealloc(p);
            }
        });
    }
    for (auto& th : ths) th.join();

    auto end = std::chrono::high_resolution_clock::now();
    double ns = std::chrono::duration<double, std::nano>(end - start).count();
    long rss1 = rss_kb();

    Result r;
    r.ns_per_op = ns / (iterations * threads);
    r.peak_kb   = std::max<long>(rss1 - rss0, 0);
    return r;
}

// ─────────────── wrappers ───────────────
void* pool_alloc(std::size_t sz)   { return mempool::MemoryPool::allocate(sz); }
void  pool_free (void* p)          { mempool::MemoryPool::deallocate(p);      }

std::pmr::memory_resource* sysmr = std::pmr::new_delete_resource();
void* sys_alloc (std::size_t sz)  { return sysmr->allocate(sz); }
void  sys_free  (void* p)         { sysmr->deallocate(p, 0);    }

// ─────────────── pretty print ───────────────
void print_table(const char* title, const Result& sys, const Result& pool)
{
    printf("=== %s ===\n", title);
    printf("%-10s %12s %15s\n", "策略", "ns/操作", "额外峰值RSS(kB)");
    printf("%-10s %12.1f %15ld\n", "new/delete", sys.ns_per_op,   sys.peak_kb);
    printf("%-10s %12.1f %15ld\n", "MemoryPool", pool.ns_per_op, pool.peak_kb);
    printf("--------------------------------------------------\n");
}

int main()
{
    const std::size_t N_single = 1'00'00000;   // 1e8 次
    const std::size_t N_multi  = 10'000'00;    // 每线程1e7
    const int         T        = 4;

    // 单线程
    auto r_sys1  = run_bench(N_single, sys_alloc,  sys_free , 1);
    auto r_pool1 = run_bench(N_single, pool_alloc, pool_free, 1);
    print_table("单线程 1e8 次 8-256B alloc/free", r_sys1, r_pool1);

    // 多线程
    auto r_sys4  = run_bench(N_multi, sys_alloc,  sys_free , T);
    auto r_pool4 = run_bench(N_multi, pool_alloc, pool_free, T);
    print_table("4线程×1e7 次", r_sys4, r_pool4);

    printf("提示：若 MemoryPool ns/操作 更低，且峰值 RSS 更小/相近，\n"
           "则表明线程私有缓存和页级回收显著减少了系统调用与碎片。\n");
    return 0;
}
