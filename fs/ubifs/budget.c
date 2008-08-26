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
 * Authors: Adrian Hunter
 *          Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file implements the budgeting sub-system which is responsible for UBIFS
 * space management.
 *
 * Factors such as compression, wasted space at the ends of LEBs, space in other
 * journal heads, the effect of updates on the index, and so on, make it
 * impossible to accurately predict the amount of space needed. Consequently
 * approximations are used.
 */

#include "ubifs.h"
#include <linux/writeback.h>
#include <asm/div64.h>

/*
 * When pessimistic budget calculations say that there is no enough space,
 * UBIFS starts writing back dirty inodes and pages, doing garbage collection,
 * or committing. The below constants define maximum number of times UBIFS
 * repeats the operations.
 */
#define MAX_SHRINK_RETRIES 8
#define MAX_GC_RETRIES     4
#define MAX_CMT_RETRIES    2
#define MAX_NOSPC_RETRIES  1

/*
 * The below constant defines amount of dirty pages which should be written
 * back at when trying to shrink the liability.
 */
#define NR_TO_WRITE 16

/**
 * struct retries_info - information about re-tries while making free space.
 * @prev_liability: previous liability
 * @shrink_cnt: how many times the liability was shrinked
 * @shrink_retries: count of liability shrink re-tries (increased when
 *                  liability does not shrink)
 * @try_gc: GC should be tried first
 * @gc_retries: how many times GC was run
 * @cmt_retries: how many times commit has been done
 * @nospc_retries: how many times GC returned %-ENOSPC
 *
 * Since we consider budgeting to be the fast-path, and this structure has to
 * be allocated on stack and zeroed out, we make it smaller using bit-fields.
 */
struct retries_info {
	long long prev_liability;
	unsigned int shrink_cnt;
	unsigned int shrink_retries:5;
	unsigned int try_gc:1;
	unsigned int gc_retries:4;
	unsigned int cmt_retries:3;
	unsigned int nospc_retries:1;
};

/**
 * shrink_liability - write-back some dirty pages/inodes.
 * @c: UBIFS file-system description object
 * @nr_to_write: how many dirty pages to write-back
 *
 * This function shrinks UBIFS liability by means of writing back some amount
 * of dirty inodes and their pages. Returns the amount of pages which were
 * written back. The returned value does not include dirty inodes which were
 * synchronized.
 *
 * Note, this function synchronizes even VFS inodes which are locked
 * (@i_mutex) by the caller of the budgeting function, because write-back does
 * not touch @i_mutex.
 */
static int shrink_liability(struct ubifs_info *c, int nr_to_write)
{
	int nr_written;
	struct writeback_control wbc = {
		.sync_mode   = WB_SYNC_NONE,
		.range_end   = LLONG_MAX,
		.nr_to_write = nr_to_write,
	};

	generic_sync_sb_inodes(c->vfs_sb, &wbc);
	nr_written = nr_to_write - wbc.nr_to_write;

	if (!nr_written) {
		/*
		 * Re-try again but wait on pages/inodes which are being
		 * written-back concurrently (e.g., by pdflush).
		 */
		memset(&wbc, 0, sizeof(struct writeback_control));
		wbc.sync_mode   = WB_SYNC_ALL;
		wbc.range_end   = LLONG_MAX;
		wbc.nr_to_write = nr_to_write;
		generic_sync_sb_inodes(c->vfs_sb, &wbc);
		nr_written = nr_to_write - wbc.nr_to_write;
	}

	dbg_budg("%d pages were written back", nr_written);
	return nr_written;
}


/**
 * run_gc - run garbage collector.
 * @c: UBIFS file-system description object
 *
 * This function runs garbage collector to make some more free space. Returns
 * zero if a free LEB has been produced, %-EAGAIN if commit is required, and a
 * negative error code in case of failure.
 */
static int run_gc(struct ubifs_info *c)
{
	int err, lnum;

	/* Make some free space by garbage-collecting dirty space */
	down_read(&c->commit_sem);
	lnum = ubifs_garbage_collect(c, 1);
	up_read(&c->commit_sem);
	if (lnum < 0)
		return lnum;

	/* GC freed one LEB, return it to lprops */
	dbg_budg("GC freed LEB %d", lnum);
	err = ubifs_return_leb(c, lnum);
	if (err)
		return err;
	return 0;
}

