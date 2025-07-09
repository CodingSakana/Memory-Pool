#pragma once
/**
 * class ThreadCache — 线程独享的内存分配器
 *  func:
 *      allocate(size)   — 先查本地空闲链；不够则从 CentralCache 拉批量
 *      deallocate(ptr)  — 解析 BlockHeader 获取大小后挂回本地链
 *                         当本地链过长时，回收一部分给 CentralCache
 */
#include <array>
#include <cstddef>

#include "CentralCache.h" // CentralCache::fetchRange / returnRange
#include "Common.h"       // BlockHeader / SizeClass / kFreeListNum …

namespace mempool
{

class ThreadCache {
public:
    /** 当前线程唯一实例 */
    static ThreadCache& getInstance();

    /** 分配 size 字节：返回用户区域首地址 */
    void* allocate(std::size_t size);

    /** 归还内存：无需再传 size */
    void deallocate(void* ptr);

private:
    ThreadCache();
    ~ThreadCache() = default;

    ThreadCache(const ThreadCache&) = delete;
    ThreadCache& operator=(const ThreadCache&) = delete;

    /** 当本地空链为空时，从 CentralCache 批量抓取 */
    void* fetchFromCentralCache(std::size_t index);

    /** 当本地空链过长时，将一部分区块归还给 CentralCache */
    void returnToCentralCache(BlockHeader* start, std::size_t index);

    /** 根据区块大小决定一次批量抓取多少块 */
    static std::size_t batchNumForSize(std::size_t bytes) noexcept;

    /** 判断该 index 的空链是否需要回收给 CentralCache */
    inline bool shouldReturnToCentralCache(std::size_t index) const noexcept {
        /** 阈值：保持链表大小不超过 batch * 16 */
        return freeListSize_[index] > batchNumForSize((index + 1) * kAlignment) * 16;
    }

    /** 每个 size-class 的空闲链表头指针 */
    std::array<BlockHeader*, kFreeListNum> freeList_{};

    /** 对应空链当前区块数量 */
    std::array<std::size_t, kFreeListNum> freeListSize_{};
};

} // namespace mempool
