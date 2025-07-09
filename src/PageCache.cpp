#include "PageCache.h"

#include <cassert>
#include <cstring>  // std::memset
#include <iostream> // 可选：调试日志

namespace mempool
{
/* 静态成员唯一定义 */
std::unordered_set<void*> PageCache::systemBases_;

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

/* ~PageCache */
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
        } else {                                             // 相当于从链表中间删除节点
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

/*──────────── 3) mergeWithNeighbors ────────────*/
void PageCache::mergeWithNeighbors(Span*& span) {
    /* ---------- 向前合并 ---------- */
    auto itPrev = addrSpanMap_.lower_bound(span->pageAddr);
    if (itPrev != addrSpanMap_.begin()) {
        --itPrev;
        Span* prev = itPrev->second;
        char* prevEnd = static_cast<char*>(prev->pageAddr) + prev->numPages * kPageSize;
        if (prevEnd == span->pageAddr) {
            void*       prevAddr  = prev->pageAddr;
            std::size_t prevPages = prev->numPages;

            totalFreePages_ -= prevPages;   // ★ 从全局计数先扣掉
            eraseSpan(prev);                //   再删除 prev

            span->pageAddr  = prevAddr;
            span->numPages += prevPages;
        }
    }

    /* ---------- 向后合并 ---------- */
    auto itNext = addrSpanMap_.upper_bound(span->pageAddr);
    if (itNext != addrSpanMap_.end()) {
        Span* next = itNext->second;
        char* spanEnd = static_cast<char*>(span->pageAddr) + span->numPages * kPageSize;
        if (spanEnd == next->pageAddr) {
            std::size_t nextPages = next->numPages;

            totalFreePages_ -= nextPages;   // ★ 同理，先扣
            eraseSpan(next);

            span->numPages += nextPages;
        }
    }

    /* 把合并后的 span 重新挂回空闲表 */
    insertSpan(span);
}


/* 若空闲页总量过大，则把最大的 span 直接释放给系统 */
void PageCache::releaseIfExcess() {
    // 只要逻辑空闲页超标，就尝试回收
    while (totalFreePages_ > kReleaseThresholdPages && !freeSpans_.empty()) {
        // 从最大 span 开始往前找
        auto it = freeSpans_.end();
        bool didFree = false;

        // 向前迭代：注意 it 最初是 end()，要 --it
        while (it != freeSpans_.begin()) {
            --it;  // it 现在指向 freeSpans_ 中最大的一个元素
            Span* span = it->second;
            void* base = span->pageAddr;
            if (systemBases_.count(base)) {
                // 找到真正的系统基址，执行释放
                std::size_t pages = span->numPages;
                eraseSpan(span);             // 从 freeSpans_/addrSpanMap_ 中删
                systemBases_.erase(base);    // 从基址集合中删

    #if __cpp_aligned_new >= 201606
                ::operator delete(base, std::align_val_t{kPageSize});
    #else
                std::free(base);
    #endif
                totalFreePages_ -= pages;
                didFree = true;
                break;  // 本次循环结束，重新从尾部开始新一轮回收
            }
            // 否则跳过这个 span，继续往前找
        }

        if (!didFree) {
            // 在整个 freeSpans_ 里都没有找到可释放的基址，退出
            break;
        }
        // 如果 didFree == true，外层 while 会根据新 totalFreePages_ 决定是否继续
    }
}


} // namespace mempool
