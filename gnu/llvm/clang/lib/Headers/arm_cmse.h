//===---- arm_cmse.h - Arm CMSE support -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __ARM_CMSE_H
#define __ARM_CMSE_H

#if (__ARM_FEATURE_CMSE & 0x1)
#include <stddef.h>
#include <stdint.h>

#define __ARM_CMSE_SECURE_MODE (__ARM_FEATURE_CMSE & 0x2)
#define CMSE_MPU_READWRITE 1 /* checks if readwrite_ok field is set */
#define CMSE_AU_NONSECURE  2 /* checks if permissions have secure field unset */
#define CMSE_MPU_UNPRIV    4 /* sets T flag on TT insrtuction */
#define CMSE_MPU_READ      8 /* checks if read_ok field is set */
#define CMSE_MPU_NONSECURE 16 /* sets A flag, checks if secure field unset */
#define CMSE_NONSECURE (CMSE_AU_NONSECURE | CMSE_MPU_NONSECURE)

#define cmse_check_pointed_object(p, f) \
  cmse_check_address_range((p), sizeof(*(p)), (f))

#if defined(__cplusplus)
extern "C" {
#endif

typedef union {
  struct cmse_address_info {
#ifdef __ARM_BIG_ENDIAN
    /* __ARM_BIG_ENDIAN */
#if (__ARM_CMSE_SECURE_MODE)
    unsigned idau_region : 8;
    unsigned idau_region_valid : 1;
    unsigned secure : 1;
    unsigned nonsecure_readwrite_ok : 1;
    unsigned nonsecure_read_ok : 1;
#else
    unsigned : 12;
#endif
    unsigned readwrite_ok : 1;
    unsigned read_ok : 1;
#if (__ARM_CMSE_SECURE_MODE)
    unsigned sau_region_valid : 1;
#else
    unsigned : 1;
#endif
    unsigned mpu_region_valid : 1;
#if (__ARM_CMSE_SECURE_MODE)
    unsigned sau_region : 8;
#else
    unsigned : 8;
#endif
    unsigned mpu_region : 8;

#else /* __ARM_LITTLE_ENDIAN */
    unsigned mpu_region : 8;
#if (__ARM_CMSE_SECURE_MODE)
    unsigned sau_region : 8;
#else
    unsigned : 8;
#endif
    unsigned mpu_region_valid : 1;
#if (__ARM_CMSE_SECURE_MODE)
    unsigned sau_region_valid : 1;
#else
    unsigned : 1;
#endif
    unsigned read_ok : 1;
    unsigned readwrite_ok : 1;
#if (__ARM_CMSE_SECURE_MODE)
    unsigned nonsecure_read_ok : 1;
    unsigned nonsecure_readwrite_ok : 1;
    unsigned secure : 1;
    unsigned idau_region_valid : 1;
    unsigned idau_region : 8;
#else
    unsigned : 12;
#endif
#endif /*__ARM_LITTLE_ENDIAN */
  } flags;
  unsigned value;
} cmse_address_info_t;

static cmse_address_info_t __attribute__((__always_inline__, __nodebug__))
cmse_TT(void *__p) {
  cmse_address_info_t __u;
  __u.value = __builtin_arm_cmse_TT(__p);
  return __u;
}
static cmse_address_info_t __attribute__((__always_inline__, __nodebug__))
cmse_TTT(void *__p) {
  cmse_address_info_t __u;
  __u.value = __builtin_arm_cmse_TTT(__p);
  return __u;
}

#if __ARM_CMSE_SECURE_MODE
static cmse_address_info_t __attribute__((__always_inline__, __nodebug__))
cmse_TTA(void *__p) {
  cmse_address_info_t __u;
  __u.value = __builtin_arm_cmse_TTA(__p);
  return __u;
}
static cmse_address_info_t __attribute__((__always_inline__, __nodebug__))
cmse_TTAT(void *__p) {
  cmse_address_info_t __u;
  __u.value = __builtin_arm_cmse_TTAT(__p);
  return __u;
}
#endif

#define cmse_TT_fptr(p) cmse_TT(__builtin_bit_cast(void *, (p)))
#define cmse_TTT_fptr(p) cmse_TTT(__builtin_bit_cast(void *, (p)))

#if __ARM_CMSE_SECURE_MODE
#define cmse_TTA_fptr(p) cmse_TTA(__builtin_bit_cast(void *, (p)))
#define cmse_TTAT_fptr(p) cmse_TTAT(__builtin_bit_cast(void *, (p)))
#endif

static void *__attribute__((__always_inline__))
cmse_check_address_range(void *__pb, size_t __s, int __flags) {
  uintptr_t __begin = (uintptr_t)__pb;
  uintptr_t __end = __begin + __s - 1;

  if (__end < __begin)
    return NULL; /* wrap around check */

  /* Check whether the range crosses a 32-bytes aligned address */
  const int __single_check = (__begin ^ __end) < 0x20u;

  /* execute the right variant of the TT instructions */
  void *__pe = (void *)__end;
  cmse_address_info_t __permb, __perme;
  switch (__flags & (CMSE_MPU_UNPRIV | CMSE_MPU_NONSECURE)) {
  case 0:
    __permb = cmse_TT(__pb);
    __perme = __single_check ? __permb : cmse_TT(__pe);
    break;
  case CMSE_MPU_UNPRIV:
    __permb = cmse_TTT(__pb);
    __perme = __single_check ? __permb : cmse_TTT(__pe);
    break;
#if __ARM_CMSE_SECURE_MODE
  case CMSE_MPU_NONSECURE:
    __permb = cmse_TTA(__pb);
    __perme = __single_check ? __permb : cmse_TTA(__pe);
    break;
  case CMSE_MPU_UNPRIV | CMSE_MPU_NONSECURE:
    __permb = cmse_TTAT(__pb);
    __perme = __single_check ? __permb : cmse_TTAT(__pe);
    break;
#endif
  /* if CMSE_NONSECURE is specified w/o __ARM_CMSE_SECURE_MODE */
  default:
    return NULL;
  }

  /* check that the range does not cross MPU, SAU, or IDAU region boundaries */
  if (__permb.value != __perme.value)
    return NULL;
#if !(__ARM_CMSE_SECURE_MODE)
  /* CMSE_AU_NONSECURE is only supported when __ARM_FEATURE_CMSE & 0x2 */
  if (__flags & CMSE_AU_NONSECURE)
    return NULL;
#endif

  /* check the permission on the range */
  switch (__flags & ~(CMSE_MPU_UNPRIV | CMSE_MPU_NONSECURE)) {
#if (__ARM_CMSE_SECURE_MODE)
  case CMSE_MPU_READ | CMSE_MPU_READWRITE | CMSE_AU_NONSECURE:
  case CMSE_MPU_READWRITE | CMSE_AU_NONSECURE:
    return __permb.flags.nonsecure_readwrite_ok ? __pb : NULL;

  case CMSE_MPU_READ | CMSE_AU_NONSECURE:
    return __permb.flags.nonsecure_read_ok ? __pb : NULL;

  case CMSE_AU_NONSECURE:
    return __permb.flags.secure ? NULL : __pb;
#endif
  case CMSE_MPU_READ | CMSE_MPU_READWRITE:
  case CMSE_MPU_READWRITE:
    return __permb.flags.readwrite_ok ? __pb : NULL;

  case CMSE_MPU_READ:
    return __permb.flags.read_ok ? __pb : NULL;

  default:
    return NULL;
  }
}

#if __ARM_CMSE_SECURE_MODE
static int __attribute__((__always_inline__, __nodebug__))
cmse_nonsecure_caller(void) {
  return !((uintptr_t)__builtin_return_address(0) & 1);
}

#define cmse_nsfptr_create(p)                                                  \
  __builtin_bit_cast(__typeof__(p),                                            \
                     (__builtin_bit_cast(uintptr_t, p) & ~(uintptr_t)1))

#define cmse_is_nsfptr(p) ((__builtin_bit_cast(uintptr_t, p) & 1) == 0)

#endif /* __ARM_CMSE_SECURE_MODE */

void __attribute__((__noreturn__)) cmse_abort(void);
#if defined(__cplusplus)
}
#endif

#endif /* (__ARM_FEATURE_CMSE & 0x1) */

#endif /* __ARM_CMSE_H */
