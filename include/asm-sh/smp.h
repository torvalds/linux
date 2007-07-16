/*
 * include/asm-sh/smp.h
 *
 * Copyright (C) 2002, 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive for
 * more details.
 */
#ifndef __ASM_SH_SMP_H
#define __ASM_SH_SMP_H

#include <linux/bitops.h>
#include <linux/cpumask.h>

#ifdef CONFIG_SMP

#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <asm/current.h>

#define raw_smp_processor_id()	(current_thread_info()->cpu)

/* I've no idea what the real meaning of this is */
#define PROC_CHANGE_PENALTY	20

#define NO_PROC_ID	(-1)

struct smp_fn_call_struct {
	spinlock_t lock;
	atomic_t   finished;
	void (*fn)(void *);
	void *data;
};

extern struct smp_fn_call_struct smp_fn_call;

#define SMP_MSG_RESCHEDULE	0x0001

#endif /* CONFIG_SMP */

#define hard_smp_processor_id()	(0)

#endif /* __ASM_SH_SMP_H */
