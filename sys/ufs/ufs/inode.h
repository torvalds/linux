/*	$OpenBSD: inode.h,v 1.54 2024/02/03 18:51:58 beck Exp $	*/
/*	$NetBSD: inode.h,v 1.8 1995/06/15 23:22:50 cgd Exp $	*/

/*
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
 *	@(#)inode.h	8.5 (Berkeley) 7/8/94
 */

#include <sys/buf.h>
#include <sys/lock.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ext2fs/ext2fs_dinode.h>
#include <ufs/ext2fs/ext2fs_extents.h>


/*
 * Per-filesystem inode extensions.
 */
struct ext2fs_inode_ext {
	u_int32_t	ext2fs_last_lblk;	/* last logical blk allocated */
	u_int32_t	ext2fs_last_blk;	/* last blk allocated on disk */
	u_int32_t	ext2fs_effective_uid;	/* effective inode uid */
	u_int32_t	ext2fs_effective_gid;	/* effective inode gid */
	struct ext4_extent_cache	ext2fs_extent_cache;
};

/*
 * The inode is used to describe each active (or recently active) file in the
 * UFS filesystem. It is composed of two types of information. The first part
 * is the information that is needed only while the file is active (such as
 * the identity of the file and linkage to speed its lookup). The second part
 * is * the permanent meta-data associated with the file which is read in
 * from the permanent dinode from long term storage when the file becomes
 * active, and is put back when the file is no longer being used.
 */
struct inode {
	LIST_ENTRY(inode) i_hash; /* Hash chain */
	struct	vnode  *i_vnode;/* Vnode associated with this inode. */
	struct	ufsmount *i_ump;
	u_int32_t i_flag;	/* flags, see below */
	dev_t	  i_dev;	/* Device associated with the inode. */
	ufsino_t  i_number;	/* The identity of the inode. */
	int       i_effnlink;   /* i_nlink when I/O completes */

	union {			/* Associated filesystem. */
		struct	fs *fs;			/* FFS */
		struct  m_ext2fs *e2fs;		/* EXT2FS */
	} inode_u;

#define	i_fs	inode_u.fs
#define	i_e2fs	inode_u.e2fs

	struct   cluster_info i_ci;
	struct	 dquot *i_dquot[MAXQUOTAS]; /* Dquot structures. */
	u_quad_t i_modrev;	/* Revision level for NFS lease. */
	struct	 lockf_state *i_lockf; /* Byte-level lock state. */
	struct   rrwlock i_lock;/* Inode lock */

	/*
	 * Side effects; used during directory lookup.
	 */
	int32_t	  i_count;	/* Size of free slot in directory. */
	doff_t	  i_endoff;	/* End of useful stuff in directory. */
	doff_t	  i_diroff;	/* Offset in dir, where we found last entry. */
	doff_t	  i_offset;	/* Offset of free space in directory. */
	ufsino_t  i_ino;	/* Inode number of found directory. */
	u_int32_t i_reclen;	/* Size of found directory entry. */
	/*
	 * Inode extensions
	 */
	union {
		/* Other extensions could go here... */
		struct ext2fs_inode_ext   e2fs;
		struct dirhash *dirhash;
	} inode_ext;

#define i_e2fs_last_lblk	inode_ext.e2fs.ext2fs_last_lblk
#define i_e2fs_last_blk		inode_ext.e2fs.ext2fs_last_blk
#define i_e2fs_uid		inode_ext.e2fs.ext2fs_effective_uid
#define i_e2fs_gid		inode_ext.e2fs.ext2fs_effective_gid
#define i_e2fs_ext_cache	inode_ext.e2fs.ext2fs_extent_cache
#define	i_dirhash		inode_ext.dirhash

	/*
	 * The on-disk dinode itself.
	 */
	union {
		struct ufs1_dinode     *ffs1_din;
		struct ufs2_dinode     *ffs2_din;
		struct ext2fs_dinode   *e2fs_din;
	} dinode_u;

#define i_din1	dinode_u.ffs1_din
#define i_din2	dinode_u.ffs2_din
#define	i_e2din	dinode_u.e2fs_din

	struct inode_vtbl *i_vtbl;
};

struct inode_vtbl {
	int (* iv_truncate)(struct inode *, off_t, int, 
	    struct ucred *);
	int (* iv_update)(struct inode *, int waitfor);
	int (* iv_inode_alloc)(struct inode *, mode_t mode, 
	    struct ucred *, struct vnode **);
	int (* iv_inode_free)(struct inode *, ufsino_t ino, mode_t mode);
	int (* iv_buf_alloc)(struct inode *, off_t, int, struct ucred *,
	    int, struct buf **);
	int (* iv_bufatoff)(struct inode *, off_t offset, char **res,
	    struct buf **bpp);
};

#define UFS_TRUNCATE(ip, off, flags, cred) \
    ((ip)->i_vtbl->iv_truncate)((ip), (off), (flags), (cred))

