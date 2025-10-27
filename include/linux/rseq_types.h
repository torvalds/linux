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
 * @sched_switch:	True if the task was scheduled and needs update on
 *			exit to user
 * @ids_changed:	Indicator that IDs need to be updated
 * @user_irq:		True on interrupt entry from user mode
 * @has_rseq:		True if the task has a rseq pointer installed
 * @error:		Compound error code for the slow path to analyze
 * @fatal:		User space data corrupted or invalid
 * @slowpath:		Indicator that slow path processing via TIF_NOTIFY_RESUME
 *			is required
 *
 * @sched_switch and @ids_changed must be adjacent and the combo must be
 * 16bit aligned to allow a single store, when both are set at the same
 * time in the scheduler.
 */
struct rseq_event {
	union {
		u64				all;
		struct {
			union {
				u32		events;
				struct {
					u8	sched_switch;
					u8	ids_changed;
					u8	user_irq;
				};
			};

			u8			has_rseq;
			u8			__pad;
			union {
				u16		error;
				struct {
					u8	fatal;
					u8	slowpath;
				};
			};
		};
	};
};

/**
 * struct rseq_ids - Cache for ids, which need to be updated
 * @cpu_cid:	Compound of @cpu_id and @mm_cid to make the
 *		compiler emit a single compare on 64-bit
 * @cpu_id:	The CPU ID which was written last to user space
 * @mm_cid:	The MM CID which was written last to user space
 *
 * @cpu_id and @mm_cid are updated when the data is written to user space.
 */
struct rseq_ids {
	union {
		u64		cpu_cid;
		struct {
			u32	cpu_id;
			u32	mm_cid;
		};
	};
};

/**
 * struct rseq_data - Storage for all rseq related data
 * @usrptr:	Pointer to the registered user space RSEQ memory
 * @len:	Length of the RSEQ region
 * @sig:	Signature of critial section abort IPs
 * @event:	Storage for event management
 * @ids:	Storage for cached CPU ID and MM CID
 */
struct rseq_data {
	struct rseq __user		*usrptr;
	u32				len;
	u32				sig;
	struct rseq_event		event;
	struct rseq_ids			ids;
};

#else /* CONFIG_RSEQ */
struct rseq_data { };
#endif /* !CONFIG_RSEQ */

#endif
