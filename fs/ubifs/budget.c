// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
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
#include <linux/math64.h>

/*
 * When pessimistic budget calculations say that there is no enough space,
 * UBIFS starts writing back dirty inodes and pages, doing garbage collection,
 * or committing. The below constant defines maximum number of times UBIFS
 * repeats the operations.
 */
#define MAX_MKSPC_RETRIES 3

/*
 * The below constant defines amount of dirty pages which should be written
 * back at when trying to shrink the liability.
 */
#define NR_TO_WRITE 16

/**
 * shrink_liability - write-back some dirty pages/inodes.
 * @c: UBIFS file-system description object
 * @nr_to_write: how many dirty pages to write-back
 *
 * This function shrinks UBIFS liability by means of writing back some amount
 * of dirty inodes and their pages.
 *
 * Note, this function synchronizes even VFS inodes which are locked
 * (@i_mutex) by the caller of the budgeting function, because write-back does
 * not touch @i_mutex.
 */
static void shrink_liability(struct ubifs_info *c, int nr_to_write)
{
	down_read(&c->vfs_sb->s_umount);
	writeback_inodes_sb_nr(c->vfs_sb, nr_to_write, WB_REASON_FS_FREE_SPACE);
	up_read(&c->vfs_sb->s_umount);
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
	int lnum;

	/* Make some free space by garbage-collecting dirty space */
	down_read(&c->commit_sem);
	lnum = ubifs_garbage_collect(c, 1);
	up_read(&c->commit_sem);
	if (lnum < 0)
		return lnum;

	/* GC freed one LEB, return it to lprops */
	dbg_budg("GC freed LEB %d", lnum);
	return ubifs_return_leb(c, lnum);
}

/**
 * get_liability - calculate current liability.
 * @c: UBIFS file-system description object
 *
 * This function calculates and returns current UBIFS liability, i.e. the
 * amount of bytes UBIFS has "promised" to write to the media.
 */
static long long get_liability(struct ubifs_info *c)
{
	long long liab;

	spin_lock(&c->space_lock);
	liab = c->bi.idx_growth + c->bi.data_growth + c->bi.dd_growth;
	spin_unlock(&c->space_lock);
	return liab;
}

/**
 * make_free_space - make more free space on the file-system.
 * @c: UBIFS file-system description object
 *
 * This function is called when an operation cannot be budgeted because there
 * is supposedly no free space. But in most cases there is some free space:
 *   o budgeting is pessimistic, so it always budgets more than it is actually
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
static int make_free_space(struct ubifs_info *c)
{
	int err, retries = 0;
	long long liab1, liab2;

	do {
		liab1 = get_liability(c);
		/*
		 * We probably have some dirty pages or inodes (liability), try
		 * to write them back.
		 */
		dbg_budg("liability %lld, run write-back", liab1);
		shrink_liability(c, NR_TO_WRITE);

		liab2 = get_liability(c);
		if (liab2 < liab1)
			return -EAGAIN;

		dbg_budg("new liability %lld (not shrunk)", liab2);

		/* Liability did not shrink again, try GC */
		dbg_budg("Run GC");
		err = run_gc(c);
		if (!err)
			return -EAGAIN;

		if (err != -EAGAIN && err != -ENOSPC)
			/* Some real error happened */
			return err;

		dbg_budg("Run commit (retries %d)", retries);
		err = ubifs_run_commit(c);
		if (err)
			return err;
	} while (retries++ < MAX_MKSPC_RETRIES);

	return -ENOSPC;
}

/**
 * ubifs_calc_min_idx_lebs - calculate amount of LEBs for the index.
 * @c: UBIFS file-system description object
 *
 * This function calculates and returns the number of LEBs which should be kept
 * for index usage.
 */
