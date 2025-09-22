//===-- memprof_descriptions.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// MemProf-private header for memprof_descriptions.cpp.
//===----------------------------------------------------------------------===//
#ifndef MEMPROF_DESCRIPTIONS_H
#define MEMPROF_DESCRIPTIONS_H

#include "memprof_allocator.h"
#include "memprof_thread.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_report_decorator.h"

namespace __memprof {

void DescribeThread(MemprofThreadContext *context);
inline void DescribeThread(MemprofThread *t) {
  if (t)
    DescribeThread(t->context());
}

class MemprofThreadIdAndName {
public:
  explicit MemprofThreadIdAndName(MemprofThreadContext *t);
  explicit MemprofThreadIdAndName(u32 tid);

  // Contains "T%tid (%name)" or "T%tid" if the name is empty.
  const char *c_str() const { return &name[0]; }

private:
  void Init(u32 tid, const char *tname);

  char name[128];
};

} // namespace __memprof

#endif // MEMPROF_DESCRIPTIONS_H
