//===-- cpu_model.c - Support for __cpu_model builtin  ------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file is based on LLVM's lib/Support/Host.cpp.
//  It implements the operating system Host concept and builtin
//  __cpu_model for the compiler_rt library for x86 and
//  __aarch64_have_lse_atomics, __aarch64_cpu_features for AArch64.
//
//===----------------------------------------------------------------------===//

#ifndef __has_attribute
#define __has_attribute(attr) 0
#endif

#if __has_attribute(constructor)
#if __GNUC__ >= 9
// Ordinarily init priorities below 101 are disallowed as they are reserved for the
// implementation. However, we are the implementation, so silence the diagnostic,
// since it doesn't apply to us.
#pragma GCC diagnostic ignored "-Wprio-ctor-dtor"
#endif
// We're choosing init priority 90 to force our constructors to run before any
// constructors in the end user application (starting at priority 101). This value
// matches the libgcc choice for the same functions.
#define CONSTRUCTOR_ATTRIBUTE __attribute__((constructor(90)))
#else
// FIXME: For MSVC, we should make a function pointer global in .CRT$X?? so that
// this runs during initialization.
#define CONSTRUCTOR_ATTRIBUTE
#endif

#if (defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) ||           \
     defined(_M_X64)) &&                                                       \
    (defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER))

#include <assert.h>

#define bool int
#define true 1
#define false 0

#ifdef _MSC_VER
#include <intrin.h>
#endif

enum VendorSignatures {
  SIG_INTEL = 0x756e6547, // Genu
  SIG_AMD = 0x68747541,   // Auth
};

enum ProcessorVendors {
  VENDOR_INTEL = 1,
  VENDOR_AMD,
  VENDOR_OTHER,
  VENDOR_MAX
};

enum ProcessorTypes {
  INTEL_BONNELL = 1,
  INTEL_CORE2,
  INTEL_COREI7,
  AMDFAM10H,
  AMDFAM15H,
  INTEL_SILVERMONT,
  INTEL_KNL,
  AMD_BTVER1,
  AMD_BTVER2,
  AMDFAM17H,
  INTEL_KNM,
  INTEL_GOLDMONT,
  INTEL_GOLDMONT_PLUS,
  INTEL_TREMONT,
  AMDFAM19H,
  ZHAOXIN_FAM7H,
  INTEL_SIERRAFOREST,
  INTEL_GRANDRIDGE,
  CPU_TYPE_MAX
};

enum ProcessorSubtypes {
  INTEL_COREI7_NEHALEM = 1,
  INTEL_COREI7_WESTMERE,
  INTEL_COREI7_SANDYBRIDGE,
  AMDFAM10H_BARCELONA,
  AMDFAM10H_SHANGHAI,
  AMDFAM10H_ISTANBUL,
  AMDFAM15H_BDVER1,
  AMDFAM15H_BDVER2,
  AMDFAM15H_BDVER3,
  AMDFAM15H_BDVER4,
  AMDFAM17H_ZNVER1,
  INTEL_COREI7_IVYBRIDGE,
  INTEL_COREI7_HASWELL,
  INTEL_COREI7_BROADWELL,
  INTEL_COREI7_SKYLAKE,
  INTEL_COREI7_SKYLAKE_AVX512,
  INTEL_COREI7_CANNONLAKE,
  INTEL_COREI7_ICELAKE_CLIENT,
  INTEL_COREI7_ICELAKE_SERVER,
  AMDFAM17H_ZNVER2,
  INTEL_COREI7_CASCADELAKE,
  INTEL_COREI7_TIGERLAKE,
  INTEL_COREI7_COOPERLAKE,
  INTEL_COREI7_SAPPHIRERAPIDS,
  INTEL_COREI7_ALDERLAKE,
  AMDFAM19H_ZNVER3,
  INTEL_COREI7_ROCKETLAKE,
  ZHAOXIN_FAM7H_LUJIAZUI,
  AMDFAM19H_ZNVER4,
  INTEL_COREI7_GRANITERAPIDS,
  CPU_SUBTYPE_MAX
};

enum ProcessorFeatures {
  FEATURE_CMOV = 0,
  FEATURE_MMX,
  FEATURE_POPCNT,
  FEATURE_SSE,
  FEATURE_SSE2,
  FEATURE_SSE3,
  FEATURE_SSSE3,
  FEATURE_SSE4_1,
  FEATURE_SSE4_2,
  FEATURE_AVX,
  FEATURE_AVX2,
  FEATURE_SSE4_A,
  FEATURE_FMA4,
  FEATURE_XOP,
  FEATURE_FMA,
  FEATURE_AVX512F,
  FEATURE_BMI,
  FEATURE_BMI2,
  FEATURE_AES,
  FEATURE_PCLMUL,
  FEATURE_AVX512VL,
  FEATURE_AVX512BW,
  FEATURE_AVX512DQ,
  FEATURE_AVX512CD,
  FEATURE_AVX512ER,
  FEATURE_AVX512PF,
  FEATURE_AVX512VBMI,
  FEATURE_AVX512IFMA,
  FEATURE_AVX5124VNNIW,
  FEATURE_AVX5124FMAPS,
  FEATURE_AVX512VPOPCNTDQ,
  FEATURE_AVX512VBMI2,
  FEATURE_GFNI,
  FEATURE_VPCLMULQDQ,
  FEATURE_AVX512VNNI,
  FEATURE_AVX512BITALG,
  FEATURE_AVX512BF16,
  FEATURE_AVX512VP2INTERSECT,
  CPU_FEATURE_MAX
};

// The check below for i386 was copied from clang's cpuid.h (__get_cpuid_max).
// Check motivated by bug reports for OpenSSL crashing on CPUs without CPUID
// support. Consequently, for i386, the presence of CPUID is checked first
// via the corresponding eflags bit.
static bool isCpuIdSupported(void) {
#if defined(__GNUC__) || defined(__clang__)
#if defined(__i386__)
  int __cpuid_supported;
  __asm__("  pushfl\n"
          "  popl   %%eax\n"
          "  movl   %%eax,%%ecx\n"
          "  xorl   $0x00200000,%%eax\n"
          "  pushl  %%eax\n"
          "  popfl\n"
          "  pushfl\n"
          "  popl   %%eax\n"
          "  movl   $0,%0\n"
          "  cmpl   %%eax,%%ecx\n"
          "  je     1f\n"
          "  movl   $1,%0\n"
          "1:"
          : "=r"(__cpuid_supported)
          :
          : "eax", "ecx");
  if (!__cpuid_supported)
    return false;
#endif
  return true;
#endif
  return true;
}

