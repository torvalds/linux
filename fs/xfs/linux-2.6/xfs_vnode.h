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
#ifndef __XFS_VNODE_H__
#define __XFS_VNODE_H__

struct uio;
struct file;
struct bhv_vfs;
struct bhv_vattr;
struct xfs_iomap;
struct attrlist_cursor_kern;

typedef struct dentry	bhv_vname_t;
typedef __u64		bhv_vnumber_t;

typedef enum bhv_vflags {
	VMODIFIED	= 0x08,	/* XFS inode state possibly differs */
				/* to the Linux inode state. */
	VTRUNCATED	= 0x40,	/* truncated down so flush-on-close */
} bhv_vflags_t;

/*
 * MP locking protocols:
 *	v_flag, v_vfsp				VN_LOCK/VN_UNLOCK
 */
typedef struct bhv_vnode {
	bhv_vflags_t	v_flag;			/* vnode flags (see above) */
	bhv_vfs_t	*v_vfsp;		/* ptr to containing VFS */
	bhv_vnumber_t	v_number;		/* in-core vnode number */
	bhv_head_t	v_bh;			/* behavior head */
	spinlock_t	v_lock;			/* VN_LOCK/VN_UNLOCK */
	atomic_t	v_iocount;		/* outstanding I/O count */
#ifdef XFS_VNODE_TRACE
	struct ktrace	*v_trace;		/* trace header structure    */
#endif
	struct inode	v_inode;		/* Linux inode */
	/* inode MUST be last */
} bhv_vnode_t;

#define VN_ISLNK(vp)	S_ISLNK((vp)->v_inode.i_mode)
#define VN_ISREG(vp)	S_ISREG((vp)->v_inode.i_mode)
#define VN_ISDIR(vp)	S_ISDIR((vp)->v_inode.i_mode)
#define VN_ISCHR(vp)	S_ISCHR((vp)->v_inode.i_mode)
#define VN_ISBLK(vp)	S_ISBLK((vp)->v_inode.i_mode)

#define VNODE_POSITION_BASE	BHV_POSITION_BASE	/* chain bottom */
#define VNODE_POSITION_TOP	BHV_POSITION_TOP	/* chain top */
#define VNODE_POSITION_INVALID	BHV_POSITION_INVALID	/* invalid pos. num */

typedef enum {
	VN_BHV_UNKNOWN,		/* not specified */
	VN_BHV_XFS,		/* xfs */
	VN_BHV_DM,		/* data migration */
	VN_BHV_QM,		/* quota manager */
	VN_BHV_IO,		/* IO path */
	VN_BHV_END		/* housekeeping end-of-range */
} vn_bhv_t;

#define VNODE_POSITION_XFS	(VNODE_POSITION_BASE)
#define VNODE_POSITION_DM	(VNODE_POSITION_BASE+10)
#define VNODE_POSITION_QM	(VNODE_POSITION_BASE+20)
#define VNODE_POSITION_IO	(VNODE_POSITION_BASE+30)

/*
 * Macros for dealing with the behavior descriptor inside of the vnode.
 */
#define BHV_TO_VNODE(bdp)	((bhv_vnode_t *)BHV_VOBJ(bdp))
#define BHV_TO_VNODE_NULL(bdp)	((bhv_vnode_t *)BHV_VOBJNULL(bdp))

#define VN_BHV_HEAD(vp)			((bhv_head_t *)(&((vp)->v_bh)))
#define vn_bhv_head_init(bhp,name)	bhv_head_init(bhp,name)
#define vn_bhv_remove(bhp,bdp)		bhv_remove(bhp,bdp)
#define vn_bhv_lookup(bhp,ops)		bhv_lookup(bhp,ops)
#define vn_bhv_lookup_unlocked(bhp,ops) bhv_lookup_unlocked(bhp,ops)

/*
 * Vnode to Linux inode mapping.
 */
static inline struct bhv_vnode *vn_from_inode(struct inode *inode)
{
	return (bhv_vnode_t *)list_entry(inode, bhv_vnode_t, v_inode);
}
static inline struct inode *vn_to_inode(struct bhv_vnode *vnode)
{
	return &vnode->v_inode;
}

