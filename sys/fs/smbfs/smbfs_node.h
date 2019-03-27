/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
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
 *
 * $FreeBSD$
 */
#ifndef _FS_SMBFS_NODE_H_
#define _FS_SMBFS_NODE_H_

#define	SMBFS_ROOT_INO		2	/* just like in UFS */

/* Bits for smbnode.n_flag */
#define	NFLUSHINPROG		0x0001
#define	NFLUSHWANT		0x0002	/* they should gone ... */
#define	NMODIFIED		0x0004	/* bogus, until async IO implemented */
/*efine	NNEW			0x0008*//* smb/vnode has been allocated */
#define	NREFPARENT		0x0010	/* node holds parent from recycling */
#define	NFLUSHWIRE		0x1000	/* pending flush request */
#define	NOPEN			0x2000	/* file is open */
#define	NGONE			0x4000	/* file has been removed/renamed */

struct smbfs_fctx;

struct smbnode {
	int			n_flag;
	struct vnode *		n_parent;
	struct vnode *		n_vnode;
	struct smbmount *	n_mount;
	time_t			n_attrage;	/* attributes cache time */
/*	time_t			n_ctime;*/
	struct timespec		n_mtime;	/* modify time */
	struct timespec		n_atime;	/* last access time */
	u_quad_t		n_size;
	long			n_ino;
	long			n_parentino;	/* parent inode number */
	int			n_dosattr;
	u_int16_t		n_fid;		/* file handle */
	int			n_rwstate;	/* granted access mode */
	int			n_rplen;
	char *			n_rpath;
	u_char			n_nmlen;
	u_char *		n_name;
	struct smbfs_fctx *	n_dirseq;	/* ff context */
	long			n_dirofs;	/* last ff offset */
	LIST_ENTRY(smbnode)	n_hash;
};

struct smbcmp {
	struct vnode * 		n_parent;
	int 			n_nmlen;
	const char *		n_name;
};

#define VTOSMB(vp)	((struct smbnode *)(vp)->v_data)
#define SMBTOV(np)	((struct vnode *)(np)->n_vnode)

#define	SMBFS_DNP_SEP(dnp)	((dnp->n_rplen > 1) ? '\\' : '\0')

struct vop_getpages_args;
struct vop_inactive_args;
struct vop_putpages_args;
struct vop_reclaim_args;
struct ucred;
struct uio;
struct smbfattr;

int  smbfs_inactive(struct vop_inactive_args *);
int  smbfs_reclaim(struct vop_reclaim_args *);
int smbfs_nget(struct mount *mp, struct vnode *dvp, const char *name, int nmlen,
	struct smbfattr *fap, struct vnode **vpp);
u_int32_t smbfs_hash(const u_char *name, int nmlen);

int  smbfs_getpages(struct vop_getpages_args *);
int  smbfs_putpages(struct vop_putpages_args *);
int  smbfs_readvnode(struct vnode *vp, struct uio *uiop, struct ucred *cred);
int  smbfs_writevnode(struct vnode *vp, struct uio *uiop, struct ucred *cred, int ioflag);
void smbfs_attr_cacheenter(struct vnode *vp, struct smbfattr *fap);
int  smbfs_attr_cachelookup(struct vnode *vp ,struct vattr *va);

#define smbfs_attr_cacheremove(vp)	VTOSMB(vp)->n_attrage = 0

#endif /* _FS_SMBFS_NODE_H_ */