// This code is copied from lib/Support/Host.cpp.
// Changes to either file should be mirrored in the other.

/// getX86CpuIDAndInfo - Execute the specified cpuid and return the 4 values in
/// the specified arguments.  If we can't run cpuid on the host, return true.
static bool getX86CpuIDAndInfo(unsigned value, unsigned *rEAX, unsigned *rEBX,
                               unsigned *rECX, unsigned *rEDX) {
#if defined(__GNUC__) || defined(__clang__)
#if defined(__x86_64__)
  // gcc doesn't know cpuid would clobber ebx/rbx. Preserve it manually.
  // FIXME: should we save this for Clang?
  __asm__("movq\t%%rbx, %%rsi\n\t"
          "cpuid\n\t"
          "xchgq\t%%rbx, %%rsi\n\t"
          : "=a"(*rEAX), "=S"(*rEBX), "=c"(*rECX), "=d"(*rEDX)
          : "a"(value));
  return false;
#elif defined(__i386__)
  __asm__("movl\t%%ebx, %%esi\n\t"
          "cpuid\n\t"
          "xchgl\t%%ebx, %%esi\n\t"
          : "=a"(*rEAX), "=S"(*rEBX), "=c"(*rECX), "=d"(*rEDX)
          : "a"(value));
  return false;
#else
  return true;
#endif
#elif defined(_MSC_VER)
  // The MSVC intrinsic is portable across x86 and x64.
  int registers[4];
  __cpuid(registers, value);
  *rEAX = registers[0];
  *rEBX = registers[1];
  *rECX = registers[2];
  *rEDX = registers[3];
  return false;
#else
  return true;
#endif
}

/// getX86CpuIDAndInfoEx - Execute the specified cpuid with subleaf and return
/// the 4 values in the specified arguments.  If we can't run cpuid on the host,
/// return true.
static bool getX86CpuIDAndInfoEx(unsigned value, unsigned subleaf,
                                 unsigned *rEAX, unsigned *rEBX, unsigned *rECX,
                                 unsigned *rEDX) {
#if defined(__GNUC__) || defined(__clang__)
#if defined(__x86_64__)
  // gcc doesn't know cpuid would clobber ebx/rbx. Preserve it manually.
  // FIXME: should we save this for Clang?
  __asm__("movq\t%%rbx, %%rsi\n\t"
          "cpuid\n\t"
          "xchgq\t%%rbx, %%rsi\n\t"
          : "=a"(*rEAX), "=S"(*rEBX), "=c"(*rECX), "=d"(*rEDX)
          : "a"(value), "c"(subleaf));
  return false;
#elif defined(__i386__)
  __asm__("movl\t%%ebx, %%esi\n\t"
          "cpuid\n\t"
          "xchgl\t%%ebx, %%esi\n\t"
          : "=a"(*rEAX), "=S"(*rEBX), "=c"(*rECX), "=d"(*rEDX)
          : "a"(value), "c"(subleaf));
  return false;
#else
  return true;
#endif
#elif defined(_MSC_VER)
  int registers[4];
  __cpuidex(registers, value, subleaf);
  *rEAX = registers[0];
  *rEBX = registers[1];
  *rECX = registers[2];
  *rEDX = registers[3];
  return false;
#else
  return true;
#endif
}

// Read control register 0 (XCR0). Used to detect features such as AVX.
static bool getX86XCR0(unsigned *rEAX, unsigned *rEDX) {
#if defined(__GNUC__) || defined(__clang__)
  // Check xgetbv; this uses a .byte sequence instead of the instruction
  // directly because older assemblers do not include support for xgetbv and
  // there is no easy way to conditionally compile based on the assembler used.
  __asm__(".byte 0x0f, 0x01, 0xd0" : "=a"(*rEAX), "=d"(*rEDX) : "c"(0));
  return false;
#elif defined(_MSC_FULL_VER) && defined(_XCR_XFEATURE_ENABLED_MASK)
  unsigned long long Result = _xgetbv(_XCR_XFEATURE_ENABLED_MASK);
  *rEAX = Result;
  *rEDX = Result >> 32;
  return false;
#else
  return true;
#endif
}

static void detectX86FamilyModel(unsigned EAX, unsigned *Family,
                                 unsigned *Model) {
  *Family = (EAX >> 8) & 0xf; // Bits 8 - 11
  *Model = (EAX >> 4) & 0xf;  // Bits 4 - 7
  if (*Family == 6 || *Family == 0xf) {
    if (*Family == 0xf)
      // Examine extended family ID if family ID is F.
      *Family += (EAX >> 20) & 0xff; // Bits 20 - 27
    // Examine extended model ID if family ID is 6 or F.
    *Model += ((EAX >> 16) & 0xf) << 4; // Bits 16 - 19
  }
}

