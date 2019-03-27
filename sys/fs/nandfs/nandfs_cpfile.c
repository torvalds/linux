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

#include "nandfs_mount.h"
#include "nandfs.h"
#include "nandfs_subr.h"


static int
nandfs_checkpoint_size(struct nandfs_device *fsdev)
{

	return (fsdev->nd_fsdata.f_checkpoint_size);
}

static int
nandfs_checkpoint_blk_offset(struct nandfs_device *fsdev, uint64_t cn,
    uint64_t *blk, uint64_t *offset)
{
	uint64_t off;
	uint16_t cp_size, cp_per_blk;

	KASSERT((cn), ("checkpoing cannot be zero"));

	cp_size = fsdev->nd_fsdata.f_checkpoint_size;
	cp_per_blk = fsdev->nd_blocksize / cp_size;
	off = roundup(sizeof(struct nandfs_cpfile_header), cp_size) / cp_size;
	off += (cn - 1);

	*blk = off / cp_per_blk;
	*offset = (off % cp_per_blk) * cp_size;

	return (0);
}

static int
nandfs_checkpoint_blk_remaining(struct nandfs_device *fsdev, uint64_t cn,
    uint64_t blk, uint64_t offset)
{
	uint16_t cp_size, cp_remaining;

	cp_size = fsdev->nd_fsdata.f_checkpoint_size;
	cp_remaining = (fsdev->nd_blocksize - offset) / cp_size;

	return (cp_remaining);
}

int
nandfs_get_checkpoint(struct nandfs_device *fsdev, struct nandfs_node *cp_node,
    uint64_t cn)
{
	struct buf *bp;
	uint64_t blk, offset;
	int error;

	if (cn != fsdev->nd_last_cno && cn != (fsdev->nd_last_cno + 1)) {
		return (-1);
	}

	error = nandfs_bread(cp_node, 0, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return (-1);
	}

	error = nandfs_dirty_buf(bp, 0);
	if (error)
		return (-1);


	nandfs_checkpoint_blk_offset(fsdev, cn, &blk, &offset);

	if (blk != 0) {
		if (blk < cp_node->nn_inode.i_blocks)
			error = nandfs_bread(cp_node, blk, NOCRED, 0, &bp);
		else
			error = nandfs_bcreate(cp_node, blk, NOCRED, 0, &bp);
		if (error) {
			if (bp)
				brelse(bp);
			return (-1);
		}

		nandfs_dirty_buf(bp, 1);
	}

	DPRINTF(CPFILE, ("%s: cn:%#jx entry block:%#jx offset:%#jx\n",
	    __func__, (uintmax_t)cn, (uintmax_t)blk, (uintmax_t)offset));

	return (0);
}

int
nandfs_set_checkpoint(struct nandfs_device *fsdev, struct nandfs_node *cp_node,
    uint64_t cn, struct nandfs_inode *ifile_inode, uint64_t nblocks)
{
	struct nandfs_cpfile_header *cnh;
	struct nandfs_checkpoint *cnp;
	struct buf *bp;
	uint64_t blk, offset;
	int error;

	if (cn != fsdev->nd_last_cno && cn != (fsdev->nd_last_cno + 1)) {
		nandfs_error("%s: trying to set invalid chekpoint %jx - %jx\n",
		    __func__, cn, fsdev->nd_last_cno);
		return (-1);
	}

	error = nandfs_bread(cp_node, 0, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return error;
	}

	cnh = (struct nandfs_cpfile_header *) bp->b_data;
	cnh->ch_ncheckpoints++;

	nandfs_checkpoint_blk_offset(fsdev, cn, &blk, &offset);

	if(blk != 0) {
		brelse(bp);
		error = nandfs_bread(cp_node, blk, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			return error;
		}
	}

	cnp = (struct nandfs_checkpoint *)((uint8_t *)bp->b_data + offset);
	cnp->cp_flags = 0;
	cnp->cp_checkpoints_count = 1;
	memset(&cnp->cp_snapshot_list, 0, sizeof(struct nandfs_snapshot_list));
	cnp->cp_cno = cn;
	cnp->cp_create = fsdev->nd_ts.tv_sec;
	cnp->cp_nblk_inc = nblocks;
	cnp->cp_blocks_count = 0;
	memcpy (&cnp->cp_ifile_inode, ifile_inode, sizeof(cnp->cp_ifile_inode));

	DPRINTF(CPFILE, ("%s: cn:%#jx ctime:%#jx nblk:%#jx\n",
	    __func__, (uintmax_t)cn, (uintmax_t)cnp->cp_create,
	    (uintmax_t)nblocks));

	brelse(bp);
	return (0);
}

