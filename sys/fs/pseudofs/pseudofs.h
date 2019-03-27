/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *      $FreeBSD$
 */

#ifndef _PSEUDOFS_H_INCLUDED
#define _PSEUDOFS_H_INCLUDED

#include <sys/jail.h>

/*
 * Opaque structures
 */
struct mntarg;
struct mount;
struct nameidata;
struct proc;
struct sbuf;
struct statfs;
struct thread;
struct uio;
struct vfsconf;
struct vnode;

/*
 * Limits and constants
 */
#define PFS_NAMELEN		128
#define PFS_FSNAMELEN		16	/* equal to MFSNAMELEN */
#define PFS_DELEN		(offsetof(struct dirent, d_name) + PFS_NAMELEN)

typedef enum {
	pfstype_none = 0,
	pfstype_root,
	pfstype_dir,
	pfstype_this,
	pfstype_parent,
	pfstype_file,
	pfstype_symlink,
	pfstype_procdir
} pfs_type_t;

/*
 * Flags
 */
#define PFS_RD		0x0001	/* readable */
#define PFS_WR		0x0002	/* writeable */
#define PFS_RDWR	(PFS_RD|PFS_WR)
#define PFS_RAWRD	0x0004	/* raw reader */
#define	PFS_RAWWR	0x0008	/* raw writer */
#define PFS_RAW		(PFS_RAWRD|PFS_RAWWR)
#define PFS_PROCDEP	0x0010	/* process-dependent */
#define PFS_NOWAIT	0x0020 /* allow malloc to fail */

/*
 * Data structures
 */
struct pfs_info;
struct pfs_node;

/*
 * Init / uninit callback
 */
#define PFS_INIT_ARGS \
	struct pfs_info *pi, struct vfsconf *vfc
#define PFS_INIT_ARGNAMES \
	pi, vfc
#define PFS_INIT_PROTO(name) \
	int name(PFS_INIT_ARGS);
typedef int (*pfs_init_t)(PFS_INIT_ARGS);

/*
 * Filler callback
 * Called with proc held but unlocked
 */
#define PFS_FILL_ARGS \
	struct thread *td, struct proc *p, struct pfs_node *pn, \
	struct sbuf *sb, struct uio *uio
#define PFS_FILL_ARGNAMES \
	td, p, pn, sb, uio
#define PFS_FILL_PROTO(name) \
	int name(PFS_FILL_ARGS);
typedef int (*pfs_fill_t)(PFS_FILL_ARGS);

/*
 * Attribute callback
 * Called with proc locked
 */
struct vattr;
#define PFS_ATTR_ARGS \
	struct thread *td, struct proc *p, struct pfs_node *pn, \
	struct vattr *vap
#define PFS_ATTR_ARGNAMES \
	td, p, pn, vap
#define PFS_ATTR_PROTO(name) \
	int name(PFS_ATTR_ARGS);
typedef int (*pfs_attr_t)(PFS_ATTR_ARGS);

/*
 * Visibility callback
 * Called with proc locked
 */
#define PFS_VIS_ARGS \
	struct thread *td, struct proc *p, struct pfs_node *pn
#define PFS_VIS_ARGNAMES \
	td, p, pn
#define PFS_VIS_PROTO(name) \
	int name(PFS_VIS_ARGS);
typedef int (*pfs_vis_t)(PFS_VIS_ARGS);

/*
 * Ioctl callback
 * Called with proc locked
 */
#define PFS_IOCTL_ARGS \
	struct thread *td, struct proc *p, struct pfs_node *pn, \
	unsigned long cmd, void *data
#define PFS_IOCTL_ARGNAMES \
	td, p, pn, cmd, data
#define PFS_IOCTL_PROTO(name) \
	int name(PFS_IOCTL_ARGS);
typedef int (*pfs_ioctl_t)(PFS_IOCTL_ARGS);

/*
 * Getextattr callback
 * Called with proc locked
 */
#define PFS_GETEXTATTR_ARGS \
	struct thread *td, struct proc *p, struct pfs_node *pn, \
	int attrnamespace, const char *name, struct uio *uio,	\
	size_t *size, struct ucred *cred
#define PFS_GETEXTATTR_ARGNAMES \
	td, p, pn, attrnamespace, name, uio, size, cred
#define PFS_GETEXTATTR_PROTO(name) \
	int name(PFS_GETEXTATTR_ARGS);
struct ucred;
typedef int (*pfs_getextattr_t)(PFS_GETEXTATTR_ARGS);

/*
 * Last-close callback
 * Called with proc locked
 */
#define PFS_CLOSE_ARGS \
	struct thread *td, struct proc *p, struct pfs_node *pn
#define PFS_CLOSE_ARGNAMES \
	td, p, pn
#define PFS_CLOSE_PROTO(name) \
	int name(PFS_CLOSE_ARGS);
typedef int (*pfs_close_t)(PFS_CLOSE_ARGS);

/*
 * Destroy callback
 */
#define PFS_DESTROY_ARGS \
	struct pfs_node *pn
#define PFS_DESTROY_ARGNAMES \
	pn
