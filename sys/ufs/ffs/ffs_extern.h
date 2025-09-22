/*	$OpenBSD: ffs_extern.h,v 1.51 2024/10/08 02:58:26 jsg Exp $	*/
/*	$NetBSD: ffs_extern.h,v 1.4 1996/02/09 22:22:22 christos Exp $	*/

/*
 * Copyright (c) 1991, 1993, 1994
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
 *	@(#)ffs_extern.h	8.3 (Berkeley) 4/16/94
 */

#define FFS_CLUSTERREAD		1	/* cluster reading enabled */
#define FFS_CLUSTERWRITE	2	/* cluster writing enabled */
#define FFS_REALLOCBLKS		3	/* block reallocation enabled */
#define FFS_ASYNCFREE		4	/* asynchronous block freeing enabled */
#define	FFS_MAX_SOFTDEPS	5	/* maximum structs before slowdown */
#define	FFS_SD_TICKDELAY	6	/* ticks to pause during slowdown */
#define	FFS_SD_WORKLIST_PUSH	7	/* # of worklist cleanups */
#define	FFS_SD_BLK_LIMIT_PUSH	8	/* # of times block limit neared */
#define	FFS_SD_INO_LIMIT_PUSH	9	/* # of times inode limit neared */
#define	FFS_SD_BLK_LIMIT_HIT	10	/* # of times block slowdown imposed */
#define	FFS_SD_INO_LIMIT_HIT	11	/* # of times inode slowdown imposed */
#define	FFS_SD_SYNC_LIMIT_HIT	12	/* # of synchronous slowdowns imposed */
#define	FFS_SD_INDIR_BLK_PTRS	13	/* bufs redirtied as indir ptrs not written */
#define	FFS_SD_INODE_BITMAP	14	/* bufs redirtied as inode bitmap not written */
#define	FFS_SD_DIRECT_BLK_PTRS	15	/* bufs redirtied as direct ptrs not written */
#define	FFS_SD_DIR_ENTRY	16	/* bufs redirtied as dir entry cannot write */
#define	FFS_DIRHASH_DIRSIZE	17	/* min directory size, in bytes */
#define	FFS_DIRHASH_MAXMEM	18	/* max kvm to use, in bytes */
#define	FFS_DIRHASH_MEM		19	/* current mem usage, in bytes */
#define	FFS_MAXID		20	/* number of valid ffs ids */

#define FFS_NAMES { \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "dirhash_dirsize", CTLTYPE_INT }, \
	{ "dirhash_maxmem", CTLTYPE_INT }, \
	{ "dirhash_mem", CTLTYPE_INT }, \
}

struct buf;
struct fid;
struct fs;
struct inode;
struct mount;
struct nameidata;
struct proc;
struct statfs;
struct timeval;
struct ucred;
struct ufsmount;
struct vfsconf;
struct uio;
struct vnode;
struct mbuf;
struct cg;
struct vop_vfree_args;

extern const struct vops	ffs_vops;
extern const struct vops	ffs_specvops;
extern const struct vops	ffs_fifovops;

/* ffs_alloc.c */
int ffs_alloc(struct inode *, daddr_t, daddr_t , int, struct ucred *,
		   daddr_t *);
int ffs_realloccg(struct inode *, daddr_t, daddr_t, int, int ,
		       struct ucred *, struct buf **, daddr_t *);
int ffs_inode_alloc(struct inode *, mode_t, struct ucred *, struct vnode **);
int ffs_inode_free(struct inode *, ufsino_t, mode_t);
int ffs_freefile(struct inode *, ufsino_t, mode_t);

int32_t ffs1_blkpref(struct inode *, daddr_t, int, int32_t *);
#ifdef FFS2
int64_t ffs2_blkpref(struct inode *, daddr_t, int, int64_t *);
#endif
void ffs_blkfree(struct inode *, daddr_t, long);
void ffs_clusteracct(struct fs *, struct cg *, daddr_t, int);

/* ffs_balloc.c */
int ffs_balloc(struct inode *, off_t, int, struct ucred *, int, struct buf **);

/* ffs_inode.c */
int ffs_init(struct vfsconf *);
int ffs_update(struct inode *, int);
int ffs_truncate(struct inode *, off_t, int, struct ucred *);

/* ffs_subr.c */
int  ffs_bufatoff(struct inode *, off_t, char **, struct buf **);
void ffs_fragacct(struct fs *, int, int32_t[], int);
int  ffs_isfreeblock(struct fs *, u_char *, daddr_t);
int  ffs_isblock(struct fs *, u_char *, daddr_t);
void ffs_clrblock(struct fs *, u_char *, daddr_t);
void ffs_setblock(struct fs *, u_char *, daddr_t);
int  ffs_vinit(struct mount *, struct vnode **);

/* ffs_vfsops.c */
int ffs_mountroot(void);
int ffs_mount(struct mount *, const char *, void *, struct nameidata *,
		   struct proc *);
int ffs_reload(struct mount *, struct ucred *, struct proc *);
int ffs_mountfs(struct vnode *, struct mount *, struct proc *);
int ffs_oldfscompat(struct fs *);
int ffs_unmount(struct mount *, int, struct proc *);
int ffs_flushfiles(struct mount *, int, struct proc *);
int ffs_statfs(struct mount *, struct statfs *, struct proc *);
int ffs_sync(struct mount *, int, int, struct ucred *, struct proc *);
int ffs_vget(struct mount *, ino_t, struct vnode **);
int ffs_fhtovp(struct mount *, struct fid *, struct vnode **);
int ffs_vptofh(struct vnode *, struct fid *);
int ffs_sysctl(int *, u_int, void *, size_t *, void *, size_t,
		    struct proc *);
int ffs_sbupdate(struct ufsmount *, int);

/* ffs_vnops.c */
int ffs_read(void *);
int ffs_write(void *);
int ffs_fsync(void *);
int ffs_reclaim(void *);
int ffsfifo_reclaim(void *);

/*
 * Soft dependency function prototypes.
 */

struct vop_vfree_args;
struct vop_fsync_args;

extern struct pool ffs_ino_pool;	/* memory pool for inodes */
extern struct pool ffs_dinode1_pool;	/* memory pool for UFS1 dinodes */
#ifdef FFS2
extern struct pool ffs_dinode2_pool;	/* memory pool for UFS2 dinodes */
#endif
