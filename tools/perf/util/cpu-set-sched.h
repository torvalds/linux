// SPDX-License-Identifier: LGPL-2.1
// Definitions taken from glibc for use with older systems, same licensing.
#ifndef _CPU_SET_SCHED_PERF_H
#define _CPU_SET_SCHED_PERF_H

#include <features.h>
#include <sched.h>

#ifndef CPU_EQUAL
#ifndef __CPU_EQUAL_S
#if __GNUC_PREREQ (2, 91)
# define __CPU_EQUAL_S(setsize, cpusetp1, cpusetp2) \
  (__builtin_memcmp (cpusetp1, cpusetp2, setsize) == 0)
#else
# define __CPU_EQUAL_S(setsize, cpusetp1, cpusetp2) \
  (__extension__							      \
   ({ const __cpu_mask *__arr1 = (cpusetp1)->__bits;			      \
      const __cpu_mask *__arr2 = (cpusetp2)->__bits;			      \
      size_t __imax = (setsize) / sizeof (__cpu_mask);			      \
      size_t __i;							      \
      for (__i = 0; __i < __imax; ++__i)				      \
	if (__arr1[__i] != __arr2[__i])					      \
	  break;							      \
      __i == __imax; }))
#endif
#endif // __CPU_EQUAL_S

#define CPU_EQUAL(cpusetp1, cpusetp2) \
  __CPU_EQUAL_S (sizeof (cpu_set_t), cpusetp1, cpusetp2)
#endif // CPU_EQUAL

#ifndef CPU_OR
#ifndef __CPU_OP_S
#define __CPU_OP_S(setsize, destset, srcset1, srcset2, op) \
  (__extension__							      \
   ({ cpu_set_t *__dest = (destset);					      \
      const __cpu_mask *__arr1 = (srcset1)->__bits;			      \
      const __cpu_mask *__arr2 = (srcset2)->__bits;			      \
      size_t __imax = (setsize) / sizeof (__cpu_mask);			      \
      size_t __i;							      \
      for (__i = 0; __i < __imax; ++__i)				      \
	((__cpu_mask *) __dest->__bits)[__i] = __arr1[__i] op __arr2[__i];    \
      __dest; }))
#endif // __CPU_OP_S

#define CPU_OR(destset, srcset1, srcset2) \
  __CPU_OP_S (sizeof (cpu_set_t), destset, srcset1, srcset2, |)
#endif // CPU_OR

#endif // _CPU_SET_SCHED_PERF_H
