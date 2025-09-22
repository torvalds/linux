//===-- sanitizer_stack_store.cpp -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "sanitizer_stack_store.h"

#include "sanitizer_atomic.h"
#include "sanitizer_common.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_leb128.h"
#include "sanitizer_lzw.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_stacktrace.h"

namespace __sanitizer {

namespace {
struct StackTraceHeader {
  static constexpr u32 kStackSizeBits = 8;

  u8 size;
  u8 tag;
  explicit StackTraceHeader(const StackTrace &trace)
      : size(Min<uptr>(trace.size, (1u << 8) - 1)), tag(trace.tag) {
    CHECK_EQ(trace.tag, static_cast<uptr>(tag));
  }
  explicit StackTraceHeader(uptr h)
      : size(h & ((1 << kStackSizeBits) - 1)), tag(h >> kStackSizeBits) {}

  uptr ToUptr() const {
    return static_cast<uptr>(size) | (static_cast<uptr>(tag) << kStackSizeBits);
  }
};
}  // namespace

StackStore::Id StackStore::Store(const StackTrace &trace, uptr *pack) {
  if (!trace.size && !trace.tag)
    return 0;
  StackTraceHeader h(trace);
  uptr idx = 0;
  *pack = 0;
  uptr *stack_trace = Alloc(h.size + 1, &idx, pack);
  // No more space.
  if (stack_trace == nullptr)
    return 0;
  *stack_trace = h.ToUptr();
  internal_memcpy(stack_trace + 1, trace.trace, h.size * sizeof(uptr));
  *pack += blocks_[GetBlockIdx(idx)].Stored(h.size + 1);
  return OffsetToId(idx);
}

StackTrace StackStore::Load(Id id) {
  if (!id)
    return {};
  uptr idx = IdToOffset(id);
  uptr block_idx = GetBlockIdx(idx);
  CHECK_LT(block_idx, ARRAY_SIZE(blocks_));
  const uptr *stack_trace = blocks_[block_idx].GetOrUnpack(this);
  if (!stack_trace)
    return {};
  stack_trace += GetInBlockIdx(idx);
  StackTraceHeader h(*stack_trace);
  return StackTrace(stack_trace + 1, h.size, h.tag);
}

uptr StackStore::Allocated() const {
  return atomic_load_relaxed(&allocated_) + sizeof(*this);
}

uptr *StackStore::Alloc(uptr count, uptr *idx, uptr *pack) {
  for (;;) {
    // Optimisic lock-free allocation, essentially try to bump the
    // total_frames_.
    uptr start = atomic_fetch_add(&total_frames_, count, memory_order_relaxed);
    uptr block_idx = GetBlockIdx(start);
    uptr last_idx = GetBlockIdx(start + count - 1);
    if (LIKELY(block_idx == last_idx)) {
      // Fits into a single block.
      // No more available blocks.  Indicate inability to allocate more memory.
      if (block_idx >= ARRAY_SIZE(blocks_))
        return nullptr;
      *idx = start;
      return blocks_[block_idx].GetOrCreate(this) + GetInBlockIdx(start);
    }

    // Retry. We can't use range allocated in two different blocks.
    CHECK_LE(count, kBlockSizeFrames);
    uptr in_first = kBlockSizeFrames - GetInBlockIdx(start);
    // Mark tail/head of these blocks as "stored".to avoid waiting before we can
    // Pack().
    *pack += blocks_[block_idx].Stored(in_first);
    *pack += blocks_[last_idx].Stored(count - in_first);
  }
}

void *StackStore::Map(uptr size, const char *mem_type) {
  atomic_fetch_add(&allocated_, size, memory_order_relaxed);
  return MmapNoReserveOrDie(size, mem_type);
}

void StackStore::Unmap(void *addr, uptr size) {
  atomic_fetch_sub(&allocated_, size, memory_order_relaxed);
  UnmapOrDie(addr, size);
}

uptr StackStore::Pack(Compression type) {
  uptr res = 0;
  for (BlockInfo &b : blocks_) res += b.Pack(type, this);
  return res;
}

void StackStore::LockAll() {
  for (BlockInfo &b : blocks_) b.Lock();
}

void StackStore::UnlockAll() {
  for (BlockInfo &b : blocks_) b.Unlock();
}

void StackStore::TestOnlyUnmap() {
  for (BlockInfo &b : blocks_) b.TestOnlyUnmap(this);
  internal_memset(this, 0, sizeof(*this));
}

uptr *StackStore::BlockInfo::Get() const {
  // Idiomatic double-checked locking uses memory_order_acquire here. But
  // relaxed is fine for us, justification is similar to
  // TwoLevelMap::GetOrCreate.
  return reinterpret_cast<uptr *>(atomic_load_relaxed(&data_));
}

uptr *StackStore::BlockInfo::Create(StackStore *store) {
  SpinMutexLock l(&mtx_);
  uptr *ptr = Get();
  if (!ptr) {
    ptr = reinterpret_cast<uptr *>(store->Map(kBlockSizeBytes, "StackStore"));
    atomic_store(&data_, reinterpret_cast<uptr>(ptr), memory_order_release);
  }
  return ptr;
}

uptr *StackStore::BlockInfo::GetOrCreate(StackStore *store) {
  uptr *ptr = Get();
  if (LIKELY(ptr))
    return ptr;
  return Create(store);
}

class SLeb128Encoder {
 public:
  SLeb128Encoder(u8 *begin, u8 *end) : begin(begin), end(end) {}

