/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_VMPRESSURE_H
#define __LINUX_VMPRESSURE_H

#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/cgroup.h>
#include <linux/eventfd.h>

struct vmpressure {
	unsigned long scanned;
	unsigned long reclaimed;
	/* The lock is used to keep the scanned/reclaimed in sync. */
	spinlock_t sr_lock;

#ifdef CONFIG_MEMCG_V1
	/*
	 * tree=true accumulators feed the v1 userspace eventfd interface
	 * (memory.pressure_level). Drained by @work. v2 has no equivalent
	 * interface, so this state is omitted on CONFIG_MEMCG_V1=n builds.
	 */
	unsigned long tree_scanned;
	unsigned long tree_reclaimed;
	/* The list of vmpressure_event structs. */
	struct list_head events;
	/* Have to grab the lock on events traversal or modifications. */
	struct mutex events_lock;

	struct work_struct work;
#endif
};

enum vmpressure_levels {
	VMPRESSURE_LOW = 0,
	VMPRESSURE_MEDIUM,
	VMPRESSURE_CRITICAL,
	VMPRESSURE_NUM_LEVELS,
};

struct mem_cgroup;

#ifdef CONFIG_MEMCG
void vmpressure(gfp_t gfp, int order, struct mem_cgroup *memcg, bool tree,
		unsigned long scanned, unsigned long reclaimed);
extern void vmpressure_init(struct vmpressure *vmpr);
extern void vmpressure_cleanup(struct vmpressure *vmpr);
extern struct vmpressure *memcg_to_vmpressure(struct mem_cgroup *memcg);
extern struct mem_cgroup *vmpressure_to_memcg(struct vmpressure *vmpr);

/* Shared with the v1 vmpressure block in mm/memcontrol-v1.c. */
extern const unsigned long vmpressure_win;
extern enum vmpressure_levels vmpressure_calc_level(unsigned long scanned,
						    unsigned long reclaimed);

#ifdef CONFIG_MEMCG_V1
extern void vmpressure_prio(gfp_t gfp, struct mem_cgroup *memcg, int prio);
extern int vmpressure_register_event(struct mem_cgroup *memcg,
				     struct eventfd_ctx *eventfd,
				     const char *args);
extern void vmpressure_unregister_event(struct mem_cgroup *memcg,
					struct eventfd_ctx *eventfd);

/* v1 hooks called from mm/vmpressure.c; no-ops below when !MEMCG_V1. */
extern void vmpressure_v1_init(struct vmpressure *vmpr);
extern void vmpressure_v1_cleanup(struct vmpressure *vmpr);
extern void vmpressure_v1_account_tree(struct vmpressure *vmpr,
				       unsigned long scanned,
				       unsigned long reclaimed);
#else
static inline void vmpressure_prio(gfp_t gfp, struct mem_cgroup *memcg,
				   int prio) {}
static inline void vmpressure_v1_init(struct vmpressure *vmpr) {}
static inline void vmpressure_v1_cleanup(struct vmpressure *vmpr) {}
static inline void vmpressure_v1_account_tree(struct vmpressure *vmpr,
					      unsigned long scanned,
					      unsigned long reclaimed) {}
#endif /* CONFIG_MEMCG_V1 */

#else /* !CONFIG_MEMCG */
static inline void vmpressure(gfp_t gfp, int order, struct mem_cgroup *memcg,
			      bool tree, unsigned long scanned,
			      unsigned long reclaimed) {}
static inline void vmpressure_prio(gfp_t gfp, struct mem_cgroup *memcg,
				   int prio) {}
#endif /* CONFIG_MEMCG */
#endif /* __LINUX_VMPRESSURE_H */