/*
 * Values for the vop_rwlock/rwunlock flags parameter.
 */
typedef enum bhv_vrwlock {
	VRWLOCK_NONE,
	VRWLOCK_READ,
	VRWLOCK_WRITE,
	VRWLOCK_WRITE_DIRECT,
	VRWLOCK_TRY_READ,
	VRWLOCK_TRY_WRITE
} bhv_vrwlock_t;

/*
 * Return values for bhv_vop_inactive.  A return value of
 * VN_INACTIVE_NOCACHE implies that the file system behavior
 * has disassociated its state and bhv_desc_t from the vnode.
 */
#define	VN_INACTIVE_CACHE	0
#define	VN_INACTIVE_NOCACHE	1

/*
 * Values for the cmd code given to vop_vnode_change.
 */
typedef enum bhv_vchange {
	VCHANGE_FLAGS_FRLOCKS		= 0,
	VCHANGE_FLAGS_ENF_LOCKING	= 1,
	VCHANGE_FLAGS_TRUNCATED		= 2,
	VCHANGE_FLAGS_PAGE_DIRTY	= 3,
	VCHANGE_FLAGS_IOEXCL_COUNT	= 4
} bhv_vchange_t;

typedef enum { L_FALSE, L_TRUE } lastclose_t;

typedef int	(*vop_open_t)(bhv_desc_t *, struct cred *);
typedef int	(*vop_close_t)(bhv_desc_t *, int, lastclose_t, struct cred *);
typedef ssize_t (*vop_read_t)(bhv_desc_t *, struct kiocb *,
				const struct iovec *, unsigned int,
				loff_t *, int, struct cred *);
typedef ssize_t (*vop_write_t)(bhv_desc_t *, struct kiocb *,
				const struct iovec *, unsigned int,
				loff_t *, int, struct cred *);
typedef ssize_t (*vop_sendfile_t)(bhv_desc_t *, struct file *,
				loff_t *, int, size_t, read_actor_t,
				void *, struct cred *);
typedef ssize_t (*vop_splice_read_t)(bhv_desc_t *, struct file *, loff_t *,
				struct pipe_inode_info *, size_t, int, int,
				struct cred *);
typedef ssize_t (*vop_splice_write_t)(bhv_desc_t *, struct pipe_inode_info *,
				struct file *, loff_t *, size_t, int, int,
				struct cred *);
typedef int	(*vop_ioctl_t)(bhv_desc_t *, struct inode *, struct file *,
				int, unsigned int, void __user *);
typedef int	(*vop_getattr_t)(bhv_desc_t *, struct bhv_vattr *, int,
				struct cred *);
typedef int	(*vop_setattr_t)(bhv_desc_t *, struct bhv_vattr *, int,
				struct cred *);
typedef int	(*vop_access_t)(bhv_desc_t *, int, struct cred *);
typedef int	(*vop_lookup_t)(bhv_desc_t *, bhv_vname_t *, bhv_vnode_t **,
				int, bhv_vnode_t *, struct cred *);
typedef int	(*vop_create_t)(bhv_desc_t *, bhv_vname_t *, struct bhv_vattr *,
				bhv_vnode_t **, struct cred *);
typedef int	(*vop_remove_t)(bhv_desc_t *, bhv_vname_t *, struct cred *);
typedef int	(*vop_link_t)(bhv_desc_t *, bhv_vnode_t *, bhv_vname_t *,
				struct cred *);
typedef int	(*vop_rename_t)(bhv_desc_t *, bhv_vname_t *, bhv_vnode_t *,
				bhv_vname_t *, struct cred *);
typedef int	(*vop_mkdir_t)(bhv_desc_t *, bhv_vname_t *, struct bhv_vattr *,
				bhv_vnode_t **, struct cred *);
typedef int	(*vop_rmdir_t)(bhv_desc_t *, bhv_vname_t *, struct cred *);
typedef int	(*vop_readdir_t)(bhv_desc_t *, struct uio *, struct cred *,
				int *);
