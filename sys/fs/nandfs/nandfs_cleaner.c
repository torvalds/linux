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
#include <sys/buf.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/bio.h>

#include <fs/nandfs/nandfs_mount.h>
#include <fs/nandfs/nandfs.h>
#include <fs/nandfs/nandfs_subr.h>

#define	NANDFS_CLEANER_KILL	1

static void nandfs_cleaner(struct nandfs_device *);
static int nandfs_cleaner_clean_segments(struct nandfs_device *,
    struct nandfs_vinfo *, uint32_t, struct nandfs_period *, uint32_t,
    struct nandfs_bdesc *, uint32_t, uint64_t *, uint32_t);

static int
nandfs_process_bdesc(struct nandfs_device *nffsdev, struct nandfs_bdesc *bd,
    uint64_t nmembs);

static void
nandfs_wakeup_wait_cleaner(struct nandfs_device *fsdev, int reason)
{

	mtx_lock(&fsdev->nd_clean_mtx);
	if (reason == NANDFS_CLEANER_KILL)
		fsdev->nd_cleaner_exit = 1;
	if (fsdev->nd_cleaning == 0) {
		fsdev->nd_cleaning = 1;
		wakeup(&fsdev->nd_cleaning);
	}
	cv_wait(&fsdev->nd_clean_cv, &fsdev->nd_clean_mtx);
	mtx_unlock(&fsdev->nd_clean_mtx);
}

int
nandfs_start_cleaner(struct nandfs_device *fsdev)
{
	int error;

	MPASS(fsdev->nd_cleaner == NULL);

	fsdev->nd_cleaner_exit = 0;

	error = kthread_add((void(*)(void *))nandfs_cleaner, fsdev, NULL,
	    &fsdev->nd_cleaner, 0, 0, "nandfs_cleaner");
	if (error)
		printf("nandfs: could not start cleaner: %d\n", error);

	return (error);
}

int
nandfs_stop_cleaner(struct nandfs_device *fsdev)
{

	MPASS(fsdev->nd_cleaner != NULL);
	nandfs_wakeup_wait_cleaner(fsdev, NANDFS_CLEANER_KILL);
	fsdev->nd_cleaner = NULL;

	DPRINTF(CLEAN, ("cleaner stopped\n"));
	return (0);
}

static int
nandfs_cleaner_finished(struct nandfs_device *fsdev)
{
	int exit;

	mtx_lock(&fsdev->nd_clean_mtx);
	fsdev->nd_cleaning = 0;
	if (!fsdev->nd_cleaner_exit) {
		DPRINTF(CLEAN, ("%s: sleep\n", __func__));
		msleep(&fsdev->nd_cleaning, &fsdev->nd_clean_mtx, PRIBIO, "-",
		    hz * nandfs_cleaner_interval);
	}
	exit = fsdev->nd_cleaner_exit;
	cv_broadcast(&fsdev->nd_clean_cv);
	mtx_unlock(&fsdev->nd_clean_mtx);
	if (exit) {
		DPRINTF(CLEAN, ("%s: no longer active\n", __func__));
		return (1);
	}

	return (0);
}

static void
print_suinfo(struct nandfs_suinfo *suinfo, int nsegs)
{
	int i;

	for (i = 0; i < nsegs; i++) {
		DPRINTF(CLEAN, ("%jx  %jd  %c%c%c  %10u\n",
		    suinfo[i].nsi_num, suinfo[i].nsi_lastmod,
		    (suinfo[i].nsi_flags &
		    (NANDFS_SEGMENT_USAGE_ACTIVE) ? 'a' : '-'),
		    (suinfo[i].nsi_flags &
		    (NANDFS_SEGMENT_USAGE_DIRTY) ? 'd' : '-'),
		    (suinfo[i].nsi_flags &
		    (NANDFS_SEGMENT_USAGE_ERROR) ? 'e' : '-'),
		    suinfo[i].nsi_blocks));
	}
}

static int
nandfs_cleaner_vblock_is_alive(struct nandfs_device *fsdev,
    struct nandfs_vinfo *vinfo, struct nandfs_cpinfo *cp, uint32_t ncps)
{
	int64_t idx, min, max;

	if (vinfo->nvi_end >= fsdev->nd_last_cno)
		return (1);

	if (ncps == 0)
		return (0);

