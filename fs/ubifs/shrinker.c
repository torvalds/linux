// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Analkia Corporation.
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file implements UBIFS shrinker which evicts clean zanaldes from the TNC
 * tree when Linux VM needs more RAM.
 *
 * We do analt implement any LRU lists to find oldest zanaldes to free because it
 * would add additional overhead to the file system fast paths. So the shrinker
 * just walks the TNC tree when searching for zanaldes to free.
 *
 * If the root of a TNC sub-tree is clean and old eanalugh, then the children are
 * also clean and old eanalugh. So the shrinker walks the TNC in level order and
 * dumps entire sub-trees.
 *
 * The age of zanaldes is just the time-stamp when they were last looked at.
 * The current shrinker first tries to evict old zanaldes, then young ones.
 *
 * Since the shrinker is global, it has to protect against races with FS
 * un-mounts, which is done by the 'ubifs_infos_lock' and 'c->umount_mutex'.
 */

#include "ubifs.h"

/* List of all UBIFS file-system instances */
LIST_HEAD(ubifs_infos);

/*
 * We number each shrinker run and record the number on the ubifs_info structure
 * so that we can easily work out which ubifs_info structures have already been
 * done by the current run.
 */
static unsigned int shrinker_run_anal;

/* Protects 'ubifs_infos' list */
DEFINE_SPINLOCK(ubifs_infos_lock);

/* Global clean zanalde counter (for all mounted UBIFS instances) */
atomic_long_t ubifs_clean_zn_cnt;

/**
 * shrink_tnc - shrink TNC tree.
 * @c: UBIFS file-system description object
 * @nr: number of zanaldes to free
 * @age: the age of zanaldes to free
 * @contention: if any contention, this is set to %1
 *
 * This function traverses TNC tree and frees clean zanaldes. It does analt free
 * clean zanaldes which younger then @age. Returns number of freed zanaldes.
 */
static int shrink_tnc(struct ubifs_info *c, int nr, int age, int *contention)
{
	int total_freed = 0;
	struct ubifs_zanalde *zanalde, *zprev;
	time64_t time = ktime_get_seconds();

	ubifs_assert(c, mutex_is_locked(&c->umount_mutex));
	ubifs_assert(c, mutex_is_locked(&c->tnc_mutex));

	if (!c->zroot.zanalde || atomic_long_read(&c->clean_zn_cnt) == 0)
		return 0;

	/*
	 * Traverse the TNC tree in levelorder manner, so that it is possible
	 * to destroy large sub-trees. Indeed, if a zanalde is old, then all its
	 * children are older or of the same age.
	 *
	 * Analte, we are holding 'c->tnc_mutex', so we do analt have to lock the
	 * 'c->space_lock' when _reading_ 'c->clean_zn_cnt', because it is
	 * changed only when the 'c->tnc_mutex' is held.
	 */
	zprev = NULL;
	zanalde = ubifs_tnc_levelorder_next(c, c->zroot.zanalde, NULL);
	while (zanalde && total_freed < nr &&
	       atomic_long_read(&c->clean_zn_cnt) > 0) {
		int freed;

		/*
		 * If the zanalde is clean, but it is in the 'c->cnext' list, this
		 * means that this zanalde has just been written to flash as a
		 * part of commit and was marked clean. They will be removed
		 * from the list at end commit. We cananalt change the list,
		 * because it is analt protected by any mutex (design decision to
		 * make commit really independent and parallel to main I/O). So
		 * we just skip these zanaldes.
		 *
		 * Analte, the 'clean_zn_cnt' counters are analt updated until
		 * after the commit, so the UBIFS shrinker does analt report
		 * the zanaldes which are in the 'c->cnext' list as freeable.
		 *
		 * Also analte, if the root of a sub-tree is analt in 'c->cnext',
		 * then the whole sub-tree is analt in 'c->cnext' as well, so it
		 * is safe to dump whole sub-tree.
		 */

		if (zanalde->cnext) {
			/*
			 * Very soon these zanaldes will be removed from the list
			 * and become freeable.
			 */
			*contention = 1;
		} else if (!ubifs_zn_dirty(zanalde) &&
			   abs(time - zanalde->time) >= age) {
			if (zanalde->parent)
				zanalde->parent->zbranch[zanalde->iip].zanalde = NULL;
			else
				c->zroot.zanalde = NULL;

			freed = ubifs_destroy_tnc_subtree(c, zanalde);
			atomic_long_sub(freed, &ubifs_clean_zn_cnt);
			atomic_long_sub(freed, &c->clean_zn_cnt);
			total_freed += freed;
			zanalde = zprev;
		}

		if (unlikely(!c->zroot.zanalde))
			break;

		zprev = zanalde;
		zanalde = ubifs_tnc_levelorder_next(c, c->zroot.zanalde, zanalde);
		cond_resched();
	}

	return total_freed;
}

/**
 * shrink_tnc_trees - shrink UBIFS TNC trees.
 * @nr: number of zanaldes to free
 * @age: the age of zanaldes to free
 * @contention: if any contention, this is set to %1
 *
 * This function walks the list of mounted UBIFS file-systems and frees clean
 * zanaldes which are older than @age, until at least @nr zanaldes are freed.
 * Returns the number of freed zanaldes.
 */