typedef int	(*vop_symlink_t)(bhv_desc_t *, bhv_vname_t *, struct bhv_vattr*,
				char *, bhv_vnode_t **, struct cred *);
typedef int	(*vop_readlink_t)(bhv_desc_t *, struct uio *, int,
				struct cred *);
typedef int	(*vop_fsync_t)(bhv_desc_t *, int, struct cred *,
				xfs_off_t, xfs_off_t);
typedef int	(*vop_inactive_t)(bhv_desc_t *, struct cred *);
typedef int	(*vop_fid2_t)(bhv_desc_t *, struct fid *);
typedef int	(*vop_release_t)(bhv_desc_t *);
typedef int	(*vop_rwlock_t)(bhv_desc_t *, bhv_vrwlock_t);
typedef void	(*vop_rwunlock_t)(bhv_desc_t *, bhv_vrwlock_t);
typedef int	(*vop_bmap_t)(bhv_desc_t *, xfs_off_t, ssize_t, int,
				struct xfs_iomap *, int *);
typedef int	(*vop_reclaim_t)(bhv_desc_t *);
typedef int	(*vop_attr_get_t)(bhv_desc_t *, const char *, char *, int *,
				int, struct cred *);
typedef	int	(*vop_attr_set_t)(bhv_desc_t *, const char *, char *, int,
				int, struct cred *);
typedef	int	(*vop_attr_remove_t)(bhv_desc_t *, const char *,
				int, struct cred *);
typedef	int	(*vop_attr_list_t)(bhv_desc_t *, char *, int, int,
				struct attrlist_cursor_kern *, struct cred *);
typedef void	(*vop_link_removed_t)(bhv_desc_t *, bhv_vnode_t *, int);
typedef void	(*vop_vnode_change_t)(bhv_desc_t *, bhv_vchange_t, __psint_t);
typedef void	(*vop_ptossvp_t)(bhv_desc_t *, xfs_off_t, xfs_off_t, int);
typedef void	(*vop_pflushinvalvp_t)(bhv_desc_t *, xfs_off_t, xfs_off_t, int);
typedef int	(*vop_pflushvp_t)(bhv_desc_t *, xfs_off_t, xfs_off_t,
				uint64_t, int);
typedef int	(*vop_iflush_t)(bhv_desc_t *, int);


typedef struct bhv_vnodeops {
	bhv_position_t  vn_position;    /* position within behavior chain */
	vop_open_t		vop_open;
	vop_close_t		vop_close;
	vop_read_t		vop_read;
	vop_write_t		vop_write;
	vop_sendfile_t		vop_sendfile;
	vop_splice_read_t	vop_splice_read;
	vop_splice_write_t	vop_splice_write;
	vop_ioctl_t		vop_ioctl;
	vop_getattr_t		vop_getattr;
	vop_setattr_t		vop_setattr;
	vop_access_t		vop_access;
	vop_lookup_t		vop_lookup;
	vop_create_t		vop_create;
	vop_remove_t		vop_remove;
	vop_link_t		vop_link;
	vop_rename_t		vop_rename;
	vop_mkdir_t		vop_mkdir;
	vop_rmdir_t		vop_rmdir;
	vop_readdir_t		vop_readdir;
	vop_symlink_t		vop_symlink;
	vop_readlink_t		vop_readlink;
	vop_fsync_t		vop_fsync;
	vop_inactive_t		vop_inactive;
	vop_fid2_t		vop_fid2;
	vop_rwlock_t		vop_rwlock;
	vop_rwunlock_t		vop_rwunlock;
	vop_bmap_t		vop_bmap;
	vop_reclaim_t		vop_reclaim;
	vop_attr_get_t		vop_attr_get;
	vop_attr_set_t		vop_attr_set;
	vop_attr_remove_t	vop_attr_remove;
	vop_attr_list_t		vop_attr_list;
	vop_link_removed_t	vop_link_removed;
	vop_vnode_change_t	vop_vnode_change;
	vop_ptossvp_t		vop_tosspages;
	vop_pflushinvalvp_t	vop_flushinval_pages;
	vop_pflushvp_t		vop_flush_pages;
	vop_release_t		vop_release;
	vop_iflush_t		vop_iflush;
} bhv_vnodeops_t;

