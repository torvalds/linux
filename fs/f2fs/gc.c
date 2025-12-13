// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/gc.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/f2fs_fs.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/sched/signal.h>
#include <linux/random.h>
#include <linux/sched/mm.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "gc.h"
#include "iostat.h"
#include <trace/events/f2fs.h>

static struct kmem_cache *victim_entry_slab;

static unsigned int count_bits(const unsigned long *addr,
				unsigned int offset, unsigned int len);

static int gc_thread_func(void *data)
{
	struct f2fs_sb_info *sbi = data;
	struct f2fs_gc_kthread *gc_th = sbi->gc_thread;
	wait_queue_head_t *wq = &sbi->gc_thread->gc_wait_queue_head;
	wait_queue_head_t *fggc_wq = &sbi->gc_thread->fggc_wq;
	unsigned int wait_ms;
	struct f2fs_gc_control gc_control = {
		.victim_segno = NULL_SEGNO,
		.should_migrate_blocks = false,
		.err_gc_skipped = false };

	wait_ms = gc_th->min_sleep_time;

	set_freezable();
	do {
		bool sync_mode, foreground = false;

		wait_event_freezable_timeout(*wq,
				kthread_should_stop() ||
				waitqueue_active(fggc_wq) ||
				gc_th->gc_wake,
				msecs_to_jiffies(wait_ms));

		if (test_opt(sbi, GC_MERGE) && waitqueue_active(fggc_wq))
			foreground = true;

		/* give it a try one time */
		if (gc_th->gc_wake)
			gc_th->gc_wake = false;

		if (f2fs_readonly(sbi->sb)) {
			stat_other_skip_bggc_count(sbi);
			continue;
		}
		if (kthread_should_stop())
			break;

		if (sbi->sb->s_writers.frozen >= SB_FREEZE_WRITE) {
			increase_sleep_time(gc_th, &wait_ms);
			stat_other_skip_bggc_count(sbi);
			continue;
		}

		if (time_to_inject(sbi, FAULT_CHECKPOINT))
			f2fs_stop_checkpoint(sbi, false,
					STOP_CP_REASON_FAULT_INJECT);

		if (!sb_start_write_trylock(sbi->sb)) {
			stat_other_skip_bggc_count(sbi);
			continue;
		}

		gc_control.one_time = false;

		/*
		 * [GC triggering condition]
		 * 0. GC is not conducted currently.
		 * 1. There are enough dirty segments.
		 * 2. IO subsystem is idle by checking the # of writeback pages.
		 * 3. IO subsystem is idle by checking the # of requests in
		 *    bdev's request list.
		 *
		 * Note) We have to avoid triggering GCs frequently.
		 * Because it is possible that some segments can be
		 * invalidated soon after by user update or deletion.
		 * So, I'd like to wait some time to collect dirty segments.
		 */
		if (sbi->gc_mode == GC_URGENT_HIGH ||
				sbi->gc_mode == GC_URGENT_MID) {
			wait_ms = gc_th->urgent_sleep_time;
			f2fs_down_write(&sbi->gc_lock);
			goto do_gc;
		}

		if (foreground) {
			f2fs_down_write(&sbi->gc_lock);
			goto do_gc;
		} else if (!f2fs_down_write_trylock(&sbi->gc_lock)) {
			stat_other_skip_bggc_count(sbi);
			goto next;
		}

		if (!is_idle(sbi, GC_TIME)) {
			increase_sleep_time(gc_th, &wait_ms);
			f2fs_up_write(&sbi->gc_lock);
			stat_io_skip_bggc_count(sbi);
			goto next;
		}

		if (f2fs_sb_has_blkzoned(sbi)) {
			if (has_enough_free_blocks(sbi,
				gc_th->no_zoned_gc_percent)) {
				wait_ms = gc_th->no_gc_sleep_time;
				f2fs_up_write(&sbi->gc_lock);
				goto next;
			}
			if (wait_ms == gc_th->no_gc_sleep_time)
				wait_ms = gc_th->max_sleep_time;
		}

		if (need_to_boost_gc(sbi)) {
			decrease_sleep_time(gc_th, &wait_ms);
			if (f2fs_sb_has_blkzoned(sbi))
				gc_control.one_time = true;
		} else {
			increase_sleep_time(gc_th, &wait_ms);
		}
do_gc:
		stat_inc_gc_call_count(sbi, foreground ?
					FOREGROUND : BACKGROUND);

		sync_mode = (F2FS_OPTION(sbi).bggc_mode == BGGC_MODE_SYNC) ||
			(gc_control.one_time && gc_th->boost_gc_greedy);

		/* foreground GC was been triggered via f2fs_balance_fs() */
		if (foreground && !f2fs_sb_has_blkzoned(sbi))
			sync_mode = false;

		gc_control.init_gc_type = sync_mode ? FG_GC : BG_GC;
		gc_control.no_bg_gc = foreground;
		gc_control.nr_free_secs = foreground ? 1 : 0;

		/* if return value is not zero, no victim was selected */
		if (f2fs_gc(sbi, &gc_control)) {
			/* don't bother wait_ms by foreground gc */
			if (!foreground)
				wait_ms = gc_th->no_gc_sleep_time;
		} else {
			/* reset wait_ms to default sleep time */
			if (wait_ms == gc_th->no_gc_sleep_time)
				wait_ms = gc_th->min_sleep_time;
		}

		if (foreground)
			wake_up_all(&gc_th->fggc_wq);

		trace_f2fs_background_gc(sbi->sb, wait_ms,
				prefree_segments(sbi), free_segments(sbi));

		/* balancing f2fs's metadata periodically */
		f2fs_balance_fs_bg(sbi, true);
next:
		if (sbi->gc_mode != GC_NORMAL) {
			spin_lock(&sbi->gc_remaining_trials_lock);
			if (sbi->gc_remaining_trials) {
				sbi->gc_remaining_trials--;
				if (!sbi->gc_remaining_trials)
					sbi->gc_mode = GC_NORMAL;
			}
			spin_unlock(&sbi->gc_remaining_trials_lock);
		}
		sb_end_write(sbi->sb);

	} while (!kthread_should_stop());
	return 0;
}

int f2fs_start_gc_thread(struct f2fs_sb_info *sbi)
{
	struct f2fs_gc_kthread *gc_th;
	dev_t dev = sbi->sb->s_bdev->bd_dev;

	gc_th = f2fs_kmalloc(sbi, sizeof(struct f2fs_gc_kthread), GFP_KERNEL);
	if (!gc_th)
		return -ENOMEM;

	gc_th->urgent_sleep_time = DEF_GC_THREAD_URGENT_SLEEP_TIME;
	gc_th->valid_thresh_ratio = DEF_GC_THREAD_VALID_THRESH_RATIO;
	gc_th->boost_gc_multiple = BOOST_GC_MULTIPLE;
	gc_th->boost_gc_greedy = GC_GREEDY;

	if (f2fs_sb_has_blkzoned(sbi)) {
		gc_th->min_sleep_time = DEF_GC_THREAD_MIN_SLEEP_TIME_ZONED;
		gc_th->max_sleep_time = DEF_GC_THREAD_MAX_SLEEP_TIME_ZONED;
		gc_th->no_gc_sleep_time = DEF_GC_THREAD_NOGC_SLEEP_TIME_ZONED;
		gc_th->no_zoned_gc_percent = LIMIT_NO_ZONED_GC;
		gc_th->boost_zoned_gc_percent = LIMIT_BOOST_ZONED_GC;
	} else {
		gc_th->min_sleep_time = DEF_GC_THREAD_MIN_SLEEP_TIME;
		gc_th->max_sleep_time = DEF_GC_THREAD_MAX_SLEEP_TIME;
		gc_th->no_gc_sleep_time = DEF_GC_THREAD_NOGC_SLEEP_TIME;
		gc_th->no_zoned_gc_percent = 0;
		gc_th->boost_zoned_gc_percent = 0;
	}

	gc_th->gc_wake = false;

	sbi->gc_thread = gc_th;
	init_waitqueue_head(&sbi->gc_thread->gc_wait_queue_head);
	init_waitqueue_head(&sbi->gc_thread->fggc_wq);
	sbi->gc_thread->f2fs_gc_task = kthread_run(gc_thread_func, sbi,
			"f2fs_gc-%u:%u", MAJOR(dev), MINOR(dev));
	if (IS_ERR(gc_th->f2fs_gc_task)) {
		int err = PTR_ERR(gc_th->f2fs_gc_task);

		kfree(gc_th);
		sbi->gc_thread = NULL;
		return err;
	}

	return 0;
}

void f2fs_stop_gc_thread(struct f2fs_sb_info *sbi)
{
	struct f2fs_gc_kthread *gc_th = sbi->gc_thread;

	if (!gc_th)
		return;
	kthread_stop(gc_th->f2fs_gc_task);
	wake_up_all(&gc_th->fggc_wq);
	kfree(gc_th);
	sbi->gc_thread = NULL;
}

static int select_gc_type(struct f2fs_sb_info *sbi, int gc_type)
{
	int gc_mode;

	if (gc_type == BG_GC) {
		if (sbi->am.atgc_enabled)
			gc_mode = GC_AT;
		else
			gc_mode = GC_CB;
	} else {
		gc_mode = GC_GREEDY;
	}

	switch (sbi->gc_mode) {
	case GC_IDLE_CB:
	case GC_URGENT_LOW:
	case GC_URGENT_MID:
		gc_mode = GC_CB;
		break;
	case GC_IDLE_GREEDY:
	case GC_URGENT_HIGH:
		gc_mode = GC_GREEDY;
		break;
	case GC_IDLE_AT:
		gc_mode = GC_AT;
		break;
	}

	return gc_mode;
}

