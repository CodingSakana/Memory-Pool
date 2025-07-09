#pragma once
/**
 * class MemoryPool
 *  func:
 *      void*  allocate(std::size_t size);   — 分配内存
 *      void   deallocate(void* ptr);        — 回收内存
 */
#include "ThreadCache.h"

namespace mempool
{

class MemoryPool {
public:
    /**  分配 size 字节的对象 */
    static void* allocate(std::size_t size) { return ThreadCache::getInstance().allocate(size); }

    /** 归还内存（自动根据 BlockHeader 解析大小）*/
    static void deallocate(void* ptr) { ThreadCache::getInstance().deallocate(ptr); }
};

} // namespace mempool