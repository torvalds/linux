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

#include <geom/geom.h>
#include <geom/geom_vfs.h>

#include <fs/nandfs/nandfs_mount.h>
#include <fs/nandfs/nandfs.h>
#include <fs/nandfs/nandfs_subr.h>

#define	SU_USAGE_OFF(bp, offset) \
	((struct nandfs_segment_usage *)((bp)->b_data + offset))

static int
nandfs_seg_usage_blk_offset(struct nandfs_device *fsdev, uint64_t seg,
    uint64_t *blk, uint64_t *offset)
{
	uint64_t off;
	uint16_t seg_size;

	seg_size = fsdev->nd_fsdata.f_segment_usage_size;

	off = roundup(sizeof(struct nandfs_sufile_header), seg_size);
	off += (seg * seg_size);

	*blk = off / fsdev->nd_blocksize;
	*offset = off % fsdev->nd_blocksize;
	return (0);
}

/* Alloc new segment */
int
nandfs_alloc_segment(struct nandfs_device *fsdev, uint64_t *seg)
{
	struct nandfs_node *su_node;
	struct nandfs_sufile_header *su_header;
	struct nandfs_segment_usage *su_usage;
	struct buf *bp_header, *bp;
	uint64_t blk, vblk, offset, i, rest, nsegments;
	uint16_t seg_size;
	int error, found;

	seg_size = fsdev->nd_fsdata.f_segment_usage_size;
	nsegments = fsdev->nd_fsdata.f_nsegments;

	su_node = fsdev->nd_su_node;
	ASSERT_VOP_LOCKED(NTOV(su_node), __func__);

	/* Read header buffer */
	error = nandfs_bread(su_node, 0, NOCRED, 0, &bp_header);
	if (error) {
		brelse(bp_header);
		return (error);
	}

	su_header = (struct nandfs_sufile_header *)bp_header->b_data;

	/* Get last allocated segment */
	i = su_header->sh_last_alloc + 1;

	found = 0;
	bp = NULL;
	while (!found) {
		nandfs_seg_usage_blk_offset(fsdev, i, &blk, &offset);
		if(blk != 0) {
			error = nandfs_bmap_lookup(su_node, blk, &vblk);
			if (error) {
				nandfs_error("%s: cannot find vblk for blk "
				    "blk:%jx\n", __func__, blk);
				return (error);
			}
			if (vblk)
				error = nandfs_bread(su_node, blk, NOCRED, 0,
				    &bp);
			else
				error = nandfs_bcreate(su_node, blk, NOCRED, 0,
				    &bp);
			if (error) {
				nandfs_error("%s: cannot create/read "
				    "vblk:%jx\n", __func__, vblk);
				if (bp)
					brelse(bp);
				return (error);
			}

			su_usage = SU_USAGE_OFF(bp, offset);
		} else {
			su_usage = SU_USAGE_OFF(bp_header, offset);
			bp = bp_header;
		}

		rest = (fsdev->nd_blocksize - offset) / seg_size;
		/* Go through all su usage in block */
		while (rest) {
			/* When last check start from beginning */
			if (i == nsegments)
				break;

			if (!su_usage->su_flags) {
				su_usage->su_flags = 1;
				found = 1;
				break;
			}
			su_usage++;
			i++;

			/* If all checked return error */
			if (i == su_header->sh_last_alloc) {
				DPRINTF(SEG, ("%s: cannot allocate segment \n",
				    __func__));
				brelse(bp_header);
				if (blk != 0)
					brelse(bp);
				return (1);
			}
			rest--;
		}
		if (!found) {
			/* Otherwise read another block */
			if (blk != 0)
				brelse(bp);
			if (i == nsegments) {
				blk = 0;
				i = 0;
			} else
				blk++;
			offset = 0;
		}
	}

	if (found) {
		*seg = i;
		su_header->sh_last_alloc = i;
		su_header->sh_ncleansegs--;
		su_header->sh_ndirtysegs++;

		fsdev->nd_super.s_free_blocks_count = su_header->sh_ncleansegs *
		    fsdev->nd_fsdata.f_blocks_per_segment;
		fsdev->nd_clean_segs--;

		/*
		 * It is mostly called from syncer() so we want to force
		 * making buf dirty.
		 */
		error = nandfs_dirty_buf(bp_header, 1);
		if (error) {
			if (bp && bp != bp_header)
				brelse(bp);
			return (error);
		}
		if (bp && bp != bp_header)
			nandfs_dirty_buf(bp, 1);

		DPRINTF(SEG, ("%s: seg:%#jx\n", __func__, (uintmax_t)i));

		return (0);
	}

	DPRINTF(SEG, ("%s: failed\n", __func__));

	return (1);
}

