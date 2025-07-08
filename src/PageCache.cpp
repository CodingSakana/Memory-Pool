#include "PageCache.h"
#include <cstring>
#include <sys/mman.h>

namespace mempool
{

void* PageCache::allocateSpanToCentral(size_t memSize) {
    // 计算需求页数：向上取整，且至少 kMinSpanPages (8)
    size_t needPages = (memSize + kPageSize - 1) / kPageSize;
    if (needPages < kMinSpanPages) needPages = kMinSpanPages;

    // 在 freeSpans_ 中找 ≥ needPages 的最小桶
    while (true) {
        auto it = freeSpans_.lower_bound(needPages); // 第一个页数 ≥ need
        if (it != freeSpans_.end()) {
            Span* span = it->second;                    // 头结点
            size_t remain = span->numPages - needPages; // 剩余页

            /* --- 2.1 可直接拿：剩余 0 页 或 ≥ 8 页 --- */
            if (remain == 0 || remain >= kMinSpanPages) {
                /* 从桶链表摘下 span */
                if (span->next)
                    freeSpans_[it->first] = span->next;
                else
                    freeSpans_.erase(it);

                span->isUsed = true;
                spanMap_[span->pageAddr] = span;

                /* --- 2.1.1 需要切分 --- */
                if (remain >= kMinSpanPages) {
                    /* 创建剩余部分 splitSpan */
                    void* splitAddr = static_cast<char*>(span->pageAddr) + needPages * kPageSize;
                    Span* splitSpan = new Span(splitAddr, remain, nullptr, span, false);

                    /* 挂回 freeSpans_[remain] 桶的头部 */
                    splitSpan->next = freeSpans_[remain];
                    freeSpans_[remain] = splitSpan;

                    /* 更新原 span 页数 */
                    span->numPages = needPages;
                }

                return span->pageAddr; // 返回页首给 Central
            }
            /* --- 2.2 剩余 1~7 页：整块拿不合算，继续找更大桶 --- */
            needPages = it->first + 1; // 下一个更大桶
            continue;
        }

        /* 3️⃣  没找到桶 → 向系统申请，策略：一次申请 max(needPages, kMinSpanPages*2) */
        size_t requestPages = std::max(needPages, kMinSpanPages * 2);
        size_t requestBytes = requestPages * kPageSize;
        void* memory = systemAlloc(requestBytes);
        if (!memory) return nullptr; // 系统 OOM

        /* 3.1 新 span 覆盖整个申请区域 */
        Span* newSpan = new Span(memory, requestPages, nullptr, nullptr, false);

        /* 3.2 建 CompleteSpan（供后续整页回收） */
        addrToCompSpanMap_[memory] = new CompleteSpan(memory, newSpan);

        /* 3.3 头插进 freeSpans_[requestPages] 桶 */
        newSpan->next = freeSpans_[requestPages];
        freeSpans_[requestPages] = newSpan;

        /* 再循环，让 lower_bound() 能找到它 */
    }
}

// void* PageCache::allocateSpanToCentral(size_t memSize) {
//     size_t numPages = memSize / kPageSize;
//     size_t numPages = (memSize + kPageSize - 1) / kPageSize;
//     if (numPages < kMinSpanPages) numPages = kMinSpanPages;
//     // 普通互斥锁
//     // std::lock_guard<std::mutex> lock(mutex_);

//     // 查找合适的空闲 span
//     // lower_bound() 返回第一个大于等于 numPages 的元素的迭代器
//     while (true) {
//         auto it = freeSpans_.lower_bound(numPages);
//         Span* allocatedSpan = it->second;
//         size_t remainPage = allocatedSpan->numPages - numPages;

//         // 找到的话，要么刚好可以分配，要么 remainPage > 8，否则重新向系统分配新的 Span
//         if (it != freeSpans_.end() && (remainPage == 0 || remainPage > kMinSpanPages)) {
//             // 将取出的 span 从原有的空闲链表 freeSpans_[it->first] 中移除
//             if (allocatedSpan->next) {
//                 freeSpans_[it->first] = allocatedSpan->next;
//             } else {
//                 freeSpans_.erase(it);
//             }
//             allocatedSpan->isUsed = true;
//             if (remainPage == numPages) {
//                 spanMap_[allocatedSpan->pageAddr] = allocatedSpan;
//                 return allocatedSpan->pageAddr;
//             } else {
//                 // 如果 span 太大且分割后的部分大于等于 8 页，则进行分割
//                 Span* splitSpan =
//                     new Span(static_cast<char*>(allocatedSpan->pageAddr) + memSize,
//                              allocatedSpan->numPages - numPages, nullptr, allocatedSpan,
//                              false); // splitSpan 是分割后超出的部分

//                 // 将超出部分放回空闲 Span* 列表头部
//                 auto& list = freeSpans_[splitSpan->numPages];
//                 splitSpan->next = list;
//                 list = splitSpan;

//                 allocatedSpan->numPages = numPages;
//                 spanMap_[allocatedSpan->pageAddr] = allocatedSpan;
//                 return allocatedSpan->pageAddr;
//             }
//         }
//         // 没有合适的span，向系统申请
//         void* memory = systemAlloc(kMaxBytes);
//         if (!memory) return nullptr;

//         // 创建新的 Span
//         Span* newSpan = new Span(memory, numPages, nullptr, nullptr, false);
//         // 维护 addrToCompSpanMap_ (head 指向 CompleteSpan*)
//         addrToCompSpanMap_[memory] = new CompleteSpan(memory, newSpan);
//         freeSpans_[kMaxPages] = newSpan;
//     }
// }

// TODO
// void PageCache::deallocateSpan(void* ptr, size_t numPages) {
//     // std::lock_guard<std::mutex> lock(mutex_);

//     // 查找对应的 span，没找到代表不是 PageCache 分配的内存，直接返回
//     // auto it = spanMap_.find(ptr);
//     // if (it == spanMap_.end()) return;

//     Span* span = it->second;

//     // 尝试合并相邻的span
//     void* nextAddr =
//         static_cast<char*>(ptr) + numPages * PAGE_SIZE; // 算这个 span 紧挨着的下一个地址
//     auto nextIt = spanMap_.find(nextAddr);

//     if (nextIt != spanMap_.end()) {
//         Span* nextSpan = nextIt->second;

//         // 1. 首先检查nextSpan是否在空闲链表中
//         bool found = false;
//         auto& nextList = freeSpans_[nextSpan->numPages];

//         // 检查是否是头节点
//         if (nextList == nextSpan) {
//             nextList = nextSpan->next;
//             found = true;
//         } else if (nextList) { // 只有在链表非空时才遍历
//             Span* prev = nextList;
//             while (prev->next) {
//                 if (prev->next == nextSpan) {
//                     // 将nextSpan从空闲链表中移除
//                     prev->next = nextSpan->next;
//                     found = true;
//                     break;
//                 }
//                 prev = prev->next;
//             }
//         }

//         // 2. 只有在找到nextSpan的情况下才进行合并
//         if (found) {
//             // 合并span
//             span->numPages += nextSpan->numPages;
//             spanMap_.erase(nextAddr);
//             delete nextSpan;
//         }
//     }

//     // 将合并后的span通过头插法插入空闲列表
//     auto& list = freeSpans_[span->numPages];
//     span->next = list;
//     list = span;
// }

void* PageCache::systemAlloc(size_t memSize) {
    // 使用mmap分配内存
    // MAP_PRIVATE | MAP_ANONYMOUS 的意思是 写时复制(不是直写) | 匿名(不是文件)
    void* ptr = mmap(nullptr, memSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return nullptr;

    // 清零内存
    memset(ptr, 0, memSize);
    return ptr;
}

void PageCache::insertToSpanSet(std::unordered_set<CompleteSpan*>& returnedAddrList, void* addr) {
    Span* head = spanMap_[addr];
    while (head->headSpan) {
        head = head->headSpan;
    }
    returnedAddrList.insert(addrToCompSpanMap_[head]);
}

} // namespace mempool