/*
 * Virtual node operations, operating from head bhv.
 */
#define VNHEAD(vp)	((vp)->v_bh.bh_first)
#define VOP(op, vp)	(*((bhv_vnodeops_t *)VNHEAD(vp)->bd_ops)->op)
#define bhv_vop_open(vp, cr)		VOP(vop_open, vp)(VNHEAD(vp),cr)
#define bhv_vop_close(vp, f,last,cr)	VOP(vop_close, vp)(VNHEAD(vp),f,last,cr)
#define bhv_vop_read(vp,file,iov,segs,offset,ioflags,cr)		\
		VOP(vop_read, vp)(VNHEAD(vp),file,iov,segs,offset,ioflags,cr)
#define bhv_vop_write(vp,file,iov,segs,offset,ioflags,cr)		\
		VOP(vop_write, vp)(VNHEAD(vp),file,iov,segs,offset,ioflags,cr)
#define bhv_vop_sendfile(vp,f,off,ioflags,cnt,act,targ,cr)		\
		VOP(vop_sendfile, vp)(VNHEAD(vp),f,off,ioflags,cnt,act,targ,cr)
#define bhv_vop_splice_read(vp,f,o,pipe,cnt,fl,iofl,cr)			\
		VOP(vop_splice_read, vp)(VNHEAD(vp),f,o,pipe,cnt,fl,iofl,cr)
#define bhv_vop_splice_write(vp,f,o,pipe,cnt,fl,iofl,cr)		\
		VOP(vop_splice_write, vp)(VNHEAD(vp),f,o,pipe,cnt,fl,iofl,cr)
#define bhv_vop_bmap(vp,of,sz,rw,b,n)					\
		VOP(vop_bmap, vp)(VNHEAD(vp),of,sz,rw,b,n)
#define bhv_vop_getattr(vp, vap,f,cr)					\
		VOP(vop_getattr, vp)(VNHEAD(vp), vap,f,cr)
#define	bhv_vop_setattr(vp, vap,f,cr)					\
		VOP(vop_setattr, vp)(VNHEAD(vp), vap,f,cr)
#define	bhv_vop_access(vp, mode,cr)	VOP(vop_access, vp)(VNHEAD(vp), mode,cr)
#define	bhv_vop_lookup(vp,d,vpp,f,rdir,cr)				\
		VOP(vop_lookup, vp)(VNHEAD(vp),d,vpp,f,rdir,cr)
#define bhv_vop_create(dvp,d,vap,vpp,cr)				\
		VOP(vop_create, dvp)(VNHEAD(dvp),d,vap,vpp,cr)
#define bhv_vop_remove(dvp,d,cr)	VOP(vop_remove, dvp)(VNHEAD(dvp),d,cr)
#define	bhv_vop_link(dvp,fvp,d,cr)	VOP(vop_link, dvp)(VNHEAD(dvp),fvp,d,cr)
#define	bhv_vop_rename(fvp,fnm,tdvp,tnm,cr)				\
		VOP(vop_rename, fvp)(VNHEAD(fvp),fnm,tdvp,tnm,cr)
#define	bhv_vop_mkdir(dp,d,vap,vpp,cr)					\
		VOP(vop_mkdir, dp)(VNHEAD(dp),d,vap,vpp,cr)
#define	bhv_vop_rmdir(dp,d,cr)	 	VOP(vop_rmdir, dp)(VNHEAD(dp),d,cr)
#define	bhv_vop_readdir(vp,uiop,cr,eofp)				\
		VOP(vop_readdir, vp)(VNHEAD(vp),uiop,cr,eofp)
#define	bhv_vop_symlink(dvp,d,vap,tnm,vpp,cr)				\
		VOP(vop_symlink, dvp)(VNHEAD(dvp),d,vap,tnm,vpp,cr)
#define	bhv_vop_readlink(vp,uiop,fl,cr)					\
		VOP(vop_readlink, vp)(VNHEAD(vp),uiop,fl,cr)
