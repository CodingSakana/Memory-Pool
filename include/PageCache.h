#pragma once
#include "Common.h"
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <CompleteSpan.h>
#include <Span.h>

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

    void insertToSpanSet(std::unordered_set<CompleteSpan*>& returnedAddrList, void* addr);

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
    // 使用 map 而不是 unordered_map 是为了使用 lower_bound()
    // 按页数管理空闲span，不同页数对应不同Span链表
    std::map<size_t, Span*> freeSpans_;

    std::unordered_map<void*, Span*> spanMap_;  // 地址映射至 Span*

    std::unordered_map<void*, CompleteSpan*> addrToCompSpanMap_;    // head 映射至 complSpan

    // std::mutex mutex_;
};

} // namespace mempool