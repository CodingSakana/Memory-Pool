#pragma once

#include <map>
#include <Span.h>

struct CompleteSpan {
    std::map<void*, Span*> spanMap_;

    CompleteSpan(void* addr_, Span* span_) { spanMap_[addr_] = span_; }
};