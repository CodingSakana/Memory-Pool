#pragma once
/**
 * class PageCache  — 以页为粒度的全局级分配器
 *  func:
 *      allocateSpan(numPages)       — 向系统申请或复用一段连续页
 *      freeSpan(addr, numPages)     — 将页段归还（目前 CentralCache 未用到，可留接口）
 *
 * struct Span      — Span 信息结构体
 */
#include <cstddef>
#include <cstdint>
#include <cstdlib> // aligned_alloc
#include <map>
#include <mutex>
#include <new>
#include <unordered_map>
#include <unordered_set>

#include "Common.h" // kPageSize

namespace mempool
{

/** 表示一段连续的物理页：起始地址 + 页数 + 指向同 size 桶中下一段的链指针 */
struct Span {
    void* pageAddr{nullptr}; // 该 span 对应的起始页地址（已对齐至 kPageSize）
    std::size_t numPages{0}; // 该 span 包含的页数
    Span* next{nullptr};     // 同一桶中下一段 span

    Span() = default; // 默认构造函数

    /* 构造一个包含 addr 开始、pages 页数的 Span */
    Span(void* addr, std::size_t pages) : pageAddr(addr), numPages(pages) {}
};

class PageCache {
public:
    /** 单例 */
    static PageCache& getInstance();

    /** 分配 numPages 个连续页，返回首地址（对齐至 kPageSize） */
    void* allocateSpan(std::size_t numPages);

    /** 归还 span */
    void freeSpan(void* addr, std::size_t numPages);

    /** 调试：空闲总页数 */
    std::size_t freePages() const noexcept { return totalFreePages_; }

    /* 超过此空闲页阈值（页数）时，自动释放回系统；默认 16K 页（约 64 MB） */
    static constexpr std::size_t kReleaseThresholdPages = 16 * 1024; // 64 MB (4 K 页)
    /* 记录所有系统级别实际分配的首地址，析构时统一释放 */
    static std::unordered_set<void*> systemBases_;

private:
    PageCache() = default;
    ~PageCache();

    PageCache(const PageCache&) = delete;
    PageCache& operator=(const PageCache&) = delete;

    /** 从操作系统请求整段页内存（对齐到页大小） */
    static void* systemAllocPages(std::size_t numPages);

    /* 按页数升序的 size → Span*（链表）的 map */
    std::map<std::size_t, Span*> freeSpans_;

    /* 按地址升序的空闲 addr → Span*（链表）的 map，用于相邻合并 */
    std::map<void*, Span*> addrSpanMap_;

    /* 全局互斥保护 */
    std::mutex mutex_;

    /* 统计空闲总页数；超阈值时把大块释放回系统 */
    std::size_t totalFreePages_{0};

    /* ---------- 内部辅助 ---------- */
    void insertSpan(Span* span);          // 插入两张 map
    void eraseSpan(Span* span);           // 双 map 都删
    void mergeWithNeighbors(Span*& span); // 相邻页连续则合并
    void releaseIfExcess();               // 当 free 页太多时回收

    /* 活跃表：所有已借出（尚未 freeSpan）的 span。 span.addr → numPages */
    std::unordered_map<void*, std::size_t> activeSpans_;
};

} // namespace mempool
