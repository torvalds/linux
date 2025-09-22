/*	$OpenBSD: vnode.h,v 1.178 2025/09/20 13:53:36 mpi Exp $	*/
/*	$NetBSD: vnode.h,v 1.38 1996/02/29 20:59:05 cgd Exp $	*/

/*
 * Copyright (c) 1989, 1993
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
 *	@(#)vnode.h	8.11 (Berkeley) 11/21/94
 */

#ifndef _SYS_VNODE_H_
#define _SYS_VNODE_H_

#include <sys/buf.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/queue.h>
#include <sys/tree.h>

/*
 * The vnode is the focus of all file activity in UNIX.  There is a
 * unique vnode allocated for each active file, each current directory,
 * each mounted-on file, text file, and the root.
 */

/*
 * Vnode types.  VNON means no type.
 */
enum vtype	{ VNON, VREG, VDIR, VBLK, VCHR, VLNK, VSOCK, VFIFO, VBAD };

#define	VTYPE_NAMES \
    "VNON", "VREG", "VDIR", "VBLK", "VCHR", "VLNK", "VSOCK", "VFIFO", "VBAD"

/*
 * Vnode tag types.
 * These are for the benefit of external programs only (e.g., pstat)
 * and should NEVER be inspected by the kernel.
 *
 * Note that v_tag is actually used to tell MFS from FFS, and EXT2FS from
 * the rest, so don't believe the above comment!
 */
enum vtagtype	{
	VT_NON, VT_UFS, VT_NFS, VT_MFS, VT_MSDOSFS,
	VT_PORTAL, VT_PROCFS, VT_AFS, VT_ISOFS, VT_ADOSFS,
	VT_EXT2FS, VT_VFS, VT_NTFS, VT_UDF, VT_FUSEFS, VT_TMPFS,
};

#define	VTAG_NAMES \
    "NON", "UFS", "NFS", "MFS", "MSDOSFS",			\
    "unused", "unused", "unused", "ISOFS", "unused",		\
    "EXT2FS", "VFS", "NTFS", "UDF", "FUSEFS", "TMPFS"

/*
 * Each underlying filesystem allocates its own private area and hangs
 * it from v_data.  If non-null, this area is freed in getnewvnode().
 */
LIST_HEAD(buflists, buf);

RBT_HEAD(buf_rb_bufs, buf);

struct namecache;
RBT_HEAD(namecache_rb_cache, namecache);

/*
 * Locks used to protect struct members in struct vnode:
 *	a	atomic
 *	V	vnode_mtx
 *	B	IPL_BIO
 */
struct uvm_vnode;
struct vnode {
	struct uvm_vnode *v_uvm;	/* uvm data */
	const struct vops *v_op;	/* vnode operations vector */
	enum	vtype v_type;		/* vnode type */
	enum	vtagtype v_tag;		/* type of underlying data */
	u_int	v_flag;			/* vnode flags (see below) */
	u_int	v_lflag;		/* [V] lock vnode flags */
	u_int   v_usecount;		/* reference count of users */
	u_int   v_uvcount;		/* unveil references */
	u_int   v_writecount;		/* reference count of writers */
	u_int	v_lockcount;		/* [V] # threads waiting on lock */

	u_int   v_bioflag;		/* [B] flags accessed in interrupts */
	u_int   v_holdcnt;		/* [B] buffer references */
	u_int   v_id;				/* capability identifier */
	struct	mount *v_mount;			/* ptr to vfs we are in */
	TAILQ_ENTRY(vnode) v_freelist;	/* [B] vnode freelist */
	TAILQ_ENTRY(vnode) v_mntvnodes;		/* vnodes for mount point */
	struct	buf_rb_bufs v_bufs_tree;/* [B] lookup of all bufs */
	struct	buflists v_cleanblkhd;	/* [B] clean blocklist head */
	struct	buflists v_dirtyblkhd;	/* [B] dirty blocklist head */
	u_int   v_numoutput;		/* [B] num of writes in progress */
	LIST_ENTRY(vnode) v_synclist;	/* [B] vnode with dirty buffers */
	union {
		struct mount	*vu_mountedhere;/* ptr to mounted vfs (VDIR) */
		struct socket	*vu_socket;	/* unix ipc (VSOCK) */
		struct specinfo	*vu_specinfo;	/* device (VCHR, VBLK) */
		struct fifoinfo	*vu_fifoinfo;	/* fifo (VFIFO) */
	} v_un;

	/* VFS namecache */
	struct namecache_rb_cache v_nc_tree;
	TAILQ_HEAD(, namecache) v_cache_dst;	 /* cache entries to us */

