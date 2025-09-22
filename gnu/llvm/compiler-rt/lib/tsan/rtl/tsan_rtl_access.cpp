//===-- tsan_rtl_access.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Definitions of memory access and function entry/exit entry points.
//===----------------------------------------------------------------------===//

#include "tsan_rtl.h"

namespace __tsan {

ALWAYS_INLINE USED bool TryTraceMemoryAccess(ThreadState* thr, uptr pc,
                                             uptr addr, uptr size,
                                             AccessType typ) {
  DCHECK(size == 1 || size == 2 || size == 4 || size == 8);
  if (!kCollectHistory)
    return true;
  EventAccess* ev;
  if (UNLIKELY(!TraceAcquire(thr, &ev)))
    return false;
  u64 size_log = size == 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : 3;
  uptr pc_delta = pc - thr->trace_prev_pc + (1 << (EventAccess::kPCBits - 1));
  thr->trace_prev_pc = pc;
  if (LIKELY(pc_delta < (1 << EventAccess::kPCBits))) {
    ev->is_access = 1;
    ev->is_read = !!(typ & kAccessRead);
    ev->is_atomic = !!(typ & kAccessAtomic);
    ev->size_log = size_log;
    ev->pc_delta = pc_delta;
    DCHECK_EQ(ev->pc_delta, pc_delta);
    ev->addr = CompressAddr(addr);
    TraceRelease(thr, ev);
    return true;
  }
  auto* evex = reinterpret_cast<EventAccessExt*>(ev);
  evex->is_access = 0;
  evex->is_func = 0;
  evex->type = EventType::kAccessExt;
  evex->is_read = !!(typ & kAccessRead);
  evex->is_atomic = !!(typ & kAccessAtomic);
  evex->size_log = size_log;
  // Note: this is important, see comment in EventAccessExt.
  evex->_ = 0;
  evex->addr = CompressAddr(addr);
  evex->pc = pc;
  TraceRelease(thr, evex);
  return true;
}

ALWAYS_INLINE
bool TryTraceMemoryAccessRange(ThreadState* thr, uptr pc, uptr addr, uptr size,
                               AccessType typ) {
  if (!kCollectHistory)
    return true;
  EventAccessRange* ev;
  if (UNLIKELY(!TraceAcquire(thr, &ev)))
    return false;
  thr->trace_prev_pc = pc;
  ev->is_access = 0;
  ev->is_func = 0;
  ev->type = EventType::kAccessRange;
  ev->is_read = !!(typ & kAccessRead);
  ev->is_free = !!(typ & kAccessFree);
  ev->size_lo = size;
  ev->pc = CompressAddr(pc);
  ev->addr = CompressAddr(addr);
  ev->size_hi = size >> EventAccessRange::kSizeLoBits;
  TraceRelease(thr, ev);
  return true;
}

void TraceMemoryAccessRange(ThreadState* thr, uptr pc, uptr addr, uptr size,
                            AccessType typ) {
  if (LIKELY(TryTraceMemoryAccessRange(thr, pc, addr, size, typ)))
    return;
  TraceSwitchPart(thr);
  UNUSED bool res = TryTraceMemoryAccessRange(thr, pc, addr, size, typ);
  DCHECK(res);
}

void TraceFunc(ThreadState* thr, uptr pc) {
  if (LIKELY(TryTraceFunc(thr, pc)))
    return;
  TraceSwitchPart(thr);
  UNUSED bool res = TryTraceFunc(thr, pc);
  DCHECK(res);
}

NOINLINE void TraceRestartFuncEntry(ThreadState* thr, uptr pc) {
  TraceSwitchPart(thr);
  FuncEntry(thr, pc);
}

NOINLINE void TraceRestartFuncExit(ThreadState* thr) {
  TraceSwitchPart(thr);
  FuncExit(thr);
}

void TraceMutexLock(ThreadState* thr, EventType type, uptr pc, uptr addr,
                    StackID stk) {
  DCHECK(type == EventType::kLock || type == EventType::kRLock);
  if (!kCollectHistory)
    return;
  EventLock ev;
  ev.is_access = 0;
  ev.is_func = 0;
  ev.type = type;
  ev.pc = CompressAddr(pc);
  ev.stack_lo = stk;
  ev.stack_hi = stk >> EventLock::kStackIDLoBits;
  ev._ = 0;
  ev.addr = CompressAddr(addr);
  TraceEvent(thr, ev);
}

void TraceMutexUnlock(ThreadState* thr, uptr addr) {
  if (!kCollectHistory)
    return;
  EventUnlock ev;
  ev.is_access = 0;
  ev.is_func = 0;
  ev.type = EventType::kUnlock;
  ev._ = 0;
  ev.addr = CompressAddr(addr);
  TraceEvent(thr, ev);
}

void TraceTime(ThreadState* thr) {
  if (!kCollectHistory)
    return;
  FastState fast_state = thr->fast_state;
  EventTime ev;
  ev.is_access = 0;
  ev.is_func = 0;
  ev.type = EventType::kTime;
  ev.sid = static_cast<u64>(fast_state.sid());
  ev.epoch = static_cast<u64>(fast_state.epoch());
  ev._ = 0;
  TraceEvent(thr, ev);
}

NOINLINE void DoReportRace(ThreadState* thr, RawShadow* shadow_mem, Shadow cur,
                           Shadow old,
                           AccessType typ) SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  // For the free shadow markers the first element (that contains kFreeSid)
  // triggers the race, but the second element contains info about the freeing
  // thread, take it.
  if (old.sid() == kFreeSid)
    old = Shadow(LoadShadow(&shadow_mem[1]));
  // This prevents trapping on this address in future.
  for (uptr i = 0; i < kShadowCnt; i++)
    StoreShadow(&shadow_mem[i], i == 0 ? Shadow::kRodata : Shadow::kEmpty);
  // See the comment in MemoryRangeFreed as to why the slot is locked
  // for free memory accesses. ReportRace must not be called with
  // the slot locked because of the fork. But MemoryRangeFreed is not
  // called during fork because fork sets ignore_reads_and_writes,
  // so simply unlocking the slot should be fine.
  if (typ & kAccessSlotLocked)
    SlotUnlock(thr);
  ReportRace(thr, shadow_mem, cur, Shadow(old), typ);
  if (typ & kAccessSlotLocked)
    SlotLock(thr);
}

