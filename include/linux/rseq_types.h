/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RSEQ_TYPES_H
#define _LINUX_RSEQ_TYPES_H

#include <linux/irq_work_types.h>
#include <linux/types.h>
#include <linux/workqueue_types.h>

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

#ifdef CONFIG_SCHED_MM_CID

#define MM_CID_UNSET	BIT(31)
#define MM_CID_ONCPU	BIT(30)
#define MM_CID_TRANSIT	BIT(29)

/**
 * struct sched_mm_cid - Storage for per task MM CID data
 * @active:	MM CID is active for the task
 * @cid:	The CID associated to the task either permanently or
 *		borrowed from the CPU
 */
struct sched_mm_cid {
	unsigned int		active;
	unsigned int		cid;
};

/**
 * struct mm_cid_pcpu - Storage for per CPU MM_CID data
 * @cid:	The CID associated to the CPU either permanently or
 *		while a task with a CID is running
 */
struct mm_cid_pcpu {
	unsigned int	cid;
}____cacheline_aligned_in_smp;

/**
 * struct mm_mm_cid - Storage for per MM CID data
 * @pcpu:		Per CPU storage for CIDs associated to a CPU
 * @percpu:		Set, when CIDs are in per CPU mode
 * @transit:		Set to MM_CID_TRANSIT during a mode change transition phase
 * @max_cids:		The exclusive maximum CID value for allocation and convergence
 * @irq_work:		irq_work to handle the affinity mode change case
 * @work:		Regular work to handle the affinity mode change case
 * @lock:		Spinlock to protect against affinity setting which can't take @mutex
 * @mutex:		Mutex to serialize forks and exits related to this mm
 * @nr_cpus_allowed:	The number of CPUs in the per MM allowed CPUs map. The map
 *			is growth only.
 * @users:		The number of tasks sharing this MM. Separate from mm::mm_users
 *			as that is modified by mmget()/mm_put() by other entities which
 *			do not actually share the MM.
 * @pcpu_thrs:		Threshold for switching back from per CPU mode
 * @update_deferred:	A deferred switch back to per task mode is pending.
 */
struct mm_mm_cid {
	/* Hotpath read mostly members */
	struct mm_cid_pcpu	__percpu *pcpu;
	unsigned int		percpu;
	unsigned int		transit;
	unsigned int		max_cids;

	/* Rarely used. Moves @lock and @mutex into the second cacheline */
	struct irq_work		irq_work;
	struct work_struct	work;

	raw_spinlock_t		lock;
	struct mutex		mutex;

	/* Low frequency modified */
	unsigned int		nr_cpus_allowed;
	unsigned int		users;
	unsigned int		pcpu_thrs;
	unsigned int		update_deferred;
}____cacheline_aligned_in_smp;
#else /* CONFIG_SCHED_MM_CID */
struct mm_mm_cid { };
struct sched_mm_cid { };
#endif /* !CONFIG_SCHED_MM_CID */

#endif