  bool operator==(const SLeb128Encoder &other) const {
    return begin == other.begin;
  }

  bool operator!=(const SLeb128Encoder &other) const {
    return begin != other.begin;
  }

  SLeb128Encoder &operator=(uptr v) {
    sptr diff = v - previous;
    begin = EncodeSLEB128(diff, begin, end);
    previous = v;
    return *this;
  }
  SLeb128Encoder &operator*() { return *this; }
  SLeb128Encoder &operator++() { return *this; }

  u8 *base() const { return begin; }

 private:
  u8 *begin;
  u8 *end;
  uptr previous = 0;
};

class SLeb128Decoder {
 public:
  SLeb128Decoder(const u8 *begin, const u8 *end) : begin(begin), end(end) {}

  bool operator==(const SLeb128Decoder &other) const {
    return begin == other.begin;
  }

  bool operator!=(const SLeb128Decoder &other) const {
    return begin != other.begin;
  }

  uptr operator*() {
    sptr diff;
    begin = DecodeSLEB128(begin, end, &diff);
    previous += diff;
    return previous;
  }
  SLeb128Decoder &operator++() { return *this; }

  SLeb128Decoder operator++(int) { return *this; }

 private:
  const u8 *begin;
  const u8 *end;
  uptr previous = 0;
};

static u8 *CompressDelta(const uptr *from, const uptr *from_end, u8 *to,
                         u8 *to_end) {
  SLeb128Encoder encoder(to, to_end);
  for (; from != from_end; ++from, ++encoder) *encoder = *from;
  return encoder.base();
}

static uptr *UncompressDelta(const u8 *from, const u8 *from_end, uptr *to,
                             uptr *to_end) {
  SLeb128Decoder decoder(from, from_end);
  SLeb128Decoder end(from_end, from_end);
  for (; decoder != end; ++to, ++decoder) *to = *decoder;
  CHECK_EQ(to, to_end);
  return to;
}

static u8 *CompressLzw(const uptr *from, const uptr *from_end, u8 *to,
                       u8 *to_end) {
  SLeb128Encoder encoder(to, to_end);
  encoder = LzwEncode<uptr>(from, from_end, encoder);
  return encoder.base();
}

static uptr *UncompressLzw(const u8 *from, const u8 *from_end, uptr *to,
                           uptr *to_end) {
  SLeb128Decoder decoder(from, from_end);
  SLeb128Decoder end(from_end, from_end);
  to = LzwDecode<uptr>(decoder, end, to);
  CHECK_EQ(to, to_end);
  return to;
}

#if defined(_MSC_VER) && !defined(__clang__)
#  pragma warning(push)
// Disable 'nonstandard extension used: zero-sized array in struct/union'.
#  pragma warning(disable : 4200)
#endif
namespace {
struct PackedHeader {
  uptr size;
  StackStore::Compression type;
  u8 data[];
};
}  // namespace
#if defined(_MSC_VER) && !defined(__clang__)
#  pragma warning(pop)
#endif

uptr *StackStore::BlockInfo::GetOrUnpack(StackStore *store) {
  SpinMutexLock l(&mtx_);
  switch (state) {
    case State::Storing:
      state = State::Unpacked;
      FALLTHROUGH;
    case State::Unpacked:
      return Get();
    case State::Packed:
      break;
  }

  u8 *ptr = reinterpret_cast<u8 *>(Get());
  CHECK_NE(nullptr, ptr);
  const PackedHeader *header = reinterpret_cast<const PackedHeader *>(ptr);
  CHECK_LE(header->size, kBlockSizeBytes);
  CHECK_GE(header->size, sizeof(PackedHeader));

  uptr packed_size_aligned = RoundUpTo(header->size, GetPageSizeCached());

  uptr *unpacked =
      reinterpret_cast<uptr *>(store->Map(kBlockSizeBytes, "StackStoreUnpack"));

  uptr *unpacked_end;
  switch (header->type) {
    case Compression::Delta:
      unpacked_end = UncompressDelta(header->data, ptr + header->size, unpacked,
                                     unpacked + kBlockSizeFrames);
      break;
    case Compression::LZW:
      unpacked_end = UncompressLzw(header->data, ptr + header->size, unpacked,
                                   unpacked + kBlockSizeFrames);
      break;
    default:
      UNREACHABLE("Unexpected type");
      break;
  }

  CHECK_EQ(kBlockSizeFrames, unpacked_end - unpacked);

  MprotectReadOnly(reinterpret_cast<uptr>(unpacked), kBlockSizeBytes);
  atomic_store(&data_, reinterpret_cast<uptr>(unpacked), memory_order_release);
  store->Unmap(ptr, packed_size_aligned);

  state = State::Unpacked;
  return Get();
}

uptr StackStore::BlockInfo::Pack(Compression type, StackStore *store) {
  if (type == Compression::None)
    return 0;

  SpinMutexLock l(&mtx_);
  switch (state) {
    case State::Unpacked:
    case State::Packed:
      return 0;
    case State::Storing:
      break;
  }

  uptr *ptr = Get();
  if (!ptr || !Stored(0))
    return 0;

  u8 *packed =
      reinterpret_cast<u8 *>(store->Map(kBlockSizeBytes, "StackStorePack"));
  PackedHeader *header = reinterpret_cast<PackedHeader *>(packed);
  u8 *alloc_end = packed + kBlockSizeBytes;

  u8 *packed_end = nullptr;
  switch (type) {
    case Compression::Delta:
      packed_end =
          CompressDelta(ptr, ptr + kBlockSizeFrames, header->data, alloc_end);
      break;
    case Compression::LZW:
      packed_end =
          CompressLzw(ptr, ptr + kBlockSizeFrames, header->data, alloc_end);
      break;
    default:
      UNREACHABLE("Unexpected type");
      break;
  }

  header->type = type;
  header->size = packed_end - packed;

  VPrintf(1, "Packed block of %zu KiB to %zu KiB\n", kBlockSizeBytes >> 10,
          header->size >> 10);

  if (kBlockSizeBytes - header->size < kBlockSizeBytes / 8) {
    VPrintf(1, "Undo and keep block unpacked\n");
    MprotectReadOnly(reinterpret_cast<uptr>(ptr), kBlockSizeBytes);
    store->Unmap(packed, kBlockSizeBytes);
    state = State::Unpacked;
    return 0;
  }

  uptr packed_size_aligned = RoundUpTo(header->size, GetPageSizeCached());
  store->Unmap(packed + packed_size_aligned,
               kBlockSizeBytes - packed_size_aligned);
  MprotectReadOnly(reinterpret_cast<uptr>(packed), packed_size_aligned);

  atomic_store(&data_, reinterpret_cast<uptr>(packed), memory_order_release);
  store->Unmap(ptr, kBlockSizeBytes);

  state = State::Packed;
  return kBlockSizeBytes - packed_size_aligned;
}

void StackStore::BlockInfo::TestOnlyUnmap(StackStore *store) {
  if (uptr *ptr = Get())
    store->Unmap(ptr, kBlockSizeBytes);
}

bool StackStore::BlockInfo::Stored(uptr n) {
  return n + atomic_fetch_add(&stored_, n, memory_order_release) ==
         kBlockSizeFrames;
}

bool StackStore::BlockInfo::IsPacked() const {
  SpinMutexLock l(&mtx_);
  return state == State::Packed;
}

}  // namespace __sanitizer
