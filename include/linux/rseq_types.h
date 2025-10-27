/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RSEQ_TYPES_H
#define _LINUX_RSEQ_TYPES_H

#include <linux/types.h>

#ifdef CONFIG_RSEQ
struct rseq;

/**
 * struct rseq_event - Storage for rseq related event management
 * @all:		Compound to initialize and clear the data efficiently
 * @events:		Compound to access events with a single load/store
 * @sched_switch:	True if the task was scheduled out
 * @has_rseq:		True if the task has a rseq pointer installed
 */
struct rseq_event {
	union {
		u32				all;
		struct {
			union {
				u16		events;
				struct {
					u8	sched_switch;
				};
			};

			u8			has_rseq;
		};
	};
};

/**
 * struct rseq_data - Storage for all rseq related data
 * @usrptr:	Pointer to the registered user space RSEQ memory
 * @len:	Length of the RSEQ region
 * @sig:	Signature of critial section abort IPs
 * @event:	Storage for event management
 */
struct rseq_data {
	struct rseq __user		*usrptr;
	u32				len;
	u32				sig;
	struct rseq_event		event;
};

#else /* CONFIG_RSEQ */
struct rseq_data { };
#endif /* !CONFIG_RSEQ */

#endif
