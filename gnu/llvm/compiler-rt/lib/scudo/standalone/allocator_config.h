//===-- allocator_config.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_ALLOCATOR_CONFIG_H_
#define SCUDO_ALLOCATOR_CONFIG_H_

#include "combined.h"
#include "common.h"
#include "condition_variable.h"
#include "flags.h"
#include "primary32.h"
#include "primary64.h"
#include "secondary.h"
#include "size_class_map.h"
#include "tsd_exclusive.h"
#include "tsd_shared.h"

// To import a custom configuration, define `SCUDO_USE_CUSTOM_CONFIG` and
// aliasing the `Config` like:
//
// namespace scudo {
//   // The instance of Scudo will be initiated with `Config`.
//   typedef CustomConfig Config;
//   // Aliasing as default configuration to run the tests with this config.
//   typedef CustomConfig DefaultConfig;
// } // namespace scudo
//
// Put them in the header `custom_scudo_config.h` then you will be using the
// custom configuration and able to run all the tests as well.
#ifdef SCUDO_USE_CUSTOM_CONFIG
#include "custom_scudo_config.h"
#endif

namespace scudo {

// Scudo uses a structure as a template argument that specifies the
// configuration options for the various subcomponents of the allocator. See the
// following configs as examples and check `allocator_config.def` for all the
// available options.

#ifndef SCUDO_USE_CUSTOM_CONFIG

// Default configurations for various platforms. Note this is only enabled when
// there's no custom configuration in the build system.
struct DefaultConfig {
  static const bool MaySupportMemoryTagging = true;
  template <class A> using TSDRegistryT = TSDRegistryExT<A>; // Exclusive

  struct Primary {
    using SizeClassMap = DefaultSizeClassMap;
#if SCUDO_CAN_USE_PRIMARY64
    static const uptr RegionSizeLog = 32U;
    static const uptr GroupSizeLog = 21U;
    typedef uptr CompactPtrT;
    static const uptr CompactPtrScale = 0;
    static const bool EnableRandomOffset = true;
    static const uptr MapSizeIncrement = 1UL << 18;
#else
    static const uptr RegionSizeLog = 19U;
    static const uptr GroupSizeLog = 19U;
    typedef uptr CompactPtrT;
#endif
    static const s32 MinReleaseToOsIntervalMs = INT32_MIN;
    static const s32 MaxReleaseToOsIntervalMs = INT32_MAX;
  };
#if SCUDO_CAN_USE_PRIMARY64
  template <typename Config> using PrimaryT = SizeClassAllocator64<Config>;
#else
  template <typename Config> using PrimaryT = SizeClassAllocator32<Config>;
#endif

  struct Secondary {
    struct Cache {
      static const u32 EntriesArraySize = 32U;
      static const u32 QuarantineSize = 0U;
      static const u32 DefaultMaxEntriesCount = 32U;
      static const uptr DefaultMaxEntrySize = 1UL << 19;
      static const s32 MinReleaseToOsIntervalMs = INT32_MIN;
      static const s32 MaxReleaseToOsIntervalMs = INT32_MAX;
    };
    template <typename Config> using CacheT = MapAllocatorCache<Config>;
  };

  template <typename Config> using SecondaryT = MapAllocator<Config>;
};

#endif // SCUDO_USE_CUSTOM_CONFIG

struct AndroidConfig {
  static const bool MaySupportMemoryTagging = true;
  template <class A>
  using TSDRegistryT = TSDRegistrySharedT<A, 8U, 2U>; // Shared, max 8 TSDs.

  struct Primary {
    using SizeClassMap = AndroidSizeClassMap;
#if SCUDO_CAN_USE_PRIMARY64
    static const uptr RegionSizeLog = 28U;
    typedef u32 CompactPtrT;
    static const uptr CompactPtrScale = SCUDO_MIN_ALIGNMENT_LOG;
    static const uptr GroupSizeLog = 20U;
    static const bool EnableRandomOffset = true;
    static const uptr MapSizeIncrement = 1UL << 18;
#else
    static const uptr RegionSizeLog = 18U;
    static const uptr GroupSizeLog = 18U;
    typedef uptr CompactPtrT;
#endif
    static const s32 MinReleaseToOsIntervalMs = 1000;
    static const s32 MaxReleaseToOsIntervalMs = 1000;
  };
#if SCUDO_CAN_USE_PRIMARY64
  template <typename Config> using PrimaryT = SizeClassAllocator64<Config>;
#else
  template <typename Config> using PrimaryT = SizeClassAllocator32<Config>;
#endif

