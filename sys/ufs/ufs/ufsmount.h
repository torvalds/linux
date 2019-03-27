/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ufsmount.h	8.6 (Berkeley) 3/30/95
 * $FreeBSD$
 */

#ifndef _UFS_UFS_UFSMOUNT_H_
#define	_UFS_UFS_UFSMOUNT_H_

/*
 * Arguments to mount UFS-based filesystems
 */
struct ufs_args {
	char	*fspec;			/* block special device to mount */
	struct	oexport_args export;	/* network export information */
};

#ifdef _KERNEL

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_UFSMNT);
MALLOC_DECLARE(M_TRIM);
#endif

struct buf;
struct inode;
struct nameidata;
struct taskqueue;
struct timeval;
struct ucred;
struct uio;
struct vnode;
struct ufs_extattr_per_mount;
struct jblocks;
struct inodedep;

TAILQ_HEAD(inodedeplst, inodedep);
LIST_HEAD(bmsafemaphd, bmsafemap);
LIST_HEAD(trimlist_hashhead, ffs_blkfree_trim_params);

/*
 * This structure describes the UFS specific mount structure data.
 * The function operators are used to support different versions of
 * UFS (UFS1, UFS2, etc).
 *
 * Lock reference:
 *	c - set at allocation then constant until freed
 *	i - ufsmount interlock (UFS_LOCK / UFS_UNLOCK)
 *	q - associated quota file is locked
 *	r - ref to parent mount structure is held (vfs_busy / vfs_unbusy)
 *	u - managed by user process fsck_ufs
 */
struct ufsmount {
	struct	mount *um_mountp;		/* (r) filesystem vfs struct */
	struct	cdev *um_dev;			/* (r) device mounted */
	struct	g_consumer *um_cp;		/* (r) GEOM access point */
	struct	bufobj *um_bo;			/* (r) Buffer cache object */
	struct	vnode *um_devvp;		/* (r) blk dev mounted vnode */
	u_long	um_fstype;			/* (c) type of filesystem */
	struct	fs *um_fs;			/* (r) pointer to superblock */
	struct	ufs_extattr_per_mount um_extattr; /* (c) extended attrs */
	u_long	um_nindir;			/* (c) indirect ptrs per blk */
	u_long	um_bptrtodb;			/* (c) indir disk block ptr */
	u_long	um_seqinc;			/* (c) inc between seq blocks */
	struct	mtx um_lock;			/* (c) Protects ufsmount & fs */
	pid_t	um_fsckpid;			/* (u) PID can do fsck sysctl */
	struct	mount_softdeps *um_softdep;	/* (c) softdep mgmt structure */
	struct	vnode *um_quotas[MAXQUOTAS];	/* (q) pointer to quota files */
	struct	ucred *um_cred[MAXQUOTAS];	/* (q) quota file access cred */
	time_t	um_btime[MAXQUOTAS];		/* (q) block quota time limit */
	time_t	um_itime[MAXQUOTAS];		/* (q) inode quota time limit */
	char	um_qflags[MAXQUOTAS];		/* (i) quota specific flags */
	int64_t	um_savedmaxfilesize;		/* (c) track maxfilesize */
	u_int	um_flags;			/* (i) filesystem flags */
	u_int	um_trim_inflight;		/* (i) outstanding trim count */
	u_int	um_trim_inflight_blks;		/* (i) outstanding trim blks */
	u_long	um_trim_total;			/* (i) total trim count */
	u_long	um_trim_total_blks;		/* (i) total trim block count */
	struct	taskqueue *um_trim_tq;		/* (c) trim request queue */
	struct	trimlist_hashhead *um_trimhash;	/* (i) trimlist hash table */
	u_long	um_trimlisthashsize;		/* (i) trim hash table size-1 */
						/* (c) - below function ptrs */
	int	(*um_balloc)(struct vnode *, off_t, int, struct ucred *,
		    int, struct buf **);
	int	(*um_blkatoff)(struct vnode *, off_t, char **, struct buf **);
	int	(*um_truncate)(struct vnode *, off_t, int, struct ucred *);
	int	(*um_update)(struct vnode *, int);
	int	(*um_valloc)(struct vnode *, int, struct ucred *,
		    struct vnode **);
	int	(*um_vfree)(struct vnode *, ino_t, int);
	void	(*um_ifree)(struct ufsmount *, struct inode *);
	int	(*um_rdonly)(struct inode *);
	void	(*um_snapgone)(struct inode *);
};

/*
 * filesystem flags
 */
#define UM_CANDELETE		0x00000001	/* devvp supports TRIM */
#define UM_WRITESUSPENDED	0x00000002	/* suspension in progress */

/*
 * function prototypes
 */
#define	UFS_BALLOC(aa, bb, cc, dd, ee, ff) VFSTOUFS((aa)->v_mount)->um_balloc(aa, bb, cc, dd, ee, ff)
#define	UFS_BLKATOFF(aa, bb, cc, dd) VFSTOUFS((aa)->v_mount)->um_blkatoff(aa, bb, cc, dd)
#define	UFS_TRUNCATE(aa, bb, cc, dd) VFSTOUFS((aa)->v_mount)->um_truncate(aa, bb, cc, dd)
#define	UFS_UPDATE(aa, bb) VFSTOUFS((aa)->v_mount)->um_update(aa, bb)
#define	UFS_VALLOC(aa, bb, cc, dd) VFSTOUFS((aa)->v_mount)->um_valloc(aa, bb, cc, dd)
#define	UFS_VFREE(aa, bb, cc) VFSTOUFS((aa)->v_mount)->um_vfree(aa, bb, cc)
#define	UFS_IFREE(aa, bb) ((aa)->um_ifree(aa, bb))
#define	UFS_RDONLY(aa) (ITOUMP(aa)->um_rdonly(aa))
#define	UFS_SNAPGONE(aa) (ITOUMP(aa)->um_snapgone(aa))

#define	UFS_LOCK(aa)	mtx_lock(&(aa)->um_lock)
#define	UFS_UNLOCK(aa)	mtx_unlock(&(aa)->um_lock)
#define	UFS_MTX(aa)	(&(aa)->um_lock)

/*
 * Filesystem types
 */
#define	UFS1	1
#define	UFS2	2

/*
 * Flags describing the state of quotas.
 */
#define	QTF_OPENING	0x01			/* Q_QUOTAON in progress */
#define	QTF_CLOSING	0x02			/* Q_QUOTAOFF in progress */
#define	QTF_64BIT	0x04			/* 64-bit quota file */

/* Convert mount ptr to ufsmount ptr. */
#define	VFSTOUFS(mp)	((struct ufsmount *)((mp)->mnt_data))
#define	UFSTOVFS(ump)	(ump)->um_mountp

/*
 * Macros to access filesystem parameters in the ufsmount structure.
 * Used by ufs_bmap.
 */
#define	MNINDIR(ump)			((ump)->um_nindir)
#define	blkptrtodb(ump, b)		((b) << (ump)->um_bptrtodb)
#define	is_sequential(ump, a, b)	((b) == (a) + ump->um_seqinc)
#endif /* _KERNEL */

#endif