	void	*v_data;			/* private data for fs */
	struct	klist v_klist;			/* identity of poller(s) */
};
#define	v_mountedhere	v_un.vu_mountedhere
#define	v_socket	v_un.vu_socket
#define	v_specinfo	v_un.vu_specinfo
#define	v_fifoinfo	v_un.vu_fifoinfo

/*
 * Vnode flags.
 */
#define	VROOT		0x0001	/* root of its file system */
#define	VTEXT		0x0002	/* vnode is a pure text prototype */
#define	VSYSTEM		0x0004	/* vnode being used by kernel */
#define	VISTTY		0x0008	/* vnode represents a tty */
#define	VXLOCK		0x0100	/* vnode is locked to change underlying type */
#define	VXWANT		0x0200	/* process is waiting for vnode */
#define	VCLONED		0x0400	/* vnode was cloned */
#define	VALIASED	0x0800	/* vnode has an alias */
#define	VLARVAL		0x1000	/* vnode data not yet set up by higher level */
#define	VLOCKSWORK	0x4000	/* FS supports locking discipline */
#define	VCLONE		0x8000	/* vnode is a clone */

/*
 * (v_bioflag) Flags that may be manipulated by interrupt handlers
 */
#define	VBIOWAIT	0x0001	/* waiting for output to complete */
#define VBIOONSYNCLIST	0x0002	/* Vnode is on syncer worklist */
#define VBIOONFREELIST  0x0004  /* Vnode is on a free list */
#define VBIOERROR	0x0008  /* A write failed */

/*
 * Vnode attributes.  A field value of VNOVAL represents a field whose value
 * is unavailable (getattr) or which is not to be changed (setattr).  For
 * the timespec fields, only the tv_nsec member needs to be set to VNOVAL:
 * if tv_nsec != VNOVAL then both tv_sec and tv_nsec are valid.
 */
struct vattr {
	enum vtype	va_type;	/* vnode type (for create) */
	mode_t		va_mode;	/* files access mode and type */
	nlink_t		va_nlink;	/* number of references to file */
	uid_t		va_uid;		/* owner user id */
	gid_t		va_gid;		/* owner group id */
	long		va_fsid;	/* file system id (dev for now) */
	u_quad_t	va_fileid;	/* file id */
	u_quad_t	va_size;	/* file size in bytes */
	long		va_blocksize;	/* blocksize preferred for i/o */
	struct timespec	va_atime;	/* time of last access */
	struct timespec	va_mtime;	/* time of last modification */
	struct timespec	va_ctime;	/* time file changed */
	u_long		va_gen;		/* generation number of file */
	u_long		va_flags;	/* flags defined for file */
	dev_t		va_rdev;	/* device the special file represents */
	u_quad_t	va_bytes;	/* bytes of disk space held by file */
	u_quad_t	va_filerev;	/* file modification number */
	u_int		va_vaflags;	/* operations flags, see below */
	long		va_spare;	/* remain quad aligned */
};

/*
 * Flags for va_vaflags.
 */
#define	VA_UTIMES_NULL		0x01	/* utimes argument was NULL */
#define	VA_EXCLUSIVE		0x02	/* exclusive create request */
#define	VA_UTIMES_CHANGE	0x04	/* ctime should be updated */
/*
 * Flags for ioflag.
 */
#define	IO_UNIT		0x01		/* do I/O as atomic unit */
#define	IO_APPEND	0x02		/* append write to end */
#define	IO_SYNC		0x04		/* do I/O synchronously */
#define	IO_NODELOCKED	0x08		/* underlying node already locked */
#define	IO_NDELAY	0x10		/* FNDELAY flag set in file table */
#define	IO_NOLIMIT	0x20		/* don't enforce limits on i/o */
#define	IO_NOCACHE	0x40		/* don't cache result of this i/o */

/*
 *  Modes.  Some values same as Ixxx entries from inode.h for now.
 */
#define	VSUID	04000		/* set user id on execution */
#define	VSGID	02000		/* set group id on execution */
#define	VSVTX	01000		/* save swapped text even after use */
#define	VREAD	00400		/* read, write, execute permissions */
#define	VWRITE	00200
#define	VEXEC	00100

/*
 * Token indicating no attribute value yet assigned.
 */
#define	VNOVAL	(-1)

#ifdef _KERNEL
RBT_PROTOTYPE(buf_rb_bufs, buf, b_rbbufs, rb_buf_compare);
/*
 * Convert between vnode types and inode formats (since POSIX.1
 * defines mode word of stat structure in terms of inode formats).
 */
