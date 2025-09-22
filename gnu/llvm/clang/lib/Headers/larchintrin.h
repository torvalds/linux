/*===------------ larchintrin.h - LoongArch intrinsics ---------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef _LOONGARCH_BASE_INTRIN_H
#define _LOONGARCH_BASE_INTRIN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rdtime {
  unsigned int value;
  unsigned int timeid;
} __rdtime_t;

#if __loongarch_grlen == 64
typedef struct drdtime {
  unsigned long dvalue;
  unsigned long dtimeid;
} __drdtime_t;

extern __inline __drdtime_t
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __rdtime_d(void) {
  __drdtime_t __drdtime;
  __asm__ volatile(
      "rdtime.d %[val], %[tid]\n\t"
      : [val] "=&r"(__drdtime.dvalue), [tid] "=&r"(__drdtime.dtimeid));
  return __drdtime;
}
#endif

extern __inline __rdtime_t
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __rdtimeh_w(void) {
  __rdtime_t __rdtime;
  __asm__ volatile("rdtimeh.w %[val], %[tid]\n\t"
                   : [val] "=&r"(__rdtime.value), [tid] "=&r"(__rdtime.timeid));
  return __rdtime;
}

extern __inline __rdtime_t
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __rdtimel_w(void) {
  __rdtime_t __rdtime;
  __asm__ volatile("rdtimel.w %[val], %[tid]\n\t"
                   : [val] "=&r"(__rdtime.value), [tid] "=&r"(__rdtime.timeid));
  return __rdtime;
}

#if __loongarch_grlen == 64
extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __crc_w_b_w(char _1, int _2) {
  return (int)__builtin_loongarch_crc_w_b_w((char)_1, (int)_2);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __crc_w_h_w(short _1, int _2) {
  return (int)__builtin_loongarch_crc_w_h_w((short)_1, (int)_2);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __crc_w_w_w(int _1, int _2) {
  return (int)__builtin_loongarch_crc_w_w_w((int)_1, (int)_2);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __crc_w_d_w(long int _1, int _2) {
  return (int)__builtin_loongarch_crc_w_d_w((long int)_1, (int)_2);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __crcc_w_b_w(char _1, int _2) {
  return (int)__builtin_loongarch_crcc_w_b_w((char)_1, (int)_2);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __crcc_w_h_w(short _1, int _2) {
  return (int)__builtin_loongarch_crcc_w_h_w((short)_1, (int)_2);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __crcc_w_w_w(int _1, int _2) {
  return (int)__builtin_loongarch_crcc_w_w_w((int)_1, (int)_2);
}

extern __inline int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __crcc_w_d_w(long int _1, int _2) {
  return (int)__builtin_loongarch_crcc_w_d_w((long int)_1, (int)_2);
}
#endif

#define __break(/*ui15*/ _1) __builtin_loongarch_break((_1))

#if __loongarch_grlen == 32
#define __cacop_w(/*uimm5*/ _1, /*unsigned int*/ _2, /*simm12*/ _3)            \
  ((void)__builtin_loongarch_cacop_w((_1), (unsigned int)(_2), (_3)))
#endif

#if __loongarch_grlen == 64
#define __cacop_d(/*uimm5*/ _1, /*unsigned long int*/ _2, /*simm12*/ _3)       \
  ((void)__builtin_loongarch_cacop_d((_1), (unsigned long int)(_2), (_3)))
#endif

#define __dbar(/*ui15*/ _1) __builtin_loongarch_dbar((_1))

#define __ibar(/*ui15*/ _1) __builtin_loongarch_ibar((_1))

#define __movfcsr2gr(/*ui5*/ _1) __builtin_loongarch_movfcsr2gr((_1));

#define __movgr2fcsr(/*ui5*/ _1, _2)                                           \
  __builtin_loongarch_movgr2fcsr((_1), (unsigned int)_2);

#define __syscall(/*ui15*/ _1) __builtin_loongarch_syscall((_1))

#define __csrrd_w(/*ui14*/ _1) ((unsigned int)__builtin_loongarch_csrrd_w((_1)))