static void select_policy(struct f2fs_sb_info *sbi, int gc_type,
			int type, struct victim_sel_policy *p)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);

	if (p->alloc_mode == SSR || p->alloc_mode == AT_SSR) {
		p->gc_mode = GC_GREEDY;
		p->dirty_bitmap = dirty_i->dirty_segmap[type];
		p->max_search = dirty_i->nr_dirty[type];
		p->ofs_unit = 1;
	} else {
		p->gc_mode = select_gc_type(sbi, gc_type);
		p->ofs_unit = SEGS_PER_SEC(sbi);
		if (__is_large_section(sbi)) {
			p->dirty_bitmap = dirty_i->dirty_secmap;
			p->max_search = count_bits(p->dirty_bitmap,
						0, MAIN_SECS(sbi));
		} else {
			p->dirty_bitmap = dirty_i->dirty_segmap[DIRTY];
			p->max_search = dirty_i->nr_dirty[DIRTY];
		}
	}

	/*
	 * adjust candidates range, should select all dirty segments for
	 * foreground GC and urgent GC cases.
	 */
	if (gc_type != FG_GC &&
			(sbi->gc_mode != GC_URGENT_HIGH) &&
			(p->gc_mode != GC_AT && p->alloc_mode != AT_SSR) &&
			p->max_search > sbi->max_victim_search)
		p->max_search = sbi->max_victim_search;

	/* let's select beginning hot/small space first. */
	if (f2fs_need_rand_seg(sbi))
		p->offset = get_random_u32_below(MAIN_SECS(sbi) *
						SEGS_PER_SEC(sbi));
	else if (type == CURSEG_HOT_DATA || IS_NODESEG(type))
		p->offset = 0;
	else
		p->offset = SIT_I(sbi)->last_victim[p->gc_mode];
}

static unsigned int get_max_cost(struct f2fs_sb_info *sbi,
				struct victim_sel_policy *p)
{
	/* SSR allocates in a segment unit */
	if (p->alloc_mode == SSR)
		return BLKS_PER_SEG(sbi);
	else if (p->alloc_mode == AT_SSR)
		return UINT_MAX;

	/* LFS */
	if (p->gc_mode == GC_GREEDY)
		return SEGS_TO_BLKS(sbi, 2 * p->ofs_unit);
	else if (p->gc_mode == GC_CB)
		return UINT_MAX;
	else if (p->gc_mode == GC_AT)
		return UINT_MAX;
	else /* No other gc_mode */
		return 0;
}

static unsigned int check_bg_victims(struct f2fs_sb_info *sbi)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	unsigned int secno;

	/*
	 * If the gc_type is FG_GC, we can select victim segments
	 * selected by background GC before.
	 * Those segments guarantee they have small valid blocks.
	 */
	for_each_set_bit(secno, dirty_i->victim_secmap, MAIN_SECS(sbi)) {
		if (sec_usage_check(sbi, secno))
			continue;
		clear_bit(secno, dirty_i->victim_secmap);
		return GET_SEG_FROM_SEC(sbi, secno);
	}
	return NULL_SEGNO;
}

static unsigned int get_cb_cost(struct f2fs_sb_info *sbi, unsigned int segno)
{
	struct sit_info *sit_i = SIT_I(sbi);
	unsigned long long mtime = 0;
	unsigned int vblocks;
	unsigned char age = 0;
	unsigned char u;
	unsigned int usable_segs_per_sec = f2fs_usable_segs_in_sec(sbi);

	mtime = f2fs_get_section_mtime(sbi, segno);
	f2fs_bug_on(sbi, mtime == INVALID_MTIME);
	vblocks = get_valid_blocks(sbi, segno, true);
	vblocks = div_u64(vblocks, usable_segs_per_sec);

	u = BLKS_TO_SEGS(sbi, vblocks * 100);

	/* Handle if the system time has changed by the user */
	if (mtime < sit_i->min_mtime)
		sit_i->min_mtime = mtime;
	if (mtime > sit_i->max_mtime)
		sit_i->max_mtime = mtime;
	if (sit_i->max_mtime != sit_i->min_mtime)
		age = 100 - div64_u64(100 * (mtime - sit_i->min_mtime),
				sit_i->max_mtime - sit_i->min_mtime);

	return UINT_MAX - ((100 * (100 - u) * age) / (100 + u));
}

static inline unsigned int get_gc_cost(struct f2fs_sb_info *sbi,
			unsigned int segno, struct victim_sel_policy *p,
			unsigned int valid_thresh_ratio)
{
	if (p->alloc_mode == SSR)
		return get_seg_entry(sbi, segno)->ckpt_valid_blocks;

	if (p->one_time_gc && (valid_thresh_ratio < 100) &&
			(get_valid_blocks(sbi, segno, true) >=
			CAP_BLKS_PER_SEC(sbi) * valid_thresh_ratio / 100))
		return UINT_MAX;

	/* alloc_mode == LFS */
	if (p->gc_mode == GC_GREEDY)
		return get_valid_blocks(sbi, segno, true);
	else if (p->gc_mode == GC_CB)
		return get_cb_cost(sbi, segno);

	f2fs_bug_on(sbi, 1);
	return 0;
}

static unsigned int count_bits(const unsigned long *addr,
				unsigned int offset, unsigned int len)
{
	unsigned int end = offset + len, sum = 0;

	while (offset < end) {
		if (test_bit(offset++, addr))
			++sum;
	}
	return sum;
}

static bool f2fs_check_victim_tree(struct f2fs_sb_info *sbi,
				struct rb_root_cached *root)
{
#ifdef CONFIG_F2FS_CHECK_FS
	struct rb_node *cur = rb_first_cached(root), *next;
	struct victim_entry *cur_ve, *next_ve;

	while (cur) {
		next = rb_next(cur);
		if (!next)
			return true;

		cur_ve = rb_entry(cur, struct victim_entry, rb_node);
		next_ve = rb_entry(next, struct victim_entry, rb_node);

		if (cur_ve->mtime > next_ve->mtime) {
			f2fs_info(sbi, "broken victim_rbtree, "
				"cur_mtime(%llu) next_mtime(%llu)",
				cur_ve->mtime, next_ve->mtime);
			return false;
		}
		cur = next;
	}
#endif
	return true;
}

static struct victim_entry *__lookup_victim_entry(struct f2fs_sb_info *sbi,
					unsigned long long mtime)
{
	struct atgc_management *am = &sbi->am;
	struct rb_node *node = am->root.rb_root.rb_node;
	struct victim_entry *ve = NULL;

	while (node) {
		ve = rb_entry(node, struct victim_entry, rb_node);

		if (mtime < ve->mtime)
			node = node->rb_left;
		else
			node = node->rb_right;
	}
	return ve;
}

static struct victim_entry *__create_victim_entry(struct f2fs_sb_info *sbi,
		unsigned long long mtime, unsigned int segno)
{
	struct atgc_management *am = &sbi->am;
	struct victim_entry *ve;

	ve =  f2fs_kmem_cache_alloc(victim_entry_slab, GFP_NOFS, true, NULL);

	ve->mtime = mtime;
	ve->segno = segno;

	list_add_tail(&ve->list, &am->victim_list);
	am->victim_count++;

	return ve;
}

static void __insert_victim_entry(struct f2fs_sb_info *sbi,
				unsigned long long mtime, unsigned int segno)
{
	struct atgc_management *am = &sbi->am;
	struct rb_root_cached *root = &am->root;
	struct rb_node **p = &root->rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct victim_entry *ve;
	bool left_most = true;

	/* look up rb tree to find parent node */
	while (*p) {
		parent = *p;
		ve = rb_entry(parent, struct victim_entry, rb_node);

		if (mtime < ve->mtime) {
			p = &(*p)->rb_left;
		} else {
			p = &(*p)->rb_right;
			left_most = false;
		}
	}

	ve = __create_victim_entry(sbi, mtime, segno);

	rb_link_node(&ve->rb_node, parent, p);
	rb_insert_color_cached(&ve->rb_node, root, left_most);
}

static void add_victim_entry(struct f2fs_sb_info *sbi,
				struct victim_sel_policy *p, unsigned int segno)
{
	struct sit_info *sit_i = SIT_I(sbi);
	unsigned long long mtime = 0;

	if (unlikely(is_sbi_flag_set(sbi, SBI_CP_DISABLED))) {
		if (p->gc_mode == GC_AT &&
			get_valid_blocks(sbi, segno, true) == 0)
			return;
	}

	mtime = f2fs_get_section_mtime(sbi, segno);
	f2fs_bug_on(sbi, mtime == INVALID_MTIME);

	/* Handle if the system time has changed by the user */
	if (mtime < sit_i->min_mtime)
		sit_i->min_mtime = mtime;
	if (mtime > sit_i->max_mtime)
		sit_i->max_mtime = mtime;
	if (mtime < sit_i->dirty_min_mtime)
		sit_i->dirty_min_mtime = mtime;
	if (mtime > sit_i->dirty_max_mtime)
		sit_i->dirty_max_mtime = mtime;

	/* don't choose young section as candidate */
	if (sit_i->dirty_max_mtime - mtime < p->age_threshold)
		return;

	__insert_victim_entry(sbi, mtime, segno);
}

static void atgc_lookup_victim(struct f2fs_sb_info *sbi,
						struct victim_sel_policy *p)
{
	struct sit_info *sit_i = SIT_I(sbi);
	struct atgc_management *am = &sbi->am;
	struct rb_root_cached *root = &am->root;
	struct rb_node *node;
	struct victim_entry *ve;
	unsigned long long total_time;
	unsigned long long age, u, accu;
	unsigned long long max_mtime = sit_i->dirty_max_mtime;
	unsigned long long min_mtime = sit_i->dirty_min_mtime;
	unsigned int sec_blocks = CAP_BLKS_PER_SEC(sbi);
	unsigned int vblocks;
	unsigned int dirty_threshold = max(am->max_candidate_count,
					am->candidate_ratio *
					am->victim_count / 100);
	unsigned int age_weight = am->age_weight;
	unsigned int cost;
	unsigned int iter = 0;

	if (max_mtime < min_mtime)
		return;

	max_mtime += 1;
	total_time = max_mtime - min_mtime;

	accu = div64_u64(ULLONG_MAX, total_time);
	accu = min_t(unsigned long long, div_u64(accu, 100),
					DEFAULT_ACCURACY_CLASS);

	node = rb_first_cached(root);
next:
	ve = rb_entry_safe(node, struct victim_entry, rb_node);
	if (!ve)
		return;