	if (vinfo->nvi_end < cp[0].nci_cno ||
	    vinfo->nvi_start > cp[ncps - 1].nci_cno)
		return (0);

	idx = min = 0;
	max = ncps - 1;
	while (min <= max) {
		idx = (min + max) / 2;
		if (vinfo->nvi_start == cp[idx].nci_cno)
			return (1);
		if (vinfo->nvi_start < cp[idx].nci_cno)
			max = idx - 1;
		else
			min = idx + 1;
	}

	return (vinfo->nvi_end >= cp[idx].nci_cno);
}

static void
nandfs_cleaner_vinfo_mark_alive(struct nandfs_device *fsdev,
    struct nandfs_vinfo *vinfo, uint32_t nmembs, struct nandfs_cpinfo *cp,
    uint32_t ncps)
{
	uint32_t i;

	for (i = 0; i < nmembs; i++)
		vinfo[i].nvi_alive =
		    nandfs_cleaner_vblock_is_alive(fsdev, &vinfo[i], cp, ncps);
}

static int
nandfs_cleaner_bdesc_is_alive(struct nandfs_device *fsdev,
    struct nandfs_bdesc *bdesc)
{
	int alive;

	alive = bdesc->bd_oblocknr == bdesc->bd_blocknr;
	if (!alive)
		MPASS(abs(bdesc->bd_oblocknr - bdesc->bd_blocknr) > 2);

	return (alive);
}

static void
nandfs_cleaner_bdesc_mark_alive(struct nandfs_device *fsdev,
    struct nandfs_bdesc *bdesc, uint32_t nmembs)
{
	uint32_t i;

	for (i = 0; i < nmembs; i++)
		bdesc[i].bd_alive = nandfs_cleaner_bdesc_is_alive(fsdev,
		    &bdesc[i]);
}

static void
nandfs_cleaner_iterate_psegment(struct nandfs_device *fsdev,
    struct nandfs_segment_summary *segsum, union nandfs_binfo *binfo,
    nandfs_daddr_t blk, struct nandfs_vinfo **vipp, struct nandfs_bdesc **bdpp)
{
	int i;

	DPRINTF(CLEAN, ("%s nbinfos %x\n", __func__, segsum->ss_nbinfos));
	for (i = 0; i < segsum->ss_nbinfos; i++) {
		if (binfo[i].bi_v.bi_ino == NANDFS_DAT_INO) {
			(*bdpp)->bd_oblocknr = blk + segsum->ss_nblocks -
			    segsum->ss_nbinfos + i;
			/*
			 * XXX Hack
			 */
			if (segsum->ss_flags & NANDFS_SS_SR)
				(*bdpp)->bd_oblocknr--;
			(*bdpp)->bd_level = binfo[i].bi_dat.bi_level;
			(*bdpp)->bd_offset = binfo[i].bi_dat.bi_blkoff;
			(*bdpp)++;
		} else {
			(*vipp)->nvi_ino = binfo[i].bi_v.bi_ino;
			(*vipp)->nvi_vblocknr = binfo[i].bi_v.bi_vblocknr;
			(*vipp)++;
		}
	}
}

static int
nandfs_cleaner_iterate_segment(struct nandfs_device *fsdev, uint64_t segno,
    struct nandfs_vinfo **vipp, struct nandfs_bdesc **bdpp, int *select)
{
	struct nandfs_segment_summary *segsum;
	union nandfs_binfo *binfo;
	struct buf *bp;
	uint32_t nblocks;
	nandfs_daddr_t curr, start, end;
	int error = 0;

	nandfs_get_segment_range(fsdev, segno, &start, &end);

	DPRINTF(CLEAN, ("%s: segno %jx start %jx end %jx\n", __func__, segno,
	    start, end));

	*select = 0;

	for (curr = start; curr < end; curr += nblocks) {
		error = nandfs_dev_bread(fsdev, curr, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			nandfs_error("%s: couldn't load segment summary of %jx: %d\n",
			    __func__, segno, error);
			return (error);
		}

		segsum = (struct nandfs_segment_summary *)bp->b_data;
		binfo = (union nandfs_binfo *)(bp->b_data + segsum->ss_bytes);

		if (!nandfs_segsum_valid(segsum)) {
			brelse(bp);
			nandfs_error("nandfs: invalid summary of segment %jx\n", segno);
			return (error);
		}

		DPRINTF(CLEAN, ("%s: %jx magic %x bytes %x nblocks %x nbinfos "
		    "%x\n", __func__, segno, segsum->ss_magic, segsum->ss_bytes,
		    segsum->ss_nblocks, segsum->ss_nbinfos));

		nandfs_cleaner_iterate_psegment(fsdev, segsum, binfo, curr,
		    vipp, bdpp);
		nblocks = segsum->ss_nblocks;
		brelse(bp);
	}

