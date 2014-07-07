/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file implements UBIFS shrinker which evicts clean znodes from the TNC
 * tree when Linux VM needs more RAM.
 *
 * We do not implement any LRU lists to find oldest znodes to free because it
 * would add additional overhead to the file system fast paths. So the shrinker
 * just walks the TNC tree when searching for znodes to free.
 *
 * If the root of a TNC sub-tree is clean and old enough, then the children are
 * also clean and old enough. So the shrinker walks the TNC in level order and
 * dumps entire sub-trees.
 *
 * The age of znodes is just the time-stamp when they were last looked at.
 * The current shrinker first tries to evict old znodes, then young ones.
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
static unsigned int shrinker_run_no;

/* Protects 'ubifs_infos' list */
DEFINE_SPINLOCK(ubifs_infos_lock);

/* Global clean znode counter (for all mounted UBIFS instances) */
atomic_long_t ubifs_clean_zn_cnt;

/**
 * shrink_tnc - shrink TNC tree.
 * @c: UBIFS file-system description object
 * @nr: number of znodes to free
 * @age: the age of znodes to free
 * @contention: if any contention, this is set to %1
 *
 * This function traverses TNC tree and frees clean znodes. It does not free
 * clean znodes which younger then @age. Returns number of freed znodes.
 */
static int shrink_tnc(struct ubifs_info *c, int nr, int age, int *contention)
{
	int total_freed = 0;
	struct ubifs_znode *znode, *zprev;
	int time = get_seconds();

	ubifs_assert(mutex_is_locked(&c->umount_mutex));
	ubifs_assert(mutex_is_locked(&c->tnc_mutex));

	if (!c->zroot.znode || atomic_long_read(&c->clean_zn_cnt) == 0)
		return 0;

	/*
	 * Traverse the TNC tree in levelorder manner, so that it is possible
	 * to destroy large sub-trees. Indeed, if a znode is old, then all its
	 * children are older or of the same age.
	 *
	 * Note, we are holding 'c->tnc_mutex', so we do not have to lock the
	 * 'c->space_lock' when _reading_ 'c->clean_zn_cnt', because it is
	 * changed only when the 'c->tnc_mutex' is held.
	 */
	zprev = NULL;
	znode = ubifs_tnc_levelorder_next(c->zroot.znode, NULL);
	while (znode && total_freed < nr &&
	       atomic_long_read(&c->clean_zn_cnt) > 0) {
		int freed;

		/*
		 * If the znode is clean, but it is in the 'c->cnext' list, this
		 * means that this znode has just been written to flash as a
		 * part of commit and was marked clean. They will be removed
		 * from the list at end commit. We cannot change the list,
		 * because it is not protected by any mutex (design decision to
		 * make commit really independent and parallel to main I/O). So
		 * we just skip these znodes.
		 *
		 * Note, the 'clean_zn_cnt' counters are not updated until
		 * after the commit, so the UBIFS shrinker does not report
		 * the znodes which are in the 'c->cnext' list as freeable.
		 *
		 * Also note, if the root of a sub-tree is not in 'c->cnext',
		 * then the whole sub-tree is not in 'c->cnext' as well, so it
		 * is safe to dump whole sub-tree.
		 */

		if (znode->cnext) {
			/*
			 * Very soon these znodes will be removed from the list
			 * and become freeable.
			 */
			*contention = 1;
		} else if (!ubifs_zn_dirty(znode) &&
			   abs(time - znode->time) >= age) {
			if (znode->parent)
				znode->parent->zbranch[znode->iip].znode = NULL;
			else
				c->zroot.znode = NULL;

			freed = ubifs_destroy_tnc_subtree(znode);
			atomic_long_sub(freed, &ubifs_clean_zn_cnt);
			atomic_long_sub(freed, &c->clean_zn_cnt);
			total_freed += freed;
			znode = zprev;
		}

		if (unlikely(!c->zroot.znode))
			break;

		zprev = znode;
		znode = ubifs_tnc_levelorder_next(c->zroot.znode, znode);
		cond_resched();
	}

	return total_freed;
}

/**
 * shrink_tnc_trees - shrink UBIFS TNC trees.
 * @nr: number of znodes to free
 * @age: the age of znodes to free
 * @contention: if any contention, this is set to %1
 *
 * This function walks the list of mounted UBIFS file-systems and frees clean
 * znodes which are older than @age, until at least @nr znodes are freed.
 * Returns the number of freed znodes.
 */
static int shrink_tnc_trees(int nr, int age, int *contention)
{
	struct ubifs_info *c;
	struct list_head *p;
	unsigned int run_no;
	int freed = 0;

	spin_lock(&ubifs_infos_lock);
	do {
		run_no = ++shrinker_run_no;
	} while (run_no == 0);
	/* Iterate over all mounted UBIFS file-systems and try to shrink them */
	p = ubifs_infos.next;
	while (p != &ubifs_infos) {
		c = list_entry(p, struct ubifs_info, infos_list);
		/*
		 * We move the ones we do to the end of the list, so we stop
		 * when we see one we have already done.
		 */
		if (c->shrinker_run_no == run_no)
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
		 * OK, now we have TNC locked, the file-system cannot go away -
		 * it is safe to reap the cache.
		 */
		c->shrinker_run_no = run_no;
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
 * %-1 if a thread was kicked or there is another reason to assume the memory
 * will soon be freed or become freeable. If there are no dirty znodes, returns
 * %0.
 */
static int kick_a_thread(void)
{
	int i;
	struct ubifs_info *c;

	/*
	 * Iterate over all mounted UBIFS file-systems and find out if there is
	 * already an ongoing commit operation there. If no, then iterate for
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

int ubifs_shrinker(struct shrinker *shrink, struct shrink_control *sc)
{
	int nr = sc->nr_to_scan;
	int freed, contention = 0;
	long clean_zn_cnt = atomic_long_read(&ubifs_clean_zn_cnt);

	if (nr == 0)
		/*
		 * Due to the way UBIFS updates the clean znode counter it may
		 * temporarily be negative.
		 */
		return clean_zn_cnt >= 0 ? clean_zn_cnt : 1;

	if (!clean_zn_cnt) {
		/*
		 * No clean znodes, nothing to reap. All we can do in this case
		 * is to kick background threads to start commit, which will
		 * probably make clean znodes which, in turn, will be freeable.
		 * And we return -1 which means will make VM call us again
		 * later.
		 */
		dbg_tnc("no clean znodes, kick a thread");
		return kick_a_thread();
	}

	freed = shrink_tnc_trees(nr, OLD_ZNODE_AGE, &contention);
	if (freed >= nr)
		goto out;

	dbg_tnc("not enough old znodes, try to free young ones");
	freed += shrink_tnc_trees(nr - freed, YOUNG_ZNODE_AGE, &contention);
	if (freed >= nr)
		goto out;

	dbg_tnc("not enough young znodes, free all");
	freed += shrink_tnc_trees(nr - freed, 0, &contention);

	if (!freed && contention) {
		dbg_tnc("freed nothing, but contention");
		return -1;
	}

out:
	dbg_tnc("%d znodes were freed, requested %d", freed, nr);
	return freed;
}
