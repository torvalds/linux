/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1994, 95, 96, 97, 98, 2000, 01 Ralf Baechle
 * Copyright (c) 2000 by Silicon Graphics, Inc.
 * Copyright (c) 2001 MIPS Technologies, Inc.
 */
#ifndef _ASM_STRING_H
#define _ASM_STRING_H

#include <linux/config.h>

/*
 * Most of the inline functions are rather naive implementations so I just
 * didn't bother updating them for 64-bit ...
 */
#ifdef CONFIG_MIPS32

#ifndef IN_STRING_C

#define __HAVE_ARCH_STRCPY
static __inline__ char *strcpy(char *__dest, __const__ char *__src)
{
  char *__xdest = __dest;

  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t$1,(%1)\n\t"
	"addiu\t%1,1\n\t"
	"sb\t$1,(%0)\n\t"
	"bnez\t$1,1b\n\t"
	"addiu\t%0,1\n\t"
	".set\tat\n\t"
	".set\treorder"
	: "=r" (__dest), "=r" (__src)
        : "0" (__dest), "1" (__src)
	: "memory");

  return __xdest;
}

#define __HAVE_ARCH_STRNCPY
static __inline__ char *strncpy(char *__dest, __const__ char *__src, size_t __n)
{
  char *__xdest = __dest;

  if (__n == 0)
    return __xdest;

  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t$1,(%1)\n\t"
	"subu\t%2,1\n\t"
	"sb\t$1,(%0)\n\t"
	"beqz\t$1,2f\n\t"
	"addiu\t%0,1\n\t"
	"bnez\t%2,1b\n\t"
	"addiu\t%1,1\n"
	"2:\n\t"
	".set\tat\n\t"
	".set\treorder"
        : "=r" (__dest), "=r" (__src), "=r" (__n)
        : "0" (__dest), "1" (__src), "2" (__n)
        : "memory");

  return __xdest;
}

#define __HAVE_ARCH_STRCMP
static __inline__ int strcmp(__const__ char *__cs, __const__ char *__ct)
{
  int __res;

  __asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n\t"
	"lbu\t%2,(%0)\n"
	"1:\tlbu\t$1,(%1)\n\t"
	"addiu\t%0,1\n\t"
	"bne\t$1,%2,2f\n\t"
	"addiu\t%1,1\n\t"
	"bnez\t%2,1b\n\t"
	"lbu\t%2,(%0)\n\t"
#if defined(CONFIG_CPU_R3000)
	"nop\n\t"
#endif
	"move\t%2,$1\n"
	"2:\tsubu\t%2,$1\n"
	"3:\t.set\tat\n\t"
	".set\treorder"
	: "=r" (__cs), "=r" (__ct), "=r" (__res)
	: "0" (__cs), "1" (__ct));

  return __res;
}

#endif /* !defined(IN_STRING_C) */

#define __HAVE_ARCH_STRNCMP
static __inline__ int
strncmp(__const__ char *__cs, __const__ char *__ct, size_t __count)
{
	int __res;

	__asm__ __volatile__(
	".set\tnoreorder\n\t"
	".set\tnoat\n"
	"1:\tlbu\t%3,(%0)\n\t"
	"beqz\t%2,2f\n\t"
	"lbu\t$1,(%1)\n\t"
	"subu\t%2,1\n\t"
	"bne\t$1,%3,3f\n\t"
	"addiu\t%0,1\n\t"
	"bnez\t%3,1b\n\t"
	"addiu\t%1,1\n"
	"2:\n\t"
#if defined(CONFIG_CPU_R3000)
	"nop\n\t"
#endif
	"move\t%3,$1\n"
	"3:\tsubu\t%3,$1\n\t"
	".set\tat\n\t"
	".set\treorder"
	: "=r" (__cs), "=r" (__ct), "=r" (__count), "=r" (__res)
	: "0" (__cs), "1" (__ct), "2" (__count));

	return __res;
}
#endif /* CONFIG_MIPS32 */

#define __HAVE_ARCH_MEMSET
extern void *memset(void *__s, int __c, size_t __count);

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *__to, __const__ void *__from, size_t __n);

#define __HAVE_ARCH_MEMMOVE
extern void *memmove(void *__dest, __const__ void *__src, size_t __n);

#ifdef CONFIG_MIPS32
#define __HAVE_ARCH_MEMSCAN
static __inline__ void *memscan(void *__addr, int __c, size_t __size)
{
	char *__end = (char *)__addr + __size;
	unsigned char __uc = (unsigned char) __c;

	__asm__(".set\tpush\n\t"
		".set\tnoat\n\t"
		".set\treorder\n\t"
		"1:\tbeq\t%0,%1,2f\n\t"
		"addiu\t%0,1\n\t"
		"lbu\t$1,-1(%0)\n\t"
		"bne\t$1,%z4,1b\n"
		"2:\t.set\tpop"
		: "=r" (__addr), "=r" (__end)
		: "0" (__addr), "1" (__end), "Jr" (__uc));

	return __addr;
}
#endif /* CONFIG_MIPS32 */

#endif /* _ASM_STRING_H */
