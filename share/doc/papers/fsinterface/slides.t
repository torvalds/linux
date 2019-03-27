.\" Copyright (c) 1986 The Regents of the University of California.
.\" All rights reserved.
.\"
.\" Redistribution and use in source and binary forms, with or without
.\" modification, are permitted provided that the following conditions
.\" are met:
.\" 1. Redistributions of source code must retain the above copyright
.\"    notice, this list of conditions and the following disclaimer.
.\" 2. Redistributions in binary form must reproduce the above copyright
.\"    notice, this list of conditions and the following disclaimer in the
.\"    documentation and/or other materials provided with the distribution.
.\" 3. Neither the name of the University nor the names of its contributors
.\"    may be used to endorse or promote products derived from this software
.\"    without specific prior written permission.
.\"
.\" THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
.\" ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
.\" IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
.\" ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
.\" FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
.\" DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
.\" OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
.\" HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
.\" LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
.\" OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
.\" SUCH DAMAGE.
.\"
.\"	@(#)slides.t	5.2 (Berkeley) 4/16/91
.\"
.so macros
.nf
.LL
Encapsulation of namei parameters
.NP 0
.ta .5i +\w'caddr_t\0\0'u +\w'struct\0\0'u +\w'vnode *nc_prevdir;\0\0\0\0\0'u
struct nameidata {
   /* arguments and context: */
	caddr_t	ni_dirp;
	enum	uio_seg ni_seg;
	short	ni_nameiop;
	struct	vnode *ni_cdir;
	struct	vnode *ni_rdir;
	struct	ucred *ni_cred;
.sp .2
   /* shared with lookup and commit: */
	caddr_t	ni_pnbuf;
	char	*ni_ptr;
	int	ni_pathlen;
	short	ni_more;
	short	ni_loopcnt;
.sp .2
   /* results: */
	struct	vnode *ni_vp;
	struct	vnode *ni_dvp;
.sp .2
/* BEGIN UFS SPECIFIC */
	struct diroffcache {
		struct	vnode *nc_prevdir;
		long	nc_id;
		off_t	nc_prevoffset;
	} ni_nc;
/* END UFS SPECIFIC */
};
.bp


.LL
Namei operations and modifiers

.NP 0
.ta \w'#define\0\0'u +\w'WANTPARENT\0\0'u +\w'0x40\0\0\0\0\0\0\0'u
#define	LOOKUP	0	/* name lookup only */
#define	CREATE	1	/* setup for creation */
#define	DELETE	2	/* setup for deletion */
#define	WANTPARENT	0x10	/* return parent vnode also */
#define	NOCACHE	0x20	/* remove name from cache */
#define	FOLLOW	0x40	/* follow symbolic links */
.bp

.LL
Namei operations and modifiers

.NP 0
.ta \w'#define\0\0'u +\w'WANTPARENT\0\0'u +\w'0x40\0\0\0\0\0\0\0'u
#define	LOOKUP	0
#define	CREATE	1
#define	DELETE	2
#define	WANTPARENT	0x10
#define	NOCACHE	0x20
#define	FOLLOW	0x40
.bp


.LL
Credentials

.NP 0
.ta .5i +\w'caddr_t\0\0\0'u +\w'struct\0\0'u +\w'vnode *nc_prevdir;\0\0\0\0\0'u
struct ucred {
	u_short	cr_ref;
	uid_t	cr_uid;
	short	cr_ngroups;
	gid_t	cr_groups[NGROUPS];
	/*
	 * The following either should not be here,
	 * or should be treated as opaque.
	 */
	uid_t   cr_ruid;
	gid_t   cr_svgid;
};
.bp
.LL
Scatter-gather I/O
.NP 0
.ta .5i +\w'caddr_t\0\0\0'u +\w'struct\0\0'u +\w'vnode *nc_prevdir;\0\0\0\0\0'u
struct uio {
	struct	iovec *uio_iov;
	int	uio_iovcnt;
	off_t	uio_offset;
	int	uio_resid;
	enum	uio_rw uio_rw;
};

enum	uio_rw { UIO_READ, UIO_WRITE };



.ta .5i +\w'caddr_t\0\0\0'u +\w'vnode *nc_prevdir;\0\0\0\0\0'u
struct iovec {
	caddr_t	iov_base;
	int	iov_len;
	enum	uio_seg iov_segflg;
	int	(*iov_op)();
};
.bp
.LL
Per-filesystem information
.NP 0
.ta .25i +\w'struct vfsops\0\0\0'u +\w'*vfs_vnodecovered;\0\0\0\0\0'u
struct vfs {
	struct vfs	*vfs_next;
\fB+\fP	struct vfs	*vfs_prev;
	struct vfsops	*vfs_op;
	struct vnode	*vfs_vnodecovered;
	int	vfs_flag;
\fB!\fP	int	vfs_fsize;
\fB+\fP	int	vfs_bsize;
\fB!\fP	uid_t	vfs_exroot;
	short	vfs_exflags;
	caddr_t	vfs_data;
};

.NP 0
.ta \w'\fB+\fP 'u +\w'#define\0\0'u +\w'VFS_EXPORTED\0\0'u +\w'0x40\0\0\0\0\0'u
	/* vfs flags: */
	#define	VFS_RDONLY	0x01
\fB+\fP	#define	VFS_NOEXEC	0x02
	#define	VFS_MLOCK	0x04
	#define	VFS_MWAIT	0x08
	#define	VFS_NOSUID	0x10
	#define	VFS_EXPORTED	0x20

	/* exported vfs flags: */
	#define	EX_RDONLY	0x01
.bp


.LL
Operations supported on virtual file system.