	if (ve->mtime >= max_mtime || ve->mtime < min_mtime)
		goto skip;

	/* age = 10000 * x% * 60 */
	age = div64_u64(accu * (max_mtime - ve->mtime), total_time) *
								age_weight;

	vblocks = get_valid_blocks(sbi, ve->segno, true);
	f2fs_bug_on(sbi, !vblocks || vblocks == sec_blocks);

	/* u = 10000 * x% * 40 */
	u = div64_u64(accu * (sec_blocks - vblocks), sec_blocks) *
							(100 - age_weight);

	f2fs_bug_on(sbi, age + u >= UINT_MAX);

	cost = UINT_MAX - (age + u);
	iter++;

	if (cost < p->min_cost ||
			(cost == p->min_cost && age > p->oldest_age)) {
		p->min_cost = cost;
		p->oldest_age = age;
		p->min_segno = ve->segno;
	}
skip:
	if (iter < dirty_threshold) {
		node = rb_next(node);
		goto next;
	}
}

/*
 * select candidates around source section in range of
 * [target - dirty_threshold, target + dirty_threshold]
 */
static void atssr_lookup_victim(struct f2fs_sb_info *sbi,
						struct victim_sel_policy *p)
{
	struct sit_info *sit_i = SIT_I(sbi);
	struct atgc_management *am = &sbi->am;
	struct victim_entry *ve;
	unsigned long long age;
	unsigned long long max_mtime = sit_i->dirty_max_mtime;
	unsigned long long min_mtime = sit_i->dirty_min_mtime;
	unsigned int vblocks;
	unsigned int dirty_threshold = max(am->max_candidate_count,
					am->candidate_ratio *
					am->victim_count / 100);
	unsigned int cost, iter;
	int stage = 0;

	if (max_mtime < min_mtime)
		return;
	max_mtime += 1;
next_stage:
	iter = 0;
	ve = __lookup_victim_entry(sbi, p->age);
next_node:
	if (!ve) {
		if (stage++ == 0)
			goto next_stage;
		return;
	}

	if (ve->mtime >= max_mtime || ve->mtime < min_mtime)
		goto skip_node;

	age = max_mtime - ve->mtime;

	vblocks = get_seg_entry(sbi, ve->segno)->ckpt_valid_blocks;
	f2fs_bug_on(sbi, !vblocks);

	/* rare case */
	if (vblocks == BLKS_PER_SEG(sbi))
		goto skip_node;

	iter++;

	age = max_mtime - abs(p->age - age);
	cost = UINT_MAX - vblocks;

	if (cost < p->min_cost ||
			(cost == p->min_cost && age > p->oldest_age)) {
		p->min_cost = cost;
		p->oldest_age = age;
		p->min_segno = ve->segno;
	}
skip_node:
	if (iter < dirty_threshold) {
		ve = rb_entry(stage == 0 ? rb_prev(&ve->rb_node) :
					rb_next(&ve->rb_node),
					struct victim_entry, rb_node);
		goto next_node;
	}

	if (stage++ == 0)
		goto next_stage;
}

static void lookup_victim_by_age(struct f2fs_sb_info *sbi,
						struct victim_sel_policy *p)
{
	f2fs_bug_on(sbi, !f2fs_check_victim_tree(sbi, &sbi->am.root));

	if (p->gc_mode == GC_AT)
		atgc_lookup_victim(sbi, p);
	else if (p->alloc_mode == AT_SSR)
		atssr_lookup_victim(sbi, p);
	else
		f2fs_bug_on(sbi, 1);
}

static void release_victim_entry(struct f2fs_sb_info *sbi)
{
	struct atgc_management *am = &sbi->am;
	struct victim_entry *ve, *tmp;

	list_for_each_entry_safe(ve, tmp, &am->victim_list, list) {
		list_del(&ve->list);
		kmem_cache_free(victim_entry_slab, ve);
		am->victim_count--;
	}

	am->root = RB_ROOT_CACHED;

	f2fs_bug_on(sbi, am->victim_count);
	f2fs_bug_on(sbi, !list_empty(&am->victim_list));
}

static bool f2fs_pin_section(struct f2fs_sb_info *sbi, unsigned int segno)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	unsigned int secno = GET_SEC_FROM_SEG(sbi, segno);

	if (!dirty_i->enable_pin_section)
		return false;
	if (!test_and_set_bit(secno, dirty_i->pinned_secmap))
		dirty_i->pinned_secmap_cnt++;
	return true;
}

static bool f2fs_pinned_section_exists(struct dirty_seglist_info *dirty_i)
{
	return dirty_i->pinned_secmap_cnt;
}

static bool f2fs_section_is_pinned(struct dirty_seglist_info *dirty_i,
						unsigned int secno)
{
	return dirty_i->enable_pin_section &&
		f2fs_pinned_section_exists(dirty_i) &&
		test_bit(secno, dirty_i->pinned_secmap);
}

static void f2fs_unpin_all_sections(struct f2fs_sb_info *sbi, bool enable)
{
	unsigned int bitmap_size = f2fs_bitmap_size(MAIN_SECS(sbi));

	if (f2fs_pinned_section_exists(DIRTY_I(sbi))) {
		memset(DIRTY_I(sbi)->pinned_secmap, 0, bitmap_size);
		DIRTY_I(sbi)->pinned_secmap_cnt = 0;
	}
	DIRTY_I(sbi)->enable_pin_section = enable;
}

static int f2fs_gc_pinned_control(struct inode *inode, int gc_type,
							unsigned int segno)
{
	if (!f2fs_is_pinned_file(inode))
		return 0;
	if (gc_type != FG_GC)
		return -EBUSY;
	if (!f2fs_pin_section(F2FS_I_SB(inode), segno))
		f2fs_pin_file_control(inode, true);
	return -EAGAIN;
}

/*
 * This function is called from two paths.
 * One is garbage collection and the other is SSR segment selection.
 * When it is called during GC, it just gets a victim segment
 * and it does not remove it from dirty seglist.
 * When it is called from SSR segment selection, it finds a segment
 * which has minimum valid blocks and removes it from dirty seglist.
 */
int f2fs_get_victim(struct f2fs_sb_info *sbi, unsigned int *result,
			int gc_type, int type, char alloc_mode,
			unsigned long long age, bool one_time)
{
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	struct sit_info *sm = SIT_I(sbi);
	struct victim_sel_policy p;
	unsigned int secno, last_victim;
	unsigned int last_segment;
	unsigned int nsearched;
	unsigned int valid_thresh_ratio = 100;
	bool is_atgc;
	int ret = 0;

	mutex_lock(&dirty_i->seglist_lock);
	last_segment = MAIN_SECS(sbi) * SEGS_PER_SEC(sbi);

	p.alloc_mode = alloc_mode;
	p.age = age;
	p.age_threshold = sbi->am.age_threshold;
	if (one_time) {
		p.one_time_gc = one_time;
		if (has_enough_free_secs(sbi, 0, NR_PERSISTENT_LOG))
			valid_thresh_ratio = sbi->gc_thread->valid_thresh_ratio;
	}

retry:
	select_policy(sbi, gc_type, type, &p);
	p.min_segno = NULL_SEGNO;
	p.oldest_age = 0;
	p.min_cost = get_max_cost(sbi, &p);

	is_atgc = (p.gc_mode == GC_AT || p.alloc_mode == AT_SSR);
	nsearched = 0;

	if (is_atgc)
		SIT_I(sbi)->dirty_min_mtime = ULLONG_MAX;

	if (*result != NULL_SEGNO) {
		if (!get_valid_blocks(sbi, *result, false)) {
			ret = -ENODATA;
			goto out;
		}

		if (sec_usage_check(sbi, GET_SEC_FROM_SEG(sbi, *result))) {
			ret = -EBUSY;
			goto out;
		}
		if (gc_type == FG_GC)
			clear_bit(GET_SEC_FROM_SEG(sbi, *result), dirty_i->victim_secmap);
		p.min_segno = *result;
		goto got_result;
	}

	ret = -ENODATA;
	if (p.max_search == 0)
		goto out;

	if (__is_large_section(sbi) && p.alloc_mode == LFS) {
		if (sbi->next_victim_seg[BG_GC] != NULL_SEGNO) {
			p.min_segno = sbi->next_victim_seg[BG_GC];
			*result = p.min_segno;
			sbi->next_victim_seg[BG_GC] = NULL_SEGNO;
			goto got_result;
		}
		if (gc_type == FG_GC &&
				sbi->next_victim_seg[FG_GC] != NULL_SEGNO) {
			p.min_segno = sbi->next_victim_seg[FG_GC];
			*result = p.min_segno;
			sbi->next_victim_seg[FG_GC] = NULL_SEGNO;
			goto got_result;
		}
	}

	last_victim = sm->last_victim[p.gc_mode];
	if (p.alloc_mode == LFS && gc_type == FG_GC) {
		p.min_segno = check_bg_victims(sbi);
		if (p.min_segno != NULL_SEGNO)
			goto got_it;
	}

	while (1) {
		unsigned long cost, *dirty_bitmap;
		unsigned int unit_no, segno;

		dirty_bitmap = p.dirty_bitmap;
		unit_no = find_next_bit(dirty_bitmap,
				last_segment / p.ofs_unit,
				p.offset / p.ofs_unit);
		segno = unit_no * p.ofs_unit;
		if (segno >= last_segment) {
			if (sm->last_victim[p.gc_mode]) {
				last_segment =
					sm->last_victim[p.gc_mode];
				sm->last_victim[p.gc_mode] = 0;
				p.offset = 0;
				continue;
			}
			break;
		}

		p.offset = segno + p.ofs_unit;
		nsearched++;

#ifdef CONFIG_F2FS_CHECK_FS
		/*
		 * skip selecting the invalid segno (that is failed due to block
		 * validity check failure during GC) to avoid endless GC loop in
		 * such cases.
		 */
		if (test_bit(segno, sm->invalid_segmap))
			goto next;
#endif

		secno = GET_SEC_FROM_SEG(sbi, segno);

		if (sec_usage_check(sbi, secno))
			goto next;

		/* Don't touch checkpointed data */
		if (unlikely(is_sbi_flag_set(sbi, SBI_CP_DISABLED))) {
			if (p.alloc_mode == LFS) {
				/*
				 * LFS is set to find source section during GC.
				 * The victim should have no checkpointed data.
				 */
				if (get_ckpt_valid_blocks(sbi, segno, true))
					goto next;
			} else {
				/*
				 * SSR | AT_SSR are set to find target segment
				 * for writes which can be full by checkpointed
				 * and newly written blocks.
				 */
				if (!f2fs_segment_has_free_slot(sbi, segno))
					goto next;
			}
		}

		if (gc_type == BG_GC && test_bit(secno, dirty_i->victim_secmap))
			goto next;

		if (gc_type == FG_GC && f2fs_section_is_pinned(dirty_i, secno))
			goto next;

		if (is_atgc) {
			add_victim_entry(sbi, &p, segno);
			goto next;
		}

		cost = get_gc_cost(sbi, segno, &p, valid_thresh_ratio);

		if (p.min_cost > cost) {
			p.min_segno = segno;
			p.min_cost = cost;
		}
next:
		if (nsearched >= p.max_search) {
			if (!sm->last_victim[p.gc_mode] && segno <= last_victim)
				sm->last_victim[p.gc_mode] =
					last_victim + p.ofs_unit;
			else
				sm->last_victim[p.gc_mode] = segno + p.ofs_unit;
			sm->last_victim[p.gc_mode] %=
				(MAIN_SECS(sbi) * SEGS_PER_SEC(sbi));
			break;
		}
	}

	/* get victim for GC_AT/AT_SSR */
	if (is_atgc) {
		lookup_victim_by_age(sbi, &p);
		release_victim_entry(sbi);
	}

	if (is_atgc && p.min_segno == NULL_SEGNO &&
			sm->elapsed_time < p.age_threshold) {
		p.age_threshold = 0;
		goto retry;
	}

	if (p.min_segno != NULL_SEGNO) {
got_it:
		*result = (p.min_segno / p.ofs_unit) * p.ofs_unit;
got_result:
		if (p.alloc_mode == LFS) {
			secno = GET_SEC_FROM_SEG(sbi, p.min_segno);
			if (gc_type == FG_GC)
				sbi->cur_victim_sec = secno;
			else
				set_bit(secno, dirty_i->victim_secmap);
		}
		ret = 0;

	}
out:
	if (p.min_segno != NULL_SEGNO)
		trace_f2fs_get_victim(sbi->sb, type, gc_type, &p,
				sbi->cur_victim_sec,
				prefree_segments(sbi), free_segments(sbi));
	mutex_unlock(&dirty_i->seglist_lock);

	return ret;
}