/*
 * Make buffer dirty, it will be updated soon but first it need to be
 * gathered by syncer.
 */
int
nandfs_touch_segment(struct nandfs_device *fsdev, uint64_t seg)
{
	struct nandfs_node *su_node;
	struct buf *bp;
	uint64_t blk, offset;
	int error;

	su_node = fsdev->nd_su_node;
	ASSERT_VOP_LOCKED(NTOV(su_node), __func__);

	nandfs_seg_usage_blk_offset(fsdev, seg, &blk, &offset);

	error = nandfs_bread(su_node, blk, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		nandfs_error("%s: cannot preallocate new segment\n", __func__);
		return (error);
	} else
		nandfs_dirty_buf(bp, 1);

	DPRINTF(SEG, ("%s: seg:%#jx\n", __func__, (uintmax_t)seg));
	return (error);
}

/* Update block count of segment */
int
nandfs_update_segment(struct nandfs_device *fsdev, uint64_t seg, uint32_t nblks)
{
	struct nandfs_node *su_node;
	struct nandfs_segment_usage *su_usage;
	struct buf *bp;
	uint64_t blk, offset;
	int error;

	su_node = fsdev->nd_su_node;
	ASSERT_VOP_LOCKED(NTOV(su_node), __func__);

	nandfs_seg_usage_blk_offset(fsdev, seg, &blk, &offset);

	error = nandfs_bread(su_node, blk, NOCRED, 0, &bp);
	if (error) {
		nandfs_error("%s: read block:%jx to update\n",
		    __func__, blk);
		brelse(bp);
		return (error);
	}

	su_usage = SU_USAGE_OFF(bp, offset);
	su_usage->su_lastmod = fsdev->nd_ts.tv_sec;
	su_usage->su_flags = NANDFS_SEGMENT_USAGE_DIRTY;
	su_usage->su_nblocks += nblks;

	DPRINTF(SEG, ("%s: seg:%#jx inc:%#x cur:%#x\n",  __func__,
	    (uintmax_t)seg, nblks, su_usage->su_nblocks));

	nandfs_dirty_buf(bp, 1);

	return (0);
}

/* Make segment free */
int
nandfs_free_segment(struct nandfs_device *fsdev, uint64_t seg)
{
	struct nandfs_node *su_node;
	struct nandfs_sufile_header *su_header;
	struct nandfs_segment_usage *su_usage;
	struct buf *bp_header, *bp;
	uint64_t blk, offset;
	int error;

	su_node = fsdev->nd_su_node;
	ASSERT_VOP_LOCKED(NTOV(su_node), __func__);

	/* Read su header */
	error = nandfs_bread(su_node, 0, NOCRED, 0, &bp_header);
	if (error) {
		brelse(bp_header);
		return (error);
	}

	su_header = (struct nandfs_sufile_header *)bp_header->b_data;
	nandfs_seg_usage_blk_offset(fsdev, seg, &blk, &offset);

	/* Read su usage block if other than su header block */
	if (blk != 0) {
		error = nandfs_bread(su_node, blk, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			brelse(bp_header);
			return (error);
		}
	} else
		bp = bp_header;

	/* Reset su usage data */
	su_usage = SU_USAGE_OFF(bp, offset);
	su_usage->su_lastmod = fsdev->nd_ts.tv_sec;
	su_usage->su_nblocks = 0;
	su_usage->su_flags = 0;

	/* Update clean/dirty counter in header */
	su_header->sh_ncleansegs++;
	su_header->sh_ndirtysegs--;

	/*
	 *  Make buffers dirty, called by cleaner
	 *  so force dirty even if no much space left
	 *  on device
	 */
	nandfs_dirty_buf(bp_header, 1);
	if (bp != bp_header)
		nandfs_dirty_buf(bp, 1);

	/* Update free block count */
	fsdev->nd_super.s_free_blocks_count = su_header->sh_ncleansegs *
	    fsdev->nd_fsdata.f_blocks_per_segment;
	fsdev->nd_clean_segs++;

	DPRINTF(SEG, ("%s: seg:%#jx\n", __func__, (uintmax_t)seg));

	return (0);
}

