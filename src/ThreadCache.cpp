#include "ThreadCache.h"
#include "CentralCache.h"
#include <cstdlib>

namespace mempool
{
// 分配 块 内存
void* ThreadCache::allocate(size_t memSize) {
    if (memSize == 0) memSize = kAlignment;           // 至少分配一个 kAlignment
    if (memSize > kMaxBytes) {
        size_t totalSize = memSize + sizeof(BlockHeader);
        char* realPtr = (char*)malloc(totalSize);   // 分配 header + data
        BlockHeader* header = (BlockHeader*)realPtr;
        header->index = kLargeAllocIndex;
        return realPtr + sizeof(BlockHeader);
    }

    size_t index = SizeClass::getIndex(memSize); // 获取所需的内存组编号 memSize <= index * 8B

    // 如果线程本地自由链表为空，则从中心缓存获取一批内存
    if (threadFreeListSize_[index] == 0 || threadFreeList_[index] == nullptr) fetchFromCentralCache(index);
    // 更新对应自由链表的长度计数
    threadFreeListSize_[index]--;

    void* block = threadFreeList_[index];
    // 更新 threadFreeList_[index] 指向下一指针地址,
    threadFreeList_[index] = *reinterpret_cast<void**>(block);
    return static_cast<char*>(block) + sizeof(BlockHeader);
}

// 归还 块 内存
void ThreadCache::deallocate(void* userPtr) {
    // 排除大内存
    char* realPtr = (char*)userPtr - sizeof(BlockHeader);
    BlockHeader* header = (BlockHeader*)realPtr;
    if (header->index == kLargeAllocIndex) {
        free(realPtr);
        return;
    }

    size_t index = header->index;

    // 插入到线程本地自由链表
    *reinterpret_cast<void**>(realPtr) = threadFreeList_[index]; // ptr 被临时解释为 void** 类型, 下一句还是恢复为 void*
    threadFreeList_[index] = realPtr;

    // 更新对应自由链表的长度计数
    threadFreeListSize_[index]++;

    // 判断是否需要将部分内存回收给中心缓存
    if (shouldReturnToCentralCache(index)) {
        returnToCentralCache(threadFreeList_[index], index);
    }
}

void ThreadCache::fetchFromCentralCache(size_t index) {
    size_t size = (index + 1) * kAlignment;
    // 根据对象内存大小计算批量获取的数量
    size_t batchNum = getBatchNum(size);
    // 从中心缓存批量获取内存
    void* start = CentralCache::getInstance().fetchToThreadCache(index, batchNum);
    if (!start) return;

    // 更新自由链表大小
    threadFreeListSize_[index] += batchNum;
    threadFreeList_[index] = start;

    return;
}

// 判断是否需要将内存回收给中心缓存
bool ThreadCache::shouldReturnToCentralCache(size_t index) {
    return threadFreeListSize_[index] >= kReturnToCentralThreshold;
}

void ThreadCache::returnToCentralCache(void* start, size_t index) {
    // // 获取对齐后的实际块大小
    // size_t alignedSize = SizeClass::roundUp(size);

    // 计算要归还内存块数量
    size_t batchNum = threadFreeListSize_[index];
    if (batchNum <= 1) return; // 如果只有一个块，则不归还

    // 保留一部分在ThreadCache中（比如保留1/4）
    size_t keepNum = std::max(batchNum / 4, size_t(1));
    size_t returnNum = batchNum - keepNum;

    // 将内存块串成链表
    char* current = static_cast<char*>(start);
    // 使用对齐后的大小计算分割点
    char* splitNode = current;
    for (size_t i = 0; i < keepNum - 1; ++i) {
        splitNode = reinterpret_cast<char*>(*reinterpret_cast<void**>(splitNode));
        if (splitNode == nullptr) {
            // 如果链表提前结束，更新实际的返回数量
            returnNum = batchNum - (i + 1);
            break;
        }
    }

    if (splitNode != nullptr) {
        // 将要返回的部分和要保留的部分断开
        void* nextNode = *reinterpret_cast<void**>(splitNode);
        *reinterpret_cast<void**>(splitNode) = nullptr; // 断开连接

        // 更新ThreadCache的空闲链表
        threadFreeList_[index] = start;

        // 更新自由链表大小
        threadFreeListSize_[index] = keepNum;

        // 将剩余部分返回给CentralCache
        if (returnNum > 0 && nextNode != nullptr) {
            CentralCache::getInstance().returnFromThreadCache(nextNode, returnNum * kAlignment, index);
        }
    }
}

size_t ThreadCache::getBatchNum(size_t size) {
    // 基准：每次批量获取不超过4KB内存
    constexpr size_t MAX_BATCH_SIZE = 4 * 1024; // 4KB

    // 根据对象大小设置合理的基准批量数
    size_t baseNum;
    if (size <= 32)
        baseNum = 64; // 64 * 32 = 2KB
    else if (size <= 64)
        baseNum = 32; // 32 * 64 = 2KB
    else if (size <= 128)
        baseNum = 16; // 16 * 128 = 2KB
    else if (size <= 256)
        baseNum = 8; // 8 * 256 = 2KB
    else if (size <= 512)
        baseNum = 4; // 4 * 512 = 2KB
    else if (size <= 1024)
        baseNum = 2; // 2 * 1024 = 2KB
    else
        baseNum = 1; // 大于1024的对象每次只从中心缓存取1个

    // 计算最大批量数
    size_t maxNum = std::max(size_t(1), MAX_BATCH_SIZE / size);

    // 取最小值，但确保至少返回1
    return std::max(sizeof(1), std::min(maxNum, baseNum));
}

} // namespace mempool