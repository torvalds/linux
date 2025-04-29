/* SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause */
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD_COMMON_CPU_H
#define ZSTD_COMMON_CPU_H

/*
 * Implementation taken from folly/CpuId.h
 * https://github.com/facebook/folly/blob/master/folly/CpuId.h
 */

#include "mem.h"


typedef struct {
    U32 f1c;
    U32 f1d;
    U32 f7b;
    U32 f7c;
} ZSTD_cpuid_t;

MEM_STATIC ZSTD_cpuid_t ZSTD_cpuid(void) {
    U32 f1c = 0;
    U32 f1d = 0;
    U32 f7b = 0;
    U32 f7c = 0;
#if defined(__i386__) && defined(__PIC__) && !defined(__clang__) && defined(__GNUC__)
    /* The following block like the normal cpuid branch below, but gcc
     * reserves ebx for use of its pic register so we must specially
     * handle the save and restore to avoid clobbering the register
     */
    U32 n;
    __asm__(
        "pushl %%ebx\n\t"
        "cpuid\n\t"
        "popl %%ebx\n\t"
        : "=a"(n)
        : "a"(0)
        : "ecx", "edx");
    if (n >= 1) {
      U32 f1a;
      __asm__(
          "pushl %%ebx\n\t"
          "cpuid\n\t"
          "popl %%ebx\n\t"
          : "=a"(f1a), "=c"(f1c), "=d"(f1d)
          : "a"(1));
    }
    if (n >= 7) {
      __asm__(
          "pushl %%ebx\n\t"
          "cpuid\n\t"
          "movl %%ebx, %%eax\n\t"
          "popl %%ebx"
          : "=a"(f7b), "=c"(f7c)
          : "a"(7), "c"(0)
          : "edx");
    }
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    U32 n;
    __asm__("cpuid" : "=a"(n) : "a"(0) : "ebx", "ecx", "edx");
    if (n >= 1) {
      U32 f1a;
      __asm__("cpuid" : "=a"(f1a), "=c"(f1c), "=d"(f1d) : "a"(1) : "ebx");
    }
    if (n >= 7) {
      U32 f7a;
      __asm__("cpuid"
              : "=a"(f7a), "=b"(f7b), "=c"(f7c)
              : "a"(7), "c"(0)
              : "edx");
    }
#endif
    {
        ZSTD_cpuid_t cpuid;
        cpuid.f1c = f1c;
        cpuid.f1d = f1d;
        cpuid.f7b = f7b;
        cpuid.f7c = f7c;
        return cpuid;
    }
}

#define X(name, r, bit)                                                        \
  MEM_STATIC int ZSTD_cpuid_##name(ZSTD_cpuid_t const cpuid) {                 \
    return ((cpuid.r) & (1U << bit)) != 0;                                     \
  }

/* cpuid(1): Processor Info and Feature Bits. */
#define C(name, bit) X(name, f1c, bit)
  C(sse3, 0)
  C(pclmuldq, 1)
  C(dtes64, 2)
  C(monitor, 3)
  C(dscpl, 4)
  C(vmx, 5)
  C(smx, 6)
  C(eist, 7)
  C(tm2, 8)
  C(ssse3, 9)
  C(cnxtid, 10)
  C(fma, 12)
  C(cx16, 13)
  C(xtpr, 14)
  C(pdcm, 15)
  C(pcid, 17)
  C(dca, 18)
  C(sse41, 19)
  C(sse42, 20)
  C(x2apic, 21)
  C(movbe, 22)
  C(popcnt, 23)
  C(tscdeadline, 24)
  C(aes, 25)
  C(xsave, 26)
  C(osxsave, 27)
  C(avx, 28)
  C(f16c, 29)
  C(rdrand, 30)
#undef C
#define D(name, bit) X(name, f1d, bit)
  D(fpu, 0)
  D(vme, 1)
  D(de, 2)
  D(pse, 3)
  D(tsc, 4)
  D(msr, 5)
  D(pae, 6)
  D(mce, 7)
  D(cx8, 8)
  D(apic, 9)
  D(sep, 11)
  D(mtrr, 12)
  D(pge, 13)
  D(mca, 14)
  D(cmov, 15)
  D(pat, 16)
  D(pse36, 17)
  D(psn, 18)
  D(clfsh, 19)
  D(ds, 21)
  D(acpi, 22)
  D(mmx, 23)
  D(fxsr, 24)
  D(sse, 25)
  D(sse2, 26)
  D(ss, 27)
  D(htt, 28)
  D(tm, 29)
  D(pbe, 31)
#undef D

/* cpuid(7): Extended Features. */
#define B(name, bit) X(name, f7b, bit)
  B(bmi1, 3)
  B(hle, 4)
  B(avx2, 5)
  B(smep, 7)
  B(bmi2, 8)
  B(erms, 9)
  B(invpcid, 10)
  B(rtm, 11)
  B(mpx, 14)
  B(avx512f, 16)
  B(avx512dq, 17)
  B(rdseed, 18)
  B(adx, 19)
  B(smap, 20)
  B(avx512ifma, 21)
  B(pcommit, 22)
  B(clflushopt, 23)
  B(clwb, 24)
  B(avx512pf, 26)
  B(avx512er, 27)
  B(avx512cd, 28)
  B(sha, 29)
  B(avx512bw, 30)
  B(avx512vl, 31)
#undef B
#define C(name, bit) X(name, f7c, bit)
  C(prefetchwt1, 0)
  C(avx512vbmi, 1)
#undef C

#undef X

#endif /* ZSTD_COMMON_CPU_H */
