/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf
 * Copyright (c) 2008, 2009 Reinoud Zandijk
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * From: NetBSD: nilfs_vfsops.c,v 1.1 2009/07/18 16:31:42 reinoud Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/sysctl.h>
#include <sys/libkern.h>

#include <geom/geom.h>
#include <geom/geom_vfs.h>

#include <machine/_inttypes.h>

#include <fs/nandfs/nandfs_mount.h>
#include <fs/nandfs/nandfs.h>
#include <fs/nandfs/nandfs_subr.h>

static MALLOC_DEFINE(M_NANDFSMNT, "nandfs_mount", "NANDFS mount structure");

#define	NANDFS_SET_SYSTEMFILE(vp) {	\
	(vp)->v_vflag |= VV_SYSTEM;	\
	vref(vp);			\
	vput(vp); }

#define	NANDFS_UNSET_SYSTEMFILE(vp) {	\
	VOP_LOCK(vp, LK_EXCLUSIVE);	\
	MPASS(vp->v_bufobj.bo_dirty.bv_cnt == 0); \
	(vp)->v_vflag &= ~VV_SYSTEM;	\
	vgone(vp);			\
	vput(vp); }

/* Globals */
struct _nandfs_devices nandfs_devices;

/* Parameters */
int nandfs_verbose = 0;

static void
nandfs_tunable_init(void *arg)
{

	TUNABLE_INT_FETCH("vfs.nandfs.verbose", &nandfs_verbose);
}
SYSINIT(nandfs_tunables, SI_SUB_VFS, SI_ORDER_ANY, nandfs_tunable_init, NULL);

static SYSCTL_NODE(_vfs, OID_AUTO, nandfs, CTLFLAG_RD, 0, "NAND filesystem");
static SYSCTL_NODE(_vfs_nandfs, OID_AUTO, mount, CTLFLAG_RD, 0,
    "NANDFS mountpoints");
SYSCTL_INT(_vfs_nandfs, OID_AUTO, verbose, CTLFLAG_RW, &nandfs_verbose, 0, "");

#define NANDFS_CONSTR_INTERVAL	5
int nandfs_sync_interval = NANDFS_CONSTR_INTERVAL; /* sync every 5 seconds */
SYSCTL_UINT(_vfs_nandfs, OID_AUTO, sync_interval, CTLFLAG_RW,
    &nandfs_sync_interval, 0, "");

#define NANDFS_MAX_DIRTY_SEGS	5
int nandfs_max_dirty_segs = NANDFS_MAX_DIRTY_SEGS; /* sync when 5 dirty seg */
SYSCTL_UINT(_vfs_nandfs, OID_AUTO, max_dirty_segs, CTLFLAG_RW,
    &nandfs_max_dirty_segs, 0, "");

#define NANDFS_CPS_BETWEEN_SBLOCKS 5
int nandfs_cps_between_sblocks = NANDFS_CPS_BETWEEN_SBLOCKS; /* write superblock every 5 checkpoints */
SYSCTL_UINT(_vfs_nandfs, OID_AUTO, cps_between_sblocks, CTLFLAG_RW,
    &nandfs_cps_between_sblocks, 0, "");

#define NANDFS_CLEANER_ENABLE 1
int nandfs_cleaner_enable = NANDFS_CLEANER_ENABLE;
SYSCTL_UINT(_vfs_nandfs, OID_AUTO, cleaner_enable, CTLFLAG_RW,
    &nandfs_cleaner_enable, 0, "");

#define NANDFS_CLEANER_INTERVAL 5
int nandfs_cleaner_interval = NANDFS_CLEANER_INTERVAL;
SYSCTL_UINT(_vfs_nandfs, OID_AUTO, cleaner_interval, CTLFLAG_RW,
    &nandfs_cleaner_interval, 0, "");

#define NANDFS_CLEANER_SEGMENTS 5
int nandfs_cleaner_segments = NANDFS_CLEANER_SEGMENTS;
SYSCTL_UINT(_vfs_nandfs, OID_AUTO, cleaner_segments, CTLFLAG_RW,
    &nandfs_cleaner_segments, 0, "");

static int nandfs_mountfs(struct vnode *devvp, struct mount *mp);
static vfs_mount_t	nandfs_mount;
static vfs_root_t	nandfs_root;
static vfs_statfs_t	nandfs_statfs;
static vfs_unmount_t	nandfs_unmount;
static vfs_vget_t	nandfs_vget;
static vfs_sync_t	nandfs_sync;
static const char *nandfs_opts[] = {
	"snap", "from", "noatime", NULL
};

/* System nodes */
static int
nandfs_create_system_nodes(struct nandfs_device *nandfsdev)
{
	int error;

	error = nandfs_get_node_raw(nandfsdev, NULL, NANDFS_DAT_INO,
	    &nandfsdev->nd_super_root.sr_dat, &nandfsdev->nd_dat_node);
	if (error)
		goto errorout;

	error = nandfs_get_node_raw(nandfsdev, NULL, NANDFS_CPFILE_INO,
	    &nandfsdev->nd_super_root.sr_cpfile, &nandfsdev->nd_cp_node);
	if (error)
		goto errorout;

	error = nandfs_get_node_raw(nandfsdev, NULL, NANDFS_SUFILE_INO,
	    &nandfsdev->nd_super_root.sr_sufile, &nandfsdev->nd_su_node);
	if (error)
		goto errorout;

	error = nandfs_get_node_raw(nandfsdev, NULL, NANDFS_GC_INO,
	    NULL, &nandfsdev->nd_gc_node);
	if (error)
		goto errorout;

	NANDFS_SET_SYSTEMFILE(NTOV(nandfsdev->nd_dat_node));
	NANDFS_SET_SYSTEMFILE(NTOV(nandfsdev->nd_cp_node));
	NANDFS_SET_SYSTEMFILE(NTOV(nandfsdev->nd_su_node));
	NANDFS_SET_SYSTEMFILE(NTOV(nandfsdev->nd_gc_node));

	DPRINTF(VOLUMES, ("System vnodes: dat: %p cp: %p su: %p\n",
	    NTOV(nandfsdev->nd_dat_node), NTOV(nandfsdev->nd_cp_node),
	    NTOV(nandfsdev->nd_su_node)));
	return (0);

errorout:
	nandfs_dispose_node(&nandfsdev->nd_gc_node);
	nandfs_dispose_node(&nandfsdev->nd_dat_node);
	nandfs_dispose_node(&nandfsdev->nd_cp_node);
	nandfs_dispose_node(&nandfsdev->nd_su_node);

	return (error);
}

static void
nandfs_release_system_nodes(struct nandfs_device *nandfsdev)
{

	if (!nandfsdev)
		return;
	if (nandfsdev->nd_refcnt > 0)
		return;

	if (nandfsdev->nd_gc_node)
		NANDFS_UNSET_SYSTEMFILE(NTOV(nandfsdev->nd_gc_node));
	if (nandfsdev->nd_dat_node)
		NANDFS_UNSET_SYSTEMFILE(NTOV(nandfsdev->nd_dat_node));
	if (nandfsdev->nd_cp_node)
		NANDFS_UNSET_SYSTEMFILE(NTOV(nandfsdev->nd_cp_node));
	if (nandfsdev->nd_su_node)
		NANDFS_UNSET_SYSTEMFILE(NTOV(nandfsdev->nd_su_node));
}

