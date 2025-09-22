//===-- tsan_trace.h --------------------------------------------*- C++ -*-===//
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
#ifndef TSAN_TRACE_H
#define TSAN_TRACE_H

#include "tsan_defs.h"
#include "tsan_ilist.h"
#include "tsan_mutexset.h"
#include "tsan_stack_trace.h"

namespace __tsan {

enum class EventType : u64 {
  kAccessExt,
  kAccessRange,
  kLock,
  kRLock,
  kUnlock,
  kTime,
};

// "Base" type for all events for type dispatch.
struct Event {
  // We use variable-length type encoding to give more bits to some event
  // types that need them. If is_access is set, this is EventAccess.
  // Otherwise, if is_func is set, this is EventFunc.
  // Otherwise type denotes the type.
  u64 is_access : 1;
  u64 is_func : 1;
  EventType type : 3;
  u64 _ : 59;
};
static_assert(sizeof(Event) == 8, "bad Event size");

// Nop event used as padding and does not affect state during replay.
static constexpr Event NopEvent = {1, 0, EventType::kAccessExt, 0};

// Compressed memory access can represent only some events with PCs
// close enough to each other. Otherwise we fall back to EventAccessExt.
struct EventAccess {
  static constexpr uptr kPCBits = 15;
  static_assert(kPCBits + kCompressedAddrBits + 5 == 64,
                "unused bits in EventAccess");

  u64 is_access : 1;  // = 1
  u64 is_read : 1;
  u64 is_atomic : 1;
  u64 size_log : 2;
  u64 pc_delta : kPCBits;  // signed delta from the previous memory access PC
  u64 addr : kCompressedAddrBits;
};
static_assert(sizeof(EventAccess) == 8, "bad EventAccess size");

// Function entry (pc != 0) or exit (pc == 0).
struct EventFunc {
  u64 is_access : 1;  // = 0
  u64 is_func : 1;    // = 1
  u64 pc : 62;
};
static_assert(sizeof(EventFunc) == 8, "bad EventFunc size");

// Extended memory access with full PC.
struct EventAccessExt {
  // Note: precisely specifying the unused parts of the bitfield is critical for
  // performance. If we don't specify them, compiler will generate code to load
  // the old value and shuffle it to extract the unused bits to apply to the new
  // value. If we specify the unused part and store 0 in there, all that
  // unnecessary code goes away (store of the 0 const is combined with other
  // constant parts).
  static constexpr uptr kUnusedBits = 11;
  static_assert(kCompressedAddrBits + kUnusedBits + 9 == 64,
                "unused bits in EventAccessExt");

  u64 is_access : 1;   // = 0
  u64 is_func : 1;     // = 0
  EventType type : 3;  // = EventType::kAccessExt
  u64 is_read : 1;
  u64 is_atomic : 1;
  u64 size_log : 2;
  u64 _ : kUnusedBits;
  u64 addr : kCompressedAddrBits;
  u64 pc;
};
static_assert(sizeof(EventAccessExt) == 16, "bad EventAccessExt size");

// Access to a memory range.
struct EventAccessRange {
  static constexpr uptr kSizeLoBits = 13;
  static_assert(kCompressedAddrBits + kSizeLoBits + 7 == 64,
                "unused bits in EventAccessRange");

  u64 is_access : 1;   // = 0
  u64 is_func : 1;     // = 0
  EventType type : 3;  // = EventType::kAccessRange
  u64 is_read : 1;
  u64 is_free : 1;
  u64 size_lo : kSizeLoBits;
  u64 pc : kCompressedAddrBits;
  u64 addr : kCompressedAddrBits;
  u64 size_hi : 64 - kCompressedAddrBits;
};
static_assert(sizeof(EventAccessRange) == 16, "bad EventAccessRange size");

// Mutex lock.
struct EventLock {
  static constexpr uptr kStackIDLoBits = 15;
  static constexpr uptr kStackIDHiBits =
      sizeof(StackID) * kByteBits - kStackIDLoBits;
  static constexpr uptr kUnusedBits = 3;
  static_assert(kCompressedAddrBits + kStackIDLoBits + 5 == 64,
                "unused bits in EventLock");
  static_assert(kCompressedAddrBits + kStackIDHiBits + kUnusedBits == 64,
                "unused bits in EventLock");

