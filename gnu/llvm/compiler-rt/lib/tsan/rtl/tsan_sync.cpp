//===-- tsan_sync.cpp -----------------------------------------------------===//
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
#include "sanitizer_common/sanitizer_placement_new.h"
#include "tsan_sync.h"
#include "tsan_rtl.h"
#include "tsan_mman.h"

namespace __tsan {

void DDMutexInit(ThreadState *thr, uptr pc, SyncVar *s);

SyncVar::SyncVar() : mtx(MutexTypeSyncVar) { Reset(); }

void SyncVar::Init(ThreadState *thr, uptr pc, uptr addr, bool save_stack) {
  Reset();
  this->addr = addr;
  next = 0;
  if (save_stack && !SANITIZER_GO)  // Go does not use them
    creation_stack_id = CurrentStackId(thr, pc);
  if (common_flags()->detect_deadlocks)
    DDMutexInit(thr, pc, this);
}

void SyncVar::Reset() {
  CHECK(!ctx->resetting);
  creation_stack_id = kInvalidStackID;
  owner_tid = kInvalidTid;
  last_lock.Reset();
  recursion = 0;
  atomic_store_relaxed(&flags, 0);
  Free(clock);
  Free(read_clock);
}

MetaMap::MetaMap()
    : block_alloc_("heap block allocator"), sync_alloc_("sync allocator") {}

void MetaMap::AllocBlock(ThreadState *thr, uptr pc, uptr p, uptr sz) {
  u32 idx = block_alloc_.Alloc(&thr->proc()->block_cache);
  MBlock *b = block_alloc_.Map(idx);
  b->siz = sz;
  b->tag = 0;
  b->tid = thr->tid;
  b->stk = CurrentStackId(thr, pc);
  u32 *meta = MemToMeta(p);
  DCHECK_EQ(*meta, 0);
  *meta = idx | kFlagBlock;
}

uptr MetaMap::FreeBlock(Processor *proc, uptr p, bool reset) {
  MBlock* b = GetBlock(p);
  if (b == 0)
    return 0;
  uptr sz = RoundUpTo(b->siz, kMetaShadowCell);
  FreeRange(proc, p, sz, reset);
  return sz;
}

bool MetaMap::FreeRange(Processor *proc, uptr p, uptr sz, bool reset) {
  bool has_something = false;
  u32 *meta = MemToMeta(p);
  u32 *end = MemToMeta(p + sz);
  if (end == meta)
    end++;
  for (; meta < end; meta++) {
    u32 idx = *meta;
    if (idx == 0) {
      // Note: don't write to meta in this case -- the block can be huge.
      continue;
    }
    *meta = 0;
    has_something = true;
    while (idx != 0) {
      if (idx & kFlagBlock) {
        block_alloc_.Free(&proc->block_cache, idx & ~kFlagMask);
        break;
      } else if (idx & kFlagSync) {
        DCHECK(idx & kFlagSync);
        SyncVar *s = sync_alloc_.Map(idx & ~kFlagMask);
        u32 next = s->next;
        if (reset)
          s->Reset();
        sync_alloc_.Free(&proc->sync_cache, idx & ~kFlagMask);
        idx = next;
      } else {
        CHECK(0);
      }
    }
  }
  return has_something;
}

// ResetRange removes all meta objects from the range.
// It is called for large mmap-ed regions. The function is best-effort wrt
// freeing of meta objects, because we don't want to page in the whole range
// which can be huge. The function probes pages one-by-one until it finds a page
// without meta objects, at this point it stops freeing meta objects. Because
// thread stacks grow top-down, we do the same starting from end as well.
void MetaMap::ResetRange(Processor *proc, uptr p, uptr sz, bool reset) {
  if (SANITIZER_GO) {
    // UnmapOrDie/MmapFixedNoReserve does not work on Windows,
    // so we do the optimization only for C/C++.
    FreeRange(proc, p, sz, reset);
    return;
  }
  const uptr kMetaRatio = kMetaShadowCell / kMetaShadowSize;
  const uptr kPageSize = GetPageSizeCached() * kMetaRatio;
  if (sz <= 4 * kPageSize) {
    // If the range is small, just do the normal free procedure.
    FreeRange(proc, p, sz, reset);
    return;
  }
  // First, round both ends of the range to page size.
  uptr diff = RoundUp(p, kPageSize) - p;
  if (diff != 0) {
    FreeRange(proc, p, diff, reset);
    p += diff;
    sz -= diff;
  }
  diff = p + sz - RoundDown(p + sz, kPageSize);
  if (diff != 0) {
    FreeRange(proc, p + sz - diff, diff, reset);
    sz -= diff;
  }
  // Now we must have a non-empty page-aligned range.
  CHECK_GT(sz, 0);
  CHECK_EQ(p, RoundUp(p, kPageSize));
  CHECK_EQ(sz, RoundUp(sz, kPageSize));
  const uptr p0 = p;
  const uptr sz0 = sz;
  // Probe start of the range.
  for (uptr checked = 0; sz > 0; checked += kPageSize) {
    bool has_something = FreeRange(proc, p, kPageSize, reset);
    p += kPageSize;
    sz -= kPageSize;
    if (!has_something && checked > (128 << 10))
      break;
  }
  // Probe end of the range.
  for (uptr checked = 0; sz > 0; checked += kPageSize) {
    bool has_something = FreeRange(proc, p + sz - kPageSize, kPageSize, reset);
    sz -= kPageSize;
    // Stacks grow down, so sync object are most likely at the end of the region
    // (if it is a stack). The very end of the stack is TLS and tsan increases
    // TLS by at least 256K, so check at least 512K.
    if (!has_something && checked > (512 << 10))
      break;
  }
  // Finally, page out the whole range (including the parts that we've just
  // freed). Note: we can't simply madvise, because we need to leave a zeroed
  // range (otherwise __tsan_java_move can crash if it encounters a left-over
  // meta objects in java heap).
  uptr metap = (uptr)MemToMeta(p0);
  uptr metasz = sz0 / kMetaRatio;
  UnmapOrDie((void*)metap, metasz);
  if (!MmapFixedSuperNoReserve(metap, metasz))
    Die();
}

void MetaMap::ResetClocks() {
  // This can be called from the background thread
  // which does not have proc/cache.
  // The cache is too large for stack.
  static InternalAllocatorCache cache;
  internal_memset(&cache, 0, sizeof(cache));
  internal_allocator()->InitCache(&cache);
  sync_alloc_.ForEach([&](SyncVar *s) {
    if (s->clock) {
      InternalFree(s->clock, &cache);
      s->clock = nullptr;
    }
    if (s->read_clock) {
      InternalFree(s->read_clock, &cache);
      s->read_clock = nullptr;
    }
    s->last_lock.Reset();
  });
  internal_allocator()->DestroyCache(&cache);
}

MBlock* MetaMap::GetBlock(uptr p) {
  u32 *meta = MemToMeta(p);
  u32 idx = *meta;
  for (;;) {
    if (idx == 0)
      return 0;
    if (idx & kFlagBlock)
      return block_alloc_.Map(idx & ~kFlagMask);
    DCHECK(idx & kFlagSync);
    SyncVar * s = sync_alloc_.Map(idx & ~kFlagMask);
    idx = s->next;
  }
}

SyncVar *MetaMap::GetSync(ThreadState *thr, uptr pc, uptr addr, bool create,
                          bool save_stack) {
  DCHECK(!create || thr->slot_locked);
  u32 *meta = MemToMeta(addr);
  u32 idx0 = *meta;
  u32 myidx = 0;
  SyncVar *mys = nullptr;
  for (;;) {
    for (u32 idx = idx0; idx && !(idx & kFlagBlock);) {
      DCHECK(idx & kFlagSync);
      SyncVar * s = sync_alloc_.Map(idx & ~kFlagMask);
      if (LIKELY(s->addr == addr)) {
        if (UNLIKELY(myidx != 0)) {
          mys->Reset();
          sync_alloc_.Free(&thr->proc()->sync_cache, myidx);
        }
        return s;
      }
      idx = s->next;
    }
    if (!create)
      return nullptr;
    if (UNLIKELY(*meta != idx0)) {
      idx0 = *meta;
      continue;
    }

    if (LIKELY(myidx == 0)) {
      myidx = sync_alloc_.Alloc(&thr->proc()->sync_cache);
      mys = sync_alloc_.Map(myidx);
      mys->Init(thr, pc, addr, save_stack);
    }
    mys->next = idx0;
    if (atomic_compare_exchange_strong((atomic_uint32_t*)meta, &idx0,
        myidx | kFlagSync, memory_order_release)) {
      return mys;
    }
  }
}

void MetaMap::MoveMemory(uptr src, uptr dst, uptr sz) {
  // src and dst can overlap,
  // there are no concurrent accesses to the regions (e.g. stop-the-world).
  CHECK_NE(src, dst);
  CHECK_NE(sz, 0);
  uptr diff = dst - src;
  u32 *src_meta = MemToMeta(src);
  u32 *dst_meta = MemToMeta(dst);
  u32 *src_meta_end = MemToMeta(src + sz);
  uptr inc = 1;
  if (dst > src) {
    src_meta = MemToMeta(src + sz) - 1;
    dst_meta = MemToMeta(dst + sz) - 1;
    src_meta_end = MemToMeta(src) - 1;
    inc = -1;
  }
  for (; src_meta != src_meta_end; src_meta += inc, dst_meta += inc) {
    CHECK_EQ(*dst_meta, 0);
    u32 idx = *src_meta;
    *src_meta = 0;
    *dst_meta = idx;
    // Patch the addresses in sync objects.
    while (idx != 0) {
      if (idx & kFlagBlock)
        break;
      CHECK(idx & kFlagSync);
      SyncVar *s = sync_alloc_.Map(idx & ~kFlagMask);
      s->addr += diff;
      idx = s->next;
    }
  }
}

void MetaMap::OnProcIdle(Processor *proc) {
  block_alloc_.FlushCache(&proc->block_cache);
  sync_alloc_.FlushCache(&proc->sync_cache);
}

MetaMap::MemoryStats MetaMap::GetMemoryStats() const {
  MemoryStats stats;
  stats.mem_block = block_alloc_.AllocatedMemory();
  stats.sync_obj = sync_alloc_.AllocatedMemory();
  return stats;
}

}  // namespace __tsan