/**
 * make_free_space - make more free space on the file-system.
 * @c: UBIFS file-system description object
 * @ri: information about previous invocations of this function
 *
 * This function is called when an operation cannot be budgeted because there
 * is supposedly no free space. But in most cases there is some free space:
 *   o budgeting is pessimistic, so it always budgets more then it is actually
 *     needed, so shrinking the liability is one way to make free space - the
 *     cached data will take less space then it was budgeted for;
 *   o GC may turn some dark space into free space (budgeting treats dark space
 *     as not available);
 *   o commit may free some LEB, i.e., turn freeable LEBs into free LEBs.
 *
 * So this function tries to do the above. Returns %-EAGAIN if some free space
 * was presumably made and the caller has to re-try budgeting the operation.
 * Returns %-ENOSPC if it couldn't do more free space, and other negative error
 * codes on failures.
 */
static int make_free_space(struct ubifs_info *c, struct retries_info *ri)
{
	int err;

	/*
	 * If we have some dirty pages and inodes (liability), try to write
	 * them back unless this was tried too many times without effect
	 * already.
	 */
	if (ri->shrink_retries < MAX_SHRINK_RETRIES && !ri->try_gc) {
		long long liability;

		spin_lock(&c->space_lock);
		liability = c->budg_idx_growth + c->budg_data_growth +
			    c->budg_dd_growth;
		spin_unlock(&c->space_lock);

		if (ri->prev_liability >= liability) {
			/* Liability does not shrink, next time try GC then */
			ri->shrink_retries += 1;
			if (ri->gc_retries < MAX_GC_RETRIES)
				ri->try_gc = 1;
			dbg_budg("liability did not shrink: retries %d of %d",
				 ri->shrink_retries, MAX_SHRINK_RETRIES);
		}

		dbg_budg("force write-back (count %d)", ri->shrink_cnt);
		shrink_liability(c, NR_TO_WRITE + ri->shrink_cnt);

		ri->prev_liability = liability;
		ri->shrink_cnt += 1;
		return -EAGAIN;
	}

	/*
	 * Try to run garbage collector unless it was already tried too many
	 * times.
	 */
	if (ri->gc_retries < MAX_GC_RETRIES) {
		ri->gc_retries += 1;
		dbg_budg("run GC, retries %d of %d",
			 ri->gc_retries, MAX_GC_RETRIES);

		ri->try_gc = 0;
		err = run_gc(c);
		if (!err)
			return -EAGAIN;

		if (err == -EAGAIN) {
			dbg_budg("GC asked to commit");
			err = ubifs_run_commit(c);
			if (err)
				return err;
			return -EAGAIN;
		}

		if (err != -ENOSPC)
			return err;

		/*
		 * GC could not make any progress. If this is the first time,
		 * then it makes sense to try to commit, because it might make
		 * some dirty space.
		 */
		dbg_budg("GC returned -ENOSPC, retries %d",
			 ri->nospc_retries);
		if (ri->nospc_retries >= MAX_NOSPC_RETRIES)
			return err;
		ri->nospc_retries += 1;
	}

	/* Neither GC nor write-back helped, try to commit */
	if (ri->cmt_retries < MAX_CMT_RETRIES) {
		ri->cmt_retries += 1;
		dbg_budg("run commit, retries %d of %d",
			 ri->cmt_retries, MAX_CMT_RETRIES);
		err = ubifs_run_commit(c);
		if (err)
			return err;
		return -EAGAIN;
	}
	return -ENOSPC;
}

/**
 * ubifs_calc_min_idx_lebs - calculate amount of eraseblocks for the index.
 * @c: UBIFS file-system description object
 *
 * This function calculates and returns the number of eraseblocks which should
 * be kept for index usage.
 */
int ubifs_calc_min_idx_lebs(struct ubifs_info *c)
{
	int ret;
	uint64_t idx_size;

	idx_size = c->old_idx_sz + c->budg_idx_growth + c->budg_uncommitted_idx;

	/* And make sure we have thrice the index size of space reserved */
	idx_size = idx_size + (idx_size << 1);

	/*
	 * We do not maintain 'old_idx_size' as 'old_idx_lebs'/'old_idx_bytes'
	 * pair, nor similarly the two variables for the new index size, so we
	 * have to do this costly 64-bit division on fast-path.
	 */
	if (do_div(idx_size, c->leb_size - c->max_idx_node_sz))
		ret = idx_size + 1;
	else
		ret = idx_size;
	/*
	 * The index head is not available for the in-the-gaps method, so add an
	 * extra LEB to compensate.
	 */
	ret += 1;
	/*
	 * At present the index needs at least 2 LEBs: one for the index head
	 * and one for in-the-gaps method (which currently does not cater for
	 * the index head and so excludes it from consideration).
	 */
	if (ret < 2)
		ret = 2;
	return ret;
}