#define	bhv_vop_fsync(vp,f,cr,b,e)	VOP(vop_fsync, vp)(VNHEAD(vp),f,cr,b,e)
#define bhv_vop_inactive(vp,cr)		VOP(vop_inactive, vp)(VNHEAD(vp),cr)
#define bhv_vop_release(vp)		VOP(vop_release, vp)(VNHEAD(vp))
#define bhv_vop_fid2(vp,fidp)		VOP(vop_fid2, vp)(VNHEAD(vp),fidp)
#define bhv_vop_rwlock(vp,i)		VOP(vop_rwlock, vp)(VNHEAD(vp),i)
#define bhv_vop_rwlock_try(vp,i)	VOP(vop_rwlock, vp)(VNHEAD(vp),i)
#define bhv_vop_rwunlock(vp,i)		VOP(vop_rwunlock, vp)(VNHEAD(vp),i)
#define bhv_vop_frlock(vp,c,fl,flags,offset,fr)				\
		VOP(vop_frlock, vp)(VNHEAD(vp),c,fl,flags,offset,fr)
#define bhv_vop_reclaim(vp)		VOP(vop_reclaim, vp)(VNHEAD(vp))
#define bhv_vop_attr_get(vp, name, val, vallenp, fl, cred)		\
		VOP(vop_attr_get, vp)(VNHEAD(vp),name,val,vallenp,fl,cred)
#define	bhv_vop_attr_set(vp, name, val, vallen, fl, cred)		\
		VOP(vop_attr_set, vp)(VNHEAD(vp),name,val,vallen,fl,cred)
#define	bhv_vop_attr_remove(vp, name, flags, cred)			\
		VOP(vop_attr_remove, vp)(VNHEAD(vp),name,flags,cred)
#define	bhv_vop_attr_list(vp, buf, buflen, fl, cursor, cred)		\
		VOP(vop_attr_list, vp)(VNHEAD(vp),buf,buflen,fl,cursor,cred)
#define bhv_vop_link_removed(vp, dvp, linkzero)				\
		VOP(vop_link_removed, vp)(VNHEAD(vp), dvp, linkzero)
#define bhv_vop_vnode_change(vp, cmd, val)				\
		VOP(vop_vnode_change, vp)(VNHEAD(vp), cmd, val)
#define bhv_vop_toss_pages(vp, first, last, fiopt)			\
		VOP(vop_tosspages, vp)(VNHEAD(vp), first, last, fiopt)
#define bhv_vop_flushinval_pages(vp, first, last, fiopt)		\
		VOP(vop_flushinval_pages, vp)(VNHEAD(vp),first,last,fiopt)
#define bhv_vop_flush_pages(vp, first, last, flags, fiopt)		\
		VOP(vop_flush_pages, vp)(VNHEAD(vp),first,last,flags,fiopt)
#define bhv_vop_ioctl(vp, inode, filp, fl, cmd, arg)			\
		VOP(vop_ioctl, vp)(VNHEAD(vp),inode,filp,fl,cmd,arg)
#define bhv_vop_iflush(vp, flags)	VOP(vop_iflush, vp)(VNHEAD(vp), flags)

/*
 * Flags for read/write calls - same values as IRIX
 */
#define IO_ISAIO	0x00001		/* don't wait for completion */
#define IO_ISDIRECT	0x00004		/* bypass page cache */
#define IO_INVIS	0x00020		/* don't update inode timestamps */

/*
 * Flags for vop_iflush call
 */
#define FLUSH_SYNC		1	/* wait for flush to complete	*/
#define FLUSH_INODE		2	/* flush the inode itself	*/
#define FLUSH_LOG		4	/* force the last log entry for
					 * this inode out to disk	*/

/*
 * Flush/Invalidate options for vop_toss/flush/flushinval_pages.
 */
#define FI_NONE			0	/* none */
#define FI_REMAPF		1	/* Do a remapf prior to the operation */
#define FI_REMAPF_LOCKED	2	/* Do a remapf prior to the operation.
					   Prevent VM access to the pages until
					   the operation completes. */

/*
 * Vnode attributes.  va_mask indicates those attributes the caller
 * wants to set or extract.
 */
