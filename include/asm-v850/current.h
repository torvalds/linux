/*
 * include/asm-v850/current.h -- Current task
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_CURRENT_H__
#define __V850_CURRENT_H__

#ifndef __ASSEMBLY__ /* <linux/thread_info.h> is not asm-safe.  */
#include <linux/thread_info.h>
#endif

#include <asm/macrology.h>


/* Register used to hold the current task pointer while in the kernel.
   Any `call clobbered' register without a special meaning should be OK,
   but check asm/v850/kernel/entry.S to be sure.  */
#define CURRENT_TASK_REGNUM	16
#define CURRENT_TASK 		macrology_paste (r, CURRENT_TASK_REGNUM)


#ifdef __ASSEMBLY__

/* Put a pointer to the current task structure into REG.  */
#define GET_CURRENT_TASK(reg)						\
	GET_CURRENT_THREAD(reg);					\
	ld.w	TI_TASK[reg], reg

#else /* !__ASSEMBLY__ */

/* A pointer to the current task.  */
register struct task_struct *current					\
   __asm__ (macrology_stringify (CURRENT_TASK));

#endif /* __ASSEMBLY__ */


#endif /* _V850_CURRENT_H */
