/*===- DataFlowCallbacks.cpp - a standalone DataFlow trace          -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Instrumentation callbacks for DataFlow.cpp.
// These functions should not be instrumented by DFSan, so we
// keep them in a separate file and compile it w/o DFSan.
//===----------------------------------------------------------------------===*/
#include "DataFlow.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

static __thread size_t CurrentFunc;
static uint32_t *GuardsBeg, *GuardsEnd;
static inline bool BlockIsEntry(size_t BlockIdx) {
  return __dft.PCsBeg[BlockIdx * 2 + 1] & PCFLAG_FUNC_ENTRY;
}

extern "C" {

void __sanitizer_cov_trace_pc_guard_init(uint32_t *start,
                                         uint32_t *stop) {
  assert(__dft.NumFuncs == 0 && "This tool does not support DSOs");
  assert(start < stop && "The code is not instrumented for coverage");
  if (start == stop || *start) return;  // Initialize only once.
  GuardsBeg = start;
  GuardsEnd = stop;
}

void __sanitizer_cov_pcs_init(const uintptr_t *pcs_beg,
                              const uintptr_t *pcs_end) {
  if (__dft.NumGuards) return;  // Initialize only once.
  __dft.NumGuards = GuardsEnd - GuardsBeg;
  __dft.PCsBeg = pcs_beg;
  __dft.PCsEnd = pcs_end;
  assert(__dft.NumGuards == (__dft.PCsEnd - __dft.PCsBeg) / 2);
  for (size_t i = 0; i < __dft.NumGuards; i++) {
    if (BlockIsEntry(i)) {
      __dft.NumFuncs++;
      GuardsBeg[i] = __dft.NumFuncs;
    }
  }
  __dft.BBExecuted = (bool*)calloc(__dft.NumGuards, sizeof(bool));
  fprintf(stderr, "INFO: %zd instrumented function(s) observed "
          "and %zd basic blocks\n", __dft.NumFuncs, __dft.NumGuards);
}

void __sanitizer_cov_trace_pc_indir(uint64_t x){}  // unused.

void __sanitizer_cov_trace_pc_guard(uint32_t *guard) {
  size_t GuardIdx = guard - GuardsBeg;
  // assert(GuardIdx < __dft.NumGuards);
  __dft.BBExecuted[GuardIdx] = true;
  if (!*guard) return;  // not a function entry.
  uint32_t FuncNum = *guard - 1;  // Guards start from 1.
  // assert(FuncNum < __dft.NumFuncs);
  CurrentFunc = FuncNum;
}

void __dfsw___sanitizer_cov_trace_switch(uint64_t Val, uint64_t *Cases,
                                         dfsan_label L1, dfsan_label UnusedL) {
  assert(CurrentFunc < __dft.NumFuncs);
  __dft.FuncLabels[CurrentFunc] |= L1;
}

#define HOOK(Name, Type)                                                       \
  void Name(Type Arg1, Type Arg2, dfsan_label L1, dfsan_label L2) {            \
    __dft.FuncLabels[CurrentFunc] |= L1 | L2;                                  \
  }
    //assert(CurrentFunc < __dft.NumFuncs);

HOOK(__dfsw___sanitizer_cov_trace_const_cmp1, uint8_t)
HOOK(__dfsw___sanitizer_cov_trace_const_cmp2, uint16_t)
HOOK(__dfsw___sanitizer_cov_trace_const_cmp4, uint32_t)
HOOK(__dfsw___sanitizer_cov_trace_const_cmp8, uint64_t)
HOOK(__dfsw___sanitizer_cov_trace_cmp1, uint8_t)
HOOK(__dfsw___sanitizer_cov_trace_cmp2, uint16_t)
HOOK(__dfsw___sanitizer_cov_trace_cmp4, uint32_t)
HOOK(__dfsw___sanitizer_cov_trace_cmp8, uint64_t)

} // extern "C"
