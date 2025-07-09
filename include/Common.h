#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <thread>

namespace mempool
{
/*--------------------------------------------------
 | 1. 对齐修正
 |   - UBSan 报 store to misaligned address，是因为
 |     BlockHeader 仅 4 B，user 区相对页首偏移=4，
 |     但我们又在 user 区里写入 void*（要求 8 B 对齐）。
 |   - 解决：让 BlockHeader 本身占 8 B，并显式 alignas(8)。
 *-------------------------------------------------*/
struct alignas(8) BlockHeader {
    uint32_t index;   // size-class 索引（0 ~ 32767 | 0xFFFFFFFF=大块）
    uint32_t reserved /*=0*/; // 仅作填充，保证 sizeof == 8
};
static_assert(sizeof(BlockHeader) == 8,
              "BlockHeader must be 8-byte aligned, otherwise pointer write will break.");

/*—— 其余常量不动 ——*/
constexpr size_t kAlignment = 8;
constexpr size_t kMaxBytes  = 256 * 1024;               // 256 KB
constexpr size_t kFreeListSize = kMaxBytes / kAlignment;
constexpr size_t kPageSize  = 4096;
constexpr size_t kReturnToCentralThreshold = 256;
constexpr uint32_t kLargeAllocIndex = 0xFFFFFFFF;
/* 小修：最大页数应 = kMaxBytes / kPageSize */
constexpr size_t kMaxPages = kMaxBytes / kPageSize;     // 64
constexpr size_t kMaxMemPoolSize = kMaxBytes * 1024;    // 256 MB
constexpr size_t kMinSpanPages = 8;

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