static int
nandfs_check_fsdata_crc(struct nandfs_fsdata *fsdata)
{
	uint32_t fsdata_crc, comp_crc;

	if (fsdata->f_magic != NANDFS_FSDATA_MAGIC)
		return (0);

	/* Preserve CRC */
	fsdata_crc = fsdata->f_sum;

	/* Calculate */
	fsdata->f_sum = (0);
	comp_crc = crc32((uint8_t *)fsdata, fsdata->f_bytes);

	/* Restore */
	fsdata->f_sum = fsdata_crc;

	/* Check CRC */
	return (fsdata_crc == comp_crc);
}

static int
nandfs_check_superblock_crc(struct nandfs_fsdata *fsdata,
    struct nandfs_super_block *super)
{
	uint32_t super_crc, comp_crc;

	/* Check super block magic */
	if (super->s_magic != NANDFS_SUPER_MAGIC)
		return (0);

	/* Preserve CRC */
	super_crc = super->s_sum;

	/* Calculate */
	super->s_sum = (0);
	comp_crc = crc32((uint8_t *)super, fsdata->f_sbbytes);

	/* Restore */
	super->s_sum = super_crc;

	/* Check CRC */
	return (super_crc == comp_crc);
}

static void
nandfs_calc_superblock_crc(struct nandfs_fsdata *fsdata,
    struct nandfs_super_block *super)
{
	uint32_t comp_crc;

	/* Calculate */
	super->s_sum = 0;
	comp_crc = crc32((uint8_t *)super, fsdata->f_sbbytes);

	/* Restore */
	super->s_sum = comp_crc;
}

static int
nandfs_is_empty(u_char *area, int size)
{
	int i;

	for (i = 0; i < size; i++)
		if (area[i] != 0xff)
			return (0);

	return (1);
}

static __inline int
nandfs_sblocks_in_esize(struct nandfs_device *fsdev)
{

	return ((fsdev->nd_erasesize - NANDFS_SBLOCK_OFFSET_BYTES) /
	    sizeof(struct nandfs_super_block));
}

static __inline int
nandfs_max_sblocks(struct nandfs_device *fsdev)
{

	return (NANDFS_NFSAREAS * nandfs_sblocks_in_esize(fsdev));
}

static __inline int
nandfs_sblocks_in_block(struct nandfs_device *fsdev)
{

	return (fsdev->nd_devblocksize / sizeof(struct nandfs_super_block));
}

#if 0
static __inline int
nandfs_sblocks_in_first_block(struct nandfs_device *fsdev)
{
	int n;

	n = nandfs_sblocks_in_block(fsdev) -
	    NANDFS_SBLOCK_OFFSET_BYTES / sizeof(struct nandfs_super_block);
	if (n < 0)
		n = 0;

	return (n);
}
#endif

static int
nandfs_write_superblock_at(struct nandfs_device *fsdev,
    struct nandfs_fsarea *fstp)
{
	struct nandfs_super_block *super, *supert;
	struct buf *bp;
	int sb_per_sector, sbs_in_fsd, read_block;
	int index, pos, error;
	off_t offset;

	DPRINTF(SYNC, ("%s: last_used %d nandfs_sblocks_in_esize %d\n",
	    __func__, fstp->last_used, nandfs_sblocks_in_esize(fsdev)));
	if (fstp->last_used == nandfs_sblocks_in_esize(fsdev) - 1)
		index = 0;
	else
		index = fstp->last_used + 1;

	super = &fsdev->nd_super;
	supert = NULL;

	sb_per_sector = nandfs_sblocks_in_block(fsdev);
	sbs_in_fsd = sizeof(struct nandfs_fsdata) /
	    sizeof(struct nandfs_super_block);
	index += sbs_in_fsd;
	offset = fstp->offset;

	DPRINTF(SYNC, ("%s: offset %#jx s_last_pseg %#jx s_last_cno %#jx "
	    "s_last_seq %#jx wtime %jd index %d\n", __func__, offset,
	    super->s_last_pseg, super->s_last_cno, super->s_last_seq,
	    super->s_wtime, index));

	read_block = btodb(offset + rounddown(index, sb_per_sector) *
	    sizeof(struct nandfs_super_block));

	DPRINTF(SYNC, ("%s: read_block %#x\n", __func__, read_block));

	if (index == sbs_in_fsd) {
		error = nandfs_erase(fsdev, offset, fsdev->nd_erasesize);
		if (error)
			return (error);

		error = bread(fsdev->nd_devvp, btodb(offset),
		    fsdev->nd_devblocksize, NOCRED, &bp);
		if (error) {
			printf("NANDFS: couldn't read initial data: %d\n",
			    error);
			brelse(bp);
			return (error);
		}
		memcpy(bp->b_data, &fsdev->nd_fsdata, sizeof(fsdev->nd_fsdata));
		/*
		 * 0xff-out the rest. This bp could be cached, so potentially
		 * b_data contains stale super blocks.
		 *
		 * We don't mind cached bp since most of the time we just add
		 * super blocks to already 0xff-out b_data and don't need to
		 * perform actual read.
		 */
		if (fsdev->nd_devblocksize > sizeof(fsdev->nd_fsdata))
			memset(bp->b_data + sizeof(fsdev->nd_fsdata), 0xff,
			    fsdev->nd_devblocksize - sizeof(fsdev->nd_fsdata));
		error = bwrite(bp);
		if (error) {
			printf("NANDFS: cannot rewrite initial data at %jx\n",
			    offset);
			return (error);
		}
	}

	error = bread(fsdev->nd_devvp, read_block, fsdev->nd_devblocksize,
	    NOCRED, &bp);
	if (error) {
		brelse(bp);
		return (error);
	}

	supert = (struct nandfs_super_block *)(bp->b_data);
	pos = index % sb_per_sector;

	DPRINTF(SYNC, ("%s: storing at %d\n", __func__, pos));
	memcpy(&supert[pos], super, sizeof(struct nandfs_super_block));

	/*
	 * See comment above in code that performs erase.
	 */
	if (pos == 0)
		memset(&supert[1], 0xff,
		    (sb_per_sector - 1) * sizeof(struct nandfs_super_block));

	error = bwrite(bp);
	if (error) {
		printf("NANDFS: cannot update superblock at %jx\n", offset);
		return (error);
	}

	DPRINTF(SYNC, ("%s: fstp->last_used %d -> %d\n", __func__,
	    fstp->last_used, index - sbs_in_fsd));
	fstp->last_used = index - sbs_in_fsd;

	return (0);
}