int ubifs_calc_min_idx_lebs(struct ubifs_info *c)
{
	int idx_lebs;
	long long idx_size;

	idx_size = c->bi.old_idx_sz + c->bi.idx_growth + c->bi.uncommitted_idx;
	/* And make sure we have thrice the index size of space reserved */
	idx_size += idx_size << 1;
	/*
	 * We do not maintain 'old_idx_size' as 'old_idx_lebs'/'old_idx_bytes'
	 * pair, nor similarly the two variables for the new index size, so we
	 * have to do this costly 64-bit division on fast-path.
	 */
	idx_lebs = div_u64(idx_size + c->idx_leb_size - 1, c->idx_leb_size);
	/*
	 * The index head is not available for the in-the-gaps method, so add an
	 * extra LEB to compensate.
	 */
	idx_lebs += 1;
	if (idx_lebs < MIN_INDEX_LEBS)
		idx_lebs = MIN_INDEX_LEBS;
	return idx_lebs;
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
	 * Since different write types go to different heads, we should
	 * reserve one leb for each head.
	 */
	subtract_lebs += c->jhead_cnt;

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
	if (uid_eq(current_fsuid(), c->rp_uid) || capable(CAP_SYS_RESOURCE) ||
	    (!gid_eq(c->rp_gid, GLOBAL_ROOT_GID) && in_group_p(c->rp_gid)))
		return 1;
	return 0;
}

/**
 * do_budget_space - reserve flash space for index and data growth.
 * @c: UBIFS file-system description object
 *
 * This function makes sure UBIFS has enough free LEBs for index growth and
 * data.
 *
 * When budgeting index space, UBIFS reserves thrice as many LEBs as the index
 * would take if it was consolidated and written to the flash. This guarantees
 * that the "in-the-gaps" commit method always succeeds and UBIFS will always
 * be able to commit dirty index. So this function basically adds amount of
 * budgeted index space to the size of the current index, multiplies this by 3,
 * and makes sure this does not exceed the amount of free LEBs.
 *
 * Notes about @c->bi.min_idx_lebs and @c->lst.idx_lebs variables:
 * o @c->lst.idx_lebs is the number of LEBs the index currently uses. It might
 *    be large, because UBIFS does not do any index consolidation as long as
 *    there is free space. IOW, the index may take a lot of LEBs, but the LEBs
 *    will contain a lot of dirt.
 * o @c->bi.min_idx_lebs is the number of LEBS the index presumably takes. IOW,
 *    the index may be consolidated to take up to @c->bi.min_idx_lebs LEBs.
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
	 * @c->lst.empty_lebs are available because they are empty.
	 * @c->freeable_cnt are available because they contain only free and
	 * dirty space, @c->idx_gc_cnt are available because they are index
	 * LEBs that have been garbage collected and are awaiting the commit
	 * before they can be used. And the in-the-gaps method will grab these
	 * if it needs them. @c->lst.taken_empty_lebs are empty LEBs that have
	 * already been allocated for some purpose.
	 *
	 * Note, @c->idx_gc_cnt is included to both @c->lst.empty_lebs (because
	 * these LEBs are empty) and to @c->lst.taken_empty_lebs (because they
	 * are taken until after the commit).
	 *
	 * Note, @c->lst.taken_empty_lebs may temporarily be higher by one
	 * because of the way we serialize LEB allocations and budgeting. See a
	 * comment in 'ubifs_find_free_space()'.
	 */
	lebs = c->lst.empty_lebs + c->freeable_cnt + c->idx_gc_cnt -
	       c->lst.taken_empty_lebs;
	if (unlikely(rsvd_idx_lebs > lebs)) {
		dbg_budg("out of indexing space: min_idx_lebs %d (old %d), rsvd_idx_lebs %d",
			 min_idx_lebs, c->bi.min_idx_lebs, rsvd_idx_lebs);
		return -ENOSPC;
	}

	available = ubifs_calc_available(c, min_idx_lebs);
	outstanding = c->bi.data_growth + c->bi.dd_growth;

	if (unlikely(available < outstanding)) {
		dbg_budg("out of data space: available %lld, outstanding %lld",
			 available, outstanding);
		return -ENOSPC;
	}

	if (available - outstanding <= c->rp_size && !can_use_rp(c))
		return -ENOSPC;

	c->bi.min_idx_lebs = min_idx_lebs;
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

	data_growth = req->new_ino  ? c->bi.inode_budget : 0;
	if (req->new_page)
		data_growth += c->bi.page_budget;
	if (req->new_dent)
		data_growth += c->bi.dent_budget;
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

	dd_growth = req->dirtied_page ? c->bi.page_budget : 0;

	if (req->dirtied_ino)
		dd_growth += c->bi.inode_budget * req->dirtied_ino;
	if (req->mod_dent)
		dd_growth += c->bi.dent_budget;
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
	int err, idx_growth, data_growth, dd_growth, retried = 0;

	ubifs_assert(c, req->new_page <= 1);
	ubifs_assert(c, req->dirtied_page <= 1);
	ubifs_assert(c, req->new_dent <= 1);
	ubifs_assert(c, req->mod_dent <= 1);
	ubifs_assert(c, req->new_ino <= 1);
	ubifs_assert(c, req->new_ino_d <= UBIFS_MAX_INO_DATA);
	ubifs_assert(c, req->dirtied_ino <= 4);
	ubifs_assert(c, req->dirtied_ino_d <= UBIFS_MAX_INO_DATA * 4);
	ubifs_assert(c, !(req->new_ino_d & 7));
	ubifs_assert(c, !(req->dirtied_ino_d & 7));

	data_growth = calc_data_growth(c, req);
	dd_growth = calc_dd_growth(c, req);
	if (!data_growth && !dd_growth)
		return 0;
	idx_growth = calc_idx_growth(c, req);

