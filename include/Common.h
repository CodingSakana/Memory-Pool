#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <thread>

namespace mempool
{
constexpr size_t kAlignment = 8;
constexpr size_t kMaxBytes = 256 * 1024;                 // 256KB
constexpr size_t kFreeListSize = kMaxBytes / kAlignment; // 需要维护的自由链表个数, 多少个大小等级
constexpr size_t kPageSize = 4096;                       // 页大小
constexpr size_t kReturnToCentralThreshold = 256;        // 归还至 Central 的阈值
constexpr uint32_t kLargeAllocIndex = 0xFFFFFFFF;        // 标记大块分配
constexpr size_t kMaxPages = kMaxBytes / kMaxBytes;      // Span 可以包含的最大页数
constexpr size_t kMaxMemPoolSize = kMaxBytes * 1024;     // 内存池最大支持容量 256MB
constexpr size_t kMinSpanPages = 8;                      // Span 的最小页数

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

// 自旋锁类
class SpinLock {
private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }
    void unlock() { flag.clear(std::memory_order_release); }
};

inline std::array<SpinLock, kFreeListSize> locks_;

} // namespace mempool