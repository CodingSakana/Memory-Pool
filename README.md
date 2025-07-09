# 高性能 Memory-Pool（C++20 内存池）

一款面向 **内存需求较为固定的高频分配** 场景的跨平台内存池，核心组件包括：

| 组件               | 作用                                          |
| ---------------- | ------------------------------------------- |
| **ThreadCache**  | 每线程本地缓存；批量归还 / 批量获取。              |
| **CentralCache** | 跨线程共享中心缓存，负责不同线程之间的空闲块均衡。                   |
| **PageCache**    | 以页为粒度向系统申请与释放内存，负责相邻 Span 合并、阈值回收。          |
| **MemoryPool**   | 统一外部接口：`allocate(size)` / `deallocate(ptr)` |

> **目标**：  
> 在多相同大小尺寸内存分配时，相比 `new/delete` 获得约 **1.3x - 8x** 的吞吐提升（随分配对象大小不同而不同）。  
> 在多随机大小尺寸内存分配时，与 `new/delete` 性能持平

---

## 特性

- **C++20 / STL** 实现，依赖极少。
- **包含块头部管理**：紧贴 **user内存** 前的 **8个** 字节用于存放 BlockHeader 管理user内存大小和 空闲链表的后继
- **API 签名简单**：`deallocate()` 无需显式指定内存 size 大小
- **线程本地（ThreadCache）**：小对象分配零锁，按 size-class 批量管理。
- **自适应批量**：`batchNumForSize()` 依据块大小动态决定一次抓取数量。
- **页级别合并 & 回收**：空闲页超过阈值（默认 **64 MB**）时自动整段归还系统。
- **ASan / TSan** 全通过：集成单元测试脚本可一键跑 AddressSanitizer / ThreadSanitizer。
- **CMake** 构建，跨 Windows / Linux / macOS。

---

## 目录结构

```
Memory-Pool/
│
├─ include/         头文件（公共 API, 结构定义）
├─ src/             源文件（各模块实现）
├─ tests/           测试 & 基准
│   ├─ mempool_full_test.cpp    功能 & 稳定性单测
│   ├─ perf_compare.cpp         大小多维度性能对比
├─ example/         测试 & 基准的示例输出
├─ CMakeLists.txt   CMake 构建脚本
└─ README.md        使用说明（本文件）
```

---

## 构建 & 运行

### 1. 生成构建文件

> 项目根目录下运行
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
```

> 调试模式可改为 `-DCMAKE_BUILD_TYPE=Debug`，会带 `-g` 并关闭优化。

### 2. 编译

```bash
make
```

### 3. 运行单元测试

```bash
make test
```

### 4. 运行性能基准

```bash
make perf
```

---

## 示例性能（Intel i7-10875H · 16 线程 ·  WSL2 Ubuntu 24.04.2 LTS · gcc 13.3 -O3）

```
[100%] Built target perf_compare
===== MemoryPool vs new/delete =====

4B Single 100000000:
MemoryPool : 792.95 ms
New/Delete : 1092.58 ms
Speedup     : 1.38x

16-thread ×100000000 each:
MemoryPool : 1654.65 ms
New/Delete : 2821.55 ms
Speedup     : 1.71x

64B Single 100000000:
MemoryPool : 751.89 ms
New/Delete : 1106.30 ms
Speedup     : 1.47x

16-thread ×100000000 each:
MemoryPool : 1518.91 ms
New/Delete : 2867.91 ms
Speedup     : 1.89x

4096B Single 100000000:
MemoryPool : 769.09 ms
New/Delete : 5140.16 ms
Speedup     : 6.68x

16-thread ×100000000 each:
MemoryPool : 1579.70 ms
New/Delete : 7662.36 ms
Speedup     : 4.85x

Mixed size ST 8-256B × 100000000:
MemoryPool : 1616.21 ms
New/Delete : 1497.40 ms
Speedup     : 0.93x

Mixed size MT 16-thread ×10000000 each:
MemoryPool : 303.29 ms
New/Delete : 403.85 ms
Speedup     : 1.33x
[100%] Built target perf
```

---

## API

```cpp
#include "MemoryPool.h"

void* p = mempool::MemoryPool::allocate(64);   // 任意大小
mempool::MemoryPool::deallocate(p);            // 无需显式传 size
```

 - 对齐粒度为 **8 B**
 - 系统页大小为 **4 KB**
 - 内存池可分配的最大字节数为 **256 KB**
 - **256 KB** 以上的内存分配请求默认直接转发至 `std::malloc()`。

---

## 贡献

欢迎 PR / Issue！

---

**Enjoy fast allocation! 🚀**