/**
 * ubifs_calc_available - calculate available FS space.
 * @c: UBIFS file-system description object
 * @min_idx_lebs: minimum number of LEBs reserved for the index
 *
 * This function calculates and returns amount of FS space available for use.
 */
long long ubifs_calc_available(const struct ubifs_info *c, int min_idx_lebs)
{
	int subtract_lebs;
	long long available;

	/*
	 * Force the amount available to the total size reported if the used
	 * space is zero.
	 */
	if (c->lst.total_used <= UBIFS_INO_NODE_SZ &&
	    c->budg_data_growth + c->budg_dd_growth == 0) {
		/* Do the same calculation as for c->block_cnt */
		available = c->main_lebs - 2;
		available *= c->leb_size - c->dark_wm;
		return available;
	}

	available = c->main_bytes - c->lst.total_used;

	/*
	 * Now 'available' contains theoretically available flash space
	 * assuming there is no index, so we have to subtract the space which
	 * is reserved for the index.
	 */
	subtract_lebs = min_idx_lebs;

	/* Take into account that GC reserves one LEB for its own needs */
	subtract_lebs += 1;

	/*
	 * The GC journal head LEB is not really accessible. And since
	 * different write types go to different heads, we may count only on
	 * one head's space.
	 */
	subtract_lebs += c->jhead_cnt - 1;

	/* We also reserve one LEB for deletions, which bypass budgeting */
	subtract_lebs += 1;

	available -= (long long)subtract_lebs * c->leb_size;

	/* Subtract the dead space which is not available for use */
	available -= c->lst.total_dead;

	/*
	 * Subtract dark space, which might or might not be usable - it depends
	 * on the data which we have on the media and which will be written. If
	 * this is a lot of uncompressed or not-compressible data, the dark
	 * space cannot be used.
	 */
	available -= c->lst.total_dark;

	/*
	 * However, there is more dark space. The index may be bigger than
	 * @min_idx_lebs. Those extra LEBs are assumed to be available, but
	 * their dark space is not included in total_dark, so it is subtracted
	 * here.
	 */
	if (c->lst.idx_lebs > min_idx_lebs) {
		subtract_lebs = c->lst.idx_lebs - min_idx_lebs;
		available -= subtract_lebs * c->dark_wm;
	}

	/* The calculations are rough and may end up with a negative number */
	return available > 0 ? available : 0;
}

/**
 * can_use_rp - check whether the user is allowed to use reserved pool.
 * @c: UBIFS file-system description object
 *
 * UBIFS has so-called "reserved pool" which is flash space reserved
 * for the superuser and for uses whose UID/GID is recorded in UBIFS superblock.
 * This function checks whether current user is allowed to use reserved pool.
 * Returns %1  current user is allowed to use reserved pool and %0 otherwise.
 */
static int can_use_rp(struct ubifs_info *c)
{
	if (current->fsuid == c->rp_uid || capable(CAP_SYS_RESOURCE) ||
	    (c->rp_gid != 0 && in_group_p(c->rp_gid)))
		return 1;
	return 0;
}

/**
 * do_budget_space - reserve flash space for index and data growth.
 * @c: UBIFS file-system description object
 *
 * This function makes sure UBIFS has enough free eraseblocks for index growth
 * and data.
 *
 * When budgeting index space, UBIFS reserves thrice as many LEBs as the index
 * would take if it was consolidated and written to the flash. This guarantees
 * that the "in-the-gaps" commit method always succeeds and UBIFS will always
 * be able to commit dirty index. So this function basically adds amount of
 * budgeted index space to the size of the current index, multiplies this by 3,
 * and makes sure this does not exceed the amount of free eraseblocks.
 *
 * Notes about @c->min_idx_lebs and @c->lst.idx_lebs variables:
 * o @c->lst.idx_lebs is the number of LEBs the index currently uses. It might
 *    be large, because UBIFS does not do any index consolidation as long as
 *    there is free space. IOW, the index may take a lot of LEBs, but the LEBs
 *    will contain a lot of dirt.
 * o @c->min_idx_lebs is the the index presumably takes. IOW, the index may be
 *   consolidated to take up to @c->min_idx_lebs LEBs.
 *
 * This function returns zero in case of success, and %-ENOSPC in case of
 * failure.
 */