#if !TSAN_VECTORIZE
ALWAYS_INLINE
bool ContainsSameAccess(RawShadow* s, Shadow cur, int unused0, int unused1,
                        AccessType typ) {
  for (uptr i = 0; i < kShadowCnt; i++) {
    auto old = LoadShadow(&s[i]);
    if (!(typ & kAccessRead)) {
      if (old == cur.raw())
        return true;
      continue;
    }
    auto masked = static_cast<RawShadow>(static_cast<u32>(old) |
                                         static_cast<u32>(Shadow::kRodata));
    if (masked == cur.raw())
      return true;
    if (!(typ & kAccessNoRodata) && !SANITIZER_GO) {
      if (old == Shadow::kRodata)
        return true;
    }
  }
  return false;
}

ALWAYS_INLINE
bool CheckRaces(ThreadState* thr, RawShadow* shadow_mem, Shadow cur,
                int unused0, int unused1, AccessType typ) {
  bool stored = false;
  for (uptr idx = 0; idx < kShadowCnt; idx++) {
    RawShadow* sp = &shadow_mem[idx];
    Shadow old(LoadShadow(sp));
    if (LIKELY(old.raw() == Shadow::kEmpty)) {
      if (!(typ & kAccessCheckOnly) && !stored)
        StoreShadow(sp, cur.raw());
      return false;
    }
    if (LIKELY(!(cur.access() & old.access())))
      continue;
    if (LIKELY(cur.sid() == old.sid())) {
      if (!(typ & kAccessCheckOnly) &&
          LIKELY(cur.access() == old.access() && old.IsRWWeakerOrEqual(typ))) {
        StoreShadow(sp, cur.raw());
        stored = true;
      }
      continue;
    }
    if (LIKELY(old.IsBothReadsOrAtomic(typ)))
      continue;
    if (LIKELY(thr->clock.Get(old.sid()) >= old.epoch()))
      continue;
    DoReportRace(thr, shadow_mem, cur, old, typ);
    return true;
  }
  // We did not find any races and had already stored
  // the current access info, so we are done.
  if (LIKELY(stored))
    return false;
  // Choose a random candidate slot and replace it.
  uptr index =
      atomic_load_relaxed(&thr->trace_pos) / sizeof(Event) % kShadowCnt;
  StoreShadow(&shadow_mem[index], cur.raw());
  return false;
}

