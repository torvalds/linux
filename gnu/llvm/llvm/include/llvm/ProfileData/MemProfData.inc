#ifndef MEMPROF_DATA_INC
#define MEMPROF_DATA_INC
/*===-- MemProfData.inc - MemProf profiling runtime structures -*- C++ -*-=== *\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/
/*
 * This is the main file that defines all the data structure, signature,
 * constant literals that are shared across profiling runtime library,
 * and host tools (reader/writer).
 *
 * This file has two identical copies. The primary copy lives in LLVM and
 * the other one sits in compiler-rt/include/profile directory. To make changes
 * in this file, first modify the primary copy and copy it over to compiler-rt.
 * Testing of any change in this file can start only after the two copies are
 * synced up.
 *
\*===----------------------------------------------------------------------===*/
#include <string.h>

#ifdef _MSC_VER
#define PACKED(...) __pragma(pack(push,1)) __VA_ARGS__ __pragma(pack(pop))
#else
#define PACKED(...) __VA_ARGS__ __attribute__((__packed__))
#endif

// A 64-bit magic number to uniquely identify the raw binary memprof profile file.
#define MEMPROF_RAW_MAGIC_64                                                                        \
  ((uint64_t)255 << 56 | (uint64_t)'m' << 48 | (uint64_t)'p' << 40 | (uint64_t)'r' << 32 |          \
   (uint64_t)'o' << 24 | (uint64_t)'f' << 16 | (uint64_t)'r' << 8 | (uint64_t)129)

// The version number of the raw binary format.
#define MEMPROF_RAW_VERSION 4ULL

// Currently supported versions.
#define MEMPROF_RAW_SUPPORTED_VERSIONS                                         \
  { 3ULL, 4ULL }

#define MEMPROF_V3_MIB_SIZE 132ULL;

#define MEMPROF_BUILDID_MAX_SIZE 32ULL