typedef struct bhv_vattr {
	int		va_mask;	/* bit-mask of attributes present */
	mode_t		va_mode;	/* file access mode and type */
	xfs_nlink_t	va_nlink;	/* number of references to file */
	uid_t		va_uid;		/* owner user id */
	gid_t		va_gid;		/* owner group id */
	xfs_ino_t	va_nodeid;	/* file id */
	xfs_off_t	va_size;	/* file size in bytes */
	u_long		va_blocksize;	/* blocksize preferred for i/o */
	struct timespec	va_atime;	/* time of last access */
	struct timespec	va_mtime;	/* time of last modification */
	struct timespec	va_ctime;	/* time file changed */
	u_int		va_gen;		/* generation number of file */
	xfs_dev_t	va_rdev;	/* device the special file represents */
	__int64_t	va_nblocks;	/* number of blocks allocated */
	u_long		va_xflags;	/* random extended file flags */
	u_long		va_extsize;	/* file extent size */
	u_long		va_nextents;	/* number of extents in file */
	u_long		va_anextents;	/* number of attr extents in file */
	prid_t		va_projid;	/* project id */
} bhv_vattr_t;

/*
 * setattr or getattr attributes
 */
#define XFS_AT_TYPE		0x00000001
#define XFS_AT_MODE		0x00000002
#define XFS_AT_UID		0x00000004
#define XFS_AT_GID		0x00000008
#define XFS_AT_FSID		0x00000010
#define XFS_AT_NODEID		0x00000020
#define XFS_AT_NLINK		0x00000040
#define XFS_AT_SIZE		0x00000080
#define XFS_AT_ATIME		0x00000100
#define XFS_AT_MTIME		0x00000200
#define XFS_AT_CTIME		0x00000400
#define XFS_AT_RDEV		0x00000800
#define XFS_AT_BLKSIZE		0x00001000
#define XFS_AT_NBLOCKS		0x00002000
#define XFS_AT_VCODE		0x00004000
#define XFS_AT_MAC		0x00008000
#define XFS_AT_UPDATIME		0x00010000
#define XFS_AT_UPDMTIME		0x00020000
#define XFS_AT_UPDCTIME		0x00040000
#define XFS_AT_ACL		0x00080000
#define XFS_AT_CAP		0x00100000
#define XFS_AT_INF		0x00200000
#define XFS_AT_XFLAGS		0x00400000
#define XFS_AT_EXTSIZE		0x00800000
#define XFS_AT_NEXTENTS		0x01000000
#define XFS_AT_ANEXTENTS	0x02000000
#define XFS_AT_PROJID		0x04000000
#define XFS_AT_SIZE_NOPERM	0x08000000
#define XFS_AT_GENCOUNT		0x10000000

#define XFS_AT_ALL	(XFS_AT_TYPE|XFS_AT_MODE|XFS_AT_UID|XFS_AT_GID|\
		XFS_AT_FSID|XFS_AT_NODEID|XFS_AT_NLINK|XFS_AT_SIZE|\
		XFS_AT_ATIME|XFS_AT_MTIME|XFS_AT_CTIME|XFS_AT_RDEV|\
		XFS_AT_BLKSIZE|XFS_AT_NBLOCKS|XFS_AT_VCODE|XFS_AT_MAC|\
		XFS_AT_ACL|XFS_AT_CAP|XFS_AT_INF|XFS_AT_XFLAGS|XFS_AT_EXTSIZE|\
		XFS_AT_NEXTENTS|XFS_AT_ANEXTENTS|XFS_AT_PROJID|XFS_AT_GENCOUNT)

#define XFS_AT_STAT	(XFS_AT_TYPE|XFS_AT_MODE|XFS_AT_UID|XFS_AT_GID|\
		XFS_AT_FSID|XFS_AT_NODEID|XFS_AT_NLINK|XFS_AT_SIZE|\
		XFS_AT_ATIME|XFS_AT_MTIME|XFS_AT_CTIME|XFS_AT_RDEV|\
		XFS_AT_BLKSIZE|XFS_AT_NBLOCKS|XFS_AT_PROJID)

