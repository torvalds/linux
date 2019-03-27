/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/bio.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>

#include <fs/nandfs/nandfs_mount.h>
#include <fs/nandfs/nandfs.h>
#include <fs/nandfs/nandfs_subr.h>

static void
nandfs_get_desc_block_nr(struct nandfs_mdt *mdt, uint64_t desc,
    uint64_t *desc_block)
{

	*desc_block = desc * mdt->blocks_per_desc_block;
}

static void
nandfs_get_group_block_nr(struct nandfs_mdt *mdt, uint64_t group,
    uint64_t *group_block)
{
	uint64_t desc, group_off;

	desc = group / mdt->groups_per_desc_block;
	group_off = group % mdt->groups_per_desc_block;
	*group_block = desc * mdt->blocks_per_desc_block +
	    1 + group_off * mdt->blocks_per_group;
}

static void
init_desc_block(struct nandfs_mdt *mdt, uint8_t *block_data)
{
	struct nandfs_block_group_desc *desc;
	uint32_t i;

	desc = (struct nandfs_block_group_desc *) block_data;
	for (i = 0; i < mdt->groups_per_desc_block; i++)
		desc[i].bg_nfrees = mdt->entries_per_group;
}

int
nandfs_find_free_entry(struct nandfs_mdt *mdt, struct nandfs_node *node,
    struct nandfs_alloc_request *req)
{
	nandfs_daddr_t desc, group, maxgroup, maxdesc, pos = 0;
	nandfs_daddr_t start_group, start_desc;
	nandfs_daddr_t desc_block, group_block;
	nandfs_daddr_t file_blocks;
	struct nandfs_block_group_desc *descriptors;
	struct buf *bp, *bp2;
	uint32_t *mask, i, mcount, msize;
	int error;

	file_blocks = node->nn_inode.i_blocks;
	maxgroup = 0x100000000ull / mdt->entries_per_group;
	maxdesc = maxgroup / mdt->groups_per_desc_block;
	start_group = req->entrynum / mdt->entries_per_group;
	start_desc = start_group / mdt->groups_per_desc_block;

	bp = bp2 = NULL;
restart:
	for (desc = start_desc; desc < maxdesc; desc++) {
		nandfs_get_desc_block_nr(mdt, desc, &desc_block);

		if (bp)
			brelse(bp);
		if (desc_block < file_blocks) {
			error = nandfs_bread(node, desc_block, NOCRED, 0, &bp);
			if (error) {
				brelse(bp);
				return (error);
			}
		} else {
			error = nandfs_bcreate(node, desc_block, NOCRED, 0,
			    &bp);
			if (error)
				return (error);
			file_blocks++;
			init_desc_block(mdt, bp->b_data);
		}

		descriptors = (struct nandfs_block_group_desc *) bp->b_data;
		for (group = start_group; group < mdt->groups_per_desc_block;
		    group++) {
			if (descriptors[group].bg_nfrees > 0) {
				nandfs_get_group_block_nr(mdt, group,
				    &group_block);

				if (bp2)
					brelse(bp2);
				if (group_block < file_blocks) {
					error = nandfs_bread(node, group_block,
					    NOCRED, 0, &bp2);
					if (error) {
						brelse(bp);
						return (error);
					}
				} else {
					error = nandfs_bcreate(node,
					    group_block, NOCRED, 0, &bp2);
					if (error)
						return (error);
					file_blocks++;
				}
				mask = (uint32_t *)bp2->b_data;
				msize = (sizeof(uint32_t) * __CHAR_BIT);
				mcount = mdt->entries_per_group / msize;
				for (i = 0; i < mcount; i++) {
					if (mask[i] == UINT32_MAX)
						continue;

					pos = ffs(~mask[i]) - 1;
					pos += (msize * i);
					pos += (group * mdt->entries_per_group);
					pos += desc * group *
					    mdt->groups_per_desc_block *
					    mdt->entries_per_group;
					goto found;
				}
			}
		}
		start_group = 0;
	}

	if (start_desc != 0) {
		maxdesc = start_desc;
		start_desc = 0;
		req->entrynum = 0;
		goto restart;
	}

	return (ENOENT);

found:
	req->entrynum = pos;
	req->bp_desc = bp;
	req->bp_bitmap = bp2;
	DPRINTF(ALLOC, ("%s: desc: %p bitmap: %p entry: %#jx\n",
	    __func__, req->bp_desc, req->bp_bitmap, (uintmax_t)pos));

	return (0);
}

int
nandfs_find_entry(struct nandfs_mdt* mdt, struct nandfs_node *nnode,
    struct nandfs_alloc_request *req)
{
	uint64_t dblock, bblock, eblock;
	uint32_t offset;
	int error;

	nandfs_mdt_trans_blk(mdt, req->entrynum, &dblock, &bblock, &eblock,
	    &offset);

	error = nandfs_bread(nnode, dblock, NOCRED, 0, &req->bp_desc);
	if (error) {
		brelse(req->bp_desc);
		return (error);
	}

	error = nandfs_bread(nnode, bblock, NOCRED, 0, &req->bp_bitmap);
	if (error) {
		brelse(req->bp_desc);
		brelse(req->bp_bitmap);
		return (error);
	}

	error = nandfs_bread(nnode, eblock, NOCRED, 0, &req->bp_entry);
	if (error) {
		brelse(req->bp_desc);
		brelse(req->bp_bitmap);
		brelse(req->bp_entry);
		return (error);
	}

	DPRINTF(ALLOC,
	    ("%s: desc_buf: %p bitmap_buf %p entry_buf %p offset %x\n",
	    __func__, req->bp_desc, req->bp_bitmap, req->bp_entry, offset));

