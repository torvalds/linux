/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 * $FreeBSD$
 */

#ifndef _UFS_UFS_QUOTA_H_
#define	_UFS_UFS_QUOTA_H_

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
#define	INITQFNAMES { \
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
#define	SUBCMDMASK	0x00ff
#define	SUBCMDSHIFT	8
#define	QCMD(cmd, type)	(((cmd) << SUBCMDSHIFT) | ((type) & SUBCMDMASK))

#define	Q_QUOTAON	0x0100	/* enable quotas */
#define	Q_QUOTAOFF	0x0200	/* disable quotas */
#define	Q_GETQUOTA32	0x0300	/* get limits and usage (32-bit version) */
#define	Q_SETQUOTA32	0x0400	/* set limits and usage (32-bit version) */
#define	Q_SETUSE32	0x0500	/* set usage (32-bit version) */
#define	Q_SYNC		0x0600	/* sync disk copy of a filesystems quotas */
#define	Q_GETQUOTA	0x0700	/* get limits and usage (64-bit version) */
#define	Q_SETQUOTA	0x0800	/* set limits and usage (64-bit version) */
#define	Q_SETUSE	0x0900	/* set usage (64-bit version) */
#define	Q_GETQUOTASIZE	0x0A00	/* get bit-size of quota file fields */

/*
 * The following structure defines the format of the disk quota file
 * (as it appears on disk) - the file is an array of these structures
 * indexed by user or group number.  The setquota system call establishes
 * the vnode for each quota file (a pointer is retained in the ufsmount
 * structure).
 */
struct dqblk32 {
	u_int32_t dqb_bhardlimit;	/* absolute limit on disk blks alloc */
	u_int32_t dqb_bsoftlimit;	/* preferred limit on disk blks */
	u_int32_t dqb_curblocks;	/* current block count */
	u_int32_t dqb_ihardlimit;	/* maximum # allocated inodes + 1 */
	u_int32_t dqb_isoftlimit;	/* preferred inode limit */
	u_int32_t dqb_curinodes;	/* current # allocated inodes */
	int32_t   dqb_btime;		/* time limit for excessive disk use */
	int32_t   dqb_itime;		/* time limit for excessive files */
};

struct dqblk64 {
	u_int64_t dqb_bhardlimit;	/* absolute limit on disk blks alloc */
	u_int64_t dqb_bsoftlimit;	/* preferred limit on disk blks */
	u_int64_t dqb_curblocks;	/* current block count */
	u_int64_t dqb_ihardlimit;	/* maximum # allocated inodes + 1 */
	u_int64_t dqb_isoftlimit;	/* preferred inode limit */
	u_int64_t dqb_curinodes;	/* current # allocated inodes */
	int64_t   dqb_btime;		/* time limit for excessive disk use */
	int64_t   dqb_itime;		/* time limit for excessive files */
};

#define	dqblk dqblk64

#define	Q_DQHDR64_MAGIC "QUOTA64"
#define	Q_DQHDR64_VERSION 0x20081104

struct dqhdr64 {
	char	  dqh_magic[8];		/* Q_DQHDR64_MAGIC */
	uint32_t  dqh_version;		/* Q_DQHDR64_VERSION */
	uint32_t  dqh_hdrlen;		/* header length */
	uint32_t  dqh_reclen;		/* record length */
	char	  dqh_unused[44];	/* reserved for future extension */
};

#ifdef _KERNEL

#include <sys/queue.h>

/*
 * The following structure records disk usage for a user or group on a
 * filesystem. There is one allocated for each quota that exists on any
 * filesystem for the current user or group. A cache is kept of recently
 * used entries.
 * (h) protected by dqhlock
 */
struct dquot {
	LIST_ENTRY(dquot) dq_hash;	/* (h) hash list */
	TAILQ_ENTRY(dquot) dq_freelist;	/* (h) free list */
	struct mtx dq_lock;		/* lock for concurrency */
	u_int16_t dq_flags;		/* flags, see below */
	u_int16_t dq_type;		/* quota type of this dquot */
	u_int32_t dq_cnt;		/* (h) count of active references */
	u_int32_t dq_id;		/* identifier this applies to */
	struct ufsmount *dq_ump;	/* (h) filesystem that this is
					   taken from */
	struct dqblk64 dq_dqb;		/* actual usage & quotas */
};
/*
 * Flag values.
 */