  u64 is_access : 1;   // = 0
  u64 is_func : 1;     // = 0
  EventType type : 3;  // = EventType::kLock or EventType::kRLock
  u64 pc : kCompressedAddrBits;
  u64 stack_lo : kStackIDLoBits;
  u64 stack_hi : sizeof(StackID) * kByteBits - kStackIDLoBits;
  u64 _ : kUnusedBits;
  u64 addr : kCompressedAddrBits;
};
static_assert(sizeof(EventLock) == 16, "bad EventLock size");

// Mutex unlock.
struct EventUnlock {
  static constexpr uptr kUnusedBits = 15;
  static_assert(kCompressedAddrBits + kUnusedBits + 5 == 64,
                "unused bits in EventUnlock");

  u64 is_access : 1;   // = 0
  u64 is_func : 1;     // = 0
  EventType type : 3;  // = EventType::kUnlock
  u64 _ : kUnusedBits;
  u64 addr : kCompressedAddrBits;
};
static_assert(sizeof(EventUnlock) == 8, "bad EventUnlock size");

// Time change event.
struct EventTime {
  static constexpr uptr kUnusedBits = 37;
  static_assert(kUnusedBits + sizeof(Sid) * kByteBits + kEpochBits + 5 == 64,
                "unused bits in EventTime");

  u64 is_access : 1;   // = 0
  u64 is_func : 1;     // = 0
  EventType type : 3;  // = EventType::kTime
  u64 sid : sizeof(Sid) * kByteBits;
  u64 epoch : kEpochBits;
  u64 _ : kUnusedBits;
};
static_assert(sizeof(EventTime) == 8, "bad EventTime size");

struct Trace;

struct TraceHeader {
  Trace* trace = nullptr;  // back-pointer to Trace containing this part
  INode trace_parts;       // in Trace::parts
  INode global;            // in Contex::trace_part_recycle
};

struct TracePart : TraceHeader {
  // There are a lot of goroutines in Go, so we use smaller parts.
  static constexpr uptr kByteSize = (SANITIZER_GO ? 128 : 256) << 10;
  static constexpr uptr kSize =
      (kByteSize - sizeof(TraceHeader)) / sizeof(Event);
  // TraceAcquire does a fast event pointer overflow check by comparing
  // pointer into TracePart::events with kAlignment mask. Since TracePart's
  // are allocated page-aligned, this check detects end of the array
  // (it also have false positives in the middle that are filtered separately).
  // This also requires events to be the last field.
  static constexpr uptr kAlignment = 0xff0;
  Event events[kSize];

  TracePart() {}
};
static_assert(sizeof(TracePart) == TracePart::kByteSize, "bad TracePart size");

struct Trace {
  Mutex mtx;
  IList<TraceHeader, &TraceHeader::trace_parts, TracePart> parts;
  // First node non-queued into ctx->trace_part_recycle.
  TracePart* local_head;
  // Final position in the last part for finished threads.
  Event* final_pos = nullptr;
  // Number of trace parts allocated on behalf of this trace specifically.
  // Total number of parts in this trace can be larger if we retake some
  // parts from other traces.
  uptr parts_allocated = 0;

  Trace() : mtx(MutexTypeTrace) {}

  // We need at least 3 parts per thread, because we want to keep at last
  // 2 parts per thread that are not queued into ctx->trace_part_recycle
  // (the current one being filled and one full part that ensures that
  // we always have at least one part worth of previous memory accesses).
  static constexpr uptr kMinParts = 3;

  static constexpr uptr kFinishedThreadLo = 16;
  static constexpr uptr kFinishedThreadHi = 64;
};

}  // namespace __tsan

#endif  // TSAN_TRACE_H