int
nandfs_write_superblock(struct nandfs_device *fsdev)
{
	struct nandfs_super_block *super;
	struct timespec ts;
	int error;
	int i, j;

	vfs_timestamp(&ts);

	super = &fsdev->nd_super;

	super->s_last_pseg = fsdev->nd_last_pseg;
	super->s_last_cno = fsdev->nd_last_cno;
	super->s_last_seq = fsdev->nd_seg_sequence;
	super->s_wtime = ts.tv_sec;

	nandfs_calc_superblock_crc(&fsdev->nd_fsdata, super);

	error = 0;
	for (i = 0, j = fsdev->nd_last_fsarea; i < NANDFS_NFSAREAS;
	    i++, j = (j + 1 % NANDFS_NFSAREAS)) {
		if (fsdev->nd_fsarea[j].flags & NANDFS_FSSTOR_FAILED) {
			DPRINTF(SYNC, ("%s: skipping %d\n", __func__, j));
			continue;
		}
		error = nandfs_write_superblock_at(fsdev, &fsdev->nd_fsarea[j]);
		if (error) {
			printf("NANDFS: writing superblock at offset %d failed:"
			    "%d\n", j * fsdev->nd_erasesize, error);
			fsdev->nd_fsarea[j].flags |= NANDFS_FSSTOR_FAILED;
		} else
			break;
	}

	if (i == NANDFS_NFSAREAS) {
		printf("NANDFS: superblock was not written\n");
		/*
		 * TODO: switch to read-only?
		 */
		return (error);
	} else
		fsdev->nd_last_fsarea = (j + 1) % NANDFS_NFSAREAS;

	return (0);
}

static int
nandfs_select_fsdata(struct nandfs_device *fsdev,
    struct nandfs_fsdata *fsdatat, struct nandfs_fsdata **fsdata, int nfsds)
{
	int i;

	*fsdata = NULL;
	for (i = 0; i < nfsds; i++) {
		DPRINTF(VOLUMES, ("%s: i %d f_magic %x f_crc %x\n", __func__,
		    i, fsdatat[i].f_magic, fsdatat[i].f_sum));
		if (!nandfs_check_fsdata_crc(&fsdatat[i]))
			continue;
		*fsdata = &fsdatat[i];
		break;
	}

	return (*fsdata != NULL ? 0 : EINVAL);
}

static int
nandfs_select_sb(struct nandfs_device *fsdev,
    struct nandfs_super_block *supert, struct nandfs_super_block **super,
    int nsbs)
{
	int i;

	*super = NULL;
	for (i = 0; i < nsbs; i++) {
		if (!nandfs_check_superblock_crc(&fsdev->nd_fsdata, &supert[i]))
			continue;
		DPRINTF(SYNC, ("%s: i %d s_last_cno %jx s_magic %x "
		    "s_wtime %jd\n", __func__, i, supert[i].s_last_cno,
		    supert[i].s_magic, supert[i].s_wtime));
		if (*super == NULL || supert[i].s_last_cno >
		    (*super)->s_last_cno)
			*super = &supert[i];
	}

	return (*super != NULL ? 0 : EINVAL);
}

static int
nandfs_read_structures_at(struct nandfs_device *fsdev,
    struct nandfs_fsarea *fstp, struct nandfs_fsdata *fsdata,
    struct nandfs_super_block *super)
{
	struct nandfs_super_block *tsuper, *tsuperd;
	struct buf *bp;
	int error, read_size;
	int i;
	int offset;

	offset = fstp->offset;

	if (fsdev->nd_erasesize > MAXBSIZE)
		read_size = MAXBSIZE;
	else
		read_size = fsdev->nd_erasesize;

	error = bread(fsdev->nd_devvp, btodb(offset), read_size, NOCRED, &bp);
	if (error) {
		printf("couldn't read: %d\n", error);
		brelse(bp);
		fstp->flags |= NANDFS_FSSTOR_FAILED;
		return (error);
	}

	tsuper = super;

	memcpy(fsdata, bp->b_data, sizeof(struct nandfs_fsdata));
	memcpy(tsuper, (bp->b_data + sizeof(struct nandfs_fsdata)),
	    read_size - sizeof(struct nandfs_fsdata));
	brelse(bp);

	tsuper += (read_size - sizeof(struct nandfs_fsdata)) /
	    sizeof(struct nandfs_super_block);

	for (i = 1; i < fsdev->nd_erasesize / read_size; i++) {
		error = bread(fsdev->nd_devvp, btodb(offset + i * read_size),
		    read_size, NOCRED, &bp);
		if (error) {
			printf("couldn't read: %d\n", error);
			brelse(bp);
			fstp->flags |= NANDFS_FSSTOR_FAILED;
			return (error);
		}
		memcpy(tsuper, bp->b_data, read_size);
		tsuper += read_size / sizeof(struct nandfs_super_block);
		brelse(bp);
	}

	tsuper -= 1;
	fstp->last_used = nandfs_sblocks_in_esize(fsdev) - 1;
	for (tsuperd = super - 1; (tsuper != tsuperd); tsuper -= 1) {
		if (nandfs_is_empty((u_char *)tsuper, sizeof(*tsuper)))
			fstp->last_used--;
		else
			break;
	}

	DPRINTF(VOLUMES, ("%s: last_used %d\n", __func__, fstp->last_used));

	return (0);
}

static int
nandfs_read_structures(struct nandfs_device *fsdev)
{
	struct nandfs_fsdata *fsdata, *fsdatat;
	struct nandfs_super_block *sblocks, *ssblock;
	u_int nsbs, nfsds, i;
	int error = 0;
	int nrsbs;

	nfsds = NANDFS_NFSAREAS;
	nsbs = nandfs_max_sblocks(fsdev);

	fsdatat = malloc(sizeof(struct nandfs_fsdata) * nfsds, M_NANDFSTEMP,
	    M_WAITOK | M_ZERO);
	sblocks = malloc(sizeof(struct nandfs_super_block) * nsbs, M_NANDFSTEMP,
	    M_WAITOK | M_ZERO);

	nrsbs = 0;
	for (i = 0; i < NANDFS_NFSAREAS; i++) {
		fsdev->nd_fsarea[i].offset = i * fsdev->nd_erasesize;
		error = nandfs_read_structures_at(fsdev, &fsdev->nd_fsarea[i],
		    &fsdatat[i], sblocks + nrsbs);
		if (error)
			continue;
		nrsbs += (fsdev->nd_fsarea[i].last_used + 1);
		if (fsdev->nd_fsarea[fsdev->nd_last_fsarea].last_used >
		    fsdev->nd_fsarea[i].last_used)
			fsdev->nd_last_fsarea = i;
	}

	if (nrsbs == 0) {
		printf("nandfs: no valid superblocks found\n");
		error = EINVAL;
		goto out;
	}

	error = nandfs_select_fsdata(fsdev, fsdatat, &fsdata, nfsds);
	if (error)
		goto out;
	memcpy(&fsdev->nd_fsdata, fsdata, sizeof(struct nandfs_fsdata));

	error = nandfs_select_sb(fsdev, sblocks, &ssblock, nsbs);
	if (error)
		goto out;

	memcpy(&fsdev->nd_super, ssblock, sizeof(struct nandfs_super_block));
out:
	free(fsdatat, M_NANDFSTEMP);
	free(sblocks, M_NANDFSTEMP);

	if (error == 0)
		DPRINTF(VOLUMES, ("%s: selected sb with w_time %jd "
		    "last_pseg %#jx\n", __func__, fsdev->nd_super.s_wtime,
		    fsdev->nd_super.s_last_pseg));

	return (error);
}