again:
	spin_lock(&c->space_lock);
	ubifs_assert(c, c->bi.idx_growth >= 0);
	ubifs_assert(c, c->bi.data_growth >= 0);
	ubifs_assert(c, c->bi.dd_growth >= 0);

	if (unlikely(c->bi.nospace) && (c->bi.nospace_rp || !can_use_rp(c))) {
		dbg_budg("no space");
		spin_unlock(&c->space_lock);
		return -ENOSPC;
	}

	c->bi.idx_growth += idx_growth;
	c->bi.data_growth += data_growth;
	c->bi.dd_growth += dd_growth;

	err = do_budget_space(c);
	if (likely(!err)) {
		req->idx_growth = idx_growth;
		req->data_growth = data_growth;
		req->dd_growth = dd_growth;
		spin_unlock(&c->space_lock);
		return 0;
	}

	/* Restore the old values */
	c->bi.idx_growth -= idx_growth;
	c->bi.data_growth -= data_growth;
	c->bi.dd_growth -= dd_growth;
	spin_unlock(&c->space_lock);

	if (req->fast) {
		dbg_budg("no space for fast budgeting");
		return err;
	}

	err = make_free_space(c);
	cond_resched();
	if (err == -EAGAIN) {
		dbg_budg("try again");
		goto again;
	} else if (err == -ENOSPC) {
		if (!retried) {
			retried = 1;
			dbg_budg("-ENOSPC, but anyway try once again");
			goto again;
		}
		dbg_budg("FS is full, -ENOSPC");
		c->bi.nospace = 1;
		if (can_use_rp(c) || c->rp_size == 0)
			c->bi.nospace_rp = 1;
		smp_wmb();
	} else
		ubifs_err(c, "cannot budget space, error %d", err);
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
 * from @c->bi.idx_growth to @c->bi.uncommitted_idx. The latter will be zeroed
 * by the commit operation.
 */