#define UFS_UPDATE(ip, sync) \
    ((ip)->i_vtbl->iv_update)((ip), (sync))

#define UFS_INODE_ALLOC(pip, mode, cred, vpp) \
    ((pip)->i_vtbl->iv_inode_alloc)((pip), (mode), (cred), (vpp))

#define UFS_INODE_FREE(pip, ino, mode) \
    ((pip)->i_vtbl->iv_inode_free)((pip), (ino), (mode))

#define UFS_BUF_ALLOC(ip, startoffset, size, cred, flags, bpp) \
    ((ip)->i_vtbl->iv_buf_alloc)((ip), (startoffset), (size), (cred), \
        (flags), (bpp))
 
#define UFS_BUFATOFF(ip, offset, res, bpp) \
    ((ip)->i_vtbl->iv_bufatoff)((ip), (offset), (res), (bpp))

#define	i_ffs1_atime		i_din1->di_atime
#define	i_ffs1_atimensec	i_din1->di_atimensec
#define	i_ffs1_blocks		i_din1->di_blocks
#define	i_ffs1_ctime		i_din1->di_ctime
#define	i_ffs1_ctimensec	i_din1->di_ctimensec
#define	i_ffs1_db		i_din1->di_db
#define	i_ffs1_flags		i_din1->di_flags
#define	i_ffs1_gen		i_din1->di_gen
#define	i_ffs1_gid		i_din1->di_gid
#define	i_ffs1_ib		i_din1->di_ib
#define	i_ffs1_mode		i_din1->di_mode
#define	i_ffs1_mtime		i_din1->di_mtime
#define	i_ffs1_mtimensec	i_din1->di_mtimensec
#define	i_ffs1_nlink		i_din1->di_nlink
#define	i_ffs1_rdev		i_din1->di_rdev
#define	i_ffs1_shortlink	i_din1->di_shortlink
#define	i_ffs1_size		i_din1->di_size
#define	i_ffs1_uid		i_din1->di_uid

#define	i_ffs2_atime		i_din2->di_atime
#define	i_ffs2_atimensec	i_din2->di_atimensec
#define	i_ffs2_blocks		i_din2->di_blocks
#define	i_ffs2_blksize		i_din2->di_blksize
#define	i_ffs2_ctime		i_din2->di_ctime
#define	i_ffs2_ctimensec	i_din2->di_ctimensec
#define	i_ffs2_db		i_din2->di_db
#define	i_ffs2_flags		i_din2->di_flags
#define	i_ffs2_gen		i_din2->di_gen
#define	i_ffs2_gid		i_din2->di_gid
#define	i_ffs2_ib		i_din2->di_ib
#define	i_ffs2_mode		i_din2->di_mode
#define	i_ffs2_mtime		i_din2->di_mtime
#define	i_ffs2_mtimensec	i_din2->di_mtimensec
#define	i_ffs2_nlink		i_din2->di_nlink
#define	i_ffs2_rdev		i_din2->di_rdev
#define	i_ffs2_size		i_din2->di_size
#define	i_ffs2_uid		i_din2->di_uid

#ifndef _KERNEL
/*
 * These are here purely for backwards compatibility for userland.
 * They allow direct references to FFS structures using the old names.
 */
#define	i_atime			i_din1->di_atime
#define	i_atimensec		i_din1->di_atimensec
#define	i_blocks		i_din1->di_blocks
#define	i_ctime			i_din1->di_ctime
#define	i_ctimensec		i_din1->di_ctimensec
#define	i_db			i_din1->di_db
#define	i_flags			i_din1->di_flags
#define	i_gen			i_din1->di_gen
#define	i_gid			i_din1->di_gid
#define	i_ib			i_din1->di_ib
#define	i_mode			i_din1->di_mode
#define	i_mtime			i_din1->di_mtime
#define	i_mtimensec		i_din1->di_mtimensec
#define	i_nlink			i_din1->di_nlink
#define	i_rdev			i_din1->di_rdev
#define	i_shortlink		i_din1->di_shortlink
#define	i_size			i_din1->di_size
#define	i_uid			i_din1->di_uid
#endif	/* _KERNEL */

