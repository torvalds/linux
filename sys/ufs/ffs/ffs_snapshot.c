/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2000 Marshall Kirk McKusick. All Rights Reserved.
 *
 * Further information about snapshots can be obtained from:
 *
 *	Marshall Kirk McKusick		http://www.mckusick.com/softdep/
 *	1614 Oxford Street		mckusick@mckusick.com
 *	Berkeley, CA 94709-1608		+1-510-843-9542
 *	USA
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY MARSHALL KIRK MCKUSICK ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL MARSHALL KIRK MCKUSICK BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ffs_snapshot.c	8.11 (McKusick) 7/23/00
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_quota.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/sched.h>
#include <sys/stat.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/vnode.h>

#include <geom/geom.h>

#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufs_extern.h>

#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>

#define KERNCRED thread0.td_ucred
#define DEBUG 1

#include "opt_ffs.h"

#ifdef NO_FFS_SNAPSHOT
int
ffs_snapshot(mp, snapfile)
	struct mount *mp;
	char *snapfile;
{
	return (EINVAL);
}

int
ffs_snapblkfree(fs, devvp, bno, size, inum, vtype, wkhd)
	struct fs *fs;
	struct vnode *devvp;
	ufs2_daddr_t bno;
	long size;
	ino_t inum;
	enum vtype vtype;
	struct workhead *wkhd;
{
	return (EINVAL);
}

void
ffs_snapremove(vp)
	struct vnode *vp;
{
}

void
ffs_snapshot_mount(mp)
	struct mount *mp;
{
}

void
ffs_snapshot_unmount(mp)
	struct mount *mp;
{
}

void
ffs_snapgone(ip)
	struct inode *ip;
{
}

int
ffs_copyonwrite(devvp, bp)
	struct vnode *devvp;
	struct buf *bp;
{
	return (EINVAL);
}

void
ffs_sync_snap(mp, waitfor)
	struct mount *mp;
	int waitfor;
{
}

#else
FEATURE(ffs_snapshot, "FFS snapshot support");

LIST_HEAD(, snapdata) snapfree;
static struct mtx snapfree_lock;
MTX_SYSINIT(ffs_snapfree, &snapfree_lock, "snapdata free list", MTX_DEF);

static int cgaccount(int, struct vnode *, struct buf *, int);
static int expunge_ufs1(struct vnode *, struct inode *, struct fs *,
    int (*)(struct vnode *, ufs1_daddr_t *, ufs1_daddr_t *, struct fs *,
    ufs_lbn_t, int), int, int);
static int indiracct_ufs1(struct vnode *, struct vnode *, int,
    ufs1_daddr_t, ufs_lbn_t, ufs_lbn_t, ufs_lbn_t, ufs_lbn_t, struct fs *,
    int (*)(struct vnode *, ufs1_daddr_t *, ufs1_daddr_t *, struct fs *,
    ufs_lbn_t, int), int);
static int fullacct_ufs1(struct vnode *, ufs1_daddr_t *, ufs1_daddr_t *,
    struct fs *, ufs_lbn_t, int);
static int snapacct_ufs1(struct vnode *, ufs1_daddr_t *, ufs1_daddr_t *,
    struct fs *, ufs_lbn_t, int);
static int mapacct_ufs1(struct vnode *, ufs1_daddr_t *, ufs1_daddr_t *,
    struct fs *, ufs_lbn_t, int);
static int expunge_ufs2(struct vnode *, struct inode *, struct fs *,
    int (*)(struct vnode *, ufs2_daddr_t *, ufs2_daddr_t *, struct fs *,
    ufs_lbn_t, int), int, int);
static int indiracct_ufs2(struct vnode *, struct vnode *, int,
    ufs2_daddr_t, ufs_lbn_t, ufs_lbn_t, ufs_lbn_t, ufs_lbn_t, struct fs *,
    int (*)(struct vnode *, ufs2_daddr_t *, ufs2_daddr_t *, struct fs *,
    ufs_lbn_t, int), int);
static int fullacct_ufs2(struct vnode *, ufs2_daddr_t *, ufs2_daddr_t *,
    struct fs *, ufs_lbn_t, int);
static int snapacct_ufs2(struct vnode *, ufs2_daddr_t *, ufs2_daddr_t *,
    struct fs *, ufs_lbn_t, int);
static int mapacct_ufs2(struct vnode *, ufs2_daddr_t *, ufs2_daddr_t *,
    struct fs *, ufs_lbn_t, int);
static int readblock(struct vnode *vp, struct buf *, ufs2_daddr_t);
static void try_free_snapdata(struct vnode *devvp);
static struct snapdata *ffs_snapdata_acquire(struct vnode *devvp);
static int ffs_bp_snapblk(struct vnode *, struct buf *);

/*
 * To ensure the consistency of snapshots across crashes, we must
 * synchronously write out copied blocks before allowing the
 * originals to be modified. Because of the rather severe speed
 * penalty that this imposes, the code normally only ensures
 * persistence for the filesystem metadata contained within a
 * snapshot. Setting the following flag allows this crash
 * persistence to be enabled for file contents.
 */
int dopersistence = 0;

#ifdef DEBUG
#include <sys/sysctl.h>
SYSCTL_INT(_debug, OID_AUTO, dopersistence, CTLFLAG_RW, &dopersistence, 0, "");
static int snapdebug = 0;
SYSCTL_INT(_debug, OID_AUTO, snapdebug, CTLFLAG_RW, &snapdebug, 0, "");
int collectsnapstats = 0;
SYSCTL_INT(_debug, OID_AUTO, collectsnapstats, CTLFLAG_RW, &collectsnapstats,
	0, "");
#endif /* DEBUG */

/*
 * Create a snapshot file and initialize it for the filesystem.
 */