#  define LOAD_CURRENT_SHADOW(cur, shadow_mem) UNUSED int access = 0, shadow = 0

#else /* !TSAN_VECTORIZE */

ALWAYS_INLINE
bool ContainsSameAccess(RawShadow* unused0, Shadow unused1, m128 shadow,
                        m128 access, AccessType typ) {
  // Note: we could check if there is a larger access of the same type,
  // e.g. we just allocated/memset-ed a block (so it contains 8 byte writes)
  // and now do smaller reads/writes, these can also be considered as "same
  // access". However, it will make the check more expensive, so it's unclear
  // if it's worth it. But this would conserve trace space, so it's useful
  // besides potential speed up.
  if (!(typ & kAccessRead)) {
    const m128 same = _mm_cmpeq_epi32(shadow, access);
    return _mm_movemask_epi8(same);
  }
  // For reads we need to reset read bit in the shadow,
  // because we need to match read with both reads and writes.
  // Shadow::kRodata has only read bit set, so it does what we want.
  // We also abuse it for rodata check to save few cycles
  // since we already loaded Shadow::kRodata into a register.
  // Reads from rodata can't race.
  // Measurements show that they can be 10-20% of all memory accesses.
  // Shadow::kRodata has epoch 0 which cannot appear in shadow normally
  // (thread epochs start from 1). So the same read bit mask
  // serves as rodata indicator.
  const m128 read_mask = _mm_set1_epi32(static_cast<u32>(Shadow::kRodata));
  const m128 masked_shadow = _mm_or_si128(shadow, read_mask);
  m128 same = _mm_cmpeq_epi32(masked_shadow, access);
  // Range memory accesses check Shadow::kRodata before calling this,
  // Shadow::kRodatas is not possible for free memory access
  // and Go does not use Shadow::kRodata.
  if (!(typ & kAccessNoRodata) && !SANITIZER_GO) {
    const m128 ro = _mm_cmpeq_epi32(shadow, read_mask);
    same = _mm_or_si128(ro, same);
  }
  return _mm_movemask_epi8(same);
}

NOINLINE void DoReportRaceV(ThreadState* thr, RawShadow* shadow_mem, Shadow cur,
                            u32 race_mask, m128 shadow, AccessType typ) {
  // race_mask points which of the shadow elements raced with the current
  // access. Extract that element.
  CHECK_NE(race_mask, 0);
  u32 old;
  // Note: _mm_extract_epi32 index must be a constant value.
  switch (__builtin_ffs(race_mask) / 4) {
    case 0:
      old = _mm_extract_epi32(shadow, 0);
      break;
    case 1:
      old = _mm_extract_epi32(shadow, 1);
      break;
    case 2:
      old = _mm_extract_epi32(shadow, 2);
      break;
    case 3:
      old = _mm_extract_epi32(shadow, 3);
      break;
  }
  Shadow prev(static_cast<RawShadow>(old));
  // For the free shadow markers the first element (that contains kFreeSid)
  // triggers the race, but the second element contains info about the freeing
  // thread, take it.
  if (prev.sid() == kFreeSid)
    prev = Shadow(static_cast<RawShadow>(_mm_extract_epi32(shadow, 1)));
  DoReportRace(thr, shadow_mem, cur, prev, typ);
}

