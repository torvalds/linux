/* ===-- int_lib.h - configuration header for compiler-rt  -----------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file is a compat header for compiler-rt source files used in the OpenBSD
 * kernel.
 * This file is not part of the interface of this library.
 *
 * ===----------------------------------------------------------------------===
 */

#ifndef _CRT_INT_LIB_H_
#define  _CRT_INT_LIB_H_

#include <sys/limits.h>
#include <sys/endian.h>

typedef int      si_int;
typedef unsigned int su_int;
typedef          long long di_int;
typedef unsigned long long du_int;
typedef int      ti_int __attribute__ ((mode (TI)));
typedef int      tu_int __attribute__ ((mode (TI)));

#if BYTE_ORDER == LITTLE_ENDIAN
#define _YUGA_LITTLE_ENDIAN 0
#else
#define _YUGA_LITTLE_ENDIAN 1
#endif

typedef union
{
    ti_int all;
    struct
    {
#if _YUGA_LITTLE_ENDIAN
        du_int low;
        di_int high;
#else
        di_int high;
        du_int low;
#endif /* _YUGA_LITTLE_ENDIAN */
    }s;
} twords;

typedef union
{
	tu_int all;
	struct
	{
#if _YUGA_LITTLE_ENDIAN
		du_int low;
		du_int high;
#else
		du_int high;
		du_int low;
#endif /* _YUGA_LITTLE_ENDIAN */
	}s;
} utwords;

#endif /* _CRT_INT_LIB_H_ */