int
ffs_snapshot(mp, snapfile)
	struct mount *mp;
	char *snapfile;
{
	ufs2_daddr_t numblks, blkno, *blkp, *snapblklist;
	int error, cg, snaploc;
	int i, size, len, loc;
	ufs2_daddr_t blockno;
	uint64_t flag;
	struct timespec starttime = {0, 0}, endtime;
	char saved_nice = 0;
	long redo = 0, snaplistsize = 0;
	int32_t *lp;
	void *space;
	struct fs *copy_fs = NULL, *fs;
	struct thread *td = curthread;
	struct inode *ip, *xp;
	struct buf *bp, *nbp, *ibp;
	struct nameidata nd;
	struct mount *wrtmp;
	struct vattr vat;
	struct vnode *vp, *xvp, *mvp, *devvp;
	struct uio auio;
	struct iovec aiov;
	struct snapdata *sn;
	struct ufsmount *ump;

	ump = VFSTOUFS(mp);
	fs = ump->um_fs;
	sn = NULL;
	/*
	 * At the moment, journaled soft updates cannot support
	 * taking snapshots.
	 */
	if (MOUNTEDSUJ(mp)) {
		vfs_mount_error(mp, "%s: Snapshots are not yet supported when "
		    "running with journaled soft updates", fs->fs_fsmnt);
		return (EOPNOTSUPP);
	}
	MNT_ILOCK(mp);
	flag = mp->mnt_flag;
	MNT_IUNLOCK(mp);
	/*
	 * Need to serialize access to snapshot code per filesystem.
	 */
	/*
	 * Assign a snapshot slot in the superblock.
	 */
	UFS_LOCK(ump);
	for (snaploc = 0; snaploc < FSMAXSNAP; snaploc++)
		if (fs->fs_snapinum[snaploc] == 0)
			break;
	UFS_UNLOCK(ump);
	if (snaploc == FSMAXSNAP)
		return (ENOSPC);
	/*
	 * Create the snapshot file.
	 */
restart:
	NDINIT(&nd, CREATE, LOCKPARENT | LOCKLEAF | NOCACHE, UIO_SYSSPACE,
	    snapfile, td);
	if ((error = namei(&nd)) != 0)
		return (error);
	if (nd.ni_vp != NULL) {
		vput(nd.ni_vp);
		error = EEXIST;
	}
	if (nd.ni_dvp->v_mount != mp)
		error = EXDEV;
	if (error) {
		NDFREE(&nd, NDF_ONLY_PNBUF);
		if (nd.ni_dvp == nd.ni_vp)
			vrele(nd.ni_dvp);
		else
			vput(nd.ni_dvp);
		return (error);
	}
	VATTR_NULL(&vat);
	vat.va_type = VREG;
	vat.va_mode = S_IRUSR;
	vat.va_vaflags |= VA_EXCLUSIVE;
	if (VOP_GETWRITEMOUNT(nd.ni_dvp, &wrtmp))
		wrtmp = NULL;
	if (wrtmp != mp)
		panic("ffs_snapshot: mount mismatch");
	vfs_rel(wrtmp);
	if (vn_start_write(NULL, &wrtmp, V_NOWAIT) != 0) {
		NDFREE(&nd, NDF_ONLY_PNBUF);
		vput(nd.ni_dvp);
		if ((error = vn_start_write(NULL, &wrtmp,
		    V_XSLEEP | PCATCH)) != 0)
			return (error);
		goto restart;
	}
	error = VOP_CREATE(nd.ni_dvp, &nd.ni_vp, &nd.ni_cnd, &vat);
	VOP_UNLOCK(nd.ni_dvp, 0);
	if (error) {
		NDFREE(&nd, NDF_ONLY_PNBUF);
		vn_finished_write(wrtmp);
		vrele(nd.ni_dvp);
		return (error);
	}
	vp = nd.ni_vp;
	vnode_create_vobject(nd.ni_vp, fs->fs_size, td);
	vp->v_vflag |= VV_SYSTEM;
	ip = VTOI(vp);
	devvp = ITODEVVP(ip);
	/*
	 * Allocate and copy the last block contents so as to be able
	 * to set size to that of the filesystem.
	 */
	numblks = howmany(fs->fs_size, fs->fs_frag);
	error = UFS_BALLOC(vp, lblktosize(fs, (off_t)(numblks - 1)),
	    fs->fs_bsize, KERNCRED, BA_CLRBUF, &bp);
	if (error)
		goto out;
	ip->i_size = lblktosize(fs, (off_t)numblks);
	DIP_SET(ip, i_size, ip->i_size);
	ip->i_flag |= IN_CHANGE | IN_UPDATE;
	error = readblock(vp, bp, numblks - 1);
	bawrite(bp);
	if (error != 0)
		goto out;
	/*
	 * Preallocate critical data structures so that we can copy
	 * them in without further allocation after we suspend all
	 * operations on the filesystem. We would like to just release
	 * the allocated buffers without writing them since they will
	 * be filled in below once we are ready to go, but this upsets
	 * the soft update code, so we go ahead and write the new buffers.
	 *
	 * Allocate all indirect blocks and mark all of them as not
	 * needing to be copied.
	 */
	for (blkno = UFS_NDADDR; blkno < numblks; blkno += NINDIR(fs)) {
		error = UFS_BALLOC(vp, lblktosize(fs, (off_t)blkno),
		    fs->fs_bsize, td->td_ucred, BA_METAONLY, &ibp);
		if (error)
			goto out;
		bawrite(ibp);
	}
	/*
	 * Allocate copies for the superblock and its summary information.
	 */
	error = UFS_BALLOC(vp, fs->fs_sblockloc, fs->fs_sbsize, KERNCRED,
	    0, &nbp);
	if (error)
		goto out;
	bawrite(nbp);
	blkno = fragstoblks(fs, fs->fs_csaddr);
	len = howmany(fs->fs_cssize, fs->fs_bsize);
	for (loc = 0; loc < len; loc++) {
		error = UFS_BALLOC(vp, lblktosize(fs, (off_t)(blkno + loc)),
		    fs->fs_bsize, KERNCRED, 0, &nbp);
		if (error)
			goto out;
		bawrite(nbp);
	}
	/*
	 * Allocate all cylinder group blocks.
	 */
	for (cg = 0; cg < fs->fs_ncg; cg++) {
		error = UFS_BALLOC(vp, lfragtosize(fs, cgtod(fs, cg)),
		    fs->fs_bsize, KERNCRED, 0, &nbp);
		if (error)
			goto out;
		bawrite(nbp);
		if (cg % 10 == 0)
			ffs_syncvnode(vp, MNT_WAIT, 0);
	}
	/*
	 * Copy all the cylinder group maps. Although the
	 * filesystem is still active, we hope that only a few
	 * cylinder groups will change between now and when we
	 * suspend operations. Thus, we will be able to quickly
	 * touch up the few cylinder groups that changed during
	 * the suspension period.
	 */
	len = howmany(fs->fs_ncg, NBBY);
	space = malloc(len, M_DEVBUF, M_WAITOK|M_ZERO);
	UFS_LOCK(ump);
	fs->fs_active = space;
	UFS_UNLOCK(ump);
	for (cg = 0; cg < fs->fs_ncg; cg++) {
		error = UFS_BALLOC(vp, lfragtosize(fs, cgtod(fs, cg)),
		    fs->fs_bsize, KERNCRED, 0, &nbp);
		if (error)
			goto out;
		error = cgaccount(cg, vp, nbp, 1);
		bawrite(nbp);
		if (cg % 10 == 0)
			ffs_syncvnode(vp, MNT_WAIT, 0);
		if (error)
			goto out;
	}
	/*
	 * Change inode to snapshot type file.
	 */
	ip->i_flags |= SF_SNAPSHOT;
	DIP_SET(ip, i_flags, ip->i_flags);
	ip->i_flag |= IN_CHANGE | IN_UPDATE;
	/*
	 * Ensure that the snapshot is completely on disk.
	 * Since we have marked it as a snapshot it is safe to
	 * unlock it as no process will be allowed to write to it.
	 */
	if ((error = ffs_syncvnode(vp, MNT_WAIT, 0)) != 0)
		goto out;
	VOP_UNLOCK(vp, 0);
	/*
	 * All allocations are done, so we can now snapshot the system.
	 *
	 * Recind nice scheduling while running with the filesystem suspended.
	 */
	if (td->td_proc->p_nice > 0) {
		struct proc *p;

		p = td->td_proc;
		PROC_LOCK(p);
		saved_nice = p->p_nice;
		sched_nice(p, 0);
		PROC_UNLOCK(p);
	}
	/*
	 * Suspend operation on filesystem.
	 */
	for (;;) {
		vn_finished_write(wrtmp);
		if ((error = vfs_write_suspend(vp->v_mount, 0)) != 0) {
			vn_start_write(NULL, &wrtmp, V_WAIT);
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
			goto out;
		}
		if (mp->mnt_kern_flag & MNTK_SUSPENDED)
			break;
		vn_start_write(NULL, &wrtmp, V_WAIT);
	}
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (ip->i_effnlink == 0) {
		error = ENOENT;		/* Snapshot file unlinked */
		goto out1;
	}
	if (collectsnapstats)
		nanotime(&starttime);

	/* The last block might have changed.  Copy it again to be sure. */
	error = UFS_BALLOC(vp, lblktosize(fs, (off_t)(numblks - 1)),
	    fs->fs_bsize, KERNCRED, BA_CLRBUF, &bp);
	if (error != 0)
		goto out1;
	error = readblock(vp, bp, numblks - 1);
	bp->b_flags |= B_VALIDSUSPWRT;
	bawrite(bp);
	if (error != 0)
		goto out1;
	/*
	 * First, copy all the cylinder group maps that have changed.
	 */
	for (cg = 0; cg < fs->fs_ncg; cg++) {
		if ((ACTIVECGNUM(fs, cg) & ACTIVECGOFF(cg)) != 0)
			continue;
		redo++;
		error = UFS_BALLOC(vp, lfragtosize(fs, cgtod(fs, cg)),
		    fs->fs_bsize, KERNCRED, 0, &nbp);
		if (error)
			goto out1;
		error = cgaccount(cg, vp, nbp, 2);
		bawrite(nbp);
		if (error)
			goto out1;
	}
	/*
	 * Grab a copy of the superblock and its summary information.
	 * We delay writing it until the suspension is released below.
	 */
	copy_fs = malloc((u_long)fs->fs_bsize, M_UFSMNT, M_WAITOK);
	bcopy(fs, copy_fs, fs->fs_sbsize);
	if ((fs->fs_flags & (FS_UNCLEAN | FS_NEEDSFSCK)) == 0)
		copy_fs->fs_clean = 1;
	size = fs->fs_bsize < SBLOCKSIZE ? fs->fs_bsize : SBLOCKSIZE;
	if (fs->fs_sbsize < size)
		bzero(&((char *)copy_fs)[fs->fs_sbsize],
		    size - fs->fs_sbsize);
	size = blkroundup(fs, fs->fs_cssize);
	if (fs->fs_contigsumsize > 0)
		size += fs->fs_ncg * sizeof(int32_t);
	space = malloc((u_long)size, M_UFSMNT, M_WAITOK);
	copy_fs->fs_csp = space;
	bcopy(fs->fs_csp, copy_fs->fs_csp, fs->fs_cssize);
	space = (char *)space + fs->fs_cssize;
	loc = howmany(fs->fs_cssize, fs->fs_fsize);
	i = fs->fs_frag - loc % fs->fs_frag;
	len = (i == fs->fs_frag) ? 0 : i * fs->fs_fsize;
	if (len > 0) {
		if ((error = bread(devvp, fsbtodb(fs, fs->fs_csaddr + loc),
		    len, KERNCRED, &bp)) != 0) {
			brelse(bp);
			free(copy_fs->fs_csp, M_UFSMNT);
			free(copy_fs, M_UFSMNT);
			copy_fs = NULL;
			goto out1;
		}
		bcopy(bp->b_data, space, (u_int)len);
		space = (char *)space + len;
		bp->b_flags |= B_INVAL | B_NOCACHE;
		brelse(bp);
	}
	if (fs->fs_contigsumsize > 0) {
		copy_fs->fs_maxcluster = lp = space;
		for (i = 0; i < fs->fs_ncg; i++)
			*lp++ = fs->fs_contigsumsize;
	}
	/*
	 * We must check for active files that have been unlinked
	 * (e.g., with a zero link count). We have to expunge all
	 * trace of these files from the snapshot so that they are
	 * not reclaimed prematurely by fsck or unnecessarily dumped.
	 * We turn off the MNTK_SUSPENDED flag to avoid a panic from
	 * spec_strategy about writing on a suspended filesystem.
	 * Note that we skip unlinked snapshot files as they will
	 * be handled separately below.
	 *
	 * We also calculate the needed size for the snapshot list.
	 */
	snaplistsize = fs->fs_ncg + howmany(fs->fs_cssize, fs->fs_bsize) +
	    FSMAXSNAP + 1 /* superblock */ + 1 /* last block */ + 1 /* size */;
	MNT_ILOCK(mp);
	mp->mnt_kern_flag &= ~MNTK_SUSPENDED;
	MNT_IUNLOCK(mp);
loop:
	MNT_VNODE_FOREACH_ALL(xvp, mp, mvp) {
		if ((xvp->v_usecount == 0 &&
		     (xvp->v_iflag & (VI_OWEINACT | VI_DOINGINACT)) == 0) ||
		    xvp->v_type == VNON ||
		    IS_SNAPSHOT(VTOI(xvp))) {
			VI_UNLOCK(xvp);
			continue;
		}
		/*
		 * We can skip parent directory vnode because it must have
		 * this snapshot file in it.
		 */
		if (xvp == nd.ni_dvp) {
			VI_UNLOCK(xvp);
			continue;
		}
		vholdl(xvp);
		if (vn_lock(xvp, LK_EXCLUSIVE | LK_INTERLOCK) != 0) {
			MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
			vdrop(xvp);
			goto loop;
		}
		VI_LOCK(xvp);
		if (xvp->v_usecount == 0 &&
		    (xvp->v_iflag & (VI_OWEINACT | VI_DOINGINACT)) == 0) {
			VI_UNLOCK(xvp);
			VOP_UNLOCK(xvp, 0);
			vdrop(xvp);
			continue;
		}
		VI_UNLOCK(xvp);
		if (snapdebug)
			vn_printf(xvp, "ffs_snapshot: busy vnode ");
		if (VOP_GETATTR(xvp, &vat, td->td_ucred) == 0 &&
		    vat.va_nlink > 0) {
			VOP_UNLOCK(xvp, 0);
			vdrop(xvp);
			continue;
		}
		xp = VTOI(xvp);
		if (ffs_checkfreefile(copy_fs, vp, xp->i_number)) {
			VOP_UNLOCK(xvp, 0);
			vdrop(xvp);
			continue;
		}
		/*
		 * If there is a fragment, clear it here.
		 */
		blkno = 0;
		loc = howmany(xp->i_size, fs->fs_bsize) - 1;
		if (loc < UFS_NDADDR) {
			len = fragroundup(fs, blkoff(fs, xp->i_size));
			if (len != 0 && len < fs->fs_bsize) {
				ffs_blkfree(ump, copy_fs, vp,
				    DIP(xp, i_db[loc]), len, xp->i_number,
				    xvp->v_type, NULL, SINGLETON_KEY);
				blkno = DIP(xp, i_db[loc]);
				DIP_SET(xp, i_db[loc], 0);
			}
		}
		snaplistsize += 1;
		if (I_IS_UFS1(xp))
			error = expunge_ufs1(vp, xp, copy_fs, fullacct_ufs1,
			    BLK_NOCOPY, 1);
		else
			error = expunge_ufs2(vp, xp, copy_fs, fullacct_ufs2,
			    BLK_NOCOPY, 1);
		if (blkno)
			DIP_SET(xp, i_db[loc], blkno);
		if (!error)
			error = ffs_freefile(ump, copy_fs, vp, xp->i_number,
			    xp->i_mode, NULL);
		VOP_UNLOCK(xvp, 0);
		vdrop(xvp);
		if (error) {
			free(copy_fs->fs_csp, M_UFSMNT);
			free(copy_fs, M_UFSMNT);
			copy_fs = NULL;
			MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
			goto out1;
		}
	}
	/*
	 * Erase the journal file from the snapshot.
	 */
	if (fs->fs_flags & FS_SUJ) {
		error = softdep_journal_lookup(mp, &xvp);
		if (error) {
			free(copy_fs->fs_csp, M_UFSMNT);
			free(copy_fs, M_UFSMNT);
			copy_fs = NULL;
			goto out1;
		}
		xp = VTOI(xvp);
		if (I_IS_UFS1(xp))
			error = expunge_ufs1(vp, xp, copy_fs, fullacct_ufs1,
			    BLK_NOCOPY, 0);
		else
			error = expunge_ufs2(vp, xp, copy_fs, fullacct_ufs2,
			    BLK_NOCOPY, 0);
		vput(xvp);
	}
	/*
	 * Acquire a lock on the snapdata structure, creating it if necessary.
	 */
	sn = ffs_snapdata_acquire(devvp);
	/* 
	 * Change vnode to use shared snapshot lock instead of the original
	 * private lock.
	 */
	vp->v_vnlock = &sn->sn_lock;
	lockmgr(&vp->v_lock, LK_RELEASE, NULL);
	xp = TAILQ_FIRST(&sn->sn_head);
	/*
	 * If this is the first snapshot on this filesystem, then we need
	 * to allocate the space for the list of preallocated snapshot blocks.
	 * This list will be refined below, but this preliminary one will
	 * keep us out of deadlock until the full one is ready.
	 */
	if (xp == NULL) {
		snapblklist = malloc(snaplistsize * sizeof(daddr_t),
		    M_UFSMNT, M_WAITOK);
		blkp = &snapblklist[1];
		*blkp++ = lblkno(fs, fs->fs_sblockloc);
		blkno = fragstoblks(fs, fs->fs_csaddr);
		for (cg = 0; cg < fs->fs_ncg; cg++) {
			if (fragstoblks(fs, cgtod(fs, cg) > blkno))
				break;
			*blkp++ = fragstoblks(fs, cgtod(fs, cg));
		}
		len = howmany(fs->fs_cssize, fs->fs_bsize);
		for (loc = 0; loc < len; loc++)
			*blkp++ = blkno + loc;
		for (; cg < fs->fs_ncg; cg++)
			*blkp++ = fragstoblks(fs, cgtod(fs, cg));
		snapblklist[0] = blkp - snapblklist;
		VI_LOCK(devvp);
		if (sn->sn_blklist != NULL)
			panic("ffs_snapshot: non-empty list");
		sn->sn_blklist = snapblklist;
		sn->sn_listsize = blkp - snapblklist;
		VI_UNLOCK(devvp);
	}
	/*
	 * Record snapshot inode. Since this is the newest snapshot,
	 * it must be placed at the end of the list.
	 */
	VI_LOCK(devvp);
	fs->fs_snapinum[snaploc] = ip->i_number;
	if (ip->i_nextsnap.tqe_prev != 0)
		panic("ffs_snapshot: %ju already on list",
		    (uintmax_t)ip->i_number);
	TAILQ_INSERT_TAIL(&sn->sn_head, ip, i_nextsnap);
	devvp->v_vflag |= VV_COPYONWRITE;
	VI_UNLOCK(devvp);
	ASSERT_VOP_LOCKED(vp, "ffs_snapshot vp");
out1:
	KASSERT((sn != NULL && copy_fs != NULL && error == 0) ||
		(sn == NULL && copy_fs == NULL && error != 0),
		("email phk@ and mckusick@"));
	/*
	 * Resume operation on filesystem.
	 */
	vfs_write_resume(vp->v_mount, VR_START_WRITE | VR_NO_SUSPCLR);
	if (collectsnapstats && starttime.tv_sec > 0) {
		nanotime(&endtime);
		timespecsub(&endtime, &starttime, &endtime);
		printf("%s: suspended %ld.%03ld sec, redo %ld of %d\n",
		    vp->v_mount->mnt_stat.f_mntonname, (long)endtime.tv_sec,
		    endtime.tv_nsec / 1000000, redo, fs->fs_ncg);
	}
	if (copy_fs == NULL)
		goto out;
	/*
	 * Copy allocation information from all the snapshots in
	 * this snapshot and then expunge them from its view.
	 */
	TAILQ_FOREACH(xp, &sn->sn_head, i_nextsnap) {
		if (xp == ip)
			break;
		if (I_IS_UFS1(xp))
			error = expunge_ufs1(vp, xp, fs, snapacct_ufs1,
			    BLK_SNAP, 0);
		else
			error = expunge_ufs2(vp, xp, fs, snapacct_ufs2,
			    BLK_SNAP, 0);
		if (error == 0 && xp->i_effnlink == 0) {
			error = ffs_freefile(ump,
					     copy_fs,
					     vp,
					     xp->i_number,
					     xp->i_mode, NULL);
		}
		if (error) {
			fs->fs_snapinum[snaploc] = 0;
			goto done;
		}
	}
	/*
	 * Allocate space for the full list of preallocated snapshot blocks.
	 */
	snapblklist = malloc(snaplistsize * sizeof(daddr_t),
	    M_UFSMNT, M_WAITOK);
	ip->i_snapblklist = &snapblklist[1];
	/*
	 * Expunge the blocks used by the snapshots from the set of
	 * blocks marked as used in the snapshot bitmaps. Also, collect
	 * the list of allocated blocks in i_snapblklist.
	 */
	if (I_IS_UFS1(ip))
		error = expunge_ufs1(vp, ip, copy_fs, mapacct_ufs1,
		    BLK_SNAP, 0);
	else
		error = expunge_ufs2(vp, ip, copy_fs, mapacct_ufs2,
		    BLK_SNAP, 0);
	if (error) {
		fs->fs_snapinum[snaploc] = 0;
		free(snapblklist, M_UFSMNT);
		goto done;
	}
	if (snaplistsize < ip->i_snapblklist - snapblklist)
		panic("ffs_snapshot: list too small");
	snaplistsize = ip->i_snapblklist - snapblklist;
	snapblklist[0] = snaplistsize;
	ip->i_snapblklist = 0;
	/*
	 * Write out the list of allocated blocks to the end of the snapshot.
	 */
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	aiov.iov_base = (void *)snapblklist;
	aiov.iov_len = snaplistsize * sizeof(daddr_t);
	auio.uio_resid = aiov.iov_len;
	auio.uio_offset = ip->i_size;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_td = td;
	if ((error = VOP_WRITE(vp, &auio, IO_UNIT, td->td_ucred)) != 0) {
		fs->fs_snapinum[snaploc] = 0;
		free(snapblklist, M_UFSMNT);
		goto done;
	}
	/*
	 * Write the superblock and its summary information
	 * to the snapshot.
	 */
	blkno = fragstoblks(fs, fs->fs_csaddr);
	len = howmany(fs->fs_cssize, fs->fs_bsize);
	space = copy_fs->fs_csp;
	for (loc = 0; loc < len; loc++) {
		error = bread(vp, blkno + loc, fs->fs_bsize, KERNCRED, &nbp);
		if (error) {
			brelse(nbp);
			fs->fs_snapinum[snaploc] = 0;
			free(snapblklist, M_UFSMNT);
			goto done;
		}
		bcopy(space, nbp->b_data, fs->fs_bsize);
		space = (char *)space + fs->fs_bsize;
		bawrite(nbp);
	}
	error = bread(vp, lblkno(fs, fs->fs_sblockloc), fs->fs_bsize,
	    KERNCRED, &nbp);
	if (error) {
		brelse(nbp);
	} else {
		loc = blkoff(fs, fs->fs_sblockloc);
		copy_fs->fs_ckhash = ffs_calc_sbhash(copy_fs);
		bcopy((char *)copy_fs, &nbp->b_data[loc], (u_int)fs->fs_sbsize);
		bawrite(nbp);
	}
	/*
	 * As this is the newest list, it is the most inclusive, so
	 * should replace the previous list.
	 */
	VI_LOCK(devvp);
	space = sn->sn_blklist;
	sn->sn_blklist = snapblklist;
	sn->sn_listsize = snaplistsize;
	VI_UNLOCK(devvp);
	if (space != NULL)
		free(space, M_UFSMNT);
	/*
	 * Preallocate all the direct blocks in the snapshot inode so
	 * that we never have to write the inode itself to commit an
	 * update to the contents of the snapshot. Note that once
	 * created, the size of the snapshot will never change, so
	 * there will never be a need to write the inode except to
	 * update the non-integrity-critical time fields and
	 * allocated-block count.
	 */
	for (blockno = 0; blockno < UFS_NDADDR; blockno++) {
		if (DIP(ip, i_db[blockno]) != 0)
			continue;
		error = UFS_BALLOC(vp, lblktosize(fs, blockno),
		    fs->fs_bsize, KERNCRED, BA_CLRBUF, &bp);
		if (error)
			break;
		error = readblock(vp, bp, blockno);
		bawrite(bp);
		if (error != 0)
			break;
	}
done:
	free(copy_fs->fs_csp, M_UFSMNT);
	free(copy_fs, M_UFSMNT);
	copy_fs = NULL;
out:
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (saved_nice > 0) {
		struct proc *p;

		p = td->td_proc;
		PROC_LOCK(p);
		sched_nice(td->td_proc, saved_nice);
		PROC_UNLOCK(td->td_proc);
	}
	UFS_LOCK(ump);
	if (fs->fs_active != 0) {
		free(fs->fs_active, M_DEVBUF);
		fs->fs_active = 0;
	}
	UFS_UNLOCK(ump);
	MNT_ILOCK(mp);
	mp->mnt_flag = (mp->mnt_flag & MNT_QUOTA) | (flag & ~MNT_QUOTA);
	MNT_IUNLOCK(mp);
	if (error)
		(void) ffs_truncate(vp, (off_t)0, 0, NOCRED);
	(void) ffs_syncvnode(vp, MNT_WAIT, 0);
	if (error)
		vput(vp);
	else
		VOP_UNLOCK(vp, 0);
	vrele(nd.ni_dvp);
	vn_finished_write(wrtmp);
	process_deferred_inactive(mp);
	return (error);
}