static struct inode *find_gc_inode(struct gc_inode_list *gc_list, nid_t ino)
{
	struct inode_entry *ie;

	ie = radix_tree_lookup(&gc_list->iroot, ino);
	if (ie)
		return ie->inode;
	return NULL;
}

static void add_gc_inode(struct gc_inode_list *gc_list, struct inode *inode)
{
	struct inode_entry *new_ie;

	if (inode == find_gc_inode(gc_list, inode->i_ino)) {
		iput(inode);
		return;
	}
	new_ie = f2fs_kmem_cache_alloc(f2fs_inode_entry_slab,
					GFP_NOFS, true, NULL);
	new_ie->inode = inode;

	f2fs_radix_tree_insert(&gc_list->iroot, inode->i_ino, new_ie);
	list_add_tail(&new_ie->list, &gc_list->ilist);
}

static void put_gc_inode(struct gc_inode_list *gc_list)
{
	struct inode_entry *ie, *next_ie;

	list_for_each_entry_safe(ie, next_ie, &gc_list->ilist, list) {
		radix_tree_delete(&gc_list->iroot, ie->inode->i_ino);
		iput(ie->inode);
		list_del(&ie->list);
		kmem_cache_free(f2fs_inode_entry_slab, ie);
	}
}

static int check_valid_map(struct f2fs_sb_info *sbi,
				unsigned int segno, int offset)
{
	struct sit_info *sit_i = SIT_I(sbi);
	struct seg_entry *sentry;
	int ret;

	down_read(&sit_i->sentry_lock);
	sentry = get_seg_entry(sbi, segno);
	ret = f2fs_test_bit(offset, sentry->cur_valid_map);
	up_read(&sit_i->sentry_lock);
	return ret;
}

/*
 * This function compares node address got in summary with that in NAT.
 * On validity, copy that node with cold status, otherwise (invalid node)
 * ignore that.
 */
static int gc_node_segment(struct f2fs_sb_info *sbi,
		struct f2fs_summary *sum, unsigned int segno, int gc_type)
{
	struct f2fs_summary *entry;
	block_t start_addr;
	int off;
	int phase = 0;
	bool fggc = (gc_type == FG_GC);
	int submitted = 0;
	unsigned int usable_blks_in_seg = f2fs_usable_blks_in_seg(sbi, segno);

	start_addr = START_BLOCK(sbi, segno);

next_step:
	entry = sum;

	if (fggc && phase == 2)
		atomic_inc(&sbi->wb_sync_req[NODE]);

	for (off = 0; off < usable_blks_in_seg; off++, entry++) {
		nid_t nid = le32_to_cpu(entry->nid);
		struct folio *node_folio;
		struct node_info ni;
		int err;

		/* stop BG_GC if there is not enough free sections. */
		if (gc_type == BG_GC && has_not_enough_free_secs(sbi, 0, 0))
			return submitted;

		if (check_valid_map(sbi, segno, off) == 0)
			continue;

		if (phase == 0) {
			f2fs_ra_meta_pages(sbi, NAT_BLOCK_OFFSET(nid), 1,
							META_NAT, true);
			continue;
		}

		if (phase == 1) {
			f2fs_ra_node_page(sbi, nid);
			continue;
		}

		/* phase == 2 */
		node_folio = f2fs_get_node_folio(sbi, nid, NODE_TYPE_REGULAR);
		if (IS_ERR(node_folio))
			continue;

		/* block may become invalid during f2fs_get_node_folio */
		if (check_valid_map(sbi, segno, off) == 0) {
			f2fs_folio_put(node_folio, true);
			continue;
		}

		if (f2fs_get_node_info(sbi, nid, &ni, false)) {
			f2fs_folio_put(node_folio, true);
			continue;
		}

		if (ni.blk_addr != start_addr + off) {
			f2fs_folio_put(node_folio, true);
			continue;
		}

		err = f2fs_move_node_folio(node_folio, gc_type);
		if (!err && gc_type == FG_GC)
			submitted++;
		stat_inc_node_blk_count(sbi, 1, gc_type);
	}

	if (++phase < 3)
		goto next_step;

	if (fggc)
		atomic_dec(&sbi->wb_sync_req[NODE]);
	return submitted;
}

/*
 * Calculate start block index indicating the given node offset.
 * Be careful, caller should give this node offset only indicating direct node
 * blocks. If any node offsets, which point the other types of node blocks such
 * as indirect or double indirect node blocks, are given, it must be a caller's
 * bug.
 */
block_t f2fs_start_bidx_of_node(unsigned int node_ofs, struct inode *inode)
{
	unsigned int indirect_blks = 2 * NIDS_PER_BLOCK + 4;
	unsigned int bidx;

	if (node_ofs == 0)
		return 0;

	if (node_ofs <= 2) {
		bidx = node_ofs - 1;
	} else if (node_ofs <= indirect_blks) {
		int dec = (node_ofs - 4) / (NIDS_PER_BLOCK + 1);

		bidx = node_ofs - 2 - dec;
	} else {
		int dec = (node_ofs - indirect_blks - 3) / (NIDS_PER_BLOCK + 1);

		bidx = node_ofs - 5 - dec;
	}
	return bidx * ADDRS_PER_BLOCK(inode) + ADDRS_PER_INODE(inode);
}

static bool is_alive(struct f2fs_sb_info *sbi, struct f2fs_summary *sum,
		struct node_info *dni, block_t blkaddr, unsigned int *nofs)
{
	struct folio *node_folio;
	nid_t nid;
	unsigned int ofs_in_node, max_addrs, base;
	block_t source_blkaddr;

	nid = le32_to_cpu(sum->nid);
	ofs_in_node = le16_to_cpu(sum->ofs_in_node);

	node_folio = f2fs_get_node_folio(sbi, nid, NODE_TYPE_REGULAR);
	if (IS_ERR(node_folio))
		return false;

	if (f2fs_get_node_info(sbi, nid, dni, false)) {
		f2fs_folio_put(node_folio, true);
		return false;
	}

	if (sum->version != dni->version) {
		f2fs_warn(sbi, "%s: valid data with mismatched node version.",
			  __func__);
		set_sbi_flag(sbi, SBI_NEED_FSCK);
	}

	if (f2fs_check_nid_range(sbi, dni->ino)) {
		f2fs_folio_put(node_folio, true);
		return false;
	}

	if (IS_INODE(node_folio)) {
		base = offset_in_addr(F2FS_INODE(node_folio));
		max_addrs = DEF_ADDRS_PER_INODE;
	} else {
		base = 0;
		max_addrs = DEF_ADDRS_PER_BLOCK;
	}

	if (base + ofs_in_node >= max_addrs) {
		f2fs_err(sbi, "Inconsistent blkaddr offset: base:%u, ofs_in_node:%u, max:%u, ino:%u, nid:%u",
			base, ofs_in_node, max_addrs, dni->ino, dni->nid);
		f2fs_folio_put(node_folio, true);
		return false;
	}

	*nofs = ofs_of_node(node_folio);
	source_blkaddr = data_blkaddr(NULL, node_folio, ofs_in_node);
	f2fs_folio_put(node_folio, true);

	if (source_blkaddr != blkaddr) {
#ifdef CONFIG_F2FS_CHECK_FS
		unsigned int segno = GET_SEGNO(sbi, blkaddr);
		unsigned long offset = GET_BLKOFF_FROM_SEG0(sbi, blkaddr);

		if (unlikely(check_valid_map(sbi, segno, offset))) {
			if (!test_and_set_bit(segno, SIT_I(sbi)->invalid_segmap)) {
				f2fs_err(sbi, "mismatched blkaddr %u (source_blkaddr %u) in seg %u",
					 blkaddr, source_blkaddr, segno);
				set_sbi_flag(sbi, SBI_NEED_FSCK);
			}
		}
#endif
		return false;
	}
	return true;
}

