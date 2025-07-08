# cmake_minimum_required(VERSION 3.10)
# project(memory_pool)

# # 设置C++标准
# set(CMAKE_CXX_STANDARD 17)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)

# # 编译选项
# add_compile_options(-Wall -O2)

# # 查找pthread库
# find_package(Threads REQUIRED)

# # 设置目录
# set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
# set(INC_DIR ${CMAKE_SOURCE_DIR}/include)
# set(TEST_DIR ${CMAKE_SOURCE_DIR}/tests)

# # 源文件
# file(GLOB SOURCES "${SRC_DIR}/*.cpp")

# # 添加头文件目录
# include_directories(${INC_DIR})

# # 创建单元测试可执行文件
# add_executable(unit_test 
#     ${SOURCES}
#     ${TEST_DIR}/UnitTest.cpp
# )

# # 创建性能测试可执行文件
# add_executable(perf_test
#     ${SOURCES}
#     ${TEST_DIR}/PerformanceTest.cpp
# )

# # 链接pthread库
# target_link_libraries(unit_test PRIVATE Threads::Threads)
# target_link_libraries(perf_test PRIVATE Threads::Threads)

# # 添加测试命令
# add_custom_target(test
#     COMMAND ./unit_test
#     DEPENDS unit_test
# )

# add_custom_target(perf
#     COMMAND ./perf_test
#     DEPENDS perf_test
# )

cmake_minimum_required(VERSION 3.20)
project(MemoryPoolDemo LANGUAGES CXX)

# —— 1. 通用编译选项 —— 
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

option(SANITIZE "Enable Address/Undefined behaviour sanitizers" OFF)
if (SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options    (-fsanitize=address,undefined)
endif()

# —— 2. 包含路径 —— 
include_directories(include)

# —— 3. 库目标 —— 
file(GLOB_RECURSE MEMPOOL_SRC CONFIGURE_DEPENDS src/*.cpp)
add_library(mempool STATIC ${MEMPOOL_SRC})
if (NOT MSVC)
    target_link_libraries(mempool PUBLIC pthread)
endif()

# —— 4. demotest 可执行 —— 
add_executable(demotest tests/demotest.cpp)
target_link_libraries(demotest PRIVATE mempool)

# —— 5. 输出目录 —— 
set_target_properties(mempool PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set_target_properties(demotest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
