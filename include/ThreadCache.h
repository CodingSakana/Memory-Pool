#pragma once
#include "Common.h"

// ThreadCache 是每个线程独立的内存池，按大小组织空闲块链表，从 CentralCache 批量获取小块内存。

namespace mempool
{

// 线程本地缓存
class ThreadCache {
public:
    // 每个线程独立一份, 且只在首次访问时调用一次构造函数
    static ThreadCache& getInstance() {
        static thread_local ThreadCache instance;
        return instance;
    }
    // 分配内存
    void* allocate(size_t size);
    // 归还内存
    void deallocate(void* userPtr);

private:
    ThreadCache() {
        freeList_.fill(nullptr);
        freeListSize_.fill(0);
    }

    // 从中心缓存获取内存
    void fetchFromCentralCache(size_t index);
    // 归还内存到中心缓存
    void returnToCentralCache(void* start, size_t size);
    // 是否需要归还到中心缓存
    bool shouldReturnToCentralCache(size_t index);
    
    size_t getBatchNum(size_t size);

private:
    // 每个线程的自由链表数组
    std::array<void*, kFreeListSize> freeList_;
    // 不同内存大小自由链表的大小统计
    std::array<size_t, kFreeListSize> freeListSize_;
};

} // namespace mempool