ALWAYS_INLINE
bool CheckRaces(ThreadState* thr, RawShadow* shadow_mem, Shadow cur,
                m128 shadow, m128 access, AccessType typ) {
  // Note: empty/zero slots don't intersect with any access.
  const m128 zero = _mm_setzero_si128();
  const m128 mask_access = _mm_set1_epi32(0x000000ff);
  const m128 mask_sid = _mm_set1_epi32(0x0000ff00);
  const m128 mask_read_atomic = _mm_set1_epi32(0xc0000000);
  const m128 access_and = _mm_and_si128(access, shadow);
  const m128 access_xor = _mm_xor_si128(access, shadow);
  const m128 intersect = _mm_and_si128(access_and, mask_access);
  const m128 not_intersect = _mm_cmpeq_epi32(intersect, zero);
  const m128 not_same_sid = _mm_and_si128(access_xor, mask_sid);
  const m128 same_sid = _mm_cmpeq_epi32(not_same_sid, zero);
  const m128 both_read_or_atomic = _mm_and_si128(access_and, mask_read_atomic);
  const m128 no_race =
      _mm_or_si128(_mm_or_si128(not_intersect, same_sid), both_read_or_atomic);
  const int race_mask = _mm_movemask_epi8(_mm_cmpeq_epi32(no_race, zero));
  if (UNLIKELY(race_mask))
    goto SHARED;

STORE : {
  if (typ & kAccessCheckOnly)
    return false;
  // We could also replace different sid's if access is the same,
  // rw weaker and happens before. However, just checking access below
  // is not enough because we also need to check that !both_read_or_atomic
  // (reads from different sids can be concurrent).
  // Theoretically we could replace smaller accesses with larger accesses,
  // but it's unclear if it's worth doing.
  const m128 mask_access_sid = _mm_set1_epi32(0x0000ffff);
  const m128 not_same_sid_access = _mm_and_si128(access_xor, mask_access_sid);
  const m128 same_sid_access = _mm_cmpeq_epi32(not_same_sid_access, zero);
  const m128 access_read_atomic =
      _mm_set1_epi32((typ & (kAccessRead | kAccessAtomic)) << 30);
  const m128 rw_weaker =
      _mm_cmpeq_epi32(_mm_max_epu32(shadow, access_read_atomic), shadow);
  const m128 rewrite = _mm_and_si128(same_sid_access, rw_weaker);
  const int rewrite_mask = _mm_movemask_epi8(rewrite);
  int index = __builtin_ffs(rewrite_mask);
  if (UNLIKELY(index == 0)) {
    const m128 empty = _mm_cmpeq_epi32(shadow, zero);
    const int empty_mask = _mm_movemask_epi8(empty);
    index = __builtin_ffs(empty_mask);
    if (UNLIKELY(index == 0))
      index = (atomic_load_relaxed(&thr->trace_pos) / 2) % 16;
  }
  StoreShadow(&shadow_mem[index / 4], cur.raw());
  // We could zero other slots determined by rewrite_mask.
  // That would help other threads to evict better slots,
  // but it's unclear if it's worth it.
  return false;
}

SHARED:
  m128 thread_epochs = _mm_set1_epi32(0x7fffffff);
  // Need to unwind this because _mm_extract_epi8/_mm_insert_epi32
  // indexes must be constants.
#  define LOAD_EPOCH(idx)                                                     \
    if (LIKELY(race_mask & (1 << (idx * 4)))) {                               \
      u8 sid = _mm_extract_epi8(shadow, idx * 4 + 1);                         \
      u16 epoch = static_cast<u16>(thr->clock.Get(static_cast<Sid>(sid)));    \
      thread_epochs = _mm_insert_epi32(thread_epochs, u32(epoch) << 16, idx); \
    }
  LOAD_EPOCH(0);
  LOAD_EPOCH(1);
  LOAD_EPOCH(2);
  LOAD_EPOCH(3);
#  undef LOAD_EPOCH
  const m128 mask_epoch = _mm_set1_epi32(0x3fff0000);
  const m128 shadow_epochs = _mm_and_si128(shadow, mask_epoch);
  const m128 concurrent = _mm_cmplt_epi32(thread_epochs, shadow_epochs);
  const int concurrent_mask = _mm_movemask_epi8(concurrent);
  if (LIKELY(concurrent_mask == 0))
    goto STORE;

  DoReportRaceV(thr, shadow_mem, cur, concurrent_mask, shadow, typ);
  return true;
}

#  define LOAD_CURRENT_SHADOW(cur, shadow_mem)                         \
    const m128 access = _mm_set1_epi32(static_cast<u32>((cur).raw())); \
    const m128 shadow = _mm_load_si128(reinterpret_cast<m128*>(shadow_mem))
#endif

char* DumpShadow(char* buf, RawShadow raw) {
  if (raw == Shadow::kEmpty) {
    internal_snprintf(buf, 64, "0");
    return buf;
  }
  Shadow s(raw);
  AccessType typ;
  s.GetAccess(nullptr, nullptr, &typ);
  internal_snprintf(buf, 64, "{tid=%u@%u access=0x%x typ=%x}",
                    static_cast<u32>(s.sid()), static_cast<u32>(s.epoch()),
                    s.access(), static_cast<u32>(typ));
  return buf;
}