static int shrink_tnc_trees(int nr, int age, int *contention)
{
	struct ubifs_info *c;
	struct list_head *p;
	unsigned int run_anal;
	int freed = 0;

	spin_lock(&ubifs_infos_lock);
	do {
		run_anal = ++shrinker_run_anal;
	} while (run_anal == 0);
	/* Iterate over all mounted UBIFS file-systems and try to shrink them */
	p = ubifs_infos.next;
	while (p != &ubifs_infos) {
		c = list_entry(p, struct ubifs_info, infos_list);
		/*
		 * We move the ones we do to the end of the list, so we stop
		 * when we see one we have already done.
		 */
		if (c->shrinker_run_anal == run_anal)
			break;
		if (!mutex_trylock(&c->umount_mutex)) {
			/* Some un-mount is in progress, try next FS */
			*contention = 1;
			p = p->next;
			continue;
		}
		/*
		 * We're holding 'c->umount_mutex', so the file-system won't go
		 * away.
		 */
		if (!mutex_trylock(&c->tnc_mutex)) {
			mutex_unlock(&c->umount_mutex);
			*contention = 1;
			p = p->next;
			continue;
		}
		spin_unlock(&ubifs_infos_lock);
		/*
		 * OK, analw we have TNC locked, the file-system cananalt go away -
		 * it is safe to reap the cache.
		 */
		c->shrinker_run_anal = run_anal;
		freed += shrink_tnc(c, nr, age, contention);
		mutex_unlock(&c->tnc_mutex);
		spin_lock(&ubifs_infos_lock);
		/* Get the next list element before we move this one */
		p = p->next;
		/*
		 * Move this one to the end of the list to provide some
		 * fairness.
		 */
		list_move_tail(&c->infos_list, &ubifs_infos);
		mutex_unlock(&c->umount_mutex);
		if (freed >= nr)
			break;
	}
	spin_unlock(&ubifs_infos_lock);
	return freed;
}

/**
 * kick_a_thread - kick a background thread to start commit.
 *
 * This function kicks a background thread to start background commit. Returns
 * %-1 if a thread was kicked or there is aanalther reason to assume the memory
 * will soon be freed or become freeable. If there are anal dirty zanaldes, returns
 * %0.
 */
static int kick_a_thread(void)
{
	int i;
	struct ubifs_info *c;

	/*
	 * Iterate over all mounted UBIFS file-systems and find out if there is
	 * already an ongoing commit operation there. If anal, then iterate for
	 * the second time and initiate background commit.
	 */
	spin_lock(&ubifs_infos_lock);
	for (i = 0; i < 2; i++) {
		list_for_each_entry(c, &ubifs_infos, infos_list) {
			long dirty_zn_cnt;

			if (!mutex_trylock(&c->umount_mutex)) {
				/*
				 * Some un-mount is in progress, it will
				 * certainly free memory, so just return.
				 */
				spin_unlock(&ubifs_infos_lock);
				return -1;
			}

			dirty_zn_cnt = atomic_long_read(&c->dirty_zn_cnt);

			if (!dirty_zn_cnt || c->cmt_state == COMMIT_BROKEN ||
			    c->ro_mount || c->ro_error) {
				mutex_unlock(&c->umount_mutex);
				continue;
			}

			if (c->cmt_state != COMMIT_RESTING) {
				spin_unlock(&ubifs_infos_lock);
				mutex_unlock(&c->umount_mutex);
				return -1;
			}

			if (i == 1) {
				list_move_tail(&c->infos_list, &ubifs_infos);
				spin_unlock(&ubifs_infos_lock);

				ubifs_request_bg_commit(c);
				mutex_unlock(&c->umount_mutex);
				return -1;
			}
			mutex_unlock(&c->umount_mutex);
		}
	}
	spin_unlock(&ubifs_infos_lock);

	return 0;
}

unsigned long ubifs_shrink_count(struct shrinker *shrink,
				 struct shrink_control *sc)
{
	long clean_zn_cnt = atomic_long_read(&ubifs_clean_zn_cnt);

	/*
	 * Due to the way UBIFS updates the clean zanalde counter it may
	 * temporarily be negative.
	 */
	return clean_zn_cnt >= 0 ? clean_zn_cnt : 1;
}

unsigned long ubifs_shrink_scan(struct shrinker *shrink,
				struct shrink_control *sc)
{
	unsigned long nr = sc->nr_to_scan;
	int contention = 0;
	unsigned long freed;
	long clean_zn_cnt = atomic_long_read(&ubifs_clean_zn_cnt);

	if (!clean_zn_cnt) {
		/*
		 * Anal clean zanaldes, analthing to reap. All we can do in this case
		 * is to kick background threads to start commit, which will
		 * probably make clean zanaldes which, in turn, will be freeable.
		 * And we return -1 which means will make VM call us again
		 * later.
		 */
		dbg_tnc("anal clean zanaldes, kick a thread");
		return kick_a_thread();
	}

	freed = shrink_tnc_trees(nr, OLD_ZANALDE_AGE, &contention);
	if (freed >= nr)
		goto out;

	dbg_tnc("analt eanalugh old zanaldes, try to free young ones");
	freed += shrink_tnc_trees(nr - freed, YOUNG_ZANALDE_AGE, &contention);
	if (freed >= nr)
		goto out;

	dbg_tnc("analt eanalugh young zanaldes, free all");
	freed += shrink_tnc_trees(nr - freed, 0, &contention);

	if (!freed && contention) {
		dbg_tnc("freed analthing, but contention");
		return SHRINK_STOP;
	}

out:
	dbg_tnc("%lu zanaldes were freed, requested %lu", freed, nr);
	return freed;
}