  struct Secondary {
    struct Cache {
      static const u32 EntriesArraySize = 256U;
      static const u32 QuarantineSize = 32U;
      static const u32 DefaultMaxEntriesCount = 32U;
      static const uptr DefaultMaxEntrySize = 2UL << 20;
      static const s32 MinReleaseToOsIntervalMs = 0;
      static const s32 MaxReleaseToOsIntervalMs = 1000;
    };
    template <typename Config> using CacheT = MapAllocatorCache<Config>;
  };

  template <typename Config> using SecondaryT = MapAllocator<Config>;
};

#if SCUDO_CAN_USE_PRIMARY64
struct FuchsiaConfig {
  static const bool MaySupportMemoryTagging = false;
  template <class A>
  using TSDRegistryT = TSDRegistrySharedT<A, 8U, 4U>; // Shared, max 8 TSDs.

  struct Primary {
    using SizeClassMap = FuchsiaSizeClassMap;
#if SCUDO_RISCV64
    // Support 39-bit VMA for riscv-64
    static const uptr RegionSizeLog = 28U;
    static const uptr GroupSizeLog = 19U;
    static const bool EnableContiguousRegions = false;
#else
    static const uptr RegionSizeLog = 30U;
    static const uptr GroupSizeLog = 21U;
#endif
    typedef u32 CompactPtrT;
    static const bool EnableRandomOffset = true;
    static const uptr MapSizeIncrement = 1UL << 18;
    static const uptr CompactPtrScale = SCUDO_MIN_ALIGNMENT_LOG;
    static const s32 MinReleaseToOsIntervalMs = INT32_MIN;
    static const s32 MaxReleaseToOsIntervalMs = INT32_MAX;
  };
  template <typename Config> using PrimaryT = SizeClassAllocator64<Config>;

  struct Secondary {
    template <typename Config> using CacheT = MapAllocatorNoCache<Config>;
  };
  template <typename Config> using SecondaryT = MapAllocator<Config>;
};

struct TrustyConfig {
  static const bool MaySupportMemoryTagging = true;
  template <class A>
  using TSDRegistryT = TSDRegistrySharedT<A, 1U, 1U>; // Shared, max 1 TSD.

  struct Primary {
    using SizeClassMap = TrustySizeClassMap;
    static const uptr RegionSizeLog = 28U;
    static const uptr GroupSizeLog = 20U;
    typedef u32 CompactPtrT;
    static const bool EnableRandomOffset = false;
    static const uptr MapSizeIncrement = 1UL << 12;
    static const uptr CompactPtrScale = SCUDO_MIN_ALIGNMENT_LOG;
    static const s32 MinReleaseToOsIntervalMs = INT32_MIN;
    static const s32 MaxReleaseToOsIntervalMs = INT32_MAX;
  };
  template <typename Config> using PrimaryT = SizeClassAllocator64<Config>;

  struct Secondary {
    template <typename Config> using CacheT = MapAllocatorNoCache<Config>;
  };

  template <typename Config> using SecondaryT = MapAllocator<Config>;
};
#endif

#ifndef SCUDO_USE_CUSTOM_CONFIG

#if SCUDO_ANDROID
typedef AndroidConfig Config;
#elif SCUDO_FUCHSIA
typedef FuchsiaConfig Config;
#elif SCUDO_TRUSTY
typedef TrustyConfig Config;
#else
typedef DefaultConfig Config;
#endif

#endif // SCUDO_USE_CUSTOM_CONFIG

} // namespace scudo

#endif // SCUDO_ALLOCATOR_CONFIG_H_