extern enum vtype	iftovt_tab[];
extern int		vttoif_tab[];
#define IFTOVT(mode)	(iftovt_tab[((mode) & S_IFMT) >> 12])
#define VTTOIF(indx)	(vttoif_tab[(int)(indx)])
#define MAKEIMODE(indx, mode)	(int)(VTTOIF(indx) | (mode))

/*
 * Flags to various vnode functions.
 */
#define	SKIPSYSTEM	0x0001		/* vflush: skip vnodes marked VSYSTEM */
#define	FORCECLOSE	0x0002		/* vflush: force file closure */
#define	WRITECLOSE	0x0004		/* vflush: only close writeable files */
#define	DOCLOSE		0x0008		/* vclean: close active files */
#define	IGNORECLEAN	0x0010		/* vflush: ignore clean vnodes */
#define	V_SAVE		0x0001		/* vinvalbuf: sync file first */
#define	V_SAVEMETA	0x0002		/* vinvalbuf: leave indirect blocks */

#define REVOKEALL	0x0001		/* vop_revoke: revoke all aliases */


#define	VN_KNOTE(vp, b)					\
	knote_locked(&vp->v_klist, (b))

/*
 * Global vnode data.
 */
extern	struct vnode *rootvnode;	/* root (i.e. "/") vnode */
extern	int initialvnodes;		/* XXX number of vnodes to start */
extern	int maxvnodes;			/* XXX number of vnodes to allocate */
extern	int syncdelay;			/* seconds to delay syncing vnodes */
extern	int rushjob;			/* # of slots syncer should run ASAP */
extern	struct mutex vnode_mtx;
extern void    vhold(struct vnode *);
extern void    vdrop(struct vnode *);

/* vnode operations */
struct vops {
	int	(*vop_lock)(void *);
	int	(*vop_unlock)(void *);
	int	(*vop_islocked)(void *);
	int	(*vop_abortop)(void *);
	int	(*vop_access)(void *);
	int	(*vop_advlock)(void *);
	int	(*vop_bmap)(void *);
	int	(*vop_bwrite)(void *);
	int	(*vop_close)(void *);
	int	(*vop_create)(void *);
	int	(*vop_fsync)(void *);
	int	(*vop_getattr)(void *);
	int	(*vop_inactive)(void *);
	int	(*vop_ioctl)(void *);
	int	(*vop_link)(void *);
	int	(*vop_lookup)(void *);
	int	(*vop_mknod)(void *);
	int	(*vop_open)(void *);
	int	(*vop_pathconf)(void *);
	int	(*vop_print)(void *);
	int	(*vop_read)(void *);
	int	(*vop_readdir)(void *);
	int	(*vop_readlink)(void *);
	int	(*vop_reclaim)(void *);
	int	(*vop_remove)(void *);
	int	(*vop_rename)(void *);
	int	(*vop_revoke)(void *);
	int	(*vop_mkdir)(void *);
	int	(*vop_rmdir)(void *);
	int	(*vop_setattr)(void *);
	int	(*vop_strategy)(void *);
	int	(*vop_symlink)(void *);
	int	(*vop_write)(void *);
	int	(*vop_kqfilter)(void *);
};

extern const struct vops dead_vops;
extern const struct vops spec_vops;

struct vop_generic_args {
	void		*a_garbage;
	/* Other data probably follows; */
};

struct vop_islocked_args {
	struct vnode *a_vp;
};
int VOP_ISLOCKED(struct vnode *);

struct vop_lookup_args {
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
};
int VOP_LOOKUP(struct vnode *, struct vnode **, struct componentname *);

struct vop_create_args {
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
};
int VOP_CREATE(struct vnode *, struct vnode **, struct componentname *,
    struct vattr *);

struct vop_mknod_args {
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
};
int VOP_MKNOD(struct vnode *, struct vnode **, struct componentname *,
    struct vattr *);

struct vop_open_args {
	struct vnode *a_vp;
	int a_mode;
	struct ucred *a_cred;
	struct proc *a_p;
};
int VOP_OPEN(struct vnode *, int, struct ucred *, struct proc *);

struct vop_close_args {
	struct vnode *a_vp;
	int a_fflag;
	struct ucred *a_cred;
	struct proc *a_p;
};
int VOP_CLOSE(struct vnode *, int, struct ucred *, struct proc *);

struct vop_access_args {
	struct vnode *a_vp;
	int a_mode;
	struct ucred *a_cred;
	struct proc *a_p;
};
int VOP_ACCESS(struct vnode *, int, struct ucred *, struct proc *);