#define XFS_AT_TIMES	(XFS_AT_ATIME|XFS_AT_MTIME|XFS_AT_CTIME)

#define XFS_AT_UPDTIMES	(XFS_AT_UPDATIME|XFS_AT_UPDMTIME|XFS_AT_UPDCTIME)

#define XFS_AT_NOSET	(XFS_AT_NLINK|XFS_AT_RDEV|XFS_AT_FSID|XFS_AT_NODEID|\
		XFS_AT_TYPE|XFS_AT_BLKSIZE|XFS_AT_NBLOCKS|XFS_AT_VCODE|\
		XFS_AT_NEXTENTS|XFS_AT_ANEXTENTS|XFS_AT_GENCOUNT)

/*
 *  Modes.
 */
#define VSUID	S_ISUID		/* set user id on execution */
#define VSGID	S_ISGID		/* set group id on execution */
#define VSVTX	S_ISVTX		/* save swapped text even after use */
#define VREAD	S_IRUSR		/* read, write, execute permissions */
#define VWRITE	S_IWUSR
#define VEXEC	S_IXUSR

#define MODEMASK S_IALLUGO	/* mode bits plus permission bits */

/*
 * Check whether mandatory file locking is enabled.
 */
#define MANDLOCK(vp, mode)	\
	(VN_ISREG(vp) && ((mode) & (VSGID|(VEXEC>>3))) == VSGID)

extern void	vn_init(void);
extern bhv_vnode_t	*vn_initialize(struct inode *);
extern int	vn_revalidate(struct bhv_vnode *);
extern int	__vn_revalidate(struct bhv_vnode *, bhv_vattr_t *);
extern void	vn_revalidate_core(struct bhv_vnode *, bhv_vattr_t *);

extern void	vn_iowait(struct bhv_vnode *vp);
extern void	vn_iowake(struct bhv_vnode *vp);

extern void	vn_ioerror(struct bhv_vnode *vp, int error, char *f, int l);

static inline int vn_count(struct bhv_vnode *vp)
{
	return atomic_read(&vn_to_inode(vp)->i_count);
}

/*
 * Vnode reference counting functions (and macros for compatibility).
 */
extern bhv_vnode_t	*vn_hold(struct bhv_vnode *);

#if defined(XFS_VNODE_TRACE)
#define VN_HOLD(vp)		\
	((void)vn_hold(vp),	\
	  vn_trace_hold(vp, __FILE__, __LINE__, (inst_t *)__return_address))
#define VN_RELE(vp)		\
	  (vn_trace_rele(vp, __FILE__, __LINE__, (inst_t *)__return_address), \
	   iput(vn_to_inode(vp)))
#else
#define VN_HOLD(vp)		((void)vn_hold(vp))
#define VN_RELE(vp)		(iput(vn_to_inode(vp)))
#endif

static inline struct bhv_vnode *vn_grab(struct bhv_vnode *vp)
{
	struct inode *inode = igrab(vn_to_inode(vp));
	return inode ? vn_from_inode(inode) : NULL;
}

/*
 * Vname handling macros.
 */
#define VNAME(dentry)		((char *) (dentry)->d_name.name)
#define VNAMELEN(dentry)	((dentry)->d_name.len)
#define VNAME_TO_VNODE(dentry)	(vn_from_inode((dentry)->d_inode))

/*
 * Vnode spinlock manipulation.
 */
#define VN_LOCK(vp)		mutex_spinlock(&(vp)->v_lock)
#define VN_UNLOCK(vp, s)	mutex_spinunlock(&(vp)->v_lock, s)

static __inline__ void vn_flagset(struct bhv_vnode *vp, uint flag)
{
	spin_lock(&vp->v_lock);
	vp->v_flag |= flag;
	spin_unlock(&vp->v_lock);
}

static __inline__ uint vn_flagclr(struct bhv_vnode *vp, uint flag)
{
	uint	cleared;

	spin_lock(&vp->v_lock);
	cleared = (vp->v_flag & flag);
	vp->v_flag &= ~flag;
	spin_unlock(&vp->v_lock);
	return cleared;
}

