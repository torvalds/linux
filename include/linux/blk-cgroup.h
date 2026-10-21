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

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/types.h>

struct bio;
struct cgroup_subsys_state;
struct gendisk;

#define FC_APPID_LEN              129

#ifdef CONFIG_BLK_CGROUP
extern struct cgroup_subsys_state * const blkcg_root_css;
extern atomic_t blkcg_nr_congested;

void blkcg_schedule_throttle(struct gendisk *disk, bool use_memdelay);
void blkcg_maybe_throttle_current(void);
bool __blk_cgroup_congested(void);

/**
 * blk_cgroup_congested - is the current task in a throttled blkcg?
 *
 * Called from mm hot paths where the answer is almost always false, so keep
 * that case to a load and a branch and only walk the hierarchy out of line
 * when something in the system really is throttled.
 *
 * Return: %true if the current task's blkcg or any of its ancestors is
 * throttled, %false otherwise.
 */
static inline bool blk_cgroup_congested(void)
{
	if (likely(!atomic_read(&blkcg_nr_congested)))
		return false;
	return __blk_cgroup_congested();
}

void blkcg_pin_online(struct cgroup_subsys_state *blkcg_css);
void blkcg_unpin_online(struct cgroup_subsys_state *blkcg_css);
struct list_head *blkcg_get_cgwb_list(struct cgroup_subsys_state *css);
struct cgroup_subsys_state *bio_blkcg_css(struct bio *bio);

#else	/* CONFIG_BLK_CGROUP */

#define blkcg_root_css	((struct cgroup_subsys_state *)ERR_PTR(-EINVAL))

static inline void blkcg_maybe_throttle_current(void) { }
static inline bool blk_cgroup_congested(void) { return false; }
static inline struct cgroup_subsys_state *bio_blkcg_css(struct bio *bio)
{
	return NULL;
}
#endif	/* CONFIG_BLK_CGROUP */

int blkcg_set_fc_appid(char *app_id, u64 cgrp_id, size_t app_id_len);
char *blkcg_get_fc_appid(struct bio *bio);

#endif	/* _BLK_CGROUP_H */
