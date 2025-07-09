#pragma once
/**
 * struct SpinLock      — 轻量自旋锁
 * 
 * class CentralCache   — 多线程共享的小对象中央缓存
 *  func:
 *      fetchBatch      — 为各 ThreadCache 批量提供 BlockHeader 链表
 *      returnBatch     — 当线程归还过多区块时，接收并缓存，在链表耗尽时向 PageCache 请求新的 span
 */
#include <array>
#include <atomic>
#include <cstddef>
#include <thread>

#include "Common.h" // BlockHeader / kFreeListNum / kAlignment / kPageSize
#include "PageCache.h"

#ifdef __x86_64__
#include <immintrin.h>
#endif

namespace mempool
{

struct SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

    void lock() noexcept {
        unsigned spins = 0;
        while (flag.test_and_set(std::memory_order_acquire)) {
#ifdef __x86_64__
            /**
             *  在这里插入 PAUSE 指令：
                （1）在 Intel/AMD CPU 上，PAUSE 会告诉处理器“我正在做忙等”，
                    有助于减少功耗并降低总线/缓存一致性流量。
                （2）在超线程（SMT）环境下，它还能让出执行资源给同核的另一个硬线程，
                    提高整体吞吐量、减少忙等对同核任务的干扰。
                （3）它是单周期指令，开销远低于 yield 或 sleep_for。
             */
            _mm_pause();
#else
            // 非 x86-64 平台无法使用 PAUSE，直接继续忙等
            ;
#endif
        }
    }

    void unlock() noexcept { flag.clear(std::memory_order_release); }
};

class CentralCache {
public:
    /** 全局唯一实例 */
    static CentralCache& getInstance();

    /**
     * 从指定 size-class 取出 ≥ batchNum 个区块（等于或足够多即可）。
     * 返回一条 BlockHeader* 单链，调用者拥有所有权。
     */
    BlockHeader* fetchBatch(std::size_t index, std::size_t batchNum);

    /** 将区块链（blockNum 个）归还给指定 size-class 的中央缓存 */
    void returnBatch(BlockHeader* start, std::size_t blockNum, std::size_t index);

private:
    CentralCache();
    ~CentralCache() = default;

    CentralCache(const CentralCache&) = delete;
    CentralCache& operator=(const CentralCache&) = delete;

    /* 向 PageCache 申请 span 并切分为 BlockHeader 链 */
    void refillFromPageCache(std::size_t index);

private:
    /* 各 size-class 的空闲链表头 */
    std::array<std::atomic<BlockHeader*>, kFreeListNum> centralFreeList_{};

    /* 对应的自旋锁 */
    std::array<SpinLock, kFreeListNum> locks_{};
};

} // namespace mempool
