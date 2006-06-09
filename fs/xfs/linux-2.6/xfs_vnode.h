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
 *
 * Portions Copyright (c) 1989, 1993
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
 */
#ifndef __XFS_VNODE_H__
#define __XFS_VNODE_H__

struct uio;
struct file;
struct vattr;
struct xfs_iomap;
struct attrlist_cursor_kern;


typedef xfs_ino_t vnumber_t;
typedef struct dentry vname_t;
typedef bhv_head_t vn_bhv_head_t;

typedef enum vflags {
	VMODIFIED	= 0x08,	/* XFS inode state possibly differs */
				/* to the Linux inode state. */
	VTRUNCATED	= 0x40,	/* truncated down so flush-on-close */
} vflags_t;

/*
 * MP locking protocols:
 *	v_flag, v_vfsp				VN_LOCK/VN_UNLOCK
 */
typedef struct vnode {
	vflags_t	v_flag;			/* vnode flags (see above) */
	struct bhv_vfs	*v_vfsp;		/* ptr to containing VFS */
	vnumber_t	v_number;		/* in-core vnode number */
	vn_bhv_head_t	v_bh;			/* behavior head */
	spinlock_t	v_lock;			/* VN_LOCK/VN_UNLOCK */
	atomic_t	v_iocount;		/* outstanding I/O count */
#ifdef XFS_VNODE_TRACE
	struct ktrace	*v_trace;		/* trace header structure    */
#endif
	struct inode	v_inode;		/* Linux inode */
	/* inode MUST be last */
} vnode_t;

#define VN_ISLNK(vp)	S_ISLNK((vp)->v_inode.i_mode)
#define VN_ISREG(vp)	S_ISREG((vp)->v_inode.i_mode)
#define VN_ISDIR(vp)	S_ISDIR((vp)->v_inode.i_mode)
#define VN_ISCHR(vp)	S_ISCHR((vp)->v_inode.i_mode)
#define VN_ISBLK(vp)	S_ISBLK((vp)->v_inode.i_mode)

#define v_fbhv			v_bh.bh_first	       /* first behavior */
#define v_fops			v_bh.bh_first->bd_ops  /* first behavior ops */

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
#define BHV_TO_VNODE(bdp)	((vnode_t *)BHV_VOBJ(bdp))
#define BHV_TO_VNODE_NULL(bdp)	((vnode_t *)BHV_VOBJNULL(bdp))

#define VN_BHV_HEAD(vp)			((bhv_head_t *)(&((vp)->v_bh)))
#define vn_bhv_head_init(bhp,name)	bhv_head_init(bhp,name)
#define vn_bhv_remove(bhp,bdp)		bhv_remove(bhp,bdp)
#define vn_bhv_lookup(bhp,ops)		bhv_lookup(bhp,ops)
#define vn_bhv_lookup_unlocked(bhp,ops) bhv_lookup_unlocked(bhp,ops)

/*
 * Vnode to Linux inode mapping.
 */
static inline struct vnode *vn_from_inode(struct inode *inode)
{
	return (vnode_t *)list_entry(inode, vnode_t, v_inode);
}
static inline struct inode *vn_to_inode(struct vnode *vnode)
{
	return &vnode->v_inode;
}

/*
 * Values for the VOP_RWLOCK and VOP_RWUNLOCK flags parameter.
 */
typedef enum vrwlock {
	VRWLOCK_NONE,
	VRWLOCK_READ,
	VRWLOCK_WRITE,
	VRWLOCK_WRITE_DIRECT,
	VRWLOCK_TRY_READ,
	VRWLOCK_TRY_WRITE
} vrwlock_t;

/*
 * Return values for VOP_INACTIVE.  A return value of
 * VN_INACTIVE_NOCACHE implies that the file system behavior
 * has disassociated its state and bhv_desc_t from the vnode.
 */
#define	VN_INACTIVE_CACHE	0
#define	VN_INACTIVE_NOCACHE	1

