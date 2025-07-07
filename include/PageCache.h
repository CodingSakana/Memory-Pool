#pragma once
#include "Common.h"
#include <map>
#include <mutex>

// PageCache 管理页级内存（span），通过 mmap 向系统申请物理页，并合并相邻空闲 span，供给
// CentralCache 使用。

namespace mempool
{
class PageCache {
public:
    // static const size_t PAGE_SIZE = 4096; // 页大小

    static PageCache& getInstance() {
        static PageCache instance;
        return instance;
    }

    // 分配指定页数的 Span 给 CentralCache，返回的是 Span 的地址
    void* allocateSpanToCentral(size_t requiredMemSize);

    // 释放span todo
    void deallocateSpan(void* ptr, size_t numPages);

private:
    PageCache() = default;
    ~PageCache() = default;

    // 禁止拷贝构造和赋值
    PageCache(const PageCache&) = delete;
    PageCache& operator=(const PageCache&) = delete;

    // 禁止移动构造和赋值
    PageCache(PageCache&&) = delete;
    PageCache& operator=(PageCache&&) = delete;

    // 向系统申请内存
    void* systemAlloc(size_t numPages);

private:
    struct Span;

    struct CompeleteSpan {
        std::map<void*, Span*> spanMap_;
    };

    struct Span {
        void* pageAddr;  // 起始地址
        size_t numPages; // 页数
        Span* next;      // 后一个 Span
        size_t compSpanIndex;
        bool isUsed;

        Span() : pageAddr(nullptr), numPages(0), next(nullptr), compSpanIndex(0), isUsed(false) {};
        Span(void* pageAddr_, size_t numPages_, Span* next_, size_t compSpanIndex_, bool isUsed_)
            : pageAddr(pageAddr_), numPages(numPages_), next(next_), compSpanIndex(compSpanIndex_), isUsed(isUsed_) {};
    };

    // 使用 map 而不是 unordered_map 是为了使用 lower_bound()
    // 按页数管理空闲span，不同页数对应不同Span链表
    std::map<size_t, Span*> freeSpans_;

    // 地址到span的映射，用于回收
    std::map<void*, Span*> spanMap_;
    std::array<CompeleteSpan, kMaxMemPoolSize / kMaxBytes> completeSpanList_;

    // std::mutex mutex_;
};

} // namespace mempool