namespace llvm {
namespace memprof {
// A struct describing the header used for the raw binary memprof profile format.
PACKED(struct Header {
  uint64_t Magic;
  uint64_t Version;
  uint64_t TotalSize;
  uint64_t SegmentOffset;
  uint64_t MIBOffset;
  uint64_t StackOffset;
});

// A struct describing the information necessary to describe a /proc/maps
// segment entry for a particular binary/library identified by its build id.
PACKED(struct SegmentEntry {
  uint64_t Start;
  uint64_t End;
  uint64_t Offset;
  uint64_t BuildIdSize;
  uint8_t BuildId[MEMPROF_BUILDID_MAX_SIZE] = {0};

  // This constructor is only used in tests so don't set the BuildId.
  SegmentEntry(uint64_t S, uint64_t E, uint64_t O)
      : Start(S), End(E), Offset(O), BuildIdSize(0) {}

  SegmentEntry(const SegmentEntry& S) {
    Start = S.Start;
    End = S.End;
    Offset = S.Offset;
    BuildIdSize = S.BuildIdSize;
    memcpy(BuildId, S.BuildId, S.BuildIdSize);
  }

  SegmentEntry& operator=(const SegmentEntry& S) {
    Start = S.Start;
    End = S.End;
    Offset = S.Offset;
    BuildIdSize = S.BuildIdSize;
    memcpy(BuildId, S.BuildId, S.BuildIdSize);
    return *this;
  }

  bool operator==(const SegmentEntry& S) const {
    return Start == S.Start && End == S.End && Offset == S.Offset &&
           BuildIdSize == S.BuildIdSize &&
           memcmp(BuildId, S.BuildId, S.BuildIdSize) == 0;
  }
});

// Packed struct definition for MSVC. We can't use the PACKED macro defined in
// MemProfData.inc since it would mean we are embedding a directive (the
// #include for MIBEntryDef) into the macros which is undefined behaviour.
#ifdef _MSC_VER
__pragma(pack(push,1))
#endif

// A struct representing the heap allocation characteristics of a particular
// runtime context. This struct is shared between the compiler-rt runtime and
// the raw profile reader. The indexed format uses a separate, self-describing
// backwards compatible format.
struct MemInfoBlock{

#define MIBEntryDef(NameTag, Name, Type) Type Name;
#include "MIBEntryDef.inc"
#undef MIBEntryDef

bool operator==(const MemInfoBlock& Other) const {
  bool IsEqual = true;
#define MIBEntryDef(NameTag, Name, Type) \
  IsEqual = (IsEqual && Name == Other.Name);
#include "MIBEntryDef.inc"
#undef MIBEntryDef
  return IsEqual;
}

MemInfoBlock() {
#define MIBEntryDef(NameTag, Name, Type) Name = Type();
#include "MIBEntryDef.inc"
#undef MIBEntryDef
}

MemInfoBlock(uint32_t Size, uint64_t AccessCount, uint32_t AllocTs,
             uint32_t DeallocTs, uint32_t AllocCpu, uint32_t DeallocCpu,
             uintptr_t Histogram, uint32_t HistogramSize)
    : MemInfoBlock() {
  AllocCount = 1U;
  TotalAccessCount = AccessCount;
  MinAccessCount = AccessCount;
  MaxAccessCount = AccessCount;
  TotalSize = Size;
  MinSize = Size;
  MaxSize = Size;
  AllocTimestamp = AllocTs;
  DeallocTimestamp = DeallocTs;
  TotalLifetime = DeallocTimestamp - AllocTimestamp;
  MinLifetime = TotalLifetime;
  MaxLifetime = TotalLifetime;
  // Access density is accesses per byte. Multiply by 100 to include the
  // fractional part.
  TotalAccessDensity = AccessCount * 100 / Size;
  MinAccessDensity = TotalAccessDensity;
  MaxAccessDensity = TotalAccessDensity;
  // Lifetime access density is the access density per second of lifetime.
  // Multiply by 1000 to convert denominator lifetime to seconds (using a
  // minimum lifetime of 1ms to avoid divide by 0. Do the multiplication first
  // to reduce truncations to 0.
  TotalLifetimeAccessDensity =
      TotalAccessDensity * 1000 / (TotalLifetime ? TotalLifetime : 1);
  MinLifetimeAccessDensity = TotalLifetimeAccessDensity;
  MaxLifetimeAccessDensity = TotalLifetimeAccessDensity;
  AllocCpuId = AllocCpu;
  DeallocCpuId = DeallocCpu;
  NumMigratedCpu = AllocCpuId != DeallocCpuId;
  AccessHistogramSize = HistogramSize;
  AccessHistogram = Histogram;
}

void Merge(const MemInfoBlock &newMIB) {
  AllocCount += newMIB.AllocCount;

  TotalAccessCount += newMIB.TotalAccessCount;
  MinAccessCount = newMIB.MinAccessCount < MinAccessCount ? newMIB.MinAccessCount : MinAccessCount;
  MaxAccessCount = newMIB.MaxAccessCount > MaxAccessCount ? newMIB.MaxAccessCount : MaxAccessCount;

  TotalSize += newMIB.TotalSize;
  MinSize = newMIB.MinSize < MinSize ? newMIB.MinSize : MinSize;
  MaxSize = newMIB.MaxSize > MaxSize ? newMIB.MaxSize : MaxSize;

  TotalLifetime += newMIB.TotalLifetime;
  MinLifetime = newMIB.MinLifetime < MinLifetime ? newMIB.MinLifetime : MinLifetime;
  MaxLifetime = newMIB.MaxLifetime > MaxLifetime ? newMIB.MaxLifetime : MaxLifetime;

  TotalAccessDensity += newMIB.TotalAccessDensity;
  MinAccessDensity = newMIB.MinAccessDensity < MinAccessDensity
                         ? newMIB.MinAccessDensity
                         : MinAccessDensity;
  MaxAccessDensity = newMIB.MaxAccessDensity > MaxAccessDensity
                         ? newMIB.MaxAccessDensity
                         : MaxAccessDensity;

  TotalLifetimeAccessDensity += newMIB.TotalLifetimeAccessDensity;
  MinLifetimeAccessDensity =
      newMIB.MinLifetimeAccessDensity < MinLifetimeAccessDensity
          ? newMIB.MinLifetimeAccessDensity
          : MinLifetimeAccessDensity;
  MaxLifetimeAccessDensity =
      newMIB.MaxLifetimeAccessDensity > MaxLifetimeAccessDensity
          ? newMIB.MaxLifetimeAccessDensity
          : MaxLifetimeAccessDensity;

  // We know newMIB was deallocated later, so just need to check if it was
  // allocated before last one deallocated.
  NumLifetimeOverlaps += newMIB.AllocTimestamp < DeallocTimestamp;
  AllocTimestamp = newMIB.AllocTimestamp;
  DeallocTimestamp = newMIB.DeallocTimestamp;

  NumSameAllocCpu += AllocCpuId == newMIB.AllocCpuId;
  NumSameDeallocCpu += DeallocCpuId == newMIB.DeallocCpuId;
  AllocCpuId = newMIB.AllocCpuId;
  DeallocCpuId = newMIB.DeallocCpuId;

  // For merging histograms, we always keep the longer histogram, and add
  // values of shorter histogram to larger one.
  uintptr_t ShorterHistogram;
  uint32_t ShorterHistogramSize;
  if (newMIB.AccessHistogramSize > AccessHistogramSize) {
    ShorterHistogram = AccessHistogram;
    ShorterHistogramSize = AccessHistogramSize;
    // Swap histogram of current to larger histogram
    AccessHistogram = newMIB.AccessHistogram;
    AccessHistogramSize = newMIB.AccessHistogramSize;
  } else {
    ShorterHistogram = newMIB.AccessHistogram;
    ShorterHistogramSize = newMIB.AccessHistogramSize;
  }
  for (size_t i = 0; i < ShorterHistogramSize; ++i) {
    ((uint64_t *)AccessHistogram)[i] += ((uint64_t *)ShorterHistogram)[i];
  }
}

#ifdef _MSC_VER
} __pragma(pack(pop));
#else
} __attribute__((__packed__));
#endif

} // namespace memprof
} // namespace llvm

#endif