#define __csrwr_w(/*unsigned int*/ _1, /*ui14*/ _2)                            \
  ((unsigned int)__builtin_loongarch_csrwr_w((unsigned int)(_1), (_2)))

#define __csrxchg_w(/*unsigned int*/ _1, /*unsigned int*/ _2, /*ui14*/ _3)     \
  ((unsigned int)__builtin_loongarch_csrxchg_w((unsigned int)(_1),             \
                                               (unsigned int)(_2), (_3)))

#if __loongarch_grlen == 64
#define __csrrd_d(/*ui14*/ _1)                                                 \
  ((unsigned long int)__builtin_loongarch_csrrd_d((_1)))

#define __csrwr_d(/*unsigned long int*/ _1, /*ui14*/ _2)                       \
  ((unsigned long int)__builtin_loongarch_csrwr_d((unsigned long int)(_1),     \
                                                  (_2)))

#define __csrxchg_d(/*unsigned long int*/ _1, /*unsigned long int*/ _2,        \
                    /*ui14*/ _3)                                               \
  ((unsigned long int)__builtin_loongarch_csrxchg_d(                           \
      (unsigned long int)(_1), (unsigned long int)(_2), (_3)))
#endif

extern __inline unsigned char
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __iocsrrd_b(unsigned int _1) {
  return (unsigned char)__builtin_loongarch_iocsrrd_b((unsigned int)_1);
}

extern __inline unsigned short
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __iocsrrd_h(unsigned int _1) {
  return (unsigned short)__builtin_loongarch_iocsrrd_h((unsigned int)_1);
}

extern __inline unsigned int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __iocsrrd_w(unsigned int _1) {
  return (unsigned int)__builtin_loongarch_iocsrrd_w((unsigned int)_1);
}

#if __loongarch_grlen == 64
extern __inline unsigned long int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __iocsrrd_d(unsigned int _1) {
  return (unsigned long int)__builtin_loongarch_iocsrrd_d((unsigned int)_1);
}
#endif

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __iocsrwr_b(unsigned char _1, unsigned int _2) {
  __builtin_loongarch_iocsrwr_b((unsigned char)_1, (unsigned int)_2);
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __iocsrwr_h(unsigned short _1, unsigned int _2) {
  __builtin_loongarch_iocsrwr_h((unsigned short)_1, (unsigned int)_2);
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __iocsrwr_w(unsigned int _1, unsigned int _2) {
  __builtin_loongarch_iocsrwr_w((unsigned int)_1, (unsigned int)_2);
}

extern __inline unsigned int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __cpucfg(unsigned int _1) {
  return (unsigned int)__builtin_loongarch_cpucfg((unsigned int)_1);
}

#if __loongarch_grlen == 64
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __iocsrwr_d(unsigned long int _1, unsigned int _2) {
  __builtin_loongarch_iocsrwr_d((unsigned long int)_1, (unsigned int)_2);
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __asrtgt_d(long int _1, long int _2) {
  __builtin_loongarch_asrtgt_d((long int)_1, (long int)_2);
}

extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    __asrtle_d(long int _1, long int _2) {
  __builtin_loongarch_asrtle_d((long int)_1, (long int)_2);
}
#endif

#if __loongarch_grlen == 64
#define __lddir_d(/*long int*/ _1, /*ui5*/ _2)                                 \
  ((long int)__builtin_loongarch_lddir_d((long int)(_1), (_2)))

#define __ldpte_d(/*long int*/ _1, /*ui5*/ _2)                                 \
  ((void)__builtin_loongarch_ldpte_d((long int)(_1), (_2)))
#endif

#define __frecipe_s(/*float*/ _1)                                              \
  (float)__builtin_loongarch_frecipe_s((float)_1)

#define __frecipe_d(/*double*/ _1)                                             \
  (double)__builtin_loongarch_frecipe_d((double)_1)

#define __frsqrte_s(/*float*/ _1)                                              \
  (float)__builtin_loongarch_frsqrte_s((float)_1)

#define __frsqrte_d(/*double*/ _1)                                             \
  (double)__builtin_loongarch_frsqrte_d((double)_1)

#ifdef __cplusplus
}
#endif
#endif /* _LOONGARCH_BASE_INTRIN_H */