struct vop_getattr_args {
	struct vnode *a_vp;
	struct vattr *a_vap;
	struct ucred *a_cred;
	struct proc *a_p;
};
int VOP_GETATTR(struct vnode *, struct vattr *, struct ucred *, struct proc *);

struct vop_setattr_args {
	struct vnode *a_vp;
	struct vattr *a_vap;
	struct ucred *a_cred;
	struct proc *a_p;
};
int VOP_SETATTR(struct vnode *, struct vattr *, struct ucred *, struct proc *);

struct vop_read_args {
	struct vnode *a_vp;
	struct uio *a_uio;
	int a_ioflag;
	struct ucred *a_cred;
};
int VOP_READ(struct vnode *, struct uio *, int, struct ucred *);

struct vop_write_args {
	struct vnode *a_vp;
	struct uio *a_uio;
	int a_ioflag;
	struct ucred *a_cred;
};
int VOP_WRITE(struct vnode *, struct uio *, int, struct ucred *);

struct vop_ioctl_args {
	struct vnode *a_vp;
	u_long a_command;
	void *a_data;
	int a_fflag;
	struct ucred *a_cred;
	struct proc *a_p;
};
int VOP_IOCTL(struct vnode *, u_long, void *, int, struct ucred *,
    struct proc *);

struct vop_kqfilter_args {
	struct vnode *a_vp;
	int a_fflag;
	struct knote *a_kn;
};
int VOP_KQFILTER(struct vnode *, int, struct knote *);

struct vop_revoke_args {
	struct vnode *a_vp;
	int a_flags;
};
int VOP_REVOKE(struct vnode *, int);

struct vop_fsync_args {
	struct vnode *a_vp;
	struct ucred *a_cred;
	int a_waitfor;
	struct proc *a_p;
};
int VOP_FSYNC(struct vnode *, struct ucred *, int, struct proc *);

struct vop_remove_args {
	struct vnode *a_dvp;
	struct vnode *a_vp;
	struct componentname *a_cnp;
};
int VOP_REMOVE(struct vnode *, struct vnode *, struct componentname *);

struct vop_link_args {
	struct vnode *a_dvp;
	struct vnode *a_vp;
	struct componentname *a_cnp;
};
int VOP_LINK(struct vnode *, struct vnode *, struct componentname *);

struct vop_rename_args {
	struct vnode *a_fdvp;
	struct vnode *a_fvp;
	struct componentname *a_fcnp;
	struct vnode *a_tdvp;
	struct vnode *a_tvp;
	struct componentname *a_tcnp;
};
int VOP_RENAME(struct vnode *, struct vnode *, struct componentname *,
    struct vnode *, struct vnode *, struct componentname *);

struct vop_mkdir_args {
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
};
int VOP_MKDIR(struct vnode *, struct vnode **, struct componentname *,
    struct vattr *);

struct vop_rmdir_args {
	struct vnode *a_dvp;
	struct vnode *a_vp;
	struct componentname *a_cnp;
};
int VOP_RMDIR(struct vnode *, struct vnode *, struct componentname *);

struct vop_symlink_args {
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
	char *a_target;
};
int VOP_SYMLINK(struct vnode *, struct vnode **, struct componentname *,
    struct vattr *, char *);

struct vop_readdir_args {
	struct vnode *a_vp;
	struct uio *a_uio;
	struct ucred *a_cred;
	int *a_eofflag;
};
int VOP_READDIR(struct vnode *, struct uio *, struct ucred *, int *);

struct vop_readlink_args {
	struct vnode *a_vp;
	struct uio *a_uio;
	struct ucred *a_cred;
};
int VOP_READLINK(struct vnode *, struct uio *, struct ucred *);

struct vop_abortop_args {
	struct vnode *a_dvp;
	struct componentname *a_cnp;
};
int VOP_ABORTOP(struct vnode *, struct componentname *);

struct vop_inactive_args {
	struct vnode *a_vp;
	struct proc *a_p;
};
int VOP_INACTIVE(struct vnode *, struct proc *);

struct vop_reclaim_args {
	struct vnode *a_vp;
	struct proc *a_p;
};
int VOP_RECLAIM(struct vnode *, struct proc *);

struct vop_lock_args {
	struct vnode *a_vp;
	int a_flags;
};
int VOP_LOCK(struct vnode *, int);

struct vop_unlock_args {
	struct vnode *a_vp;
};
int VOP_UNLOCK(struct vnode *);

struct vop_bmap_args {
	struct vnode *a_vp;
	daddr_t a_bn;
	struct vnode **a_vpp;
	daddr_t *a_bnp;
	int *a_runp;
};
int VOP_BMAP(struct vnode *, daddr_t, struct vnode **, daddr_t *, int *);

