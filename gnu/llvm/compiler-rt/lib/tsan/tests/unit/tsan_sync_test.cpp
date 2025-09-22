//===-- tsan_sync_test.cpp ------------------------------------------------===//
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
#include "tsan_sync.h"
#include "tsan_rtl.h"
#include "gtest/gtest.h"

namespace __tsan {

TEST(MetaMap, Basic) {
  ThreadState *thr = cur_thread();
  SlotLocker locker(thr);
  MetaMap *m = &ctx->metamap;
  u64 block[1] = {};  // fake malloc block
  m->AllocBlock(thr, 0, (uptr)&block[0], 1 * sizeof(u64));
  MBlock *mb = m->GetBlock((uptr)&block[0]);
  CHECK_NE(mb, (MBlock *)0);
  CHECK_EQ(mb->siz, 1 * sizeof(u64));
  CHECK_EQ(mb->tid, thr->tid);
  uptr sz = m->FreeBlock(thr->proc(), (uptr)&block[0], true);
  CHECK_EQ(sz, 1 * sizeof(u64));
  mb = m->GetBlock((uptr)&block[0]);
  CHECK_EQ(mb, (MBlock *)0);
}

TEST(MetaMap, FreeRange) {
  ThreadState *thr = cur_thread();
  SlotLocker locker(thr);
  MetaMap *m = &ctx->metamap;
  u64 block[4] = {};  // fake malloc block
  m->AllocBlock(thr, 0, (uptr)&block[0], 1 * sizeof(u64));
  m->AllocBlock(thr, 0, (uptr)&block[1], 3 * sizeof(u64));
  MBlock *mb1 = m->GetBlock((uptr)&block[0]);
  CHECK_EQ(mb1->siz, 1 * sizeof(u64));
  MBlock *mb2 = m->GetBlock((uptr)&block[1]);
  CHECK_EQ(mb2->siz, 3 * sizeof(u64));
  m->FreeRange(thr->proc(), (uptr)&block[0], 4 * sizeof(u64), true);
  mb1 = m->GetBlock((uptr)&block[0]);
  CHECK_EQ(mb1, (MBlock *)0);
  mb2 = m->GetBlock((uptr)&block[1]);
  CHECK_EQ(mb2, (MBlock *)0);
}

TEST(MetaMap, Sync) {
  // CHECK can call memset/etc. Disable interceptors to prevent
  // them from detecting that we exit runtime with mutexes held.
  ScopedIgnoreInterceptors ignore;
  ThreadState *thr = cur_thread();
  SlotLocker locker(thr);
  MetaMap *m = &ctx->metamap;
  u64 block[4] = {};  // fake malloc block
  m->AllocBlock(thr, 0, (uptr)&block[0], 4 * sizeof(u64));
  SyncVar *s1 = m->GetSyncIfExists((uptr)&block[0]);
  CHECK_EQ(s1, (SyncVar *)0);
  s1 = m->GetSyncOrCreate(thr, 0, (uptr)&block[0], false);
  CHECK_NE(s1, (SyncVar *)0);
  CHECK_EQ(s1->addr, (uptr)&block[0]);
  SyncVar *s2 = m->GetSyncOrCreate(thr, 0, (uptr)&block[1], false);
  CHECK_NE(s2, (SyncVar *)0);
  CHECK_EQ(s2->addr, (uptr)&block[1]);
  m->FreeBlock(thr->proc(), (uptr)&block[0], true);
  s1 = m->GetSyncIfExists((uptr)&block[0]);
  CHECK_EQ(s1, (SyncVar *)0);
  s2 = m->GetSyncIfExists((uptr)&block[1]);
  CHECK_EQ(s2, (SyncVar *)0);
  m->OnProcIdle(thr->proc());
}

TEST(MetaMap, MoveMemory) {
  ScopedIgnoreInterceptors ignore;
  ThreadState *thr = cur_thread();
  SlotLocker locker(thr);
  MetaMap *m = &ctx->metamap;
  u64 block1[4] = {};  // fake malloc block
  u64 block2[4] = {};  // fake malloc block
  m->AllocBlock(thr, 0, (uptr)&block1[0], 3 * sizeof(u64));
  m->AllocBlock(thr, 0, (uptr)&block1[3], 1 * sizeof(u64));
  SyncVar *s1 = m->GetSyncOrCreate(thr, 0, (uptr)&block1[0], false);
  SyncVar *s2 = m->GetSyncOrCreate(thr, 0, (uptr)&block1[1], false);
  m->MoveMemory((uptr)&block1[0], (uptr)&block2[0], 4 * sizeof(u64));
  MBlock *mb1 = m->GetBlock((uptr)&block1[0]);
  CHECK_EQ(mb1, (MBlock *)0);
  MBlock *mb2 = m->GetBlock((uptr)&block1[3]);
  CHECK_EQ(mb2, (MBlock *)0);
  mb1 = m->GetBlock((uptr)&block2[0]);
  CHECK_NE(mb1, (MBlock *)0);
  CHECK_EQ(mb1->siz, 3 * sizeof(u64));
  mb2 = m->GetBlock((uptr)&block2[3]);
  CHECK_NE(mb2, (MBlock *)0);
  CHECK_EQ(mb2->siz, 1 * sizeof(u64));
  s1 = m->GetSyncIfExists((uptr)&block1[0]);
  CHECK_EQ(s1, (SyncVar *)0);
  s2 = m->GetSyncIfExists((uptr)&block1[1]);
  CHECK_EQ(s2, (SyncVar *)0);
  s1 = m->GetSyncIfExists((uptr)&block2[0]);
  CHECK_NE(s1, (SyncVar *)0);
  CHECK_EQ(s1->addr, (uptr)&block2[0]);
  s2 = m->GetSyncIfExists((uptr)&block2[1]);
  CHECK_NE(s2, (SyncVar *)0);
  CHECK_EQ(s2->addr, (uptr)&block2[1]);
  m->FreeRange(thr->proc(), (uptr)&block2[0], 4 * sizeof(u64), true);
}

TEST(MetaMap, ResetSync) {
  ScopedIgnoreInterceptors ignore;
  ThreadState *thr = cur_thread();
  SlotLocker locker(thr);
  MetaMap *m = &ctx->metamap;
  u64 block[1] = {};  // fake malloc block
  m->AllocBlock(thr, 0, (uptr)&block[0], 1 * sizeof(u64));
  SyncVar *s = m->GetSyncOrCreate(thr, 0, (uptr)&block[0], false);
  s->Reset();
  uptr sz = m->FreeBlock(thr->proc(), (uptr)&block[0], true);
  CHECK_EQ(sz, 1 * sizeof(u64));
}

}  // namespace __tsan