static const char *
getIntelProcessorTypeAndSubtype(unsigned Family, unsigned Model,
                                const unsigned *Features,
                                unsigned *Type, unsigned *Subtype) {
#define testFeature(F)                                                         \
  (Features[F / 32] & (1 << (F % 32))) != 0

  // We select CPU strings to match the code in Host.cpp, but we don't use them
  // in compiler-rt.
  const char *CPU = 0;

  switch (Family) {
  case 6:
    switch (Model) {
    case 0x0f: // Intel Core 2 Duo processor, Intel Core 2 Duo mobile
               // processor, Intel Core 2 Quad processor, Intel Core 2 Quad
               // mobile processor, Intel Core 2 Extreme processor, Intel
               // Pentium Dual-Core processor, Intel Xeon processor, model
               // 0Fh. All processors are manufactured using the 65 nm process.
    case 0x16: // Intel Celeron processor model 16h. All processors are
               // manufactured using the 65 nm process
      CPU = "core2";
      *Type = INTEL_CORE2;
      break;
    case 0x17: // Intel Core 2 Extreme processor, Intel Xeon processor, model
               // 17h. All processors are manufactured using the 45 nm process.
               //
               // 45nm: Penryn , Wolfdale, Yorkfield (XE)
    case 0x1d: // Intel Xeon processor MP. All processors are manufactured using
               // the 45 nm process.
      CPU = "penryn";
      *Type = INTEL_CORE2;
      break;
    case 0x1a: // Intel Core i7 processor and Intel Xeon processor. All
               // processors are manufactured using the 45 nm process.
    case 0x1e: // Intel(R) Core(TM) i7 CPU         870  @ 2.93GHz.
               // As found in a Summer 2010 model iMac.
    case 0x1f:
    case 0x2e:              // Nehalem EX
      CPU = "nehalem";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_NEHALEM;
      break;
    case 0x25: // Intel Core i7, laptop version.
    case 0x2c: // Intel Core i7 processor and Intel Xeon processor. All
               // processors are manufactured using the 32 nm process.
    case 0x2f: // Westmere EX
      CPU = "westmere";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_WESTMERE;
      break;
    case 0x2a: // Intel Core i7 processor. All processors are manufactured
               // using the 32 nm process.
    case 0x2d:
      CPU = "sandybridge";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_SANDYBRIDGE;
      break;
    case 0x3a:
    case 0x3e:              // Ivy Bridge EP
      CPU = "ivybridge";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_IVYBRIDGE;
      break;

    // Haswell:
    case 0x3c:
    case 0x3f:
    case 0x45:
    case 0x46:
      CPU = "haswell";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_HASWELL;
      break;

    // Broadwell:
    case 0x3d:
    case 0x47:
    case 0x4f:
    case 0x56:
      CPU = "broadwell";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_BROADWELL;
      break;

    // Skylake:
    case 0x4e:              // Skylake mobile
    case 0x5e:              // Skylake desktop
    case 0x8e:              // Kaby Lake mobile
    case 0x9e:              // Kaby Lake desktop
    case 0xa5:              // Comet Lake-H/S
    case 0xa6:              // Comet Lake-U
      CPU = "skylake";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_SKYLAKE;
      break;

    // Rocketlake:
    case 0xa7:
      CPU = "rocketlake";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_ROCKETLAKE;
      break;

    // Skylake Xeon:
    case 0x55:
      *Type = INTEL_COREI7;
      if (testFeature(FEATURE_AVX512BF16)) {
        CPU = "cooperlake";
        *Subtype = INTEL_COREI7_COOPERLAKE;
      } else if (testFeature(FEATURE_AVX512VNNI)) {
        CPU = "cascadelake";
        *Subtype = INTEL_COREI7_CASCADELAKE;
      } else {
        CPU = "skylake-avx512";
        *Subtype = INTEL_COREI7_SKYLAKE_AVX512;
      }
      break;

    // Cannonlake:
    case 0x66:
      CPU = "cannonlake";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_CANNONLAKE;
      break;

    // Icelake:
    case 0x7d:
    case 0x7e:
      CPU = "icelake-client";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_ICELAKE_CLIENT;
      break;

    // Tigerlake:
    case 0x8c:
    case 0x8d:
      CPU = "tigerlake";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_TIGERLAKE;
      break;

    // Alderlake:
    case 0x97:
    case 0x9a:
    // Raptorlake:
    case 0xb7:
    // Meteorlake:
    case 0xaa:
    case 0xac:
      CPU = "alderlake";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_ALDERLAKE;
      break;

    // Icelake Xeon:
    case 0x6a:
    case 0x6c:
      CPU = "icelake-server";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_ICELAKE_SERVER;
      break;

    // Emerald Rapids:
    case 0xcf:
    // Sapphire Rapids:
    case 0x8f:
      CPU = "sapphirerapids";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_SAPPHIRERAPIDS;
      break;

    // Granite Rapids:
    case 0xae:
    case 0xad:
      CPU = "graniterapids";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_GRANITERAPIDS;
      break;

    case 0x1c: // Most 45 nm Intel Atom processors
    case 0x26: // 45 nm Atom Lincroft
    case 0x27: // 32 nm Atom Medfield
    case 0x35: // 32 nm Atom Midview
    case 0x36: // 32 nm Atom Midview
      CPU = "bonnell";
      *Type = INTEL_BONNELL;
      break;

    // Atom Silvermont codes from the Intel software optimization guide.
    case 0x37:
    case 0x4a:
    case 0x4d:
    case 0x5a:
    case 0x5d:
    case 0x4c: // really airmont
      CPU = "silvermont";
      *Type = INTEL_SILVERMONT;
      break;
    // Goldmont:
    case 0x5c: // Apollo Lake
    case 0x5f: // Denverton
      CPU = "goldmont";
      *Type = INTEL_GOLDMONT;
      break; // "goldmont"
    case 0x7a:
      CPU = "goldmont-plus";
      *Type = INTEL_GOLDMONT_PLUS;
      break;
    case 0x86:
      CPU = "tremont";
      *Type = INTEL_TREMONT;
      break;

    // Sierraforest:
    case 0xaf:
      CPU = "sierraforest";
      *Type = INTEL_SIERRAFOREST;
      break;

    // Grandridge:
    case 0xb6:
      CPU = "grandridge";
      *Type = INTEL_GRANDRIDGE;
      break;

    case 0x57:
      CPU = "knl";
      *Type = INTEL_KNL;
      break;

    case 0x85:
      CPU = "knm";
      *Type = INTEL_KNM;
      break;

    default: // Unknown family 6 CPU.
      break;
    }
    break;
  default:
    break; // Unknown.
  }

  return CPU;
}