static int ra_data_block(struct inode *inode, pgoff_t index)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct address_space *mapping = f2fs_is_cow_file(inode) ?
				F2FS_I(inode)->atomic_inode->i_mapping : inode->i_mapping;
	struct dnode_of_data dn;
	struct folio *folio;
	struct f2fs_io_info fio = {
		.sbi = sbi,
		.ino = inode->i_ino,
		.type = DATA,
		.temp = COLD,
		.op = REQ_OP_READ,
		.op_flags = 0,
		.encrypted_page = NULL,
		.in_list = 0,
	};
	int err;

	folio = f2fs_grab_cache_folio(mapping, index, true);
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	if (f2fs_lookup_read_extent_cache_block(inode, index,
						&dn.data_blkaddr)) {
		if (unlikely(!f2fs_is_valid_blkaddr(sbi, dn.data_blkaddr,
						DATA_GENERIC_ENHANCE_READ))) {
			err = -EFSCORRUPTED;
			goto put_folio;
		}
		goto got_it;
	}

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = f2fs_get_dnode_of_data(&dn, index, LOOKUP_NODE);
	if (err)
		goto put_folio;
	f2fs_put_dnode(&dn);

	if (!__is_valid_data_blkaddr(dn.data_blkaddr)) {
		err = -ENOENT;
		goto put_folio;
	}
	if (unlikely(!f2fs_is_valid_blkaddr(sbi, dn.data_blkaddr,
						DATA_GENERIC_ENHANCE))) {
		err = -EFSCORRUPTED;
		goto put_folio;
	}
got_it:
	/* read folio */
	fio.folio = folio;
	fio.new_blkaddr = fio.old_blkaddr = dn.data_blkaddr;

	/*
	 * don't cache encrypted data into meta inode until previous dirty
	 * data were writebacked to avoid racing between GC and flush.
	 */
	f2fs_folio_wait_writeback(folio, DATA, true, true);

	f2fs_wait_on_block_writeback(inode, dn.data_blkaddr);

	fio.encrypted_page = f2fs_pagecache_get_page(META_MAPPING(sbi),
					dn.data_blkaddr,
					FGP_LOCK | FGP_CREAT, GFP_NOFS);
	if (!fio.encrypted_page) {
		err = -ENOMEM;
		goto put_folio;
	}

	err = f2fs_submit_page_bio(&fio);
	if (err)
		goto put_encrypted_page;
	f2fs_put_page(fio.encrypted_page, 0);
	f2fs_folio_put(folio, true);

	f2fs_update_iostat(sbi, inode, FS_DATA_READ_IO, F2FS_BLKSIZE);
	f2fs_update_iostat(sbi, NULL, FS_GDATA_READ_IO, F2FS_BLKSIZE);

	return 0;
put_encrypted_page:
	f2fs_put_page(fio.encrypted_page, 1);
put_folio:
	f2fs_folio_put(folio, true);
	return err;
}

/*
 * Move data block via META_MAPPING while keeping locked data page.
 * This can be used to move blocks, aka LBAs, directly on disk.
 */
static int move_data_block(struct inode *inode, block_t bidx,
				int gc_type, unsigned int segno, int off)
{
	struct address_space *mapping = f2fs_is_cow_file(inode) ?
				F2FS_I(inode)->atomic_inode->i_mapping : inode->i_mapping;
	struct f2fs_io_info fio = {
		.sbi = F2FS_I_SB(inode),
		.ino = inode->i_ino,
		.type = DATA,
		.temp = COLD,
		.op = REQ_OP_READ,
		.op_flags = 0,
		.encrypted_page = NULL,
		.in_list = 0,
	};
	struct dnode_of_data dn;
	struct f2fs_summary sum;
	struct node_info ni;
	struct folio *folio, *mfolio;
	block_t newaddr;
	int err = 0;
	bool lfs_mode = f2fs_lfs_mode(fio.sbi);
	int type = fio.sbi->am.atgc_enabled && (gc_type == BG_GC) &&
				(fio.sbi->gc_mode != GC_URGENT_HIGH) ?
				CURSEG_ALL_DATA_ATGC : CURSEG_COLD_DATA;

	/* do not read out */
	folio = f2fs_grab_cache_folio(mapping, bidx, false);
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	if (!check_valid_map(F2FS_I_SB(inode), segno, off)) {
		err = -ENOENT;
		goto out;
	}

	err = f2fs_gc_pinned_control(inode, gc_type, segno);
	if (err)
		goto out;

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = f2fs_get_dnode_of_data(&dn, bidx, LOOKUP_NODE);
	if (err)
		goto out;

	if (unlikely(dn.data_blkaddr == NULL_ADDR)) {
		folio_clear_uptodate(folio);
		err = -ENOENT;
		goto put_out;
	}

	/*
	 * don't cache encrypted data into meta inode until previous dirty
	 * data were writebacked to avoid racing between GC and flush.
	 */
	f2fs_folio_wait_writeback(folio, DATA, true, true);

	f2fs_wait_on_block_writeback(inode, dn.data_blkaddr);

	err = f2fs_get_node_info(fio.sbi, dn.nid, &ni, false);
	if (err)
		goto put_out;

	/* read page */
	fio.folio = folio;
	fio.new_blkaddr = fio.old_blkaddr = dn.data_blkaddr;

	if (lfs_mode)
		f2fs_down_write(&fio.sbi->io_order_lock);

	mfolio = f2fs_grab_cache_folio(META_MAPPING(fio.sbi),
					fio.old_blkaddr, false);
	if (IS_ERR(mfolio)) {
		err = PTR_ERR(mfolio);
		goto up_out;
	}

	fio.encrypted_page = folio_file_page(mfolio, fio.old_blkaddr);

	/* read source block in mfolio */
	if (!folio_test_uptodate(mfolio)) {
		err = f2fs_submit_page_bio(&fio);
		if (err) {
			f2fs_folio_put(mfolio, true);
			goto up_out;
		}

		f2fs_update_iostat(fio.sbi, inode, FS_DATA_READ_IO,
							F2FS_BLKSIZE);
		f2fs_update_iostat(fio.sbi, NULL, FS_GDATA_READ_IO,
							F2FS_BLKSIZE);

		folio_lock(mfolio);
		if (unlikely(!is_meta_folio(mfolio) ||
			     !folio_test_uptodate(mfolio))) {
			err = -EIO;
			f2fs_folio_put(mfolio, true);
			goto up_out;
		}
	}

	set_summary(&sum, dn.nid, dn.ofs_in_node, ni.version);

	/* allocate block address */
	err = f2fs_allocate_data_block(fio.sbi, NULL, fio.old_blkaddr, &newaddr,
				&sum, type, NULL);
	if (err) {
		f2fs_folio_put(mfolio, true);
		/* filesystem should shutdown, no need to recovery block */
		goto up_out;
	}

	fio.encrypted_page = f2fs_pagecache_get_page(META_MAPPING(fio.sbi),
				newaddr, FGP_LOCK | FGP_CREAT, GFP_NOFS);
	if (!fio.encrypted_page) {
		err = -ENOMEM;
		f2fs_folio_put(mfolio, true);
		goto recover_block;
	}

	/* write target block */
	f2fs_wait_on_page_writeback(fio.encrypted_page, DATA, true, true);
	memcpy(page_address(fio.encrypted_page),
				folio_address(mfolio), PAGE_SIZE);
	f2fs_folio_put(mfolio, true);

	f2fs_invalidate_internal_cache(fio.sbi, fio.old_blkaddr, 1);

	set_page_dirty(fio.encrypted_page);
	if (clear_page_dirty_for_io(fio.encrypted_page))
		dec_page_count(fio.sbi, F2FS_DIRTY_META);

	set_page_writeback(fio.encrypted_page);

	fio.op = REQ_OP_WRITE;
	fio.op_flags = REQ_SYNC;
	fio.new_blkaddr = newaddr;
	f2fs_submit_page_write(&fio);

	f2fs_update_iostat(fio.sbi, NULL, FS_GC_DATA_IO, F2FS_BLKSIZE);

	f2fs_update_data_blkaddr(&dn, newaddr);
	set_inode_flag(inode, FI_APPEND_WRITE);

	f2fs_put_page(fio.encrypted_page, 1);
recover_block:
	if (err)
		f2fs_do_replace_block(fio.sbi, &sum, newaddr, fio.old_blkaddr,
							true, true, true);
up_out:
	if (lfs_mode)
		f2fs_up_write(&fio.sbi->io_order_lock);
put_out:
	f2fs_put_dnode(&dn);
out:
	f2fs_folio_put(folio, true);
	return err;
}

static int move_data_page(struct inode *inode, block_t bidx, int gc_type,
						unsigned int segno, int off)
{
	struct folio *folio;
	int err = 0;

	folio = f2fs_get_lock_data_folio(inode, bidx, true);
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	if (!check_valid_map(F2FS_I_SB(inode), segno, off)) {
		err = -ENOENT;
		goto out;
	}

	err = f2fs_gc_pinned_control(inode, gc_type, segno);
	if (err)
		goto out;

	if (gc_type == BG_GC) {
		if (folio_test_writeback(folio)) {
			err = -EAGAIN;
			goto out;
		}
		folio_mark_dirty(folio);
		folio_set_f2fs_gcing(folio);
	} else {
		struct f2fs_io_info fio = {
			.sbi = F2FS_I_SB(inode),
			.ino = inode->i_ino,
			.type = DATA,
			.temp = COLD,
			.op = REQ_OP_WRITE,
			.op_flags = REQ_SYNC,
			.old_blkaddr = NULL_ADDR,
			.folio = folio,
			.encrypted_page = NULL,
			.need_lock = LOCK_REQ,
			.io_type = FS_GC_DATA_IO,
		};
		bool is_dirty = folio_test_dirty(folio);

retry:
		f2fs_folio_wait_writeback(folio, DATA, true, true);

		folio_mark_dirty(folio);
		if (folio_clear_dirty_for_io(folio)) {
			inode_dec_dirty_pages(inode);
			f2fs_remove_dirty_inode(inode);
		}

		folio_set_f2fs_gcing(folio);

		err = f2fs_do_write_data_page(&fio);
		if (err) {
			folio_clear_f2fs_gcing(folio);
			if (err == -ENOMEM) {
				memalloc_retry_wait(GFP_NOFS);
				goto retry;
			}
			if (is_dirty)
				folio_mark_dirty(folio);
		}
	}
out:
	f2fs_folio_put(folio, true);
	return err;
}

