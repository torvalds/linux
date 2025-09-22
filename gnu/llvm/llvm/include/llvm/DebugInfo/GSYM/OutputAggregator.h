//===- DwarfTransformer.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_OUTPUTAGGREGATOR_H
#define LLVM_DEBUGINFO_GSYM_OUTPUTAGGREGATOR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/GSYM/ExtractRanges.h"

#include <map>
#include <string>

namespace llvm {

class raw_ostream;

namespace gsym {

class OutputAggregator {
protected:
  // A std::map is preferable over an llvm::StringMap for presenting results
  // in a predictable order.
  std::map<std::string, unsigned> Aggregation;
  raw_ostream *Out;

public:
  OutputAggregator(raw_ostream *out) : Out(out) {}

  size_t GetNumCategories() const { return Aggregation.size(); }

  void Report(StringRef s, std::function<void(raw_ostream &o)> detailCallback) {
    Aggregation[std::string(s)]++;
    if (GetOS())
      detailCallback(*Out);
  }

  void EnumerateResults(
      std::function<void(StringRef, unsigned)> handleCounts) const {
    for (auto &&[name, count] : Aggregation)
      handleCounts(name, count);
  }

  raw_ostream *GetOS() const { return Out; }

  // You can just use the stream, and if it's null, nothing happens.
  // Don't do a lot of stuff like this, but it's convenient for silly stuff.
  // It doesn't work with things that have custom insertion operators, though.
  template <typename T> OutputAggregator &operator<<(T &&value) {
    if (Out != nullptr)
      *Out << value;
    return *this;
  }

  // For multi-threaded usage, we can collect stuff in another aggregator,
  // then merge it in here. Note that this is *not* thread safe. It is up to
  // the caller to ensure that this is only called from one thread at a time.
  void Merge(const OutputAggregator &other) {
    for (auto &&[name, count] : other.Aggregation) {
      auto [it, inserted] = Aggregation.emplace(name, count);
      if (!inserted)
        it->second += count;
    }
  }
};

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_OUTPUTAGGREGATOR_H