static const char *
getAMDProcessorTypeAndSubtype(unsigned Family, unsigned Model,
                              const unsigned *Features,
                              unsigned *Type, unsigned *Subtype) {
  // We select CPU strings to match the code in Host.cpp, but we don't use them
  // in compiler-rt.
  const char *CPU = 0;

  switch (Family) {
  case 16:
    CPU = "amdfam10";
    *Type = AMDFAM10H;
    switch (Model) {
    case 2:
      *Subtype = AMDFAM10H_BARCELONA;
      break;
    case 4:
      *Subtype = AMDFAM10H_SHANGHAI;
      break;
    case 8:
      *Subtype = AMDFAM10H_ISTANBUL;
      break;
    }
    break;
  case 20:
    CPU = "btver1";
    *Type = AMD_BTVER1;
    break;
  case 21:
    CPU = "bdver1";
    *Type = AMDFAM15H;
    if (Model >= 0x60 && Model <= 0x7f) {
      CPU = "bdver4";
      *Subtype = AMDFAM15H_BDVER4;
      break; // 60h-7Fh: Excavator
    }
    if (Model >= 0x30 && Model <= 0x3f) {
      CPU = "bdver3";
      *Subtype = AMDFAM15H_BDVER3;
      break; // 30h-3Fh: Steamroller
    }
    if ((Model >= 0x10 && Model <= 0x1f) || Model == 0x02) {
      CPU = "bdver2";
      *Subtype = AMDFAM15H_BDVER2;
      break; // 02h, 10h-1Fh: Piledriver
    }
    if (Model <= 0x0f) {
      *Subtype = AMDFAM15H_BDVER1;
      break; // 00h-0Fh: Bulldozer
    }
    break;
  case 22:
    CPU = "btver2";
    *Type = AMD_BTVER2;
    break;
  case 23:
    CPU = "znver1";
    *Type = AMDFAM17H;
    if ((Model >= 0x30 && Model <= 0x3f) || Model == 0x71) {
      CPU = "znver2";
      *Subtype = AMDFAM17H_ZNVER2;
      break; // 30h-3fh, 71h: Zen2
    }
    if (Model <= 0x0f) {
      *Subtype = AMDFAM17H_ZNVER1;
      break; // 00h-0Fh: Zen1
    }
    break;
  case 25:
    CPU = "znver3";
    *Type = AMDFAM19H;
    if (Model <= 0x0f || (Model >= 0x20 && Model <= 0x5f)) {
      // Family 19h Models 00h-0Fh - Zen3
      // Family 19h Models 20h-2Fh - Zen3
      // Family 19h Models 30h-3Fh - Zen3
      // Family 19h Models 40h-4Fh - Zen3+
      // Family 19h Models 50h-5Fh - Zen3+
      *Subtype = AMDFAM19H_ZNVER3;
      break;
    }
    if ((Model >= 0x10 && Model <= 0x1f) ||
        (Model >= 0x60 && Model <= 0x74) ||
        (Model >= 0x78 && Model <= 0x7b) ||
        (Model >= 0xA0 && Model <= 0xAf)) {
      CPU = "znver4";
      *Subtype = AMDFAM19H_ZNVER4;
      break; //  "znver4"
    }
    break;
  default:
    break; // Unknown AMD CPU.
  }

  return CPU;
}

