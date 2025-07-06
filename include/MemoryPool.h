#pragma once
#include "ThreadCache.h"

namespace mempool
{

class MemoryPool
{
public:
    static void* allocate(size_t size)
    {
        return ThreadCache::getInstance().allocate(size);
    }

    static void deallocate(void* userPtr)
    {
        ThreadCache::getInstance().deallocate(userPtr);
    }
};

} // namespace mempool