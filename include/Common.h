#pragma once
/**
 * 全局常量定义
 * 
 * struct BlockHeader     — 小块头部，记录大小并串联空闲链表
 * struct SizeClass       — 简单尺寸类工具（对齐/索引）
 */
#include <cstddef> // size_t
#include <cstdint> // uintptr_t

namespace mempool
{

// ────────────────────────────────────────────────────────────
// 全局常量
// ────────────────────────────────────────────────────────────
constexpr std::size_t kAlignment = 8;                        // 最小对齐粒度
constexpr std::size_t kPageSize = 4096;                      // 系统页大小
constexpr std::size_t kMaxBytes = 256 * 1024;                // 内存池可分配的最大字节数（256 KB）
constexpr std::size_t kFreeListNum = kMaxBytes / kAlignment; // 小对象自由链表数量

// ────────────────────────────────────────────────────────────
// 块头部：分配时紧贴 user 内存之前
// ────────────────────────────────────────────────────────────
struct BlockHeader {
    std::size_t size;  // 不含头部的 user 内存大小
    BlockHeader* next; // 空闲链中的后继
};

// ────────────────────────────────────────────────────────────
// SizeClass：字节数 <-> 索引 的映射
// ────────────────────────────────────────────────────────────
struct SizeClass {
    /** 按 kAlignment 向上取整 */
    static inline std::size_t roundUp(std::size_t bytes) noexcept {
        return (bytes + kAlignment - 1) & ~(kAlignment - 1);
    }

    /** 将内存大小映射到自由链表下标 */
    static inline std::size_t getIndex(std::size_t bytes) noexcept {
        return (roundUp(bytes) / kAlignment) - 1;
    }
};

} // namespace mempool