static int
nandfs_cp_mounted(struct nandfs_device *nandfsdev, uint64_t cno)
{
	struct nandfsmount *nmp;
	int mounted = 0;

	mtx_lock(&nandfsdev->nd_mutex);
	/* No double-mounting of the same checkpoint */
	STAILQ_FOREACH(nmp, &nandfsdev->nd_mounts, nm_next_mount) {
		if (nmp->nm_mount_args.cpno == cno) {
			mounted = 1;
			break;
		}
	}
	mtx_unlock(&nandfsdev->nd_mutex);

	return (mounted);
}

static int
nandfs_cp_set_snapshot(struct nandfs_node *cp_node, uint64_t cno)
{
	struct nandfs_device *fsdev;
	struct nandfs_cpfile_header *cnh;
	struct nandfs_checkpoint *cnp;
	struct nandfs_snapshot_list *list;
	struct buf *bp;
	uint64_t blk, prev_blk, offset;
	uint64_t curr, prev;
	int error;

	fsdev = cp_node->nn_nandfsdev;

	/* Get snapshot data */
	nandfs_checkpoint_blk_offset(fsdev, cno, &blk, &offset);
	error = nandfs_bread(cp_node, blk, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	cnp = (struct nandfs_checkpoint *)(bp->b_data + offset);
	if (cnp->cp_flags & NANDFS_CHECKPOINT_INVALID) {
		brelse(bp);
		return (ENOENT);
	}
	if ((cnp->cp_flags & NANDFS_CHECKPOINT_SNAPSHOT)) {
		brelse(bp);
		return (EINVAL);
	}

	brelse(bp);
	/* Get list from header */
	error = nandfs_bread(cp_node, 0, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	cnh = (struct nandfs_cpfile_header *) bp->b_data;
	list = &cnh->ch_snapshot_list;
	prev = list->ssl_prev;
	brelse(bp);
	prev_blk = ~(0);
	curr = 0;
	while (prev > cno) {
		curr = prev;
		nandfs_checkpoint_blk_offset(fsdev, prev, &prev_blk, &offset);
		error = nandfs_bread(cp_node, prev_blk, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}
		cnp = (struct nandfs_checkpoint *)(bp->b_data + offset);
		list = &cnp->cp_snapshot_list;
		prev = list->ssl_prev;
		brelse(bp);
	}

	if (curr == 0) {
		nandfs_bread(cp_node, 0, NOCRED, 0, &bp);
		cnh = (struct nandfs_cpfile_header *) bp->b_data;
		list = &cnh->ch_snapshot_list;
	} else {
		nandfs_checkpoint_blk_offset(fsdev, curr, &blk, &offset);
		error = nandfs_bread(cp_node, blk, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}
		cnp = (struct nandfs_checkpoint *)(bp->b_data + offset);
		list = &cnp->cp_snapshot_list;
	}

	list->ssl_prev = cno;
	error = nandfs_dirty_buf(bp, 0);
	if (error)
		return (error);


	/* Update snapshot for cno */
	nandfs_checkpoint_blk_offset(fsdev, cno, &blk, &offset);
	error = nandfs_bread(cp_node, blk, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	cnp = (struct nandfs_checkpoint *)(bp->b_data + offset);
	list = &cnp->cp_snapshot_list;
	list->ssl_prev = prev;
	list->ssl_next = curr;
	cnp->cp_flags |= NANDFS_CHECKPOINT_SNAPSHOT;
	nandfs_dirty_buf(bp, 1);

	if (prev == 0) {
		nandfs_bread(cp_node, 0, NOCRED, 0, &bp);
		cnh = (struct nandfs_cpfile_header *) bp->b_data;
		list = &cnh->ch_snapshot_list;
	} else {
		/* Update snapshot list for prev */
		nandfs_checkpoint_blk_offset(fsdev, prev, &blk, &offset);
		error = nandfs_bread(cp_node, blk, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}
		cnp = (struct nandfs_checkpoint *)(bp->b_data + offset);
		list = &cnp->cp_snapshot_list;
	}
	list->ssl_next = cno;
	nandfs_dirty_buf(bp, 1);

	/* Update header */
	error = nandfs_bread(cp_node, 0, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	cnh = (struct nandfs_cpfile_header *) bp->b_data;
	cnh->ch_nsnapshots++;
	nandfs_dirty_buf(bp, 1);

	return (0);
}

static int
nandfs_cp_clr_snapshot(struct nandfs_node *cp_node, uint64_t cno)
{
	struct nandfs_device *fsdev;
	struct nandfs_cpfile_header *cnh;
	struct nandfs_checkpoint *cnp;
	struct nandfs_snapshot_list *list;
	struct buf *bp;
	uint64_t blk, offset, snapshot_cnt;
	uint64_t next, prev;
	int error;

	fsdev = cp_node->nn_nandfsdev;

	/* Get snapshot data */
	nandfs_checkpoint_blk_offset(fsdev, cno, &blk, &offset);
	error = nandfs_bread(cp_node, blk, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	cnp = (struct nandfs_checkpoint *)(bp->b_data + offset);
	if (cnp->cp_flags & NANDFS_CHECKPOINT_INVALID) {
		brelse(bp);
		return (ENOENT);
	}
	if (!(cnp->cp_flags & NANDFS_CHECKPOINT_SNAPSHOT)) {
		brelse(bp);
		return (EINVAL);
	}

	list = &cnp->cp_snapshot_list;
	next = list->ssl_next;
	prev = list->ssl_prev;
	brelse(bp);

	/* Get previous snapshot */
	if (prev != 0) {
		nandfs_checkpoint_blk_offset(fsdev, prev, &blk, &offset);
		error = nandfs_bread(cp_node, blk, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}
		cnp = (struct nandfs_checkpoint *)(bp->b_data + offset);
		list = &cnp->cp_snapshot_list;
	} else {
		nandfs_bread(cp_node, 0, NOCRED, 0, &bp);
		cnh = (struct nandfs_cpfile_header *) bp->b_data;
		list = &cnh->ch_snapshot_list;
	}

	list->ssl_next = next;
	error = nandfs_dirty_buf(bp, 0);
	if (error)
		return (error);

	/* Get next snapshot */
	if (next != 0) {
		nandfs_checkpoint_blk_offset(fsdev, next, &blk, &offset);
		error = nandfs_bread(cp_node, blk, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}
		cnp = (struct nandfs_checkpoint *)(bp->b_data + offset);
		list = &cnp->cp_snapshot_list;
	} else {
		nandfs_bread(cp_node, 0, NOCRED, 0, &bp);
		cnh = (struct nandfs_cpfile_header *) bp->b_data;
		list = &cnh->ch_snapshot_list;
	}
	list->ssl_prev = prev;
	nandfs_dirty_buf(bp, 1);

	/* Update snapshot list for cno */
	nandfs_checkpoint_blk_offset(fsdev, cno, &blk, &offset);
	error = nandfs_bread(cp_node, blk, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	cnp = (struct nandfs_checkpoint *)(bp->b_data + offset);
	list = &cnp->cp_snapshot_list;
	list->ssl_prev = 0;
	list->ssl_next = 0;
	cnp->cp_flags &= !NANDFS_CHECKPOINT_SNAPSHOT;
	nandfs_dirty_buf(bp, 1);

	/* Update header */
	error = nandfs_bread(cp_node, 0, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}
	cnh = (struct nandfs_cpfile_header *) bp->b_data;
	snapshot_cnt = cnh->ch_nsnapshots;
	snapshot_cnt--;
	cnh->ch_nsnapshots = snapshot_cnt;
	nandfs_dirty_buf(bp, 1);

	return (0);
}

int
nandfs_chng_cpmode(struct nandfs_node *node, struct nandfs_cpmode *ncpm)
{
	struct nandfs_device *fsdev;
	uint64_t cno = ncpm->ncpm_cno;
	int mode = ncpm->ncpm_mode;
	int ret;

	fsdev = node->nn_nandfsdev;
	VOP_LOCK(NTOV(node), LK_EXCLUSIVE);
	switch (mode) {
	case NANDFS_CHECKPOINT:
		if (nandfs_cp_mounted(fsdev, cno)) {
			ret = EBUSY;
		} else
			ret = nandfs_cp_clr_snapshot(node, cno);
		break;
	case NANDFS_SNAPSHOT:
		ret = nandfs_cp_set_snapshot(node, cno);
		break;
	default:
		ret = EINVAL;
		break;
	}
	VOP_UNLOCK(NTOV(node), 0);

	return (ret);
}

static void
nandfs_cpinfo_fill(struct nandfs_checkpoint *cnp, struct nandfs_cpinfo *nci)
{

	nci->nci_flags = cnp->cp_flags;
	nci->nci_pad = 0;
	nci->nci_cno = cnp->cp_cno;
	nci->nci_create = cnp->cp_create;
	nci->nci_nblk_inc = cnp->cp_nblk_inc;
	nci->nci_blocks_count = cnp->cp_blocks_count;
	nci->nci_next = cnp->cp_snapshot_list.ssl_next;
	DPRINTF(CPFILE, ("%s: cn:%#jx ctime:%#jx\n",
	    __func__, (uintmax_t)cnp->cp_cno,
	    (uintmax_t)cnp->cp_create));
}

static int
nandfs_get_cpinfo_cp(struct nandfs_node *node, uint64_t cno,
    struct nandfs_cpinfo *nci, uint32_t mnmembs, uint32_t *nmembs)
{
	struct nandfs_device *fsdev;
	struct buf *bp;
	uint64_t blk, offset, last_cno, i;
	uint16_t remaining;
	int error;
#ifdef INVARIANTS
	uint64_t testblk, testoffset;
#endif

	if (cno == 0) {
		return (ENOENT);
	}

	if (mnmembs < 1) {
		return (EINVAL);
	}

	fsdev = node->nn_nandfsdev;
	last_cno = fsdev->nd_last_cno;
	DPRINTF(CPFILE, ("%s: cno:%#jx mnmembs: %#jx last:%#jx\n", __func__,
	    (uintmax_t)cno, (uintmax_t)mnmembs,
	    (uintmax_t)fsdev->nd_last_cno));

	/*
	 * do {
	 * 	get block
	 * 	read checkpoints until we hit last checkpoint, end of block or
	 * 	requested number
	 * } while (last read checkpoint <= last checkpoint on fs &&
	 * 		read checkpoints < request number);
	 */
	*nmembs = i = 0;
	do {
		nandfs_checkpoint_blk_offset(fsdev, cno, &blk, &offset);
		remaining = nandfs_checkpoint_blk_remaining(fsdev, cno,
		    blk, offset);
		error = nandfs_bread(node, blk, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}

		while (cno <= last_cno && i < mnmembs && remaining) {
#ifdef INVARIANTS
			nandfs_checkpoint_blk_offset(fsdev, cno, &testblk,
			    &testoffset);
			KASSERT(testblk == blk, ("testblk != blk"));
			KASSERT(testoffset == offset, ("testoffset != offset"));
#endif
			DPRINTF(CPFILE, ("%s: cno %#jx\n", __func__,
			    (uintmax_t)cno));

			nandfs_cpinfo_fill((struct nandfs_checkpoint *)
			    (bp->b_data + offset), nci);
			offset += nandfs_checkpoint_size(fsdev);
			i++;
			nci++;
			cno++;
			(*nmembs)++;
			remaining--;
		}
		brelse(bp);
	} while (cno <= last_cno && i < mnmembs);

	return (0);
}

static int
nandfs_get_cpinfo_sp(struct nandfs_node *node, uint64_t cno,
    struct nandfs_cpinfo *nci, uint32_t mnmembs, uint32_t *nmembs)
{
	struct nandfs_checkpoint *cnp;
	struct nandfs_cpfile_header *cnh;
	struct nandfs_device *fsdev;
	struct buf *bp = NULL;
	uint64_t curr = 0;
	uint64_t blk, offset, curr_cno;
	uint32_t flag;
	int i, error;

	if (cno == 0 || cno == ~(0))
		return (ENOENT);

	fsdev = node->nn_nandfsdev;
	curr_cno = cno;

	if (nmembs)
		*nmembs = 0;
	if (curr_cno == 1) {
		/* Get list from header */
		error = nandfs_bread(node, 0, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			return (error);
		}
		cnh = (struct nandfs_cpfile_header *) bp->b_data;
		curr_cno = cnh->ch_snapshot_list.ssl_next;
		brelse(bp);
		bp = NULL;

		/* No snapshots */
		if (curr_cno == 0)
			return (0);
	}

	for (i = 0; i < mnmembs; i++, nci++) {
		nandfs_checkpoint_blk_offset(fsdev, curr_cno, &blk, &offset);
		if (i == 0 || curr != blk) {
			if (bp)
				brelse(bp);
			error = nandfs_bread(node, blk, NOCRED, 0, &bp);
			if (error) {
				brelse(bp);
				return (ENOENT);
			}
			curr = blk;
		}
		cnp = (struct nandfs_checkpoint *)(bp->b_data + offset);
		flag = cnp->cp_flags;
		if (!(flag & NANDFS_CHECKPOINT_SNAPSHOT) ||
		    (flag & NANDFS_CHECKPOINT_INVALID))
			break;

		nci->nci_flags = flag;
		nci->nci_pad = 0;
		nci->nci_cno = cnp->cp_cno;
		nci->nci_create = cnp->cp_create;
		nci->nci_nblk_inc = cnp->cp_nblk_inc;
		nci->nci_blocks_count = cnp->cp_blocks_count;
		nci->nci_next = cnp->cp_snapshot_list.ssl_next;
		if (nmembs)
			(*nmembs)++;

		curr_cno = nci->nci_next;
		if (!curr_cno)
			break;
	}

	brelse(bp);

	return (0);
}

int
nandfs_get_cpinfo(struct nandfs_node *node, uint64_t cno, uint16_t flags,
    struct nandfs_cpinfo *nci, uint32_t nmembs, uint32_t *nnmembs)
{
	int error;

	VOP_LOCK(NTOV(node), LK_EXCLUSIVE);
	switch (flags) {
	case NANDFS_CHECKPOINT:
		error = nandfs_get_cpinfo_cp(node, cno, nci, nmembs, nnmembs);
		break;
	case NANDFS_SNAPSHOT:
		error = nandfs_get_cpinfo_sp(node, cno, nci, nmembs, nnmembs);
		break;
	default:
		error = EINVAL;
		break;
	}
	VOP_UNLOCK(NTOV(node), 0);

	return (error);
}

int
nandfs_get_cpinfo_ioctl(struct nandfs_node *node, struct nandfs_argv *nargv)
{
	struct nandfs_cpinfo *nci;
	uint64_t cno = nargv->nv_index;
	void *buf = (void *)((uintptr_t)nargv->nv_base);
	uint16_t flags = nargv->nv_flags;
	uint32_t nmembs = 0;
	int error;

	if (nargv->nv_nmembs > NANDFS_CPINFO_MAX)
		return (EINVAL);

	nci = malloc(sizeof(struct nandfs_cpinfo) * nargv->nv_nmembs,
	    M_NANDFSTEMP, M_WAITOK | M_ZERO);

	error = nandfs_get_cpinfo(node, cno, flags, nci, nargv->nv_nmembs, &nmembs);

	if (error == 0) {
		nargv->nv_nmembs = nmembs;
		error = copyout(nci, buf,
		    sizeof(struct nandfs_cpinfo) * nmembs);
	}

	free(nci, M_NANDFSTEMP);
	return (error);
}

int
nandfs_delete_cp(struct nandfs_node *node, uint64_t start, uint64_t end)
{
	struct nandfs_checkpoint *cnp;
	struct nandfs_device *fsdev;
	struct buf *bp;
	uint64_t cno = start, blk, offset;
	int error;

	DPRINTF(CPFILE, ("%s: delete cno %jx-%jx\n", __func__, start, end));
	VOP_LOCK(NTOV(node), LK_EXCLUSIVE);
	fsdev = node->nn_nandfsdev;
	for (cno = start; cno <= end; cno++) {
		if (!cno)
			continue;

		nandfs_checkpoint_blk_offset(fsdev, cno, &blk, &offset);
		error = nandfs_bread(node, blk, NOCRED, 0, &bp);
		if (error) {
			VOP_UNLOCK(NTOV(node), 0);
			brelse(bp);
			return (error);
		}

		cnp = (struct nandfs_checkpoint *)(bp->b_data + offset);
		if (cnp->cp_flags & NANDFS_CHECKPOINT_SNAPSHOT) {
			brelse(bp);
			VOP_UNLOCK(NTOV(node), 0);
			return (0);
		}

		cnp->cp_flags |= NANDFS_CHECKPOINT_INVALID;

		error = nandfs_dirty_buf(bp, 0);
		if (error)
			return (error);
	}
	VOP_UNLOCK(NTOV(node), 0);

	return (0);
}

int
nandfs_make_snap(struct nandfs_device *fsdev, uint64_t *cno)
{
	struct nandfs_cpmode cpm;
	int error;

	*cno = cpm.ncpm_cno = fsdev->nd_last_cno;
	cpm.ncpm_mode = NANDFS_SNAPSHOT;
	error = nandfs_chng_cpmode(fsdev->nd_cp_node, &cpm);
	return (error);
}

int
nandfs_delete_snap(struct nandfs_device *fsdev, uint64_t cno)
{
	struct nandfs_cpmode cpm;
	int error;

	cpm.ncpm_cno = cno;
	cpm.ncpm_mode = NANDFS_CHECKPOINT;
	error = nandfs_chng_cpmode(fsdev->nd_cp_node, &cpm);
	return (error);
}

int nandfs_get_cpstat(struct nandfs_node *cp_node, struct nandfs_cpstat *ncp)
{
	struct nandfs_device *fsdev;
	struct nandfs_cpfile_header *cnh;
	struct buf *bp;
	int error;

	VOP_LOCK(NTOV(cp_node), LK_EXCLUSIVE);
	fsdev = cp_node->nn_nandfsdev;

	/* Get header */
	error = nandfs_bread(cp_node, 0, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		VOP_UNLOCK(NTOV(cp_node), 0);
		return (error);
	}
	cnh = (struct nandfs_cpfile_header *) bp->b_data;
	ncp->ncp_cno = fsdev->nd_last_cno;
	ncp->ncp_ncps = cnh->ch_ncheckpoints;
	ncp->ncp_nss = cnh->ch_nsnapshots;
	DPRINTF(CPFILE, ("%s: cno:%#jx ncps:%#jx nss:%#jx\n",
	    __func__, ncp->ncp_cno, ncp->ncp_ncps, ncp->ncp_nss));
	brelse(bp);
	VOP_UNLOCK(NTOV(cp_node), 0);

	return (0);
}
