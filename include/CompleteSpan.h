#pragma once

#include <map>
#include <Span.h>

struct CompleteSpan {
    std::map<void*, Span*> compSpanMap_;

    CompleteSpan(void* addr_, Span* span_) { compSpanMap_[addr_] = span_; }
};