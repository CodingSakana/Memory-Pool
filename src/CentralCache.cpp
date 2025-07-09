#include "CentralCache.h"
#include "PageCache.h"
#include <cassert>
#include <chrono>
#include <thread>
#include <unordered_set>

namespace mempool
{
// 延时间隔
// const std::chrono::milliseconds CentralCache::DELAY_INTERVAL{1000}; // 1000 ms

// 每次从 PageCache 获取 span 大小（以页为单位）
// static const size_t SPAN_PAGES = 8;

// 初始化
CentralCache::CentralCache() {
    for (auto& head : centralFreeList_)
        head.store(nullptr, std::memory_order_relaxed);
    for (auto& cnt : centralFreeListSize_)
        cnt = 0;
    for (auto& lck : locks_)
        lck.unlock();
}

// // 初始化延迟归还相关的成员变量
// for (auto& count : delayCounts_) {
//     count.store(0, std::memory_order_relaxed);
// }
// for (auto& time : lastReturnTimes_) {
//     time = std::chrono::steady_clock::now();
// }
// spanCount_.store(0, std::memory_order_relaxed);

void* CentralCache::fetchToThreadCache(size_t index, size_t batchNum) {
    // // 索引检查，当索引大于等于kFreeListSize时，说明申请内存过大应直接向系统申请
    // if (index >= kFreeListSize) return nullptr;
    const size_t userSize = (index + 1) * kAlignment;
    const size_t blockTotalSize = userSize + sizeof(BlockHeader);
    // 自旋锁保护
    locks_[index].lock();

    void* result = nullptr;
    try {
        // 尝试从中心缓存获取内存块
        result = centralFreeList_[index].load(std::memory_order_relaxed);

        // central 不够时
        if (batchNum > centralFreeListSize_[index]) {
            // 计算需要从 pagecache 拿来的 totalMemSize (包括 BlockHeader)
            size_t totalMemSize = (batchNum - centralFreeListSize_[index]) * blockTotalSize;
            locks_[index].unlock();
            result = fetchFromPageCache(totalMemSize); // 1个满足 size 的 span 的地址

            // 还是没有就返回 nullptr
            if (!result) {
                return nullptr;
            }

            // 将获取的内存块切分成小块
            char* start = static_cast<char*>(result) + sizeof(BlockHeader);
            void* firstUser = start;

            // 计算实际分配的页数，因为 PageCache 最少也得分配 8 页
            size_t numPages = (totalMemSize <= kMinSpanPages * kPageSize)
                                  ? kMinSpanPages
                                  : (totalMemSize + kPageSize - 1) / kPageSize; // 向上取整

            // 使用实际页数计算块数
            size_t blockNum = (numPages * kPageSize) / blockTotalSize;

            if (blockNum > 1) {
                // 确保至少有两个块才构建链表
                for (size_t i = 0; i < blockNum; ++i) { // *** CHG: 从 0 开始
                    char* currentHdr = static_cast<char*>(result) + i * blockTotalSize;
                    reinterpret_cast<BlockHeader*>(currentHdr)->index = // *** NEW: 写 header->index
                        static_cast<uint32_t>(index);

                    void* currentUser = currentHdr + sizeof(BlockHeader); // user 区地址
                    void* nextUser = (i + 1 == blockNum) ? nullptr : static_cast<char*>(currentUser) + blockTotalSize;
                    *reinterpret_cast<void**>(currentUser) = nextUser; // 串链
                }
                // 多余块挂回 central（锁内）
                locks_[index].lock(); // *** NEW: 重新上锁
                *reinterpret_cast<void**>(start + (blockNum - 1) * blockTotalSize) =    // 计算最后一个节点的地址
                    centralFreeList_[index].load(std::memory_order_relaxed);
                centralFreeList_[index].store(firstUser,
                                              std::memory_order_release); // 存 user 指针
                centralFreeListSize_[index] += (blockNum - batchNum);     // NEW: 更新库存

                // 寻找需要切断的节点
                void* splitNode = firstUser;
                for (size_t i = 0; i < batchNum - 1; ++i) {
                    splitNode = *reinterpret_cast<void**>(splitNode);
                }
                // 保存result的下一个节点
                void* next = *reinterpret_cast<void**>(splitNode);
                // 将result与链表断开
                *reinterpret_cast<void**>(splitNode) = nullptr;

                // 更新中心缓存
                centralFreeList_[index].store(next, std::memory_order_release);

                // 中心已给线程 batchNum 块
                centralFreeListSize_[index] -= batchNum; // *** NEW

                locks_[index].unlock(); // *** NEW: 释放锁

                return firstUser;
                
                // 使用无锁方式记录span信息
                // 做记录是为了将中心缓存多余内存块归还给页缓存做准备。考虑点：
                // 1.CentralCache 管理的是小块内存，这些内存可能不连续
                // 2.PageCache 的 deallocateSpan 要求归还连续的内存
                // size_t trackerIndex = spanCount_++;
                // if (trackerIndex < spanTrackers_.size()) {
                //     spanTrackers_[trackerIndex].spanAddr.store(start,
                //     std::memory_order_release);
                //     spanTrackers_[trackerIndex].numPages.store(numPages,
                //     std::memory_order_release); spanTrackers_[trackerIndex].blockCount.store(
                //         blockNum, std::memory_order_release); // 共分配了blockNum个内存块
                //     spanTrackers_[trackerIndex].freeCount.store(blockNum - 1,
                //                                                 std::memory_order_release);
                //                                                 //
                //                                                 第一个块result已被分配出去，所以初始空闲块数为blockNum
                //                                                 - 1
                // }
            }
        } else { // 当 central 的 blocks 够时
            void* splitNode = result;
            for (size_t i = 0; i < batchNum - 1; ++i) {
                splitNode = *reinterpret_cast<void**>(splitNode);
            }
            // 保存 splitNode 的下一个节点
            void* next = *reinterpret_cast<void**>(splitNode);
            // 将 result 与链表断开
            *reinterpret_cast<void**>(splitNode) = nullptr;

            // 更新中心缓存
            centralFreeList_[index].store(next, std::memory_order_release);
            centralFreeListSize_[index] -= batchNum;
            locks_[index].unlock(); // *** NEW: 提前解锁
            return result;
            // // 更新span的空闲计数
            // SpanTracker* tracker = getSpanTracker(result);
            // if (tracker) {
            //     // 减少一个空闲块
            //     tracker->freeCount.fetch_sub(1, std::memory_order_release);
            // }
        }
    } catch (...) {
        locks_[index].unlock();
        throw;
    }
    return nullptr;
}

void CentralCache::returnFromThreadCache(void* start, size_t totalUserSize, size_t index) {
    if (!start || index >= kFreeListSize) return;

    // size_t blockSize = (index + 1) * kAlignment;
    // size_t blockCount = totalUserSize / blockSize;

    locks_[index].lock();

    try {
        // 1. 将归还的链表连接到中心缓存
        void* end = start;
        size_t count = 1;
        std::unordered_set<CompleteSpan*>
            returnedAddrList; // 这里都是需要待会检查是否能合并的 CompleteSpan
        while (*reinterpret_cast<void**>(end) != nullptr) {
            // PageCache::getInstance().insertToSpanSet(returnedAddrList, end);
            end = *reinterpret_cast<void**>(end);
            count++;
        }

        // PageCache::getInstance().insertToSpanSet(returnedAddrList,
        //                                          end); // *** 新增：把“最后一个”节点也插进去
        count++;                                       /* 上面多插了一个，记得 ++ */
        void* current = centralFreeList_[index].load(std::memory_order_relaxed);
        *reinterpret_cast<void**>(end) = current; // 头插法（将原有链表接在归还链表后边）
        centralFreeList_[index].store(start, std::memory_order_release);
        centralFreeListSize_[index] += count;

        // 2. 更新延迟计数
        // size_t currentCount = delayCounts_[index].fetch_add(count, std::memory_order_relaxed)
        // + count; auto currentTime = std::chrono::steady_clock::now();

        // 3. 检查是否需要执行延迟归还
        // if (shouldPerformDelayedReturn(index, currentCount, currentTime)) {
        //     performDelayedReturn(index);
        // }

    } catch (...) {
        locks_[index].unlock();
        throw;
    }

    locks_[index].unlock();
}

// // 检查是否需要执行延迟归还
// bool CentralCache::shouldPerformDelayedReturn(size_t index, size_t currentCount,
//                                               std::chrono::steady_clock::time_point
//                                               currentTime)
//                                               {
//     // 基于计数和时间的双重检查
//     if (currentCount >= MAX_DELAY_COUNT) {
//         return true;
//     }

//     auto lastTime = lastReturnTimes_[index];
//     return (currentTime - lastTime) >= DELAY_INTERVAL;
// }

// // 执行延迟归还
// void CentralCache::performDelayedReturn(size_t index) {
//     // 重置延迟计数
//     delayCounts_[index].store(0, std::memory_order_relaxed);
//     // 更新最后归还时间
//     lastReturnTimes_[index] = std::chrono::steady_clock::now();

//     // 统计每个span的空闲块数
//     std::unordered_map<SpanTracker*, size_t> spanFreeCounts;
//     void* currentBlock = centralFreeList_[index].load(std::memory_order_relaxed);

//     // 遍历并写入 unordered_map
//     while (currentBlock) {
//         SpanTracker* tracker = getSpanTracker(currentBlock);
//         if (tracker) {
//             spanFreeCounts[tracker]++;
//         }
//         currentBlock = *reinterpret_cast<void**>(currentBlock);
//     }

//     // 更新每个span的空闲计数并检查是否可以归还
//     for (const auto& [tracker, newFreeBlocks] : spanFreeCounts) {
//         updateSpanFreeCount(tracker, newFreeBlocks, index);
//     }
// }

// void CentralCache::updateSpanFreeCount(SpanTracker* tracker, size_t newFreeBlocks, size_t
// index)
// {
//     size_t oldFreeCount = tracker->freeCount.load(std::memory_order_relaxed);
//     size_t newFreeCount = oldFreeCount + newFreeBlocks;
//     tracker->freeCount.store(newFreeCount, std::memory_order_release);

//     // 如果所有块都空闲，归还span
//     if (newFreeCount == tracker->blockCount.load(std::memory_order_relaxed)) {
//         void* spanAddr = tracker->spanAddr.load(std::memory_order_relaxed);
//         size_t numPages = tracker->numPages.load(std::memory_order_relaxed);

//         // 从自由链表中移除这些块
//         void* head = centralFreeList_[index].load(std::memory_order_relaxed);
//         void* newHead = nullptr;
//         void* prev = nullptr;
//         void* current = head;

//         while (current) {
//             void* next = *reinterpret_cast<void**>(current);
//             if (current >= spanAddr &&
//                 current < static_cast<char*>(spanAddr) + numPages * kPageSize) {
//                 if (prev) {
//                     *reinterpret_cast<void**>(prev) = next;
//                 } else {
//                     newHead = next;
//                 }
//             } else {
//                 prev = current;
//             }
//             current = next;
//         }

//         centralFreeList_[index].store(newHead, std::memory_order_release);
//         PageCache::getInstance().deallocateSpan(spanAddr, numPages);
//     }
// }

void* CentralCache::fetchFromPageCache(size_t totalMemSize) {
    // 1. 计算实际需要的页数
    size_t numPages = (totalMemSize + kPageSize - 1) / kPageSize;
    numPages = std::max(numPages, kMinSpanPages);
    // 2. 根据大小决定分配策略
    return PageCache::getInstance().allocateSpanToCentral(numPages * kPageSize);
}

// SpanTracker* CentralCache::getSpanTracker(void* blockAddr) {
//     // 遍历spanTrackers_数组，找到blockAddr所属的span
//     for (size_t i = 0; i < spanCount_.load(std::memory_order_relaxed); ++i) {
//         void* spanAddr = spanTrackers_[i].spanAddr.load(std::memory_order_relaxed);
//         size_t numPages = spanTrackers_[i].numPages.load(std::memory_order_relaxed);

//         // 判断是否在该 span 的范围内
//         // static_cast<char*>(spanAddr) 是为了需要做加法，得先确定指针类型，
//         // char* 即以 1B 为单位
//         if (blockAddr >= spanAddr &&
//             blockAddr < static_cast<char*>(spanAddr) + numPages * kPageSize) {
//             return &spanTrackers_[i];
//         }
//     }
//     return nullptr;
// }

} // namespace mempool