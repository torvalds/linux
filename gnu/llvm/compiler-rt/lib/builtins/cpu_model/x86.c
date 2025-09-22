//===-- cpu_model/x86.c - Support for __cpu_model builtin  --------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file is based on LLVM's lib/Support/Host.cpp.
//  It implements the operating system Host concept and builtin
//  __cpu_model for the compiler_rt library for x86.
//
//===----------------------------------------------------------------------===//

#include "cpu_model.h"

#if !(defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) ||          \
      defined(_M_X64))
#error This file is intended only for x86-based targets
#endif

#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)

#include <assert.h>

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
  INTEL_CLEARWATERFOREST,
  AMDFAM1AH,
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
  INTEL_COREI7_GRANITERAPIDS_D,
  INTEL_COREI7_ARROWLAKE,
  INTEL_COREI7_ARROWLAKE_S,
  INTEL_COREI7_PANTHERLAKE,
  AMDFAM1AH_ZNVER5,
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
  // FIXME: Below Features has some missings comparing to gcc, it's because gcc
  // has some not one-to-one mapped in llvm.
  // FEATURE_3DNOW,
  // FEATURE_3DNOWP,
  FEATURE_ADX = 40,
  // FEATURE_ABM,
  FEATURE_CLDEMOTE = 42,
  FEATURE_CLFLUSHOPT,
  FEATURE_CLWB,
  FEATURE_CLZERO,
  FEATURE_CMPXCHG16B,
  // FIXME: Not adding FEATURE_CMPXCHG8B is a workaround to make 'generic' as
  // a cpu string with no X86_FEATURE_COMPAT features, which is required in
  // current implementantion of cpu_specific/cpu_dispatch FMV feature.
  // FEATURE_CMPXCHG8B,
  FEATURE_ENQCMD = 48,
  FEATURE_F16C,
  FEATURE_FSGSBASE,
  // FEATURE_FXSAVE,
  // FEATURE_HLE,
  // FEATURE_IBT,
  FEATURE_LAHF_LM = 54,
  FEATURE_LM,
  FEATURE_LWP,
  FEATURE_LZCNT,
  FEATURE_MOVBE,
  FEATURE_MOVDIR64B,
  FEATURE_MOVDIRI,
  FEATURE_MWAITX,
  // FEATURE_OSXSAVE,
  FEATURE_PCONFIG = 63,
  FEATURE_PKU,
  FEATURE_PREFETCHWT1,
  FEATURE_PRFCHW,
  FEATURE_PTWRITE,
  FEATURE_RDPID,
  FEATURE_RDRND,
  FEATURE_RDSEED,
  FEATURE_RTM,
  FEATURE_SERIALIZE,
  FEATURE_SGX,
  FEATURE_SHA,
  FEATURE_SHSTK,
  FEATURE_TBM,
  FEATURE_TSXLDTRK,
  FEATURE_VAES,
  FEATURE_WAITPKG,
  FEATURE_WBNOINVD,
  FEATURE_XSAVE,
  FEATURE_XSAVEC,
  FEATURE_XSAVEOPT,
  FEATURE_XSAVES,
  FEATURE_AMX_TILE,
  FEATURE_AMX_INT8,
  FEATURE_AMX_BF16,
  FEATURE_UINTR,
  FEATURE_HRESET,
  FEATURE_KL,
  // FEATURE_AESKLE,
  FEATURE_WIDEKL = 92,
  FEATURE_AVXVNNI,
  FEATURE_AVX512FP16,
  FEATURE_X86_64_BASELINE,
  FEATURE_X86_64_V2,
  FEATURE_X86_64_V3,
  FEATURE_X86_64_V4,
  FEATURE_AVXIFMA,
  FEATURE_AVXVNNIINT8,
  FEATURE_AVXNECONVERT,
  FEATURE_CMPCCXADD,
  FEATURE_AMX_FP16,
  FEATURE_PREFETCHI,
  FEATURE_RAOINT,
  FEATURE_AMX_COMPLEX,
  FEATURE_AVXVNNIINT16,
  FEATURE_SM3,
  FEATURE_SHA512,
  FEATURE_SM4,
  FEATURE_APXF,
  FEATURE_USERMSR,
  FEATURE_AVX10_1_256,
  FEATURE_AVX10_1_512,
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