/*
 * Values for the cmd code given to VOP_VNODE_CHANGE.
 */
typedef enum vchange {
	VCHANGE_FLAGS_FRLOCKS		= 0,
	VCHANGE_FLAGS_ENF_LOCKING	= 1,
	VCHANGE_FLAGS_TRUNCATED		= 2,
	VCHANGE_FLAGS_PAGE_DIRTY	= 3,
	VCHANGE_FLAGS_IOEXCL_COUNT	= 4
} vchange_t;

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
typedef int	(*vop_getattr_t)(bhv_desc_t *, struct vattr *, int,
				struct cred *);
typedef int	(*vop_setattr_t)(bhv_desc_t *, struct vattr *, int,
				struct cred *);
typedef int	(*vop_access_t)(bhv_desc_t *, int, struct cred *);
typedef int	(*vop_lookup_t)(bhv_desc_t *, vname_t *, vnode_t **,
				int, vnode_t *, struct cred *);
typedef int	(*vop_create_t)(bhv_desc_t *, vname_t *, struct vattr *,
				vnode_t **, struct cred *);
typedef int	(*vop_remove_t)(bhv_desc_t *, vname_t *, struct cred *);
typedef int	(*vop_link_t)(bhv_desc_t *, vnode_t *, vname_t *,
				struct cred *);
typedef int	(*vop_rename_t)(bhv_desc_t *, vname_t *, vnode_t *, vname_t *,
				struct cred *);
typedef int	(*vop_mkdir_t)(bhv_desc_t *, vname_t *, struct vattr *,
				vnode_t **, struct cred *);
typedef int	(*vop_rmdir_t)(bhv_desc_t *, vname_t *, struct cred *);
typedef int	(*vop_readdir_t)(bhv_desc_t *, struct uio *, struct cred *,
				int *);
typedef int	(*vop_symlink_t)(bhv_desc_t *, vname_t *, struct vattr *,
				char *, vnode_t **, struct cred *);
typedef int	(*vop_readlink_t)(bhv_desc_t *, struct uio *, int,
				struct cred *);
typedef int	(*vop_fsync_t)(bhv_desc_t *, int, struct cred *,
				xfs_off_t, xfs_off_t);
typedef int	(*vop_inactive_t)(bhv_desc_t *, struct cred *);
typedef int	(*vop_fid2_t)(bhv_desc_t *, struct fid *);
typedef int	(*vop_release_t)(bhv_desc_t *);
typedef int	(*vop_rwlock_t)(bhv_desc_t *, vrwlock_t);
typedef void	(*vop_rwunlock_t)(bhv_desc_t *, vrwlock_t);
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
typedef void	(*vop_link_removed_t)(bhv_desc_t *, vnode_t *, int);
typedef void	(*vop_vnode_change_t)(bhv_desc_t *, vchange_t, __psint_t);
typedef void	(*vop_ptossvp_t)(bhv_desc_t *, xfs_off_t, xfs_off_t, int);
typedef void	(*vop_pflushinvalvp_t)(bhv_desc_t *, xfs_off_t, xfs_off_t, int);
typedef int	(*vop_pflushvp_t)(bhv_desc_t *, xfs_off_t, xfs_off_t,
				uint64_t, int);
typedef int	(*vop_iflush_t)(bhv_desc_t *, int);


typedef struct vnodeops {
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
} vnodeops_t;

/*
 * VOP's.
 */
#define _VOP_(op, vp)	(*((vnodeops_t *)(vp)->v_fops)->op)

#define VOP_OPEN(vp, cr, rv)						\
	rv = _VOP_(vop_open, vp)((vp)->v_fbhv, cr)
#define VOP_CLOSE(vp, f, last, cr, rv)					\
	rv = _VOP_(vop_close, vp)((vp)->v_fbhv, f, last, cr)
#define VOP_READ(vp,file,iov,segs,offset,ioflags,cr,rv)			\
	rv = _VOP_(vop_read, vp)((vp)->v_fbhv,file,iov,segs,offset,ioflags,cr)
