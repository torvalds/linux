/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_VFS_H__
#define __XFS_VFS_H__

#include <linux/vfs.h>
#include "xfs_fs.h"

struct fid;
struct vfs;
struct cred;
struct vnode;
struct kstatfs;
struct seq_file;
struct super_block;
struct xfs_mount_args;

typedef struct kstatfs xfs_statfs_t;

typedef struct vfs_sync_work {
	struct list_head	w_list;
	struct vfs		*w_vfs;
	void			*w_data;	/* syncer routine argument */
	void			(*w_syncer)(struct vfs *, void *);
} vfs_sync_work_t;

typedef struct vfs {
	u_int			vfs_flag;	/* flags */
	xfs_fsid_t		vfs_fsid;	/* file system ID */
	xfs_fsid_t		*vfs_altfsid;	/* An ID fixed for life of FS */
	bhv_head_t		vfs_bh;		/* head of vfs behavior chain */
	struct super_block	*vfs_super;	/* generic superblock pointer */
	struct task_struct	*vfs_sync_task;	/* generalised sync thread */
	vfs_sync_work_t		vfs_sync_work;	/* work item for VFS_SYNC */
	struct list_head	vfs_sync_list;	/* sync thread work item list */
	spinlock_t		vfs_sync_lock;	/* work item list lock */
	int 			vfs_sync_seq;	/* sync thread generation no. */
	wait_queue_head_t	vfs_wait_single_sync_task;
} vfs_t;

#define vfs_fbhv		vfs_bh.bh_first	/* 1st on vfs behavior chain */

#define bhvtovfs(bdp)		( (struct vfs *)BHV_VOBJ(bdp) )
#define bhvtovfsops(bdp)	( (struct vfsops *)BHV_OPS(bdp) )
#define VFS_BHVHEAD(vfs)	( &(vfs)->vfs_bh )
#define VFS_REMOVEBHV(vfs, bdp)	( bhv_remove(VFS_BHVHEAD(vfs), bdp) )

#define VFS_POSITION_BASE	BHV_POSITION_BASE	/* chain bottom */
#define VFS_POSITION_TOP	BHV_POSITION_TOP	/* chain top */
#define VFS_POSITION_INVALID	BHV_POSITION_INVALID	/* invalid pos. num */

typedef enum {
	VFS_BHV_UNKNOWN,	/* not specified */
	VFS_BHV_XFS,		/* xfs */
	VFS_BHV_DM,		/* data migration */
	VFS_BHV_QM,		/* quota manager */
	VFS_BHV_IO,		/* IO path */
	VFS_BHV_END		/* housekeeping end-of-range */
} vfs_bhv_t;

#define VFS_POSITION_XFS	(BHV_POSITION_BASE)
#define VFS_POSITION_DM		(VFS_POSITION_BASE+10)
#define VFS_POSITION_QM		(VFS_POSITION_BASE+20)
#define VFS_POSITION_IO		(VFS_POSITION_BASE+30)

#define VFS_RDONLY		0x0001	/* read-only vfs */
#define VFS_GRPID		0x0002	/* group-ID assigned from directory */
#define VFS_DMI			0x0004	/* filesystem has the DMI enabled */
#define VFS_32BITINODES		0x0008	/* do not use inums above 32 bits */
#define VFS_END			0x0008	/* max flag */

#define SYNC_ATTR		0x0001	/* sync attributes */
#define SYNC_CLOSE		0x0002	/* close file system down */
#define SYNC_DELWRI		0x0004	/* look at delayed writes */
#define SYNC_WAIT		0x0008	/* wait for i/o to complete */
#define SYNC_BDFLUSH		0x0010	/* BDFLUSH is calling -- don't block */
#define SYNC_FSDATA		0x0020	/* flush fs data (e.g. superblocks) */
#define SYNC_REFCACHE		0x0040  /* prune some of the nfs ref cache */
#define SYNC_REMOUNT		0x0080  /* remount readonly, no dummy LRs */
#define SYNC_QUIESCE		0x0100  /* quiesce filesystem for a snapshot */

typedef int	(*vfs_mount_t)(bhv_desc_t *,
				struct xfs_mount_args *, struct cred *);
typedef int	(*vfs_parseargs_t)(bhv_desc_t *, char *,
				struct xfs_mount_args *, int);
