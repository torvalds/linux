//===-- tsan_test_util.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Test utils.
//===----------------------------------------------------------------------===//
#ifndef TSAN_TEST_UTIL_H
#define TSAN_TEST_UTIL_H

#include "gtest/gtest.h"

class ThreadSanitizer : public ::testing::Test {
 protected:
  void TearDown() override;
};

void TestMutexBeforeInit();

// A location of memory on which a race may be detected.
class MemLoc {
 public:
  explicit MemLoc(int offset_from_aligned = 0);
  explicit MemLoc(void *const real_addr) : loc_(real_addr) { }
  ~MemLoc();
  void *loc() const { return loc_; }
 private:
  void *const loc_;
  MemLoc(const MemLoc&);
  void operator = (const MemLoc&);
};

class UserMutex {
 public:
  enum Type {
    Normal,
    RW,
#ifndef __APPLE__
    Spin
#else
    Spin = Normal
#endif
  };

  explicit UserMutex(Type type = Normal);
  ~UserMutex();

  void Init();
  void StaticInit();  // Emulates static initialization (tsan invisible).
  void Destroy();
  void Lock();
  bool TryLock();
  void Unlock();
  void ReadLock();
  bool TryReadLock();
  void ReadUnlock();

 private:
  // Placeholder for pthread_mutex_t, CRITICAL_SECTION or whatever.
  void *mtx_[128];
  bool alive_;
  const Type type_;

  UserMutex(const UserMutex &);
  void operator=(const UserMutex &);
};

// A thread is started in CTOR and joined in DTOR.
class ScopedThread {
 public:
  explicit ScopedThread(bool detached = false, bool main = false);
  ~ScopedThread();
  void Detach();

  void Access(void *addr, bool is_write, int size, bool expect_race);
  void Read(const MemLoc &ml, int size, bool expect_race = false) {
    Access(ml.loc(), false, size, expect_race);
  }
  void Write(const MemLoc &ml, int size, bool expect_race = false) {
    Access(ml.loc(), true, size, expect_race);
  }
  void Read1(const MemLoc &ml, bool expect_race = false) {
    Read(ml, 1, expect_race); }
  void Read2(const MemLoc &ml, bool expect_race = false) {
    Read(ml, 2, expect_race); }
  void Read4(const MemLoc &ml, bool expect_race = false) {
    Read(ml, 4, expect_race); }
  void Read8(const MemLoc &ml, bool expect_race = false) {
    Read(ml, 8, expect_race); }
  void Write1(const MemLoc &ml, bool expect_race = false) {
    Write(ml, 1, expect_race); }
  void Write2(const MemLoc &ml, bool expect_race = false) {
    Write(ml, 2, expect_race); }
  void Write4(const MemLoc &ml, bool expect_race = false) {
    Write(ml, 4, expect_race); }
  void Write8(const MemLoc &ml, bool expect_race = false) {
    Write(ml, 8, expect_race); }

  void VptrUpdate(const MemLoc &vptr, const MemLoc &new_val,
                  bool expect_race = false);

  void Call(void(*pc)());
  void Return();

  void Create(const UserMutex &m);
  void Destroy(const UserMutex &m);
  void Lock(const UserMutex &m);
  bool TryLock(const UserMutex &m);
  void Unlock(const UserMutex &m);
  void ReadLock(const UserMutex &m);
  bool TryReadLock(const UserMutex &m);
  void ReadUnlock(const UserMutex &m);

  void Memcpy(void *dst, const void *src, int size, bool expect_race = false);
  void Memset(void *dst, int val, int size, bool expect_race = false);

 private:
  struct Impl;
  Impl *impl_;
  ScopedThread(const ScopedThread&);  // Not implemented.
  void operator = (const ScopedThread&);  // Not implemented.
};

class MainThread : public ScopedThread {
 public:
  MainThread()
    : ScopedThread(false, true) {
  }
};

#endif  // #ifndef TSAN_TEST_UTIL_H
