//===-- X86Counter.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Perf counter that reads the LBRs for measuring the benchmarked block's
/// throughput.
///
/// More info at: https://lwn.net/Articles/680985
//===----------------------------------------------------------------------===//
#ifndef LLVM_TOOLS_LLVM_EXEGESIS_LIB_X86_X86COUNTER_H
#define LLVM_TOOLS_LLVM_EXEGESIS_LIB_X86_X86COUNTER_H

#include "../PerfHelper.h"
#include "llvm/Support/Error.h"

// FIXME: Use appropriate wrappers for poll.h and mman.h
// to support Windows and remove this linux-only guard.
#if defined(__linux__) && defined(HAVE_LIBPFM) &&                              \
    defined(LIBPFM_HAS_FIELD_CYCLES)

namespace llvm {
namespace exegesis {

class X86LbrPerfEvent : public pfm::PerfEvent {
public:
  X86LbrPerfEvent(unsigned SamplingPeriod);
};

class X86LbrCounter : public pfm::CounterGroup {
public:
  static Error checkLbrSupport();

  explicit X86LbrCounter(pfm::PerfEvent &&Event);

  virtual ~X86LbrCounter();

  void start() override;

  Expected<SmallVector<int64_t, 4>>
  readOrError(StringRef FunctionBytes) const override;

private:
  Expected<SmallVector<int64_t, 4>> doReadCounter(const void *From,
                                                  const void *To) const;

  void *MMappedBuffer = nullptr;
};

} // namespace exegesis
} // namespace llvm

#endif // defined(__linux__) && defined(HAVE_LIBPFM) &&
       // defined(LIBPFM_HAS_FIELD_CYCLES)

#endif // LLVM_TOOLS_LLVM_EXEGESIS_LIB_X86_X86COUNTER_H
