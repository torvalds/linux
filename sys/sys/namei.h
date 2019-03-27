/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1985, 1989, 1991, 1993
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
 *	@(#)namei.h	8.5 (Berkeley) 1/9/95
 * $FreeBSD$
 */

#ifndef _SYS_NAMEI_H_
#define	_SYS_NAMEI_H_

#include <sys/caprights.h>
#include <sys/filedesc.h>
#include <sys/queue.h>
#include <sys/_uio.h>

struct componentname {
	/*
	 * Arguments to lookup.
	 */
	u_long	cn_nameiop;	/* namei operation */
	u_int64_t cn_flags;	/* flags to namei */
	struct	thread *cn_thread;/* thread requesting lookup */
	struct	ucred *cn_cred;	/* credentials */
	int	cn_lkflags;	/* Lock flags LK_EXCLUSIVE or LK_SHARED */
	/*
	 * Shared between lookup and commit routines.
	 */
	char	*cn_pnbuf;	/* pathname buffer */
	char	*cn_nameptr;	/* pointer to looked up name */
	long	cn_namelen;	/* length of looked up component */
};

struct nameicap_tracker;
TAILQ_HEAD(nameicap_tracker_head, nameicap_tracker);

/*
 * Encapsulation of namei parameters.
 */
struct nameidata {
	/*
	 * Arguments to namei/lookup.
	 */
	const	char *ni_dirp;		/* pathname pointer */
	enum	uio_seg ni_segflg;	/* location of pathname */
	cap_rights_t ni_rightsneeded;	/* rights required to look up vnode */
	/*
	 * Arguments to lookup.
	 */
	struct  vnode *ni_startdir;	/* starting directory */
	struct	vnode *ni_rootdir;	/* logical root directory */
	struct	vnode *ni_topdir;	/* logical top directory */
	int	ni_dirfd;		/* starting directory for *at functions */
	int	ni_lcf;			/* local call flags */
	/*
	 * Results: returned from namei
	 */
	struct filecaps ni_filecaps;	/* rights the *at base has */
	/*
	 * Results: returned from/manipulated by lookup
	 */
	struct	vnode *ni_vp;		/* vnode of result */
	struct	vnode *ni_dvp;		/* vnode of intermediate directory */
	/*
	 * Results: flags returned from namei
	 */
	u_int	ni_resflags;
	/*
	 * Shared between namei and lookup/commit routines.
	 */
	size_t	ni_pathlen;		/* remaining chars in path */
	char	*ni_next;		/* next location in pathname */
	u_int	ni_loopcnt;		/* count of symlinks encountered */
	/*
	 * Lookup parameters: this structure describes the subset of
	 * information from the nameidata structure that is passed
	 * through the VOP interface.
	 */
	struct componentname ni_cnd;
	struct nameicap_tracker_head ni_cap_tracker;
	struct vnode *ni_beneath_latch;
};

#ifdef _KERNEL
/*
 * namei operations
 */
#define	LOOKUP		0	/* perform name lookup only */
#define	CREATE		1	/* setup for file creation */
#define	DELETE		2	/* setup for file deletion */
#define	RENAME		3	/* setup for file renaming */
#define	OPMASK		3	/* mask for operation */
/*
 * namei operational modifier flags, stored in ni_cnd.flags
 */
#define	LOCKLEAF	0x0004	/* lock vnode on return */
#define	LOCKPARENT	0x0008	/* want parent vnode returned locked */
#define	WANTPARENT	0x0010	/* want parent vnode returned unlocked */
#define	NOCACHE		0x0020	/* name must not be left in cache */
#define	FOLLOW		0x0040	/* follow symbolic links */
#define	BENEATH		0x0080	/* No escape from the start dir */
#define	LOCKSHARED	0x0100	/* Shared lock leaf */
#define	NOFOLLOW	0x0000	/* do not follow symbolic links (pseudo) */
#define	MODMASK		0x01fc	/* mask of operational modifiers */
/*
 * Namei parameter descriptors.
 *
 * SAVENAME may be set by either the callers of namei or by VOP_LOOKUP.
 * If the caller of namei sets the flag (for example execve wants to
 * know the name of the program that is being executed), then it must
 * free the buffer. If VOP_LOOKUP sets the flag, then the buffer must
 * be freed by either the commit routine or the VOP_ABORT routine.
 * SAVESTART is set only by the callers of namei. It implies SAVENAME
 * plus the addition of saving the parent directory that contains the
 * name in ni_startdir. It allows repeated calls to lookup for the
 * name being sought. The caller is responsible for releasing the
 * buffer and for vrele'ing ni_startdir.
 */
#define	RDONLY		0x00000200 /* lookup with read-only semantics */
#define	HASBUF		0x00000400 /* has allocated pathname buffer */
#define	SAVENAME	0x00000800 /* save pathname buffer */
#define	SAVESTART	0x00001000 /* save starting directory */
#define	ISDOTDOT	0x00002000 /* current component name is .. */
#define	MAKEENTRY	0x00004000 /* entry is to be added to name cache */
#define	ISLASTCN	0x00008000 /* this is last component of pathname */
#define	ISSYMLINK	0x00010000 /* symlink needs interpretation */
#define	ISWHITEOUT	0x00020000 /* found whiteout */
#define	DOWHITEOUT	0x00040000 /* do whiteouts */
#define	WILLBEDIR	0x00080000 /* new files will be dirs; allow trailing / */
#define	ISUNICODE	0x00100000 /* current component name is unicode*/
#define	ISOPEN		0x00200000 /* caller is opening; return a real vnode. */
#define	NOCROSSMOUNT	0x00400000 /* do not cross mount points */
#define	NOMACCHECK	0x00800000 /* do not perform MAC checks */
#define	AUDITVNODE1	0x04000000 /* audit the looked up vnode information */
#define	AUDITVNODE2	0x08000000 /* audit the looked up vnode information */
#define	TRAILINGSLASH	0x10000000 /* path ended in a slash */
#define	NOCAPCHECK	0x20000000 /* do not perform capability checks */
#define	PARAMASK	0x3ffffe00 /* mask of parameter descriptors */

/*
 * Namei results flags
 */
#define	NIRES_ABS	0x00000001 /* Path was absolute */

/*
 * Flags in ni_lcf, valid for the duration of the namei call.
 */
#define	NI_LCF_STRICTRELATIVE	0x0001	/* relative lookup only */
#define	NI_LCF_CAP_DOTDOT	0x0002	/* ".." in strictrelative case */
#define	NI_LCF_BENEATH_ABS	0x0004	/* BENEATH with absolute path */
#define	NI_LCF_BENEATH_LATCHED	0x0008	/* BENEATH_ABS traversed starting dir */
#define	NI_LCF_LATCH		0x0010	/* ni_beneath_latch valid */

/*
 * Initialization of a nameidata structure.
 */
#define	NDINIT(ndp, op, flags, segflg, namep, td)			\
	NDINIT_ALL(ndp, op, flags, segflg, namep, AT_FDCWD, NULL, 0, td)
#define	NDINIT_AT(ndp, op, flags, segflg, namep, dirfd, td)		\
	NDINIT_ALL(ndp, op, flags, segflg, namep, dirfd, NULL, 0, td)
#define	NDINIT_ATRIGHTS(ndp, op, flags, segflg, namep, dirfd, rightsp, td) \
	NDINIT_ALL(ndp, op, flags, segflg, namep, dirfd, NULL, rightsp, td)
#define	NDINIT_ATVP(ndp, op, flags, segflg, namep, vp, td)		\
	NDINIT_ALL(ndp, op, flags, segflg, namep, AT_FDCWD, vp, 0, td)

void NDINIT_ALL(struct nameidata *ndp, u_long op, u_long flags,
    enum uio_seg segflg, const char *namep, int dirfd, struct vnode *startdir,
    cap_rights_t *rightsp, struct thread *td);

#define NDF_NO_DVP_RELE		0x00000001
#define NDF_NO_DVP_UNLOCK	0x00000002
#define NDF_NO_DVP_PUT		0x00000003
#define NDF_NO_VP_RELE		0x00000004
#define NDF_NO_VP_UNLOCK	0x00000008
#define NDF_NO_VP_PUT		0x0000000c
#define NDF_NO_STARTDIR_RELE	0x00000010
#define NDF_NO_FREE_PNBUF	0x00000020
#define NDF_ONLY_PNBUF		(~NDF_NO_FREE_PNBUF)

void NDFREE(struct nameidata *, const u_int);

int	namei(struct nameidata *ndp);
int	lookup(struct nameidata *ndp);
int	relookup(struct vnode *dvp, struct vnode **vpp,
	    struct componentname *cnp);
#endif

/*
 * Stats on usefulness of namei caches.
 */
struct nchstats {
	long	ncs_goodhits;		/* hits that we can really use */
	long	ncs_neghits;		/* negative hits that we can use */
	long	ncs_badhits;		/* hits we must drop */
	long	ncs_falsehits;		/* hits with id mismatch */
	long	ncs_miss;		/* misses */
	long	ncs_long;		/* long names that ignore cache */
	long	ncs_pass2;		/* names found with passes == 2 */
	long	ncs_2passes;		/* number of times we attempt it */
};

extern struct nchstats nchstats;

#endif /* !_SYS_NAMEI_H_ */