static int do_budget_space(struct ubifs_info *c)
{
	long long outstanding, available;
	int lebs, rsvd_idx_lebs, min_idx_lebs;

	/* First budget index space */
	min_idx_lebs = ubifs_calc_min_idx_lebs(c);

	/* Now 'min_idx_lebs' contains number of LEBs to reserve */
	if (min_idx_lebs > c->lst.idx_lebs)
		rsvd_idx_lebs = min_idx_lebs - c->lst.idx_lebs;
	else
		rsvd_idx_lebs = 0;

	/*
	 * The number of LEBs that are available to be used by the index is:
	 *
	 *    @c->lst.empty_lebs + @c->freeable_cnt + @c->idx_gc_cnt -
	 *    @c->lst.taken_empty_lebs
	 *
	 * @empty_lebs are available because they are empty. @freeable_cnt are
	 * available because they contain only free and dirty space and the
	 * index allocation always occurs after wbufs are synch'ed.
	 * @idx_gc_cnt are available because they are index LEBs that have been
	 * garbage collected (including trivial GC) and are awaiting the commit
	 * before they can be unmapped - note that the in-the-gaps method will
	 * grab these if it needs them. @taken_empty_lebs are empty_lebs that
	 * have already been allocated for some purpose (also includes those
	 * LEBs on the @idx_gc list).
	 *
	 * Note, @taken_empty_lebs may temporarily be higher by one because of
	 * the way we serialize LEB allocations and budgeting. See a comment in
	 * 'ubifs_find_free_space()'.
	 */
	lebs = c->lst.empty_lebs + c->freeable_cnt + c->idx_gc_cnt -
	       c->lst.taken_empty_lebs;
	if (unlikely(rsvd_idx_lebs > lebs)) {
		dbg_budg("out of indexing space: min_idx_lebs %d (old %d), "
			 "rsvd_idx_lebs %d", min_idx_lebs, c->min_idx_lebs,
			 rsvd_idx_lebs);
		return -ENOSPC;
	}

	available = ubifs_calc_available(c, min_idx_lebs);
	outstanding = c->budg_data_growth + c->budg_dd_growth;

	if (unlikely(available < outstanding)) {
		dbg_budg("out of data space: available %lld, outstanding %lld",
			 available, outstanding);
		return -ENOSPC;
	}

	if (available - outstanding <= c->rp_size && !can_use_rp(c))
		return -ENOSPC;

	c->min_idx_lebs = min_idx_lebs;
	return 0;
}

/**
 * calc_idx_growth - calculate approximate index growth from budgeting request.
 * @c: UBIFS file-system description object
 * @req: budgeting request
 *
 * For now we assume each new node adds one znode. But this is rather poor
 * approximation, though.
 */
static int calc_idx_growth(const struct ubifs_info *c,
			   const struct ubifs_budget_req *req)
{
	int znodes;

	znodes = req->new_ino + (req->new_page << UBIFS_BLOCKS_PER_PAGE_SHIFT) +
		 req->new_dent;
	return znodes * c->max_idx_node_sz;
}

/**
 * calc_data_growth - calculate approximate amount of new data from budgeting
 * request.
 * @c: UBIFS file-system description object
 * @req: budgeting request
 */
static int calc_data_growth(const struct ubifs_info *c,
			    const struct ubifs_budget_req *req)
{
	int data_growth;

	data_growth = req->new_ino  ? c->inode_budget : 0;
	if (req->new_page)
		data_growth += c->page_budget;
	if (req->new_dent)
		data_growth += c->dent_budget;
	data_growth += req->new_ino_d;
	return data_growth;
}

/**
 * calc_dd_growth - calculate approximate amount of data which makes other data
 * dirty from budgeting request.
 * @c: UBIFS file-system description object
 * @req: budgeting request
 */
static int calc_dd_growth(const struct ubifs_info *c,
			  const struct ubifs_budget_req *req)
{
	int dd_growth;

	dd_growth = req->dirtied_page ? c->page_budget : 0;

	if (req->dirtied_ino)
		dd_growth += c->inode_budget << (req->dirtied_ino - 1);
	if (req->mod_dent)
		dd_growth += c->dent_budget;
	dd_growth += req->dirtied_ino_d;
	return dd_growth;
}