/*
 * Copy a cylinder group map. All the unallocated blocks are marked
 * BLK_NOCOPY so that the snapshot knows that it need not copy them
 * if they are later written. If passno is one, then this is a first
 * pass, so only setting needs to be done. If passno is 2, then this
 * is a revision to a previous pass which must be undone as the
 * replacement pass is done.
 */
static int
cgaccount(cg, vp, nbp, passno)
	int cg;
	struct vnode *vp;
	struct buf *nbp;
	int passno;
{
	struct buf *bp, *ibp;
	struct inode *ip;
	struct cg *cgp;
	struct fs *fs;
	ufs2_daddr_t base, numblks;
	int error, len, loc, indiroff;

	ip = VTOI(vp);
	fs = ITOFS(ip);
	if ((error = ffs_getcg(fs, ITODEVVP(ip), cg, &bp, &cgp)) != 0)
		return (error);
	UFS_LOCK(ITOUMP(ip));
	ACTIVESET(fs, cg);
	/*
	 * Recomputation of summary information might not have been performed
	 * at mount time.  Sync up summary information for current cylinder
	 * group while data is in memory to ensure that result of background
	 * fsck is slightly more consistent.
	 */
	fs->fs_cs(fs, cg) = cgp->cg_cs;
	UFS_UNLOCK(ITOUMP(ip));
	bcopy(bp->b_data, nbp->b_data, fs->fs_cgsize);
	if (fs->fs_cgsize < fs->fs_bsize)
		bzero(&nbp->b_data[fs->fs_cgsize],
		    fs->fs_bsize - fs->fs_cgsize);
	cgp = (struct cg *)nbp->b_data;
	bqrelse(bp);
	if (passno == 2)
		nbp->b_flags |= B_VALIDSUSPWRT;
	numblks = howmany(fs->fs_size, fs->fs_frag);
	len = howmany(fs->fs_fpg, fs->fs_frag);
	base = cgbase(fs, cg) / fs->fs_frag;
	if (base + len >= numblks)
		len = numblks - base - 1;
	loc = 0;
	if (base < UFS_NDADDR) {
		for ( ; loc < UFS_NDADDR; loc++) {
			if (ffs_isblock(fs, cg_blksfree(cgp), loc))
				DIP_SET(ip, i_db[loc], BLK_NOCOPY);
			else if (passno == 2 && DIP(ip, i_db[loc])== BLK_NOCOPY)
				DIP_SET(ip, i_db[loc], 0);
			else if (passno == 1 && DIP(ip, i_db[loc])== BLK_NOCOPY)
				panic("ffs_snapshot: lost direct block");
		}
	}
	error = UFS_BALLOC(vp, lblktosize(fs, (off_t)(base + loc)),
	    fs->fs_bsize, KERNCRED, BA_METAONLY, &ibp);
	if (error) {
		goto out;
	}
	indiroff = (base + loc - UFS_NDADDR) % NINDIR(fs);
	for ( ; loc < len; loc++, indiroff++) {
		if (indiroff >= NINDIR(fs)) {
			if (passno == 2)
				ibp->b_flags |= B_VALIDSUSPWRT;
			bawrite(ibp);
			error = UFS_BALLOC(vp,
			    lblktosize(fs, (off_t)(base + loc)),
			    fs->fs_bsize, KERNCRED, BA_METAONLY, &ibp);
			if (error) {
				goto out;
			}
			indiroff = 0;
		}
		if (I_IS_UFS1(ip)) {
			if (ffs_isblock(fs, cg_blksfree(cgp), loc))
				((ufs1_daddr_t *)(ibp->b_data))[indiroff] =
				    BLK_NOCOPY;
			else if (passno == 2 && ((ufs1_daddr_t *)(ibp->b_data))
			    [indiroff] == BLK_NOCOPY)
				((ufs1_daddr_t *)(ibp->b_data))[indiroff] = 0;
			else if (passno == 1 && ((ufs1_daddr_t *)(ibp->b_data))
			    [indiroff] == BLK_NOCOPY)
				panic("ffs_snapshot: lost indirect block");
			continue;
		}
		if (ffs_isblock(fs, cg_blksfree(cgp), loc))
			((ufs2_daddr_t *)(ibp->b_data))[indiroff] = BLK_NOCOPY;
		else if (passno == 2 &&
		    ((ufs2_daddr_t *)(ibp->b_data)) [indiroff] == BLK_NOCOPY)
			((ufs2_daddr_t *)(ibp->b_data))[indiroff] = 0;
		else if (passno == 1 &&
		    ((ufs2_daddr_t *)(ibp->b_data)) [indiroff] == BLK_NOCOPY)
			panic("ffs_snapshot: lost indirect block");
	}
	if (passno == 2)
		ibp->b_flags |= B_VALIDSUSPWRT;
	bdwrite(ibp);
out:
	/*
	 * We have to calculate the crc32c here rather than just setting the
	 * BX_CYLGRP b_xflags because the allocation of the block for the
	 * the cylinder group map will always be a full size block (fs_bsize)
	 * even though the cylinder group may be smaller (fs_cgsize). The
	 * crc32c must be computed only over fs_cgsize whereas the BX_CYLGRP
	 * flag causes it to be computed over the size of the buffer.
	 */
	if ((fs->fs_metackhash & CK_CYLGRP) != 0) {
		((struct cg *)nbp->b_data)->cg_ckhash = 0;
		((struct cg *)nbp->b_data)->cg_ckhash =
		    calculate_crc32c(~0L, nbp->b_data, fs->fs_cgsize);
	}
	return (error);
}

/*
 * Before expunging a snapshot inode, note all the
 * blocks that it claims with BLK_SNAP so that fsck will
 * be able to account for those blocks properly and so
 * that this snapshot knows that it need not copy them
 * if the other snapshot holding them is freed. This code
 * is reproduced once each for UFS1 and UFS2.
 */