#define testFeature(F) (Features[F / 32] & (1 << (F % 32))) != 0

static const char *getIntelProcessorTypeAndSubtype(unsigned Family,
                                                   unsigned Model,
                                                   const unsigned *Features,
                                                   unsigned *Type,
                                                   unsigned *Subtype) {
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
    case 0x2e: // Nehalem EX
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
    case 0x3e: // Ivy Bridge EP
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
    case 0x4e: // Skylake mobile
    case 0x5e: // Skylake desktop
    case 0x8e: // Kaby Lake mobile
    case 0x9e: // Kaby Lake desktop
    case 0xa5: // Comet Lake-H/S
    case 0xa6: // Comet Lake-U
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
    case 0xba:
    case 0xbf:
    // Meteorlake:
    case 0xaa:
    case 0xac:
    // Gracemont:
    case 0xbe:
      CPU = "alderlake";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_ALDERLAKE;
      break;

    // Arrowlake:
    case 0xc5:
      CPU = "arrowlake";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_ARROWLAKE;
      break;

    // Arrowlake S:
    case 0xc6:
    // Lunarlake:
    case 0xbd:
      CPU = "arrowlake-s";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_ARROWLAKE_S;
      break;

    // Pantherlake:
    case 0xcc:
      CPU = "pantherlake";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_PANTHERLAKE;
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
    case 0xad:
      CPU = "graniterapids";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_GRANITERAPIDS;
      break;

    // Granite Rapids D:
    case 0xae:
      CPU = "graniterapids-d";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_COREI7_GRANITERAPIDS_D;
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
    case 0x8a: // Lakefield
    case 0x96: // Elkhart Lake
    case 0x9c: // Jasper Lake
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

    // Clearwaterforest:
    case 0xdd:
      CPU = "clearwaterforest";
      *Type = INTEL_COREI7;
      *Subtype = INTEL_CLEARWATERFOREST;
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

static const char *getAMDProcessorTypeAndSubtype(unsigned Family,
                                                 unsigned Model,
                                                 const unsigned *Features,
                                                 unsigned *Type,
                                                 unsigned *Subtype) {
  const char *CPU = 0;

  switch (Family) {
  case 4:
    CPU = "i486";
    break;
  case 5:
    CPU = "pentium";
    switch (Model) {
    case 6:
    case 7:
      CPU = "k6";
      break;
    case 8:
      CPU = "k6-2";
      break;
    case 9:
    case 13:
      CPU = "k6-3";
      break;
    case 10:
      CPU = "geode";
      break;
    }
    break;
  case 6:
    if (testFeature(FEATURE_SSE)) {
      CPU = "athlon-xp";
      break;
    }
    CPU = "athlon";
    break;
  case 15:
    if (testFeature(FEATURE_SSE3)) {
      CPU = "k8-sse3";
      break;
    }
    CPU = "k8";
    break;
  case 16:
    CPU = "amdfam10";
    *Type = AMDFAM10H; // "amdfam10"
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
    if ((Model >= 0x30 && Model <= 0x3f) || (Model == 0x47) ||
        (Model >= 0x60 && Model <= 0x67) || (Model >= 0x68 && Model <= 0x6f) ||
        (Model >= 0x70 && Model <= 0x7f) || (Model >= 0x84 && Model <= 0x87) ||
        (Model >= 0x90 && Model <= 0x97) || (Model >= 0x98 && Model <= 0x9f) ||
        (Model >= 0xa0 && Model <= 0xaf)) {
      // Family 17h Models 30h-3Fh (Starship) Zen 2
      // Family 17h Models 47h (Cardinal) Zen 2
      // Family 17h Models 60h-67h (Renoir) Zen 2
      // Family 17h Models 68h-6Fh (Lucienne) Zen 2
      // Family 17h Models 70h-7Fh (Matisse) Zen 2
      // Family 17h Models 84h-87h (ProjectX) Zen 2
      // Family 17h Models 90h-97h (VanGogh) Zen 2
      // Family 17h Models 98h-9Fh (Mero) Zen 2
      // Family 17h Models A0h-AFh (Mendocino) Zen 2
      CPU = "znver2";
      *Subtype = AMDFAM17H_ZNVER2;
      break;
    }
    if ((Model >= 0x10 && Model <= 0x1f) || (Model >= 0x20 && Model <= 0x2f)) {
      // Family 17h Models 10h-1Fh (Raven1) Zen
      // Family 17h Models 10h-1Fh (Picasso) Zen+
      // Family 17h Models 20h-2Fh (Raven2 x86) Zen
      *Subtype = AMDFAM17H_ZNVER1;
      break;
    }
    break;
  case 25:
    CPU = "znver3";
    *Type = AMDFAM19H;
    if (Model <= 0x0f || (Model >= 0x20 && Model <= 0x2f) ||
        (Model >= 0x30 && Model <= 0x3f) || (Model >= 0x40 && Model <= 0x4f) ||
        (Model >= 0x50 && Model <= 0x5f)) {
      // Family 19h Models 00h-0Fh (Genesis, Chagall) Zen 3
      // Family 19h Models 20h-2Fh (Vermeer) Zen 3
      // Family 19h Models 30h-3Fh (Badami) Zen 3
      // Family 19h Models 40h-4Fh (Rembrandt) Zen 3+
      // Family 19h Models 50h-5Fh (Cezanne) Zen 3
      *Subtype = AMDFAM19H_ZNVER3;
      break;
    }
    if ((Model >= 0x10 && Model <= 0x1f) || (Model >= 0x60 && Model <= 0x6f) ||
        (Model >= 0x70 && Model <= 0x77) || (Model >= 0x78 && Model <= 0x7f) ||
        (Model >= 0xa0 && Model <= 0xaf)) {
      // Family 19h Models 10h-1Fh (Stones; Storm Peak) Zen 4
      // Family 19h Models 60h-6Fh (Raphael) Zen 4
      // Family 19h Models 70h-77h (Phoenix, Hawkpoint1) Zen 4
      // Family 19h Models 78h-7Fh (Phoenix 2, Hawkpoint2) Zen 4
      // Family 19h Models A0h-AFh (Stones-Dense) Zen 4
      CPU = "znver4";
      *Subtype = AMDFAM19H_ZNVER4;
      break; //  "znver4"
    }
    break; // family 19h
  case 26:
    CPU = "znver5";
    *Type = AMDFAM1AH;
    if (Model <= 0x77) {
      // Models 00h-0Fh (Breithorn).
      // Models 10h-1Fh (Breithorn-Dense).
      // Models 20h-2Fh (Strix 1).
      // Models 30h-37h (Strix 2).
      // Models 38h-3Fh (Strix 3).
      // Models 40h-4Fh (Granite Ridge).
      // Models 50h-5Fh (Weisshorn).
      // Models 60h-6Fh (Krackan1).
      // Models 70h-77h (Sarlak).
      CPU = "znver5";
      *Subtype = AMDFAM1AH_ZNVER5;
      break; //  "znver5"
    }
    break;
  default:
    break; // Unknown AMD CPU.
  }

  return CPU;
}

#undef testFeature

static void getAvailableFeatures(unsigned ECX, unsigned EDX, unsigned MaxLeaf,
                                 unsigned *Features) {
  unsigned EAX = 0, EBX = 0;

#define hasFeature(F) ((Features[F / 32] >> (F % 32)) & 1)
#define setFeature(F) Features[F / 32] |= 1U << (F % 32)

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
  if ((ECX >> 13) & 1)
    setFeature(FEATURE_CMPXCHG16B);
  if ((ECX >> 19) & 1)
    setFeature(FEATURE_SSE4_1);
  if ((ECX >> 20) & 1)
    setFeature(FEATURE_SSE4_2);
  if ((ECX >> 22) & 1)
    setFeature(FEATURE_MOVBE);
  if ((ECX >> 23) & 1)
    setFeature(FEATURE_POPCNT);
  if ((ECX >> 25) & 1)
    setFeature(FEATURE_AES);
  if ((ECX >> 29) & 1)
    setFeature(FEATURE_F16C);
  if ((ECX >> 30) & 1)
    setFeature(FEATURE_RDRND);

  // If CPUID indicates support for XSAVE, XRESTORE and AVX, and XGETBV
  // indicates that the AVX registers will be saved and restored on context
  // switch, then we have full AVX support.
  const unsigned AVXBits = (1 << 27) | (1 << 28);
  bool HasAVXSave = ((ECX & AVXBits) == AVXBits) && !getX86XCR0(&EAX, &EDX) &&
                    ((EAX & 0x6) == 0x6);
#if defined(__APPLE__)
  // Darwin lazily saves the AVX512 context on first use: trust that the OS will
  // save the AVX512 context if we use AVX512 instructions, even the bit is not
  // set right now.
  bool HasAVX512Save = true;
#else
  // AVX512 requires additional context to be saved by the OS.
  bool HasAVX512Save = HasAVXSave && ((EAX & 0xe0) == 0xe0);
#endif
  // AMX requires additional context to be saved by the OS.
  const unsigned AMXBits = (1 << 17) | (1 << 18);
  bool HasXSave = ((ECX >> 27) & 1) && !getX86XCR0(&EAX, &EDX);
  bool HasAMXSave = HasXSave && ((EAX & AMXBits) == AMXBits);

  if (HasAVXSave)
    setFeature(FEATURE_AVX);

  if (((ECX >> 26) & 1) && HasAVXSave)
    setFeature(FEATURE_XSAVE);

  bool HasLeaf7 =
      MaxLeaf >= 0x7 && !getX86CpuIDAndInfoEx(0x7, 0x0, &EAX, &EBX, &ECX, &EDX);

  if (HasLeaf7 && ((EBX >> 0) & 1))
    setFeature(FEATURE_FSGSBASE);
  if (HasLeaf7 && ((EBX >> 2) & 1))
    setFeature(FEATURE_SGX);
  if (HasLeaf7 && ((EBX >> 3) & 1))
    setFeature(FEATURE_BMI);
  if (HasLeaf7 && ((EBX >> 5) & 1) && HasAVXSave)
    setFeature(FEATURE_AVX2);
  if (HasLeaf7 && ((EBX >> 8) & 1))
    setFeature(FEATURE_BMI2);
  if (HasLeaf7 && ((EBX >> 11) & 1))
    setFeature(FEATURE_RTM);
  if (HasLeaf7 && ((EBX >> 16) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512F);
  if (HasLeaf7 && ((EBX >> 17) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512DQ);
  if (HasLeaf7 && ((EBX >> 18) & 1))
    setFeature(FEATURE_RDSEED);
  if (HasLeaf7 && ((EBX >> 19) & 1))
    setFeature(FEATURE_ADX);
  if (HasLeaf7 && ((EBX >> 21) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512IFMA);
  if (HasLeaf7 && ((EBX >> 24) & 1))
    setFeature(FEATURE_CLWB);
  if (HasLeaf7 && ((EBX >> 26) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512PF);
  if (HasLeaf7 && ((EBX >> 27) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512ER);
  if (HasLeaf7 && ((EBX >> 28) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512CD);
  if (HasLeaf7 && ((EBX >> 29) & 1))
    setFeature(FEATURE_SHA);
  if (HasLeaf7 && ((EBX >> 30) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512BW);
  if (HasLeaf7 && ((EBX >> 31) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512VL);

  if (HasLeaf7 && ((ECX >> 0) & 1))
    setFeature(FEATURE_PREFETCHWT1);
  if (HasLeaf7 && ((ECX >> 1) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512VBMI);
  if (HasLeaf7 && ((ECX >> 4) & 1))
    setFeature(FEATURE_PKU);
  if (HasLeaf7 && ((ECX >> 5) & 1))
    setFeature(FEATURE_WAITPKG);
  if (HasLeaf7 && ((ECX >> 6) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512VBMI2);
  if (HasLeaf7 && ((ECX >> 7) & 1))
    setFeature(FEATURE_SHSTK);
  if (HasLeaf7 && ((ECX >> 8) & 1))
    setFeature(FEATURE_GFNI);
  if (HasLeaf7 && ((ECX >> 9) & 1) && HasAVXSave)
    setFeature(FEATURE_VAES);
  if (HasLeaf7 && ((ECX >> 10) & 1) && HasAVXSave)
    setFeature(FEATURE_VPCLMULQDQ);
  if (HasLeaf7 && ((ECX >> 11) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512VNNI);
  if (HasLeaf7 && ((ECX >> 12) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512BITALG);
  if (HasLeaf7 && ((ECX >> 14) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512VPOPCNTDQ);
  if (HasLeaf7 && ((ECX >> 22) & 1))
    setFeature(FEATURE_RDPID);
  if (HasLeaf7 && ((ECX >> 23) & 1))
    setFeature(FEATURE_KL);
  if (HasLeaf7 && ((ECX >> 25) & 1))
    setFeature(FEATURE_CLDEMOTE);
  if (HasLeaf7 && ((ECX >> 27) & 1))
    setFeature(FEATURE_MOVDIRI);
  if (HasLeaf7 && ((ECX >> 28) & 1))
    setFeature(FEATURE_MOVDIR64B);
  if (HasLeaf7 && ((ECX >> 29) & 1))
    setFeature(FEATURE_ENQCMD);

  if (HasLeaf7 && ((EDX >> 2) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX5124VNNIW);
  if (HasLeaf7 && ((EDX >> 3) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX5124FMAPS);
  if (HasLeaf7 && ((EDX >> 5) & 1))
    setFeature(FEATURE_UINTR);
  if (HasLeaf7 && ((EDX >> 8) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512VP2INTERSECT);
  if (HasLeaf7 && ((EDX >> 14) & 1))
    setFeature(FEATURE_SERIALIZE);
  if (HasLeaf7 && ((EDX >> 16) & 1))
    setFeature(FEATURE_TSXLDTRK);
  if (HasLeaf7 && ((EDX >> 18) & 1))
    setFeature(FEATURE_PCONFIG);
  if (HasLeaf7 && ((EDX >> 22) & 1) && HasAMXSave)
    setFeature(FEATURE_AMX_BF16);
  if (HasLeaf7 && ((EDX >> 23) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512FP16);
  if (HasLeaf7 && ((EDX >> 24) & 1) && HasAMXSave)
    setFeature(FEATURE_AMX_TILE);
  if (HasLeaf7 && ((EDX >> 25) & 1) && HasAMXSave)
    setFeature(FEATURE_AMX_INT8);

  // EAX from subleaf 0 is the maximum subleaf supported. Some CPUs don't
  // return all 0s for invalid subleaves so check the limit.
  bool HasLeaf7Subleaf1 =
      HasLeaf7 && EAX >= 1 &&
      !getX86CpuIDAndInfoEx(0x7, 0x1, &EAX, &EBX, &ECX, &EDX);
  if (HasLeaf7Subleaf1 && ((EAX >> 0) & 1))
    setFeature(FEATURE_SHA512);
  if (HasLeaf7Subleaf1 && ((EAX >> 1) & 1))
    setFeature(FEATURE_SM3);
  if (HasLeaf7Subleaf1 && ((EAX >> 2) & 1))
    setFeature(FEATURE_SM4);
  if (HasLeaf7Subleaf1 && ((EAX >> 3) & 1))
    setFeature(FEATURE_RAOINT);
  if (HasLeaf7Subleaf1 && ((EAX >> 4) & 1) && HasAVXSave)
    setFeature(FEATURE_AVXVNNI);
  if (HasLeaf7Subleaf1 && ((EAX >> 5) & 1) && HasAVX512Save)
    setFeature(FEATURE_AVX512BF16);
  if (HasLeaf7Subleaf1 && ((EAX >> 7) & 1))
    setFeature(FEATURE_CMPCCXADD);
  if (HasLeaf7Subleaf1 && ((EAX >> 21) & 1) && HasAMXSave)
    setFeature(FEATURE_AMX_FP16);
  if (HasLeaf7Subleaf1 && ((EAX >> 22) & 1))
    setFeature(FEATURE_HRESET);
  if (HasLeaf7Subleaf1 && ((EAX >> 23) & 1) && HasAVXSave)
    setFeature(FEATURE_AVXIFMA);

  if (HasLeaf7Subleaf1 && ((EDX >> 4) & 1) && HasAVXSave)
    setFeature(FEATURE_AVXVNNIINT8);
  if (HasLeaf7Subleaf1 && ((EDX >> 5) & 1) && HasAVXSave)
    setFeature(FEATURE_AVXNECONVERT);
  if (HasLeaf7Subleaf1 && ((EDX >> 8) & 1) && HasAMXSave)
    setFeature(FEATURE_AMX_COMPLEX);
  if (HasLeaf7Subleaf1 && ((EDX >> 10) & 1) && HasAVXSave)
    setFeature(FEATURE_AVXVNNIINT16);
  if (HasLeaf7Subleaf1 && ((EDX >> 14) & 1))
    setFeature(FEATURE_PREFETCHI);
  if (HasLeaf7Subleaf1 && ((EDX >> 15) & 1))
    setFeature(FEATURE_USERMSR);
  if (HasLeaf7Subleaf1 && ((EDX >> 19) & 1))
    setFeature(FEATURE_AVX10_1_256);
  if (HasLeaf7Subleaf1 && ((EDX >> 21) & 1))
    setFeature(FEATURE_APXF);

  unsigned MaxLevel;
  getX86CpuIDAndInfo(0, &MaxLevel, &EBX, &ECX, &EDX);
  bool HasLeafD = MaxLevel >= 0xd &&
                  !getX86CpuIDAndInfoEx(0xd, 0x1, &EAX, &EBX, &ECX, &EDX);
  if (HasLeafD && ((EAX >> 0) & 1) && HasAVXSave)
    setFeature(FEATURE_XSAVEOPT);
  if (HasLeafD && ((EAX >> 1) & 1) && HasAVXSave)
    setFeature(FEATURE_XSAVEC);
  if (HasLeafD && ((EAX >> 3) & 1) && HasAVXSave)
    setFeature(FEATURE_XSAVES);

  bool HasLeaf24 =
      MaxLevel >= 0x24 && !getX86CpuIDAndInfo(0x24, &EAX, &EBX, &ECX, &EDX);
  if (HasLeaf7Subleaf1 && ((EDX >> 19) & 1) && HasLeaf24 && ((EBX >> 18) & 1))
    setFeature(FEATURE_AVX10_1_512);

  unsigned MaxExtLevel;
  getX86CpuIDAndInfo(0x80000000, &MaxExtLevel, &EBX, &ECX, &EDX);

  bool HasExtLeaf1 = MaxExtLevel >= 0x80000001 &&
                     !getX86CpuIDAndInfo(0x80000001, &EAX, &EBX, &ECX, &EDX);
  if (HasExtLeaf1) {
    if (ECX & 1)
      setFeature(FEATURE_LAHF_LM);
    if ((ECX >> 5) & 1)
      setFeature(FEATURE_LZCNT);
    if (((ECX >> 6) & 1))
      setFeature(FEATURE_SSE4_A);
    if (((ECX >> 8) & 1))
      setFeature(FEATURE_PRFCHW);
    if (((ECX >> 11) & 1))
      setFeature(FEATURE_XOP);
    if (((ECX >> 15) & 1))
      setFeature(FEATURE_LWP);
    if (((ECX >> 16) & 1))
      setFeature(FEATURE_FMA4);
    if (((ECX >> 21) & 1))
      setFeature(FEATURE_TBM);
    if (((ECX >> 29) & 1))
      setFeature(FEATURE_MWAITX);

    if (((EDX >> 29) & 1))
      setFeature(FEATURE_LM);
  }

  bool HasExtLeaf8 = MaxExtLevel >= 0x80000008 &&
                     !getX86CpuIDAndInfo(0x80000008, &EAX, &EBX, &ECX, &EDX);
  if (HasExtLeaf8 && ((EBX >> 0) & 1))
    setFeature(FEATURE_CLZERO);
  if (HasExtLeaf8 && ((EBX >> 9) & 1))
    setFeature(FEATURE_WBNOINVD);

  bool HasLeaf14 = MaxLevel >= 0x14 &&
                   !getX86CpuIDAndInfoEx(0x14, 0x0, &EAX, &EBX, &ECX, &EDX);
  if (HasLeaf14 && ((EBX >> 4) & 1))
    setFeature(FEATURE_PTWRITE);

  bool HasLeaf19 =
      MaxLevel >= 0x19 && !getX86CpuIDAndInfo(0x19, &EAX, &EBX, &ECX, &EDX);
  if (HasLeaf7 && HasLeaf19 && ((EBX >> 2) & 1))
    setFeature(FEATURE_WIDEKL);

  if (hasFeature(FEATURE_LM) && hasFeature(FEATURE_SSE2)) {
    setFeature(FEATURE_X86_64_BASELINE);
    if (hasFeature(FEATURE_CMPXCHG16B) && hasFeature(FEATURE_POPCNT) &&
        hasFeature(FEATURE_LAHF_LM) && hasFeature(FEATURE_SSE4_2)) {
      setFeature(FEATURE_X86_64_V2);
      if (hasFeature(FEATURE_AVX2) && hasFeature(FEATURE_BMI) &&
          hasFeature(FEATURE_BMI2) && hasFeature(FEATURE_F16C) &&
          hasFeature(FEATURE_FMA) && hasFeature(FEATURE_LZCNT) &&
          hasFeature(FEATURE_MOVBE)) {
        setFeature(FEATURE_X86_64_V3);
        if (hasFeature(FEATURE_AVX512BW) && hasFeature(FEATURE_AVX512CD) &&
            hasFeature(FEATURE_AVX512DQ) && hasFeature(FEATURE_AVX512VL))
          setFeature(FEATURE_X86_64_V4);
      }
    }
  }

#undef hasFeature
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
unsigned __cpu_features2[(CPU_FEATURE_MAX - 1) / 32];

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
  static_assert(sizeof(Features) / sizeof(Features[0]) == 4, "");
  static_assert(sizeof(__cpu_features2) / sizeof(__cpu_features2[0]) == 3, "");

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

  __cpu_model.__cpu_features[0] = Features[0];
  __cpu_features2[0] = Features[1];
  __cpu_features2[1] = Features[2];
  __cpu_features2[2] = Features[3];

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
#endif // defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
