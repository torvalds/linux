/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
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

struct bhv_vfs;
struct bhv_vnode;

struct fid;
struct cred;
struct seq_file;
struct super_block;
struct xfs_mount_args;

typedef struct kstatfs	bhv_statvfs_t;

typedef struct bhv_vfs_sync_work {
	struct list_head	w_list;
	struct bhv_vfs		*w_vfs;
	void			*w_data;	/* syncer routine argument */
	void			(*w_syncer)(struct bhv_vfs *, void *);
} bhv_vfs_sync_work_t;

typedef struct bhv_vfs {
	u_int			vfs_flag;	/* flags */
	xfs_fsid_t		vfs_fsid;	/* file system ID */
	xfs_fsid_t		*vfs_altfsid;	/* An ID fixed for life of FS */
	bhv_head_t		vfs_bh;		/* head of vfs behavior chain */
	struct super_block	*vfs_super;	/* generic superblock pointer */
	struct task_struct	*vfs_sync_task;	/* generalised sync thread */
	bhv_vfs_sync_work_t	vfs_sync_work;	/* work item for VFS_SYNC */
	struct list_head	vfs_sync_list;	/* sync thread work item list */
	spinlock_t		vfs_sync_lock;	/* work item list lock */
	int			vfs_sync_seq;	/* sync thread generation no. */
	wait_queue_head_t	vfs_wait_single_sync_task;
} bhv_vfs_t;

#define bhvtovfs(bdp)		( (struct bhv_vfs *)BHV_VOBJ(bdp) )
#define bhvtovfsops(bdp)	( (struct bhv_vfsops *)BHV_OPS(bdp) )
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
} bhv_vfs_type_t;

#define VFS_POSITION_XFS	(BHV_POSITION_BASE)
#define VFS_POSITION_DM		(VFS_POSITION_BASE+10)
#define VFS_POSITION_QM		(VFS_POSITION_BASE+20)
#define VFS_POSITION_IO		(VFS_POSITION_BASE+30)

#define VFS_RDONLY		0x0001	/* read-only vfs */
#define VFS_GRPID		0x0002	/* group-ID assigned from directory */
#define VFS_DMI			0x0004	/* filesystem has the DMI enabled */
/* ---- VFS_UMOUNT ----		0x0008	-- unneeded, fixed via kthread APIs */
#define VFS_32BITINODES		0x0010	/* do not use inums above 32 bits */
#define VFS_END			0x0010	/* max flag */

#define SYNC_ATTR		0x0001	/* sync attributes */
#define SYNC_CLOSE		0x0002	/* close file system down */
#define SYNC_DELWRI		0x0004	/* look at delayed writes */
#define SYNC_WAIT		0x0008	/* wait for i/o to complete */
#define SYNC_BDFLUSH		0x0010	/* BDFLUSH is calling -- don't block */
#define SYNC_FSDATA		0x0020	/* flush fs data (e.g. superblocks) */
#define SYNC_REFCACHE		0x0040  /* prune some of the nfs ref cache */
#define SYNC_REMOUNT		0x0080  /* remount readonly, no dummy LRs */
#define SYNC_QUIESCE		0x0100  /* quiesce fileystem for a snapshot */

#define SHUTDOWN_META_IO_ERROR	0x0001	/* write attempt to metadata failed */
#define SHUTDOWN_LOG_IO_ERROR	0x0002	/* write attempt to the log failed */
#define SHUTDOWN_FORCE_UMOUNT	0x0004	/* shutdown from a forced unmount */
#define SHUTDOWN_CORRUPT_INCORE	0x0008	/* corrupt in-memory data structures */
#define SHUTDOWN_REMOTE_REQ	0x0010	/* shutdown came from remote cell */
#define SHUTDOWN_DEVICE_REQ	0x0020	/* failed all paths to the device */

typedef int	(*vfs_mount_t)(bhv_desc_t *,
				struct xfs_mount_args *, struct cred *);
typedef int	(*vfs_parseargs_t)(bhv_desc_t *, char *,
				struct xfs_mount_args *, int);
typedef	int	(*vfs_showargs_t)(bhv_desc_t *, struct seq_file *);
typedef int	(*vfs_unmount_t)(bhv_desc_t *, int, struct cred *);
typedef int	(*vfs_mntupdate_t)(bhv_desc_t *, int *,
				struct xfs_mount_args *);
typedef int	(*vfs_root_t)(bhv_desc_t *, struct bhv_vnode **);
typedef int	(*vfs_statvfs_t)(bhv_desc_t *, bhv_statvfs_t *,
				struct bhv_vnode *);
typedef int	(*vfs_sync_t)(bhv_desc_t *, int, struct cred *);
typedef int	(*vfs_vget_t)(bhv_desc_t *, struct bhv_vnode **, struct fid *);
typedef int	(*vfs_dmapiops_t)(bhv_desc_t *, caddr_t);
typedef int	(*vfs_quotactl_t)(bhv_desc_t *, int, int, caddr_t);
typedef void	(*vfs_init_vnode_t)(bhv_desc_t *,
				struct bhv_vnode *, bhv_desc_t *, int);
typedef void	(*vfs_force_shutdown_t)(bhv_desc_t *, int, char *, int);
typedef void	(*vfs_freeze_t)(bhv_desc_t *);

typedef struct bhv_vfsops {
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
} bhv_vfsops_t;

/*
 * Virtual filesystem operations, operating from head bhv.
 */
