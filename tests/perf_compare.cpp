/******************************************************************
 * perf_compare_sizes_formatted.cpp
 *
 * 对比 new/delete 与 MemoryPool 在 4B、16B、64B、128B 及 混合尺寸下的性能
 * 并加入：关闭 glibc tcache 路径 + 预热（warmup）
 ******************************************************************/
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <algorithm>

#include "MemoryPool.h"

using clk = std::chrono::high_resolution_clock;
using ms  = std::chrono::duration<double, std::milli>;

// MemoryPool 接口
inline void* palloc(std::size_t n){ return mempool::MemoryPool::allocate(n); }
inline void  pfree (void* p){ mempool::MemoryPool::deallocate(p);   }

// new/delete 包装
inline void* nalloc(std::size_t n){ return ::operator new(n, std::nothrow); }
inline void  nfree (void* p){ ::operator delete(p);                   }

// 单线程固定大小
template<typename Alloc, typename Free>
double bench_ST(std::size_t N, std::size_t size, Alloc A, Free F) {
    auto t0 = clk::now();
    for (std::size_t i = 0; i < N; ++i) {
        void* p = A(size);
        F(p);
    }
    return ms(clk::now() - t0).count();
}

// 多线程固定大小
template<typename Alloc, typename Free>
double bench_MT(int thr, std::size_t perThr, std::size_t size, Alloc A, Free F) {
    std::atomic<int> ready{0};
    auto t0 = clk::now();
    std::vector<std::thread> threads;
    threads.reserve(thr);
    for (int i = 0; i < thr; ++i) {
        threads.emplace_back([&,i]{
            ready.fetch_add(1);
            while (ready.load() < thr) std::this_thread::yield();
            for (std::size_t j = 0; j < perThr; ++j) {
                void* p = A(size);
                F(p);
            }
        });
    }
    for (auto& t : threads) t.join();
    return ms(clk::now() - t0).count();
}

// 单线程混合尺寸
template<typename Alloc, typename Free>
double bench_ST_mixed(std::size_t N, Alloc A, Free F) {
    std::mt19937 rng(1);
    std::uniform_int_distribution<int> dist(8,256);
    auto t0 = clk::now();
    for (std::size_t i = 0; i < N; ++i) {
        void* p = A(dist(rng));
        F(p);
    }
    return ms(clk::now() - t0).count();
}

int main() {
    // ──────────────────────────────────────────────
    // 1) 关闭 glibc tcache 路径（Linux/glibc 专属）
    //    让 malloc/free 更多地走 mmap / brk 而非 tcache fastbin
    // ──────────────────────────────────────────────
    setenv("MALLOC_MMAP_THRESHOLD_", "1", 1);
    setenv("MALLOC_TRIM_THRESHOLD_", "1", 1);
    setenv("MALLOC_TOP_PAD_", "1", 1);

    // ──────────────────────────────────────────────
    // 2) 预热（warmup）：跑几千次小分配，确保
    //    ThreadCache 单例、TLS、PageCache 都初始化完毕
    // ──────────────────────────────────────────────
    for (int i = 0; i < 10000; ++i) {
        void* p = palloc(128); pfree(p);
        void* q = nalloc(128); nfree(q);
    }

    constexpr std::size_t N_ST  = 1'000'000;
    constexpr int         THR   = 8;
    constexpr std::size_t N_MT  = 250'000;

    std::vector<std::size_t> sizes = {4, 16, 64, 128};

    printf("===== MemoryPool vs new/delete =====\n\n");

    for (auto sz : sizes) {
        // —— 单线程 —— 
        double mp_st = bench_ST(N_ST, sz, palloc, pfree);
        double nd_st = bench_ST(N_ST, sz, nalloc, nfree);
        printf("%zuB Single %zu:\n", sz, N_ST);
        printf("MemoryPool : %.2f ms\n", mp_st);
        printf("New/Delete : %.2f ms\n", nd_st);
        printf("Speedup     : %.2fx\n\n", nd_st / mp_st);

        // —— 多线程 —— 
        double mp_mt = bench_MT(THR, N_MT, sz, palloc, pfree);
        double nd_mt = bench_MT(THR, N_MT, sz, nalloc, nfree);
        printf("%d-thread ×%zu each:\n", THR, N_MT);
        printf("MemoryPool : %.2f ms\n", mp_mt);
        printf("New/Delete : %.2f ms\n", nd_mt);
        printf("Speedup     : %.2fx\n\n", nd_mt / mp_mt);

        // —— MemoryPool ST vs MT 比较 —— 
        printf("MemoryPool MT vs ST (%zuB):\n", sz);
        printf("Single     : %.2f ms\n", mp_st);
        printf("Multi(%dT) : %.2f ms\n", THR, mp_mt);
        printf("ST / MT    : %.2fx\n\n", mp_st > 0 ? mp_st/mp_mt : 0.0);
    }

    // —— 混合尺寸 —— 
    double mp_mix = bench_ST_mixed(N_ST, palloc, pfree);
    double nd_mix = bench_ST_mixed(N_ST, nalloc, nfree);
    printf("Mixed size 8-256B × %zu:\n", N_ST);
    printf("MemoryPool : %.2f ms\n", mp_mix);
    printf("New/Delete : %.2f ms\n", nd_mix);
    printf("Speedup     : %.2fx\n", nd_mix / mp_mix);

    return 0;
}
