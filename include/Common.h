#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace mempool
{
constexpr size_t ALIGNMENT = 8;
constexpr size_t MAX_BYTES = 1 << 18;                    // 256KB
constexpr size_t FREE_LIST_SIZE = MAX_BYTES / ALIGNMENT; // 需要维护的自由链表个数, 多少个大小等级

/*
struct BlockHeader {
    size_t size; // 内存块大小
    bool isUsed; // 使用标志
    BlockHeader* next;
};
*/

// Size 计算工具类
class SizeClass {
public:
    // 向上取整
    static size_t roundUp(size_t bytes) {
        return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    }

    // 获取对应大小的 index
    static size_t getIndex(size_t bytes) {
        return bytes == 0 ? 0 : ((bytes + ALIGNMENT - 1) / ALIGNMENT) - 1; // -1 得数组下标
    }
};

} // namespace mempool