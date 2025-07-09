#include "CentralCache.h"

#include <cassert>
#include <cstring> // std::memset

namespace mempool
{

/* 单例实现 */
CentralCache& CentralCache::getInstance() {
    static CentralCache cc;
    return cc;
}

/* 初始化 */
CentralCache::CentralCache() {
    for (auto& p : centralFreeList_)
        p.store(nullptr, std::memory_order_relaxed);
}

/* 分配 batchNum 个 blocks 的链表*/
BlockHeader* CentralCache::fetchBatch(std::size_t index, std::size_t batchNum) {
    assert(index < kFreeListNum && "size-class index out of range");

    SpinLock& lk = locks_[index];
    lk.lock();

    /* 链表计数 */
    std::size_t available = 0;
    for (BlockHeader* p = centralFreeList_[index].load(std::memory_order_relaxed);
         p && available < batchNum; p = p->next)
        ++available;
    /* 若空链不足 batchNum，则补充一次 span */
    if (available < batchNum) refillFromPageCache(index);

    /* 重新取头指针 */
    BlockHeader* head = centralFreeList_[index].load(std::memory_order_relaxed);
    if (!head) {
        lk.unlock();
        return nullptr;
    } // 极端：PageCache 也失败

    /* 从链表中拆下 batchNum 个节点 */
    BlockHeader* curr = head;
    BlockHeader* prev = nullptr;
    std::size_t cnt = 0;

    while (curr && cnt < batchNum) {
        prev = curr;
        curr = curr->next;
        ++cnt;
    }

    /* prev 指向第 batchNum-1 个节点 */
    BlockHeader* remain = curr; // 拆完剩余链
    prev->next = nullptr;       // 断链

    centralFreeList_[index].store(remain, std::memory_order_relaxed);

    lk.unlock();
    return head; // 返回批量链表
}

void CentralCache::returnBatch(BlockHeader* start, std::size_t /*blockNum*/,
                               std::size_t index) {
    if (!start || index >= kFreeListNum) return;

    /* 找到链表尾 */
    BlockHeader* tail = start;
    while (tail->next)
        tail = tail->next;

    SpinLock& lk = locks_[index];
    lk.lock();

    tail->next = centralFreeList_[index].load(std::memory_order_relaxed);
    centralFreeList_[index].store(start, std::memory_order_release);

    lk.unlock();
}

// 按 size‐class 分段：小对象拿少点页，大对象拿多点页
static constexpr std::size_t kSpanPagesForIndex(size_t index) {
    if (index <= 4) return 4;    // 最小 sizeClass，用 4 页
    if (index <= 16) return 8;   // 中小型，用 8 页
    if (index <= 64) return 16;  // 中型，用 16 页
    return 32;                    // 较大，用 32 页
}

/* 向 PageCache 申请，切分成 BlockHeader 链并挂入 centralFreeList_[index] */
void CentralCache::refillFromPageCache(std::size_t index) {
    size_t spanPages = kSpanPagesForIndex(index);
    size_t spanBytes = spanPages * kPageSize;

    std::size_t userBytes = (index + 1) * kAlignment;
    std::size_t blkBytes = userBytes + sizeof(BlockHeader); // 块总大小

    if (blkBytes > spanBytes) return;

    /* 向 PageCache 申请整页内存 */
    void* spanMem = PageCache::getInstance().allocateSpan(spanPages); // 接口以页数为单位
    if (!spanMem) return;                                              // 失败则放弃

    /* 切分成链表，逆序插入头部 */
    char* ptr = static_cast<char*>(spanMem);
    std::size_t total = spanBytes / blkBytes;

    /* 开始串链 */
    BlockHeader* head = nullptr;
    BlockHeader* tail = nullptr;
    for (std::size_t i = 0; i < total; ++i) {
        auto* hd = reinterpret_cast<BlockHeader*>(ptr);
        hd->size = userBytes;
        hd->next = nullptr;
        if (!head) {
            head = tail = hd;
        } else {
            tail->next = hd;
            tail = hd;
        }
        ptr += blkBytes;
    }

    /* 挂到 freelist 头部 */
    BlockHeader* oldHead = centralFreeList_[index].load(std::memory_order_relaxed);
    tail->next = oldHead;
    centralFreeList_[index].store(head, std::memory_order_relaxed);
}

} // namespace mempool