struct vop_print_args {
	struct vnode *a_vp;
};
int VOP_PRINT(struct vnode *);

struct vop_pathconf_args {
	struct vnode *a_vp;
	int a_name;
	register_t *a_retval;
};
int VOP_PATHCONF(struct vnode *, int, register_t *);

struct vop_advlock_args {
	struct vnode *a_vp;
	void *a_id;
	int a_op;
	struct flock *a_fl;
	int a_flags;
};
int VOP_ADVLOCK(struct vnode *, void *, int, struct flock *, int);

struct vop_strategy_args {
	struct vnode *a_vp;
	struct buf *a_bp;
};
int VOP_STRATEGY(struct vnode *, struct buf *);

/* Special cases: */
struct vop_bwrite_args {
	struct buf *a_bp;
};
int VOP_BWRITE(struct buf *);
/* End of special cases. */


/* Public vnode manipulation functions. */
struct file;
struct mount;
struct nameidata;
struct proc;
struct stat;
struct statfs;
struct ucred;
struct uio;
struct vattr;
struct vnode;

/* vfs_subr */
int	bdevvp(dev_t, struct vnode **);
int	cdevvp(dev_t, struct vnode **);
struct vnode *checkalias(struct vnode *, dev_t, struct mount *);
int	getnewvnode(enum vtagtype, struct mount *, const struct vops *,
	    struct vnode **);
int	vaccess(enum vtype, mode_t, uid_t, gid_t, mode_t, struct ucred *);
int	vnoperm(struct vnode *);
void	vattr_null(struct vattr *);
void	vdevgone(int, int, int, enum vtype);
int	vcount(struct vnode *);
int	vfinddev(dev_t, enum vtype, struct vnode **);
void	vflushbuf(struct vnode *, int);
int	vflush(struct mount *, struct vnode *, int);
int	vget(struct vnode *, int);
void	vgone(struct vnode *);
void	vgonel(struct vnode *, struct proc *);
int	vinvalbuf(struct vnode *, int, struct ucred *, struct proc *,
	    int, uint64_t);
void	vntblinit(void);
int	vwaitforio(struct vnode *, int, char *, uint64_t);
void	vwakeup(struct vnode *);
void	vput(struct vnode *);
int	vrecycle(struct vnode *, struct proc *);
int	vrele(struct vnode *);
void	vref(struct vnode *);
void	vprint(char *, struct vnode *);
void	copy_statfs_info(struct statfs *, const struct mount *);

/* vfs_getcwd.c */
#define GETCWD_CHECK_ACCESS 0x0001
int vfs_getcwd_scandir(struct vnode **, struct vnode **, char **, char *,
    struct proc *);
int vfs_getcwd_common(struct vnode *, struct vnode *, char **, char *, int,
    int, struct proc *);
int vfs_getcwd_getcache(struct vnode **, struct vnode **, char **, char *);

/* vfs_default.c */
int	vop_generic_abortop(void *);
int	vop_generic_badop(void *);
int	vop_generic_bmap(void *);
int	vop_generic_bwrite(void *);
int	vop_generic_revoke(void *);
int	vop_generic_lookup(void *);

/* vfs_vnops.c */
int	vn_isunder(struct vnode *, struct vnode *, struct proc *);
int	vn_close(struct vnode *, int, struct ucred *, struct proc *);
int	vn_open(struct nameidata *, int, int);
int	vn_rdwr(enum uio_rw, struct vnode *, caddr_t, int, off_t,
	    enum uio_seg, int, struct ucred *, size_t *, struct proc *);
int	vn_stat(struct vnode *, struct stat *, struct proc *);
int	vn_statfile(struct file *, struct stat *, struct proc *);
int	vn_lock(struct vnode *, int);
int	vn_writechk(struct vnode *);
int	vn_fsizechk(struct vnode *, struct uio *, int, ssize_t *);
int	vn_ioctl(struct file *, u_long, caddr_t, struct proc *);
void	vn_marktext(struct vnode *);

/* vfs_sync.c */
void	syncer_thread(void *);
void	vn_initialize_syncerd(void);
void	vn_syncer_add_to_worklist(struct vnode *, int);

/* misc */
int 	getvnode(struct proc *, int, struct file **);

/* uvm */
void	uvm_vnp_setsize(struct vnode *, off_t);
void	uvm_vnp_sync(struct mount *);
void	uvm_vnp_terminate(struct vnode *);
int	uvm_vnp_uncache(struct vnode *);


#endif /* _KERNEL */
#endif /* _SYS_VNODE_H_ */
