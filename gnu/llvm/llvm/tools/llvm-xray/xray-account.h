//===- xray-account.h - XRay Function Call Accounting ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interface for performing some basic function call
// accounting from an XRay trace.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TOOLS_LLVM_XRAY_XRAY_ACCOUNT_H
#define LLVM_TOOLS_LLVM_XRAY_XRAY_ACCOUNT_H

#include <utility>

#include "func-id-helper.h"
#include "llvm/ADT/Bitfields.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/XRay/XRayRecord.h"

namespace llvm {
namespace xray {

class LatencyAccountant {
public:
  typedef llvm::DenseMap<int32_t, llvm::SmallVector<uint64_t, 0>>
      FunctionLatencyMap;
  typedef llvm::DenseMap<uint32_t, std::pair<uint64_t, uint64_t>>
      PerThreadMinMaxTSCMap;
  typedef llvm::DenseMap<uint8_t, std::pair<uint64_t, uint64_t>>
      PerCPUMinMaxTSCMap;
  struct FunctionStack {
    llvm::SmallVector<std::pair<int32_t, uint64_t>, 32> Stack;
    class RecursionStatus {
      uint32_t Storage = 0;
      using Depth = Bitfield::Element<int32_t, 0, 31>;    // Low 31 bits.
      using IsRecursive = Bitfield::Element<bool, 31, 1>; // Sign bit.
    public:
      RecursionStatus &operator++();
      RecursionStatus &operator--();
      bool isRecursive() const;
    };
    std::optional<llvm::DenseMap<int32_t, RecursionStatus>> RecursionDepth;
  };
  typedef llvm::DenseMap<uint32_t, FunctionStack> PerThreadFunctionStackMap;

private:
  PerThreadFunctionStackMap PerThreadFunctionStack;
  FunctionLatencyMap FunctionLatencies;
  PerThreadMinMaxTSCMap PerThreadMinMaxTSC;
  PerCPUMinMaxTSCMap PerCPUMinMaxTSC;
  FuncIdConversionHelper &FuncIdHelper;

  bool RecursiveCallsOnly = false;
  bool DeduceSiblingCalls = false;
  uint64_t CurrentMaxTSC = 0;

  void recordLatency(int32_t FuncId, uint64_t Latency) {
    FunctionLatencies[FuncId].push_back(Latency);
  }

public:
  explicit LatencyAccountant(FuncIdConversionHelper &FuncIdHelper,
                             bool RecursiveCallsOnly, bool DeduceSiblingCalls)
      : FuncIdHelper(FuncIdHelper), RecursiveCallsOnly(RecursiveCallsOnly),
        DeduceSiblingCalls(DeduceSiblingCalls) {}

  const FunctionLatencyMap &getFunctionLatencies() const {
    return FunctionLatencies;
  }

  const PerThreadMinMaxTSCMap &getPerThreadMinMaxTSC() const {
    return PerThreadMinMaxTSC;
  }

  const PerCPUMinMaxTSCMap &getPerCPUMinMaxTSC() const {
    return PerCPUMinMaxTSC;
  }

  /// Returns false in case we fail to account the provided record. This happens
  /// in the following cases:
  ///
  ///   - An exit record does not match any entry records for the same function.
  ///     If we've been set to deduce sibling calls, we try walking up the stack
  ///     and recording times for the higher level functions.
  ///   - A record has a TSC that's before the latest TSC that has been
  ///     recorded. We still record the TSC for the min-max.
  ///
  bool accountRecord(const XRayRecord &Record);

  const PerThreadFunctionStackMap &getPerThreadFunctionStack() const {
    return PerThreadFunctionStack;
  }

  // Output Functions
  // ================

  void exportStatsAsText(raw_ostream &OS, const XRayFileHeader &Header) const;
  void exportStatsAsCSV(raw_ostream &OS, const XRayFileHeader &Header) const;

private:
  // Internal helper to implement common parts of the exportStatsAs...
  // functions.
  template <class F> void exportStats(const XRayFileHeader &Header, F fn) const;
};

} // namespace xray
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_XRAY_XRAY_ACCOUNT_H