typedef	int	(*vfs_showargs_t)(bhv_desc_t *, struct seq_file *);
typedef int	(*vfs_unmount_t)(bhv_desc_t *, int, struct cred *);
typedef int	(*vfs_mntupdate_t)(bhv_desc_t *, int *,
				struct xfs_mount_args *);
typedef int	(*vfs_root_t)(bhv_desc_t *, struct vnode **);
typedef int	(*vfs_statvfs_t)(bhv_desc_t *, xfs_statfs_t *, struct vnode *);
typedef int	(*vfs_sync_t)(bhv_desc_t *, int, struct cred *);
typedef int	(*vfs_vget_t)(bhv_desc_t *, struct vnode **, struct fid *);
typedef int	(*vfs_dmapiops_t)(bhv_desc_t *, caddr_t);
typedef int	(*vfs_quotactl_t)(bhv_desc_t *, int, int, caddr_t);
typedef void	(*vfs_init_vnode_t)(bhv_desc_t *,
				struct vnode *, bhv_desc_t *, int);
typedef void	(*vfs_force_shutdown_t)(bhv_desc_t *, int, char *, int);
typedef void	(*vfs_freeze_t)(bhv_desc_t *);

typedef struct vfsops {
	bhv_position_t		vf_position;	/* behavior chain position */
	vfs_mount_t		vfs_mount;	/* mount file system */
	vfs_parseargs_t		vfs_parseargs;	/* parse mount options */
	vfs_showargs_t		vfs_showargs;	/* unparse mount options */
	vfs_unmount_t		vfs_unmount;	/* unmount file system */
	vfs_mntupdate_t		vfs_mntupdate;	/* update file system options */
	vfs_root_t		vfs_root;	/* get root vnode */
	vfs_statvfs_t		vfs_statvfs;	/* file system statistics */
	vfs_sync_t		vfs_sync;	/* flush files */
	vfs_vget_t		vfs_vget;	/* get vnode from fid */
	vfs_dmapiops_t		vfs_dmapiops;	/* data migration */
	vfs_quotactl_t		vfs_quotactl;	/* disk quota */
	vfs_init_vnode_t	vfs_init_vnode;	/* initialize a new vnode */
	vfs_force_shutdown_t	vfs_force_shutdown;	/* crash and burn */
	vfs_freeze_t		vfs_freeze;	/* freeze fs for snapshot */
} vfsops_t;

/*
 * VFS's.  Operates on vfs structure pointers (starts at bhv head).
 */
#define VHEAD(v)			((v)->vfs_fbhv)
#define VFS_MOUNT(v, ma,cr, rv)		((rv) = vfs_mount(VHEAD(v), ma,cr))
#define VFS_PARSEARGS(v, o,ma,f, rv)	((rv) = vfs_parseargs(VHEAD(v), o,ma,f))
#define VFS_SHOWARGS(v, m, rv)		((rv) = vfs_showargs(VHEAD(v), m))
#define VFS_UNMOUNT(v, f, cr, rv)	((rv) = vfs_unmount(VHEAD(v), f,cr))
#define VFS_MNTUPDATE(v, fl, args, rv)	((rv) = vfs_mntupdate(VHEAD(v), fl, args))
#define VFS_ROOT(v, vpp, rv)		((rv) = vfs_root(VHEAD(v), vpp))
#define VFS_STATVFS(v, sp,vp, rv)	((rv) = vfs_statvfs(VHEAD(v), sp,vp))
#define VFS_SYNC(v, flag,cr, rv)	((rv) = vfs_sync(VHEAD(v), flag,cr))
#define VFS_VGET(v, vpp,fidp, rv)	((rv) = vfs_vget(VHEAD(v), vpp,fidp))
#define VFS_DMAPIOPS(v, p, rv)		((rv) = vfs_dmapiops(VHEAD(v), p))
#define VFS_QUOTACTL(v, c,id,p, rv)	((rv) = vfs_quotactl(VHEAD(v), c,id,p))
#define VFS_INIT_VNODE(v, vp,b,ul)	( vfs_init_vnode(VHEAD(v), vp,b,ul) )
#define VFS_FORCE_SHUTDOWN(v, fl,f,l)	( vfs_force_shutdown(VHEAD(v), fl,f,l) )
#define VFS_FREEZE(v)			( vfs_freeze(VHEAD(v)) )

