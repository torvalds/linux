//===-- memprof_descriptions.cpp -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// MemProf functions for getting information about an address and/or printing
// it.
//===----------------------------------------------------------------------===//

#include "memprof_descriptions.h"
#include "memprof_mapping.h"
#include "memprof_stack.h"
#include "sanitizer_common/sanitizer_stackdepot.h"

namespace __memprof {

MemprofThreadIdAndName::MemprofThreadIdAndName(MemprofThreadContext *t) {
  Init(t->tid, t->name);
}

MemprofThreadIdAndName::MemprofThreadIdAndName(u32 tid) {
  if (tid == kInvalidTid) {
    Init(tid, "");
  } else {
    memprofThreadRegistry().CheckLocked();
    MemprofThreadContext *t = GetThreadContextByTidLocked(tid);
    Init(tid, t->name);
  }
}

void MemprofThreadIdAndName::Init(u32 tid, const char *tname) {
  int len = internal_snprintf(name, sizeof(name), "T%d", tid);
  CHECK(((unsigned int)len) < sizeof(name));
  if (tname[0] != '\0')
    internal_snprintf(&name[len], sizeof(name) - len, " (%s)", tname);
}

void DescribeThread(MemprofThreadContext *context) {
  CHECK(context);
  memprofThreadRegistry().CheckLocked();
  // No need to announce the main thread.
  if (context->tid == kMainTid || context->announced) {
    return;
  }
  context->announced = true;
  InternalScopedString str;
  str.AppendF("Thread %s", MemprofThreadIdAndName(context).c_str());
  if (context->parent_tid == kInvalidTid) {
    str.Append(" created by unknown thread\n");
    Printf("%s", str.data());
    return;
  }
  str.AppendF(" created by %s here:\n",
              MemprofThreadIdAndName(context->parent_tid).c_str());
  Printf("%s", str.data());
  StackDepotGet(context->stack_id).Print();
  // Recursively described parent thread if needed.
  if (flags()->print_full_thread_history) {
    MemprofThreadContext *parent_context =
        GetThreadContextByTidLocked(context->parent_tid);
    DescribeThread(parent_context);
  }
}

} // namespace __memprof