#define VOP_WRITE(vp,file,iov,segs,offset,ioflags,cr,rv)		\
	rv = _VOP_(vop_write, vp)((vp)->v_fbhv,file,iov,segs,offset,ioflags,cr)
#define VOP_SENDFILE(vp,f,off,ioflags,cnt,act,targ,cr,rv)		\
	rv = _VOP_(vop_sendfile, vp)((vp)->v_fbhv,f,off,ioflags,cnt,act,targ,cr)
#define VOP_SPLICE_READ(vp,f,o,pipe,cnt,fl,iofl,cr,rv)			\
	rv = _VOP_(vop_splice_read, vp)((vp)->v_fbhv,f,o,pipe,cnt,fl,iofl,cr)
#define VOP_SPLICE_WRITE(vp,f,o,pipe,cnt,fl,iofl,cr,rv)			\
	rv = _VOP_(vop_splice_write, vp)((vp)->v_fbhv,f,o,pipe,cnt,fl,iofl,cr)
#define VOP_BMAP(vp,of,sz,rw,b,n,rv)					\
	rv = _VOP_(vop_bmap, vp)((vp)->v_fbhv,of,sz,rw,b,n)
#define VOP_GETATTR(vp, vap, f, cr, rv)					\
	rv = _VOP_(vop_getattr, vp)((vp)->v_fbhv, vap, f, cr)
#define	VOP_SETATTR(vp, vap, f, cr, rv)					\
	rv = _VOP_(vop_setattr, vp)((vp)->v_fbhv, vap, f, cr)
#define	VOP_ACCESS(vp, mode, cr, rv)					\
	rv = _VOP_(vop_access, vp)((vp)->v_fbhv, mode, cr)
#define	VOP_LOOKUP(vp,d,vpp,f,rdir,cr,rv)				\
	rv = _VOP_(vop_lookup, vp)((vp)->v_fbhv,d,vpp,f,rdir,cr)
#define VOP_CREATE(dvp,d,vap,vpp,cr,rv)					\
	rv = _VOP_(vop_create, dvp)((dvp)->v_fbhv,d,vap,vpp,cr)
#define VOP_REMOVE(dvp,d,cr,rv)						\
	rv = _VOP_(vop_remove, dvp)((dvp)->v_fbhv,d,cr)
#define	VOP_LINK(tdvp,fvp,d,cr,rv)					\
	rv = _VOP_(vop_link, tdvp)((tdvp)->v_fbhv,fvp,d,cr)
#define	VOP_RENAME(fvp,fnm,tdvp,tnm,cr,rv)				\
	rv = _VOP_(vop_rename, fvp)((fvp)->v_fbhv,fnm,tdvp,tnm,cr)
#define	VOP_MKDIR(dp,d,vap,vpp,cr,rv)					\
	rv = _VOP_(vop_mkdir, dp)((dp)->v_fbhv,d,vap,vpp,cr)
#define	VOP_RMDIR(dp,d,cr,rv)	 					\
	rv = _VOP_(vop_rmdir, dp)((dp)->v_fbhv,d,cr)
#define	VOP_READDIR(vp,uiop,cr,eofp,rv)					\
	rv = _VOP_(vop_readdir, vp)((vp)->v_fbhv,uiop,cr,eofp)
#define	VOP_SYMLINK(dvp,d,vap,tnm,vpp,cr,rv)				\
	rv = _VOP_(vop_symlink, dvp) ((dvp)->v_fbhv,d,vap,tnm,vpp,cr)
#define	VOP_READLINK(vp,uiop,fl,cr,rv)					\
	rv = _VOP_(vop_readlink, vp)((vp)->v_fbhv,uiop,fl,cr)
#define	VOP_FSYNC(vp,f,cr,b,e,rv)					\
	rv = _VOP_(vop_fsync, vp)((vp)->v_fbhv,f,cr,b,e)
