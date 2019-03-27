/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)inode.h	8.9 (Berkeley) 5/14/95
 * $FreeBSD$
 */

#ifndef _UFS_UFS_INODE_H_
#define	_UFS_UFS_INODE_H_

#include <sys/lock.h>
#include <sys/queue.h>
#include <ufs/ufs/dinode.h>

/*
 * This must agree with the definition in <ufs/ufs/dir.h>.
 */
#define	doff_t		int32_t

/*
 * The inode is used to describe each active (or recently active) file in the
 * UFS filesystem. It is composed of two types of information. The first part
 * is the information that is needed only while the file is active (such as
 * the identity of the file and linkage to speed its lookup). The second part
 * is the permanent meta-data associated with the file which is read in
 * from the permanent dinode from long term storage when the file becomes
 * active, and is put back when the file is no longer being used.
 *
 * An inode may only be changed while holding either the exclusive
 * vnode lock or the shared vnode lock and the vnode interlock. We use
 * the latter only for "read" and "get" operations that require
 * changing i_flag, or a timestamp. This locking protocol allows executing
 * those operations without having to upgrade the vnode lock from shared to
 * exclusive.
 */
struct inode {
	TAILQ_ENTRY(inode) i_nextsnap; /* snapshot file list. */
	struct	vnode  *i_vnode;/* Vnode associated with this inode. */
	struct 	ufsmount *i_ump;/* Ufsmount point associated with this inode. */
	struct	 dquot *i_dquot[MAXQUOTAS]; /* Dquot structures. */
	union {
		struct dirhash *dirhash; /* Hashing for large directories. */
		daddr_t *snapblklist;    /* Collect expunged snapshot blocks. */
	} i_un;
	/*
	 * The real copy of the on-disk inode.
	 */
	union {
		struct ufs1_dinode *din1;	/* UFS1 on-disk dinode. */
		struct ufs2_dinode *din2;	/* UFS2 on-disk dinode. */
	} dinode_u;

	ino_t	  i_number;	/* The identity of the inode. */
	u_int32_t i_flag;	/* flags, see below */
	int	  i_effnlink;	/* i_nlink when I/O completes */


	/*
	 * Side effects; used during directory lookup.
	 */
	int32_t	  i_count;	/* Size of free slot in directory. */
	doff_t	  i_endoff;	/* End of useful stuff in directory. */
	doff_t	  i_diroff;	/* Offset in dir, where we found last entry. */
	doff_t	  i_offset;	/* Offset of free space in directory. */

	int	i_nextclustercg; /* last cg searched for cluster */

	/*
	 * Data for extended attribute modification.
 	 */
	u_char	  *i_ea_area;	/* Pointer to malloced copy of EA area */
	unsigned  i_ea_len;	/* Length of i_ea_area */
	int	  i_ea_error;	/* First errno in transaction */
	int	  i_ea_refs;	/* Number of users of EA area */

	/*
	 * Copies from the on-disk dinode itself.
	 */
	u_int64_t i_size;	/* File byte count. */
	u_int64_t i_gen;	/* Generation number. */
	u_int32_t i_flags;	/* Status flags (chflags). */
	u_int32_t i_uid;	/* File owner. */
	u_int32_t i_gid;	/* File group. */
	u_int16_t i_mode;	/* IFMT, permissions; see below. */
	int16_t	  i_nlink;	/* File link count. */
};
/*
 * These flags are kept in i_flag.
 */
#define	IN_ACCESS	0x0001		/* Access time update request. */
#define	IN_CHANGE	0x0002		/* Inode change time update request. */
#define	IN_UPDATE	0x0004		/* Modification time update request. */
#define	IN_MODIFIED	0x0008		/* Inode has been modified. */
#define	IN_NEEDSYNC	0x0010		/* Inode requires fsync. */
#define	IN_LAZYMOD	0x0020		/* Modified, but don't write yet. */
#define	IN_LAZYACCESS	0x0040		/* Process IN_ACCESS after the
					   suspension finished */
#define	IN_EA_LOCKED	0x0080
#define	IN_EA_LOCKWAIT	0x0100

#define	IN_TRUNCATED	0x0200		/* Journaled truncation pending. */

#define	IN_UFS2		0x0400		/* UFS2 vs UFS1 */

#define PRINT_INODE_FLAGS "\20\20b16\17b15\16b14\15b13" \
	"\14b12\13is_ufs2\12truncated\11ea_lockwait\10ea_locked" \
	"\7lazyaccess\6lazymod\5needsync\4modified\3update\2change\1access"

#define	i_dirhash i_un.dirhash
#define	i_snapblklist i_un.snapblklist
#define	i_din1 dinode_u.din1
#define	i_din2 dinode_u.din2

#ifdef _KERNEL

#define	ITOUMP(ip)	((ip)->i_ump)
#define	ITODEV(ip)	(ITOUMP(ip)->um_dev)
#define	ITODEVVP(ip)	(ITOUMP(ip)->um_devvp)
#define	ITOFS(ip)	(ITOUMP(ip)->um_fs)
#define	ITOVFS(ip)	((ip)->i_vnode->v_mount)

static inline _Bool
I_IS_UFS1(const struct inode *ip)
{

	return ((ip->i_flag & IN_UFS2) == 0);
}

static inline _Bool
I_IS_UFS2(const struct inode *ip)
{

	return ((ip->i_flag & IN_UFS2) != 0);
}

/*
 * The DIP macro is used to access fields in the dinode that are
 * not cached in the inode itself.
 */
#define	DIP(ip, field)	(I_IS_UFS1(ip) ? (ip)->i_din1->d##field : \
    (ip)->i_din2->d##field)
#define	DIP_SET(ip, field, val) do {				\
	if (I_IS_UFS1(ip))					\
		(ip)->i_din1->d##field = (val); 		\
	else							\
		(ip)->i_din2->d##field = (val); 		\
	} while (0)

#define	SHORTLINK(ip)	(I_IS_UFS1(ip) ?			\
    (caddr_t)(ip)->i_din1->di_db : (caddr_t)(ip)->i_din2->di_db)
#define	IS_SNAPSHOT(ip)		((ip)->i_flags & SF_SNAPSHOT)

/*
 * Structure used to pass around logical block paths generated by
 * ufs_getlbns and used by truncate and bmap code.
 */
struct indir {
	ufs2_daddr_t in_lbn;		/* Logical block number. */
	int	in_off;			/* Offset in buffer. */
};

/* Convert between inode pointers and vnode pointers. */
#define	VTOI(vp)	((struct inode *)(vp)->v_data)
#define	ITOV(ip)	((ip)->i_vnode)

/* Determine if soft dependencies are being done */
#define	DOINGSOFTDEP(vp)   ((vp)->v_mount->mnt_flag & (MNT_SOFTDEP | MNT_SUJ))
#define	MOUNTEDSOFTDEP(mp) ((mp)->mnt_flag & (MNT_SOFTDEP | MNT_SUJ))
#define	DOINGSUJ(vp)	   ((vp)->v_mount->mnt_flag & MNT_SUJ)
#define	MOUNTEDSUJ(mp)	   ((mp)->mnt_flag & MNT_SUJ)

/* This overlays the fid structure (see mount.h). */
struct ufid {
	u_int16_t ufid_len;	/* Length of structure. */
	u_int16_t ufid_pad;	/* Force 32-bit alignment. */
	uint32_t  ufid_ino;	/* File number (ino). */
	uint32_t  ufid_gen;	/* Generation number. */
};
#endif /* _KERNEL */

#endif /* !_UFS_UFS_INODE_H_ */
