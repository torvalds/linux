//===-- tsan_mutexset.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// MutexSet holds the set of mutexes currently held by a thread.
//===----------------------------------------------------------------------===//
#ifndef TSAN_MUTEXSET_H
#define TSAN_MUTEXSET_H

#include "tsan_defs.h"

namespace __tsan {

class MutexSet {
 public:
  // Holds limited number of mutexes.
  // The oldest mutexes are discarded on overflow.
  static constexpr uptr kMaxSize = 16;
  struct Desc {
    uptr addr;
    StackID stack_id;
    u32 seq;
    u32 count;
    bool write;

    Desc() { internal_memset(this, 0, sizeof(*this)); }
    Desc(const Desc& other) { *this = other; }
    Desc& operator=(const MutexSet::Desc& other) {
      internal_memcpy(this, &other, sizeof(*this));
      return *this;
    }
  };

  MutexSet();
  void Reset();
  void AddAddr(uptr addr, StackID stack_id, bool write);
  void DelAddr(uptr addr, bool destroy = false);
  uptr Size() const;
  Desc Get(uptr i) const;

 private:
#if !SANITIZER_GO
  u32 seq_ = 0;
  uptr size_ = 0;
  Desc descs_[kMaxSize];

  void RemovePos(uptr i);
#endif
};

// MutexSet is too large to live on stack.
// DynamicMutexSet can be use used to create local MutexSet's.
class DynamicMutexSet {
 public:
  DynamicMutexSet();
  ~DynamicMutexSet();
  MutexSet* operator->() { return ptr_; }
  operator MutexSet*() { return ptr_; }
  DynamicMutexSet(const DynamicMutexSet&) = delete;
  DynamicMutexSet& operator=(const DynamicMutexSet&) = delete;

 private:
  MutexSet* ptr_;
#if SANITIZER_GO
  MutexSet set_;
#endif
};

// Go does not have mutexes, so do not spend memory and time.
// (Go sync.Mutex is actually a semaphore -- can be unlocked
// in different goroutine).
#if SANITIZER_GO
MutexSet::MutexSet() {}
void MutexSet::Reset() {}
void MutexSet::AddAddr(uptr addr, StackID stack_id, bool write) {}
void MutexSet::DelAddr(uptr addr, bool destroy) {}
uptr MutexSet::Size() const { return 0; }
MutexSet::Desc MutexSet::Get(uptr i) const { return Desc(); }
DynamicMutexSet::DynamicMutexSet() : ptr_(&set_) {}
DynamicMutexSet::~DynamicMutexSet() {}
#endif

}  // namespace __tsan

#endif  // TSAN_MUTEXSET_H
