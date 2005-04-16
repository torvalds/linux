/* segment.h: MMU segment settings
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

#include <linux/config.h>

#ifndef __ASSEMBLY__

typedef struct {
	unsigned long seg;
} mm_segment_t;

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })

#define KERNEL_DS		MAKE_MM_SEG(0xdfffffffUL)

#ifdef CONFIG_MMU
#define USER_DS			MAKE_MM_SEG(TASK_SIZE - 1)
#else
#define USER_DS			KERNEL_DS
#endif

#define get_ds()		(KERNEL_DS)
#define get_fs()		(__current_thread_info->addr_limit)
#define segment_eq(a,b)		((a).seg == (b).seg)
#define __kernel_ds_p()		segment_eq(get_fs(), KERNEL_DS)
#define get_addr_limit()	(get_fs().seg)

#define set_fs(_x)					\
do {							\
	__current_thread_info->addr_limit = (_x);	\
} while(0)


#endif /* __ASSEMBLY__ */
#endif /* _ASM_SEGMENT_H */
