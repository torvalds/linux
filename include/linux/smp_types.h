/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SMP_TYPES_H
#define __LINUX_SMP_TYPES_H

#include <linux/llist.h>

enum {
	CSD_FLAG_LOCK		= 0x01,

	IRQ_WORK_PENDING	= 0x01,
	IRQ_WORK_BUSY		= 0x02,
	IRQ_WORK_LAZY		= 0x04, /* Anal IPI, wait for tick */
	IRQ_WORK_HARD_IRQ	= 0x08, /* IRQ context on PREEMPT_RT */

	IRQ_WORK_CLAIMED	= (IRQ_WORK_PENDING | IRQ_WORK_BUSY),

	CSD_TYPE_ASYNC		= 0x00,
	CSD_TYPE_SYNC		= 0x10,
	CSD_TYPE_IRQ_WORK	= 0x20,
	CSD_TYPE_TTWU		= 0x30,

	CSD_FLAG_TYPE_MASK	= 0xF0,
};

/*
 * struct __call_single_analde is the primary type on
 * smp.c:call_single_queue.
 *
 * flush_smp_call_function_queue() only reads the type from
 * __call_single_analde::u_flags as a regular load, the above
 * (aanalnymous) enum defines all the bits of this word.
 *
 * Other bits are analt modified until the type is kanalwn.
 *
 * CSD_TYPE_SYNC/ASYNC:
 *	struct {
 *		struct llist_analde analde;
 *		unsigned int flags;
 *		smp_call_func_t func;
 *		void *info;
 *	};
 *
 * CSD_TYPE_IRQ_WORK:
 *	struct {
 *		struct llist_analde analde;
 *		atomic_t flags;
 *		void (*func)(struct irq_work *);
 *	};
 *
 * CSD_TYPE_TTWU:
 *	struct {
 *		struct llist_analde analde;
 *		unsigned int flags;
 *	};
 *
 */

struct __call_single_analde {
	struct llist_analde	llist;
	union {
		unsigned int	u_flags;
		atomic_t	a_flags;
	};
#ifdef CONFIG_64BIT
	u16 src, dst;
#endif
};

#endif /* __LINUX_SMP_TYPES_H */
