//===-- size_class_map.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_SIZE_CLASS_MAP_H_
#define SCUDO_SIZE_CLASS_MAP_H_

#include "chunk.h"
#include "common.h"
#include "string_utils.h"

namespace scudo {

inline uptr scaledLog2(uptr Size, uptr ZeroLog, uptr LogBits) {
  const uptr L = getMostSignificantSetBitIndex(Size);
  const uptr LBits = (Size >> (L - LogBits)) - (1 << LogBits);
  const uptr HBits = (L - ZeroLog) << LogBits;
  return LBits + HBits;
}

template <typename Config> struct SizeClassMapBase {
  static u16 getMaxCachedHint(uptr Size) {
    DCHECK_NE(Size, 0);
    u32 N;
    // Force a 32-bit division if the template parameters allow for it.
    if (Config::MaxBytesCachedLog > 31 || Config::MaxSizeLog > 31)
      N = static_cast<u32>((1UL << Config::MaxBytesCachedLog) / Size);
    else
      N = (1U << Config::MaxBytesCachedLog) / static_cast<u32>(Size);

    // Note that Config::MaxNumCachedHint is u16 so the result is guaranteed to
    // fit in u16.
    return static_cast<u16>(Max(1U, Min<u32>(Config::MaxNumCachedHint, N)));
  }
};

// SizeClassMap maps allocation sizes into size classes and back, in an
// efficient table-free manner.
//
// Class 0 is a special class that doesn't abide by the same rules as other
// classes. The allocator uses it to hold batches.
//
// The other sizes are controlled by the template parameters:
// - MinSizeLog: defines the first class as 2^MinSizeLog bytes.
// - MaxSizeLog: defines the last class as 2^MaxSizeLog bytes.
// - MidSizeLog: classes increase with step 2^MinSizeLog from 2^MinSizeLog to
//               2^MidSizeLog bytes.
// - NumBits: the number of non-zero bits in sizes after 2^MidSizeLog.
//            eg. with NumBits==3 all size classes after 2^MidSizeLog look like
//            0b1xx0..0 (where x is either 0 or 1).
//
// This class also gives a hint to a thread-caching allocator about the amount
// of chunks that can be cached per-thread:
// - MaxNumCachedHint is a hint for the max number of chunks cached per class.
// - 2^MaxBytesCachedLog is the max number of bytes cached per class.
template <typename Config>
class FixedSizeClassMap : public SizeClassMapBase<Config> {
  typedef SizeClassMapBase<Config> Base;

  static const uptr MinSize = 1UL << Config::MinSizeLog;
  static const uptr MidSize = 1UL << Config::MidSizeLog;
  static const uptr MidClass = MidSize / MinSize;
  static const u8 S = Config::NumBits - 1;
  static const uptr M = (1UL << S) - 1;

public:
  static const u16 MaxNumCachedHint = Config::MaxNumCachedHint;

  static const uptr MaxSize = (1UL << Config::MaxSizeLog) + Config::SizeDelta;
  static const uptr NumClasses =
      MidClass + ((Config::MaxSizeLog - Config::MidSizeLog) << S) + 1;
  static_assert(NumClasses <= 256, "");
  static const uptr LargestClassId = NumClasses - 1;
  static const uptr BatchClassId = 0;

  static uptr getSizeByClassId(uptr ClassId) {
    DCHECK_NE(ClassId, BatchClassId);
    if (ClassId <= MidClass)
      return (ClassId << Config::MinSizeLog) + Config::SizeDelta;
    ClassId -= MidClass;
    const uptr T = MidSize << (ClassId >> S);
    return T + (T >> S) * (ClassId & M) + Config::SizeDelta;
  }

  static u8 getSizeLSBByClassId(uptr ClassId) {
    return u8(getLeastSignificantSetBitIndex(getSizeByClassId(ClassId)));
  }

  static constexpr bool usesCompressedLSBFormat() { return false; }

  static uptr getClassIdBySize(uptr Size) {
    if (Size <= Config::SizeDelta + (1 << Config::MinSizeLog))
      return 1;
    Size -= Config::SizeDelta;
    DCHECK_LE(Size, MaxSize);
    if (Size <= MidSize)
      return (Size + MinSize - 1) >> Config::MinSizeLog;
    return MidClass + 1 + scaledLog2(Size - 1, Config::MidSizeLog, S);
  }

  static u16 getMaxCachedHint(uptr Size) {
    DCHECK_LE(Size, MaxSize);
    return Base::getMaxCachedHint(Size);
  }
};

template <typename Config>
class TableSizeClassMap : public SizeClassMapBase<Config> {
  typedef SizeClassMapBase<Config> Base;

  static const u8 S = Config::NumBits - 1;
  static const uptr M = (1UL << S) - 1;
  static const uptr ClassesSize =
      sizeof(Config::Classes) / sizeof(Config::Classes[0]);