	if (error == 0)
		*select = 1;

	return (error);
}

static int
nandfs_cleaner_choose_segment(struct nandfs_device *fsdev, uint64_t **segpp,
    uint64_t nsegs, uint64_t *rseg)
{
	struct nandfs_suinfo *suinfo;
	uint64_t i, ssegs;
	int error;

	suinfo = malloc(sizeof(*suinfo) * nsegs, M_NANDFSTEMP,
	    M_ZERO | M_WAITOK);

	if (*rseg >= fsdev->nd_fsdata.f_nsegments)
		*rseg = 0;

retry:
	error = nandfs_get_segment_info_filter(fsdev, suinfo, nsegs, *rseg,
	    &ssegs, NANDFS_SEGMENT_USAGE_DIRTY,
	    NANDFS_SEGMENT_USAGE_ACTIVE | NANDFS_SEGMENT_USAGE_ERROR |
	    NANDFS_SEGMENT_USAGE_GC);
	if (error) {
		nandfs_error("%s:%d", __FILE__, __LINE__);
		goto out;
	}
	if (ssegs == 0 && *rseg != 0) {
		*rseg = 0;
		goto retry;
	}
	if (ssegs > 0) {
		print_suinfo(suinfo, ssegs);

		for (i = 0; i < ssegs; i++) {
			(**segpp) = suinfo[i].nsi_num;
			(*segpp)++;
		}
		*rseg = suinfo[i - 1].nsi_num + 1;
	}

out:
	free(suinfo, M_NANDFSTEMP);
	return (error);
}