/*
 * PVFS's.  Operates on behavior descriptor pointers.
 */
#define PVFS_MOUNT(b, ma,cr, rv)	((rv) = vfs_mount(b, ma,cr))
#define PVFS_PARSEARGS(b, o,ma,f, rv)	((rv) = vfs_parseargs(b, o,ma,f))
#define PVFS_SHOWARGS(b, m, rv)		((rv) = vfs_showargs(b, m))
#define PVFS_UNMOUNT(b, f,cr, rv)	((rv) = vfs_unmount(b, f,cr))
#define PVFS_MNTUPDATE(b, fl, args, rv)	((rv) = vfs_mntupdate(b, fl, args))
#define PVFS_ROOT(b, vpp, rv)		((rv) = vfs_root(b, vpp))
#define PVFS_STATVFS(b, sp,vp, rv)	((rv) = vfs_statvfs(b, sp,vp))
#define PVFS_SYNC(b, flag,cr, rv)	((rv) = vfs_sync(b, flag,cr))
#define PVFS_VGET(b, vpp,fidp, rv)	((rv) = vfs_vget(b, vpp,fidp))
#define PVFS_DMAPIOPS(b, p, rv)		((rv) = vfs_dmapiops(b, p))
#define PVFS_QUOTACTL(b, c,id,p, rv)	((rv) = vfs_quotactl(b, c,id,p))
#define PVFS_INIT_VNODE(b, vp,b2,ul)	( vfs_init_vnode(b, vp,b2,ul) )
#define PVFS_FORCE_SHUTDOWN(b, fl,f,l)	( vfs_force_shutdown(b, fl,f,l) )
#define PVFS_FREEZE(b)			( vfs_freeze(b) )

extern int vfs_mount(bhv_desc_t *, struct xfs_mount_args *, struct cred *);
extern int vfs_parseargs(bhv_desc_t *, char *, struct xfs_mount_args *, int);
extern int vfs_showargs(bhv_desc_t *, struct seq_file *);
extern int vfs_unmount(bhv_desc_t *, int, struct cred *);
extern int vfs_mntupdate(bhv_desc_t *, int *, struct xfs_mount_args *);
extern int vfs_root(bhv_desc_t *, struct vnode **);
extern int vfs_statvfs(bhv_desc_t *, xfs_statfs_t *, struct vnode *);
extern int vfs_sync(bhv_desc_t *, int, struct cred *);
extern int vfs_vget(bhv_desc_t *, struct vnode **, struct fid *);
extern int vfs_dmapiops(bhv_desc_t *, caddr_t);
extern int vfs_quotactl(bhv_desc_t *, int, int, caddr_t);
extern void vfs_init_vnode(bhv_desc_t *, struct vnode *, bhv_desc_t *, int);
extern void vfs_force_shutdown(bhv_desc_t *, int, char *, int);
extern void vfs_freeze(bhv_desc_t *);

typedef struct bhv_vfsops {
	struct vfsops		bhv_common;
	void *			bhv_custom;
} bhv_vfsops_t;

#define vfs_bhv_lookup(v, id)	( bhv_lookup_range(&(v)->vfs_bh, (id), (id)) )
#define vfs_bhv_custom(b)	( ((bhv_vfsops_t *)BHV_OPS(b))->bhv_custom )
#define vfs_bhv_set_custom(b,o)	( (b)->bhv_custom = (void *)(o))
#define vfs_bhv_clr_custom(b)	( (b)->bhv_custom = NULL )

extern vfs_t *vfs_allocate(struct super_block *);
extern vfs_t *vfs_from_sb(struct super_block *);
extern void vfs_deallocate(vfs_t *);
extern void vfs_insertops(vfs_t *, bhv_vfsops_t *);
extern void vfs_insertbhv(vfs_t *, bhv_desc_t *, vfsops_t *, void *);

extern void bhv_insert_all_vfsops(struct vfs *);
extern void bhv_remove_all_vfsops(struct vfs *, int);
extern void bhv_remove_vfsops(struct vfs *, int);

#define fs_frozen(vfsp)		((vfsp)->vfs_super->s_frozen)
#define fs_check_frozen(vfsp, level) \
	vfs_check_frozen(vfsp->vfs_super, level);

#endif	/* __XFS_VFS_H__ */