/*
 * This function tries to get parent node of victim data block, and identifies
 * data block validity. If the block is valid, copy that with cold status and
 * modify parent node.
 * If the parent node is not valid or the data block address is different,
 * the victim data block is ignored.
 */
static int gc_data_segment(struct f2fs_sb_info *sbi, struct f2fs_summary *sum,
		struct gc_inode_list *gc_list, unsigned int segno, int gc_type,
		bool force_migrate)
{
	struct super_block *sb = sbi->sb;
	struct f2fs_summary *entry;
	block_t start_addr;
	int off;
	int phase = 0;
	int submitted = 0;
	unsigned int usable_blks_in_seg = f2fs_usable_blks_in_seg(sbi, segno);

	start_addr = START_BLOCK(sbi, segno);

next_step:
	entry = sum;

	for (off = 0; off < usable_blks_in_seg; off++, entry++) {
		struct inode *inode;
		struct node_info dni; /* dnode info for the data */
		unsigned int ofs_in_node, nofs;
		block_t start_bidx;
		nid_t nid = le32_to_cpu(entry->nid);

		/*
		 * stop BG_GC if there is not enough free sections.
		 * Or, stop GC if the segment becomes fully valid caused by
		 * race condition along with SSR block allocation.
		 */
		if ((gc_type == BG_GC && has_not_enough_free_secs(sbi, 0, 0)) ||
			(!force_migrate && get_valid_blocks(sbi, segno, true) ==
							CAP_BLKS_PER_SEC(sbi)))
			return submitted;

		if (check_valid_map(sbi, segno, off) == 0)
			continue;

		if (phase == 0) {
			f2fs_ra_meta_pages(sbi, NAT_BLOCK_OFFSET(nid), 1,
							META_NAT, true);
			continue;
		}

		if (phase == 1) {
			f2fs_ra_node_page(sbi, nid);
			continue;
		}

		/* Get an inode by ino with checking validity */
		if (!is_alive(sbi, entry, &dni, start_addr + off, &nofs))
			continue;

		if (phase == 2) {
			f2fs_ra_node_page(sbi, dni.ino);
			continue;
		}

		ofs_in_node = le16_to_cpu(entry->ofs_in_node);

		if (phase == 3) {
			struct folio *data_folio;
			int err;

			inode = f2fs_iget(sb, dni.ino);
			if (IS_ERR(inode))
				continue;

			if (is_bad_inode(inode) ||
					special_file(inode->i_mode)) {
				iput(inode);
				continue;
			}

			if (f2fs_has_inline_data(inode)) {
				iput(inode);
				set_sbi_flag(sbi, SBI_NEED_FSCK);
				f2fs_err_ratelimited(sbi,
					"inode %lx has both inline_data flag and "
					"data block, nid=%u, ofs_in_node=%u",
					inode->i_ino, dni.nid, ofs_in_node);
				continue;
			}

			err = f2fs_gc_pinned_control(inode, gc_type, segno);
			if (err == -EAGAIN) {
				iput(inode);
				return submitted;
			}

			if (!f2fs_down_write_trylock(
				&F2FS_I(inode)->i_gc_rwsem[WRITE])) {
				iput(inode);
				sbi->skipped_gc_rwsem++;
				continue;
			}

			start_bidx = f2fs_start_bidx_of_node(nofs, inode) +
								ofs_in_node;

			if (f2fs_meta_inode_gc_required(inode)) {
				int err = ra_data_block(inode, start_bidx);

				f2fs_up_write(&F2FS_I(inode)->i_gc_rwsem[WRITE]);
				if (err) {
					iput(inode);
					continue;
				}
				add_gc_inode(gc_list, inode);
				continue;
			}

			data_folio = f2fs_get_read_data_folio(inode, start_bidx,
							REQ_RAHEAD, true, NULL);
			f2fs_up_write(&F2FS_I(inode)->i_gc_rwsem[WRITE]);
			if (IS_ERR(data_folio)) {
				iput(inode);
				continue;
			}

			f2fs_folio_put(data_folio, false);
			add_gc_inode(gc_list, inode);
			continue;
		}

		/* phase 4 */
		inode = find_gc_inode(gc_list, dni.ino);
		if (inode) {
			struct f2fs_inode_info *fi = F2FS_I(inode);
			bool locked = false;
			int err;

			if (S_ISREG(inode->i_mode)) {
				if (!f2fs_down_write_trylock(&fi->i_gc_rwsem[WRITE])) {
					sbi->skipped_gc_rwsem++;
					continue;
				}
				if (!f2fs_down_write_trylock(
						&fi->i_gc_rwsem[READ])) {
					sbi->skipped_gc_rwsem++;
					f2fs_up_write(&fi->i_gc_rwsem[WRITE]);
					continue;
				}
				locked = true;

				/* wait for all inflight aio data */
				inode_dio_wait(inode);
			}

			start_bidx = f2fs_start_bidx_of_node(nofs, inode)
								+ ofs_in_node;
			if (f2fs_meta_inode_gc_required(inode))
				err = move_data_block(inode, start_bidx,
							gc_type, segno, off);
			else
				err = move_data_page(inode, start_bidx, gc_type,
								segno, off);

			if (!err && (gc_type == FG_GC ||
					f2fs_meta_inode_gc_required(inode)))
				submitted++;

			if (locked) {
				f2fs_up_write(&fi->i_gc_rwsem[READ]);
				f2fs_up_write(&fi->i_gc_rwsem[WRITE]);
			}

			stat_inc_data_blk_count(sbi, 1, gc_type);
		}
	}

	if (++phase < 5)
		goto next_step;

	return submitted;
}

static int __get_victim(struct f2fs_sb_info *sbi, unsigned int *victim,
			int gc_type, bool one_time)
{
	struct sit_info *sit_i = SIT_I(sbi);
	int ret;

	down_write(&sit_i->sentry_lock);
	ret = f2fs_get_victim(sbi, victim, gc_type, NO_CHECK_TYPE,
			LFS, 0, one_time);
	up_write(&sit_i->sentry_lock);
	return ret;
}

static int do_garbage_collect(struct f2fs_sb_info *sbi,
				unsigned int start_segno,
				struct gc_inode_list *gc_list, int gc_type,
				bool force_migrate, bool one_time)
{
	struct blk_plug plug;
	unsigned int segno = start_segno;
	unsigned int end_segno = start_segno + SEGS_PER_SEC(sbi);
	unsigned int sec_end_segno;
	int seg_freed = 0, migrated = 0;
	unsigned char type = IS_DATASEG(get_seg_entry(sbi, segno)->type) ?
						SUM_TYPE_DATA : SUM_TYPE_NODE;
	unsigned char data_type = (type == SUM_TYPE_DATA) ? DATA : NODE;
	int submitted = 0;

	if (__is_large_section(sbi)) {
		sec_end_segno = rounddown(end_segno, SEGS_PER_SEC(sbi));

		/*
		 * zone-capacity can be less than zone-size in zoned devices,
		 * resulting in less than expected usable segments in the zone,
		 * calculate the end segno in the zone which can be garbage
		 * collected
		 */
		if (f2fs_sb_has_blkzoned(sbi))
			sec_end_segno -= SEGS_PER_SEC(sbi) -
					f2fs_usable_segs_in_sec(sbi);

		if (gc_type == BG_GC || one_time) {
			unsigned int window_granularity =
				sbi->migration_window_granularity;

			if (f2fs_sb_has_blkzoned(sbi) &&
					!has_enough_free_blocks(sbi,
					sbi->gc_thread->boost_zoned_gc_percent))
				window_granularity *=
					sbi->gc_thread->boost_gc_multiple;

			end_segno = start_segno + window_granularity;
		}

		if (end_segno > sec_end_segno)
			end_segno = sec_end_segno;
	}

	sanity_check_seg_type(sbi, get_seg_entry(sbi, segno)->type);

	/* readahead multi ssa blocks those have contiguous address */
	if (__is_large_section(sbi))
		f2fs_ra_meta_pages(sbi, GET_SUM_BLOCK(sbi, segno),
					end_segno - segno, META_SSA, true);

	/* reference all summary page */
	while (segno < end_segno) {
		struct folio *sum_folio = f2fs_get_sum_folio(sbi, segno++);
		if (IS_ERR(sum_folio)) {
			int err = PTR_ERR(sum_folio);

			end_segno = segno - 1;
			for (segno = start_segno; segno < end_segno; segno++) {
				sum_folio = filemap_get_folio(META_MAPPING(sbi),
						GET_SUM_BLOCK(sbi, segno));
				folio_put_refs(sum_folio, 2);
			}
			return err;
		}
		folio_unlock(sum_folio);
	}

	blk_start_plug(&plug);

	for (segno = start_segno; segno < end_segno; segno++) {
		struct f2fs_summary_block *sum;

		/* find segment summary of victim */
		struct folio *sum_folio = filemap_get_folio(META_MAPPING(sbi),
					GET_SUM_BLOCK(sbi, segno));

		if (is_cursec(sbi, GET_SEC_FROM_SEG(sbi, segno))) {
			f2fs_err(sbi, "%s: segment %u is used by log",
							__func__, segno);
			f2fs_bug_on(sbi, 1);
			goto skip;
		}

		if (get_valid_blocks(sbi, segno, false) == 0)
			goto freed;
		if (gc_type == BG_GC && __is_large_section(sbi) &&
				migrated >= sbi->migration_granularity)
			goto skip;
		if (!folio_test_uptodate(sum_folio) ||
		    unlikely(f2fs_cp_error(sbi)))
			goto skip;

		sum = folio_address(sum_folio);
		if (type != GET_SUM_TYPE((&sum->footer))) {
			f2fs_err(sbi, "Inconsistent segment (%u) type [%d, %d] in SIT and SSA",
				 segno, type, GET_SUM_TYPE((&sum->footer)));
			f2fs_stop_checkpoint(sbi, false,
				STOP_CP_REASON_CORRUPTED_SUMMARY);
			goto skip;
		}

		/*
		 * this is to avoid deadlock:
		 * - lock_page(sum_page)         - f2fs_replace_block
		 *  - check_valid_map()            - down_write(sentry_lock)
		 *   - down_read(sentry_lock)     - change_curseg()
		 *                                  - lock_page(sum_page)
		 */
		if (type == SUM_TYPE_NODE)
			submitted += gc_node_segment(sbi, sum->entries, segno,
								gc_type);
		else
			submitted += gc_data_segment(sbi, sum->entries, gc_list,
							segno, gc_type,
							force_migrate);

		stat_inc_gc_seg_count(sbi, data_type, gc_type);
		sbi->gc_reclaimed_segs[sbi->gc_mode]++;
		migrated++;

freed:
		if (gc_type == FG_GC &&
				get_valid_blocks(sbi, segno, false) == 0)
			seg_freed++;

		if (__is_large_section(sbi))
			sbi->next_victim_seg[gc_type] =
				(segno + 1 < sec_end_segno) ?
					segno + 1 : NULL_SEGNO;
skip:
		folio_put_refs(sum_folio, 2);
	}

	if (submitted)
		f2fs_submit_merged_write(sbi, data_type);

	blk_finish_plug(&plug);

	if (migrated)
		stat_inc_gc_sec_count(sbi, data_type, gc_type);

	return seg_freed;
}

