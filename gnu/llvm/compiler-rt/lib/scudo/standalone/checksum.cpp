//===-- checksum.cpp --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "checksum.h"
#include "atomic_helpers.h"
#include "chunk.h"

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#elif defined(__arm__) || defined(__aarch64__)
#if SCUDO_FUCHSIA
#include <zircon/features.h>
#include <zircon/syscalls.h>
#else
#include <sys/auxv.h>
#endif
#elif defined(__loongarch__)
#include <sys/auxv.h>
#endif

namespace scudo {

Checksum HashAlgorithm = {Checksum::BSD};

#if defined(__x86_64__) || defined(__i386__)
// i386 and x86_64 specific code to detect CRC32 hardware support via CPUID.
// CRC32 requires the SSE 4.2 instruction set.
#ifndef bit_SSE4_2
#define bit_SSE4_2 bit_SSE42 // clang and gcc have different defines.
#endif

#ifndef signature_HYGON_ebx // They are not defined in gcc.
// HYGON: "HygonGenuine".
#define signature_HYGON_ebx 0x6f677948
#define signature_HYGON_edx 0x6e65476e
#define signature_HYGON_ecx 0x656e6975
#endif

bool hasHardwareCRC32() {
  u32 Eax, Ebx = 0, Ecx = 0, Edx = 0;
  __get_cpuid(0, &Eax, &Ebx, &Ecx, &Edx);
  const bool IsIntel = (Ebx == signature_INTEL_ebx) &&
                       (Edx == signature_INTEL_edx) &&
                       (Ecx == signature_INTEL_ecx);
  const bool IsAMD = (Ebx == signature_AMD_ebx) && (Edx == signature_AMD_edx) &&
                     (Ecx == signature_AMD_ecx);
  const bool IsHygon = (Ebx == signature_HYGON_ebx) &&
                       (Edx == signature_HYGON_edx) &&
                       (Ecx == signature_HYGON_ecx);
  if (!IsIntel && !IsAMD && !IsHygon)
    return false;
  __get_cpuid(1, &Eax, &Ebx, &Ecx, &Edx);
  return !!(Ecx & bit_SSE4_2);
}
#elif defined(__arm__) || defined(__aarch64__)
#ifndef AT_HWCAP
#define AT_HWCAP 16
#endif
#ifndef HWCAP_CRC32
#define HWCAP_CRC32 (1U << 7) // HWCAP_CRC32 is missing on older platforms.
#endif

bool hasHardwareCRC32() {
#if SCUDO_FUCHSIA
  u32 HWCap;
  const zx_status_t Status =
      zx_system_get_features(ZX_FEATURE_KIND_CPU, &HWCap);
  if (Status != ZX_OK)
    return false;
  return !!(HWCap & ZX_ARM64_FEATURE_ISA_CRC32);
#else
  return !!(getauxval(AT_HWCAP) & HWCAP_CRC32);
#endif // SCUDO_FUCHSIA
}
#elif defined(__loongarch__)
// The definition is only pulled in by <sys/auxv.h> since glibc 2.38, so
// supply it if missing.
#ifndef HWCAP_LOONGARCH_CRC32
#define HWCAP_LOONGARCH_CRC32 (1 << 6)
#endif
// Query HWCAP for platform capability, according to *Software Development and
// Build Convention for LoongArch Architectures* v0.1, Section 9.1.
//
// Link:
// https://github.com/loongson/la-softdev-convention/blob/v0.1/la-softdev-convention.adoc#kernel-development
bool hasHardwareCRC32() {
  return !!(getauxval(AT_HWCAP) & HWCAP_LOONGARCH_CRC32);
}
#else
// No hardware CRC32 implemented in Scudo for other architectures.
bool hasHardwareCRC32() { return false; }
#endif // defined(__x86_64__) || defined(__i386__)

} // namespace scudo