static void getAvailableFeatures(unsigned ECX, unsigned EDX, unsigned MaxLeaf,
                                 unsigned *Features) {
  unsigned EAX, EBX;

#define setFeature(F)                                                          \
  Features[F / 32] |= 1U << (F % 32)

  if ((EDX >> 15) & 1)
    setFeature(FEATURE_CMOV);
  if ((EDX >> 23) & 1)
    setFeature(FEATURE_MMX);
  if ((EDX >> 25) & 1)
    setFeature(FEATURE_SSE);
  if ((EDX >> 26) & 1)
    setFeature(FEATURE_SSE2);

  if ((ECX >> 0) & 1)
    setFeature(FEATURE_SSE3);
  if ((ECX >> 1) & 1)
    setFeature(FEATURE_PCLMUL);
  if ((ECX >> 9) & 1)
    setFeature(FEATURE_SSSE3);
  if ((ECX >> 12) & 1)
    setFeature(FEATURE_FMA);
  if ((ECX >> 19) & 1)
    setFeature(FEATURE_SSE4_1);
  if ((ECX >> 20) & 1)
    setFeature(FEATURE_SSE4_2);
  if ((ECX >> 23) & 1)
    setFeature(FEATURE_POPCNT);
  if ((ECX >> 25) & 1)
    setFeature(FEATURE_AES);

  // If CPUID indicates support for XSAVE, XRESTORE and AVX, and XGETBV
  // indicates that the AVX registers will be saved and restored on context
  // switch, then we have full AVX support.
  const unsigned AVXBits = (1 << 27) | (1 << 28);
  bool HasAVX = ((ECX & AVXBits) == AVXBits) && !getX86XCR0(&EAX, &EDX) &&
                ((EAX & 0x6) == 0x6);
#if defined(__APPLE__)
  // Darwin lazily saves the AVX512 context on first use: trust that the OS will
  // save the AVX512 context if we use AVX512 instructions, even the bit is not
  // set right now.
  bool HasAVX512Save = true;
#else
  // AVX512 requires additional context to be saved by the OS.
  bool HasAVX512Save = HasAVX && ((EAX & 0xe0) == 0xe0);
#endif

  if (HasAVX)
    setFeature(FEATURE_AVX);

  bool HasLeaf7 =
      MaxLeaf >= 0x7 && !getX86CpuIDAndInfoEx(0x7, 0x0, &EAX, &EBX, &ECX, &EDX);

  if (HasLeaf7 && ((EBX >> 3) & 1))
    setFeature(FEATURE_BMI);
  if (HasLeaf7 && ((EBX >> 5) & 1) && HasAVX)
    setFeature(FEATURE_AVX2);
  if (HasLeaf7 && ((EBX >> 8) & 1))
    setFeature(FEATURE_BMI2);
  if (HasLeaf7 && ((EBX >> 16) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512F);
  if (HasLeaf7 && ((EBX >> 17) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512DQ);
  if (HasLeaf7 && ((EBX >> 21) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512IFMA);
  if (HasLeaf7 && ((EBX >> 26) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512PF);
  if (HasLeaf7 && ((EBX >> 27) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512ER);
  if (HasLeaf7 && ((EBX >> 28) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512CD);
  if (HasLeaf7 && ((EBX >> 30) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512BW);
  if (HasLeaf7 && ((EBX >> 31) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512VL);

  if (HasLeaf7 && ((ECX >> 1) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512VBMI);
  if (HasLeaf7 && ((ECX >> 6) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512VBMI2);
  if (HasLeaf7 && ((ECX >> 8) & 1))
    setFeature(FEATURE_GFNI);
  if (HasLeaf7 && ((ECX >> 10) & 1) && HasAVX)
    setFeature(FEATURE_VPCLMULQDQ);
  if (HasLeaf7 && ((ECX >> 11) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512VNNI);
  if (HasLeaf7 && ((ECX >> 12) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512BITALG);
  if (HasLeaf7 && ((ECX >> 14) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512VPOPCNTDQ);

  if (HasLeaf7 && ((EDX >> 2) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX5124VNNIW);
  if (HasLeaf7 && ((EDX >> 3) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX5124FMAPS);
  if (HasLeaf7 && ((EDX >> 8) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512VP2INTERSECT);

  bool HasLeaf7Subleaf1 =
      MaxLeaf >= 0x7 && !getX86CpuIDAndInfoEx(0x7, 0x1, &EAX, &EBX, &ECX, &EDX);
  if (HasLeaf7Subleaf1 && ((EAX >> 5) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512BF16);

  unsigned MaxExtLevel;
  getX86CpuIDAndInfo(0x80000000, &MaxExtLevel, &EBX, &ECX, &EDX);

  bool HasExtLeaf1 = MaxExtLevel >= 0x80000001 &&
                     !getX86CpuIDAndInfo(0x80000001, &EAX, &EBX, &ECX, &EDX);
  if (HasExtLeaf1 && ((ECX >> 6) & 1))
    setFeature(FEATURE_SSE4_A);
  if (HasExtLeaf1 && ((ECX >> 11) & 1))
    setFeature(FEATURE_XOP);
  if (HasExtLeaf1 && ((ECX >> 16) & 1))
    setFeature(FEATURE_FMA4);
#undef setFeature
}

#ifndef _WIN32
__attribute__((visibility("hidden")))
#endif
int __cpu_indicator_init(void) CONSTRUCTOR_ATTRIBUTE;

#ifndef _WIN32
__attribute__((visibility("hidden")))
#endif
struct __processor_model {
  unsigned int __cpu_vendor;
  unsigned int __cpu_type;
  unsigned int __cpu_subtype;
  unsigned int __cpu_features[1];
} __cpu_model = {0, 0, 0, {0}};

#ifndef _WIN32
__attribute__((visibility("hidden")))
#endif
unsigned int __cpu_features2 = 0;

// A constructor function that is sets __cpu_model and __cpu_features2 with
// the right values.  This needs to run only once.  This constructor is
// given the highest priority and it should run before constructors without
// the priority set.  However, it still runs after ifunc initializers and
// needs to be called explicitly there.

int CONSTRUCTOR_ATTRIBUTE __cpu_indicator_init(void) {
  unsigned EAX, EBX, ECX, EDX;
  unsigned MaxLeaf = 5;
  unsigned Vendor;
  unsigned Model, Family;
  unsigned Features[(CPU_FEATURE_MAX + 31) / 32] = {0};

  // This function needs to run just once.
  if (__cpu_model.__cpu_vendor)
    return 0;

  if (!isCpuIdSupported() ||
      getX86CpuIDAndInfo(0, &MaxLeaf, &Vendor, &ECX, &EDX) || MaxLeaf < 1) {
    __cpu_model.__cpu_vendor = VENDOR_OTHER;
    return -1;
  }

  getX86CpuIDAndInfo(1, &EAX, &EBX, &ECX, &EDX);
  detectX86FamilyModel(EAX, &Family, &Model);

  // Find available features.
  getAvailableFeatures(ECX, EDX, MaxLeaf, &Features[0]);

  assert((sizeof(Features)/sizeof(Features[0])) == 2);
  __cpu_model.__cpu_features[0] = Features[0];
  __cpu_features2 = Features[1];

  if (Vendor == SIG_INTEL) {
    // Get CPU type.
    getIntelProcessorTypeAndSubtype(Family, Model, &Features[0],
                                    &(__cpu_model.__cpu_type),
                                    &(__cpu_model.__cpu_subtype));
    __cpu_model.__cpu_vendor = VENDOR_INTEL;
  } else if (Vendor == SIG_AMD) {
    // Get CPU type.
    getAMDProcessorTypeAndSubtype(Family, Model, &Features[0],
                                  &(__cpu_model.__cpu_type),
                                  &(__cpu_model.__cpu_subtype));
    __cpu_model.__cpu_vendor = VENDOR_AMD;
  } else
    __cpu_model.__cpu_vendor = VENDOR_OTHER;

  assert(__cpu_model.__cpu_vendor < VENDOR_MAX);
  assert(__cpu_model.__cpu_type < CPU_TYPE_MAX);
  assert(__cpu_model.__cpu_subtype < CPU_SUBTYPE_MAX);

  return 0;
}
#elif defined(__aarch64__)

#ifndef AT_HWCAP
#define AT_HWCAP 16
#endif
#ifndef HWCAP_CPUID
#define HWCAP_CPUID (1 << 11)
#endif
#ifndef HWCAP_FP
#define HWCAP_FP (1 << 0)
#endif
#ifndef HWCAP_ASIMD
#define HWCAP_ASIMD (1 << 1)
#endif
#ifndef HWCAP_AES
#define HWCAP_AES (1 << 3)
#endif
#ifndef HWCAP_PMULL
#define HWCAP_PMULL (1 << 4)
#endif
#ifndef HWCAP_SHA1
#define HWCAP_SHA1 (1 << 5)
#endif
#ifndef HWCAP_SHA2
#define HWCAP_SHA2 (1 << 6)
#endif
#ifndef HWCAP_ATOMICS
#define HWCAP_ATOMICS (1 << 8)
#endif
#ifndef HWCAP_FPHP
#define HWCAP_FPHP (1 << 9)
#endif
#ifndef HWCAP_ASIMDHP
#define HWCAP_ASIMDHP (1 << 10)
#endif
#ifndef HWCAP_ASIMDRDM
#define HWCAP_ASIMDRDM (1 << 12)
#endif
#ifndef HWCAP_JSCVT
#define HWCAP_JSCVT (1 << 13)
#endif
#ifndef HWCAP_FCMA
#define HWCAP_FCMA (1 << 14)
#endif
#ifndef HWCAP_LRCPC
#define HWCAP_LRCPC (1 << 15)
#endif
#ifndef HWCAP_DCPOP
#define HWCAP_DCPOP (1 << 16)
#endif
#ifndef HWCAP_SHA3
#define HWCAP_SHA3 (1 << 17)
#endif
#ifndef HWCAP_SM3
#define HWCAP_SM3 (1 << 18)
#endif
#ifndef HWCAP_SM4
#define HWCAP_SM4 (1 << 19)
#endif
#ifndef HWCAP_ASIMDDP
#define HWCAP_ASIMDDP (1 << 20)
#endif
#ifndef HWCAP_SHA512
#define HWCAP_SHA512 (1 << 21)
#endif
#ifndef HWCAP_SVE
#define HWCAP_SVE (1 << 22)
#endif
#ifndef HWCAP_ASIMDFHM
#define HWCAP_ASIMDFHM (1 << 23)
#endif
#ifndef HWCAP_DIT
#define HWCAP_DIT (1 << 24)
#endif
#ifndef HWCAP_ILRCPC
#define HWCAP_ILRCPC (1 << 26)
#endif
#ifndef HWCAP_FLAGM
#define HWCAP_FLAGM (1 << 27)
#endif
#ifndef HWCAP_SSBS
#define HWCAP_SSBS (1 << 28)
#endif
#ifndef HWCAP_SB
#define HWCAP_SB (1 << 29)
#endif

#ifndef AT_HWCAP2
#define AT_HWCAP2 26
#endif
#ifndef HWCAP2_DCPODP
#define HWCAP2_DCPODP (1 << 0)
#endif
#ifndef HWCAP2_SVE2
#define HWCAP2_SVE2 (1 << 1)
#endif
#ifndef HWCAP2_SVEAES
#define HWCAP2_SVEAES (1 << 2)
#endif
#ifndef HWCAP2_SVEPMULL
#define HWCAP2_SVEPMULL (1 << 3)
#endif
#ifndef HWCAP2_SVEBITPERM
#define HWCAP2_SVEBITPERM (1 << 4)
#endif
#ifndef HWCAP2_SVESHA3
#define HWCAP2_SVESHA3 (1 << 5)
#endif
#ifndef HWCAP2_SVESM4
#define HWCAP2_SVESM4 (1 << 6)
#endif
#ifndef HWCAP2_FLAGM2
#define HWCAP2_FLAGM2 (1 << 7)
#endif
#ifndef HWCAP2_FRINT
#define HWCAP2_FRINT (1 << 8)
#endif
#ifndef HWCAP2_SVEI8MM
#define HWCAP2_SVEI8MM (1 << 9)
#endif
#ifndef HWCAP2_SVEF32MM
#define HWCAP2_SVEF32MM (1 << 10)
#endif
#ifndef HWCAP2_SVEF64MM
#define HWCAP2_SVEF64MM (1 << 11)
#endif
#ifndef HWCAP2_SVEBF16
#define HWCAP2_SVEBF16 (1 << 12)
#endif
#ifndef HWCAP2_I8MM
#define HWCAP2_I8MM (1 << 13)
#endif
#ifndef HWCAP2_BF16
#define HWCAP2_BF16 (1 << 14)
#endif
#ifndef HWCAP2_DGH
#define HWCAP2_DGH (1 << 15)
#endif
#ifndef HWCAP2_RNG
#define HWCAP2_RNG (1 << 16)
#endif
#ifndef HWCAP2_BTI
#define HWCAP2_BTI (1 << 17)
#endif
#ifndef HWCAP2_MTE
#define HWCAP2_MTE (1 << 18)
#endif
#ifndef HWCAP2_RPRES
#define HWCAP2_RPRES (1 << 21)
#endif
#ifndef HWCAP2_MTE3
#define HWCAP2_MTE3 (1 << 22)
#endif
#ifndef HWCAP2_SME
#define HWCAP2_SME (1 << 23)
#endif
#ifndef HWCAP2_SME_I16I64
#define HWCAP2_SME_I16I64 (1 << 24)
#endif
#ifndef HWCAP2_SME_F64F64
#define HWCAP2_SME_F64F64 (1 << 25)
#endif
#ifndef HWCAP2_WFXT
#define HWCAP2_WFXT (1UL << 31)
#endif
#ifndef HWCAP2_EBF16
#define HWCAP2_EBF16 (1UL << 32)
#endif
#ifndef HWCAP2_SVE_EBF16
#define HWCAP2_SVE_EBF16 (1UL << 33)
#endif

// LSE support detection for out-of-line atomics
// using HWCAP and Auxiliary vector
_Bool __aarch64_have_lse_atomics
    __attribute__((visibility("hidden"), nocommon));

#if defined(__has_include)
#if __has_include(<sys/auxv.h>)
#include <sys/auxv.h>
#if __has_include(<asm/hwcap.h>)
#include <asm/hwcap.h>

#if defined(__ANDROID__)
#include <string.h>
#include <sys/system_properties.h>
#elif defined(__Fuchsia__)
#include <zircon/features.h>
#include <zircon/syscalls.h>
#endif

// Detect Exynos 9810 CPU
#define IF_EXYNOS9810                                                          \
  char arch[PROP_VALUE_MAX];                                                   \
  if (__system_property_get("ro.arch", arch) > 0 &&                            \
      strncmp(arch, "exynos9810", sizeof("exynos9810") - 1) == 0)

static void CONSTRUCTOR_ATTRIBUTE init_have_lse_atomics(void) {
#if defined(__FreeBSD__)
  unsigned long hwcap;
  int result = elf_aux_info(AT_HWCAP, &hwcap, sizeof hwcap);
  __aarch64_have_lse_atomics = result == 0 && (hwcap & HWCAP_ATOMICS) != 0;
#elif defined(__Fuchsia__)
  // This ensures the vDSO is a direct link-time dependency of anything that
  // needs this initializer code.
#pragma comment(lib, "zircon")
  uint32_t features;
  zx_status_t status = _zx_system_get_features(ZX_FEATURE_KIND_CPU, &features);
  __aarch64_have_lse_atomics =
      status == ZX_OK && (features & ZX_ARM64_FEATURE_ISA_ATOMICS) != 0;
#else
  unsigned long hwcap = getauxval(AT_HWCAP);
  _Bool result = (hwcap & HWCAP_ATOMICS) != 0;
#if defined(__ANDROID__)
  if (result) {
    // Some cores in the Exynos 9810 CPU are ARMv8.2 and others are ARMv8.0;
    // only the former support LSE atomics.  However, the kernel in the
    // initial Android 8.0 release of Galaxy S9/S9+ devices incorrectly
    // reported the feature as being supported.
    //
    // The kernel appears to have been corrected to mark it unsupported as of
    // the Android 9.0 release on those devices, and this issue has not been
    // observed anywhere else. Thus, this workaround may be removed if
    // compiler-rt ever drops support for Android 8.0.
    IF_EXYNOS9810 result = false;
  }
#endif // defined(__ANDROID__)
  __aarch64_have_lse_atomics = result;
#endif // defined(__FreeBSD__)
}

#if !defined(DISABLE_AARCH64_FMV)
// CPUFeatures must correspond to the same AArch64 features in
// AArch64TargetParser.h
enum CPUFeatures {
  FEAT_RNG,
  FEAT_FLAGM,
  FEAT_FLAGM2,
  FEAT_FP16FML,
  FEAT_DOTPROD,
  FEAT_SM4,
  FEAT_RDM,
  FEAT_LSE,
  FEAT_FP,
  FEAT_SIMD,
  FEAT_CRC,
  FEAT_SHA1,
  FEAT_SHA2,
  FEAT_SHA3,
  FEAT_AES,
  FEAT_PMULL,
  FEAT_FP16,
  FEAT_DIT,
  FEAT_DPB,
  FEAT_DPB2,
  FEAT_JSCVT,
  FEAT_FCMA,
  FEAT_RCPC,
  FEAT_RCPC2,
  FEAT_FRINTTS,
  FEAT_DGH,
  FEAT_I8MM,
  FEAT_BF16,
  FEAT_EBF16,
  FEAT_RPRES,
  FEAT_SVE,
  FEAT_SVE_BF16,
  FEAT_SVE_EBF16,
  FEAT_SVE_I8MM,
  FEAT_SVE_F32MM,
  FEAT_SVE_F64MM,
  FEAT_SVE2,
  FEAT_SVE_AES,
  FEAT_SVE_PMULL128,
  FEAT_SVE_BITPERM,
  FEAT_SVE_SHA3,
  FEAT_SVE_SM4,
  FEAT_SME,
  FEAT_MEMTAG,
  FEAT_MEMTAG2,
  FEAT_MEMTAG3,
  FEAT_SB,
  FEAT_PREDRES,
  FEAT_SSBS,
  FEAT_SSBS2,
  FEAT_BTI,
  FEAT_LS64,
  FEAT_LS64_V,
  FEAT_LS64_ACCDATA,
  FEAT_WFXT,
  FEAT_SME_F64,
  FEAT_SME_I64,
  FEAT_SME2,
  FEAT_MAX
};

// Architecture features used
// in Function Multi Versioning
struct {
  unsigned long long features;
  // As features grows new fields could be added
} __aarch64_cpu_features __attribute__((visibility("hidden"), nocommon));

void init_cpu_features_resolver(unsigned long hwcap, unsigned long hwcap2) {
#define setCPUFeature(F) __aarch64_cpu_features.features |= 1ULL << F
#define getCPUFeature(id, ftr) __asm__("mrs %0, " #id : "=r"(ftr))
#define extractBits(val, start, number)                                        \
  (val & ((1ULL << number) - 1ULL) << start) >> start
  if (hwcap & HWCAP_CRC32)
    setCPUFeature(FEAT_CRC);
  if (hwcap & HWCAP_PMULL)
    setCPUFeature(FEAT_PMULL);
  if (hwcap & HWCAP_FLAGM)
    setCPUFeature(FEAT_FLAGM);
  if (hwcap2 & HWCAP2_FLAGM2) {
    setCPUFeature(FEAT_FLAGM);
    setCPUFeature(FEAT_FLAGM2);
  }
  if (hwcap & HWCAP_SM3 && hwcap & HWCAP_SM4)
    setCPUFeature(FEAT_SM4);
  if (hwcap & HWCAP_ASIMDDP)
    setCPUFeature(FEAT_DOTPROD);
  if (hwcap & HWCAP_ASIMDFHM)
    setCPUFeature(FEAT_FP16FML);
  if (hwcap & HWCAP_FPHP) {
    setCPUFeature(FEAT_FP16);
    setCPUFeature(FEAT_FP);
  }
  if (hwcap & HWCAP_DIT)
    setCPUFeature(FEAT_DIT);
  if (hwcap & HWCAP_ASIMDRDM)
    setCPUFeature(FEAT_RDM);
  if (hwcap & HWCAP_ILRCPC)
    setCPUFeature(FEAT_RCPC2);
  if (hwcap & HWCAP_AES)
    setCPUFeature(FEAT_AES);
  if (hwcap & HWCAP_SHA1)
    setCPUFeature(FEAT_SHA1);
  if (hwcap & HWCAP_SHA2)
    setCPUFeature(FEAT_SHA2);
  if (hwcap & HWCAP_JSCVT)
    setCPUFeature(FEAT_JSCVT);
  if (hwcap & HWCAP_FCMA)
    setCPUFeature(FEAT_FCMA);
  if (hwcap & HWCAP_SB)
    setCPUFeature(FEAT_SB);
  if (hwcap & HWCAP_SSBS)
    setCPUFeature(FEAT_SSBS2);
  if (hwcap2 & HWCAP2_MTE) {
    setCPUFeature(FEAT_MEMTAG);
    setCPUFeature(FEAT_MEMTAG2);
  }
  if (hwcap2 & HWCAP2_MTE3) {
    setCPUFeature(FEAT_MEMTAG);
    setCPUFeature(FEAT_MEMTAG2);
    setCPUFeature(FEAT_MEMTAG3);
  }
  if (hwcap2 & HWCAP2_SVEAES)
    setCPUFeature(FEAT_SVE_AES);
  if (hwcap2 & HWCAP2_SVEPMULL) {
    setCPUFeature(FEAT_SVE_AES);
    setCPUFeature(FEAT_SVE_PMULL128);
  }
  if (hwcap2 & HWCAP2_SVEBITPERM)
    setCPUFeature(FEAT_SVE_BITPERM);
  if (hwcap2 & HWCAP2_SVESHA3)
    setCPUFeature(FEAT_SVE_SHA3);
  if (hwcap2 & HWCAP2_SVESM4)
    setCPUFeature(FEAT_SVE_SM4);
  if (hwcap2 & HWCAP2_DCPODP)
    setCPUFeature(FEAT_DPB2);
  if (hwcap & HWCAP_ATOMICS)
    setCPUFeature(FEAT_LSE);
  if (hwcap2 & HWCAP2_RNG)
    setCPUFeature(FEAT_RNG);
  if (hwcap2 & HWCAP2_I8MM)
    setCPUFeature(FEAT_I8MM);
  if (hwcap2 & HWCAP2_EBF16)
    setCPUFeature(FEAT_EBF16);
  if (hwcap2 & HWCAP2_SVE_EBF16)
    setCPUFeature(FEAT_SVE_EBF16);
  if (hwcap2 & HWCAP2_DGH)
    setCPUFeature(FEAT_DGH);
  if (hwcap2 & HWCAP2_FRINT)
    setCPUFeature(FEAT_FRINTTS);
  if (hwcap2 & HWCAP2_SVEI8MM)
    setCPUFeature(FEAT_SVE_I8MM);
  if (hwcap2 & HWCAP2_SVEF32MM)
    setCPUFeature(FEAT_SVE_F32MM);
  if (hwcap2 & HWCAP2_SVEF64MM)
    setCPUFeature(FEAT_SVE_F64MM);
  if (hwcap2 & HWCAP2_BTI)
    setCPUFeature(FEAT_BTI);
  if (hwcap2 & HWCAP2_RPRES)
    setCPUFeature(FEAT_RPRES);
  if (hwcap2 & HWCAP2_WFXT)
    setCPUFeature(FEAT_WFXT);
  if (hwcap2 & HWCAP2_SME)
    setCPUFeature(FEAT_SME);
  if (hwcap2 & HWCAP2_SME_I16I64)
    setCPUFeature(FEAT_SME_I64);
  if (hwcap2 & HWCAP2_SME_F64F64)
    setCPUFeature(FEAT_SME_F64);
  if (hwcap & HWCAP_CPUID) {
    unsigned long ftr;
    getCPUFeature(ID_AA64PFR1_EL1, ftr);
    // ID_AA64PFR1_EL1.MTE >= 0b0001
    if (extractBits(ftr, 8, 4) >= 0x1)
      setCPUFeature(FEAT_MEMTAG);
    // ID_AA64PFR1_EL1.SSBS == 0b0001
    if (extractBits(ftr, 4, 4) == 0x1)
      setCPUFeature(FEAT_SSBS);
    // ID_AA64PFR1_EL1.SME == 0b0010
    if (extractBits(ftr, 24, 4) == 0x2)
      setCPUFeature(FEAT_SME2);
    getCPUFeature(ID_AA64PFR0_EL1, ftr);
    // ID_AA64PFR0_EL1.FP != 0b1111
    if (extractBits(ftr, 16, 4) != 0xF) {
      setCPUFeature(FEAT_FP);
      // ID_AA64PFR0_EL1.AdvSIMD has the same value as ID_AA64PFR0_EL1.FP
      setCPUFeature(FEAT_SIMD);
    }
    // ID_AA64PFR0_EL1.SVE != 0b0000
    if (extractBits(ftr, 32, 4) != 0x0) {
      // get ID_AA64ZFR0_EL1, that name supported
      // if sve enabled only
      getCPUFeature(S3_0_C0_C4_4, ftr);
      // ID_AA64ZFR0_EL1.SVEver == 0b0000
      if (extractBits(ftr, 0, 4) == 0x0)
        setCPUFeature(FEAT_SVE);
      // ID_AA64ZFR0_EL1.SVEver == 0b0001
      if (extractBits(ftr, 0, 4) == 0x1)
        setCPUFeature(FEAT_SVE2);
      // ID_AA64ZFR0_EL1.BF16 != 0b0000
      if (extractBits(ftr, 20, 4) != 0x0)
        setCPUFeature(FEAT_SVE_BF16);
    }
    getCPUFeature(ID_AA64ISAR0_EL1, ftr);
    // ID_AA64ISAR0_EL1.SHA3 != 0b0000
    if (extractBits(ftr, 32, 4) != 0x0)
      setCPUFeature(FEAT_SHA3);
    getCPUFeature(ID_AA64ISAR1_EL1, ftr);
    // ID_AA64ISAR1_EL1.DPB >= 0b0001
    if (extractBits(ftr, 0, 4) >= 0x1)
      setCPUFeature(FEAT_DPB);
    // ID_AA64ISAR1_EL1.LRCPC != 0b0000
    if (extractBits(ftr, 20, 4) != 0x0)
      setCPUFeature(FEAT_RCPC);
    // ID_AA64ISAR1_EL1.SPECRES == 0b0001
    if (extractBits(ftr, 40, 4) == 0x2)
      setCPUFeature(FEAT_PREDRES);
    // ID_AA64ISAR1_EL1.BF16 != 0b0000
    if (extractBits(ftr, 44, 4) != 0x0)
      setCPUFeature(FEAT_BF16);
    // ID_AA64ISAR1_EL1.LS64 >= 0b0001
    if (extractBits(ftr, 60, 4) >= 0x1)
      setCPUFeature(FEAT_LS64);
    // ID_AA64ISAR1_EL1.LS64 >= 0b0010
    if (extractBits(ftr, 60, 4) >= 0x2)
      setCPUFeature(FEAT_LS64_V);
    // ID_AA64ISAR1_EL1.LS64 >= 0b0011
    if (extractBits(ftr, 60, 4) >= 0x3)
      setCPUFeature(FEAT_LS64_ACCDATA);
  } else {
    // Set some features in case of no CPUID support
    if (hwcap & (HWCAP_FP | HWCAP_FPHP)) {
      setCPUFeature(FEAT_FP);
      // FP and AdvSIMD fields have the same value
      setCPUFeature(FEAT_SIMD);
    }
    if (hwcap & HWCAP_DCPOP || hwcap2 & HWCAP2_DCPODP)
      setCPUFeature(FEAT_DPB);
    if (hwcap & HWCAP_LRCPC || hwcap & HWCAP_ILRCPC)
      setCPUFeature(FEAT_RCPC);
    if (hwcap2 & HWCAP2_BF16 || hwcap2 & HWCAP2_EBF16)
      setCPUFeature(FEAT_BF16);
    if (hwcap2 & HWCAP2_SVEBF16)
      setCPUFeature(FEAT_SVE_BF16);
    if (hwcap2 & HWCAP2_SVE2 && hwcap & HWCAP_SVE)
      setCPUFeature(FEAT_SVE2);
    if (hwcap & HWCAP_SHA3)
      setCPUFeature(FEAT_SHA3);
  }
}

void CONSTRUCTOR_ATTRIBUTE init_cpu_features(void) {
  unsigned long hwcap;
  unsigned long hwcap2;
  // CPU features already initialized.
  if (__aarch64_cpu_features.features)
    return;
  setCPUFeature(FEAT_MAX);
#if defined(__FreeBSD__)
  int res = 0;
  res = elf_aux_info(AT_HWCAP, &hwcap, sizeof hwcap);
  res |= elf_aux_info(AT_HWCAP2, &hwcap2, sizeof hwcap2);
  if (res)
    return;
#else
#if defined(__ANDROID__)
  // Don't set any CPU features,
  // detection could be wrong on Exynos 9810.
  IF_EXYNOS9810 return;
#endif // defined(__ANDROID__)
  hwcap = getauxval(AT_HWCAP);
  hwcap2 = getauxval(AT_HWCAP2);
#endif // defined(__FreeBSD__)
  init_cpu_features_resolver(hwcap, hwcap2);
#undef extractBits
#undef getCPUFeature
#undef setCPUFeature
#undef IF_EXYNOS9810
}
#endif // !defined(DISABLE_AARCH64_FMV)
#endif // defined(__has_include)
#endif // __has_include(<sys/auxv.h>)
#endif // __has_include(<asm/hwcap.h>)
#endif // defined(__aarch64__)