int f2fs_gc(struct f2fs_sb_info *sbi, struct f2fs_gc_control *gc_control)
{
	int gc_type = gc_control->init_gc_type;
	unsigned int segno = gc_control->victim_segno;
	int sec_freed = 0, seg_freed = 0, total_freed = 0, total_sec_freed = 0;
	int ret = 0;
	struct cp_control cpc;
	struct gc_inode_list gc_list = {
		.ilist = LIST_HEAD_INIT(gc_list.ilist),
		.iroot = RADIX_TREE_INIT(gc_list.iroot, GFP_NOFS),
	};
	unsigned int skipped_round = 0, round = 0;
	unsigned int upper_secs;

	trace_f2fs_gc_begin(sbi->sb, gc_type, gc_control->no_bg_gc,
				gc_control->nr_free_secs,
				get_pages(sbi, F2FS_DIRTY_NODES),
				get_pages(sbi, F2FS_DIRTY_DENTS),
				get_pages(sbi, F2FS_DIRTY_IMETA),
				free_sections(sbi),
				free_segments(sbi),
				reserved_segments(sbi),
				prefree_segments(sbi));

	cpc.reason = __get_cp_reason(sbi);
gc_more:
	sbi->skipped_gc_rwsem = 0;
	if (unlikely(!(sbi->sb->s_flags & SB_ACTIVE))) {
		ret = -EINVAL;
		goto stop;
	}
	if (unlikely(f2fs_cp_error(sbi))) {
		ret = -EIO;
		goto stop;
	}

	/* Let's run FG_GC, if we don't have enough space. */
	if (has_not_enough_free_secs(sbi, 0, 0)) {
		gc_type = FG_GC;
		gc_control->one_time = false;

		/*
		 * For example, if there are many prefree_segments below given
		 * threshold, we can make them free by checkpoint. Then, we
		 * secure free segments which doesn't need fggc any more.
		 */
		if (prefree_segments(sbi)) {
			stat_inc_cp_call_count(sbi, TOTAL_CALL);
			ret = f2fs_write_checkpoint(sbi, &cpc);
			if (ret)
				goto stop;
			/* Reset due to checkpoint */
			sec_freed = 0;
		}
	}

	/* f2fs_balance_fs doesn't need to do BG_GC in critical path. */
	if (gc_type == BG_GC && gc_control->no_bg_gc) {
		ret = -EINVAL;
		goto stop;
	}
retry:
	ret = __get_victim(sbi, &segno, gc_type, gc_control->one_time);
	if (ret) {
		/* allow to search victim from sections has pinned data */
		if (ret == -ENODATA && gc_type == FG_GC &&
				f2fs_pinned_section_exists(DIRTY_I(sbi))) {
			f2fs_unpin_all_sections(sbi, false);
			goto retry;
		}
		goto stop;
	}

	seg_freed = do_garbage_collect(sbi, segno, &gc_list, gc_type,
				gc_control->should_migrate_blocks,
				gc_control->one_time);
	if (seg_freed < 0)
		goto stop;

	total_freed += seg_freed;

	if (seg_freed == f2fs_usable_segs_in_sec(sbi)) {
		sec_freed++;
		total_sec_freed++;
	}

	if (gc_control->one_time)
		goto stop;

	if (gc_type == FG_GC) {
		sbi->cur_victim_sec = NULL_SEGNO;

		if (has_enough_free_secs(sbi, sec_freed, 0)) {
			if (!gc_control->no_bg_gc &&
			    total_sec_freed < gc_control->nr_free_secs)
				goto go_gc_more;
			goto stop;
		}
		if (sbi->skipped_gc_rwsem)
			skipped_round++;
		round++;
		if (skipped_round > MAX_SKIP_GC_COUNT &&
				skipped_round * 2 >= round) {
			stat_inc_cp_call_count(sbi, TOTAL_CALL);
			ret = f2fs_write_checkpoint(sbi, &cpc);
			goto stop;
		}
	} else if (has_enough_free_secs(sbi, 0, 0)) {
		goto stop;
	}

	__get_secs_required(sbi, NULL, &upper_secs, NULL);

	/*
	 * Write checkpoint to reclaim prefree segments.
	 * We need more three extra sections for writer's data/node/dentry.
	 */
	if (free_sections(sbi) <= upper_secs + NR_GC_CHECKPOINT_SECS &&
				prefree_segments(sbi)) {
		stat_inc_cp_call_count(sbi, TOTAL_CALL);
		ret = f2fs_write_checkpoint(sbi, &cpc);
		if (ret)
			goto stop;
		/* Reset due to checkpoint */
		sec_freed = 0;
	}
go_gc_more:
	segno = NULL_SEGNO;
	goto gc_more;

stop:
	SIT_I(sbi)->last_victim[ALLOC_NEXT] = 0;
	SIT_I(sbi)->last_victim[FLUSH_DEVICE] = gc_control->victim_segno;

	if (gc_type == FG_GC)
		f2fs_unpin_all_sections(sbi, true);

	trace_f2fs_gc_end(sbi->sb, ret, total_freed, total_sec_freed,
				get_pages(sbi, F2FS_DIRTY_NODES),
				get_pages(sbi, F2FS_DIRTY_DENTS),
				get_pages(sbi, F2FS_DIRTY_IMETA),
				free_sections(sbi),
				free_segments(sbi),
				reserved_segments(sbi),
				prefree_segments(sbi));

	f2fs_up_write(&sbi->gc_lock);

	put_gc_inode(&gc_list);

	if (gc_control->err_gc_skipped && !ret)
		ret = total_sec_freed ? 0 : -EAGAIN;
	return ret;
}

int __init f2fs_create_garbage_collection_cache(void)
{
	victim_entry_slab = f2fs_kmem_cache_create("f2fs_victim_entry",
					sizeof(struct victim_entry));
	return victim_entry_slab ? 0 : -ENOMEM;
}

void f2fs_destroy_garbage_collection_cache(void)
{
	kmem_cache_destroy(victim_entry_slab);
}

static void init_atgc_management(struct f2fs_sb_info *sbi)
{
	struct atgc_management *am = &sbi->am;

	if (test_opt(sbi, ATGC) &&
		SIT_I(sbi)->elapsed_time >= DEF_GC_THREAD_AGE_THRESHOLD)
		am->atgc_enabled = true;

	am->root = RB_ROOT_CACHED;
	INIT_LIST_HEAD(&am->victim_list);
	am->victim_count = 0;

	am->candidate_ratio = DEF_GC_THREAD_CANDIDATE_RATIO;
	am->max_candidate_count = DEF_GC_THREAD_MAX_CANDIDATE_COUNT;
	am->age_weight = DEF_GC_THREAD_AGE_WEIGHT;
	am->age_threshold = DEF_GC_THREAD_AGE_THRESHOLD;
}

void f2fs_build_gc_manager(struct f2fs_sb_info *sbi)
{
	sbi->gc_pin_file_threshold = DEF_GC_FAILED_PINNED_FILES;

	/* give warm/cold data area from slower device */
	if (f2fs_is_multi_device(sbi) && !__is_large_section(sbi))
		SIT_I(sbi)->last_victim[ALLOC_NEXT] =
				GET_SEGNO(sbi, FDEV(0).end_blk) + 1;

	init_atgc_management(sbi);
}

int f2fs_gc_range(struct f2fs_sb_info *sbi,
		unsigned int start_seg, unsigned int end_seg,
		bool dry_run, unsigned int dry_run_sections)
{
	unsigned int segno;
	unsigned int gc_secs = dry_run_sections;

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;

	for (segno = start_seg; segno <= end_seg; segno += SEGS_PER_SEC(sbi)) {
		struct gc_inode_list gc_list = {
			.ilist = LIST_HEAD_INIT(gc_list.ilist),
			.iroot = RADIX_TREE_INIT(gc_list.iroot, GFP_NOFS),
		};

		/*
		 * avoid migrating empty section, as it can be allocated by
		 * log in parallel.
		 */
		if (!get_valid_blocks(sbi, segno, true))
			continue;

		if (is_cursec(sbi, GET_SEC_FROM_SEG(sbi, segno)))
			continue;

		do_garbage_collect(sbi, segno, &gc_list, FG_GC, true, false);
		put_gc_inode(&gc_list);

		if (!dry_run && get_valid_blocks(sbi, segno, true))
			return -EAGAIN;
		if (dry_run && dry_run_sections &&
		    !get_valid_blocks(sbi, segno, true) && --gc_secs == 0)
			break;

		if (fatal_signal_pending(current))
			return -ERESTARTSYS;
	}

	return 0;
}