static int
nandfs_cleaner_body(struct nandfs_device *fsdev, uint64_t *rseg)
{
	struct nandfs_vinfo *vinfo, *vip, *vipi;
	struct nandfs_bdesc *bdesc, *bdp, *bdpi;
	struct nandfs_cpstat cpstat;
	struct nandfs_cpinfo *cpinfo = NULL;
	uint64_t *segnums, *segp;
	int select, selected;
	int error = 0;
	int nsegs;
	int i;

	nsegs = nandfs_cleaner_segments;

	vip = vinfo = malloc(sizeof(*vinfo) *
	    fsdev->nd_fsdata.f_blocks_per_segment * nsegs, M_NANDFSTEMP,
	    M_ZERO | M_WAITOK);
	bdp = bdesc = malloc(sizeof(*bdesc) *
	    fsdev->nd_fsdata.f_blocks_per_segment * nsegs, M_NANDFSTEMP,
	    M_ZERO | M_WAITOK);
	segp = segnums = malloc(sizeof(*segnums) * nsegs, M_NANDFSTEMP,
	    M_WAITOK);

	error = nandfs_cleaner_choose_segment(fsdev, &segp, nsegs, rseg);
	if (error) {
		nandfs_error("%s:%d", __FILE__, __LINE__);
		goto out;
	}

	if (segnums == segp)
		goto out;

	selected = 0;
	for (i = 0; i < segp - segnums; i++) {
		error = nandfs_cleaner_iterate_segment(fsdev, segnums[i], &vip,
		    &bdp, &select);
		if (error) {
			/*
			 * XXX deselect (see below)?
			 */
			goto out;
		}
		if (!select)
			segnums[i] = NANDFS_NOSEGMENT;
		else {
			error = nandfs_markgc_segment(fsdev, segnums[i]);
			if (error) {
				nandfs_error("%s:%d\n", __FILE__, __LINE__);
				goto out;
			}
			selected++;
		}
	}

	if (selected == 0) {
		MPASS(vinfo == vip);
		MPASS(bdesc == bdp);
		goto out;
	}

	error = nandfs_get_cpstat(fsdev->nd_cp_node, &cpstat);
	if (error) {
		nandfs_error("%s:%d\n", __FILE__, __LINE__);
		goto out;
	}

	if (cpstat.ncp_nss != 0) {
		cpinfo = malloc(sizeof(struct nandfs_cpinfo) * cpstat.ncp_nss,
		    M_NANDFSTEMP, M_WAITOK);
		error = nandfs_get_cpinfo(fsdev->nd_cp_node, 1, NANDFS_SNAPSHOT,
		    cpinfo, cpstat.ncp_nss, NULL);
		if (error) {
			nandfs_error("%s:%d\n", __FILE__, __LINE__);
			goto out_locked;
		}
	}

	NANDFS_WRITELOCK(fsdev);
	DPRINTF(CLEAN, ("%s: got lock\n", __func__));

	error = nandfs_get_dat_vinfo(fsdev, vinfo, vip - vinfo);
	if (error) {
		nandfs_error("%s:%d\n", __FILE__, __LINE__);
		goto out_locked;
	}

	nandfs_cleaner_vinfo_mark_alive(fsdev, vinfo, vip - vinfo, cpinfo,
	    cpstat.ncp_nss);

	error = nandfs_get_dat_bdescs(fsdev, bdesc, bdp - bdesc);
	if (error) {
		nandfs_error("%s:%d\n", __FILE__, __LINE__);
		goto out_locked;
	}

	nandfs_cleaner_bdesc_mark_alive(fsdev, bdesc, bdp - bdesc);

	DPRINTF(CLEAN, ("got:\n"));
	for (vipi = vinfo; vipi < vip; vipi++) {
		DPRINTF(CLEAN, ("v ino %jx vblocknr %jx start %jx end %jx "
		    "alive %d\n", vipi->nvi_ino, vipi->nvi_vblocknr,
		    vipi->nvi_start, vipi->nvi_end, vipi->nvi_alive));
	}
	for (bdpi = bdesc; bdpi < bdp; bdpi++) {
		DPRINTF(CLEAN, ("b oblocknr %jx blocknr %jx offset %jx "
		    "alive %d\n", bdpi->bd_oblocknr, bdpi->bd_blocknr,
		    bdpi->bd_offset, bdpi->bd_alive));
	}
	DPRINTF(CLEAN, ("end list\n"));

	error = nandfs_cleaner_clean_segments(fsdev, vinfo, vip - vinfo, NULL,
	    0, bdesc, bdp - bdesc, segnums, segp - segnums);
	if (error)
		nandfs_error("%s:%d\n", __FILE__, __LINE__);

out_locked:
	NANDFS_WRITEUNLOCK(fsdev);
out:
	free(cpinfo, M_NANDFSTEMP);
	free(segnums, M_NANDFSTEMP);
	free(bdesc, M_NANDFSTEMP);
	free(vinfo, M_NANDFSTEMP);

	return (error);
}

static void
nandfs_cleaner(struct nandfs_device *fsdev)
{
	uint64_t checked_seg = 0;
	int error;

	while (!nandfs_cleaner_finished(fsdev)) {
		if (!nandfs_cleaner_enable || rebooting)
			continue;

		DPRINTF(CLEAN, ("%s: run started\n", __func__));

		fsdev->nd_cleaning = 1;

		error = nandfs_cleaner_body(fsdev, &checked_seg);

		DPRINTF(CLEAN, ("%s: run finished error %d\n", __func__,
		    error));
	}

	DPRINTF(CLEAN, ("%s: exiting\n", __func__));
	kthread_exit();
}

