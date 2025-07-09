// pool_vs_new_fixed.cpp
//
// 编译：g++ -std=c++20 -O3 -pthread tests/pool_vs_new_fixed.cpp -I./include src/*.cpp -o pool_vs_new
// 运行：./pool_vs_new [threads] [loops] [batch]
// 例   ：./pool_vs_new  8 400 10000   （默认值）

#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>
#include "MemoryPool.h"

using Clock = std::chrono::high_resolution_clock;
using ms    = std::chrono::duration<double, std::milli>;

struct Conf {
    std::size_t threads = 8;
    std::size_t loops   = 400;
    std::size_t batch   = 10'000;
    std::size_t size    = 128;     // 热点对象尺寸
};

template<class Alloc, class Free>
double bench(const Conf& c, Alloc alloc, Free dealloc, const char* tag)
{
    auto worker = [&](std::size_t tid) {
        std::vector<void*> v(c.batch);
        std::mt19937_64 rng{tid * 114514ULL};

        for (std::size_t l = 0; l < c.loops; ++l) {
            for (auto& p : v) p = alloc(c.size);
            std::shuffle(v.begin(), v.end(), rng);
            for (auto  p : v) dealloc(p);
        }
    };
    auto t0 = Clock::now();
    {
        std::vector<std::thread> ths;
        for (std::size_t i = 0; i < c.threads; ++i)
            ths.emplace_back(worker, i);
        for (auto& t : ths) t.join();
    }
    auto t1 = Clock::now();
    double elapsed = ms(t1 - t0).count();
    std::cout << tag << " : " << elapsed << " ms\n";
    return elapsed;
}

int main(int argc, char* argv[])
{
    Conf cfg;
    if (argc > 1) cfg.threads = std::stoull(argv[1]);
    if (argc > 2) cfg.loops   = std::stoull(argv[2]);
    if (argc > 3) cfg.batch   = std::stoull(argv[3]);

    std::cout << "==== "
              << cfg.threads << " threads × "
              << cfg.loops   << " loops × "
              << cfg.batch   << " objs @ "
              << cfg.size    << "B ====\n";

    /* 预热一次，剔除首次 mmap 影响 */
    bench(cfg,
          [](std::size_t s){ return mempool::MemoryPool::allocate(s); },
          [](void* p){ mempool::MemoryPool::deallocate(p); },
          "MemoryPool-warmup");

    std::cout << "--------- 正式计时 ---------\n";
    double mp = bench(cfg,
          [](std::size_t s){ return mempool::MemoryPool::allocate(s); },
          [](void* p){ mempool::MemoryPool::deallocate(p); },
          "MemoryPool");

    double nd = bench(cfg,
          [](std::size_t s){ return new char[s]; },
          [](void* p){ delete[] static_cast<char*>(p); },
          "new/delete");

    std::cout << "Speed-up ≈ " << nd / mp << " ×\n";
}