#define i_e2fs_mode		i_e2din->e2di_mode
#define i_e2fs_size		i_e2din->e2di_size
#define i_e2fs_atime		i_e2din->e2di_atime
#define i_e2fs_ctime		i_e2din->e2di_ctime
#define i_e2fs_mtime		i_e2din->e2di_mtime
#define i_e2fs_dtime		i_e2din->e2di_dtime
#define i_e2fs_nlink		i_e2din->e2di_nlink
#define i_e2fs_nblock		i_e2din->e2di_nblock
#define i_e2fs_flags		i_e2din->e2di_flags
#define i_e2fs_blocks		i_e2din->e2di_blocks
#define i_e2fs_gen		i_e2din->e2di_gen
#define i_e2fs_facl		i_e2din->e2di_facl
#define i_e2fs_size_hi		i_e2din->e2di_size_hi
#define i_e2fs_faddr		i_e2din->e2di_faddr
#define i_e2fs_nblock_hi	i_e2din->e2di_nblock_hi
#define i_e2fs_faddr_hi		i_e2din->e2di_faddr_hi
#define i_e2fs_uid_low		i_e2din->e2di_uid_low
#define i_e2fs_gid_low		i_e2din->e2di_gid_low
#define i_e2fs_uid_high		i_e2din->e2di_uid_high
#define i_e2fs_gid_high		i_e2din->e2di_gid_high

/* These flags are kept in i_flag. */
#define	IN_ACCESS	0x0001		/* Access time update request. */
#define	IN_CHANGE	0x0002		/* Inode change time update request. */
#define IN_UPDATE       0x0004          /* Modification time update request */
#define	IN_MODIFIED	0x0008		/* Inode has been modified. */
#define	IN_RENAME	0x0010		/* Inode is being renamed. */
#define IN_SHLOCK       0x0020          /* File has shared lock. */
#define	IN_EXLOCK	0x0040		/* File has exclusive lock. */
#define	IN_LAZYMOD	0x0080		/* Modified, but don't write yet. */
#define	IN_HASHED	0x0100		/* Inode is on the hash chain */

#define	i_devvp i_ump->um_devvp

#ifdef _KERNEL

/*
 * The DIP macros are used to access fields in the dinode.
 */

#ifdef FFS2
#define DIP(ip, field) \
	(((ip)->i_ump->um_fstype == UM_UFS1) ? \
	(ip)->i_ffs1_##field : (ip)->i_ffs2_##field)
#else
#define DIP(ip, field) \
	((ip)->i_ffs1_##field)
#endif

#ifdef FFS2
#define DIP_OP(ip, field, op, value)					\
	do {								\
		if ((ip)->i_ump->um_fstype == UM_UFS1)			\
			(ip)->i_ffs1_##field op (value);		\
		else							\
			(ip)->i_ffs2_##field op (value);		\
	} while (0)
#else
#define DIP_OP(ip, field, op, value)					\
	do {								\
		(ip)->i_ffs1_##field op (value);			\
	} while (0)
#endif

#define DIP_ASSIGN(ip, field, value)	DIP_OP(ip, field, =, value)
#define DIP_ADD(ip, field, value)	DIP_OP(ip, field, +=, value)
#define DIP_AND(ip, field, value)	DIP_OP(ip, field, &=, value)
#define DIP_OR(ip, field, value)	DIP_OP(ip, field, |=, value)

#ifdef FFS2
#define SHORTLINK(ip) \
	(((ip)->i_ump->um_fstype == UM_UFS1) ? \
	(caddr_t)(ip)->i_ffs1_db : (caddr_t)(ip)->i_ffs2_db)
#else
#define SHORTLINK(ip) \
	((caddr_t)(ip)->i_ffs1_db)
#endif

/*
 * Structure used to pass around logical block paths generated by
 * ufs_getlbns and used by truncate and bmap code.
 */
struct indir {
	daddr_t in_lbn;		/* Logical block number. */
	int	in_off;			/* Offset in buffer. */
	int	in_exists;		/* Flag if the block exists. */
};

/* Convert between inode pointers and vnode pointers. */
#define	VTOI(vp)	((struct inode *)(vp)->v_data)
#define	ITOV(ip)	((ip)->i_vnode)

#define	EXT2FS_ITIMES(ip) do {						\
	if ((ip)->i_flag & (IN_ACCESS | IN_CHANGE | IN_UPDATE)) {	\
		(ip)->i_flag |= IN_MODIFIED;				\
		if ((ip)->i_flag & IN_ACCESS)				\
			(ip)->i_e2fs_atime = gettime();			\
		if ((ip)->i_flag & IN_UPDATE)				\
			(ip)->i_e2fs_mtime = gettime();			\
		if ((ip)->i_flag & IN_CHANGE) {				\
			(ip)->i_e2fs_ctime = gettime();			\
			(ip)->i_modrev++;				\
		}							\
		(ip)->i_flag &= ~(IN_ACCESS | IN_CHANGE | IN_UPDATE);	\
	}								\
} while (0)

/* Determine if soft dependencies are being done */
#define DOINGASYNC(vp)        ((vp)->v_mount->mnt_flag & MNT_ASYNC)

/* This overlays the fid structure (see mount.h). */
struct ufid {
	u_int16_t ufid_len;	/* Length of structure. */
	u_int16_t ufid_pad;	/* Force 32-bit alignment. */
	ufsino_t  ufid_ino;	/* File number (ino). */
	u_int32_t ufid_gen;	/* Generation number. */
};
#endif /* _KERNEL */
