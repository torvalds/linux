/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1994 The Regents of the University of California.
 * Copyright (c) 1994 Jan-Simon Pendry.
 * Copyright (c) 2005, 2006 Masanori Ozawa <ozawa@ongs.co.jp>, ONGS Inc.
 * Copyright (c) 2006 Daichi Goto <daichi@freebsd.org>
 * All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
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
 *	@(#)union.h	8.9 (Berkeley) 12/10/94
 * $FreeBSD$
 */

#ifdef _KERNEL

/* copy method of attr from lower to upper */
typedef enum _unionfs_copymode {
	UNIONFS_TRADITIONAL = 0,
	UNIONFS_TRANSPARENT,
	UNIONFS_MASQUERADE
} unionfs_copymode;

/* whiteout policy of upper layer */
typedef enum _unionfs_whitemode {
       UNIONFS_WHITE_ALWAYS = 0,
       UNIONFS_WHITE_WHENNEEDED
} unionfs_whitemode;

struct unionfs_mount {
	struct vnode   *um_lowervp;	/* VREFed once */
	struct vnode   *um_uppervp;	/* VREFed once */
	struct vnode   *um_rootvp;	/* ROOT vnode */
	unionfs_copymode um_copymode;
	unionfs_whitemode um_whitemode;
	uid_t		um_uid;
	gid_t		um_gid;
	u_short		um_udir;
	u_short		um_ufile;
};

/* unionfs status list */
struct unionfs_node_status {
	LIST_ENTRY(unionfs_node_status) uns_list;	/* Status list */
	pid_t		uns_pid;		/* current process id */
	int		uns_node_flag;		/* uns flag */
	int		uns_lower_opencnt;	/* open count of lower */
	int		uns_upper_opencnt;	/* open count of upper */
	int		uns_lower_openmode;	/* open mode of lower */
	int		uns_readdir_status;	/* read status of readdir */
};

/* union node status flags */
#define	UNS_OPENL_4_READDIR	0x01	/* open lower layer for readdir */

/* A cache of vnode references */
struct unionfs_node {
	struct vnode   *un_lowervp;		/* lower side vnode */
	struct vnode   *un_uppervp;		/* upper side vnode */
	struct vnode   *un_dvp;			/* parent unionfs vnode */
	struct vnode   *un_vnode;		/* Back pointer */
	LIST_HEAD(, unionfs_node_status) un_unshead;
						/* unionfs status head */
	LIST_HEAD(unionfs_node_hashhead, unionfs_node) *un_hashtbl;
						/* dir vnode hash table */
	LIST_ENTRY(unionfs_node)   un_hash;	/* hash list entry */
	u_long		un_hashmask;		/* bit mask */
	char           *un_path;		/* path */
	int		un_flag;		/* unionfs node flag */
};

/*
 * unionfs node flags
 * It needs the vnode with exclusive lock, when changing the un_flag variable.
 */
#define UNIONFS_OPENEXTL	0x01	/* openextattr (lower) */
#define UNIONFS_OPENEXTU	0x02	/* openextattr (upper) */

#define	MOUNTTOUNIONFSMOUNT(mp) ((struct unionfs_mount *)((mp)->mnt_data))
#define	VTOUNIONFS(vp) ((struct unionfs_node *)(vp)->v_data)
#define	UNIONFSTOV(xp) ((xp)->un_vnode)

int unionfs_init(struct vfsconf *vfsp);
int unionfs_uninit(struct vfsconf *vfsp);
int unionfs_nodeget(struct mount *mp, struct vnode *uppervp, struct vnode *lowervp, struct vnode *dvp, struct vnode **vpp, struct componentname *cnp, struct thread *td);
void unionfs_noderem(struct vnode *vp, struct thread *td);
void unionfs_get_node_status(struct unionfs_node *unp, struct thread *td, struct unionfs_node_status **unspp);
void unionfs_tryrem_node_status(struct unionfs_node *unp, struct unionfs_node_status *unsp);

int unionfs_check_rmdir(struct vnode *vp, struct ucred *cred, struct thread *td);
int unionfs_copyfile(struct unionfs_node *unp, int docopy, struct ucred *cred, struct thread *td);
void unionfs_create_uppervattr_core(struct unionfs_mount *ump, struct vattr *lva, struct vattr *uva, struct thread *td);
int unionfs_create_uppervattr(struct unionfs_mount *ump, struct vnode *lvp, struct vattr *uva, struct ucred *cred, struct thread *td);
int unionfs_mkshadowdir(struct unionfs_mount *ump, struct vnode *duvp, struct unionfs_node *unp, struct componentname *cnp, struct thread *td);
int unionfs_mkwhiteout(struct vnode *dvp, struct componentname *cnp, struct thread *td, char *path);
int unionfs_relookup(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp, struct componentname *cn, struct thread *td, char *path, int pathlen, u_long nameiop);
int unionfs_relookup_for_create(struct vnode *dvp, struct componentname *cnp, struct thread *td);
int unionfs_relookup_for_delete(struct vnode *dvp, struct componentname *cnp, struct thread *td);
int unionfs_relookup_for_rename(struct vnode *dvp, struct componentname *cnp, struct thread *td);

#ifdef DIAGNOSTIC
struct vnode   *unionfs_checklowervp(struct vnode *vp, char *fil, int lno);
struct vnode   *unionfs_checkuppervp(struct vnode *vp, char *fil, int lno);
#define	UNIONFSVPTOLOWERVP(vp) unionfs_checklowervp((vp), __FILE__, __LINE__)
#define	UNIONFSVPTOUPPERVP(vp) unionfs_checkuppervp((vp), __FILE__, __LINE__)
#else
#define	UNIONFSVPTOLOWERVP(vp) (VTOUNIONFS(vp)->un_lowervp)
#define	UNIONFSVPTOUPPERVP(vp) (VTOUNIONFS(vp)->un_uppervp)
#endif

extern struct vop_vector unionfs_vnodeops;

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_UNIONFSNODE);
MALLOC_DECLARE(M_UNIONFSPATH);
#endif

#ifdef UNIONFS_DEBUG
#define UNIONFSDEBUG(format, args...) printf(format ,## args)
#else
#define UNIONFSDEBUG(format, args...)
#endif				/* UNIONFS_DEBUG */

#endif				/* _KERNEL */