#define VOP_INACTIVE(vp, cr, rv)					\
	rv = _VOP_(vop_inactive, vp)((vp)->v_fbhv, cr)
#define VOP_RELEASE(vp, rv)						\
	rv = _VOP_(vop_release, vp)((vp)->v_fbhv)
#define VOP_FID2(vp, fidp, rv)						\
	rv = _VOP_(vop_fid2, vp)((vp)->v_fbhv, fidp)
#define VOP_RWLOCK(vp,i)						\
	(void)_VOP_(vop_rwlock, vp)((vp)->v_fbhv, i)
#define VOP_RWLOCK_TRY(vp,i)						\
	_VOP_(vop_rwlock, vp)((vp)->v_fbhv, i)
#define VOP_RWUNLOCK(vp,i)						\
	(void)_VOP_(vop_rwunlock, vp)((vp)->v_fbhv, i)
#define VOP_FRLOCK(vp,c,fl,flags,offset,fr,rv)				\
	rv = _VOP_(vop_frlock, vp)((vp)->v_fbhv,c,fl,flags,offset,fr)
#define VOP_RECLAIM(vp, rv)						\
	rv = _VOP_(vop_reclaim, vp)((vp)->v_fbhv)
#define VOP_ATTR_GET(vp, name, val, vallenp, fl, cred, rv)		\
	rv = _VOP_(vop_attr_get, vp)((vp)->v_fbhv,name,val,vallenp,fl,cred)
#define	VOP_ATTR_SET(vp, name, val, vallen, fl, cred, rv)		\
	rv = _VOP_(vop_attr_set, vp)((vp)->v_fbhv,name,val,vallen,fl,cred)
#define	VOP_ATTR_REMOVE(vp, name, flags, cred, rv)			\
	rv = _VOP_(vop_attr_remove, vp)((vp)->v_fbhv,name,flags,cred)
#define	VOP_ATTR_LIST(vp, buf, buflen, fl, cursor, cred, rv)		\
	rv = _VOP_(vop_attr_list, vp)((vp)->v_fbhv,buf,buflen,fl,cursor,cred)
#define VOP_LINK_REMOVED(vp, dvp, linkzero)				\
	(void)_VOP_(vop_link_removed, vp)((vp)->v_fbhv, dvp, linkzero)
#define VOP_VNODE_CHANGE(vp, cmd, val)					\
	(void)_VOP_(vop_vnode_change, vp)((vp)->v_fbhv,cmd,val)
/*
 * These are page cache functions that now go thru VOPs.
 * 'last' parameter is unused and left in for IRIX compatibility
 */
#define VOP_TOSS_PAGES(vp, first, last, fiopt)				\
	_VOP_(vop_tosspages, vp)((vp)->v_fbhv,first, last, fiopt)
/*
 * 'last' parameter is unused and left in for IRIX compatibility
 */
#define VOP_FLUSHINVAL_PAGES(vp, first, last, fiopt)			\
	_VOP_(vop_flushinval_pages, vp)((vp)->v_fbhv,first,last,fiopt)
/*
 * 'last' parameter is unused and left in for IRIX compatibility
 */
#define VOP_FLUSH_PAGES(vp, first, last, flags, fiopt, rv)		\
	rv = _VOP_(vop_flush_pages, vp)((vp)->v_fbhv,first,last,flags,fiopt)
#define VOP_IOCTL(vp, inode, filp, fl, cmd, arg, rv)			\
	rv = _VOP_(vop_ioctl, vp)((vp)->v_fbhv,inode,filp,fl,cmd,arg)
#define VOP_IFLUSH(vp, flags, rv)					\
	rv = _VOP_(vop_iflush, vp)((vp)->v_fbhv, flags)

/*
 * Flags for read/write calls - same values as IRIX
 */
#define IO_ISAIO	0x00001		/* don't wait for completion */
#define IO_ISDIRECT	0x00004		/* bypass page cache */
#define IO_INVIS	0x00020		/* don't update inode timestamps */

/*
 * Flags for VOP_IFLUSH call
 */