#define VFSHEAD(v)			((v)->vfs_bh.bh_first)
#define bhv_vfs_mount(v, ma,cr)		vfs_mount(VFSHEAD(v), ma,cr)
#define bhv_vfs_parseargs(v, o,ma,f)	vfs_parseargs(VFSHEAD(v), o,ma,f)
#define bhv_vfs_showargs(v, m)		vfs_showargs(VFSHEAD(v), m)
#define bhv_vfs_unmount(v, f,cr)	vfs_unmount(VFSHEAD(v), f,cr)
#define bhv_vfs_mntupdate(v, fl,args)	vfs_mntupdate(VFSHEAD(v), fl,args)
#define bhv_vfs_root(v, vpp)		vfs_root(VFSHEAD(v), vpp)
#define bhv_vfs_statvfs(v, sp,vp)	vfs_statvfs(VFSHEAD(v), sp,vp)
#define bhv_vfs_sync(v, flag,cr)	vfs_sync(VFSHEAD(v), flag,cr)
#define bhv_vfs_vget(v, vpp,fidp)	vfs_vget(VFSHEAD(v), vpp,fidp)
#define bhv_vfs_dmapiops(v, p)		vfs_dmapiops(VFSHEAD(v), p)
#define bhv_vfs_quotactl(v, c,id,p)	vfs_quotactl(VFSHEAD(v), c,id,p)
#define bhv_vfs_init_vnode(v, vp,b,ul)	vfs_init_vnode(VFSHEAD(v), vp,b,ul)
#define bhv_vfs_force_shutdown(v,u,f,l)	vfs_force_shutdown(VFSHEAD(v), u,f,l)
#define bhv_vfs_freeze(v)		vfs_freeze(VFSHEAD(v))

/*
 * Virtual filesystem operations, operating from next bhv.
 */
#define bhv_next_vfs_mount(b, ma,cr)		vfs_mount(b, ma,cr)
#define bhv_next_vfs_parseargs(b, o,ma,f)	vfs_parseargs(b, o,ma,f)
#define bhv_next_vfs_showargs(b, m)		vfs_showargs(b, m)
#define bhv_next_vfs_unmount(b, f,cr)		vfs_unmount(b, f,cr)
#define bhv_next_vfs_mntupdate(b, fl,args)	vfs_mntupdate(b, fl, args)
#define bhv_next_vfs_root(b, vpp)		vfs_root(b, vpp)
#define bhv_next_vfs_statvfs(b, sp,vp)		vfs_statvfs(b, sp,vp)
#define bhv_next_vfs_sync(b, flag,cr)		vfs_sync(b, flag,cr)
#define bhv_next_vfs_vget(b, vpp,fidp)		vfs_vget(b, vpp,fidp)
#define bhv_next_vfs_dmapiops(b, p)		vfs_dmapiops(b, p)
#define bhv_next_vfs_quotactl(b, c,id,p)	vfs_quotactl(b, c,id,p)
#define bhv_next_vfs_init_vnode(b, vp,b2,ul)	vfs_init_vnode(b, vp,b2,ul)
#define bhv_next_force_shutdown(b, fl,f,l)	vfs_force_shutdown(b, fl,f,l)
#define bhv_next_vfs_freeze(b)			vfs_freeze(b)

extern int vfs_mount(bhv_desc_t *, struct xfs_mount_args *, struct cred *);
extern int vfs_parseargs(bhv_desc_t *, char *, struct xfs_mount_args *, int);
extern int vfs_showargs(bhv_desc_t *, struct seq_file *);
extern int vfs_unmount(bhv_desc_t *, int, struct cred *);
extern int vfs_mntupdate(bhv_desc_t *, int *, struct xfs_mount_args *);
extern int vfs_root(bhv_desc_t *, struct bhv_vnode **);
extern int vfs_statvfs(bhv_desc_t *, bhv_statvfs_t *, struct bhv_vnode *);
extern int vfs_sync(bhv_desc_t *, int, struct cred *);
extern int vfs_vget(bhv_desc_t *, struct bhv_vnode **, struct fid *);
extern int vfs_dmapiops(bhv_desc_t *, caddr_t);
extern int vfs_quotactl(bhv_desc_t *, int, int, caddr_t);
extern void vfs_init_vnode(bhv_desc_t *, struct bhv_vnode *, bhv_desc_t *, int);
extern void vfs_force_shutdown(bhv_desc_t *, int, char *, int);
extern void vfs_freeze(bhv_desc_t *);

#define vfs_test_for_freeze(vfs)	((vfs)->vfs_super->s_frozen)
#define vfs_wait_for_freeze(vfs,l)	vfs_check_frozen((vfs)->vfs_super, (l))
 
typedef struct bhv_module_vfsops {
	struct bhv_vfsops	bhv_common;
	void *			bhv_custom;
} bhv_module_vfsops_t;

#define vfs_bhv_lookup(v, id)	(bhv_lookup_range(&(v)->vfs_bh, (id), (id)))
#define vfs_bhv_custom(b)	(((bhv_module_vfsops_t*)BHV_OPS(b))->bhv_custom)
#define vfs_bhv_set_custom(b,o)	((b)->bhv_custom = (void *)(o))
#define vfs_bhv_clr_custom(b)	((b)->bhv_custom = NULL)

extern bhv_vfs_t *vfs_allocate(struct super_block *);
extern bhv_vfs_t *vfs_from_sb(struct super_block *);
extern void vfs_deallocate(bhv_vfs_t *);
extern void vfs_insertbhv(bhv_vfs_t *, bhv_desc_t *, bhv_vfsops_t *, void *);

extern void vfs_insertops(bhv_vfs_t *, bhv_module_vfsops_t *);

extern void bhv_insert_all_vfsops(struct bhv_vfs *);
extern void bhv_remove_all_vfsops(struct bhv_vfs *, int);
extern void bhv_remove_vfsops(struct bhv_vfs *, int);

#endif	/* __XFS_VFS_H__ */
