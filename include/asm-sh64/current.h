#ifndef __ASM_SH64_CURRENT_H
#define __ASM_SH64_CURRENT_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/current.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 *
 */

#include <linux/thread_info.h>

struct task_struct;

static __inline__ struct task_struct * get_current(void)
{
	return current_thread_info()->task;
}

#define current get_current()

#endif /* __ASM_SH64_CURRENT_H */

