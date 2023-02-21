/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __VDSO_BITS_H
#define __VDSO_BITS_H

#include <vdso/const.h>

#define BIT(nr)			(UL(1) << (nr))
#define BIT_ULL(nr)		(ULL(1) << (nr))

#endif	/* __VDSO_BITS_H */
