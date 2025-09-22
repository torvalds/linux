//===-- tsan_mop.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#include "tsan_interface.h"
#include "tsan_test_util.h"
#include "gtest/gtest.h"
#include <stddef.h>
#include <stdint.h>

TEST_F(ThreadSanitizer, SimpleWrite) {
  ScopedThread t;
  MemLoc l;
  t.Write1(l);
}

TEST_F(ThreadSanitizer, SimpleWriteWrite) {
  ScopedThread t1, t2;
  MemLoc l1, l2;
  t1.Write1(l1);
  t2.Write1(l2);
}

TEST_F(ThreadSanitizer, WriteWriteRace) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Write1(l);
  t2.Write1(l, true);
}

TEST_F(ThreadSanitizer, ReadWriteRace) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Read1(l);
  t2.Write1(l, true);
}

TEST_F(ThreadSanitizer, WriteReadRace) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Write1(l);
  t2.Read1(l, true);
}

TEST_F(ThreadSanitizer, ReadReadNoRace) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Read1(l);
  t2.Read1(l);
}

TEST_F(ThreadSanitizer, WriteThenRead) {
  MemLoc l;
  ScopedThread t1, t2;
  t1.Write1(l);
  t1.Read1(l);
  t2.Read1(l, true);
}

TEST_F(ThreadSanitizer, WriteThenLockedRead) {
  UserMutex m(UserMutex::RW);
  MainThread t0;
  t0.Create(m);
  MemLoc l;
  {
    ScopedThread t1, t2;

    t1.Write8(l);

    t1.Lock(m);
    t1.Read8(l);
    t1.Unlock(m);

    t2.Read8(l, true);
  }
  t0.Destroy(m);
}

TEST_F(ThreadSanitizer, LockedWriteThenRead) {
  UserMutex m(UserMutex::RW);
  MainThread t0;
  t0.Create(m);
  MemLoc l;
  {
    ScopedThread t1, t2;

    t1.Lock(m);
    t1.Write8(l);
    t1.Unlock(m);

    t1.Read8(l);

    t2.Read8(l, true);
  }
  t0.Destroy(m);
}


TEST_F(ThreadSanitizer, RaceWithOffset) {
  ScopedThread t1, t2;
  {
    MemLoc l;
    t1.Access(l.loc(), true, 8, false);
    t2.Access((char*)l.loc() + 4, true, 4, true);
  }
  {
    MemLoc l;
    t1.Access(l.loc(), true, 8, false);
    t2.Access((char*)l.loc() + 7, true, 1, true);
  }
  {
    MemLoc l;
    t1.Access((char*)l.loc() + 4, true, 4, false);
    t2.Access((char*)l.loc() + 4, true, 2, true);
  }
  {
    MemLoc l;
    t1.Access((char*)l.loc() + 4, true, 4, false);
    t2.Access((char*)l.loc() + 6, true, 2, true);
  }
  {
    MemLoc l;
    t1.Access((char*)l.loc() + 3, true, 2, false);
    t2.Access((char*)l.loc() + 4, true, 1, true);
  }
  {
    MemLoc l;
    t1.Access((char*)l.loc() + 1, true, 8, false);
    t2.Access((char*)l.loc() + 3, true, 1, true);
  }
}

TEST_F(ThreadSanitizer, RaceWithOffset2) {
  ScopedThread t1, t2;
  {
    MemLoc l;
    t1.Access((char*)l.loc(), true, 4, false);
    t2.Access((char*)l.loc() + 2, true, 1, true);
  }
  {
    MemLoc l;
    t1.Access((char*)l.loc() + 2, true, 1, false);
    t2.Access((char*)l.loc(), true, 4, true);
  }
}

TEST_F(ThreadSanitizer, NoRaceWithOffset) {
  ScopedThread t1, t2;
  {
    MemLoc l;
    t1.Access(l.loc(), true, 4, false);
    t2.Access((char*)l.loc() + 4, true, 4, false);
  }
  {
    MemLoc l;
    t1.Access((char*)l.loc() + 3, true, 2, false);
    t2.Access((char*)l.loc() + 1, true, 2, false);
    t2.Access((char*)l.loc() + 5, true, 2, false);
  }
}

TEST_F(ThreadSanitizer, RaceWithDeadThread) {
  MemLoc l;
  ScopedThread t;
  ScopedThread().Write1(l);
  t.Write1(l, true);
}

TEST_F(ThreadSanitizer, BenignRaceOnVptr) {
  void *vptr_storage;
  MemLoc vptr(&vptr_storage), val;
  vptr_storage = val.loc();
  ScopedThread t1, t2;
  t1.VptrUpdate(vptr, val);
  t2.Read8(vptr);
}

TEST_F(ThreadSanitizer, HarmfulRaceOnVptr) {
  void *vptr_storage;
  MemLoc vptr(&vptr_storage), val1, val2;
  vptr_storage = val1.loc();
  ScopedThread t1, t2;
  t1.VptrUpdate(vptr, val2);
  t2.Read8(vptr, true);
}

static void foo() {
  volatile int x = 42;
  int x2 = x;
  (void)x2;
}

static void bar() {
  volatile int x = 43;
  int x2 = x;
  (void)x2;
}

TEST_F(ThreadSanitizer, ReportDeadThread) {
  MemLoc l;
  ScopedThread t1;
  {
    ScopedThread t2;
    t2.Call(&foo);
    t2.Call(&bar);
    t2.Write1(l);
  }
  t1.Write1(l, true);
}

struct ClassWithStatic {
  static int Data[4];
};

int ClassWithStatic::Data[4];

static void foobarbaz() {}

TEST_F(ThreadSanitizer, ReportRace) {
  ScopedThread t1;
  MainThread().Access(&ClassWithStatic::Data, true, 4, false);
  t1.Call(&foobarbaz);
  t1.Access(&ClassWithStatic::Data, true, 2, true);
  t1.Return();
}
