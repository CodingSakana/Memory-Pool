#include "ThreadCache.h"

#include <cassert>
#include <cstdlib> // malloc / free
#include <new>     // std::bad_alloc

namespace mempool
{
/* 单例：每个线程一个实例 */
ThreadCache& ThreadCache::getInstance() {
    thread_local ThreadCache tc;
    return tc;
}

/* 构造：初始化链表数组 */
ThreadCache::ThreadCache() {
    freeList_.fill(nullptr);
    freeListSize_.fill(0);
}

/* 批量数策略：越小的块一次拿越多 */
std::size_t ThreadCache::batchNumForSize(std::size_t bytes) noexcept {
    if (bytes <= 128) return 512;  // 128 B
    if (bytes <= 1024) return 128; // 1 KB
    if (bytes <= 8192) return 32;  // 8 KB
    if (bytes <= 65536) return 8;  // 64 KB
    return 4;                      // 128 KB < bitys <= kMaxBytes
}

void* ThreadCache::allocate(std::size_t size) {
    if (size == 0) size = kAlignment;

    /* 大对象：直接调用 malloc，但仍加头部保持统一回收逻辑 */
    if (size > kMaxBytes) {
        std::size_t total = size + sizeof(BlockHeader);
        auto* raw = static_cast<char*>(std::malloc(total));
        if (!raw) throw std::bad_alloc();

        auto* hd = reinterpret_cast<BlockHeader*>(raw);
        hd->size = size;
        hd->next = nullptr;
        return hd + 1; // 跳过头部返回给用户，加 1 相当于 + 1* sizeof (BlockHeader)
    }

    /* 小对象：先尝试本线程空闲链 */
    std::size_t index = SizeClass::getIndex(size);  // 获取空闲块链表的 index

    /* 对应的空闲链表不为空的情况 */
    if (BlockHeader* hd = freeList_[index]) {
        freeList_[index] = hd->next;
        freeListSize_[index]--;
        return hd + 1;
    }

    /* 空链为空则向 CentralCache 批量要 */
    return fetchFromCentralCache(index);
}

void ThreadCache::deallocate(void* ptr) {
    if (!ptr) return;

    auto* hd = reinterpret_cast<BlockHeader*>(ptr) - 1;
    std::size_t bytes = hd->size;

    /* 大对象：直接交由系统释放 */
    if (bytes > kMaxBytes) {
        std::free(hd);
        return;
    }

    std::size_t index = SizeClass::getIndex(bytes);

    hd->next = freeList_[index];
    freeList_[index] = hd;
    freeListSize_[index]++;

    /* 链表过长 → 归还部分给 CentralCache */
    if (shouldReturnToCentralCache(index)) returnToCentralCache(hd, index);
}

void* ThreadCache::fetchFromCentralCache(std::size_t index) {
    std::size_t userBytes = (index + 1) * kAlignment;
    std::size_t batchNum = batchNumForSize(userBytes);

    /* Central 尽力而为地提供 */
    BlockHeader* list = CentralCache::getInstance().fetchBatch(index, batchNum);
    if (!list) return nullptr; // PageCache 也没拿到，极端情况

    /* list 已经是 BlockHeader 链：第一个给用户，其余挂回本地链 */
    BlockHeader* headUser = list;
    BlockHeader* remain = headUser->next;

    /* 统计实际 remain 链的节点数（可能 < batchNum-1） */
    std::size_t actual = 0;
    for (BlockHeader* p = remain; p != nullptr; p = p->next)
        ++actual;

    freeList_[index] = remain;
    freeListSize_[index] += actual;

    headUser->next = nullptr;
    return headUser + 1;
}

/* 将本地空链拆成 keep / return 两段（keep ≈ 1/4） */
void ThreadCache::returnToCentralCache(BlockHeader* start, std::size_t index) {
    std::size_t total = freeListSize_[index];
    if (total <= 1) return;

    std::size_t keepCnt = std::max<std::size_t>(total / 4, 1);
    std::size_t retCnt = total - keepCnt;

    /* 找到需保留段的尾节点 */
    BlockHeader* tail = start;
    for (std::size_t i = 1; i < keepCnt; ++i)
        tail = tail->next;

    /* 断链：tail->next 开始的列表归还 */
    BlockHeader* retList = tail->next;
    tail->next = nullptr;

    freeList_[index] = start;
    freeListSize_[index] = keepCnt;

    CentralCache::getInstance().returnBatch(retList, retCnt, index);
}

} // namespace mempool