.NP 0
.ta .25i +\w'int\0\0'u +\w'*vfs_mountroot();\0'u
struct vfsops {
\fB!\fP	int	(*vfs_mount)(vfs, path, data, len);
\fB!\fP	int	(*vfs_unmount)(vfs, forcibly);
\fB+\fP	int	(*vfs_mountroot)();
	int	(*vfs_root)(vfs, vpp);
	int	(*vfs_statfs)(vfs, sbp);
\fB!\fP	int	(*vfs_sync)(vfs, waitfor);
\fB+\fP	int	(*vfs_fhtovp)(vfs, fhp, vpp);
\fB+\fP	int	(*vfs_vptofh)(vp, fhp);
};
.bp


.LL
Dynamic file system information

.NP 0
.ta .5i +\w'struct\0\0\0'u +\w'*vfs_vnodecovered;\0\0\0\0\0'u
struct statfs {
\fB!\fP	short	f_type;
\fB+\fP	short	f_flags;
\fB!\fP	long	f_fsize;
\fB+\fP	long	f_bsize;
	long	f_blocks;
	long	f_bfree;
	long	f_bavail;
	long	f_files;
	long	f_ffree;
	fsid_t	f_fsid;
\fB+\fP	char	*f_mntonname;
\fB+\fP	char	*f_mntfromname;
	long	f_spare[7];
};

typedef long fsid_t[2];
.bp
.LL
Filesystem objects (vnodes)
.NP 0
.ta .25i +\w'struct vnodeops\0\0'u +\w'*v_vfsmountedhere;\0\0\0'u
enum vtype 	{ VNON, VREG, VDIR, VBLK, VCHR, VLNK, VSOCK };

struct vnode {
	u_short	v_flag;
	u_short	v_count;
	u_short	v_shlockc;
	u_short	v_exlockc;
	struct vfs	*v_vfsmountedhere;
	struct vfs	*v_vfsp;
	struct vnodeops	*v_op;
\fB+\fP	struct text	*v_text;
	enum vtype	v_type;
	caddr_t	v_data;
};
.ta \w'#define\0\0'u +\w'NOFOLLOW\0\0'u +\w'0x40\0\0\0\0\0\0\0'u

/* vnode flags */
#define	VROOT	0x01
#define	VTEXT	0x02
#define	VEXLOCK	0x10
#define	VSHLOCK	0x20
#define	VLWAIT	0x40
.bp
.LL
Operations on vnodes

.NP 0
.ta .25i +\w'int\0\0'u  +\w'(*vn_getattr)(\0\0\0\0\0'u
struct vnodeops {
\fB!\fP	int	(*vn_lookup)(ndp);
\fB!\fP	int	(*vn_create)(ndp, vap, fflags);
\fB+\fP	int	(*vn_mknod)(ndp, vap, fflags);
\fB!\fP	int	(*vn_open)(vp, fflags, cred);
	int	(*vn_close)(vp, fflags, cred);
	int	(*vn_access)(vp, fflags, cred);
	int	(*vn_getattr)(vp, vap, cred);
	int	(*vn_setattr)(vp, vap, cred);
.sp .5
\fB+\fP	int	(*vn_read)(vp, uiop,
		        offp, ioflag, cred);
\fB+\fP	int	(*vn_write)(vp, uiop,
		        offp, ioflag, cred);
\fB!\fP	int	(*vn_ioctl)(vp, com,
		        data, fflag, cred);
	int	(*vn_select)(vp, which, cred);
\fB+\fP	int	(*vn_mmap)(vp, ..., cred);
	int	(*vn_fsync)(vp, cred);
\fB+\fP	int	(*vn_seek)(vp, offp, off,
		        whence);
.bp
.LL
Operations on vnodes (cont)

.NP 0
.ta .25i +\w'int\0\0'u  +\w'(*vn_getattr)(\0\0\0\0\0'u

\fB!\fP	int	(*vn_remove)(ndp);
\fB!\fP	int	(*vn_link)(vp, ndp);
\fB!\fP	int	(*vn_rename)(sndp, tndp);
\fB!\fP	int	(*vn_mkdir)(ndp, vap);
\fB!\fP	int	(*vn_rmdir)(ndp);
\fB!\fP	int	(*vn_symlink)(ndp, vap, nm);
\fB!\fP	int	(*vn_readdir)(vp, uiop,
		        offp, ioflag, cred);
\fB!\fP	int	(*vn_readlink)(vp, uiop,
		        offp, ioflag, cred);
.sp .5
\fB+\fP	int	(*vn_abortop)(ndp);
\fB!\fP	int	(*vn_inactive)(vp);
};

.NP 0
.ta \w'#define\0\0'u +\w'NOFOLLOW\0\0'u +\w'0x40\0\0\0\0\0'u
/* flags for ioflag */
#define	IO_UNIT	0x01
#define	IO_APPEND	0x02
#define	IO_SYNC	0x04
.bp

.LL
Vnode attributes

.NP 0
.ta .5i +\w'struct timeval\0\0'u +\w'*v_vfsmountedhere;\0\0\0'u
struct vattr {
	enum vtype	va_type;
	u_short	va_mode;
\fB!\fP	uid_t	va_uid;
\fB!\fP	gid_t	va_gid;
	long	va_fsid;
\fB!\fP	long	va_fileid;
	short	va_nlink;
	u_long	va_size;
\fB+\fP	u_long	va_size1;
	long	va_blocksize;
	struct timeval	va_atime;
	struct timeval	va_mtime;
	struct timeval	va_ctime;
	dev_t	va_rdev;
\fB!\fP	u_long	va_bytes;
\fB+\fP	u_long	va_bytes1;
};