/**
 * ubifs_budget_space - ensure there is enough space to complete an operation.
 * @c: UBIFS file-system description object
 * @req: budget request
 *
 * This function allocates budget for an operation. It uses pessimistic
 * approximation of how much flash space the operation needs. The goal of this
 * function is to make sure UBIFS always has flash space to flush all dirty
 * pages, dirty inodes, and dirty znodes (liability). This function may force
 * commit, garbage-collection or write-back. Returns zero in case of success,
 * %-ENOSPC if there is no free space and other negative error codes in case of
 * failures.
 */
int ubifs_budget_space(struct ubifs_info *c, struct ubifs_budget_req *req)
{
	int uninitialized_var(cmt_retries), uninitialized_var(wb_retries);
	int err, idx_growth, data_growth, dd_growth;
	struct retries_info ri;

	ubifs_assert(req->new_page <= 1);
	ubifs_assert(req->dirtied_page <= 1);
	ubifs_assert(req->new_dent <= 1);
	ubifs_assert(req->mod_dent <= 1);
	ubifs_assert(req->new_ino <= 1);
	ubifs_assert(req->new_ino_d <= UBIFS_MAX_INO_DATA);
	ubifs_assert(req->dirtied_ino <= 4);
	ubifs_assert(req->dirtied_ino_d <= UBIFS_MAX_INO_DATA * 4);
	ubifs_assert(!(req->new_ino_d & 7));
	ubifs_assert(!(req->dirtied_ino_d & 7));

	data_growth = calc_data_growth(c, req);
	dd_growth = calc_dd_growth(c, req);
	if (!data_growth && !dd_growth)
		return 0;
	idx_growth = calc_idx_growth(c, req);
	memset(&ri, 0, sizeof(struct retries_info));

again:
	spin_lock(&c->space_lock);
	ubifs_assert(c->budg_idx_growth >= 0);
	ubifs_assert(c->budg_data_growth >= 0);
	ubifs_assert(c->budg_dd_growth >= 0);

	if (unlikely(c->nospace) && (c->nospace_rp || !can_use_rp(c))) {
		dbg_budg("no space");
		spin_unlock(&c->space_lock);
		return -ENOSPC;
	}

	c->budg_idx_growth += idx_growth;
	c->budg_data_growth += data_growth;
	c->budg_dd_growth += dd_growth;

	err = do_budget_space(c);
	if (likely(!err)) {
		req->idx_growth = idx_growth;
		req->data_growth = data_growth;
		req->dd_growth = dd_growth;
		spin_unlock(&c->space_lock);
		return 0;
	}

	/* Restore the old values */
	c->budg_idx_growth -= idx_growth;
	c->budg_data_growth -= data_growth;
	c->budg_dd_growth -= dd_growth;
	spin_unlock(&c->space_lock);

	if (req->fast) {
		dbg_budg("no space for fast budgeting");
		return err;
	}

	err = make_free_space(c, &ri);
	if (err == -EAGAIN) {
		dbg_budg("try again");
		cond_resched();
		goto again;
	} else if (err == -ENOSPC) {
		dbg_budg("FS is full, -ENOSPC");
		c->nospace = 1;
		if (can_use_rp(c) || c->rp_size == 0)
			c->nospace_rp = 1;
		smp_wmb();
	} else
		ubifs_err("cannot budget space, error %d", err);
	return err;
}

/**
 * ubifs_release_budget - release budgeted free space.
 * @c: UBIFS file-system description object
 * @req: budget request
 *
 * This function releases the space budgeted by 'ubifs_budget_space()'. Note,
 * since the index changes (which were budgeted for in @req->idx_growth) will
 * only be written to the media on commit, this function moves the index budget
 * from @c->budg_idx_growth to @c->budg_uncommitted_idx. The latter will be
 * zeroed by the commit operation.
 */