// TryTrace* and TraceRestart* functions allow to turn memory access and func
// entry/exit callbacks into leaf functions with all associated performance
// benefits. These hottest callbacks do only 2 slow path calls: report a race
// and trace part switching. Race reporting is easy to turn into a tail call, we
// just always return from the runtime after reporting a race. But trace part
// switching is harder because it needs to be in the middle of callbacks. To
// turn it into a tail call we immidiately return after TraceRestart* functions,
// but TraceRestart* functions themselves recurse into the callback after
// switching trace part. As the result the hottest callbacks contain only tail
// calls, which effectively makes them leaf functions (can use all registers,
// no frame setup, etc).
NOINLINE void TraceRestartMemoryAccess(ThreadState* thr, uptr pc, uptr addr,
                                       uptr size, AccessType typ) {
  TraceSwitchPart(thr);
  MemoryAccess(thr, pc, addr, size, typ);
}

ALWAYS_INLINE USED void MemoryAccess(ThreadState* thr, uptr pc, uptr addr,
                                     uptr size, AccessType typ) {
  RawShadow* shadow_mem = MemToShadow(addr);
  UNUSED char memBuf[4][64];
  DPrintf2("#%d: Access: %d@%d %p/%zd typ=0x%x {%s, %s, %s, %s}\n", thr->tid,
           static_cast<int>(thr->fast_state.sid()),
           static_cast<int>(thr->fast_state.epoch()), (void*)addr, size,
           static_cast<int>(typ), DumpShadow(memBuf[0], shadow_mem[0]),
           DumpShadow(memBuf[1], shadow_mem[1]),
           DumpShadow(memBuf[2], shadow_mem[2]),
           DumpShadow(memBuf[3], shadow_mem[3]));

  FastState fast_state = thr->fast_state;
  Shadow cur(fast_state, addr, size, typ);

  LOAD_CURRENT_SHADOW(cur, shadow_mem);
  if (LIKELY(ContainsSameAccess(shadow_mem, cur, shadow, access, typ)))
    return;
  if (UNLIKELY(fast_state.GetIgnoreBit()))
    return;
  if (!TryTraceMemoryAccess(thr, pc, addr, size, typ))
    return TraceRestartMemoryAccess(thr, pc, addr, size, typ);
  CheckRaces(thr, shadow_mem, cur, shadow, access, typ);
}

void MemoryAccess16(ThreadState* thr, uptr pc, uptr addr, AccessType typ);

NOINLINE
void RestartMemoryAccess16(ThreadState* thr, uptr pc, uptr addr,
                           AccessType typ) {
  TraceSwitchPart(thr);
  MemoryAccess16(thr, pc, addr, typ);
}

ALWAYS_INLINE USED void MemoryAccess16(ThreadState* thr, uptr pc, uptr addr,
                                       AccessType typ) {
  const uptr size = 16;
  FastState fast_state = thr->fast_state;
  if (UNLIKELY(fast_state.GetIgnoreBit()))
    return;
  Shadow cur(fast_state, 0, 8, typ);
  RawShadow* shadow_mem = MemToShadow(addr);
  bool traced = false;
  {
    LOAD_CURRENT_SHADOW(cur, shadow_mem);
    if (LIKELY(ContainsSameAccess(shadow_mem, cur, shadow, access, typ)))
      goto SECOND;
    if (!TryTraceMemoryAccessRange(thr, pc, addr, size, typ))
      return RestartMemoryAccess16(thr, pc, addr, typ);
    traced = true;
    if (UNLIKELY(CheckRaces(thr, shadow_mem, cur, shadow, access, typ)))
      return;
  }
SECOND:
  shadow_mem += kShadowCnt;
  LOAD_CURRENT_SHADOW(cur, shadow_mem);
  if (LIKELY(ContainsSameAccess(shadow_mem, cur, shadow, access, typ)))
    return;
  if (!traced && !TryTraceMemoryAccessRange(thr, pc, addr, size, typ))
    return RestartMemoryAccess16(thr, pc, addr, typ);
  CheckRaces(thr, shadow_mem, cur, shadow, access, typ);
}

NOINLINE
void RestartUnalignedMemoryAccess(ThreadState* thr, uptr pc, uptr addr,
                                  uptr size, AccessType typ) {
  TraceSwitchPart(thr);
  UnalignedMemoryAccess(thr, pc, addr, size, typ);
}