void ubifs_release_budget(struct ubifs_info *c, struct ubifs_budget_req *req)
{
	ubifs_assert(c, req->new_page <= 1);
	ubifs_assert(c, req->dirtied_page <= 1);
	ubifs_assert(c, req->new_dent <= 1);
	ubifs_assert(c, req->mod_dent <= 1);
	ubifs_assert(c, req->new_ino <= 1);
	ubifs_assert(c, req->new_ino_d <= UBIFS_MAX_INO_DATA);
	ubifs_assert(c, req->dirtied_ino <= 4);
	ubifs_assert(c, req->dirtied_ino_d <= UBIFS_MAX_INO_DATA * 4);
	ubifs_assert(c, !(req->new_ino_d & 7));
	ubifs_assert(c, !(req->dirtied_ino_d & 7));
	if (!req->recalculate) {
		ubifs_assert(c, req->idx_growth >= 0);
		ubifs_assert(c, req->data_growth >= 0);
		ubifs_assert(c, req->dd_growth >= 0);
	}

	if (req->recalculate) {
		req->data_growth = calc_data_growth(c, req);
		req->dd_growth = calc_dd_growth(c, req);
		req->idx_growth = calc_idx_growth(c, req);
	}

	if (!req->data_growth && !req->dd_growth)
		return;

	c->bi.nospace = c->bi.nospace_rp = 0;
	smp_wmb();

	spin_lock(&c->space_lock);
	c->bi.idx_growth -= req->idx_growth;
	c->bi.uncommitted_idx += req->idx_growth;
	c->bi.data_growth -= req->data_growth;
	c->bi.dd_growth -= req->dd_growth;
	c->bi.min_idx_lebs = ubifs_calc_min_idx_lebs(c);

	ubifs_assert(c, c->bi.idx_growth >= 0);
	ubifs_assert(c, c->bi.data_growth >= 0);
	ubifs_assert(c, c->bi.dd_growth >= 0);
	ubifs_assert(c, c->bi.min_idx_lebs < c->main_lebs);
	ubifs_assert(c, !(c->bi.idx_growth & 7));
	ubifs_assert(c, !(c->bi.data_growth & 7));
	ubifs_assert(c, !(c->bi.dd_growth & 7));
	spin_unlock(&c->space_lock);
}

/**
 * ubifs_convert_page_budget - convert budget of a new page.
 * @c: UBIFS file-system description object
 *
 * This function converts budget which was allocated for a new page of data to
 * the budget of changing an existing page of data. The latter is smaller than
 * the former, so this function only does simple re-calculation and does not
 * involve any write-back.
 */
void ubifs_convert_page_budget(struct ubifs_info *c)
{
	spin_lock(&c->space_lock);
	/* Release the index growth reservation */
	c->bi.idx_growth -= c->max_idx_node_sz << UBIFS_BLOCKS_PER_PAGE_SHIFT;
	/* Release the data growth reservation */
	c->bi.data_growth -= c->bi.page_budget;
	/* Increase the dirty data growth reservation instead */
	c->bi.dd_growth += c->bi.page_budget;
	/* And re-calculate the indexing space reservation */
	c->bi.min_idx_lebs = ubifs_calc_min_idx_lebs(c);
	spin_unlock(&c->space_lock);
}

/**
 * ubifs_release_dirty_inode_budget - release dirty inode budget.
 * @c: UBIFS file-system description object
 * @ui: UBIFS inode to release the budget for
 *
 * This function releases budget corresponding to a dirty inode. It is usually
 * called when after the inode has been written to the media and marked as
 * clean. It also causes the "no space" flags to be cleared.
 */
void ubifs_release_dirty_inode_budget(struct ubifs_info *c,
				      struct ubifs_inode *ui)
{
	struct ubifs_budget_req req;

	memset(&req, 0, sizeof(struct ubifs_budget_req));
	/* The "no space" flags will be cleared because dd_growth is > 0 */
	req.dd_growth = c->bi.inode_budget + ALIGN(ui->data_len, 8);
	ubifs_release_budget(c, &req);
}

/**
 * ubifs_reported_space - calculate reported free space.
 * @c: the UBIFS file-system description object
 * @free: amount of free space
 *
 * This function calculates amount of free space which will be reported to
 * user-space. User-space application tend to expect that if the file-system
 * (e.g., via the 'statfs()' call) reports that it has N bytes available, they
 * are able to write a file of size N. UBIFS attaches node headers to each data
 * node and it has to write indexing nodes as well. This introduces additional
 * overhead, and UBIFS has to report slightly less free space to meet the above
 * expectations.
 *
 * This function assumes free space is made up of uncompressed data nodes and
 * full index nodes (one per data node, tripled because we always allow enough
 * space to write the index thrice).
 *
 * Note, the calculation is pessimistic, which means that most of the time
 * UBIFS reports less space than it actually has.
 */