static void
nandfs_unmount_base(struct nandfs_device *nandfsdev)
{
	int error;

	if (!nandfsdev)
		return;

	/* Remove all our information */
	error = vinvalbuf(nandfsdev->nd_devvp, V_SAVE, 0, 0);
	if (error) {
		/*
		 * Flushing buffers failed when fs was umounting, can't do
		 * much now, just printf error and continue with umount.
		 */
		nandfs_error("%s(): error:%d when umounting FS\n",
		    __func__, error);
	}

	/* Release the device's system nodes */
	nandfs_release_system_nodes(nandfsdev);
}

static void
nandfs_get_ncleanseg(struct nandfs_device *nandfsdev)
{
	struct nandfs_seg_stat nss;

	nandfs_get_seg_stat(nandfsdev, &nss);
	nandfsdev->nd_clean_segs = nss.nss_ncleansegs;
	DPRINTF(VOLUMES, ("nandfs_mount: clean segs: %jx\n",
	    (uintmax_t)nandfsdev->nd_clean_segs));
}


static int
nandfs_mount_base(struct nandfs_device *nandfsdev, struct mount *mp,
    struct nandfs_args *args)
{
	uint32_t log_blocksize;
	int error;

	/* Flush out any old buffers remaining from a previous use. */
	if ((error = vinvalbuf(nandfsdev->nd_devvp, V_SAVE, 0, 0)))
		return (error);

	error = nandfs_read_structures(nandfsdev);
	if (error) {
		printf("nandfs: could not get valid filesystem structures\n");
		return (error);
	}

	if (nandfsdev->nd_fsdata.f_rev_level != NANDFS_CURRENT_REV) {
		printf("nandfs: unsupported file system revision: %d "
		    "(supported is %d).\n", nandfsdev->nd_fsdata.f_rev_level,
		    NANDFS_CURRENT_REV);
		return (EINVAL);
	}

	if (nandfsdev->nd_fsdata.f_erasesize != nandfsdev->nd_erasesize) {
		printf("nandfs: erasesize mismatch (device %#x, fs %#x)\n",
		    nandfsdev->nd_erasesize, nandfsdev->nd_fsdata.f_erasesize);
		return (EINVAL);
	}

	/* Get our blocksize */
	log_blocksize = nandfsdev->nd_fsdata.f_log_block_size;
	nandfsdev->nd_blocksize = (uint64_t) 1 << (log_blocksize + 10);
	DPRINTF(VOLUMES, ("%s: blocksize:%x\n", __func__,
	    nandfsdev->nd_blocksize));

	DPRINTF(VOLUMES, ("%s: accepted super block with cp %#jx\n", __func__,
	    (uintmax_t)nandfsdev->nd_super.s_last_cno));

	/* Calculate dat structure parameters */
	nandfs_calc_mdt_consts(nandfsdev, &nandfsdev->nd_dat_mdt,
	    nandfsdev->nd_fsdata.f_dat_entry_size);
	nandfs_calc_mdt_consts(nandfsdev, &nandfsdev->nd_ifile_mdt,
	    nandfsdev->nd_fsdata.f_inode_size);

	/* Search for the super root and roll forward when needed */
	if (nandfs_search_super_root(nandfsdev)) {
		printf("Cannot find valid SuperRoot\n");
		return (EINVAL);
	}

	nandfsdev->nd_mount_state = nandfsdev->nd_super.s_state;
	if (nandfsdev->nd_mount_state != NANDFS_VALID_FS) {
		printf("FS is seriously damaged, needs repairing\n");
		printf("aborting mount\n");
		return (EINVAL);
	}

	/*
	 * FS should be ok now. The superblock and the last segsum could be
	 * updated from the repair so extract running values again.
	 */
	nandfsdev->nd_last_pseg = nandfsdev->nd_super.s_last_pseg;
	nandfsdev->nd_seg_sequence = nandfsdev->nd_super.s_last_seq;
	nandfsdev->nd_seg_num = nandfs_get_segnum_of_block(nandfsdev,
	    nandfsdev->nd_last_pseg);
	nandfsdev->nd_next_seg_num = nandfs_get_segnum_of_block(nandfsdev,
	    nandfsdev->nd_last_segsum.ss_next);
	nandfsdev->nd_ts.tv_sec = nandfsdev->nd_last_segsum.ss_create;
	nandfsdev->nd_last_cno = nandfsdev->nd_super.s_last_cno;
	nandfsdev->nd_fakevblk = 1;
	/*
	 * FIXME: bogus calculation. Should use actual number of usable segments
	 * instead of total amount.
	 */
	nandfsdev->nd_segs_reserved =
	    nandfsdev->nd_fsdata.f_nsegments *
	    nandfsdev->nd_fsdata.f_r_segments_percentage / 100;
	nandfsdev->nd_last_ino  = NANDFS_USER_INO;
	DPRINTF(VOLUMES, ("%s: last_pseg %#jx last_cno %#jx last_seq %#jx\n"
	    "fsdev: last_seg: seq %#jx num %#jx, next_seg_num %#jx "
	    "segs_reserved %#jx\n",
	    __func__, (uintmax_t)nandfsdev->nd_last_pseg,
	    (uintmax_t)nandfsdev->nd_last_cno,
	    (uintmax_t)nandfsdev->nd_seg_sequence,
	    (uintmax_t)nandfsdev->nd_seg_sequence,
	    (uintmax_t)nandfsdev->nd_seg_num,
	    (uintmax_t)nandfsdev->nd_next_seg_num,
	    (uintmax_t)nandfsdev->nd_segs_reserved));

	DPRINTF(VOLUMES, ("nandfs_mount: accepted super root\n"));

	/* Create system vnodes for DAT, CP and SEGSUM */
	error = nandfs_create_system_nodes(nandfsdev);
	if (error)
		nandfs_unmount_base(nandfsdev);

	nandfs_get_ncleanseg(nandfsdev);

	return (error);
}

