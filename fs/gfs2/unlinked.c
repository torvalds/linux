/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/kthread.h>
#include <asm/semaphore.h>

#include "gfs2.h"
#include "bmap.h"
#include "inode.h"
#include "meta_io.h"
#include "trans.h"
#include "unlinked.h"

static int munge_ondisk(struct gfs2_sbd *sdp, unsigned int slot,
			struct gfs2_unlinked_tag *ut)
{
	struct gfs2_inode *ip = get_v2ip(sdp->sd_ut_inode);
	unsigned int block, offset;
	uint64_t dblock;
	int new = 0;
	struct buffer_head *bh;
	int error;

	block = slot / sdp->sd_ut_per_block;
	offset = slot % sdp->sd_ut_per_block;

	error = gfs2_block_map(ip, block, &new, &dblock, NULL);
	if (error)
		return error;
	error = gfs2_meta_read(ip->i_gl, dblock, DIO_START | DIO_WAIT, &bh);
	if (error)
		return error;
	if (gfs2_metatype_check(sdp, bh, GFS2_METATYPE_UT)) {
		error = -EIO;
		goto out;
	}

	down(&sdp->sd_unlinked_mutex);
	gfs2_trans_add_bh(ip->i_gl, bh, 1);
	gfs2_unlinked_tag_out(ut, bh->b_data +
				  sizeof(struct gfs2_meta_header) +
				  offset * sizeof(struct gfs2_unlinked_tag));
	up(&sdp->sd_unlinked_mutex);

 out:
	brelse(bh);

	return error;
}

static void ul_hash(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul)
{
	spin_lock(&sdp->sd_unlinked_spin);
	list_add(&ul->ul_list, &sdp->sd_unlinked_list);
	gfs2_assert(sdp, ul->ul_count);
	ul->ul_count++;
	atomic_inc(&sdp->sd_unlinked_count);
	spin_unlock(&sdp->sd_unlinked_spin);
}

static void ul_unhash(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul)
{
	spin_lock(&sdp->sd_unlinked_spin);
	list_del_init(&ul->ul_list);
	gfs2_assert(sdp, ul->ul_count > 1);
	ul->ul_count--;
	gfs2_assert_warn(sdp, atomic_read(&sdp->sd_unlinked_count) > 0);
	atomic_dec(&sdp->sd_unlinked_count);
	spin_unlock(&sdp->sd_unlinked_spin);
}

static struct gfs2_unlinked *ul_fish(struct gfs2_sbd *sdp)
{
	struct list_head *head;
	struct gfs2_unlinked *ul;
	int found = 0;

	if (sdp->sd_vfs->s_flags & MS_RDONLY)
		return NULL;

	spin_lock(&sdp->sd_unlinked_spin);

	head = &sdp->sd_unlinked_list;

	list_for_each_entry(ul, head, ul_list) {
		if (test_bit(ULF_LOCKED, &ul->ul_flags))
			continue;

		list_move_tail(&ul->ul_list, head);
		ul->ul_count++;
		set_bit(ULF_LOCKED, &ul->ul_flags);
		found = 1;

		break;
	}

	if (!found)
		ul = NULL;

	spin_unlock(&sdp->sd_unlinked_spin);

	return ul;
}

/**
 * enforce_limit - limit the number of inodes waiting to be deallocated
 * @sdp: the filesystem
 *
 * Returns: errno
 */

static void enforce_limit(struct gfs2_sbd *sdp)
{
	unsigned int tries = 0, min = 0;
	int error;

	if (atomic_read(&sdp->sd_unlinked_count) >=
	    gfs2_tune_get(sdp, gt_ilimit)) {
		tries = gfs2_tune_get(sdp, gt_ilimit_tries);
		min = gfs2_tune_get(sdp, gt_ilimit_min);
	}

	while (tries--) {
		struct gfs2_unlinked *ul = ul_fish(sdp);
		if (!ul)
			break;
		error = gfs2_inode_dealloc(sdp, ul);
		gfs2_unlinked_put(sdp, ul);

		if (!error) {
			if (!--min)
				break;
		} else if (error != 1)
			break;
	}
}

static struct gfs2_unlinked *ul_alloc(struct gfs2_sbd *sdp)
{
	struct gfs2_unlinked *ul;

	ul = kzalloc(sizeof(struct gfs2_unlinked), GFP_KERNEL);
	if (ul) {
		INIT_LIST_HEAD(&ul->ul_list);
		ul->ul_count = 1;
		set_bit(ULF_LOCKED, &ul->ul_flags);
	}

