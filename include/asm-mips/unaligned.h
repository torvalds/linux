/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 Ralf Baechle (ralf@linux-mips.org)
 */
#ifndef __ASM_GENERIC_UNALIGNED_H
#define __ASM_GENERIC_UNALIGNED_H

#include <linux/compiler.h>

#define get_unaligned(ptr)					\
({								\
	struct __packed {					\
		typeof(*(ptr)) __v;				\
	} *__p = (void *) (ptr);				\
	__p->__v;						\
})

#define put_unaligned(val, ptr)					\
do {								\
	struct __packed {					\
		typeof(*(ptr)) __v;				\
	} *__p = (void *) (ptr);				\
	__p->__v = (val);					\
} while(0)

#endif /* __ASM_GENERIC_UNALIGNED_H */