	return (0);
}

static __inline void
nandfs_calc_idx_entry(struct nandfs_mdt* mdt, uint32_t entrynum,
    uint64_t *group, uint64_t *bitmap_idx, uint64_t *bitmap_off)
{

	/* Find group_desc index */
	entrynum = entrynum %
	    (mdt->entries_per_group * mdt->groups_per_desc_block);
	*group = entrynum / mdt->entries_per_group;
	/* Find bitmap index and bit offset */
	entrynum = entrynum % mdt->entries_per_group;
	*bitmap_idx = entrynum / (sizeof(uint32_t) * __CHAR_BIT);
	*bitmap_off = entrynum % (sizeof(uint32_t) * __CHAR_BIT);
}

int
nandfs_free_entry(struct nandfs_mdt* mdt, struct nandfs_alloc_request *req)
{
	struct nandfs_block_group_desc *descriptors;
	uint64_t bitmap_idx, bitmap_off;
	uint64_t group;
	uint32_t *mask, maskrw;

	nandfs_calc_idx_entry(mdt, req->entrynum, &group, &bitmap_idx,
	    &bitmap_off);

	DPRINTF(ALLOC, ("nandfs_free_entry: req->entrynum=%jx bitmap_idx=%jx"
	   " bitmap_off=%jx group=%jx\n", (uintmax_t)req->entrynum,
	   (uintmax_t)bitmap_idx, (uintmax_t)bitmap_off, (uintmax_t)group));

	/* Update counter of free entries for group */
	descriptors = (struct nandfs_block_group_desc *) req->bp_desc->b_data;
	descriptors[group].bg_nfrees++;

	/* Set bit to indicate that entry is taken */
	mask = (uint32_t *)req->bp_bitmap->b_data;
	maskrw = mask[bitmap_idx];
	KASSERT(maskrw & (1 << bitmap_off), ("freeing unallocated vblock"));
	maskrw &= ~(1 << bitmap_off);
	mask[bitmap_idx] = maskrw;

	/* Make descriptor, bitmap and entry buffer dirty */
	if (nandfs_dirty_buf(req->bp_desc, 0) == 0) {
		nandfs_dirty_buf(req->bp_bitmap, 1);
		nandfs_dirty_buf(req->bp_entry, 1);
	} else {
		brelse(req->bp_bitmap);
		brelse(req->bp_entry);
		return (-1);
	}

	return (0);
}

int
nandfs_alloc_entry(struct nandfs_mdt* mdt, struct nandfs_alloc_request *req)
{
	struct nandfs_block_group_desc *descriptors;
	uint64_t bitmap_idx, bitmap_off;
	uint64_t group;
	uint32_t *mask, maskrw;

	nandfs_calc_idx_entry(mdt, req->entrynum, &group, &bitmap_idx,
	    &bitmap_off);

	DPRINTF(ALLOC, ("nandfs_alloc_entry: req->entrynum=%jx bitmap_idx=%jx"
	    " bitmap_off=%jx group=%jx\n", (uintmax_t)req->entrynum,
	    (uintmax_t)bitmap_idx, (uintmax_t)bitmap_off, (uintmax_t)group));

	/* Update counter of free entries for group */
	descriptors = (struct nandfs_block_group_desc *) req->bp_desc->b_data;
	descriptors[group].bg_nfrees--;

	/* Clear bit to indicate that entry is free */
	mask = (uint32_t *)req->bp_bitmap->b_data;
	maskrw = mask[bitmap_idx];
	maskrw |= 1 << bitmap_off;
	mask[bitmap_idx] = maskrw;

	/* Make descriptor, bitmap and entry buffer dirty */
	if (nandfs_dirty_buf(req->bp_desc, 0) == 0) {
		nandfs_dirty_buf(req->bp_bitmap, 1);
		nandfs_dirty_buf(req->bp_entry, 1);
	} else {
		brelse(req->bp_bitmap);
		brelse(req->bp_entry);
		return (-1);
	}

	return (0);
}

void
nandfs_abort_entry(struct nandfs_alloc_request *req)
{

	brelse(req->bp_desc);
	brelse(req->bp_bitmap);
	brelse(req->bp_entry);
}

int
nandfs_get_entry_block(struct nandfs_mdt *mdt, struct nandfs_node *node,
    struct nandfs_alloc_request *req, uint32_t *entry, int create)
{
	struct buf *bp;
	nandfs_lbn_t blocknr;
	int	error;

	/* Find buffer number for given entry */
	nandfs_mdt_trans(mdt, req->entrynum, &blocknr, entry);
	DPRINTF(ALLOC, ("%s: ino %#jx entrynum:%#jx block:%#jx entry:%x\n",
	    __func__, (uintmax_t)node->nn_ino, (uintmax_t)req->entrynum,
	    (uintmax_t)blocknr, *entry));

	/* Read entry block or create if 'create' parameter is not zero */
	bp = NULL;

	if (blocknr < node->nn_inode.i_blocks)
		error = nandfs_bread(node, blocknr, NOCRED, 0, &bp);
	else if (create)
		error = nandfs_bcreate(node, blocknr, NOCRED, 0, &bp);
	else
		error = E2BIG;

	if (error) {
		DPRINTF(ALLOC, ("%s: ino %#jx block %#jx entry %x error %d\n",
		    __func__, (uintmax_t)node->nn_ino, (uintmax_t)blocknr,
		    *entry, error));
		if (bp)
			brelse(bp);
		return (error);
	}

	MPASS(nandfs_vblk_get(bp) != 0 || node->nn_ino == NANDFS_DAT_INO);

	req->bp_entry = bp;
	return (0);
}
