#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace mempool
{
constexpr size_t kAlignment = 8;
constexpr size_t kMaxBytes = 256 * 1024; // 256KB
constexpr size_t kFreeListSize = kMaxBytes / kAlignment;// 需要维护的自由链表个数, 多少个大小等级
constexpr size_t kPageSize = 4096;                      // 页大小
constexpr size_t kReturnToCentralThreshold = 256;       // 归还至 Central 的阈值
constexpr uint32_t kLargeAllocIndex = 0xFFFFFFFF;       // 标记大块分配

struct BlockHeader {
    uint32_t index; // size class 编号(0 ~ 32767)   // 可以最大表示到 (32K - 1)B
};

// Size 计算工具类
class SizeClass {
public:
    // 向上取整
    static size_t roundUp(size_t bytes) { return (bytes + kAlignment - 1) & ~(kAlignment - 1); }

    // 获取对应大小的 index
    static size_t getIndex(size_t bytes) {
        return bytes == 0 ? 0 : ((bytes + kAlignment - 1) / kAlignment) - 1; // -1 得数组下标
    }
};

} // namespace mempool