static int
expunge_ufs1(snapvp, cancelip, fs, acctfunc, expungetype, clearmode)
	struct vnode *snapvp;
	struct inode *cancelip;
	struct fs *fs;
	int (*acctfunc)(struct vnode *, ufs1_daddr_t *, ufs1_daddr_t *,
	    struct fs *, ufs_lbn_t, int);
	int expungetype;
	int clearmode;
{
	int i, error, indiroff;
	ufs_lbn_t lbn, rlbn;
	ufs2_daddr_t len, blkno, numblks, blksperindir;
	struct ufs1_dinode *dip;
	struct thread *td = curthread;
	struct buf *bp;

	/*
	 * Prepare to expunge the inode. If its inode block has not
	 * yet been copied, then allocate and fill the copy.
	 */
	lbn = fragstoblks(fs, ino_to_fsba(fs, cancelip->i_number));
	blkno = 0;
	if (lbn < UFS_NDADDR) {
		blkno = VTOI(snapvp)->i_din1->di_db[lbn];
	} else {
		if (DOINGSOFTDEP(snapvp))
			softdep_prealloc(snapvp, MNT_WAIT);
		td->td_pflags |= TDP_COWINPROGRESS;
		error = ffs_balloc_ufs1(snapvp, lblktosize(fs, (off_t)lbn),
		   fs->fs_bsize, KERNCRED, BA_METAONLY, &bp);
		td->td_pflags &= ~TDP_COWINPROGRESS;
		if (error)
			return (error);
		indiroff = (lbn - UFS_NDADDR) % NINDIR(fs);
		blkno = ((ufs1_daddr_t *)(bp->b_data))[indiroff];
		bqrelse(bp);
	}
	if (blkno != 0) {
		if ((error = bread(snapvp, lbn, fs->fs_bsize, KERNCRED, &bp)))
			return (error);
	} else {
		error = ffs_balloc_ufs1(snapvp, lblktosize(fs, (off_t)lbn),
		    fs->fs_bsize, KERNCRED, 0, &bp);
		if (error)
			return (error);
		if ((error = readblock(snapvp, bp, lbn)) != 0)
			return (error);
	}
	/*
	 * Set a snapshot inode to be a zero length file, regular files
	 * or unlinked snapshots to be completely unallocated.
	 */
	dip = (struct ufs1_dinode *)bp->b_data +
	    ino_to_fsbo(fs, cancelip->i_number);
	if (clearmode || cancelip->i_effnlink == 0)
		dip->di_mode = 0;
	dip->di_size = 0;
	dip->di_blocks = 0;
	dip->di_flags &= ~SF_SNAPSHOT;
	bzero(&dip->di_db[0], (UFS_NDADDR + UFS_NIADDR) * sizeof(ufs1_daddr_t));
	bdwrite(bp);
	/*
	 * Now go through and expunge all the blocks in the file
	 * using the function requested.
	 */
	numblks = howmany(cancelip->i_size, fs->fs_bsize);
	if ((error = (*acctfunc)(snapvp, &cancelip->i_din1->di_db[0],
	    &cancelip->i_din1->di_db[UFS_NDADDR], fs, 0, expungetype)))
		return (error);
	if ((error = (*acctfunc)(snapvp, &cancelip->i_din1->di_ib[0],
	    &cancelip->i_din1->di_ib[UFS_NIADDR], fs, -1, expungetype)))
		return (error);
	blksperindir = 1;
	lbn = -UFS_NDADDR;
	len = numblks - UFS_NDADDR;
	rlbn = UFS_NDADDR;
	for (i = 0; len > 0 && i < UFS_NIADDR; i++) {
		error = indiracct_ufs1(snapvp, ITOV(cancelip), i,
		    cancelip->i_din1->di_ib[i], lbn, rlbn, len,
		    blksperindir, fs, acctfunc, expungetype);
		if (error)
			return (error);
		blksperindir *= NINDIR(fs);
		lbn -= blksperindir + 1;
		len -= blksperindir;
		rlbn += blksperindir;
	}
	return (0);
}

/*
 * Descend an indirect block chain for vnode cancelvp accounting for all
 * its indirect blocks in snapvp.
 */ 
static int
indiracct_ufs1(snapvp, cancelvp, level, blkno, lbn, rlbn, remblks,
	    blksperindir, fs, acctfunc, expungetype)
	struct vnode *snapvp;
	struct vnode *cancelvp;
	int level;
	ufs1_daddr_t blkno;
	ufs_lbn_t lbn;
	ufs_lbn_t rlbn;
	ufs_lbn_t remblks;
	ufs_lbn_t blksperindir;
	struct fs *fs;
	int (*acctfunc)(struct vnode *, ufs1_daddr_t *, ufs1_daddr_t *,
	    struct fs *, ufs_lbn_t, int);
	int expungetype;
{
	int error, num, i;
	ufs_lbn_t subblksperindir;
	struct indir indirs[UFS_NIADDR + 2];
	ufs1_daddr_t last, *bap;
	struct buf *bp;

	if (blkno == 0) {
		if (expungetype == BLK_NOCOPY)
			return (0);
		panic("indiracct_ufs1: missing indir");
	}
	if ((error = ufs_getlbns(cancelvp, rlbn, indirs, &num)) != 0)
		return (error);
	if (lbn != indirs[num - 1 - level].in_lbn || num < 2)
		panic("indiracct_ufs1: botched params");
	/*
	 * We have to expand bread here since it will deadlock looking
	 * up the block number for any blocks that are not in the cache.
	 */
	bp = getblk(cancelvp, lbn, fs->fs_bsize, 0, 0, 0);
	bp->b_blkno = fsbtodb(fs, blkno);
	if ((bp->b_flags & (B_DONE | B_DELWRI)) == 0 &&
	    (error = readblock(cancelvp, bp, fragstoblks(fs, blkno)))) {
		brelse(bp);
		return (error);
	}
	/*
	 * Account for the block pointers in this indirect block.
	 */
	last = howmany(remblks, blksperindir);
	if (last > NINDIR(fs))
		last = NINDIR(fs);
	bap = malloc(fs->fs_bsize, M_DEVBUF, M_WAITOK);
	bcopy(bp->b_data, (caddr_t)bap, fs->fs_bsize);
	bqrelse(bp);
	error = (*acctfunc)(snapvp, &bap[0], &bap[last], fs,
	    level == 0 ? rlbn : -1, expungetype);
	if (error || level == 0)
		goto out;
	/*
	 * Account for the block pointers in each of the indirect blocks
	 * in the levels below us.
	 */
	subblksperindir = blksperindir / NINDIR(fs);
	for (lbn++, level--, i = 0; i < last; i++) {
		error = indiracct_ufs1(snapvp, cancelvp, level, bap[i], lbn,
		    rlbn, remblks, subblksperindir, fs, acctfunc, expungetype);
		if (error)
			goto out;
		rlbn += blksperindir;
		lbn -= blksperindir;
		remblks -= blksperindir;
	}
out:
	free(bap, M_DEVBUF);
	return (error);
}

/*
 * Do both snap accounting and map accounting.
 */
static int
fullacct_ufs1(vp, oldblkp, lastblkp, fs, lblkno, exptype)
	struct vnode *vp;
	ufs1_daddr_t *oldblkp, *lastblkp;
	struct fs *fs;
	ufs_lbn_t lblkno;
	int exptype;	/* BLK_SNAP or BLK_NOCOPY */
{
	int error;

	if ((error = snapacct_ufs1(vp, oldblkp, lastblkp, fs, lblkno, exptype)))
		return (error);
	return (mapacct_ufs1(vp, oldblkp, lastblkp, fs, lblkno, exptype));
}

/*
 * Identify a set of blocks allocated in a snapshot inode.
 */
static int
snapacct_ufs1(vp, oldblkp, lastblkp, fs, lblkno, expungetype)
	struct vnode *vp;
	ufs1_daddr_t *oldblkp, *lastblkp;
	struct fs *fs;
	ufs_lbn_t lblkno;
	int expungetype;	/* BLK_SNAP or BLK_NOCOPY */
{
	struct inode *ip = VTOI(vp);
	ufs1_daddr_t blkno, *blkp;
	ufs_lbn_t lbn;
	struct buf *ibp;
	int error;

	for ( ; oldblkp < lastblkp; oldblkp++) {
		blkno = *oldblkp;
		if (blkno == 0 || blkno == BLK_NOCOPY || blkno == BLK_SNAP)
			continue;
		lbn = fragstoblks(fs, blkno);
		if (lbn < UFS_NDADDR) {
			blkp = &ip->i_din1->di_db[lbn];
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
		} else {
			error = ffs_balloc_ufs1(vp, lblktosize(fs, (off_t)lbn),
			    fs->fs_bsize, KERNCRED, BA_METAONLY, &ibp);
			if (error)
				return (error);
			blkp = &((ufs1_daddr_t *)(ibp->b_data))
			    [(lbn - UFS_NDADDR) % NINDIR(fs)];
		}
		/*
		 * If we are expunging a snapshot vnode and we
		 * find a block marked BLK_NOCOPY, then it is
		 * one that has been allocated to this snapshot after
		 * we took our current snapshot and can be ignored.
		 */
		if (expungetype == BLK_SNAP && *blkp == BLK_NOCOPY) {
			if (lbn >= UFS_NDADDR)
				brelse(ibp);
		} else {
			if (*blkp != 0)
				panic("snapacct_ufs1: bad block");
			*blkp = expungetype;
			if (lbn >= UFS_NDADDR)
				bdwrite(ibp);
		}
	}
	return (0);
}

/*
 * Account for a set of blocks allocated in a snapshot inode.
 */
static int
mapacct_ufs1(vp, oldblkp, lastblkp, fs, lblkno, expungetype)
	struct vnode *vp;
	ufs1_daddr_t *oldblkp, *lastblkp;
	struct fs *fs;
	ufs_lbn_t lblkno;
	int expungetype;
{
	ufs1_daddr_t blkno;
	struct inode *ip;
	ino_t inum;
	int acctit;

	ip = VTOI(vp);
	inum = ip->i_number;
	if (lblkno == -1)
		acctit = 0;
	else
		acctit = 1;
	for ( ; oldblkp < lastblkp; oldblkp++, lblkno++) {
		blkno = *oldblkp;
		if (blkno == 0 || blkno == BLK_NOCOPY)
			continue;
		if (acctit && expungetype == BLK_SNAP && blkno != BLK_SNAP)
			*ip->i_snapblklist++ = lblkno;
		if (blkno == BLK_SNAP)
			blkno = blkstofrags(fs, lblkno);
		ffs_blkfree(ITOUMP(ip), fs, vp, blkno, fs->fs_bsize, inum,
		    vp->v_type, NULL, SINGLETON_KEY);
	}
	return (0);
}

/*
 * Before expunging a snapshot inode, note all the
 * blocks that it claims with BLK_SNAP so that fsck will
 * be able to account for those blocks properly and so
 * that this snapshot knows that it need not copy them
 * if the other snapshot holding them is freed. This code
 * is reproduced once each for UFS1 and UFS2.
 */
static int
expunge_ufs2(snapvp, cancelip, fs, acctfunc, expungetype, clearmode)
	struct vnode *snapvp;
	struct inode *cancelip;
	struct fs *fs;
	int (*acctfunc)(struct vnode *, ufs2_daddr_t *, ufs2_daddr_t *,
	    struct fs *, ufs_lbn_t, int);
	int expungetype;
	int clearmode;
{
	int i, error, indiroff;
	ufs_lbn_t lbn, rlbn;
	ufs2_daddr_t len, blkno, numblks, blksperindir;
	struct ufs2_dinode *dip;
	struct thread *td = curthread;
	struct buf *bp;

	/*
	 * Prepare to expunge the inode. If its inode block has not
	 * yet been copied, then allocate and fill the copy.
	 */
	lbn = fragstoblks(fs, ino_to_fsba(fs, cancelip->i_number));
	blkno = 0;
	if (lbn < UFS_NDADDR) {
		blkno = VTOI(snapvp)->i_din2->di_db[lbn];
	} else {
		if (DOINGSOFTDEP(snapvp))
			softdep_prealloc(snapvp, MNT_WAIT);
		td->td_pflags |= TDP_COWINPROGRESS;
		error = ffs_balloc_ufs2(snapvp, lblktosize(fs, (off_t)lbn),
		   fs->fs_bsize, KERNCRED, BA_METAONLY, &bp);
		td->td_pflags &= ~TDP_COWINPROGRESS;
		if (error)
			return (error);
		indiroff = (lbn - UFS_NDADDR) % NINDIR(fs);
		blkno = ((ufs2_daddr_t *)(bp->b_data))[indiroff];
		bqrelse(bp);
	}
	if (blkno != 0) {
		if ((error = bread(snapvp, lbn, fs->fs_bsize, KERNCRED, &bp)))
			return (error);
	} else {
		error = ffs_balloc_ufs2(snapvp, lblktosize(fs, (off_t)lbn),
		    fs->fs_bsize, KERNCRED, 0, &bp);
		if (error)
			return (error);
		if ((error = readblock(snapvp, bp, lbn)) != 0)
			return (error);
	}
	/*
	 * Set a snapshot inode to be a zero length file, regular files
	 * to be completely unallocated.
	 */
	dip = (struct ufs2_dinode *)bp->b_data +
	    ino_to_fsbo(fs, cancelip->i_number);
	dip->di_size = 0;
	dip->di_blocks = 0;
	dip->di_flags &= ~SF_SNAPSHOT;
	bzero(&dip->di_db[0], (UFS_NDADDR + UFS_NIADDR) * sizeof(ufs2_daddr_t));
	if (clearmode || cancelip->i_effnlink == 0)
		dip->di_mode = 0;
	else
		ffs_update_dinode_ckhash(fs, dip);
	bdwrite(bp);
	/*
	 * Now go through and expunge all the blocks in the file
	 * using the function requested.
	 */
	numblks = howmany(cancelip->i_size, fs->fs_bsize);
	if ((error = (*acctfunc)(snapvp, &cancelip->i_din2->di_db[0],
	    &cancelip->i_din2->di_db[UFS_NDADDR], fs, 0, expungetype)))
		return (error);
	if ((error = (*acctfunc)(snapvp, &cancelip->i_din2->di_ib[0],
	    &cancelip->i_din2->di_ib[UFS_NIADDR], fs, -1, expungetype)))
		return (error);
	blksperindir = 1;
	lbn = -UFS_NDADDR;
	len = numblks - UFS_NDADDR;
	rlbn = UFS_NDADDR;
	for (i = 0; len > 0 && i < UFS_NIADDR; i++) {
		error = indiracct_ufs2(snapvp, ITOV(cancelip), i,
		    cancelip->i_din2->di_ib[i], lbn, rlbn, len,
		    blksperindir, fs, acctfunc, expungetype);
		if (error)
			return (error);
		blksperindir *= NINDIR(fs);
		lbn -= blksperindir + 1;
		len -= blksperindir;
		rlbn += blksperindir;
	}
	return (0);
}