#define VMODIFY(vp)	vn_flagset(vp, VMODIFIED)
#define VUNMODIFY(vp)	vn_flagclr(vp, VMODIFIED)
#define VTRUNCATE(vp)	vn_flagset(vp, VTRUNCATED)
#define VUNTRUNCATE(vp)	vn_flagclr(vp, VTRUNCATED)

/*
 * Dealing with bad inodes
 */
static inline void vn_mark_bad(struct bhv_vnode *vp)
{
	make_bad_inode(vn_to_inode(vp));
}

static inline int VN_BAD(struct bhv_vnode *vp)
{
	return is_bad_inode(vn_to_inode(vp));
}

/*
 * Extracting atime values in various formats
 */
static inline void vn_atime_to_bstime(bhv_vnode_t *vp, xfs_bstime_t *bs_atime)
{
	bs_atime->tv_sec = vp->v_inode.i_atime.tv_sec;
	bs_atime->tv_nsec = vp->v_inode.i_atime.tv_nsec;
}

static inline void vn_atime_to_timespec(bhv_vnode_t *vp, struct timespec *ts)
{
	*ts = vp->v_inode.i_atime;
}

static inline void vn_atime_to_time_t(bhv_vnode_t *vp, time_t *tt)
{
	*tt = vp->v_inode.i_atime.tv_sec;
}

/*
 * Some useful predicates.
 */
#define VN_MAPPED(vp)	mapping_mapped(vn_to_inode(vp)->i_mapping)
#define VN_CACHED(vp)	(vn_to_inode(vp)->i_mapping->nrpages)
#define VN_DIRTY(vp)	mapping_tagged(vn_to_inode(vp)->i_mapping, \
					PAGECACHE_TAG_DIRTY)
#define VN_TRUNC(vp)	((vp)->v_flag & VTRUNCATED)

/*
 * Flags to vop_setattr/getattr.
 */
#define	ATTR_UTIME	0x01	/* non-default utime(2) request */
#define	ATTR_DMI	0x08	/* invocation from a DMI function */
#define	ATTR_LAZY	0x80	/* set/get attributes lazily */
#define	ATTR_NONBLOCK	0x100	/* return EAGAIN if operation would block */
#define ATTR_NOLOCK	0x200	/* Don't grab any conflicting locks */
#define ATTR_NOSIZETOK	0x400	/* Don't get the SIZE token */

/*
 * Flags to vop_fsync/reclaim.
 */
#define FSYNC_NOWAIT	0	/* asynchronous flush */
#define FSYNC_WAIT	0x1	/* synchronous fsync or forced reclaim */
#define FSYNC_INVAL	0x2	/* flush and invalidate cached data */
#define FSYNC_DATA	0x4	/* synchronous fsync of data only */

/*
 * Tracking vnode activity.
 */
#if defined(XFS_VNODE_TRACE)

#define	VNODE_TRACE_SIZE	16		/* number of trace entries */
#define	VNODE_KTRACE_ENTRY	1
#define	VNODE_KTRACE_EXIT	2
#define	VNODE_KTRACE_HOLD	3
#define	VNODE_KTRACE_REF	4
#define	VNODE_KTRACE_RELE	5

extern void vn_trace_entry(struct bhv_vnode *, const char *, inst_t *);
extern void vn_trace_exit(struct bhv_vnode *, const char *, inst_t *);
extern void vn_trace_hold(struct bhv_vnode *, char *, int, inst_t *);
extern void vn_trace_ref(struct bhv_vnode *, char *, int, inst_t *);
extern void vn_trace_rele(struct bhv_vnode *, char *, int, inst_t *);

#define	VN_TRACE(vp)		\
	vn_trace_ref(vp, __FILE__, __LINE__, (inst_t *)__return_address)
#else
#define	vn_trace_entry(a,b,c)
#define	vn_trace_exit(a,b,c)
#define	vn_trace_hold(a,b,c,d)
#define	vn_trace_ref(a,b,c,d)
#define	vn_trace_rele(a,b,c,d)
#define	VN_TRACE(vp)
#endif

#endif	/* __XFS_VNODE_H__ */