ALWAYS_INLINE USED void UnalignedMemoryAccess(ThreadState* thr, uptr pc,
                                              uptr addr, uptr size,
                                              AccessType typ) {
  DCHECK_LE(size, 8);
  FastState fast_state = thr->fast_state;
  if (UNLIKELY(fast_state.GetIgnoreBit()))
    return;
  RawShadow* shadow_mem = MemToShadow(addr);
  bool traced = false;
  uptr size1 = Min<uptr>(size, RoundUp(addr + 1, kShadowCell) - addr);
  {
    Shadow cur(fast_state, addr, size1, typ);
    LOAD_CURRENT_SHADOW(cur, shadow_mem);
    if (LIKELY(ContainsSameAccess(shadow_mem, cur, shadow, access, typ)))
      goto SECOND;
    if (!TryTraceMemoryAccessRange(thr, pc, addr, size, typ))
      return RestartUnalignedMemoryAccess(thr, pc, addr, size, typ);
    traced = true;
    if (UNLIKELY(CheckRaces(thr, shadow_mem, cur, shadow, access, typ)))
      return;
  }
SECOND:
  uptr size2 = size - size1;
  if (LIKELY(size2 == 0))
    return;
  shadow_mem += kShadowCnt;
  Shadow cur(fast_state, 0, size2, typ);
  LOAD_CURRENT_SHADOW(cur, shadow_mem);
  if (LIKELY(ContainsSameAccess(shadow_mem, cur, shadow, access, typ)))
    return;
  if (!traced && !TryTraceMemoryAccessRange(thr, pc, addr, size, typ))
    return RestartUnalignedMemoryAccess(thr, pc, addr, size, typ);
  CheckRaces(thr, shadow_mem, cur, shadow, access, typ);
}

void ShadowSet(RawShadow* p, RawShadow* end, RawShadow v) {
  DCHECK_LE(p, end);
  DCHECK(IsShadowMem(p));
  DCHECK(IsShadowMem(end));
  UNUSED const uptr kAlign = kShadowCnt * kShadowSize;
  DCHECK_EQ(reinterpret_cast<uptr>(p) % kAlign, 0);
  DCHECK_EQ(reinterpret_cast<uptr>(end) % kAlign, 0);
#if !TSAN_VECTORIZE
  for (; p < end; p += kShadowCnt) {
    p[0] = v;
    for (uptr i = 1; i < kShadowCnt; i++) p[i] = Shadow::kEmpty;
  }
#else
  m128 vv = _mm_setr_epi32(
      static_cast<u32>(v), static_cast<u32>(Shadow::kEmpty),
      static_cast<u32>(Shadow::kEmpty), static_cast<u32>(Shadow::kEmpty));
  m128* vp = reinterpret_cast<m128*>(p);
  m128* vend = reinterpret_cast<m128*>(end);
  for (; vp < vend; vp++) _mm_store_si128(vp, vv);
#endif
}

static void MemoryRangeSet(uptr addr, uptr size, RawShadow val) {
  if (size == 0)
    return;
  DCHECK_EQ(addr % kShadowCell, 0);
  DCHECK_EQ(size % kShadowCell, 0);
  // If a user passes some insane arguments (memset(0)),
  // let it just crash as usual.
  if (!IsAppMem(addr) || !IsAppMem(addr + size - 1))
    return;
  RawShadow* begin = MemToShadow(addr);
  RawShadow* end = begin + size / kShadowCell * kShadowCnt;
  // Don't want to touch lots of shadow memory.
  // If a program maps 10MB stack, there is no need reset the whole range.
  // UnmapOrDie/MmapFixedNoReserve does not work on Windows.
  if (SANITIZER_WINDOWS ||
      size <= common_flags()->clear_shadow_mmap_threshold) {
    ShadowSet(begin, end, val);
    return;
  }
  // The region is big, reset only beginning and end.
  const uptr kPageSize = GetPageSizeCached();
  // Set at least first kPageSize/2 to page boundary.
  RawShadow* mid1 =
      Min(end, reinterpret_cast<RawShadow*>(RoundUp(
                   reinterpret_cast<uptr>(begin) + kPageSize / 2, kPageSize)));
  ShadowSet(begin, mid1, val);
  // Reset middle part.
  RawShadow* mid2 = RoundDown(end, kPageSize);
  if (mid2 > mid1) {
    if (!MmapFixedSuperNoReserve((uptr)mid1, (uptr)mid2 - (uptr)mid1))
      Die();
  }
  // Set the ending.
  ShadowSet(mid2, end, val);
}