static int free_segment_range(struct f2fs_sb_info *sbi,
				unsigned int secs, bool dry_run)
{
	unsigned int next_inuse, start, end;
	struct cp_control cpc = { CP_RESIZE, 0, 0, 0 };
	int gc_mode, gc_type;
	int err = 0;
	int type;

	/* Force block allocation for GC */
	MAIN_SECS(sbi) -= secs;
	start = MAIN_SECS(sbi) * SEGS_PER_SEC(sbi);
	end = MAIN_SEGS(sbi) - 1;

	mutex_lock(&DIRTY_I(sbi)->seglist_lock);
	for (gc_mode = 0; gc_mode < MAX_GC_POLICY; gc_mode++)
		if (SIT_I(sbi)->last_victim[gc_mode] >= start)
			SIT_I(sbi)->last_victim[gc_mode] = 0;

	for (gc_type = BG_GC; gc_type <= FG_GC; gc_type++)
		if (sbi->next_victim_seg[gc_type] >= start)
			sbi->next_victim_seg[gc_type] = NULL_SEGNO;
	mutex_unlock(&DIRTY_I(sbi)->seglist_lock);

	/* Move out cursegs from the target range */
	for (type = CURSEG_HOT_DATA; type < NR_CURSEG_PERSIST_TYPE; type++) {
		err = f2fs_allocate_segment_for_resize(sbi, type, start, end);
		if (err)
			goto out;
	}

	/* do GC to move out valid blocks in the range */
	err = f2fs_gc_range(sbi, start, end, dry_run, 0);
	if (err || dry_run)
		goto out;

	stat_inc_cp_call_count(sbi, TOTAL_CALL);
	err = f2fs_write_checkpoint(sbi, &cpc);
	if (err)
		goto out;

	next_inuse = find_next_inuse(FREE_I(sbi), end + 1, start);
	if (next_inuse <= end) {
		f2fs_err(sbi, "segno %u should be free but still inuse!",
			 next_inuse);
		f2fs_bug_on(sbi, 1);
	}
out:
	MAIN_SECS(sbi) += secs;
	return err;
}

static void update_sb_metadata(struct f2fs_sb_info *sbi, int secs)
{
	struct f2fs_super_block *raw_sb = F2FS_RAW_SUPER(sbi);
	int section_count;
	int segment_count;
	int segment_count_main;
	long long block_count;
	int segs = secs * SEGS_PER_SEC(sbi);

	f2fs_down_write(&sbi->sb_lock);

	section_count = le32_to_cpu(raw_sb->section_count);
	segment_count = le32_to_cpu(raw_sb->segment_count);
	segment_count_main = le32_to_cpu(raw_sb->segment_count_main);
	block_count = le64_to_cpu(raw_sb->block_count);

	raw_sb->section_count = cpu_to_le32(section_count + secs);
	raw_sb->segment_count = cpu_to_le32(segment_count + segs);
	raw_sb->segment_count_main = cpu_to_le32(segment_count_main + segs);
	raw_sb->block_count = cpu_to_le64(block_count +
			(long long)SEGS_TO_BLKS(sbi, segs));
	if (f2fs_is_multi_device(sbi)) {
		int last_dev = sbi->s_ndevs - 1;
		int dev_segs =
			le32_to_cpu(raw_sb->devs[last_dev].total_segments);

		raw_sb->devs[last_dev].total_segments =
						cpu_to_le32(dev_segs + segs);
	}

	f2fs_up_write(&sbi->sb_lock);
}

static void update_fs_metadata(struct f2fs_sb_info *sbi, int secs)
{
	int segs = secs * SEGS_PER_SEC(sbi);
	long long blks = SEGS_TO_BLKS(sbi, segs);
	long long user_block_count =
				le64_to_cpu(F2FS_CKPT(sbi)->user_block_count);

	SM_I(sbi)->segment_count = (int)SM_I(sbi)->segment_count + segs;
	MAIN_SEGS(sbi) = (int)MAIN_SEGS(sbi) + segs;
	MAIN_SECS(sbi) += secs;
	if (sbi->allocate_section_hint > MAIN_SECS(sbi))
		sbi->allocate_section_hint = MAIN_SECS(sbi);
	FREE_I(sbi)->free_sections = (int)FREE_I(sbi)->free_sections + secs;
	FREE_I(sbi)->free_segments = (int)FREE_I(sbi)->free_segments + segs;
	F2FS_CKPT(sbi)->user_block_count = cpu_to_le64(user_block_count + blks);

	if (f2fs_is_multi_device(sbi)) {
		int last_dev = sbi->s_ndevs - 1;

		sbi->allocate_section_hint = FDEV(0).total_segments /
					SEGS_PER_SEC(sbi);

		FDEV(last_dev).total_segments =
				(int)FDEV(last_dev).total_segments + segs;
		FDEV(last_dev).end_blk =
				(long long)FDEV(last_dev).end_blk + blks;
#ifdef CONFIG_BLK_DEV_ZONED
		FDEV(last_dev).nr_blkz = FDEV(last_dev).nr_blkz +
					div_u64(blks, sbi->blocks_per_blkz);
#endif
	}
}

int f2fs_resize_fs(struct file *filp, __u64 block_count)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(file_inode(filp));
	__u64 old_block_count, shrunk_blocks;
	struct cp_control cpc = { CP_RESIZE, 0, 0, 0 };
	unsigned int secs;
	int err = 0;
	__u32 rem;

	old_block_count = le64_to_cpu(F2FS_RAW_SUPER(sbi)->block_count);
	if (block_count > old_block_count)
		return -EINVAL;

	if (f2fs_is_multi_device(sbi)) {
		int last_dev = sbi->s_ndevs - 1;
		__u64 last_segs = FDEV(last_dev).total_segments;

		if (block_count + SEGS_TO_BLKS(sbi, last_segs) <=
								old_block_count)
			return -EINVAL;
	}

	/* new fs size should align to section size */
	div_u64_rem(block_count, BLKS_PER_SEC(sbi), &rem);
	if (rem)
		return -EINVAL;

	if (block_count == old_block_count)
		return 0;

	if (is_sbi_flag_set(sbi, SBI_NEED_FSCK)) {
		f2fs_err(sbi, "Should run fsck to repair first.");
		return -EFSCORRUPTED;
	}

	if (test_opt(sbi, DISABLE_CHECKPOINT)) {
		f2fs_err(sbi, "Checkpoint should be enabled.");
		return -EINVAL;
	}

	err = mnt_want_write_file(filp);
	if (err)
		return err;

	shrunk_blocks = old_block_count - block_count;
	secs = div_u64(shrunk_blocks, BLKS_PER_SEC(sbi));

	/* stop other GC */
	if (!f2fs_down_write_trylock(&sbi->gc_lock)) {
		err = -EAGAIN;
		goto out_drop_write;
	}

	/* stop CP to protect MAIN_SEC in free_segment_range */
	f2fs_lock_op(sbi);

	spin_lock(&sbi->stat_lock);
	if (shrunk_blocks + valid_user_blocks(sbi) +
		sbi->current_reserved_blocks + sbi->unusable_block_count +
		F2FS_OPTION(sbi).root_reserved_blocks > sbi->user_block_count)
		err = -ENOSPC;
	spin_unlock(&sbi->stat_lock);

	if (err)
		goto out_unlock;

	err = free_segment_range(sbi, secs, true);

out_unlock:
	f2fs_unlock_op(sbi);
	f2fs_up_write(&sbi->gc_lock);
out_drop_write:
	mnt_drop_write_file(filp);
	if (err)
		return err;

	err = freeze_super(sbi->sb, FREEZE_HOLDER_KERNEL, NULL);
	if (err)
		return err;

	if (f2fs_readonly(sbi->sb)) {
		err = thaw_super(sbi->sb, FREEZE_HOLDER_KERNEL, NULL);
		if (err)
			return err;
		return -EROFS;
	}

	f2fs_down_write(&sbi->gc_lock);
	f2fs_down_write(&sbi->cp_global_sem);

	spin_lock(&sbi->stat_lock);
	if (shrunk_blocks + valid_user_blocks(sbi) +
		sbi->current_reserved_blocks + sbi->unusable_block_count +
		F2FS_OPTION(sbi).root_reserved_blocks > sbi->user_block_count)
		err = -ENOSPC;
	else
		sbi->user_block_count -= shrunk_blocks;
	spin_unlock(&sbi->stat_lock);
	if (err)
		goto out_err;

	set_sbi_flag(sbi, SBI_IS_RESIZEFS);
	err = free_segment_range(sbi, secs, false);
	if (err)
		goto recover_out;

	update_sb_metadata(sbi, -secs);

	err = f2fs_commit_super(sbi, false);
	if (err) {
		update_sb_metadata(sbi, secs);
		goto recover_out;
	}

	update_fs_metadata(sbi, -secs);
	clear_sbi_flag(sbi, SBI_IS_RESIZEFS);
	set_sbi_flag(sbi, SBI_IS_DIRTY);

	stat_inc_cp_call_count(sbi, TOTAL_CALL);
	err = f2fs_write_checkpoint(sbi, &cpc);
	if (err) {
		update_fs_metadata(sbi, secs);
		update_sb_metadata(sbi, secs);
		f2fs_commit_super(sbi, false);
	}
recover_out:
	clear_sbi_flag(sbi, SBI_IS_RESIZEFS);
	if (err) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_err(sbi, "resize_fs failed, should run fsck to repair!");

		spin_lock(&sbi->stat_lock);
		sbi->user_block_count += shrunk_blocks;
		spin_unlock(&sbi->stat_lock);
	}
out_err:
	f2fs_up_write(&sbi->cp_global_sem);
	f2fs_up_write(&sbi->gc_lock);
	thaw_super(sbi->sb, FREEZE_HOLDER_KERNEL, NULL);
	return err;
}