static void
nandfs_unmount_device(struct nandfs_device *nandfsdev)
{

	/* Is there anything? */
	if (nandfsdev == NULL)
		return;

	/* Remove the device only if we're the last reference */
	nandfsdev->nd_refcnt--;
	if (nandfsdev->nd_refcnt >= 1)
		return;

	MPASS(nandfsdev->nd_syncer == NULL);
	MPASS(nandfsdev->nd_cleaner == NULL);
	MPASS(nandfsdev->nd_free_base == NULL);

	/* Unmount our base */
	nandfs_unmount_base(nandfsdev);

	/* Remove from our device list */
	SLIST_REMOVE(&nandfs_devices, nandfsdev, nandfs_device, nd_next_device);

	DROP_GIANT();
	g_topology_lock();
	g_vfs_close(nandfsdev->nd_gconsumer);
	g_topology_unlock();
	PICKUP_GIANT();

	DPRINTF(VOLUMES, ("closing device\n"));

	/* Clear our mount reference and release device node */
	vrele(nandfsdev->nd_devvp);

	dev_rel(nandfsdev->nd_devvp->v_rdev);

	/* Free our device info */
	cv_destroy(&nandfsdev->nd_sync_cv);
	mtx_destroy(&nandfsdev->nd_sync_mtx);
	cv_destroy(&nandfsdev->nd_clean_cv);
	mtx_destroy(&nandfsdev->nd_clean_mtx);
	mtx_destroy(&nandfsdev->nd_mutex);
	lockdestroy(&nandfsdev->nd_seg_const);
	free(nandfsdev, M_NANDFSMNT);
}

static int
nandfs_check_mounts(struct nandfs_device *nandfsdev, struct mount *mp,
    struct nandfs_args *args)
{
	struct nandfsmount *nmp;
	uint64_t last_cno;

	/* no double-mounting of the same checkpoint */
	STAILQ_FOREACH(nmp, &nandfsdev->nd_mounts, nm_next_mount) {
		if (nmp->nm_mount_args.cpno == args->cpno)
			return (EBUSY);
	}

	/* Allow readonly mounts without questioning here */
	if (mp->mnt_flag & MNT_RDONLY)
		return (0);

	/* Read/write mount */
	STAILQ_FOREACH(nmp, &nandfsdev->nd_mounts, nm_next_mount) {
		/* Only one RW mount on this device! */
		if ((nmp->nm_vfs_mountp->mnt_flag & MNT_RDONLY)==0)
			return (EROFS);
		/* RDONLY on last mountpoint is device busy */
		last_cno = nmp->nm_nandfsdev->nd_super.s_last_cno;
		if (nmp->nm_mount_args.cpno == last_cno)
			return (EBUSY);
	}

	/* OK for now */
	return (0);
}

static int
nandfs_mount_device(struct vnode *devvp, struct mount *mp,
    struct nandfs_args *args, struct nandfs_device **nandfsdev_p)
{
	struct nandfs_device *nandfsdev;
	struct g_provider *pp;
	struct g_consumer *cp;
	struct cdev *dev;
	uint32_t erasesize;
	int error, size;
	int ronly;

	DPRINTF(VOLUMES, ("Mounting NANDFS device\n"));

	ronly = (mp->mnt_flag & MNT_RDONLY) != 0;

	/* Look up device in our nandfs_mountpoints */
	*nandfsdev_p = NULL;
	SLIST_FOREACH(nandfsdev, &nandfs_devices, nd_next_device)
		if (nandfsdev->nd_devvp == devvp)
			break;

	if (nandfsdev) {
		DPRINTF(VOLUMES, ("device already mounted\n"));
		error = nandfs_check_mounts(nandfsdev, mp, args);
		if (error)
			return error;
		nandfsdev->nd_refcnt++;
		*nandfsdev_p = nandfsdev;

		if (!ronly) {
			DROP_GIANT();
			g_topology_lock();
			error = g_access(nandfsdev->nd_gconsumer, 0, 1, 0);
			g_topology_unlock();
			PICKUP_GIANT();
		}
		return (error);
	}

	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	dev = devvp->v_rdev;
	dev_ref(dev);
	DROP_GIANT();
	g_topology_lock();
	error = g_vfs_open(devvp, &cp, "nandfs", ronly ? 0 : 1);
	pp = g_dev_getprovider(dev);
	g_topology_unlock();
	PICKUP_GIANT();
	VOP_UNLOCK(devvp, 0);
	if (error) {
		dev_rel(dev);
		return (error);
	}

	nandfsdev = malloc(sizeof(struct nandfs_device), M_NANDFSMNT, M_WAITOK | M_ZERO);

	/* Initialise */
	nandfsdev->nd_refcnt = 1;
	nandfsdev->nd_devvp = devvp;
	nandfsdev->nd_syncing = 0;
	nandfsdev->nd_cleaning = 0;
	nandfsdev->nd_gconsumer = cp;
	cv_init(&nandfsdev->nd_sync_cv, "nandfssync");
	mtx_init(&nandfsdev->nd_sync_mtx, "nffssyncmtx", NULL, MTX_DEF);
	cv_init(&nandfsdev->nd_clean_cv, "nandfsclean");
	mtx_init(&nandfsdev->nd_clean_mtx, "nffscleanmtx", NULL, MTX_DEF);
	mtx_init(&nandfsdev->nd_mutex, "nandfsdev lock", NULL, MTX_DEF);
	lockinit(&nandfsdev->nd_seg_const, PVFS, "nffssegcon", VLKTIMEOUT,
	    LK_CANRECURSE);
	STAILQ_INIT(&nandfsdev->nd_mounts);

	nandfsdev->nd_devsize = pp->mediasize;
	nandfsdev->nd_devblocksize = pp->sectorsize;

	size = sizeof(erasesize);
	error = g_io_getattr("NAND::blocksize", nandfsdev->nd_gconsumer, &size,
	    &erasesize);
	if (error) {
		DPRINTF(VOLUMES, ("couldn't get erasesize: %d\n", error));

		if (error == ENOIOCTL || error == EOPNOTSUPP) {
			/*
			 * We conclude that this is not NAND storage
			 */
			erasesize = NANDFS_DEF_ERASESIZE;
		} else {
			DROP_GIANT();
			g_topology_lock();
			g_vfs_close(nandfsdev->nd_gconsumer);
			g_topology_unlock();
			PICKUP_GIANT();
			dev_rel(dev);
			free(nandfsdev, M_NANDFSMNT);
			return (error);
		}
	}
	nandfsdev->nd_erasesize = erasesize;

	DPRINTF(VOLUMES, ("%s: erasesize %x\n", __func__,
	    nandfsdev->nd_erasesize));

	/* Register nandfs_device in list */
	SLIST_INSERT_HEAD(&nandfs_devices, nandfsdev, nd_next_device);

	error = nandfs_mount_base(nandfsdev, mp, args);
	if (error) {
		/* Remove all our information */
		nandfs_unmount_device(nandfsdev);
		return (EINVAL);
	}

	nandfsdev->nd_maxfilesize = nandfs_get_maxfilesize(nandfsdev);

	*nandfsdev_p = nandfsdev;
	DPRINTF(VOLUMES, ("NANDFS device mounted ok\n"));

	return (0);
}

