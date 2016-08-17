/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_VNODE_H
#define _SPL_VNODE_H

#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/mount.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/sunldi.h>

/*
 * Prior to linux-2.6.33 only O_DSYNC semantics were implemented and
 * they used the O_SYNC flag.  As of linux-2.6.33 the this behavior
 * was properly split in to O_SYNC and O_DSYNC respectively.
 */
#ifndef O_DSYNC
#define O_DSYNC		O_SYNC
#endif

#define FREAD		1
#define FWRITE		2
#define FCREAT		O_CREAT
#define FTRUNC		O_TRUNC
#define FOFFMAX		O_LARGEFILE
#define FSYNC		O_SYNC
#define FDSYNC		O_DSYNC
#define FRSYNC		O_SYNC
#define FEXCL		O_EXCL
#define FDIRECT		O_DIRECT
#define FAPPEND		O_APPEND

#define FNODSYNC	0x10000 /* fsync pseudo flag */
#define FNOFOLLOW	0x20000 /* don't follow symlinks */

#define F_FREESP	11 	/* Free file space */


/*
 * The vnode AT_ flags are mapped to the Linux ATTR_* flags.
 * This allows them to be used safely with an iattr structure.
 * The AT_XVATTR flag has been added and mapped to the upper
 * bit range to avoid conflicting with the standard Linux set.
 */
#undef AT_UID
#undef AT_GID

#define AT_MODE		ATTR_MODE
#define AT_UID		ATTR_UID
#define AT_GID		ATTR_GID
#define AT_SIZE		ATTR_SIZE
#define AT_ATIME	ATTR_ATIME
#define AT_MTIME	ATTR_MTIME
#define AT_CTIME	ATTR_CTIME

#define ATTR_XVATTR	(1 << 31)
#define AT_XVATTR	ATTR_XVATTR

#define ATTR_IATTR_MASK	(ATTR_MODE | ATTR_UID | ATTR_GID | ATTR_SIZE | \
			ATTR_ATIME | ATTR_MTIME | ATTR_CTIME | ATTR_FILE)

#define CRCREAT		0x01
#define RMFILE		0x02

#define B_INVAL		0x01
#define B_TRUNC		0x02

#define LOOKUP_DIR		0x01
#define LOOKUP_XATTR		0x02
#define CREATE_XATTR_DIR	0x04
#define ATTR_NOACLCHECK		0x20

typedef enum vtype {
	VNON		= 0,
	VREG		= 1,
	VDIR		= 2,
	VBLK		= 3,
	VCHR		= 4,
	VLNK		= 5,
	VFIFO		= 6,
	VDOOR		= 7,
	VPROC		= 8,
	VSOCK		= 9,
	VPORT		= 10,
	VBAD		= 11
} vtype_t;

typedef struct vattr {
	enum vtype	va_type;	/* vnode type */
	u_int		va_mask;	/* attribute bit-mask */
	u_short		va_mode;	/* acc mode */
	uid_t		va_uid;		/* owner uid */
	gid_t		va_gid;		/* owner gid */
	long		va_fsid;	/* fs id */
	long		va_nodeid;	/* node # */
	uint32_t	va_nlink;	/* # links */
	uint64_t	va_size;	/* file size */
	struct timespec	va_atime;	/* last acc */
	struct timespec	va_mtime;	/* last mod */
	struct timespec	va_ctime;	/* last chg */
	dev_t		va_rdev;	/* dev */
	uint64_t	va_nblocks;	/* space used */
	uint32_t	va_blksize;	/* block size */
	uint32_t	va_seq;		/* sequence */
	struct dentry	*va_dentry;	/* dentry to wire */
} vattr_t;

typedef struct vnode {
	struct file	*v_file;
	kmutex_t	v_lock;		/* protects vnode fields */
	uint_t		v_flag;		/* vnode flags (see below) */
	uint_t		v_count;	/* reference count */
	void		*v_data;	/* private data for fs */
	struct vfs	*v_vfsp;	/* ptr to containing VFS */
	struct stdata	*v_stream;	/* associated stream */
	enum vtype	v_type;		/* vnode type */
	dev_t		v_rdev;		/* device (VCHR, VBLK) */
	gfp_t		v_gfp_mask;	/* original mapping gfp mask */
} vnode_t;

typedef struct vn_file {
	int		f_fd;		/* linux fd for lookup */
	struct task_struct *f_task;	/* linux task this fd belongs to */
	struct file	*f_file;	/* linux file struct */
	atomic_t	f_ref;		/* ref count */
	kmutex_t	f_lock;		/* struct lock */
	loff_t		f_offset;	/* offset */
	vnode_t		*f_vnode;	/* vnode */
	struct list_head f_list;	/* list referenced file_t's */
} file_t;

extern vnode_t *vn_alloc(int flag);
void vn_free(vnode_t *vp);
extern vtype_t vn_mode_to_vtype(mode_t);
extern mode_t vn_vtype_to_mode(vtype_t);
extern int vn_open(const char *path, uio_seg_t seg, int flags, int mode,
		   vnode_t **vpp, int x1, void *x2);
extern int vn_openat(const char *path, uio_seg_t seg, int flags, int mode,
		     vnode_t **vpp, int x1, void *x2, vnode_t *vp, int fd);
extern int vn_rdwr(uio_rw_t uio, vnode_t *vp, void *addr, ssize_t len,
		   offset_t off, uio_seg_t seg, int x1, rlim64_t x2,
		   void *x3, ssize_t *residp);
extern int vn_close(vnode_t *vp, int flags, int x1, int x2, void *x3, void *x4);
extern int vn_seek(vnode_t *vp, offset_t o, offset_t *op, void *ct);

extern int vn_remove(const char *path, uio_seg_t seg, int flags);
extern int vn_rename(const char *path1, const char *path2, int x1);
extern int vn_getattr(vnode_t *vp, vattr_t *vap, int flags, void *x3, void *x4);
extern int vn_fsync(vnode_t *vp, int flags, void *x3, void *x4);
extern int vn_space(vnode_t *vp, int cmd, struct flock *bfp, int flag,
    offset_t offset, void *x6, void *x7);
extern file_t *vn_getf(int fd);
extern void vn_releasef(int fd);
extern void vn_areleasef(int fd, uf_info_t *fip);
extern int vn_set_pwd(const char *filename);

int spl_vn_init(void);
void spl_vn_fini(void);

#define VOP_CLOSE				vn_close
#define VOP_SEEK				vn_seek
#define VOP_GETATTR				vn_getattr
#define VOP_FSYNC				vn_fsync
#define VOP_SPACE				vn_space
#define VOP_PUTPAGE(vp, o, s, f, x1, x2)	((void)0)
#define vn_is_readonly(vp)			0
#define getf					vn_getf
#define releasef				vn_releasef
#define areleasef				vn_areleasef

extern vnode_t *rootdir;

#endif /* SPL_VNODE_H */