long long ubifs_reported_space(const struct ubifs_info *c, long long free)
{
	int divisor, factor, f;

	/*
	 * Reported space size is @free * X, where X is UBIFS block size
	 * divided by UBIFS block size + all overhead one data block
	 * introduces. The overhead is the node header + indexing overhead.
	 *
	 * Indexing overhead calculations are based on the following formula:
	 * I = N/(f - 1) + 1, where I - number of indexing nodes, N - number
	 * of data nodes, f - fanout. Because effective UBIFS fanout is twice
	 * as less than maximum fanout, we assume that each data node
	 * introduces 3 * @c->max_idx_node_sz / (@c->fanout/2 - 1) bytes.
	 * Note, the multiplier 3 is because UBIFS reserves thrice as more space
	 * for the index.
	 */
	f = c->fanout > 3 ? c->fanout >> 1 : 2;
	factor = UBIFS_BLOCK_SIZE;
	divisor = UBIFS_MAX_DATA_NODE_SZ;
	divisor += (c->max_idx_node_sz * 3) / (f - 1);
	free *= factor;
	return div_u64(free, divisor);
}

/**
 * ubifs_get_free_space_nolock - return amount of free space.
 * @c: UBIFS file-system description object
 *
 * This function calculates amount of free space to report to user-space.
 *
 * Because UBIFS may introduce substantial overhead (the index, node headers,
 * alignment, wastage at the end of LEBs, etc), it cannot report real amount of
 * free flash space it has (well, because not all dirty space is reclaimable,
 * UBIFS does not actually know the real amount). If UBIFS did so, it would
 * bread user expectations about what free space is. Users seem to accustomed
 * to assume that if the file-system reports N bytes of free space, they would
 * be able to fit a file of N bytes to the FS. This almost works for
 * traditional file-systems, because they have way less overhead than UBIFS.
 * So, to keep users happy, UBIFS tries to take the overhead into account.
 */
long long ubifs_get_free_space_nolock(struct ubifs_info *c)
{
	int rsvd_idx_lebs, lebs;
	long long available, outstanding, free;

	ubifs_assert(c, c->bi.min_idx_lebs == ubifs_calc_min_idx_lebs(c));
	outstanding = c->bi.data_growth + c->bi.dd_growth;
	available = ubifs_calc_available(c, c->bi.min_idx_lebs);

	/*
	 * When reporting free space to user-space, UBIFS guarantees that it is
	 * possible to write a file of free space size. This means that for
	 * empty LEBs we may use more precise calculations than
	 * 'ubifs_calc_available()' is using. Namely, we know that in empty
	 * LEBs we would waste only @c->leb_overhead bytes, not @c->dark_wm.
	 * Thus, amend the available space.
	 *
	 * Note, the calculations below are similar to what we have in
	 * 'do_budget_space()', so refer there for comments.
	 */
	if (c->bi.min_idx_lebs > c->lst.idx_lebs)
		rsvd_idx_lebs = c->bi.min_idx_lebs - c->lst.idx_lebs;
	else
		rsvd_idx_lebs = 0;
	lebs = c->lst.empty_lebs + c->freeable_cnt + c->idx_gc_cnt -
	       c->lst.taken_empty_lebs;
	lebs -= rsvd_idx_lebs;
	available += lebs * (c->dark_wm - c->leb_overhead);

	if (available > outstanding)
		free = ubifs_reported_space(c, available - outstanding);
	else
		free = 0;
	return free;
}

/**
 * ubifs_get_free_space - return amount of free space.
 * @c: UBIFS file-system description object
 *
 * This function calculates and returns amount of free space to report to
 * user-space.
 */
long long ubifs_get_free_space(struct ubifs_info *c)
{
	long long free;

	spin_lock(&c->space_lock);
	free = ubifs_get_free_space_nolock(c);
	spin_unlock(&c->space_lock);

	return free;
}