  struct SizeTable {
    constexpr SizeTable() {
      uptr Pos = 1 << Config::MidSizeLog;
      uptr Inc = 1 << (Config::MidSizeLog - S);
      for (uptr i = 0; i != getTableSize(); ++i) {
        Pos += Inc;
        if ((Pos & (Pos - 1)) == 0)
          Inc *= 2;
        Tab[i] = computeClassId(Pos + Config::SizeDelta);
      }
    }

    constexpr static u8 computeClassId(uptr Size) {
      for (uptr i = 0; i != ClassesSize; ++i) {
        if (Size <= Config::Classes[i])
          return static_cast<u8>(i + 1);
      }
      return static_cast<u8>(-1);
    }

    constexpr static uptr getTableSize() {
      return (Config::MaxSizeLog - Config::MidSizeLog) << S;
    }

    u8 Tab[getTableSize()] = {};
  };

  static constexpr SizeTable SzTable = {};

  struct LSBTable {
    constexpr LSBTable() {
      u8 Min = 255, Max = 0;
      for (uptr I = 0; I != ClassesSize; ++I) {
        for (u8 Bit = 0; Bit != 64; ++Bit) {
          if (Config::Classes[I] & (1 << Bit)) {
            Tab[I] = Bit;
            if (Bit < Min)
              Min = Bit;
            if (Bit > Max)
              Max = Bit;
            break;
          }
        }
      }

      if (Max - Min > 3 || ClassesSize > 32)
        return;

      UseCompressedFormat = true;
      CompressedMin = Min;
      for (uptr I = 0; I != ClassesSize; ++I)
        CompressedValue |= u64(Tab[I] - Min) << (I * 2);
    }

    u8 Tab[ClassesSize] = {};

    bool UseCompressedFormat = false;
    u8 CompressedMin = 0;
    u64 CompressedValue = 0;
  };

  static constexpr LSBTable LTable = {};

public:
  static const u16 MaxNumCachedHint = Config::MaxNumCachedHint;

  static const uptr NumClasses = ClassesSize + 1;
  static_assert(NumClasses < 256, "");
  static const uptr LargestClassId = NumClasses - 1;
  static const uptr BatchClassId = 0;
  static const uptr MaxSize = Config::Classes[LargestClassId - 1];

  static uptr getSizeByClassId(uptr ClassId) {
    return Config::Classes[ClassId - 1];
  }

  static u8 getSizeLSBByClassId(uptr ClassId) {
    if (LTable.UseCompressedFormat)
      return ((LTable.CompressedValue >> ((ClassId - 1) * 2)) & 3) +
             LTable.CompressedMin;
    else
      return LTable.Tab[ClassId - 1];
  }

  static constexpr bool usesCompressedLSBFormat() {
    return LTable.UseCompressedFormat;
  }

  static uptr getClassIdBySize(uptr Size) {
    if (Size <= Config::Classes[0])
      return 1;
    Size -= Config::SizeDelta;
    DCHECK_LE(Size, MaxSize);
    if (Size <= (1 << Config::MidSizeLog))
      return ((Size - 1) >> Config::MinSizeLog) + 1;
    return SzTable.Tab[scaledLog2(Size - 1, Config::MidSizeLog, S)];
  }

  static u16 getMaxCachedHint(uptr Size) {
    DCHECK_LE(Size, MaxSize);
    return Base::getMaxCachedHint(Size);
  }
};

struct DefaultSizeClassConfig {
  static const uptr NumBits = 3;
  static const uptr MinSizeLog = 5;
  static const uptr MidSizeLog = 8;
  static const uptr MaxSizeLog = 17;
  static const u16 MaxNumCachedHint = 14;
  static const uptr MaxBytesCachedLog = 10;
  static const uptr SizeDelta = 0;
};

typedef FixedSizeClassMap<DefaultSizeClassConfig> DefaultSizeClassMap;

struct FuchsiaSizeClassConfig {
  static const uptr NumBits = 3;
  static const uptr MinSizeLog = 5;
  static const uptr MidSizeLog = 8;
  static const uptr MaxSizeLog = 17;
  static const u16 MaxNumCachedHint = 12;
  static const uptr MaxBytesCachedLog = 10;
  static const uptr SizeDelta = Chunk::getHeaderSize();
};

typedef FixedSizeClassMap<FuchsiaSizeClassConfig> FuchsiaSizeClassMap;

struct AndroidSizeClassConfig {
#if SCUDO_WORDSIZE == 64U
  static const uptr NumBits = 7;
  static const uptr MinSizeLog = 4;
  static const uptr MidSizeLog = 6;
  static const uptr MaxSizeLog = 16;
  static const u16 MaxNumCachedHint = 13;
  static const uptr MaxBytesCachedLog = 13;

