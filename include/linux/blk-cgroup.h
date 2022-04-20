/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BLK_CGROUP_H
#define _BLK_CGROUP_H
/*
 * Common Block IO controller cgroup interface
 *
 * Based on ideas and code from CFQ, CFS and BFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2009 Vivek Goyal <vgoyal@redhat.com>
 * 	              Nauman Rafique <nauman@google.com>
 */

#include <linux/cgroup.h>
#include <linux/percpu.h>
#include <linux/percpu_counter.h>
#include <linux/u64_stats_sync.h>
#include <linux/seq_file.h>
#include <linux/radix-tree.h>
#include <linux/blkdev.h>
#include <linux/atomic.h>
#include <linux/kthread.h>
#include <linux/fs.h>

#define FC_APPID_LEN              129

#ifdef CONFIG_BLK_CGROUP

enum blkg_iostat_type {
	BLKG_IOSTAT_READ,
	BLKG_IOSTAT_WRITE,
	BLKG_IOSTAT_DISCARD,

	BLKG_IOSTAT_NR,
};

struct blkcg_gq;
struct blkg_policy_data;

struct blkcg {
	struct cgroup_subsys_state	css;
	spinlock_t			lock;
	refcount_t			online_pin;

	struct radix_tree_root		blkg_tree;
	struct blkcg_gq	__rcu		*blkg_hint;
	struct hlist_head		blkg_list;

	struct blkcg_policy_data	*cpd[BLKCG_MAX_POLS];

	struct list_head		all_blkcgs_node;
#ifdef CONFIG_BLK_CGROUP_FC_APPID
	char                            fc_app_id[FC_APPID_LEN];
#endif
#ifdef CONFIG_CGROUP_WRITEBACK
	struct list_head		cgwb_list;
#endif
};

struct blkg_iostat {
	u64				bytes[BLKG_IOSTAT_NR];
	u64				ios[BLKG_IOSTAT_NR];
};

struct blkg_iostat_set {
	struct u64_stats_sync		sync;
	struct blkg_iostat		cur;
	struct blkg_iostat		last;
};

/* association between a blk cgroup and a request queue */
struct blkcg_gq {
	/* Pointer to the associated request_queue */
	struct request_queue		*q;
	struct list_head		q_node;
	struct hlist_node		blkcg_node;
	struct blkcg			*blkcg;

	/* all non-root blkcg_gq's are guaranteed to have access to parent */
	struct blkcg_gq			*parent;

	/* reference count */
	struct percpu_ref		refcnt;

	/* is this blkg online? protected by both blkcg and q locks */
	bool				online;

	struct blkg_iostat_set __percpu	*iostat_cpu;
	struct blkg_iostat_set		iostat;

	struct blkg_policy_data		*pd[BLKCG_MAX_POLS];

	spinlock_t			async_bio_lock;
	struct bio_list			async_bios;
	union {
		struct work_struct	async_bio_work;
		struct work_struct	free_work;
	};

	atomic_t			use_delay;
	atomic64_t			delay_nsec;
	atomic64_t			delay_start;
	u64				last_delay;
	int				last_use;

	struct rcu_head			rcu_head;
};

extern struct cgroup_subsys_state * const blkcg_root_css;

void blkcg_destroy_blkgs(struct blkcg *blkcg);
void blkcg_schedule_throttle(struct request_queue *q, bool use_memdelay);
void blkcg_maybe_throttle_current(void);

static inline struct blkcg *css_to_blkcg(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct blkcg, css) : NULL;
}

/**
 * bio_blkcg - grab the blkcg associated with a bio
 * @bio: target bio
 *
 * This returns the blkcg associated with a bio, %NULL if not associated.
 * Callers are expected to either handle %NULL or know association has been
 * done prior to calling this.
 */
static inline struct blkcg *bio_blkcg(struct bio *bio)
{
	if (bio && bio->bi_blkg)
		return bio->bi_blkg->blkcg;
	return NULL;
}

static inline bool blk_cgroup_congested(void)
{
	struct cgroup_subsys_state *css;
	bool ret = false;

	rcu_read_lock();
	css = kthread_blkcg();
	if (!css)
		css = task_css(current, io_cgrp_id);
	while (css) {
		if (atomic_read(&css->cgroup->congestion_count)) {
			ret = true;
			break;
		}
		css = css->parent;
	}
	rcu_read_unlock();
	return ret;
}

/**
 * blkcg_parent - get the parent of a blkcg
 * @blkcg: blkcg of interest
 *
 * Return the parent blkcg of @blkcg.  Can be called anytime.
 */
static inline struct blkcg *blkcg_parent(struct blkcg *blkcg)
{
	return css_to_blkcg(blkcg->css.parent);
}

/**
 * blkcg_pin_online - pin online state
 * @blkcg: blkcg of interest
 *
 * While pinned, a blkcg is kept online.  This is primarily used to
 * impedance-match blkg and cgwb lifetimes so that blkg doesn't go offline
 * while an associated cgwb is still active.
 */
static inline void blkcg_pin_online(struct blkcg *blkcg)
{
	refcount_inc(&blkcg->online_pin);
}

/**
 * blkcg_unpin_online - unpin online state
 * @blkcg: blkcg of interest
 *
 * This is primarily used to impedance-match blkg and cgwb lifetimes so
 * that blkg doesn't go offline while an associated cgwb is still active.
 * When this count goes to zero, all active cgwbs have finished so the
 * blkcg can continue destruction by calling blkcg_destroy_blkgs().
 */
static inline void blkcg_unpin_online(struct blkcg *blkcg)
{
	do {
		if (!refcount_dec_and_test(&blkcg->online_pin))
			break;
		blkcg_destroy_blkgs(blkcg);
		blkcg = blkcg_parent(blkcg);
	} while (blkcg);
}

#else	/* CONFIG_BLK_CGROUP */

struct blkcg {
};

struct blkcg_gq {
};

#define blkcg_root_css	((struct cgroup_subsys_state *)ERR_PTR(-EINVAL))

static inline void blkcg_maybe_throttle_current(void) { }
static inline bool blk_cgroup_congested(void) { return false; }

#ifdef CONFIG_BLOCK
static inline void blkcg_schedule_throttle(struct request_queue *q, bool use_memdelay) { }
static inline struct blkcg *bio_blkcg(struct bio *bio) { return NULL; }
#endif /* CONFIG_BLOCK */

#endif	/* CONFIG_BLK_CGROUP */

int blkcg_set_fc_appid(char *app_id, u64 cgrp_id, size_t app_id_len);
char *blkcg_get_fc_appid(struct bio *bio);

#endif	/* _BLK_CGROUP_H */