void ubifs_release_budget(struct ubifs_info *c, struct ubifs_budget_req *req)
{
	ubifs_assert(req->new_page <= 1);
	ubifs_assert(req->dirtied_page <= 1);
	ubifs_assert(req->new_dent <= 1);
	ubifs_assert(req->mod_dent <= 1);
	ubifs_assert(req->new_ino <= 1);
	ubifs_assert(req->new_ino_d <= UBIFS_MAX_INO_DATA);
	ubifs_assert(req->dirtied_ino <= 4);
	ubifs_assert(req->dirtied_ino_d <= UBIFS_MAX_INO_DATA * 4);
	ubifs_assert(!(req->new_ino_d & 7));
	ubifs_assert(!(req->dirtied_ino_d & 7));
	if (!req->recalculate) {
		ubifs_assert(req->idx_growth >= 0);
		ubifs_assert(req->data_growth >= 0);
		ubifs_assert(req->dd_growth >= 0);
	}

	if (req->recalculate) {
		req->data_growth = calc_data_growth(c, req);
		req->dd_growth = calc_dd_growth(c, req);
		req->idx_growth = calc_idx_growth(c, req);
	}

	if (!req->data_growth && !req->dd_growth)
		return;

	c->nospace = c->nospace_rp = 0;
	smp_wmb();

	spin_lock(&c->space_lock);
	c->budg_idx_growth -= req->idx_growth;
	c->budg_uncommitted_idx += req->idx_growth;
	c->budg_data_growth -= req->data_growth;
	c->budg_dd_growth -= req->dd_growth;
	c->min_idx_lebs = ubifs_calc_min_idx_lebs(c);

	ubifs_assert(c->budg_idx_growth >= 0);
	ubifs_assert(c->budg_data_growth >= 0);
	ubifs_assert(c->budg_dd_growth >= 0);
	ubifs_assert(c->min_idx_lebs < c->main_lebs);
	ubifs_assert(!(c->budg_idx_growth & 7));
	ubifs_assert(!(c->budg_data_growth & 7));
	ubifs_assert(!(c->budg_dd_growth & 7));
	spin_unlock(&c->space_lock);
}

/**
 * ubifs_convert_page_budget - convert budget of a new page.
 * @c: UBIFS file-system description object
 *
 * This function converts budget which was allocated for a new page of data to
 * the budget of changing an existing page of data. The latter is smaller then
 * the former, so this function only does simple re-calculation and does not
 * involve any write-back.
 */
void ubifs_convert_page_budget(struct ubifs_info *c)
{
	spin_lock(&c->space_lock);
	/* Release the index growth reservation */
	c->budg_idx_growth -= c->max_idx_node_sz << UBIFS_BLOCKS_PER_PAGE_SHIFT;
	/* Release the data growth reservation */
	c->budg_data_growth -= c->page_budget;
	/* Increase the dirty data growth reservation instead */
	c->budg_dd_growth += c->page_budget;
	/* And re-calculate the indexing space reservation */
	c->min_idx_lebs = ubifs_calc_min_idx_lebs(c);
	spin_unlock(&c->space_lock);
}

/**
 * ubifs_release_dirty_inode_budget - release dirty inode budget.
 * @c: UBIFS file-system description object
 * @ui: UBIFS inode to release the budget for
 *
 * This function releases budget corresponding to a dirty inode. It is usually
 * called when after the inode has been written to the media and marked as
 * clean.
 */
void ubifs_release_dirty_inode_budget(struct ubifs_info *c,
				      struct ubifs_inode *ui)
{
	struct ubifs_budget_req req;

	memset(&req, 0, sizeof(struct ubifs_budget_req));
	req.dd_growth = c->inode_budget + ALIGN(ui->data_len, 8);
	ubifs_release_budget(c, &req);
}

/**
 * ubifs_budg_get_free_space - return amount of free space.
 * @c: UBIFS file-system description object
 *
 * This function returns amount of free space on the file-system.
 */
long long ubifs_budg_get_free_space(struct ubifs_info *c)
{
	int min_idx_lebs, rsvd_idx_lebs;
	long long available, outstanding, free;

	/* Do exactly the same calculations as in 'do_budget_space()' */
	spin_lock(&c->space_lock);
	min_idx_lebs = ubifs_calc_min_idx_lebs(c);

	if (min_idx_lebs > c->lst.idx_lebs)
		rsvd_idx_lebs = min_idx_lebs - c->lst.idx_lebs;
	else
		rsvd_idx_lebs = 0;

	if (rsvd_idx_lebs > c->lst.empty_lebs + c->freeable_cnt + c->idx_gc_cnt
				- c->lst.taken_empty_lebs) {
		spin_unlock(&c->space_lock);
		return 0;
	}

	available = ubifs_calc_available(c, min_idx_lebs);
	outstanding = c->budg_data_growth + c->budg_dd_growth;
	c->min_idx_lebs = min_idx_lebs;
	spin_unlock(&c->space_lock);

	if (available > outstanding)
		free = ubifs_reported_space(c, available - outstanding);
	else
		free = 0;
	return free;
}