/*
 * Descend an indirect block chain for vnode cancelvp accounting for all
 * its indirect blocks in snapvp.
 */ 
static int
indiracct_ufs2(snapvp, cancelvp, level, blkno, lbn, rlbn, remblks,
	    blksperindir, fs, acctfunc, expungetype)
	struct vnode *snapvp;
	struct vnode *cancelvp;
	int level;
	ufs2_daddr_t blkno;
	ufs_lbn_t lbn;
	ufs_lbn_t rlbn;
	ufs_lbn_t remblks;
	ufs_lbn_t blksperindir;
	struct fs *fs;
	int (*acctfunc)(struct vnode *, ufs2_daddr_t *, ufs2_daddr_t *,
	    struct fs *, ufs_lbn_t, int);
	int expungetype;
{
	int error, num, i;
	ufs_lbn_t subblksperindir;
	struct indir indirs[UFS_NIADDR + 2];
	ufs2_daddr_t last, *bap;
	struct buf *bp;

	if (blkno == 0) {
		if (expungetype == BLK_NOCOPY)
			return (0);
		panic("indiracct_ufs2: missing indir");
	}
	if ((error = ufs_getlbns(cancelvp, rlbn, indirs, &num)) != 0)
		return (error);
	if (lbn != indirs[num - 1 - level].in_lbn || num < 2)
		panic("indiracct_ufs2: botched params");
	/*
	 * We have to expand bread here since it will deadlock looking
	 * up the block number for any blocks that are not in the cache.
	 */
	bp = getblk(cancelvp, lbn, fs->fs_bsize, 0, 0, 0);
	bp->b_blkno = fsbtodb(fs, blkno);
	if ((bp->b_flags & B_CACHE) == 0 &&
	    (error = readblock(cancelvp, bp, fragstoblks(fs, blkno)))) {
		brelse(bp);
		return (error);
	}
	/*
	 * Account for the block pointers in this indirect block.
	 */
	last = howmany(remblks, blksperindir);
	if (last > NINDIR(fs))
		last = NINDIR(fs);
	bap = malloc(fs->fs_bsize, M_DEVBUF, M_WAITOK);
	bcopy(bp->b_data, (caddr_t)bap, fs->fs_bsize);
	bqrelse(bp);
	error = (*acctfunc)(snapvp, &bap[0], &bap[last], fs,
	    level == 0 ? rlbn : -1, expungetype);
	if (error || level == 0)
		goto out;
	/*
	 * Account for the block pointers in each of the indirect blocks
	 * in the levels below us.
	 */
	subblksperindir = blksperindir / NINDIR(fs);
	for (lbn++, level--, i = 0; i < last; i++) {
		error = indiracct_ufs2(snapvp, cancelvp, level, bap[i], lbn,
		    rlbn, remblks, subblksperindir, fs, acctfunc, expungetype);
		if (error)
			goto out;
		rlbn += blksperindir;
		lbn -= blksperindir;
		remblks -= blksperindir;
	}
out:
	free(bap, M_DEVBUF);
	return (error);
}

/*
 * Do both snap accounting and map accounting.
 */
static int
fullacct_ufs2(vp, oldblkp, lastblkp, fs, lblkno, exptype)
	struct vnode *vp;
	ufs2_daddr_t *oldblkp, *lastblkp;
	struct fs *fs;
	ufs_lbn_t lblkno;
	int exptype;	/* BLK_SNAP or BLK_NOCOPY */
{
	int error;

	if ((error = snapacct_ufs2(vp, oldblkp, lastblkp, fs, lblkno, exptype)))
		return (error);
	return (mapacct_ufs2(vp, oldblkp, lastblkp, fs, lblkno, exptype));
}

/*
 * Identify a set of blocks allocated in a snapshot inode.
 */
static int
snapacct_ufs2(vp, oldblkp, lastblkp, fs, lblkno, expungetype)
	struct vnode *vp;
	ufs2_daddr_t *oldblkp, *lastblkp;
	struct fs *fs;
	ufs_lbn_t lblkno;
	int expungetype;	/* BLK_SNAP or BLK_NOCOPY */
{
	struct inode *ip = VTOI(vp);
	ufs2_daddr_t blkno, *blkp;
	ufs_lbn_t lbn;
	struct buf *ibp;
	int error;

	for ( ; oldblkp < lastblkp; oldblkp++) {
		blkno = *oldblkp;
		if (blkno == 0 || blkno == BLK_NOCOPY || blkno == BLK_SNAP)
			continue;
		lbn = fragstoblks(fs, blkno);
		if (lbn < UFS_NDADDR) {
			blkp = &ip->i_din2->di_db[lbn];
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
		} else {
			error = ffs_balloc_ufs2(vp, lblktosize(fs, (off_t)lbn),
			    fs->fs_bsize, KERNCRED, BA_METAONLY, &ibp);
			if (error)
				return (error);
			blkp = &((ufs2_daddr_t *)(ibp->b_data))
			    [(lbn - UFS_NDADDR) % NINDIR(fs)];
		}
		/*
		 * If we are expunging a snapshot vnode and we
		 * find a block marked BLK_NOCOPY, then it is
		 * one that has been allocated to this snapshot after
		 * we took our current snapshot and can be ignored.
		 */
		if (expungetype == BLK_SNAP && *blkp == BLK_NOCOPY) {
			if (lbn >= UFS_NDADDR)
				brelse(ibp);
		} else {
			if (*blkp != 0)
				panic("snapacct_ufs2: bad block");
			*blkp = expungetype;
			if (lbn >= UFS_NDADDR)
				bdwrite(ibp);
		}
	}
	return (0);
}

/*
 * Account for a set of blocks allocated in a snapshot inode.
 */
static int
mapacct_ufs2(vp, oldblkp, lastblkp, fs, lblkno, expungetype)
	struct vnode *vp;
	ufs2_daddr_t *oldblkp, *lastblkp;
	struct fs *fs;
	ufs_lbn_t lblkno;
	int expungetype;
{
	ufs2_daddr_t blkno;
	struct inode *ip;
	ino_t inum;
	int acctit;

	ip = VTOI(vp);
	inum = ip->i_number;
	if (lblkno == -1)
		acctit = 0;
	else
		acctit = 1;
	for ( ; oldblkp < lastblkp; oldblkp++, lblkno++) {
		blkno = *oldblkp;
		if (blkno == 0 || blkno == BLK_NOCOPY)
			continue;
		if (acctit && expungetype == BLK_SNAP && blkno != BLK_SNAP)
			*ip->i_snapblklist++ = lblkno;
		if (blkno == BLK_SNAP)
			blkno = blkstofrags(fs, lblkno);
		ffs_blkfree(ITOUMP(ip), fs, vp, blkno, fs->fs_bsize, inum,
		    vp->v_type, NULL, SINGLETON_KEY);
	}
	return (0);
}

/*
 * Decrement extra reference on snapshot when last name is removed.
 * It will not be freed until the last open reference goes away.
 */
void
ffs_snapgone(ip)
	struct inode *ip;
{
	struct inode *xp;
	struct fs *fs;
	int snaploc;
	struct snapdata *sn;
	struct ufsmount *ump;

	/*
	 * Find snapshot in incore list.
	 */
	xp = NULL;
	sn = ITODEVVP(ip)->v_rdev->si_snapdata;
	if (sn != NULL)
		TAILQ_FOREACH(xp, &sn->sn_head, i_nextsnap)
			if (xp == ip)
				break;
	if (xp != NULL)
		vrele(ITOV(ip));
	else if (snapdebug)
		printf("ffs_snapgone: lost snapshot vnode %ju\n",
		    (uintmax_t)ip->i_number);
	/*
	 * Delete snapshot inode from superblock. Keep list dense.
	 */
	ump = ITOUMP(ip);
	fs = ump->um_fs;
	UFS_LOCK(ump);
	for (snaploc = 0; snaploc < FSMAXSNAP; snaploc++)
		if (fs->fs_snapinum[snaploc] == ip->i_number)
			break;
	if (snaploc < FSMAXSNAP) {
		for (snaploc++; snaploc < FSMAXSNAP; snaploc++) {
			if (fs->fs_snapinum[snaploc] == 0)
				break;
			fs->fs_snapinum[snaploc - 1] = fs->fs_snapinum[snaploc];
		}
		fs->fs_snapinum[snaploc - 1] = 0;
	}
	UFS_UNLOCK(ump);
}

/*
 * Prepare a snapshot file for being removed.
 */
void
ffs_snapremove(vp)
	struct vnode *vp;
{
	struct inode *ip;
	struct vnode *devvp;
	struct buf *ibp;
	struct fs *fs;
	ufs2_daddr_t numblks, blkno, dblk;
	int error, i, last, loc;
	struct snapdata *sn;