	return ul;
}

int gfs2_unlinked_get(struct gfs2_sbd *sdp, struct gfs2_unlinked **ul)
{
	unsigned int c, o = 0, b;
	unsigned char byte = 0;

	enforce_limit(sdp);

	*ul = ul_alloc(sdp);
	if (!*ul)
		return -ENOMEM;

	spin_lock(&sdp->sd_unlinked_spin);

	for (c = 0; c < sdp->sd_unlinked_chunks; c++)
		for (o = 0; o < PAGE_SIZE; o++) {
			byte = sdp->sd_unlinked_bitmap[c][o];
			if (byte != 0xFF)
				goto found;
		}

	goto fail;

 found:
	for (b = 0; b < 8; b++)
		if (!(byte & (1 << b)))
			break;
	(*ul)->ul_slot = c * (8 * PAGE_SIZE) + o * 8 + b;

	if ((*ul)->ul_slot >= sdp->sd_unlinked_slots)
		goto fail;

	sdp->sd_unlinked_bitmap[c][o] |= 1 << b;

	spin_unlock(&sdp->sd_unlinked_spin);

	return 0;

 fail:
	spin_unlock(&sdp->sd_unlinked_spin);
	kfree(*ul);
	return -ENOSPC;
}

void gfs2_unlinked_put(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul)
{
	gfs2_assert_warn(sdp, test_and_clear_bit(ULF_LOCKED, &ul->ul_flags));

	spin_lock(&sdp->sd_unlinked_spin);
	gfs2_assert(sdp, ul->ul_count);
	ul->ul_count--;
	if (!ul->ul_count) {
		gfs2_icbit_munge(sdp, sdp->sd_unlinked_bitmap, ul->ul_slot, 0);
		spin_unlock(&sdp->sd_unlinked_spin);
		kfree(ul);
	} else
		spin_unlock(&sdp->sd_unlinked_spin);
}

int gfs2_unlinked_ondisk_add(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul)
{
	int error;

	gfs2_assert_warn(sdp, test_bit(ULF_LOCKED, &ul->ul_flags));
	gfs2_assert_warn(sdp, list_empty(&ul->ul_list));

	error = munge_ondisk(sdp, ul->ul_slot, &ul->ul_ut);
	if (!error)
		ul_hash(sdp, ul);

	return error;
}

int gfs2_unlinked_ondisk_munge(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul)
{
	int error;

	gfs2_assert_warn(sdp, test_bit(ULF_LOCKED, &ul->ul_flags));
	gfs2_assert_warn(sdp, !list_empty(&ul->ul_list));

	error = munge_ondisk(sdp, ul->ul_slot, &ul->ul_ut);

	return error;
}

int gfs2_unlinked_ondisk_rm(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul)
{
	struct gfs2_unlinked_tag ut;
	int error;

	gfs2_assert_warn(sdp, test_bit(ULF_LOCKED, &ul->ul_flags));
	gfs2_assert_warn(sdp, !list_empty(&ul->ul_list));

	memset(&ut, 0, sizeof(struct gfs2_unlinked_tag));

	error = munge_ondisk(sdp, ul->ul_slot, &ut);
	if (error)
		return error;

	ul_unhash(sdp, ul);

	return 0;
}

/**
 * gfs2_unlinked_dealloc - Go through the list of inodes to be deallocated
 * @sdp: the filesystem
 *
 * Returns: errno
 */

int gfs2_unlinked_dealloc(struct gfs2_sbd *sdp)
{
	unsigned int hits, strikes;
	int error;

	for (;;) {
		hits = 0;
		strikes = 0;

		for (;;) {
			struct gfs2_unlinked *ul = ul_fish(sdp);
			if (!ul)
				return 0;
			error = gfs2_inode_dealloc(sdp, ul);
			gfs2_unlinked_put(sdp, ul);

			if (!error) {
				hits++;
				if (strikes)
					strikes--;
			} else if (error == 1) {
				strikes++;
				if (strikes >=
				    atomic_read(&sdp->sd_unlinked_count)) {
					error = 0;
					break;
				}
			} else
				return error;
		}

		if (!hits || kthread_should_stop())
			break;

		cond_resched();
	}

	return 0;
}