static int
nandfs_bad_segment(struct nandfs_device *fsdev, uint64_t seg)
{
	struct nandfs_node *su_node;
	struct nandfs_segment_usage *su_usage;
	struct buf *bp;
	uint64_t blk, offset;
	int error;

	su_node = fsdev->nd_su_node;
	ASSERT_VOP_LOCKED(NTOV(su_node), __func__);

	nandfs_seg_usage_blk_offset(fsdev, seg, &blk, &offset);

	error = nandfs_bread(su_node, blk, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	su_usage = SU_USAGE_OFF(bp, offset);
	su_usage->su_lastmod = fsdev->nd_ts.tv_sec;
	su_usage->su_flags = NANDFS_SEGMENT_USAGE_ERROR;

	DPRINTF(SEG, ("%s: seg:%#jx\n", __func__, (uintmax_t)seg));

	nandfs_dirty_buf(bp, 1);

	return (0);
}

int
nandfs_markgc_segment(struct nandfs_device *fsdev, uint64_t seg)
{
	struct nandfs_node *su_node;
	struct nandfs_segment_usage *su_usage;
	struct buf *bp;
	uint64_t blk, offset;
	int error;

	su_node = fsdev->nd_su_node;

	VOP_LOCK(NTOV(su_node), LK_EXCLUSIVE);

	nandfs_seg_usage_blk_offset(fsdev, seg, &blk, &offset);

	error = nandfs_bread(su_node, blk, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		VOP_UNLOCK(NTOV(su_node), 0);
		return (error);
	}

	su_usage = SU_USAGE_OFF(bp, offset);
	MPASS((su_usage->su_flags & NANDFS_SEGMENT_USAGE_GC) == 0);
	su_usage->su_flags |= NANDFS_SEGMENT_USAGE_GC;

	brelse(bp);
	VOP_UNLOCK(NTOV(su_node), 0);

	DPRINTF(SEG, ("%s: seg:%#jx\n", __func__, (uintmax_t)seg));

	return (0);
}

int
nandfs_clear_segment(struct nandfs_device *fsdev, uint64_t seg)
{
	uint64_t offset, segsize;
	uint32_t bps, bsize;
	int error = 0;

	bps = fsdev->nd_fsdata.f_blocks_per_segment;
	bsize = fsdev->nd_blocksize;
	segsize = bsize * bps;
	nandfs_get_segment_range(fsdev, seg, &offset, NULL);
	offset *= bsize;

	DPRINTF(SEG, ("%s: seg:%#jx\n", __func__, (uintmax_t)seg));

	/* Erase it and mark it bad when fail */
	if (nandfs_erase(fsdev, offset, segsize))
		error = nandfs_bad_segment(fsdev, seg);

	if (error)
		return (error);

	/* Mark it free */
	error = nandfs_free_segment(fsdev, seg);

	return (error);
}

int
nandfs_get_seg_stat(struct nandfs_device *nandfsdev,
    struct nandfs_seg_stat *nss)
{
	struct nandfs_sufile_header *suhdr;
	struct nandfs_node *su_node;
	struct buf *bp;
	int err;

	su_node = nandfsdev->nd_su_node;

	NANDFS_WRITELOCK(nandfsdev);
	VOP_LOCK(NTOV(su_node), LK_SHARED);
	err = nandfs_bread(nandfsdev->nd_su_node, 0, NOCRED, 0, &bp);
	if (err) {
		brelse(bp);
		VOP_UNLOCK(NTOV(su_node), 0);
		NANDFS_WRITEUNLOCK(nandfsdev);
		return (-1);
	}

	suhdr = (struct nandfs_sufile_header *)bp->b_data;
	nss->nss_nsegs = nandfsdev->nd_fsdata.f_nsegments;
	nss->nss_ncleansegs = suhdr->sh_ncleansegs;
	nss->nss_ndirtysegs = suhdr->sh_ndirtysegs;
	nss->nss_ctime = 0;
	nss->nss_nongc_ctime = nandfsdev->nd_ts.tv_sec;
	nss->nss_prot_seq = nandfsdev->nd_seg_sequence;

	brelse(bp);
	VOP_UNLOCK(NTOV(su_node), 0);

	NANDFS_WRITEUNLOCK(nandfsdev);

	return (0);
}

int
nandfs_get_segment_info_ioctl(struct nandfs_device *fsdev,
    struct nandfs_argv *nargv)
{
	struct nandfs_suinfo *nsi;
	int error;

	if (nargv->nv_nmembs > NANDFS_SEGMENTS_MAX)
		return (EINVAL);

	nsi = malloc(sizeof(struct nandfs_suinfo) * nargv->nv_nmembs,
	    M_NANDFSTEMP, M_WAITOK | M_ZERO);

	error = nandfs_get_segment_info(fsdev, nsi, nargv->nv_nmembs,
	    nargv->nv_index);

	if (error == 0)
		error = copyout(nsi, (void *)(uintptr_t)nargv->nv_base,
		    sizeof(struct nandfs_suinfo) * nargv->nv_nmembs);

	free(nsi, M_NANDFSTEMP);
	return (error);
}

int
nandfs_get_segment_info(struct nandfs_device *fsdev, struct nandfs_suinfo *nsi,
    uint32_t nmembs, uint64_t segment)
{

	return (nandfs_get_segment_info_filter(fsdev, nsi, nmembs, segment,
	    NULL, 0, 0));
}

int
nandfs_get_segment_info_filter(struct nandfs_device *fsdev,
    struct nandfs_suinfo *nsi, uint32_t nmembs, uint64_t segment,
    uint64_t *nsegs, uint32_t filter, uint32_t nfilter)
{
	struct nandfs_segment_usage *su;
	struct nandfs_node *su_node;
	struct buf *bp;
	uint64_t curr, blocknr, blockoff, i;
	uint32_t flags;
	int err = 0;

	curr = ~(0);

	lockmgr(&fsdev->nd_seg_const, LK_EXCLUSIVE, NULL);
	su_node = fsdev->nd_su_node;

	VOP_LOCK(NTOV(su_node), LK_SHARED);

	bp = NULL;
	if (nsegs !=  NULL)
		*nsegs = 0;
	for (i = 0; i < nmembs; segment++) {
		if (segment == fsdev->nd_fsdata.f_nsegments)
			break;

		nandfs_seg_usage_blk_offset(fsdev, segment, &blocknr,
		    &blockoff);

		if (i == 0 || curr != blocknr) {
			if (bp != NULL)
				brelse(bp);
			err = nandfs_bread(su_node, blocknr, NOCRED,
			    0, &bp);
			if (err) {
				goto out;
			}
			curr = blocknr;
		}

		su = SU_USAGE_OFF(bp, blockoff);
		flags = su->su_flags;
		if (segment == fsdev->nd_seg_num ||
		    segment == fsdev->nd_next_seg_num)
			flags |= NANDFS_SEGMENT_USAGE_ACTIVE;

		if (nfilter != 0 && (flags & nfilter) != 0)
			continue;
		if (filter != 0 && (flags & filter) == 0)
			continue;

		nsi->nsi_num = segment;
		nsi->nsi_lastmod = su->su_lastmod;
		nsi->nsi_blocks = su->su_nblocks;
		nsi->nsi_flags = flags;
		nsi++;
		i++;
		if (nsegs != NULL)
			(*nsegs)++;
	}

out:
	if (bp != NULL)
		brelse(bp);
	VOP_UNLOCK(NTOV(su_node), 0);
	lockmgr(&fsdev->nd_seg_const, LK_RELEASE, NULL);

	return (err);
}
