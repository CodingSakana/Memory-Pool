if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING
      "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
      FORCE)
endif()

# 指定 CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称与语言
project(MemoryPool LANGUAGES CXX)

# ───────────────────────────────────────────────────────────────
# C++ 标准设置
# ───────────────────────────────────────────────────────────────
# 要求使用 C++20，并且无法降级
set(CMAKE_CXX_STANDARD       20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 区分 Debug 与 Release 的编译选项
# Debug 模式下开启调试符号，不优化
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG}   -g")
# Release 模式下开启 O3 优化，不生成调试符号
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# ───────────────────────────────────────────────────────────────
# 查找依赖库
# ───────────────────────────────────────────────────────────────
# 找到 pthread （Threads::Threads）库，用于多线程支持
find_package(Threads REQUIRED)

# ───────────────────────────────────────────────────────────────
# 目录与源文件设置
# ───────────────────────────────────────────────────────────────
# 定义源码、头文件、测试目录
set(SRC_DIR   ${CMAKE_SOURCE_DIR}/src)
set(INC_DIR   ${CMAKE_SOURCE_DIR}/include)
set(TEST_DIR  ${CMAKE_SOURCE_DIR}/tests)

# 将 src 目录下所有 .cpp 文件都作为源码
file(GLOB SOURCES "${SRC_DIR}/*.cpp")

# ───────────────────────────────────────────────────────────────
# 可执行目标：mempool_full_test
# ───────────────────────────────────────────────────────────────
# 创建 mempool_full_test 可执行文件，包含主代码和 full_test 测试
add_executable(mempool_full_test
    ${SOURCES}
    ${TEST_DIR}/mempool_full_test.cpp
)

# 为 mempool_full_test 指定头文件搜索路径（PRIVATE：仅此 target 可见）
target_include_directories(mempool_full_test PRIVATE ${INC_DIR})

# 要求此 target 使用 C++20 特性
target_compile_features(mempool_full_test PRIVATE cxx_std_20)

# 为此 target 打开所有警告 (-Wall)
target_compile_options(mempool_full_test PRIVATE -Wall)

# 链接 pthread 库
target_link_libraries(mempool_full_test PRIVATE Threads::Threads)

# ───────────────────────────────────────────────────────────────
# 可执行目标：perf_compare
# ───────────────────────────────────────────────────────────────
# 创建 perf_compare 可执行文件，包含主代码和性能测试
add_executable(perf_compare
    ${SOURCES}
    ${TEST_DIR}/perf_compare.cpp
)

# 同样为 perf_compare 指定头文件搜索路径、C++ 标准和警告
target_include_directories(perf_compare PRIVATE ${INC_DIR})
target_compile_features(perf_compare PRIVATE cxx_std_20)
target_compile_options(perf_compare PRIVATE -Wall)
target_link_libraries(perf_compare PRIVATE Threads::Threads)

# ───────────────────────────────────────────────────────────────
# 自定义目标：test
# ───────────────────────────────────────────────────────────────
# 执行功能测试 `cmake --build . --target test` 即可编译并运行单元测试
add_custom_target(test
    DEPENDS mempool_full_test        # 确保先编译
    COMMAND mempool_full_test        # 然后执行测试可执行文件
)

# ───────────────────────────────────────────────────────────────
# 自定义目标：perf
# ───────────────────────────────────────────────────────────────
# 执行性能测试：`cmake --build . --target perf`
add_custom_target(perf
    DEPENDS perf_compare
    COMMAND perf_compare
)