int gfs2_unlinked_init(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip = get_v2ip(sdp->sd_ut_inode);
	unsigned int blocks = ip->i_di.di_size >> sdp->sd_sb.sb_bsize_shift;
	unsigned int x, slot = 0;
	unsigned int found = 0;
	uint64_t dblock;
	uint32_t extlen = 0;
	int error;

	if (!ip->i_di.di_size ||
	    ip->i_di.di_size > (64 << 20) ||
	    ip->i_di.di_size & (sdp->sd_sb.sb_bsize - 1)) {
		gfs2_consist_inode(ip);
		return -EIO;		
	}
	sdp->sd_unlinked_slots = blocks * sdp->sd_ut_per_block;
	sdp->sd_unlinked_chunks = DIV_RU(sdp->sd_unlinked_slots, 8 * PAGE_SIZE);

	error = -ENOMEM;

	sdp->sd_unlinked_bitmap = kcalloc(sdp->sd_unlinked_chunks,
					  sizeof(unsigned char *),
					  GFP_KERNEL);
	if (!sdp->sd_unlinked_bitmap)
		return error;

	for (x = 0; x < sdp->sd_unlinked_chunks; x++) {
		sdp->sd_unlinked_bitmap[x] = kzalloc(PAGE_SIZE, GFP_KERNEL);
		if (!sdp->sd_unlinked_bitmap[x])
			goto fail;
	}

	for (x = 0; x < blocks; x++) {
		struct buffer_head *bh;
		unsigned int y;

		if (!extlen) {
			int new = 0;
			error = gfs2_block_map(ip, x, &new, &dblock, &extlen);
			if (error)
				goto fail;
		}
		gfs2_meta_ra(ip->i_gl, dblock, extlen);
		error = gfs2_meta_read(ip->i_gl, dblock, DIO_START | DIO_WAIT,
				       &bh);
		if (error)
			goto fail;
		error = -EIO;
		if (gfs2_metatype_check(sdp, bh, GFS2_METATYPE_UT)) {
			brelse(bh);
			goto fail;
		}

		for (y = 0;
		     y < sdp->sd_ut_per_block && slot < sdp->sd_unlinked_slots;
		     y++, slot++) {
			struct gfs2_unlinked_tag ut;
			struct gfs2_unlinked *ul;

			gfs2_unlinked_tag_in(&ut, bh->b_data +
					  sizeof(struct gfs2_meta_header) +
					  y * sizeof(struct gfs2_unlinked_tag));
			if (!ut.ut_inum.no_addr)
				continue;

			error = -ENOMEM;
			ul = ul_alloc(sdp);
			if (!ul) {
				brelse(bh);
				goto fail;
			}
			ul->ul_ut = ut;
			ul->ul_slot = slot;

			spin_lock(&sdp->sd_unlinked_spin);
			gfs2_icbit_munge(sdp, sdp->sd_unlinked_bitmap, slot, 1);
			spin_unlock(&sdp->sd_unlinked_spin);
			ul_hash(sdp, ul);

			gfs2_unlinked_put(sdp, ul);
			found++;
		}

		brelse(bh);
		dblock++;
		extlen--;
	}

	if (found)
		fs_info(sdp, "found %u unlinked inodes\n", found);

	return 0;

 fail:
	gfs2_unlinked_cleanup(sdp);
	return error;
}

/**
 * gfs2_unlinked_cleanup - get rid of any extra struct gfs2_unlinked structures
 * @sdp: the filesystem
 *
 */

void gfs2_unlinked_cleanup(struct gfs2_sbd *sdp)
{
	struct list_head *head = &sdp->sd_unlinked_list;
	struct gfs2_unlinked *ul;
	unsigned int x;

	spin_lock(&sdp->sd_unlinked_spin);
	while (!list_empty(head)) {
		ul = list_entry(head->next, struct gfs2_unlinked, ul_list);

		if (ul->ul_count > 1) {
			list_move_tail(&ul->ul_list, head);
			spin_unlock(&sdp->sd_unlinked_spin);
			schedule();
			spin_lock(&sdp->sd_unlinked_spin);
			continue;
		}

		list_del_init(&ul->ul_list);
		atomic_dec(&sdp->sd_unlinked_count);

		gfs2_assert_warn(sdp, ul->ul_count == 1);
		gfs2_assert_warn(sdp, !test_bit(ULF_LOCKED, &ul->ul_flags));
		kfree(ul);
	}
	spin_unlock(&sdp->sd_unlinked_spin);

	gfs2_assert_warn(sdp, !atomic_read(&sdp->sd_unlinked_count));

	if (sdp->sd_unlinked_bitmap) {
		for (x = 0; x < sdp->sd_unlinked_chunks; x++)
			kfree(sdp->sd_unlinked_bitmap[x]);
		kfree(sdp->sd_unlinked_bitmap);
	}
}

