#pragma once

#include <cstddef>

struct Span {
    void* pageAddr;  // 起始地址
    size_t numPages; // 页数
    Span* next;      // 后一个 Span
    Span* headSpan;  // 如果自己就是 head 则为 nullptr. 注意，这里找头需要一直循环至 headSpan 为
                     // nullptr，headSpan 并未直接一次指向头结点
    bool isUsed;

    Span() : pageAddr(nullptr), numPages(0), next(nullptr), headSpan(nullptr), isUsed(false) {};
    Span(void* pageAddr_, size_t numPages_, Span* next_, Span* headSpan_, bool isUsed_)
        : pageAddr(pageAddr_), numPages(numPages_), next(next_), headSpan(headSpan_),
          isUsed(isUsed_) {};
};