void MemoryResetRange(ThreadState* thr, uptr pc, uptr addr, uptr size) {
  uptr addr1 = RoundDown(addr, kShadowCell);
  uptr size1 = RoundUp(size + addr - addr1, kShadowCell);
  MemoryRangeSet(addr1, size1, Shadow::kEmpty);
}

void MemoryRangeFreed(ThreadState* thr, uptr pc, uptr addr, uptr size) {
  // Callers must lock the slot to ensure synchronization with the reset.
  // The problem with "freed" memory is that it's not "monotonic"
  // with respect to bug detection: freed memory is bad to access,
  // but then if the heap block is reallocated later, it's good to access.
  // As the result a garbage "freed" shadow can lead to a false positive
  // if it happens to match a real free in the thread trace,
  // but the heap block was reallocated before the current memory access,
  // so it's still good to access. It's not the case with data races.
  DCHECK(thr->slot_locked);
  DCHECK_EQ(addr % kShadowCell, 0);
  size = RoundUp(size, kShadowCell);
  // Processing more than 1k (2k of shadow) is expensive,
  // can cause excessive memory consumption (user does not necessary touch
  // the whole range) and most likely unnecessary.
  size = Min<uptr>(size, 1024);
  const AccessType typ = kAccessWrite | kAccessFree | kAccessSlotLocked |
                         kAccessCheckOnly | kAccessNoRodata;
  TraceMemoryAccessRange(thr, pc, addr, size, typ);
  RawShadow* shadow_mem = MemToShadow(addr);
  Shadow cur(thr->fast_state, 0, kShadowCell, typ);
#if TSAN_VECTORIZE
  const m128 access = _mm_set1_epi32(static_cast<u32>(cur.raw()));
  const m128 freed = _mm_setr_epi32(
      static_cast<u32>(Shadow::FreedMarker()),
      static_cast<u32>(Shadow::FreedInfo(cur.sid(), cur.epoch())), 0, 0);
  for (; size; size -= kShadowCell, shadow_mem += kShadowCnt) {
    const m128 shadow = _mm_load_si128((m128*)shadow_mem);
    if (UNLIKELY(CheckRaces(thr, shadow_mem, cur, shadow, access, typ)))
      return;
    _mm_store_si128((m128*)shadow_mem, freed);
  }
#else
  for (; size; size -= kShadowCell, shadow_mem += kShadowCnt) {
    if (UNLIKELY(CheckRaces(thr, shadow_mem, cur, 0, 0, typ)))
      return;
    StoreShadow(&shadow_mem[0], Shadow::FreedMarker());
    StoreShadow(&shadow_mem[1], Shadow::FreedInfo(cur.sid(), cur.epoch()));
    StoreShadow(&shadow_mem[2], Shadow::kEmpty);
    StoreShadow(&shadow_mem[3], Shadow::kEmpty);
  }
#endif
}

void MemoryRangeImitateWrite(ThreadState* thr, uptr pc, uptr addr, uptr size) {
  DCHECK_EQ(addr % kShadowCell, 0);
  size = RoundUp(size, kShadowCell);
  TraceMemoryAccessRange(thr, pc, addr, size, kAccessWrite);
  Shadow cur(thr->fast_state, 0, 8, kAccessWrite);
  MemoryRangeSet(addr, size, cur.raw());
}

void MemoryRangeImitateWriteOrResetRange(ThreadState* thr, uptr pc, uptr addr,
                                         uptr size) {
  if (thr->ignore_reads_and_writes == 0)
    MemoryRangeImitateWrite(thr, pc, addr, size);
  else
    MemoryResetRange(thr, pc, addr, size);
}

ALWAYS_INLINE
bool MemoryAccessRangeOne(ThreadState* thr, RawShadow* shadow_mem, Shadow cur,
                          AccessType typ) {
  LOAD_CURRENT_SHADOW(cur, shadow_mem);
  if (LIKELY(ContainsSameAccess(shadow_mem, cur, shadow, access, typ)))
    return false;
  return CheckRaces(thr, shadow_mem, cur, shadow, access, typ);
}

