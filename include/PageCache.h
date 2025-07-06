#pragma once
#include "Common.h"
#include <map>
#include <mutex>

// PageCache 管理页级内存（span），通过 mmap 向系统申请物理页，并合并相邻空闲 span，供给 CentralCache 使用。

namespace mempool
{
class PageCache {
public:
    static const size_t PAGE_SIZE = 4096; // 页大小

    static PageCache& getInstance() {
        static PageCache instance;
        return instance;
    }

    // 分配指定页数的 span
    void* allocateSpan(size_t numPages);

    // 释放span
    void deallocateSpan(void* ptr, size_t numPages);

private:
    PageCache() = default;

    // 向系统申请内存
    void* systemAlloc(size_t numPages);

private:
    struct Span {
        void* pageAddr;  // 页起始地址
        size_t numPages; // 页数
        Span* next;      // 下一个
        Span* prev;      // 前一个
    };

    // 使用 map 而不是 unordered_map 是为了使用 lower_bound()
    // 按页数管理空闲span，不同页数对应不同Span链表
    std::map<size_t, Span*> freeSpans_;
    // 页号到span的映射，用于回收
    std::map<void*, Span*> spanMap_;
    std::mutex mutex_;
};

} // namespace mempool