#define FLUSH_SYNC		1	/* wait for flush to complete	*/
#define FLUSH_INODE		2	/* flush the inode itself	*/
#define FLUSH_LOG		4	/* force the last log entry for
					 * this inode out to disk	*/

/*
 * Flush/Invalidate options for VOP_TOSS_PAGES, VOP_FLUSHINVAL_PAGES and
 *	VOP_FLUSH_PAGES.
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
typedef struct vattr {
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
} vattr_t;

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
extern vnode_t	*vn_initialize(struct inode *);

/*
 * vnode_map structures _must_ match vn_epoch and vnode structure sizes.
 */
typedef struct vnode_map {
	bhv_vfs_t	*v_vfsp;
	vnumber_t	v_number;		/* in-core vnode number */
	xfs_ino_t	v_ino;			/* inode #	*/
} vmap_t;

#define VMAP(vp, vmap)	{(vmap).v_vfsp	 = (vp)->v_vfsp,	\
			 (vmap).v_number = (vp)->v_number,	\
			 (vmap).v_ino	 = (vp)->v_inode.i_ino; }

extern int	vn_revalidate(struct vnode *);
extern int	__vn_revalidate(struct vnode *, vattr_t *);
extern void	vn_revalidate_core(struct vnode *, vattr_t *);

extern void	vn_iowait(struct vnode *vp);
extern void	vn_iowake(struct vnode *vp);

extern void	vn_ioerror(struct vnode *vp, int error, char *f, int l);

static inline int vn_count(struct vnode *vp)
{
	return atomic_read(&vn_to_inode(vp)->i_count);
}

/*
 * Vnode reference counting functions (and macros for compatibility).
 */
extern vnode_t	*vn_hold(struct vnode *);

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

static inline struct vnode *vn_grab(struct vnode *vp)
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

static __inline__ void vn_flagset(struct vnode *vp, uint flag)
{
	spin_lock(&vp->v_lock);
	vp->v_flag |= flag;
	spin_unlock(&vp->v_lock);
}

static __inline__ uint vn_flagclr(struct vnode *vp, uint flag)
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
static inline void vn_mark_bad(struct vnode *vp)
{
	make_bad_inode(vn_to_inode(vp));
}

static inline int VN_BAD(struct vnode *vp)
{
	return is_bad_inode(vn_to_inode(vp));
}

/*
 * Extracting atime values in various formats
 */
static inline void vn_atime_to_bstime(struct vnode *vp, xfs_bstime_t *bs_atime)
{
	bs_atime->tv_sec = vp->v_inode.i_atime.tv_sec;
	bs_atime->tv_nsec = vp->v_inode.i_atime.tv_nsec;
}

static inline void vn_atime_to_timespec(struct vnode *vp, struct timespec *ts)
{
	*ts = vp->v_inode.i_atime;
}

static inline void vn_atime_to_time_t(struct vnode *vp, time_t *tt)
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
 * Flags to VOP_SETATTR/VOP_GETATTR.
 */
#define	ATTR_UTIME	0x01	/* non-default utime(2) request */
#define	ATTR_DMI	0x08	/* invocation from a DMI function */
#define	ATTR_LAZY	0x80	/* set/get attributes lazily */
#define	ATTR_NONBLOCK	0x100	/* return EAGAIN if operation would block */
#define ATTR_NOLOCK	0x200	/* Don't grab any conflicting locks */
#define ATTR_NOSIZETOK	0x400	/* Don't get the SIZE token */

/*
 * Flags to VOP_FSYNC and VOP_RECLAIM.
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

extern void vn_trace_entry(struct vnode *, const char *, inst_t *);
extern void vn_trace_exit(struct vnode *, const char *, inst_t *);
extern void vn_trace_hold(struct vnode *, char *, int, inst_t *);
extern void vn_trace_ref(struct vnode *, char *, int, inst_t *);
extern void vn_trace_rele(struct vnode *, char *, int, inst_t *);

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