	ip = VTOI(vp);
	fs = ITOFS(ip);
	devvp = ITODEVVP(ip);
	/*
	 * If active, delete from incore list (this snapshot may
	 * already have been in the process of being deleted, so
	 * would not have been active).
	 *
	 * Clear copy-on-write flag if last snapshot.
	 */
	VI_LOCK(devvp);
	if (ip->i_nextsnap.tqe_prev != 0) {
		sn = devvp->v_rdev->si_snapdata;
		TAILQ_REMOVE(&sn->sn_head, ip, i_nextsnap);
		ip->i_nextsnap.tqe_prev = 0;
		VI_UNLOCK(devvp);
		lockmgr(&vp->v_lock, LK_EXCLUSIVE, NULL);
		for (i = 0; i < sn->sn_lock.lk_recurse; i++)
			lockmgr(&vp->v_lock, LK_EXCLUSIVE, NULL);
		KASSERT(vp->v_vnlock == &sn->sn_lock,
			("ffs_snapremove: lost lock mutation")); 
		vp->v_vnlock = &vp->v_lock;
		VI_LOCK(devvp);
		while (sn->sn_lock.lk_recurse > 0)
			lockmgr(&sn->sn_lock, LK_RELEASE, NULL);
		lockmgr(&sn->sn_lock, LK_RELEASE, NULL);
		try_free_snapdata(devvp);
	} else
		VI_UNLOCK(devvp);
	/*
	 * Clear all BLK_NOCOPY fields. Pass any block claims to other
	 * snapshots that want them (see ffs_snapblkfree below).
	 */
	for (blkno = 1; blkno < UFS_NDADDR; blkno++) {
		dblk = DIP(ip, i_db[blkno]);
		if (dblk == 0)
			continue;
		if (dblk == BLK_NOCOPY || dblk == BLK_SNAP)
			DIP_SET(ip, i_db[blkno], 0);
		else if ((dblk == blkstofrags(fs, blkno) &&
		     ffs_snapblkfree(fs, ITODEVVP(ip), dblk, fs->fs_bsize,
		     ip->i_number, vp->v_type, NULL))) {
			DIP_SET(ip, i_blocks, DIP(ip, i_blocks) -
			    btodb(fs->fs_bsize));
			DIP_SET(ip, i_db[blkno], 0);
		}
	}
	numblks = howmany(ip->i_size, fs->fs_bsize);
	for (blkno = UFS_NDADDR; blkno < numblks; blkno += NINDIR(fs)) {
		error = UFS_BALLOC(vp, lblktosize(fs, (off_t)blkno),
		    fs->fs_bsize, KERNCRED, BA_METAONLY, &ibp);
		if (error)
			continue;
		if (fs->fs_size - blkno > NINDIR(fs))
			last = NINDIR(fs);
		else
			last = fs->fs_size - blkno;
		for (loc = 0; loc < last; loc++) {
			if (I_IS_UFS1(ip)) {
				dblk = ((ufs1_daddr_t *)(ibp->b_data))[loc];
				if (dblk == 0)
					continue;
				if (dblk == BLK_NOCOPY || dblk == BLK_SNAP)
					((ufs1_daddr_t *)(ibp->b_data))[loc]= 0;
				else if ((dblk == blkstofrags(fs, blkno) &&
				     ffs_snapblkfree(fs, ITODEVVP(ip), dblk,
				     fs->fs_bsize, ip->i_number, vp->v_type,
				     NULL))) {
					ip->i_din1->di_blocks -=
					    btodb(fs->fs_bsize);
					((ufs1_daddr_t *)(ibp->b_data))[loc]= 0;
				}
				continue;
			}
			dblk = ((ufs2_daddr_t *)(ibp->b_data))[loc];
			if (dblk == 0)
				continue;
			if (dblk == BLK_NOCOPY || dblk == BLK_SNAP)
				((ufs2_daddr_t *)(ibp->b_data))[loc] = 0;
			else if ((dblk == blkstofrags(fs, blkno) &&
			     ffs_snapblkfree(fs, ITODEVVP(ip), dblk,
			     fs->fs_bsize, ip->i_number, vp->v_type, NULL))) {
				ip->i_din2->di_blocks -= btodb(fs->fs_bsize);
				((ufs2_daddr_t *)(ibp->b_data))[loc] = 0;
			}
		}
		bawrite(ibp);
	}
	/*
	 * Clear snapshot flag and drop reference.
	 */
	ip->i_flags &= ~SF_SNAPSHOT;
	DIP_SET(ip, i_flags, ip->i_flags);
	ip->i_flag |= IN_CHANGE | IN_UPDATE;
	/*
	 * The dirtied indirects must be written out before
	 * softdep_setup_freeblocks() is called.  Otherwise indir_trunc()
	 * may find indirect pointers using the magic BLK_* values.
	 */
	if (DOINGSOFTDEP(vp))
		ffs_syncvnode(vp, MNT_WAIT, 0);
#ifdef QUOTA
	/*
	 * Reenable disk quotas for ex-snapshot file.
	 */
	if (!getinoquota(ip))
		(void) chkdq(ip, DIP(ip, i_blocks), KERNCRED, FORCE);
#endif
}

/*
 * Notification that a block is being freed. Return zero if the free
 * should be allowed to proceed. Return non-zero if the snapshot file
 * wants to claim the block. The block will be claimed if it is an
 * uncopied part of one of the snapshots. It will be freed if it is
 * either a BLK_NOCOPY or has already been copied in all of the snapshots.
 * If a fragment is being freed, then all snapshots that care about
 * it must make a copy since a snapshot file can only claim full sized
 * blocks. Note that if more than one snapshot file maps the block,
 * we can pick one at random to claim it. Since none of the snapshots
 * can change, we are assurred that they will all see the same unmodified
 * image. When deleting a snapshot file (see ffs_snapremove above), we
 * must push any of these claimed blocks to one of the other snapshots
 * that maps it. These claimed blocks are easily identified as they will
 * have a block number equal to their logical block number within the
 * snapshot. A copied block can never have this property because they
 * must always have been allocated from a BLK_NOCOPY location.
 */
int
ffs_snapblkfree(fs, devvp, bno, size, inum, vtype, wkhd)
	struct fs *fs;
	struct vnode *devvp;
	ufs2_daddr_t bno;
	long size;
	ino_t inum;
	enum vtype vtype;
	struct workhead *wkhd;
{
	struct buf *ibp, *cbp, *savedcbp = NULL;
	struct thread *td = curthread;
	struct inode *ip;
	struct vnode *vp = NULL;
	ufs_lbn_t lbn;
	ufs2_daddr_t blkno;
	int indiroff = 0, error = 0, claimedblk = 0;
	struct snapdata *sn;

	lbn = fragstoblks(fs, bno);
retry:
	VI_LOCK(devvp);
	sn = devvp->v_rdev->si_snapdata;
	if (sn == NULL) {
		VI_UNLOCK(devvp);
		return (0);
	}
	if (lockmgr(&sn->sn_lock, LK_INTERLOCK | LK_EXCLUSIVE | LK_SLEEPFAIL,
	    VI_MTX(devvp)) != 0)
		goto retry;
	TAILQ_FOREACH(ip, &sn->sn_head, i_nextsnap) {
		vp = ITOV(ip);
		if (DOINGSOFTDEP(vp))
			softdep_prealloc(vp, MNT_WAIT);
		/*
		 * Lookup block being written.
		 */
		if (lbn < UFS_NDADDR) {
			blkno = DIP(ip, i_db[lbn]);
		} else {
			td->td_pflags |= TDP_COWINPROGRESS;
			error = UFS_BALLOC(vp, lblktosize(fs, (off_t)lbn),
			    fs->fs_bsize, KERNCRED, BA_METAONLY, &ibp);
			td->td_pflags &= ~TDP_COWINPROGRESS;
			if (error)
				break;
			indiroff = (lbn - UFS_NDADDR) % NINDIR(fs);
			if (I_IS_UFS1(ip))
				blkno=((ufs1_daddr_t *)(ibp->b_data))[indiroff];
			else
				blkno=((ufs2_daddr_t *)(ibp->b_data))[indiroff];
		}
		/*
		 * Check to see if block needs to be copied.
		 */
		if (blkno == 0) {
			/*
			 * A block that we map is being freed. If it has not
			 * been claimed yet, we will claim or copy it (below).
			 */
			claimedblk = 1;
		} else if (blkno == BLK_SNAP) {
			/*
			 * No previous snapshot claimed the block,
			 * so it will be freed and become a BLK_NOCOPY
			 * (don't care) for us.
			 */
			if (claimedblk)
				panic("snapblkfree: inconsistent block type");
			if (lbn < UFS_NDADDR) {
				DIP_SET(ip, i_db[lbn], BLK_NOCOPY);
				ip->i_flag |= IN_CHANGE | IN_UPDATE;
			} else if (I_IS_UFS1(ip)) {
				((ufs1_daddr_t *)(ibp->b_data))[indiroff] =
				    BLK_NOCOPY;
				bdwrite(ibp);
			} else {
				((ufs2_daddr_t *)(ibp->b_data))[indiroff] =
				    BLK_NOCOPY;
				bdwrite(ibp);
			}
			continue;
		} else /* BLK_NOCOPY or default */ {
			/*
			 * If the snapshot has already copied the block
			 * (default), or does not care about the block,
			 * it is not needed.
			 */
			if (lbn >= UFS_NDADDR)
				bqrelse(ibp);
			continue;
		}
		/*
		 * If this is a full size block, we will just grab it
		 * and assign it to the snapshot inode. Otherwise we
		 * will proceed to copy it. See explanation for this
		 * routine as to why only a single snapshot needs to
		 * claim this block.
		 */
		if (size == fs->fs_bsize) {
#ifdef DEBUG
			if (snapdebug)
				printf("%s %ju lbn %jd from inum %ju\n",
				    "Grabonremove: snapino",
				    (uintmax_t)ip->i_number,
				    (intmax_t)lbn, (uintmax_t)inum);
#endif
			/*
			 * If journaling is tracking this write we must add
			 * the work to the inode or indirect being written.
			 */
			if (wkhd != NULL) {
				if (lbn < UFS_NDADDR)
					softdep_inode_append(ip,
					    curthread->td_ucred, wkhd);
				else
					softdep_buf_append(ibp, wkhd);
			}
			if (lbn < UFS_NDADDR) {
				DIP_SET(ip, i_db[lbn], bno);
			} else if (I_IS_UFS1(ip)) {
				((ufs1_daddr_t *)(ibp->b_data))[indiroff] = bno;
				bdwrite(ibp);
			} else {
				((ufs2_daddr_t *)(ibp->b_data))[indiroff] = bno;
				bdwrite(ibp);
			}
			DIP_SET(ip, i_blocks, DIP(ip, i_blocks) + btodb(size));
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
			lockmgr(vp->v_vnlock, LK_RELEASE, NULL);
			return (1);
		}
		if (lbn >= UFS_NDADDR)
			bqrelse(ibp);
		/*
		 * Allocate the block into which to do the copy. Note that this
		 * allocation will never require any additional allocations for
		 * the snapshot inode.
		 */
		td->td_pflags |= TDP_COWINPROGRESS;
		error = UFS_BALLOC(vp, lblktosize(fs, (off_t)lbn),
		    fs->fs_bsize, KERNCRED, 0, &cbp);
		td->td_pflags &= ~TDP_COWINPROGRESS;
		if (error)
			break;
#ifdef DEBUG
		if (snapdebug)
			printf("%s%ju lbn %jd %s %ju size %ld to blkno %jd\n",
			    "Copyonremove: snapino ", (uintmax_t)ip->i_number,
			    (intmax_t)lbn, "for inum", (uintmax_t)inum, size,
			    (intmax_t)cbp->b_blkno);
#endif
		/*
		 * If we have already read the old block contents, then
		 * simply copy them to the new block. Note that we need
		 * to synchronously write snapshots that have not been
		 * unlinked, and hence will be visible after a crash,
		 * to ensure their integrity. At a minimum we ensure the
		 * integrity of the filesystem metadata, but use the
		 * dopersistence sysctl-setable flag to decide on the
		 * persistence needed for file content data.
		 */
		if (savedcbp != NULL) {
			bcopy(savedcbp->b_data, cbp->b_data, fs->fs_bsize);
			bawrite(cbp);
			if ((vtype == VDIR || dopersistence) &&
			    ip->i_effnlink > 0)
				(void) ffs_syncvnode(vp, MNT_WAIT, NO_INO_UPDT);
			continue;
		}
		/*
		 * Otherwise, read the old block contents into the buffer.
		 */
		if ((error = readblock(vp, cbp, lbn)) != 0) {
			bzero(cbp->b_data, fs->fs_bsize);
			bawrite(cbp);
			if ((vtype == VDIR || dopersistence) &&
			    ip->i_effnlink > 0)
				(void) ffs_syncvnode(vp, MNT_WAIT, NO_INO_UPDT);
			break;
		}
		savedcbp = cbp;
	}
	/*
	 * Note that we need to synchronously write snapshots that
	 * have not been unlinked, and hence will be visible after
	 * a crash, to ensure their integrity. At a minimum we
	 * ensure the integrity of the filesystem metadata, but
	 * use the dopersistence sysctl-setable flag to decide on
	 * the persistence needed for file content data.
	 */
	if (savedcbp) {
		vp = savedcbp->b_vp;
		bawrite(savedcbp);
		if ((vtype == VDIR || dopersistence) &&
		    VTOI(vp)->i_effnlink > 0)
			(void) ffs_syncvnode(vp, MNT_WAIT, NO_INO_UPDT);
	}
	/*
	 * If we have been unable to allocate a block in which to do
	 * the copy, then return non-zero so that the fragment will
	 * not be freed. Although space will be lost, the snapshot
	 * will stay consistent.
	 */
	if (error != 0 && wkhd != NULL)
		softdep_freework(wkhd);
	lockmgr(&sn->sn_lock, LK_RELEASE, NULL);
	return (error);
}

/*
 * Associate snapshot files when mounting.
 */
void
ffs_snapshot_mount(mp)
	struct mount *mp;
{
	struct ufsmount *ump = VFSTOUFS(mp);
	struct vnode *devvp = ump->um_devvp;
	struct fs *fs = ump->um_fs;
	struct thread *td = curthread;
	struct snapdata *sn;
	struct vnode *vp;
	struct vnode *lastvp;
	struct inode *ip;
	struct uio auio;
	struct iovec aiov;
	void *snapblklist;
	char *reason;
	daddr_t snaplistsize;
	int error, snaploc, loc;

