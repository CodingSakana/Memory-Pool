#include "PageCache.h"
#include <cstring>
#include <sys/mman.h>

namespace mempool
{
void* PageCache::allocateSpan(size_t memSize) {
    size_t numPages = memSize / PAGE_SIZE;
    // 普通互斥锁
    // std::lock_guard<std::mutex> lock(mutex_);

    // 查找合适的空闲 span
    // lower_bound() 返回第一个大于等于 numPages 的元素的迭代器
    auto it = freeSpans_.lower_bound(numPages);
    if (it != freeSpans_.end()) {
        Span* allocatedSpan = it->second;

        // 将取出的 span 从原有的空闲链表 freeSpans_[it->first] 中移除
        if (allocatedSpan->next) {
            freeSpans_[it->first] = allocatedSpan->next;
        } else {
            freeSpans_.erase(it);
        }

        // 如果 span 太大且分割后的部分任大于 8 页，则进行分割
        if (allocatedSpan->numPages - numPages > 8) {
            Span* SplitSpan =
                new Span(static_cast<char*>(allocatedSpan->pageAddr) + memSize,
                         allocatedSpan->numPages - numPages, 
                         nullptr); // SplitSpan 是分割后超出的部分

            // 将超出部分放回空闲 Span* 列表头部
            auto& list = freeSpans_[SplitSpan->numPages];
            SplitSpan->next = list;
            list = SplitSpan;

            allocatedSpan->numPages = numPages;
        }

        // 记录span信息用于回收
        // spanMap_[allocatedSpan->pageAddr] = allocatedSpan;
        return allocatedSpan->pageAddr;
    } else {
        // 没有合适的span，向系统申请
        void* memory = systemAlloc(memSize);
        if (!memory) return nullptr;

        // 创建新的span
        Span* span = new Span(memory, numPages, nullptr);

        // 记录span信息用于回收
        // spanMap_[memory] = span;
        return memory;
    }
}

// TODO
void PageCache::deallocateSpan(void* ptr, size_t numPages) {
    // std::lock_guard<std::mutex> lock(mutex_);

    // 查找对应的 span，没找到代表不是 PageCache 分配的内存，直接返回
    // auto it = spanMap_.find(ptr);
    // if (it == spanMap_.end()) return;

    Span* span = it->second;

    // 尝试合并相邻的span
    void* nextAddr =
        static_cast<char*>(ptr) + numPages * PAGE_SIZE; // 算这个 span 紧挨着的下一个地址
    auto nextIt = spanMap_.find(nextAddr);

    if (nextIt != spanMap_.end()) {
        Span* nextSpan = nextIt->second;

        // 1. 首先检查nextSpan是否在空闲链表中
        bool found = false;
        auto& nextList = freeSpans_[nextSpan->numPages];

        // 检查是否是头节点
        if (nextList == nextSpan) {
            nextList = nextSpan->next;
            found = true;
        } else if (nextList) { // 只有在链表非空时才遍历
            Span* prev = nextList;
            while (prev->next) {
                if (prev->next == nextSpan) {
                    // 将nextSpan从空闲链表中移除
                    prev->next = nextSpan->next;
                    found = true;
                    break;
                }
                prev = prev->next;
            }
        }

        // 2. 只有在找到nextSpan的情况下才进行合并
        if (found) {
            // 合并span
            span->numPages += nextSpan->numPages;
            spanMap_.erase(nextAddr);
            delete nextSpan;
        }
    }

    // 将合并后的span通过头插法插入空闲列表
    auto& list = freeSpans_[span->numPages];
    span->next = list;
    list = span;
}

void* PageCache::systemAlloc(size_t memSize) {
    // 使用mmap分配内存
    // MAP_PRIVATE | MAP_ANONYMOUS 的意思是 写时复制(不是直写) | 匿名(不是文件)
    void* ptr = mmap(nullptr, memSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return nullptr;

    // 清零内存
    memset(ptr, 0, memSize);
    return ptr;
}

} // namespace mempool