static int
nandfs_mount_checkpoint(struct nandfsmount *nmp)
{
	struct nandfs_cpfile_header *cphdr;
	struct nandfs_checkpoint *cp;
	struct nandfs_inode ifile_inode;
	struct nandfs_node *cp_node;
	struct buf *bp;
	uint64_t ncp, nsn, cpno, fcpno, blocknr, last_cno;
	uint32_t off, dlen;
	int cp_per_block, error;

	cpno = nmp->nm_mount_args.cpno;
	if (cpno == 0)
		cpno = nmp->nm_nandfsdev->nd_super.s_last_cno;

	DPRINTF(VOLUMES, ("%s: trying to mount checkpoint number %"PRIu64"\n",
	    __func__, cpno));

	cp_node = nmp->nm_nandfsdev->nd_cp_node;

	VOP_LOCK(NTOV(cp_node), LK_SHARED);
	/* Get cpfile header from 1st block of cp file */
	error = nandfs_bread(cp_node, 0, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		VOP_UNLOCK(NTOV(cp_node), 0);
		return (error);
	}

	cphdr = (struct nandfs_cpfile_header *) bp->b_data;
	ncp = cphdr->ch_ncheckpoints;
	nsn = cphdr->ch_nsnapshots;

	brelse(bp);

	DPRINTF(VOLUMES, ("mount_nandfs: checkpoint header read in\n"));
	DPRINTF(VOLUMES, ("\tNumber of checkpoints %"PRIu64"\n", ncp));
	DPRINTF(VOLUMES, ("\tNumber of snapshots %"PRIu64"\n", nsn));

	/* Read in our specified checkpoint */
	dlen = nmp->nm_nandfsdev->nd_fsdata.f_checkpoint_size;
	cp_per_block = nmp->nm_nandfsdev->nd_blocksize / dlen;

	fcpno = cpno + NANDFS_CPFILE_FIRST_CHECKPOINT_OFFSET - 1;
	blocknr = fcpno / cp_per_block;
	off = (fcpno % cp_per_block) * dlen;
	error = nandfs_bread(cp_node, blocknr, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		VOP_UNLOCK(NTOV(cp_node), 0);
		printf("mount_nandfs: couldn't read cp block %"PRIu64"\n",
		    fcpno);
		return (EINVAL);
	}

	/* Needs to be a valid checkpoint */
	cp = (struct nandfs_checkpoint *) ((uint8_t *) bp->b_data + off);
	if (cp->cp_flags & NANDFS_CHECKPOINT_INVALID) {
		printf("mount_nandfs: checkpoint marked invalid\n");
		brelse(bp);
		VOP_UNLOCK(NTOV(cp_node), 0);
		return (EINVAL);
	}

	/* Is this really the checkpoint we want? */
	if (cp->cp_cno != cpno) {
		printf("mount_nandfs: checkpoint file corrupt? "
		    "expected cpno %"PRIu64", found cpno %"PRIu64"\n",
		    cpno, cp->cp_cno);
		brelse(bp);
		VOP_UNLOCK(NTOV(cp_node), 0);
		return (EINVAL);
	}

	/* Check if it's a snapshot ! */
	last_cno = nmp->nm_nandfsdev->nd_super.s_last_cno;
	if (cpno != last_cno) {
		/* Only allow snapshots if not mounting on the last cp */
		if ((cp->cp_flags & NANDFS_CHECKPOINT_SNAPSHOT) == 0) {
			printf( "mount_nandfs: checkpoint %"PRIu64" is not a "
			    "snapshot\n", cpno);
			brelse(bp);
			VOP_UNLOCK(NTOV(cp_node), 0);
			return (EINVAL);
		}
	}

	ifile_inode = cp->cp_ifile_inode;
	brelse(bp);

	/* Get ifile inode */
	error = nandfs_get_node_raw(nmp->nm_nandfsdev, NULL, NANDFS_IFILE_INO,
	    &ifile_inode, &nmp->nm_ifile_node);
	if (error) {
		printf("mount_nandfs: can't read ifile node\n");
		VOP_UNLOCK(NTOV(cp_node), 0);
		return (EINVAL);
	}

	NANDFS_SET_SYSTEMFILE(NTOV(nmp->nm_ifile_node));
	VOP_UNLOCK(NTOV(cp_node), 0);
	/* Get root node? */

	return (0);
}

static void
free_nandfs_mountinfo(struct mount *mp)
{
	struct nandfsmount *nmp = VFSTONANDFS(mp);

	if (nmp == NULL)
		return;

	free(nmp, M_NANDFSMNT);
}

void
nandfs_wakeup_wait_sync(struct nandfs_device *nffsdev, int reason)
{
	char *reasons[] = {
	    "umount",
	    "vfssync",
	    "bdflush",
	    "fforce",
	    "fsync",
	    "ro_upd"
	};

	DPRINTF(SYNC, ("%s: %s\n", __func__, reasons[reason]));
	mtx_lock(&nffsdev->nd_sync_mtx);
	if (nffsdev->nd_syncing)
		cv_wait(&nffsdev->nd_sync_cv, &nffsdev->nd_sync_mtx);
	if (reason == SYNCER_UMOUNT)
		nffsdev->nd_syncer_exit = 1;
	nffsdev->nd_syncing = 1;
	wakeup(&nffsdev->nd_syncing);
	cv_wait(&nffsdev->nd_sync_cv, &nffsdev->nd_sync_mtx);

	mtx_unlock(&nffsdev->nd_sync_mtx);
}

static void
nandfs_gc_finished(struct nandfs_device *nffsdev, int exit)
{
	int error;

	mtx_lock(&nffsdev->nd_sync_mtx);
	nffsdev->nd_syncing = 0;
	DPRINTF(SYNC, ("%s: cleaner finish\n", __func__));
	cv_broadcast(&nffsdev->nd_sync_cv);
	mtx_unlock(&nffsdev->nd_sync_mtx);
	if (!exit) {
		error = tsleep(&nffsdev->nd_syncing, PRIBIO, "-",
		    hz * nandfs_sync_interval);
		DPRINTF(SYNC, ("%s: cleaner waked up: %d\n",
		    __func__, error));
	}
}

static void
nandfs_syncer(struct nandfsmount *nmp)
{
	struct nandfs_device *nffsdev;
	struct mount *mp;
	int flags, error;

	mp = nmp->nm_vfs_mountp;
	nffsdev = nmp->nm_nandfsdev;
	tsleep(&nffsdev->nd_syncing, PRIBIO, "-", hz * nandfs_sync_interval);

	while (!nffsdev->nd_syncer_exit) {
		DPRINTF(SYNC, ("%s: syncer run\n", __func__));
		nffsdev->nd_syncing = 1;

		flags = (nmp->nm_flags & (NANDFS_FORCE_SYNCER | NANDFS_UMOUNT));

		error = nandfs_segment_constructor(nmp, flags);
		if (error)
			nandfs_error("%s: error:%d when creating segments\n",
			    __func__, error);

		nmp->nm_flags &= ~flags;

		nandfs_gc_finished(nffsdev, 0);
	}

	MPASS(nffsdev->nd_cleaner == NULL);
	error = nandfs_segment_constructor(nmp,
	    NANDFS_FORCE_SYNCER | NANDFS_UMOUNT);
	if (error)
		nandfs_error("%s: error:%d when creating segments\n",
		    __func__, error);
	nandfs_gc_finished(nffsdev, 1);
	nffsdev->nd_syncer = NULL;
	MPASS(nffsdev->nd_free_base == NULL);

	DPRINTF(SYNC, ("%s: exiting\n", __func__));
	kthread_exit();
}