#define	DQ_LOCK		0x01		/* this quota locked (no MODS) */
#define	DQ_WANT		0x02		/* wakeup on unlock */
#define	DQ_MOD		0x04		/* this quota modified since read */
#define	DQ_FAKE		0x08		/* no limits here, just usage */
#define	DQ_BLKS		0x10		/* has been warned about blk limit */
#define	DQ_INODS	0x20		/* has been warned about inode limit */
/*
 * Shorthand notation.
 */
#define	dq_bhardlimit	dq_dqb.dqb_bhardlimit
#define	dq_bsoftlimit	dq_dqb.dqb_bsoftlimit
#define	dq_curblocks	dq_dqb.dqb_curblocks
#define	dq_ihardlimit	dq_dqb.dqb_ihardlimit
#define	dq_isoftlimit	dq_dqb.dqb_isoftlimit
#define	dq_curinodes	dq_dqb.dqb_curinodes
#define	dq_btime	dq_dqb.dqb_btime
#define	dq_itime	dq_dqb.dqb_itime

/*
 * If the system has never checked for a quota for this file, then it is
 * set to NODQUOT.  Once a write attempt is made the inode pointer is set
 * to reference a dquot structure.
 */
#define	NODQUOT		NULL

/*
 * Flags to chkdq() and chkiq()
 */
#define	FORCE	0x01	/* force usage changes independent of limits */
#define	CHOWN	0x02	/* (advisory) change initiated by chown */

/*
 * Macros to avoid subroutine calls to trivial functions.
 */
#ifdef DIAGNOSTIC
#define	DQREF(dq)	dqref(dq)
#else
#define	DQREF(dq)	(dq)->dq_cnt++
#endif

#define	DQI_LOCK(dq)	mtx_lock(&(dq)->dq_lock)
#define	DQI_UNLOCK(dq)	mtx_unlock(&(dq)->dq_lock)

#define	DQI_WAIT(dq, prio, msg) do {		\
	while ((dq)->dq_flags & DQ_LOCK) {	\
		(dq)->dq_flags |= DQ_WANT;	\
		(void) msleep((dq),		\
		    &(dq)->dq_lock, (prio), (msg), 0); \
	}					\
} while (0)

#define	DQI_WAKEUP(dq) do {			\
	if ((dq)->dq_flags & DQ_WANT)		\
		wakeup((dq));			\
	(dq)->dq_flags &= ~(DQ_WANT|DQ_LOCK);	\
} while (0)

struct inode;
struct mount;
struct thread;
struct ucred;
struct vnode;

int	chkdq(struct inode *, int64_t, struct ucred *, int);
int	chkiq(struct inode *, int, struct ucred *, int);
void	dqinit(void);
void	dqrele(struct vnode *, struct dquot *);
void	dquninit(void);
int	getinoquota(struct inode *);
int	qsync(struct mount *);
int	qsyncvp(struct vnode *);
int	quotaoff(struct thread *, struct mount *, int);
int	quotaon(struct thread *, struct mount *, int, void *);
int	getquota32(struct thread *, struct mount *, u_long, int, void *);
int	setquota32(struct thread *, struct mount *, u_long, int, void *);
int	setuse32(struct thread *, struct mount *, u_long, int, void *);
int	getquota(struct thread *, struct mount *, u_long, int, void *);
int	setquota(struct thread *, struct mount *, u_long, int, void *);
int	setuse(struct thread *, struct mount *, u_long, int, void *);
int	getquotasize(struct thread *, struct mount *, u_long, int, void *);
vfs_quotactl_t ufs_quotactl;

#ifdef SOFTUPDATES
int	quotaref(struct vnode *, struct dquot **);
void	quotarele(struct dquot **);
void	quotaadj(struct dquot **, struct ufsmount *, int64_t);
#endif /* SOFTUPDATES */

#else /* !_KERNEL */

#include <sys/cdefs.h>

__BEGIN_DECLS
int	quotactl(const char *, int, int, void *);
__END_DECLS

#endif /* _KERNEL */

#endif /* !_UFS_UFS_QUOTA_H_ */