	/*
	 * XXX The following needs to be set before ffs_truncate or
	 * VOP_READ can be called.
	 */
	mp->mnt_stat.f_iosize = fs->fs_bsize;
	/*
	 * Process each snapshot listed in the superblock.
	 */
	vp = NULL;
	lastvp = NULL;
	sn = NULL;
	for (snaploc = 0; snaploc < FSMAXSNAP; snaploc++) {
		if (fs->fs_snapinum[snaploc] == 0)
			break;
		if ((error = ffs_vget(mp, fs->fs_snapinum[snaploc],
		    LK_EXCLUSIVE, &vp)) != 0){
			printf("ffs_snapshot_mount: vget failed %d\n", error);
			continue;
		}
		ip = VTOI(vp);
		if (vp->v_type != VREG) {
			reason = "non-file snapshot";
		} else if (!IS_SNAPSHOT(ip)) {
			reason = "non-snapshot";
		} else if (ip->i_size ==
		    lblktosize(fs, howmany(fs->fs_size, fs->fs_frag))) {
			reason = "old format snapshot";
			(void)ffs_truncate(vp, (off_t)0, 0, NOCRED);
			(void)ffs_syncvnode(vp, MNT_WAIT, 0);
		} else {
			reason = NULL;
		}
		if (reason != NULL) {
			printf("ffs_snapshot_mount: %s inode %d\n",
			    reason, fs->fs_snapinum[snaploc]);
			vput(vp);
			vp = NULL;
			for (loc = snaploc + 1; loc < FSMAXSNAP; loc++) {
				if (fs->fs_snapinum[loc] == 0)
					break;
				fs->fs_snapinum[loc - 1] = fs->fs_snapinum[loc];
			}
			fs->fs_snapinum[loc - 1] = 0;
			snaploc--;
			continue;
		}
		/*
		 * Acquire a lock on the snapdata structure, creating it if
		 * necessary.
		 */
		sn = ffs_snapdata_acquire(devvp);
		/* 
		 * Change vnode to use shared snapshot lock instead of the
		 * original private lock.
		 */
		vp->v_vnlock = &sn->sn_lock;
		lockmgr(&vp->v_lock, LK_RELEASE, NULL);
		/*
		 * Link it onto the active snapshot list.
		 */
		VI_LOCK(devvp);
		if (ip->i_nextsnap.tqe_prev != 0)
			panic("ffs_snapshot_mount: %ju already on list",
			    (uintmax_t)ip->i_number);
		else
			TAILQ_INSERT_TAIL(&sn->sn_head, ip, i_nextsnap);
		vp->v_vflag |= VV_SYSTEM;
		VI_UNLOCK(devvp);
		VOP_UNLOCK(vp, 0);
		lastvp = vp;
	}
	vp = lastvp;
	/*
	 * No usable snapshots found.
	 */
	if (sn == NULL || vp == NULL)
		return;
	/*
	 * Allocate the space for the block hints list. We always want to
	 * use the list from the newest snapshot.
	 */
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	aiov.iov_base = (void *)&snaplistsize;
	aiov.iov_len = sizeof(snaplistsize);
	auio.uio_resid = aiov.iov_len;
	auio.uio_offset =
	    lblktosize(fs, howmany(fs->fs_size, fs->fs_frag));
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_td = td;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if ((error = VOP_READ(vp, &auio, IO_UNIT, td->td_ucred)) != 0) {
		printf("ffs_snapshot_mount: read_1 failed %d\n", error);
		VOP_UNLOCK(vp, 0);
		return;
	}
	snapblklist = malloc(snaplistsize * sizeof(daddr_t),
	    M_UFSMNT, M_WAITOK);
	auio.uio_iovcnt = 1;
	aiov.iov_base = snapblklist;
	aiov.iov_len = snaplistsize * sizeof (daddr_t);
	auio.uio_resid = aiov.iov_len;
	auio.uio_offset -= sizeof(snaplistsize);
	if ((error = VOP_READ(vp, &auio, IO_UNIT, td->td_ucred)) != 0) {
		printf("ffs_snapshot_mount: read_2 failed %d\n", error);
		VOP_UNLOCK(vp, 0);
		free(snapblklist, M_UFSMNT);
		return;
	}
	VOP_UNLOCK(vp, 0);
	VI_LOCK(devvp);
	ASSERT_VOP_LOCKED(devvp, "ffs_snapshot_mount");
	sn->sn_listsize = snaplistsize;
	sn->sn_blklist = (daddr_t *)snapblklist;
	devvp->v_vflag |= VV_COPYONWRITE;
	VI_UNLOCK(devvp);
}

/*
 * Disassociate snapshot files when unmounting.
 */
void
ffs_snapshot_unmount(mp)
	struct mount *mp;
{
	struct vnode *devvp = VFSTOUFS(mp)->um_devvp;
	struct snapdata *sn;
	struct inode *xp;
	struct vnode *vp;

	VI_LOCK(devvp);
	sn = devvp->v_rdev->si_snapdata;
	while (sn != NULL && (xp = TAILQ_FIRST(&sn->sn_head)) != NULL) {
		vp = ITOV(xp);
		TAILQ_REMOVE(&sn->sn_head, xp, i_nextsnap);
		xp->i_nextsnap.tqe_prev = 0;
		lockmgr(&sn->sn_lock, LK_INTERLOCK | LK_EXCLUSIVE,
		    VI_MTX(devvp));
		lockmgr(&vp->v_lock, LK_EXCLUSIVE, NULL);
		KASSERT(vp->v_vnlock == &sn->sn_lock,
		("ffs_snapshot_unmount: lost lock mutation")); 
		vp->v_vnlock = &vp->v_lock;
		lockmgr(&vp->v_lock, LK_RELEASE, NULL);
		lockmgr(&sn->sn_lock, LK_RELEASE, NULL);
		if (xp->i_effnlink > 0)
			vrele(vp);
		VI_LOCK(devvp);
		sn = devvp->v_rdev->si_snapdata;
	}
	try_free_snapdata(devvp);
	ASSERT_VOP_LOCKED(devvp, "ffs_snapshot_unmount");
}

/*
 * Check the buffer block to be belong to device buffer that shall be
 * locked after snaplk. devvp shall be locked on entry, and will be
 * leaved locked upon exit.
 */
static int
ffs_bp_snapblk(devvp, bp)
	struct vnode *devvp;
	struct buf *bp;
{
	struct snapdata *sn;
	struct fs *fs;
	ufs2_daddr_t lbn, *snapblklist;
	int lower, upper, mid;

	ASSERT_VI_LOCKED(devvp, "ffs_bp_snapblk");
	KASSERT(devvp->v_type == VCHR, ("Not a device %p", devvp));
	sn = devvp->v_rdev->si_snapdata;
	if (sn == NULL || TAILQ_FIRST(&sn->sn_head) == NULL)
		return (0);
	fs = ITOFS(TAILQ_FIRST(&sn->sn_head));
	lbn = fragstoblks(fs, dbtofsb(fs, bp->b_blkno));
	snapblklist = sn->sn_blklist;
	upper = sn->sn_listsize - 1;
	lower = 1;
	while (lower <= upper) {
		mid = (lower + upper) / 2;
		if (snapblklist[mid] == lbn)
			break;
		if (snapblklist[mid] < lbn)
			lower = mid + 1;
		else
			upper = mid - 1;
	}
	if (lower <= upper)
		return (1);
	return (0);
}

void
ffs_bdflush(bo, bp)
	struct bufobj *bo;
	struct buf *bp;
{
	struct thread *td;
	struct vnode *vp, *devvp;
	struct buf *nbp;
	int bp_bdskip;

	if (bo->bo_dirty.bv_cnt <= dirtybufthresh)
		return;

	td = curthread;
	vp = bp->b_vp;
	devvp = bo2vnode(bo);
	KASSERT(vp == devvp, ("devvp != vp %p %p", bo, bp));

	VI_LOCK(devvp);
	bp_bdskip = ffs_bp_snapblk(devvp, bp);
	if (bp_bdskip)
		bdwriteskip++;
	VI_UNLOCK(devvp);
	if (bo->bo_dirty.bv_cnt > dirtybufthresh + 10 && !bp_bdskip) {
		(void) VOP_FSYNC(vp, MNT_NOWAIT, td);
		altbufferflushes++;
	} else {
		BO_LOCK(bo);
		/*
		 * Try to find a buffer to flush.
		 */
		TAILQ_FOREACH(nbp, &bo->bo_dirty.bv_hd, b_bobufs) {
			if ((nbp->b_vflags & BV_BKGRDINPROG) ||
			    BUF_LOCK(nbp,
				     LK_EXCLUSIVE | LK_NOWAIT, NULL))
				continue;
			if (bp == nbp)
				panic("bdwrite: found ourselves");
			BO_UNLOCK(bo);
			/*
			 * Don't countdeps with the bo lock
			 * held.
			 */
			if (buf_countdeps(nbp, 0)) {
				BO_LOCK(bo);
				BUF_UNLOCK(nbp);
				continue;
			}
			if (bp_bdskip) {
				VI_LOCK(devvp);
				if (!ffs_bp_snapblk(vp, nbp)) {
					VI_UNLOCK(devvp);
					BO_LOCK(bo);
					BUF_UNLOCK(nbp);
					continue;
				}
				VI_UNLOCK(devvp);
			}
			if (nbp->b_flags & B_CLUSTEROK) {
				vfs_bio_awrite(nbp);
			} else {
				bremfree(nbp);
				bawrite(nbp);
			}
			dirtybufferflushes++;
			break;
		}
		if (nbp == NULL)
			BO_UNLOCK(bo);
	}
}

/*
 * Check for need to copy block that is about to be written,
 * copying the block if necessary.
 */
int
ffs_copyonwrite(devvp, bp)
	struct vnode *devvp;
	struct buf *bp;
{
	struct snapdata *sn;
	struct buf *ibp, *cbp, *savedcbp = NULL;
	struct thread *td = curthread;
	struct fs *fs;
	struct inode *ip;
	struct vnode *vp = NULL;
	ufs2_daddr_t lbn, blkno, *snapblklist;
	int lower, upper, mid, indiroff, error = 0;
	int launched_async_io, prev_norunningbuf;
	long saved_runningbufspace;

