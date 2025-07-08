#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace mempool
{
constexpr size_t ALIGNMENT = 8;
constexpr size_t MAX_BYTES = 256 * 1024;                 // 256KB
constexpr size_t FREE_LIST_SIZE = MAX_BYTES / ALIGNMENT; // 需要维护的自由链表个数, 多少个大小等级

// Size 计算工具类
class SizeClass {
public:
    // 向上取整
    static size_t roundUp(size_t bytes) { return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1); }

    static size_t getIndex(size_t bytes) {
        // 确保bytes至少为ALIGNMENT
        bytes = std::max(bytes, ALIGNMENT);
        // 向上取整后-1
        return (bytes + ALIGNMENT - 1) / ALIGNMENT - 1;
    }
};

} // namespace mempool