#define PFS_DESTROY_PROTO(name) \
	int name(PFS_DESTROY_ARGS);
typedef int (*pfs_destroy_t)(PFS_DESTROY_ARGS);

/*
 * pfs_info: describes a pseudofs instance
 *
 * The pi_mutex is only used to avoid using the global subr_unit lock
 * for unrhdr.  The rest of struct pfs_info is only modified during
 * vfs_init() and vfs_uninit() of the consumer filesystem.
 */
struct pfs_info {
	char			 pi_name[PFS_FSNAMELEN];
	pfs_init_t		 pi_init;
	pfs_init_t		 pi_uninit;

	/* members below this line are initialized at run time */
	struct pfs_node		*pi_root;
	struct mtx		 pi_mutex;
	struct unrhdr		*pi_unrhdr;
};

/*
 * pfs_node: describes a node (file or directory) within a pseudofs
 *
 * - Fields marked (o) are protected by the node's own mutex.
 * - Fields marked (p) are protected by the node's parent's mutex.
 * - Remaining fields are not protected by any lock and are assumed to be
 *   immutable once the node has been created.
 *
 * To prevent deadlocks, if a node's mutex is to be held at the same time
 * as its parent's (e.g. when adding or removing nodes to a directory),
 * the parent's mutex must always be acquired first.  Unfortunately, this
 * is not enforcable by WITNESS.
 */
struct pfs_node {
	char			 pn_name[PFS_NAMELEN];
	pfs_type_t		 pn_type;
	int			 pn_flags;
	struct mtx		 pn_mutex;
	void			*pn_data;		/* (o) */

	pfs_fill_t		 pn_fill;
	pfs_ioctl_t		 pn_ioctl;
	pfs_close_t		 pn_close;
	pfs_attr_t		 pn_attr;
	pfs_vis_t		 pn_vis;
	pfs_getextattr_t	 pn_getextattr;
	pfs_destroy_t		 pn_destroy;

	struct pfs_info		*pn_info;
	u_int32_t		 pn_fileno;		/* (o) */

	struct pfs_node		*pn_parent;		/* (o) */
	struct pfs_node		*pn_nodes;		/* (o) */
	struct pfs_node		*pn_next;		/* (p) */
};

/*
 * VFS interface
 */
int		 pfs_mount	(struct pfs_info *pi, struct mount *mp);
int		 pfs_cmount	(struct mntarg *ma, void *data, uint64_t flags);
int		 pfs_unmount	(struct mount *mp, int mntflags);
int		 pfs_root	(struct mount *mp, int flags,
				 struct vnode **vpp);
int		 pfs_statfs	(struct mount *mp, struct statfs *sbp);
int		 pfs_init	(struct pfs_info *pi, struct vfsconf *vfc);
int		 pfs_uninit	(struct pfs_info *pi, struct vfsconf *vfc);

/*
 * Directory structure construction and manipulation
 */
struct pfs_node	*pfs_create_dir	(struct pfs_node *parent, const char *name,
				 pfs_attr_t attr, pfs_vis_t vis,
				 pfs_destroy_t destroy, int flags);
struct pfs_node	*pfs_create_file(struct pfs_node *parent, const char *name,
				 pfs_fill_t fill, pfs_attr_t attr,
				 pfs_vis_t vis, pfs_destroy_t destroy,
				 int flags);
struct pfs_node	*pfs_create_link(struct pfs_node *parent, const char *name,
				 pfs_fill_t fill, pfs_attr_t attr,
				 pfs_vis_t vis, pfs_destroy_t destroy,
				 int flags);
struct pfs_node	*pfs_find_node	(struct pfs_node *parent, const char *name);
void		 pfs_purge	(struct pfs_node *pn);
int		 pfs_destroy	(struct pfs_node *pn);

/*
 * Now for some initialization magic...
 */
#define PSEUDOFS(name, version, flags)					\
									\
static struct pfs_info name##_info = {					\
	#name,								\
	name##_init,							\
	name##_uninit,							\
};									\
									\
static int								\
_##name##_mount(struct mount *mp) {					\
	return (pfs_mount(&name##_info, mp));				\
}									\
									\
static int								\
_##name##_init(struct vfsconf *vfc) {					\
	return (pfs_init(&name##_info, vfc));				\
}									\
									\
static int								\
_##name##_uninit(struct vfsconf *vfc) {					\
	return (pfs_uninit(&name##_info, vfc));				\
}									\
									\
static struct vfsops name##_vfsops = {					\
	.vfs_cmount =		pfs_cmount,				\
	.vfs_init =		_##name##_init,				\
	.vfs_mount =		_##name##_mount,			\
	.vfs_root =		pfs_root,				\
	.vfs_statfs =		pfs_statfs,				\
	.vfs_uninit =		_##name##_uninit,			\
	.vfs_unmount =		pfs_unmount,				\
};									\
VFS_SET(name##_vfsops, name, VFCF_SYNTHETIC | flags);			\
MODULE_VERSION(name, version);						\
MODULE_DEPEND(name, pseudofs, 1, 1, 1);

#endif