	if (devvp != bp->b_vp && IS_SNAPSHOT(VTOI(bp->b_vp)))
		return (0);		/* Update on a snapshot file */
	if (td->td_pflags & TDP_COWINPROGRESS)
		panic("ffs_copyonwrite: recursive call");
	/*
	 * First check to see if it is in the preallocated list.
	 * By doing this check we avoid several potential deadlocks.
	 */
	VI_LOCK(devvp);
	sn = devvp->v_rdev->si_snapdata;
	if (sn == NULL ||
	    TAILQ_EMPTY(&sn->sn_head)) {
		VI_UNLOCK(devvp);
		return (0);		/* No snapshot */
	}
	ip = TAILQ_FIRST(&sn->sn_head);
	fs = ITOFS(ip);
	lbn = fragstoblks(fs, dbtofsb(fs, bp->b_blkno));
	snapblklist = sn->sn_blklist;
	upper = sn->sn_listsize - 1;
	lower = 1;
	while (lower <= upper) {
		mid = (lower + upper) / 2;
		if (snapblklist[mid] == lbn)
			break;
		if (snapblklist[mid] < lbn)
			lower = mid + 1;
		else
			upper = mid - 1;
	}
	if (lower <= upper) {
		VI_UNLOCK(devvp);
		return (0);
	}
	launched_async_io = 0;
	prev_norunningbuf = td->td_pflags & TDP_NORUNNINGBUF;
	/*
	 * Since I/O on bp isn't yet in progress and it may be blocked
	 * for a long time waiting on snaplk, back it out of
	 * runningbufspace, possibly waking other threads waiting for space.
	 */
	saved_runningbufspace = bp->b_runningbufspace;
	if (saved_runningbufspace != 0)
		runningbufwakeup(bp);
	/*
	 * Not in the precomputed list, so check the snapshots.
	 */
	while (lockmgr(&sn->sn_lock, LK_INTERLOCK | LK_EXCLUSIVE | LK_SLEEPFAIL,
	    VI_MTX(devvp)) != 0) {
		VI_LOCK(devvp);
		sn = devvp->v_rdev->si_snapdata;
		if (sn == NULL ||
		    TAILQ_EMPTY(&sn->sn_head)) {
			VI_UNLOCK(devvp);
			if (saved_runningbufspace != 0) {
				bp->b_runningbufspace = saved_runningbufspace;
				atomic_add_long(&runningbufspace,
					       bp->b_runningbufspace);
			}
			return (0);		/* Snapshot gone */
		}
	}
	TAILQ_FOREACH(ip, &sn->sn_head, i_nextsnap) {
		vp = ITOV(ip);
		if (DOINGSOFTDEP(vp))
			softdep_prealloc(vp, MNT_WAIT);
		/*
		 * We ensure that everything of our own that needs to be
		 * copied will be done at the time that ffs_snapshot is
		 * called. Thus we can skip the check here which can
		 * deadlock in doing the lookup in UFS_BALLOC.
		 */
		if (bp->b_vp == vp)
			continue;
		/*
		 * Check to see if block needs to be copied. We do not have
		 * to hold the snapshot lock while doing this lookup as it
		 * will never require any additional allocations for the
		 * snapshot inode.
		 */
		if (lbn < UFS_NDADDR) {
			blkno = DIP(ip, i_db[lbn]);
		} else {
			td->td_pflags |= TDP_COWINPROGRESS | TDP_NORUNNINGBUF;
			error = UFS_BALLOC(vp, lblktosize(fs, (off_t)lbn),
			   fs->fs_bsize, KERNCRED, BA_METAONLY, &ibp);
			td->td_pflags &= ~TDP_COWINPROGRESS;
			if (error)
				break;
			indiroff = (lbn - UFS_NDADDR) % NINDIR(fs);
			if (I_IS_UFS1(ip))
				blkno=((ufs1_daddr_t *)(ibp->b_data))[indiroff];
			else
				blkno=((ufs2_daddr_t *)(ibp->b_data))[indiroff];
			bqrelse(ibp);
		}
#ifdef INVARIANTS
		if (blkno == BLK_SNAP && bp->b_lblkno >= 0)
			panic("ffs_copyonwrite: bad copy block");
#endif
		if (blkno != 0)
			continue;
		/*
		 * Allocate the block into which to do the copy. Since
		 * multiple processes may all try to copy the same block,
		 * we have to recheck our need to do a copy if we sleep
		 * waiting for the lock.
		 *
		 * Because all snapshots on a filesystem share a single
		 * lock, we ensure that we will never be in competition
		 * with another process to allocate a block.
		 */
		td->td_pflags |= TDP_COWINPROGRESS | TDP_NORUNNINGBUF;
		error = UFS_BALLOC(vp, lblktosize(fs, (off_t)lbn),
		    fs->fs_bsize, KERNCRED, 0, &cbp);
		td->td_pflags &= ~TDP_COWINPROGRESS;
		if (error)
			break;
#ifdef DEBUG
		if (snapdebug) {
			printf("Copyonwrite: snapino %ju lbn %jd for ",
			    (uintmax_t)ip->i_number, (intmax_t)lbn);
			if (bp->b_vp == devvp)
				printf("fs metadata");
			else
				printf("inum %ju",
				    (uintmax_t)VTOI(bp->b_vp)->i_number);
			printf(" lblkno %jd to blkno %jd\n",
			    (intmax_t)bp->b_lblkno, (intmax_t)cbp->b_blkno);
		}
#endif
		/*
		 * If we have already read the old block contents, then
		 * simply copy them to the new block. Note that we need
		 * to synchronously write snapshots that have not been
		 * unlinked, and hence will be visible after a crash,
		 * to ensure their integrity. At a minimum we ensure the
		 * integrity of the filesystem metadata, but use the
		 * dopersistence sysctl-setable flag to decide on the
		 * persistence needed for file content data.
		 */
		if (savedcbp != NULL) {
			bcopy(savedcbp->b_data, cbp->b_data, fs->fs_bsize);
			bawrite(cbp);
			if ((devvp == bp->b_vp || bp->b_vp->v_type == VDIR ||
			    dopersistence) && ip->i_effnlink > 0)
				(void) ffs_syncvnode(vp, MNT_WAIT, NO_INO_UPDT);
			else
				launched_async_io = 1;
			continue;
		}
		/*
		 * Otherwise, read the old block contents into the buffer.
		 */
		if ((error = readblock(vp, cbp, lbn)) != 0) {
			bzero(cbp->b_data, fs->fs_bsize);
			bawrite(cbp);
			if ((devvp == bp->b_vp || bp->b_vp->v_type == VDIR ||
			    dopersistence) && ip->i_effnlink > 0)
				(void) ffs_syncvnode(vp, MNT_WAIT, NO_INO_UPDT);
			else
				launched_async_io = 1;
			break;
		}
		savedcbp = cbp;
	}
	/*
	 * Note that we need to synchronously write snapshots that
	 * have not been unlinked, and hence will be visible after
	 * a crash, to ensure their integrity. At a minimum we
	 * ensure the integrity of the filesystem metadata, but
	 * use the dopersistence sysctl-setable flag to decide on
	 * the persistence needed for file content data.
	 */
	if (savedcbp) {
		vp = savedcbp->b_vp;
		bawrite(savedcbp);
		if ((devvp == bp->b_vp || bp->b_vp->v_type == VDIR ||
		    dopersistence) && VTOI(vp)->i_effnlink > 0)
			(void) ffs_syncvnode(vp, MNT_WAIT, NO_INO_UPDT);
		else
			launched_async_io = 1;
	}
	lockmgr(vp->v_vnlock, LK_RELEASE, NULL);
	td->td_pflags = (td->td_pflags & ~TDP_NORUNNINGBUF) |
		prev_norunningbuf;
	if (launched_async_io && (td->td_pflags & TDP_NORUNNINGBUF) == 0)
		waitrunningbufspace();
	/*
	 * I/O on bp will now be started, so count it in runningbufspace.
	 */
	if (saved_runningbufspace != 0) {
		bp->b_runningbufspace = saved_runningbufspace;
		atomic_add_long(&runningbufspace, bp->b_runningbufspace);
	}
	return (error);
}

/*
 * sync snapshots to force freework records waiting on snapshots to claim
 * blocks to free.
 */
void
ffs_sync_snap(mp, waitfor)
	struct mount *mp;
	int waitfor;
{
	struct snapdata *sn;
	struct vnode *devvp;
	struct vnode *vp;
	struct inode *ip;

	devvp = VFSTOUFS(mp)->um_devvp;
	if ((devvp->v_vflag & VV_COPYONWRITE) == 0)
		return;
	for (;;) {
		VI_LOCK(devvp);
		sn = devvp->v_rdev->si_snapdata;
		if (sn == NULL) {
			VI_UNLOCK(devvp);
			return;
		}
		if (lockmgr(&sn->sn_lock,
		    LK_INTERLOCK | LK_EXCLUSIVE | LK_SLEEPFAIL,
		    VI_MTX(devvp)) == 0)
			break;
	}
	TAILQ_FOREACH(ip, &sn->sn_head, i_nextsnap) {
		vp = ITOV(ip);
		ffs_syncvnode(vp, waitfor, NO_INO_UPDT);
	}
	lockmgr(&sn->sn_lock, LK_RELEASE, NULL);
}

/*
 * Read the specified block into the given buffer.
 * Much of this boiler-plate comes from bwrite().
 */
static int
readblock(vp, bp, lbn)
	struct vnode *vp;
	struct buf *bp;
	ufs2_daddr_t lbn;
{
	struct inode *ip;
	struct bio *bip;
	struct fs *fs;

	ip = VTOI(vp);
	fs = ITOFS(ip);

	bip = g_alloc_bio();
	bip->bio_cmd = BIO_READ;
	bip->bio_offset = dbtob(fsbtodb(fs, blkstofrags(fs, lbn)));
	bip->bio_data = bp->b_data;
	bip->bio_length = bp->b_bcount;
	bip->bio_done = NULL;

	g_io_request(bip, ITODEVVP(ip)->v_bufobj.bo_private);
	bp->b_error = biowait(bip, "snaprdb");
	g_destroy_bio(bip);
	return (bp->b_error);
}

#endif

/*
 * Process file deletes that were deferred by ufs_inactive() due to
 * the file system being suspended. Transfer IN_LAZYACCESS into
 * IN_MODIFIED for vnodes that were accessed during suspension.
 */
void
process_deferred_inactive(struct mount *mp)
{
	struct vnode *vp, *mvp;
	struct inode *ip;
	struct thread *td;
	int error;

	td = curthread;
	(void) vn_start_secondary_write(NULL, &mp, V_WAIT);
 loop:
	MNT_VNODE_FOREACH_ALL(vp, mp, mvp) {
		/*
		 * IN_LAZYACCESS is checked here without holding any
		 * vnode lock, but this flag is set only while holding
		 * vnode interlock.
		 */
		if (vp->v_type == VNON ||
		    ((VTOI(vp)->i_flag & IN_LAZYACCESS) == 0 &&
		    ((vp->v_iflag & VI_OWEINACT) == 0 || vp->v_usecount > 0))) {
			VI_UNLOCK(vp);
			continue;
		}
		vholdl(vp);
		error = vn_lock(vp, LK_EXCLUSIVE | LK_INTERLOCK);
		if (error != 0) {
			vdrop(vp);
			if (error == ENOENT)
				continue;	/* vnode recycled */
			MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
			goto loop;
		}
		ip = VTOI(vp);
		if ((ip->i_flag & IN_LAZYACCESS) != 0) {
			ip->i_flag &= ~IN_LAZYACCESS;
			ip->i_flag |= IN_MODIFIED;
		}
		VI_LOCK(vp);
		if ((vp->v_iflag & VI_OWEINACT) == 0 || vp->v_usecount > 0) {
			VI_UNLOCK(vp);
			VOP_UNLOCK(vp, 0);
			vdrop(vp);
			continue;
		}
		vinactive(vp, td);
		VNASSERT((vp->v_iflag & VI_OWEINACT) == 0, vp,
			 ("process_deferred_inactive: got VI_OWEINACT"));
		VI_UNLOCK(vp);
		VOP_UNLOCK(vp, 0);
		vdrop(vp);
	}
	vn_finished_secondary_write(mp);
}

#ifndef NO_FFS_SNAPSHOT

static struct snapdata *
ffs_snapdata_alloc(void)
{
	struct snapdata *sn;

	/*
	 * Fetch a snapdata from the free list if there is one available.
	 */
	mtx_lock(&snapfree_lock);
	sn = LIST_FIRST(&snapfree);
	if (sn != NULL)
		LIST_REMOVE(sn, sn_link);
	mtx_unlock(&snapfree_lock);
	if (sn != NULL)
		return (sn);
	/*
 	 * If there were no free snapdatas allocate one.
	 */
	sn = malloc(sizeof *sn, M_UFSMNT, M_WAITOK | M_ZERO);
	TAILQ_INIT(&sn->sn_head);
	lockinit(&sn->sn_lock, PVFS, "snaplk", VLKTIMEOUT,
	    LK_CANRECURSE | LK_NOSHARE);
	return (sn);
}

/*
 * The snapdata is never freed because we can not be certain that
 * there are no threads sleeping on the snap lock.  Persisting
 * them permanently avoids costly synchronization in ffs_lock().
 */
static void
ffs_snapdata_free(struct snapdata *sn)
{
	mtx_lock(&snapfree_lock);
	LIST_INSERT_HEAD(&snapfree, sn, sn_link);
	mtx_unlock(&snapfree_lock);
}

/* Try to free snapdata associated with devvp */
static void
try_free_snapdata(struct vnode *devvp)
{
	struct snapdata *sn;
	ufs2_daddr_t *snapblklist;

	ASSERT_VI_LOCKED(devvp, "try_free_snapdata");
	sn = devvp->v_rdev->si_snapdata;

	if (sn == NULL || TAILQ_FIRST(&sn->sn_head) != NULL ||
	    (devvp->v_vflag & VV_COPYONWRITE) == 0) {
		VI_UNLOCK(devvp);
		return;
	}

	devvp->v_rdev->si_snapdata = NULL;
	devvp->v_vflag &= ~VV_COPYONWRITE;
	lockmgr(&sn->sn_lock, LK_DRAIN|LK_INTERLOCK, VI_MTX(devvp));
	snapblklist = sn->sn_blklist;
	sn->sn_blklist = NULL;
	sn->sn_listsize = 0;
	lockmgr(&sn->sn_lock, LK_RELEASE, NULL);
	if (snapblklist != NULL)
		free(snapblklist, M_UFSMNT);
	ffs_snapdata_free(sn);
}

static struct snapdata *
ffs_snapdata_acquire(struct vnode *devvp)
{
	struct snapdata *nsn, *sn;
	int error;

	/*
	 * Allocate a free snapdata.  This is done before acquiring the
	 * devvp lock to avoid allocation while the devvp interlock is
	 * held.
	 */
	nsn = ffs_snapdata_alloc();

	for (;;) {
		VI_LOCK(devvp);
		sn = devvp->v_rdev->si_snapdata;
		if (sn == NULL) {
			/*
			 * This is the first snapshot on this
			 * filesystem and we use our pre-allocated
			 * snapdata.  Publish sn with the sn_lock
			 * owned by us, to avoid the race.
			 */
			error = lockmgr(&nsn->sn_lock, LK_EXCLUSIVE |
			    LK_NOWAIT, NULL);
			if (error != 0)
				panic("leaked sn, lockmgr error %d", error);
			sn = devvp->v_rdev->si_snapdata = nsn;
			VI_UNLOCK(devvp);
			nsn = NULL;
			break;
		}

		/*
		 * There is a snapshots which already exists on this
		 * filesystem, grab a reference to the common lock.
		 */
		error = lockmgr(&sn->sn_lock, LK_INTERLOCK |
		    LK_EXCLUSIVE | LK_SLEEPFAIL, VI_MTX(devvp));
		if (error == 0)
			break;
	}

	/*
	 * Free any unused snapdata.
	 */
	if (nsn != NULL)
		ffs_snapdata_free(nsn);

	return (sn);
}

#endif
