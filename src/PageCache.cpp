#include "PageCache.h"

#include <cassert>
#include <cstring>  // std::memset
#include <iostream> // 可选：调试日志

namespace mempool
{
/* 构成单例 */
PageCache& PageCache::getInstance() {
    static PageCache pc;
    return pc;
}

/* 从操作系统请求整段页内存（对齐到页大小） */
void* PageCache::systemAllocPages(std::size_t numPages) {
    std::size_t bytes = numPages * kPageSize;
    /* C++ 11 所有平台的统一接口 */
    void* ptr = ::operator new(bytes, std::align_val_t{kPageSize}, std::nothrow);
    if (!ptr) throw std::bad_alloc();

    systemBases_.insert(ptr);
    return ptr;
}

/* 分配 numPages 个连续页，返回首地址（对齐至 kPageSize） */
void* PageCache::allocateSpan(std::size_t numPages) {
    if (numPages == 0) numPages = 1;

    std::lock_guard<std::mutex> lg(mutex_);

    /* 找第一个 >= numPages 的空闲 span */
    auto it = freeSpans_.lower_bound(numPages);
    /* 找得到的话 */
    if (it != freeSpans_.end()) {
        Span* span = it->second;
        assert(span && span->numPages >= numPages);

        /* 精确匹配 —— 直接取整段 */
        if (span->numPages == numPages) {
            void* addr = span->pageAddr;
            eraseSpan(span);
            totalFreePages_ -= numPages;
            activeSpans_.emplace(addr, numPages);
            return addr;
        }

        /* 较大 span —— 拆分：前半返回，后半继续留在空闲表 */
        void* addr = span->pageAddr;
        std::size_t remainPages = span->numPages - numPages;
        void* remainAddr = static_cast<char*>(span->pageAddr) + numPages * kPageSize;

        span->pageAddr = remainAddr;
        span->numPages = remainPages;

        /* 更新 size-map：先删再插入新的 key */
        freeSpans_.erase(it);
        freeSpans_.emplace(remainPages, span);

        /* 更新 addr-map：重新定位键 */
        addrSpanMap_.erase(addr);
        addrSpanMap_.emplace(remainAddr, span);

        totalFreePages_ -= numPages;
        activeSpans_.emplace(addr, numPages);

        return addr;
    }

    void* addr = systemAllocPages(numPages);
    activeSpans_.emplace(addr, numPages);
    return addr;
}

/* 归还 span */
void PageCache::freeSpan(void* addr, std::size_t numPages) {
    if (!addr || numPages == 0) return;

    std::lock_guard<std::mutex> lg(mutex_);

    // 将 span 从活跃表移除（若重复 free 会自动忽略）
    activeSpans_.erase(addr);

    Span* span = new Span(addr, numPages);
    mergeWithNeighbors(span); // 内部会把合并后的 span 插入两张 map

    totalFreePages_ += span->numPages;
    releaseIfExcess();
}

// ────────────────────────────────────────────────────────────
// ~PageCache
// ────────────────────────────────────────────────────────────
PageCache::~PageCache() {
    for (auto& kv : freeSpans_) {
        Span* node = kv.second;
        while (node) {
            Span* nxt = node->next;
            delete node; // 回收 node
            node = nxt;
        }
    }
    /* 最后统一把所有系统基址释放一次 */
    for (void* base : systemBases_) {
#if __cpp_aligned_new >= 201606
        ::operator delete(base, std::align_val_t{kPageSize});
#else
        std::free(base);
#endif
    }
    systemBases_.clear();
}

/* ────────────────────────────────────────────────────────────
 * 辅助：插入 / 删除 / 合并
 * ────────────────────────────────────────────────────────────*/
/*──────────── 1) insertSpan ────────────*/
void PageCache::insertSpan(Span* span) {
    auto it = freeSpans_.find(span->numPages);
    if (it == freeSpans_.end()) {
        span->next = nullptr;
        freeSpans_.emplace(span->numPages, span);
    } else {
        span->next = it->second;
        it->second = span;
    }
    addrSpanMap_.emplace(span->pageAddr, span);
}

/*──────────── 2) eraseSpan ────────────*/
void PageCache::eraseSpan(Span* span) {
    /* 先从 size-map 链表里摘除 */
    auto it = freeSpans_.find(span->numPages);
    if (it != freeSpans_.end()) {
        Span* head = it->second;
        if (head == span) {
            it->second = head->next;
            if (it->second == nullptr) freeSpans_.erase(it); // 链空，删整 key
        } else {    // 相当于从链表中间删除节点
            Span* prev = head;
            while (prev && prev->next != span)
                prev = prev->next;
            if (prev) prev->next = span->next;
        }
    }
    /* 再从 addr-map 删除 */
    addrSpanMap_.erase(span->pageAddr);
    delete span;
}

// ？？？
// 这个怎么没有把span自己从addr的map里拿走？可能是本来就没在里面。
/*──────────── 3) mergeWithNeighbors ────────────*/
void PageCache::mergeWithNeighbors(Span*& span) {
    /* 向前合并 */
    auto itPrev = addrSpanMap_.lower_bound(span->pageAddr);
    if (itPrev != addrSpanMap_.begin()) {
        --itPrev;
        Span* prev = itPrev->second;
        char* prevEnd = static_cast<char*>(prev->pageAddr) + prev->numPages * kPageSize;
        if (prevEnd == span->pageAddr) {
            eraseSpan(prev); // 抽出 prev
            span->pageAddr = prev->pageAddr;
            span->numPages += prev->numPages;
        }
    }

    /* 向后合并（重新 upper_bound，因为地址可能变） */
    auto itNext = addrSpanMap_.upper_bound(span->pageAddr);
    if (itNext != addrSpanMap_.end()) {
        Span* next = itNext->second;
        char* spanEnd = static_cast<char*>(span->pageAddr) + span->numPages * kPageSize;
        if (spanEnd == next->pageAddr) {
            eraseSpan(next);
            span->numPages += next->numPages;
        }
    }

    /* 将更新后的 span 重新放入 size-map（addr-map 在下面函数中已重新插入） */
    insertSpan(span);
}

void PageCache::releaseIfExcess() {
    while (totalFreePages_ > kReleaseThresholdPages && !freeSpans_.empty()) {
        auto itBiggest = std::prev(freeSpans_.end());
        Span* span = itBiggest->second;

        // 只有当 span->pageAddr 正好是系统基址才真正释放
        if (systemBases_.count(span->pageAddr)) {
            void* base = span->pageAddr;
            std::size_t pages = span->numPages;

            eraseSpan(span);              // 从表里摘掉
            systemBases_.erase(base);

#if __cpp_aligned_new >= 201606
            ::operator delete(base, std::align_val_t{kPageSize});
#else
            std::free(base);
#endif
            totalFreePages_ -= pages;
        } else {
            // 不是基址：跳过，避免浪费
            // 可选择 break; 或继续找下一个较大 span
            break;
        }
    }
}

/* 若空闲页总量过大，则把最大的 span 直接释放给系统 */
void PageCache::releaseIfExcess() {
    while (totalFreePages_ > kReleaseThresholdPages && !freeSpans_.empty()) {
        auto itBiggest = std::prev(freeSpans_.end());
        Span* span = itBiggest->second;

        // 只有当 span->pageAddr 正好是系统基址才真正释放
        void* addr = span->pageAddr; // 先保存，等会儿用
        std::size_t pages = span->numPages;

        eraseSpan(span); // 从两张 map（size-map 和 addr-map）删并 delete span （只删 meta）

        if (systemBases_.erase(addr)) { // 基址：物理回收
#if __cpp_aligned_new >= 201606
            ::operator delete(addr, std::align_val_t{kPageSize});
#else
            std::free(addr);
#endif
            totalFreePages_ -= pages;
        } else {
            /* 只是从空闲表剔除，不物理 free，但逻辑空闲页数仍减少 */
            totalFreePages_ -= pages;
        }
    }
}

} // namespace mempool