static int
nandfs_cleaner_clean_segments(struct nandfs_device *nffsdev,
    struct nandfs_vinfo *vinfo, uint32_t nvinfo,
    struct nandfs_period *pd, uint32_t npd,
    struct nandfs_bdesc *bdesc, uint32_t nbdesc,
    uint64_t *segments, uint32_t nsegs)
{
	struct nandfs_node *gc;
	struct buf *bp;
	uint32_t i;
	int error = 0;

	gc = nffsdev->nd_gc_node;

	DPRINTF(CLEAN, ("%s: enter\n", __func__));

	VOP_LOCK(NTOV(gc), LK_EXCLUSIVE);
	for (i = 0; i < nvinfo; i++) {
		if (!vinfo[i].nvi_alive)
			continue;
		DPRINTF(CLEAN, ("%s: read vblknr:%#jx blk:%#jx\n",
		    __func__, (uintmax_t)vinfo[i].nvi_vblocknr,
		    (uintmax_t)vinfo[i].nvi_blocknr));
		error = nandfs_bread(nffsdev->nd_gc_node, vinfo[i].nvi_blocknr,
		    NULL, 0, &bp);
		if (error) {
			nandfs_error("%s:%d", __FILE__, __LINE__);
			VOP_UNLOCK(NTOV(gc), 0);
			goto out;
		}
		nandfs_vblk_set(bp, vinfo[i].nvi_vblocknr);
		nandfs_buf_set(bp, NANDFS_VBLK_ASSIGNED);
		nandfs_dirty_buf(bp, 1);
	}
	VOP_UNLOCK(NTOV(gc), 0);

	/* Delete checkpoints */
	for (i = 0; i < npd; i++) {
		DPRINTF(CLEAN, ("delete checkpoint: %jx\n",
		    (uintmax_t)pd[i].p_start));
		error = nandfs_delete_cp(nffsdev->nd_cp_node, pd[i].p_start,
		    pd[i].p_end);
		if (error) {
			nandfs_error("%s:%d", __FILE__, __LINE__);
			goto out;
		}
	}

	/* Update vblocks */
	for (i = 0; i < nvinfo; i++) {
		if (vinfo[i].nvi_alive)
			continue;
		DPRINTF(CLEAN, ("freeing vblknr: %jx\n", vinfo[i].nvi_vblocknr));
		error = nandfs_vblock_free(nffsdev, vinfo[i].nvi_vblocknr);
		if (error) {
			nandfs_error("%s:%d", __FILE__, __LINE__);
			goto out;
		}
	}

	error = nandfs_process_bdesc(nffsdev, bdesc, nbdesc);
	if (error) {
		nandfs_error("%s:%d", __FILE__, __LINE__);
		goto out;
	}

	/* Add segments to clean */
	if (nffsdev->nd_free_count) {
		nffsdev->nd_free_base = realloc(nffsdev->nd_free_base,
		    (nffsdev->nd_free_count + nsegs) * sizeof(uint64_t),
		    M_NANDFSTEMP, M_WAITOK | M_ZERO);
		memcpy(&nffsdev->nd_free_base[nffsdev->nd_free_count], segments,
		    nsegs * sizeof(uint64_t));
		nffsdev->nd_free_count += nsegs;
	} else {
		nffsdev->nd_free_base = malloc(nsegs * sizeof(uint64_t),
		    M_NANDFSTEMP, M_WAITOK|M_ZERO);
		memcpy(nffsdev->nd_free_base, segments,
		    nsegs * sizeof(uint64_t));
		nffsdev->nd_free_count = nsegs;
	}

out:

	DPRINTF(CLEAN, ("%s: exit error %d\n", __func__, error));

	return (error);
}

static int
nandfs_process_bdesc(struct nandfs_device *nffsdev, struct nandfs_bdesc *bd,
    uint64_t nmembs)
{
	struct nandfs_node *dat_node;
	struct buf *bp;
	uint64_t i;
	int error;

	dat_node = nffsdev->nd_dat_node;

	VOP_LOCK(NTOV(dat_node), LK_EXCLUSIVE);

	for (i = 0; i < nmembs; i++) {
		if (!bd[i].bd_alive)
			continue;
		DPRINTF(CLEAN, ("%s: idx %jx offset %jx\n",
		    __func__, i, bd[i].bd_offset));
		if (bd[i].bd_level) {
			error = nandfs_bread_meta(dat_node, bd[i].bd_offset,
			    NULL, 0, &bp);
			if (error) {
				nandfs_error("%s: cannot read dat node "
				    "level:%d\n", __func__, bd[i].bd_level);
				brelse(bp);
				VOP_UNLOCK(NTOV(dat_node), 0);
				return (error);
			}
			nandfs_dirty_buf_meta(bp, 1);
			nandfs_bmap_dirty_blocks(VTON(bp->b_vp), bp, 1);
		} else {
			error = nandfs_bread(dat_node, bd[i].bd_offset, NULL,
			    0, &bp);
			if (error) {
				nandfs_error("%s: cannot read dat node\n",
				    __func__);
				brelse(bp);
				VOP_UNLOCK(NTOV(dat_node), 0);
				return (error);
			}
			nandfs_dirty_buf(bp, 1);
		}
		DPRINTF(CLEAN, ("%s: bp: %p\n", __func__, bp));
	}

	VOP_UNLOCK(NTOV(dat_node), 0);

	return (0);
}