template <bool is_read>
NOINLINE void RestartMemoryAccessRange(ThreadState* thr, uptr pc, uptr addr,
                                       uptr size) {
  TraceSwitchPart(thr);
  MemoryAccessRangeT<is_read>(thr, pc, addr, size);
}

template <bool is_read>
void MemoryAccessRangeT(ThreadState* thr, uptr pc, uptr addr, uptr size) {
  const AccessType typ =
      (is_read ? kAccessRead : kAccessWrite) | kAccessNoRodata;
  RawShadow* shadow_mem = MemToShadow(addr);
  DPrintf2("#%d: MemoryAccessRange: @%p %p size=%d is_read=%d\n", thr->tid,
           (void*)pc, (void*)addr, (int)size, is_read);

#if SANITIZER_DEBUG
  if (!IsAppMem(addr)) {
    Printf("Access to non app mem start: %p\n", (void*)addr);
    DCHECK(IsAppMem(addr));
  }
  if (!IsAppMem(addr + size - 1)) {
    Printf("Access to non app mem end: %p\n", (void*)(addr + size - 1));
    DCHECK(IsAppMem(addr + size - 1));
  }
  if (!IsShadowMem(shadow_mem)) {
    Printf("Bad shadow start addr: %p (%p)\n", shadow_mem, (void*)addr);
    DCHECK(IsShadowMem(shadow_mem));
  }

  RawShadow* shadow_mem_end = reinterpret_cast<RawShadow*>(
      reinterpret_cast<uptr>(shadow_mem) + size * kShadowMultiplier - 1);
  if (!IsShadowMem(shadow_mem_end)) {
    Printf("Bad shadow end addr: %p (%p)\n", shadow_mem_end,
           (void*)(addr + size - 1));
    Printf(
        "Shadow start addr (ok): %p (%p); size: 0x%zx; kShadowMultiplier: "
        "%zx\n",
        shadow_mem, (void*)addr, size, kShadowMultiplier);
    DCHECK(IsShadowMem(shadow_mem_end));
  }
#endif

  // Access to .rodata section, no races here.
  // Measurements show that it can be 10-20% of all memory accesses.
  // Check here once to not check for every access separately.
  // Note: we could (and should) do this only for the is_read case
  // (writes shouldn't go to .rodata). But it happens in Chromium tests:
  // https://bugs.chromium.org/p/chromium/issues/detail?id=1275581#c19
  // Details are unknown since it happens only on CI machines.
  if (*shadow_mem == Shadow::kRodata)
    return;

  FastState fast_state = thr->fast_state;
  if (UNLIKELY(fast_state.GetIgnoreBit()))
    return;

  if (!TryTraceMemoryAccessRange(thr, pc, addr, size, typ))
    return RestartMemoryAccessRange<is_read>(thr, pc, addr, size);

  if (UNLIKELY(addr % kShadowCell)) {
    // Handle unaligned beginning, if any.
    uptr size1 = Min(size, RoundUp(addr, kShadowCell) - addr);
    size -= size1;
    Shadow cur(fast_state, addr, size1, typ);
    if (UNLIKELY(MemoryAccessRangeOne(thr, shadow_mem, cur, typ)))
      return;
    shadow_mem += kShadowCnt;
  }
  // Handle middle part, if any.
  Shadow cur(fast_state, 0, kShadowCell, typ);
  for (; size >= kShadowCell; size -= kShadowCell, shadow_mem += kShadowCnt) {
    if (UNLIKELY(MemoryAccessRangeOne(thr, shadow_mem, cur, typ)))
      return;
  }
  // Handle ending, if any.
  if (UNLIKELY(size)) {
    Shadow cur(fast_state, 0, size, typ);
    if (UNLIKELY(MemoryAccessRangeOne(thr, shadow_mem, cur, typ)))
      return;
  }
}

template void MemoryAccessRangeT<true>(ThreadState* thr, uptr pc, uptr addr,
                                       uptr size);
template void MemoryAccessRangeT<false>(ThreadState* thr, uptr pc, uptr addr,
                                        uptr size);

}  // namespace __tsan

#if !SANITIZER_GO
// Must be included in this file to make sure everything is inlined.
#  include "tsan_interface.inc"
#endif
