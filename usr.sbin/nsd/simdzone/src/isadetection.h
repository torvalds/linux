/*
 * isadetection.h -- detect supported instruction set(s)
 *
 * Slightly modified version of isadetection.h in simdjson.
 *
 * Copyright (c) 2024      NLnet Labs               (Jeroen Koekkoek)
 * Copyright (c) 2020-     simdjson                 (Daniel Lemire,
 *                                                   Geoff Langdale,
 *                                                   John Keiser)
 * Copyright (c) 2016-     Facebook, Inc            (Adam Paszke)
 * Copyright (c) 2014-     Facebook, Inc            (Soumith Chintala)
 * Copyright (c) 2011-2014 Idiap Research Institute (Ronan Collobert)
 * Copyright (c) 2012-2014 Deepmind Technologies    (Koray Kavukcuoglu)
 * Copyright (c) 2011-2012 NEC Laboratories America (Koray Kavukcuoglu)
 * Copyright (c) 2011-2013 NYU                      (Clement Farabet)
 * Copyright (c) 2006-2010 NEC Laboratories America (Ronan Collobert,
 *                                                   Leon Bottou,
 *                                                   Iain Melvin,
 *                                                   Jason Weston)
 * Copyright (c) 2006      Idiap Research Institute (Samy Bengio)
 * Copyright (c) 2001-2004 Idiap Research Institute (Ronan Collobert,
 *                                                   Samy Bengio,
 *                                                   Johnny Mariethoz)
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the names of simdjson, Facebook, Deepmind Technologies, NYU,
 *    NEC Laboratories America and IDIAP Research Institute nor the names of
 *    its contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ISADETECTION_H
#define ISADETECTION_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#if defined(_MSC_VER)
#include <intrin.h>
#include <immintrin.h>
#elif defined(HAVE_CPUID)
#include <cpuid.h>
#endif

enum instruction_set {
  DEFAULT = 0x0,
  NEON = 0x1,
  AVX2 = 0x4,
  SSE42 = 0x8,
  PCLMULQDQ = 0x10,
  BMI1 = 0x20,
  BMI2 = 0x40,
  ALTIVEC = 0x80,
  AVX512F = 0x100,
  AVX512DQ = 0x200,
  AVX512IFMA = 0x400,
  AVX512PF = 0x800,
  AVX512ER = 0x1000,
  AVX512CD = 0x2000,
  AVX512BW = 0x4000,
  AVX512VL = 0x8000,
  AVX512VBMI2 = 0x10000
};

#if defined(__PPC64__)

static inline uint32_t detect_supported_architectures(void) {
  return ALTIVEC;
}

#elif defined(__arm__) || defined(__aarch64__) // incl. armel, armhf, arm64

#if defined(__ARM_NEON)

static inline uint32_t detect_supported_architectures(void) {
  return NEON;
}

#else // ARM without NEON

static inline uint32_t detect_supported_architectures(void) {
  return DEFAULT;
}

#endif

#elif defined(__x86_64__) || defined(_M_AMD64) // x64

// Can be found on Intel ISA Reference for CPUID
static const uint32_t cpuid_avx2_bit = 1 << 5;          ///< @private Bit  5 of EBX for EAX=0x7
static const uint32_t cpuid_bmi1_bit = 1 << 3;          ///< @private bit  3 of EBX for EAX=0x7
static const uint32_t cpuid_bmi2_bit = 1 << 8;          ///< @private bit  8 of EBX for EAX=0x7
static const uint32_t cpuid_avx512f_bit = 1 << 16;      ///< @private bit 16 of EBX for EAX=0x7
static const uint32_t cpuid_avx512dq_bit = 1 << 17;     ///< @private bit 17 of EBX for EAX=0x7
static const uint32_t cpuid_avx512ifma_bit = 1 << 21;   ///< @private bit 21 of EBX for EAX=0x7
static const uint32_t cpuid_avx512pf_bit = 1 << 26;     ///< @private bit 26 of EBX for EAX=0x7
static const uint32_t cpuid_avx512er_bit = 1 << 27;     ///< @private bit 27 of EBX for EAX=0x7
static const uint32_t cpuid_avx512cd_bit = 1 << 28;     ///< @private bit 28 of EBX for EAX=0x7
static const uint32_t cpuid_avx512bw_bit = 1 << 30;     ///< @private bit 30 of EBX for EAX=0x7
static const uint32_t cpuid_avx512vl_bit = 1U << 31;    ///< @private bit 31 of EBX for EAX=0x7
static const uint32_t cpuid_avx512vbmi2_bit = 1 << 6;   ///< @private bit  6 of ECX for EAX=0x7
static const uint32_t cpuid_sse42_bit = 1 << 20;        ///< @private bit 20 of ECX for EAX=0x1
static const uint32_t cpuid_pclmulqdq_bit = 1 << 1;     ///< @private bit  1 of ECX for EAX=0x1
static const uint32_t cpuid_have_xgetbv_bit = 1 << 27;  ///< @private bit 27 of ECX for EAX=0x1
static const uint32_t cpuid_have_avx_bit = 1 << 28;     ///< @private bit 28 of ECX for EAX=0x1

static inline void cpuid(
  uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
#if defined(_MSC_VER)
  int cpu_info[4];
  __cpuid(cpu_info, *eax);
  *eax = cpu_info[0];
  *ebx = cpu_info[1];
  *ecx = cpu_info[2];
  *edx = cpu_info[3];
#elif defined(HAVE_CPUID)
  uint32_t level = *eax;
  __get_cpuid(level, eax, ebx, ecx, edx);
#else
  uint32_t a = *eax, b, c = *ecx, d;
  asm volatile("cpuid\n\t" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
  *eax = a;
  *ebx = b;
  *ecx = c;
  *edx = d;
#endif
}

static inline uint64_t xgetbv(uint32_t ecx)
{
#if defined(_MSC_VER)
  return _xgetbv(ecx);
#else
  uint32_t a, c = ecx, d;
  asm volatile("xgetbv\n\t" : "=d"(d), "=a"(a) : "c"(c));
  uint64_t xcr0 = ((uint64_t)d << 32) | (uint64_t)a;
  return xcr0;
#endif
}

static inline uint32_t detect_supported_architectures(void)
{
  uint32_t eax, ebx, ecx, edx;
  uint32_t host_isa = 0x0, host_avx_isa = 0x0;

  // ECX for EAX=0x7
  eax = 0x7;
  ecx = 0x0;
  cpuid(&eax, &ebx, &ecx, &edx);
  if (ebx & cpuid_bmi1_bit) {
    host_isa |= BMI1;
  }

  if (ebx & cpuid_bmi2_bit) {
    host_isa |= BMI2;
  }

  if (ebx & cpuid_avx2_bit) {
    host_avx_isa |= AVX2;
  }

  if (ebx & cpuid_avx512f_bit) {
    host_avx_isa |= AVX512F;
  }

  if (ebx & cpuid_avx512dq_bit) {
    host_avx_isa |= AVX512DQ;
  }

  if (ebx & cpuid_avx512ifma_bit) {
    host_avx_isa |= AVX512IFMA;
  }

  if (ebx & cpuid_avx512pf_bit) {
    host_avx_isa |= AVX512PF;
  }

  if (ebx & cpuid_avx512er_bit) {
    host_avx_isa |= AVX512ER;
  }

  if (ebx & cpuid_avx512cd_bit) {
    host_avx_isa |= AVX512CD;
  }

  if (ebx & cpuid_avx512bw_bit) {
    host_avx_isa |= AVX512BW;
  }

  if (ebx & cpuid_avx512vl_bit) {
    host_avx_isa |= AVX512VL;
  }

  if (ecx & cpuid_avx512vbmi2_bit) {
    host_avx_isa |= AVX512VBMI2;
  }

  bool have_avx = false, have_xgetbv = false;

  // EBX for EAX=0x1
  eax = 0x1;
  cpuid(&eax, &ebx, &ecx, &edx);
  if (ecx & cpuid_sse42_bit) {
    host_isa |= SSE42;
  }

  if (ecx & cpuid_pclmulqdq_bit) {
    host_isa |= PCLMULQDQ;
  }

  // Correct detection of AVX2 support requires more than checking the CPUID
  // bit. Peter Cordes provides an excellent answer on Stack Overflow
  // (https://stackoverflow.com/a/34071400) quoting the article Introduction
  // to Intel Advanced Vector Extensions (search Wayback Machine).
  //
  // 1. Verify that the operating system supports XGETBV using
  //    CPUID.1:ECX.OSXSAVE bit 27 = 1.
  // 2. Verify the processor supports the AVX instruction extensions using:
  //    CPUID.1:ECX bit 28 = 1.
  // 3. Issue XGETBV, and verify that the feature-enabled mask at bits 1 and 2
  //    are 11b (XMM state and YMM state enabled by the operating system).


  // Determine if the CPU supports AVX
  have_avx = (ecx & cpuid_have_avx_bit) != 0;
  // Determine if the Operating System supports XGETBV
  have_xgetbv = (ecx & cpuid_have_xgetbv_bit) != 0;

  if (have_avx && have_xgetbv) {
    uint64_t xcr0 = xgetbv(0x0);
    if ((xcr0 & 0x6) == 0x6)
      host_isa |= host_avx_isa;
  }

  return host_isa;
}
#else // fallback

static inline uint32_t detect_supported_architectures(void) {
  return DEFAULT;
}

#endif // end SIMD extension detection code

#endif // ISADETECTION_H