static int
start_syncer(struct nandfsmount *nmp)
{
	int error;

	MPASS(nmp->nm_nandfsdev->nd_syncer == NULL);

	DPRINTF(SYNC, ("%s: start syncer\n", __func__));

	nmp->nm_nandfsdev->nd_syncer_exit = 0;

	error = kthread_add((void(*)(void *))nandfs_syncer, nmp, NULL,
	    &nmp->nm_nandfsdev->nd_syncer, 0, 0, "nandfs_syncer");

	if (error)
		printf("nandfs: could not start syncer: %d\n", error);

	return (error);
}

static int
stop_syncer(struct nandfsmount *nmp)
{

	MPASS(nmp->nm_nandfsdev->nd_syncer != NULL);

	nandfs_wakeup_wait_sync(nmp->nm_nandfsdev, SYNCER_UMOUNT);

	DPRINTF(SYNC, ("%s: stop syncer\n", __func__));
	return (0);
}

/*
 * Mount null layer
 */
static int
nandfs_mount(struct mount *mp)
{
	struct nandfsmount *nmp;
	struct vnode *devvp;
	struct nameidata nd;
	struct vfsoptlist *opts;
	struct thread *td;
	char *from;
	int error = 0, flags;

	DPRINTF(VOLUMES, ("%s: mp = %p\n", __func__, (void *)mp));

	td = curthread;
	opts = mp->mnt_optnew;

	if (vfs_filteropt(opts, nandfs_opts))
		return (EINVAL);

	/*
	 * Update is a no-op
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		nmp = VFSTONANDFS(mp);
		if (vfs_flagopt(mp->mnt_optnew, "export", NULL, 0)) {
			return (error);
		}
		if (!(nmp->nm_ronly) && vfs_flagopt(opts, "ro", NULL, 0)) {
			vn_start_write(NULL, &mp, V_WAIT);
			error = VFS_SYNC(mp, MNT_WAIT);
			if (error)
				return (error);
			vn_finished_write(mp);

			flags = WRITECLOSE;
			if (mp->mnt_flag & MNT_FORCE)
				flags |= FORCECLOSE;

			nandfs_wakeup_wait_sync(nmp->nm_nandfsdev,
			    SYNCER_ROUPD);
			error = vflush(mp, 0, flags, td);
			if (error)
				return (error);

			nandfs_stop_cleaner(nmp->nm_nandfsdev);
			stop_syncer(nmp);
			DROP_GIANT();
			g_topology_lock();
			g_access(nmp->nm_nandfsdev->nd_gconsumer, 0, -1, 0);
			g_topology_unlock();
			PICKUP_GIANT();
			MNT_ILOCK(mp);
			mp->mnt_flag |= MNT_RDONLY;
			MNT_IUNLOCK(mp);
			nmp->nm_ronly = 1;

		} else if ((nmp->nm_ronly) &&
		    !vfs_flagopt(opts, "ro", NULL, 0)) {
			/*
			 * Don't allow read-write snapshots.
			 */
			if (nmp->nm_mount_args.cpno != 0)
				return (EROFS);
			/*
			 * If upgrade to read-write by non-root, then verify
			 * that user has necessary permissions on the device.
			 */
			devvp = nmp->nm_nandfsdev->nd_devvp;
			vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
			error = VOP_ACCESS(devvp, VREAD | VWRITE,
			    td->td_ucred, td);
			if (error) {
				error = priv_check(td, PRIV_VFS_MOUNT_PERM);
				if (error) {
					VOP_UNLOCK(devvp, 0);
					return (error);
				}
			}

			VOP_UNLOCK(devvp, 0);
			DROP_GIANT();
			g_topology_lock();
			error = g_access(nmp->nm_nandfsdev->nd_gconsumer, 0, 1,
			    0);
			g_topology_unlock();
			PICKUP_GIANT();
			if (error)
				return (error);

			MNT_ILOCK(mp);
			mp->mnt_flag &= ~MNT_RDONLY;
			MNT_IUNLOCK(mp);
			error = start_syncer(nmp);
			if (error == 0)
				error = nandfs_start_cleaner(nmp->nm_nandfsdev);
			if (error) {
				DROP_GIANT();
				g_topology_lock();
				g_access(nmp->nm_nandfsdev->nd_gconsumer, 0, -1,
				    0);
				g_topology_unlock();
				PICKUP_GIANT();
				return (error);
			}

			nmp->nm_ronly = 0;
		}
		return (0);
	}

	from = vfs_getopts(opts, "from", &error);
	if (error)
		return (error);

	/*
	 * Find device node
	 */
	NDINIT(&nd, LOOKUP, FOLLOW|LOCKLEAF, UIO_SYSSPACE, from, curthread);
	error = namei(&nd);
	if (error)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);

	devvp = nd.ni_vp;

	if (!vn_isdisk(devvp, &error)) {
		vput(devvp);
		return (error);
	}

	/* Check the access rights on the mount device */
	error = VOP_ACCESS(devvp, VREAD, curthread->td_ucred, curthread);
	if (error)
		error = priv_check(curthread, PRIV_VFS_MOUNT_PERM);
	if (error) {
		vput(devvp);
		return (error);
	}

	vfs_getnewfsid(mp);

	error = nandfs_mountfs(devvp, mp);
	if (error)
		return (error);
	vfs_mountedfrom(mp, from);

	return (0);
}