  static constexpr uptr Classes[] = {
      0x00020, 0x00030, 0x00040, 0x00050, 0x00060, 0x00070, 0x00090, 0x000b0,
      0x000c0, 0x000e0, 0x00120, 0x00160, 0x001c0, 0x00250, 0x00320, 0x00450,
      0x00670, 0x00830, 0x00a10, 0x00c30, 0x01010, 0x01210, 0x01bd0, 0x02210,
      0x02d90, 0x03790, 0x04010, 0x04810, 0x05a10, 0x07310, 0x08210, 0x10010,
  };
  static const uptr SizeDelta = 16;
#else
  static const uptr NumBits = 8;
  static const uptr MinSizeLog = 4;
  static const uptr MidSizeLog = 7;
  static const uptr MaxSizeLog = 16;
  static const u16 MaxNumCachedHint = 14;
  static const uptr MaxBytesCachedLog = 13;

  static constexpr uptr Classes[] = {
      0x00020, 0x00030, 0x00040, 0x00050, 0x00060, 0x00070, 0x00080, 0x00090,
      0x000a0, 0x000b0, 0x000c0, 0x000e0, 0x000f0, 0x00110, 0x00120, 0x00130,
      0x00150, 0x00160, 0x00170, 0x00190, 0x001d0, 0x00210, 0x00240, 0x002a0,
      0x00330, 0x00370, 0x003a0, 0x00400, 0x00430, 0x004a0, 0x00530, 0x00610,
      0x00730, 0x00840, 0x00910, 0x009c0, 0x00a60, 0x00b10, 0x00ca0, 0x00e00,
      0x00fb0, 0x01030, 0x01130, 0x011f0, 0x01490, 0x01650, 0x01930, 0x02010,
      0x02190, 0x02490, 0x02850, 0x02d50, 0x03010, 0x03210, 0x03c90, 0x04090,
      0x04510, 0x04810, 0x05c10, 0x06f10, 0x07310, 0x08010, 0x0c010, 0x10010,
  };
  static const uptr SizeDelta = 16;
#endif
};

typedef TableSizeClassMap<AndroidSizeClassConfig> AndroidSizeClassMap;

#if SCUDO_WORDSIZE == 64U && defined(__clang__)
static_assert(AndroidSizeClassMap::usesCompressedLSBFormat(), "");
#endif

struct TrustySizeClassConfig {
  static const uptr NumBits = 1;
  static const uptr MinSizeLog = 5;
  static const uptr MidSizeLog = 5;
  static const uptr MaxSizeLog = 15;
  static const u16 MaxNumCachedHint = 12;
  static const uptr MaxBytesCachedLog = 10;
  static const uptr SizeDelta = 0;
};

typedef FixedSizeClassMap<TrustySizeClassConfig> TrustySizeClassMap;

template <typename SCMap> inline void printMap() {
  ScopedString Buffer;
  uptr PrevS = 0;
  uptr TotalCached = 0;
  for (uptr I = 0; I < SCMap::NumClasses; I++) {
    if (I == SCMap::BatchClassId)
      continue;
    const uptr S = SCMap::getSizeByClassId(I);
    const uptr D = S - PrevS;
    const uptr P = PrevS ? (D * 100 / PrevS) : 0;
    const uptr L = S ? getMostSignificantSetBitIndex(S) : 0;
    const uptr Cached = SCMap::getMaxCachedHint(S) * S;
    Buffer.append(
        "C%02zu => S: %zu diff: +%zu %02zu%% L %zu Cached: %u %zu; id %zu\n", I,
        S, D, P, L, SCMap::getMaxCachedHint(S), Cached,
        SCMap::getClassIdBySize(S));
    TotalCached += Cached;
    PrevS = S;
  }
  Buffer.append("Total Cached: %zu\n", TotalCached);
  Buffer.output();
}

template <typename SCMap> static UNUSED void validateMap() {
  for (uptr C = 0; C < SCMap::NumClasses; C++) {
    if (C == SCMap::BatchClassId)
      continue;
    const uptr S = SCMap::getSizeByClassId(C);
    CHECK_NE(S, 0U);
    CHECK_EQ(SCMap::getClassIdBySize(S), C);
    if (C < SCMap::LargestClassId)
      CHECK_EQ(SCMap::getClassIdBySize(S + 1), C + 1);
    CHECK_EQ(SCMap::getClassIdBySize(S - 1), C);
    if (C - 1 != SCMap::BatchClassId)
      CHECK_GT(SCMap::getSizeByClassId(C), SCMap::getSizeByClassId(C - 1));
  }
  // Do not perform the loop if the maximum size is too large.
  if (SCMap::MaxSize > (1 << 19))
    return;
  for (uptr S = 1; S <= SCMap::MaxSize; S++) {
    const uptr C = SCMap::getClassIdBySize(S);
    CHECK_LT(C, SCMap::NumClasses);
    CHECK_GE(SCMap::getSizeByClassId(C), S);
    if (C - 1 != SCMap::BatchClassId)
      CHECK_LT(SCMap::getSizeByClassId(C - 1), S);
  }
}
} // namespace scudo

#endif // SCUDO_SIZE_CLASS_MAP_H_
