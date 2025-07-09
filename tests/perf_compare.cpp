/******************************************************************
 * perf_compare_big.cpp  —  放大规模 & 并发，让 ThreadCache 优势突出
 ******************************************************************/
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include <barrier>
#include <random>
#include "MemoryPool.h"

using clk = std::chrono::high_resolution_clock;
using ms  = std::chrono::duration<double, std::milli>;

inline void* palloc(std::size_t n){ return mempool::MemoryPool::allocate(n);}
inline void  pfree (void* p){ mempool::MemoryPool::deallocate(p);}

inline void* nalloc(std::size_t n){ return ::operator new(n, std::nothrow);}
inline void  nfree (void* p){ ::operator delete(p);}

double bench_ST(std::size_t nIter,
                std::size_t size,
                auto alloc, auto fr)
{
    auto t0 = clk::now();
    for(size_t i=0;i<nIter;++i){ void* p=alloc(size); fr(p);}
    return ms(clk::now()-t0).count();
}

double bench_ST_mixed(std::size_t nIter, auto alloc, auto fr)
{
    std::mt19937 rng(1); std::uniform_int_distribution<int> dist(8,256);
    auto t0=clk::now();
    for(size_t i=0;i<nIter;++i){ void* p=alloc(dist(rng)); fr(p);}
    return ms(clk::now()-t0).count();
}

double bench_MT(int thr, std::size_t perThr,
                std::size_t size,
                auto alloc, auto fr)
{
    std::barrier bar(thr);
    auto t0 = clk::now();
    std::vector<std::thread> v;
    for(int t=0;t<thr;++t){
        v.emplace_back([&,t]{
            bar.arrive_and_wait();
            for(size_t i=0;i<perThr;++i){void* p=alloc(size); fr(p);}
        });
    }
    for(auto&th:v) th.join();
    return ms(clk::now()-t0).count();
}

int main()
{
    constexpr size_t N_SMALL = 1'000'000;
    constexpr size_t N_MIX   =   200'000;
    int    THR     = 8;
    constexpr size_t PER_T   = 200'000;

    printf("===== MemoryPool vs new/delete (bigger load) =====\n");

    // --- Small ST ---
    printf("Small allocations 32B × %zu:\n", N_SMALL);
    printf("MemoryPool : %.2f ms\n",
           bench_ST(N_SMALL,32,palloc,pfree));
    printf("New/Delete : %.2f ms\n\n",
           bench_ST(N_SMALL,32,nalloc,nfree));

    // --- Multi-thread ---
    printf("%d-thread concurrent 64B × %zu each:\n", THR, PER_T);
    printf("MemoryPool : %.2f ms\n",
           bench_MT(THR,PER_T,64,palloc,pfree));
    printf("New/Delete : %.2f ms\n\n",
           bench_MT(THR,PER_T,64,nalloc,nfree));

    // --- Mixed ST ---
    printf("Mixed size 8-256B × %zu:\n", N_MIX);
    printf("MemoryPool : %.2f ms\n",
           bench_ST_mixed(N_MIX,palloc,pfree));
    printf("New/Delete : %.2f ms\n", 
           bench_ST_mixed(N_MIX,nalloc,nfree));
}
