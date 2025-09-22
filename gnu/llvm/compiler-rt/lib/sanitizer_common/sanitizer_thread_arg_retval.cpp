//===-- sanitizer_thread_arg_retval.cpp -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between sanitizer tools.
//
// Tracks thread arguments and return value for leak checking.
//===----------------------------------------------------------------------===//

#include "sanitizer_thread_arg_retval.h"

#include "sanitizer_placement_new.h"

namespace __sanitizer {

void ThreadArgRetval::CreateLocked(uptr thread, bool detached,
                                   const Args& args) {
  CheckLocked();
  Data& t = data_[thread];
  t = {};
  t.gen = gen_++;
  static_assert(sizeof(gen_) == sizeof(u32) && kInvalidGen == UINT32_MAX);
  if (gen_ == kInvalidGen)
    gen_ = 0;
  t.detached = detached;
  t.args = args;
}

ThreadArgRetval::Args ThreadArgRetval::GetArgs(uptr thread) const {
  __sanitizer::Lock lock(&mtx_);
  auto t = data_.find(thread);
  CHECK(t);
  if (t->second.done)
    return {};
  return t->second.args;
}

void ThreadArgRetval::Finish(uptr thread, void* retval) {
  __sanitizer::Lock lock(&mtx_);
  auto t = data_.find(thread);
  if (!t)
    return;
  if (t->second.detached) {
    // Retval of detached thread connot be retrieved.
    data_.erase(t);
    return;
  }
  t->second.done = true;
  t->second.args.arg_retval = retval;
}

u32 ThreadArgRetval::BeforeJoin(uptr thread) const {
  __sanitizer::Lock lock(&mtx_);
  auto t = data_.find(thread);
  if (t && !t->second.detached) {
    return t->second.gen;
  }
  if (!common_flags()->detect_invalid_join)
    return kInvalidGen;
  const char* reason = "unknown";
  if (!t) {
    reason = "already joined";
  } else if (t->second.detached) {
    reason = "detached";
  }
  Report("ERROR: %s: Joining %s thread, aborting.\n", SanitizerToolName,
         reason);
  Die();
}

void ThreadArgRetval::AfterJoin(uptr thread, u32 gen) {
  __sanitizer::Lock lock(&mtx_);
  auto t = data_.find(thread);
  if (!t || gen != t->second.gen) {
    // Thread was reused and erased by any other event, or we had an invalid
    // join.
    return;
  }
  CHECK(!t->second.detached);
  data_.erase(t);
}

void ThreadArgRetval::DetachLocked(uptr thread) {
  CheckLocked();
  auto t = data_.find(thread);
  CHECK(t);
  CHECK(!t->second.detached);
  if (t->second.done) {
    // We can't retrive retval after detached thread finished.
    data_.erase(t);
    return;
  }
  t->second.detached = true;
}

void ThreadArgRetval::GetAllPtrsLocked(InternalMmapVector<uptr>* ptrs) {
  CheckLocked();
  CHECK(ptrs);
  data_.forEach([&](DenseMap<uptr, Data>::value_type& kv) -> bool {
    ptrs->push_back((uptr)kv.second.args.arg_retval);
    return true;
  });
}

}  // namespace __sanitizer