static int
nandfs_mountfs(struct vnode *devvp, struct mount *mp)
{
	struct nandfsmount *nmp = NULL;
	struct nandfs_args *args = NULL;
	struct nandfs_device *nandfsdev;
	char *from;
	int error, ronly;
	char *cpno;

	ronly = (mp->mnt_flag & MNT_RDONLY) != 0;

	if (devvp->v_rdev->si_iosize_max != 0)
		mp->mnt_iosize_max = devvp->v_rdev->si_iosize_max;
	VOP_UNLOCK(devvp, 0);

	if (mp->mnt_iosize_max > MAXPHYS)
		mp->mnt_iosize_max = MAXPHYS;

	from = vfs_getopts(mp->mnt_optnew, "from", &error);
	if (error)
		goto error;

	error = vfs_getopt(mp->mnt_optnew, "snap", (void **)&cpno, NULL);
	if (error == ENOENT)
		cpno = NULL;
	else if (error)
		goto error;

	args = (struct nandfs_args *)malloc(sizeof(struct nandfs_args),
	    M_NANDFSMNT, M_WAITOK | M_ZERO);

	if (cpno != NULL)
		args->cpno = strtoul(cpno, (char **)NULL, 10);
	else
		args->cpno = 0;
	args->fspec = from;

	if (args->cpno != 0 && !ronly) {
		error = EROFS;
		goto error;
	}

	printf("WARNING: NANDFS is considered to be a highly experimental "
	    "feature in FreeBSD.\n");

	error = nandfs_mount_device(devvp, mp, args, &nandfsdev);
	if (error)
		goto error;

	nmp = (struct nandfsmount *) malloc(sizeof(struct nandfsmount),
	    M_NANDFSMNT, M_WAITOK | M_ZERO);

	mp->mnt_data = nmp;
	nmp->nm_vfs_mountp = mp;
	nmp->nm_ronly = ronly;
	MNT_ILOCK(mp);
	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_kern_flag |= MNTK_USES_BCACHE;
	MNT_IUNLOCK(mp);
	nmp->nm_nandfsdev = nandfsdev;
	/* Add our mountpoint */
	STAILQ_INSERT_TAIL(&nandfsdev->nd_mounts, nmp, nm_next_mount);

	if (args->cpno > nandfsdev->nd_last_cno) {
		printf("WARNING: supplied checkpoint number (%jd) is greater "
		    "than last known checkpoint on filesystem (%jd). Mounting"
		    " checkpoint %jd\n", (uintmax_t)args->cpno,
		    (uintmax_t)nandfsdev->nd_last_cno,
		    (uintmax_t)nandfsdev->nd_last_cno);
		args->cpno = nandfsdev->nd_last_cno;
	}

	/* Setting up other parameters */
	nmp->nm_mount_args = *args;
	free(args, M_NANDFSMNT);
	error = nandfs_mount_checkpoint(nmp);
	if (error) {
		nandfs_unmount(mp, MNT_FORCE);
		goto unmounted;
	}

	if (!ronly) {
		error = start_syncer(nmp);
		if (error == 0)
			error = nandfs_start_cleaner(nmp->nm_nandfsdev);
		if (error)
			nandfs_unmount(mp, MNT_FORCE);
	}

	return (0);

error:
	if (args != NULL)
		free(args, M_NANDFSMNT);

	if (nmp != NULL) {
		free(nmp, M_NANDFSMNT);
		mp->mnt_data = NULL;
	}
unmounted:
	return (error);
}

static int
nandfs_unmount(struct mount *mp, int mntflags)
{
	struct nandfs_device *nandfsdev;
	struct nandfsmount *nmp;
	int error;
	int flags = 0;

	DPRINTF(VOLUMES, ("%s: mp = %p\n", __func__, (void *)mp));

	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	nmp = mp->mnt_data;
	nandfsdev = nmp->nm_nandfsdev;

	error = vflush(mp, 0, flags | SKIPSYSTEM, curthread);
	if (error)
		return (error);

	if (!(nmp->nm_ronly)) {
		nandfs_stop_cleaner(nandfsdev);
		stop_syncer(nmp);
	}

	if (nmp->nm_ifile_node)
		NANDFS_UNSET_SYSTEMFILE(NTOV(nmp->nm_ifile_node));

	/* Remove our mount point */
	STAILQ_REMOVE(&nandfsdev->nd_mounts, nmp, nandfsmount, nm_next_mount);

	/* Unmount the device itself when we're the last one */
	nandfs_unmount_device(nandfsdev);

	free_nandfs_mountinfo(mp);

	/*
	 * Finally, throw away the null_mount structure
	 */
	mp->mnt_data = 0;
	MNT_ILOCK(mp);
	mp->mnt_flag &= ~MNT_LOCAL;
	MNT_IUNLOCK(mp);

	return (0);
}

static int
nandfs_statfs(struct mount *mp, struct statfs *sbp)
{
	struct nandfsmount *nmp;
	struct nandfs_device *nandfsdev;
	struct nandfs_fsdata *fsdata;
	struct nandfs_super_block *sb;
	struct nandfs_block_group_desc *groups;
	struct nandfs_node *ifile;
	struct nandfs_mdt *mdt;
	struct buf *bp;
	int i, error;
	uint32_t entries_per_group;
	uint64_t files = 0;

	nmp = mp->mnt_data;
	nandfsdev = nmp->nm_nandfsdev;
	fsdata = &nandfsdev->nd_fsdata;
	sb = &nandfsdev->nd_super;
	ifile = nmp->nm_ifile_node;
	mdt = &nandfsdev->nd_ifile_mdt;
	entries_per_group = mdt->entries_per_group;

	VOP_LOCK(NTOV(ifile), LK_SHARED);
	error = nandfs_bread(ifile, 0, NOCRED, 0, &bp);
	if (error) {
		brelse(bp);
		VOP_UNLOCK(NTOV(ifile), 0);
		return (error);
	}

	groups = (struct nandfs_block_group_desc *)bp->b_data;

	for (i = 0; i < mdt->groups_per_desc_block; i++)
		files += (entries_per_group - groups[i].bg_nfrees);

	brelse(bp);
	VOP_UNLOCK(NTOV(ifile), 0);

	sbp->f_bsize = nandfsdev->nd_blocksize;
	sbp->f_iosize = sbp->f_bsize;
	sbp->f_blocks = fsdata->f_blocks_per_segment * fsdata->f_nsegments;
	sbp->f_bfree = sb->s_free_blocks_count;
	sbp->f_bavail = sbp->f_bfree;
	sbp->f_files = files;
	sbp->f_ffree = 0;
	return (0);
}

static int
nandfs_root(struct mount *mp, int flags, struct vnode **vpp)
{
	struct nandfsmount *nmp = VFSTONANDFS(mp);
	struct nandfs_node *node;
	int error;

	error = nandfs_get_node(nmp, NANDFS_ROOT_INO, &node);
	if (error)
		return (error);

	KASSERT(NTOV(node)->v_vflag & VV_ROOT,
	    ("root_vp->v_vflag & VV_ROOT"));

	*vpp = NTOV(node);

	return (error);
}

static int
nandfs_vget(struct mount *mp, ino_t ino, int flags, struct vnode **vpp)
{
	struct nandfsmount *nmp = VFSTONANDFS(mp);
	struct nandfs_node *node;
	int error;

	error = nandfs_get_node(nmp, ino, &node);
	if (node)
		*vpp = NTOV(node);

	return (error);
}

static int
nandfs_sync(struct mount *mp, int waitfor)
{
	struct nandfsmount *nmp = VFSTONANDFS(mp);

	DPRINTF(SYNC, ("%s: mp %p waitfor %d\n", __func__, mp, waitfor));

	/*
	 * XXX: A hack to be removed soon
	 */
	if (waitfor == MNT_LAZY)
		return (0);
	if (waitfor == MNT_SUSPEND)
		return (0);
	nandfs_wakeup_wait_sync(nmp->nm_nandfsdev, SYNCER_VFS_SYNC);
	return (0);
}

static struct vfsops nandfs_vfsops = {
	.vfs_init =		nandfs_init,
	.vfs_mount =		nandfs_mount,
	.vfs_root =		nandfs_root,
	.vfs_statfs =		nandfs_statfs,
	.vfs_uninit =		nandfs_uninit,
	.vfs_unmount =		nandfs_unmount,
	.vfs_vget =		nandfs_vget,
	.vfs_sync =		nandfs_sync,
};

VFS_SET(nandfs_vfsops, nandfs, VFCF_LOOPBACK);
