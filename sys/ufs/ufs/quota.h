/*	$OpenBSD: quota.h,v 1.12 2013/07/03 04:58:40 guenther Exp $	*/
/*	$NetBSD: quota.h,v 1.6 1995/03/26 20:38:17 jtc Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Robert Elz at The University of Melbourne.
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
 *	@(#)quota.h	8.3 (Berkeley) 8/19/94
 */

#ifndef _QUOTA_
#define _QUOTA_

/*
 * Definitions for disk quotas imposed on the average user
 * (big brother finally hits UNIX).
 *
 * The following constants define the amount of time given a user before the
 * soft limits are treated as hard limits (usually resulting in an allocation
 * failure). The timer is started when the user crosses their soft limit, it
 * is reset when they go below their soft limit.
 */
#define	MAX_IQ_TIME	(7*24*60*60)	/* seconds in 1 week */
#define	MAX_DQ_TIME	(7*24*60*60)	/* seconds in 1 week */

/*
 * The following constants define the usage of the quota file array in the
 * ufsmount structure and dquot array in the inode structure.  The semantics
 * of the elements of these arrays are defined in the routine getinoquota;
 * the remainder of the quota code treats them generically and need not be
 * inspected when changing the size of the array.
 */
#define	MAXQUOTAS	2
#define	USRQUOTA	0	/* element used for user quotas */
#define	GRPQUOTA	1	/* element used for group quotas */

/*
 * Definitions for the default names of the quotas files.
 */
#define INITQFNAMES { \
	"user",		/* USRQUOTA */ \
	"group",	/* GRPQUOTA */ \
	"undefined", \
}
#define	QUOTAFILENAME	"quota"
#define	QUOTAGROUP	"operator"

/*
 * Command definitions for the 'quotactl' system call.  The commands are
 * broken into a main command defined below and a subcommand that is used
 * to convey the type of quota that is being manipulated (see above).
 */
#define SUBCMDMASK	0x00ff
#define SUBCMDSHIFT	8
#define	QCMD(cmd, type)	(((cmd) << SUBCMDSHIFT) | ((type) & SUBCMDMASK))

#define	Q_QUOTAON	0x0100	/* enable quotas */
#define	Q_QUOTAOFF	0x0200	/* disable quotas */
#define	Q_GETQUOTA	0x0300	/* get limits and usage */
#define	Q_SETQUOTA	0x0400	/* set limits and usage */
#define	Q_SETUSE	0x0500	/* set usage */
#define	Q_SYNC		0x0600	/* sync disk copy of a filesystems quotas */

/*
 * The following structure defines the format of the disk quota file
 * (as it appears on disk) - the file is an array of these structures
 * indexed by user or group number.  The setquota system call establishes
 * the vnode for each quota file (a pointer is retained in the ufsmount
 * structure).
 */
struct dqblk {
	u_int32_t dqb_bhardlimit;	/* absolute limit on disk blks alloc */
	u_int32_t dqb_bsoftlimit;	/* preferred limit on disk blks */
	u_int32_t dqb_curblocks;	/* current block count */
	u_int32_t dqb_ihardlimit;	/* maximum # allocated inodes + 1 */
	u_int32_t dqb_isoftlimit;	/* preferred inode limit */
	u_int32_t dqb_curinodes;	/* current # allocated inodes */
					/* XXX 2038 */
	u_int32_t dqb_btime;		/* time limit for excessive disk use */
	u_int32_t dqb_itime;		/* time limit for excessive files */
};

#ifdef _KERNEL
/*
 * Flags to ufs_quota_{alloc,free}_{blocks,inode}2
 */
enum ufs_quota_flags {
	UFS_QUOTA_NOUID = 0x1,		/* Don't change UID quota */
	UFS_QUOTA_NOGID = 0x2,		/* Don't change GID quota */
	UFS_QUOTA_FORCE = 0x1000	/* don't check limits - just change it */
};     /* Change GID */

struct dquot;
struct inode;
struct mount;
struct proc;
struct ucred;
struct ufsmount;
struct vnode;
__BEGIN_DECLS
#define ufs_quota_alloc_blocks(i, c, cr) ufs_quota_alloc_blocks2(i, c, cr, 0)
#define ufs_quota_free_blocks(i, c, cr) ufs_quota_free_blocks2(i, c, cr, 0)
#define ufs_quota_alloc_inode(i, cr) ufs_quota_alloc_inode2(i, cr, 0)
#define ufs_quota_free_inode(i, cr) ufs_quota_free_inode2(i, cr, 0)
int     ufs_quota_alloc_blocks2(struct inode *, daddr_t, struct ucred *, enum ufs_quota_flags);
int     ufs_quota_free_blocks2(struct inode *, daddr_t, struct ucred *, enum ufs_quota_flags);
int     ufs_quota_alloc_inode2(struct inode *, struct ucred *, enum ufs_quota_flags);
int     ufs_quota_free_inode2(struct inode *, struct ucred *, enum ufs_quota_flags);

int     ufs_quota_delete(struct inode *);

int	getinoquota(struct inode *);
int	quotaoff(struct proc *, struct mount *, int);
int	qsync(struct mount *mp);
int	ufs_quotactl(struct mount *, int, uid_t, caddr_t, struct proc *);

void    ufs_quota_init(void);

__END_DECLS
#endif /* _KERNEL */

#endif /* _QUOTA_ */
