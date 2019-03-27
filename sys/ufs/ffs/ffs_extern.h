/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *	@(#)ffs_extern.h	8.6 (Berkeley) 3/30/95
 * $FreeBSD$
 */

#ifndef _UFS_FFS_EXTERN_H
#define	_UFS_FFS_EXTERN_H

#ifndef _KERNEL
#error "No user-serving parts inside"
#else

struct buf;
struct cg;
struct fid;
struct fs;
struct inode;
struct malloc_type;
struct mount;
struct thread;
struct sockaddr;
struct statfs;
struct ucred;
struct vnode;
struct vop_fsync_args;
struct vop_reallocblks_args;
struct workhead;

int	ffs_alloc(struct inode *, ufs2_daddr_t, ufs2_daddr_t, int, int,
	    struct ucred *, ufs2_daddr_t *);
int	ffs_balloc_ufs1(struct vnode *a_vp, off_t a_startoffset, int a_size,
            struct ucred *a_cred, int a_flags, struct buf **a_bpp);
int	ffs_balloc_ufs2(struct vnode *a_vp, off_t a_startoffset, int a_size,
            struct ucred *a_cred, int a_flags, struct buf **a_bpp);
int	ffs_blkatoff(struct vnode *, off_t, char **, struct buf **);
void	ffs_blkfree(struct ufsmount *, struct fs *, struct vnode *,
	    ufs2_daddr_t, long, ino_t, enum vtype, struct workhead *, u_long);
ufs2_daddr_t ffs_blkpref_ufs1(struct inode *, ufs_lbn_t, int, ufs1_daddr_t *);
ufs2_daddr_t ffs_blkpref_ufs2(struct inode *, ufs_lbn_t, int, ufs2_daddr_t *);
void	ffs_blkrelease_finish(struct ufsmount *, u_long);
u_long	ffs_blkrelease_start(struct ufsmount *, struct vnode *, ino_t);
uint32_t ffs_calc_sbhash(struct fs *);
int	ffs_checkfreefile(struct fs *, struct vnode *, ino_t);
void	ffs_clrblock(struct fs *, u_char *, ufs1_daddr_t);
void	ffs_clusteracct(struct fs *, struct cg *, ufs1_daddr_t, int);
void	ffs_bdflush(struct bufobj *, struct buf *);
int	ffs_copyonwrite(struct vnode *, struct buf *);
int	ffs_flushfiles(struct mount *, int, struct thread *);
void	ffs_fragacct(struct fs *, int, int32_t [], int);
int	ffs_freefile(struct ufsmount *, struct fs *, struct vnode *, ino_t,
	    int, struct workhead *);
void	ffs_fserr(struct fs *, ino_t, char *);
int	ffs_getcg(struct fs *, struct vnode *, u_int, struct buf **,
	    struct cg **);
int	ffs_isblock(struct fs *, u_char *, ufs1_daddr_t);
int	ffs_isfreeblock(struct fs *, u_char *, ufs1_daddr_t);
int	ffs_load_inode(struct buf *, struct inode *, struct fs *, ino_t);
void	ffs_oldfscompat_write(struct fs *, struct ufsmount *);
int	ffs_own_mount(const struct mount *mp);
int	ffs_reallocblks(struct vop_reallocblks_args *);
int	ffs_realloccg(struct inode *, ufs2_daddr_t, ufs2_daddr_t,
	    ufs2_daddr_t, int, int, int, struct ucred *, struct buf **);
int	ffs_reload(struct mount *, struct thread *, int);
int	ffs_sbget(void *, struct fs **, off_t, struct malloc_type *,
	    int (*)(void *, off_t, void **, int));
int	ffs_sbput(void *, struct fs *, off_t, int (*)(void *, off_t, void *,
	    int));
int	ffs_sbupdate(struct ufsmount *, int, int);
void	ffs_setblock(struct fs *, u_char *, ufs1_daddr_t);
int	ffs_snapblkfree(struct fs *, struct vnode *, ufs2_daddr_t, long, ino_t,
	    enum vtype, struct workhead *);
void	ffs_snapremove(struct vnode *vp);
int	ffs_snapshot(struct mount *mp, char *snapfile);
void	ffs_snapshot_mount(struct mount *mp);
void	ffs_snapshot_unmount(struct mount *mp);
void	ffs_susp_initialize(void);
void	ffs_susp_uninitialize(void);
void	ffs_sync_snap(struct mount *, int);
int	ffs_syncvnode(struct vnode *vp, int waitfor, int flags);
int	ffs_truncate(struct vnode *, off_t, int, struct ucred *);
int	ffs_update(struct vnode *, int);
void	ffs_update_dinode_ckhash(struct fs *, struct ufs2_dinode *);
int	ffs_verify_dinode_ckhash(struct fs *, struct ufs2_dinode *);
int	ffs_valloc(struct vnode *, int, struct ucred *, struct vnode **);
int	ffs_vfree(struct vnode *, ino_t, int);
vfs_vget_t ffs_vget;
int	ffs_vgetf(struct mount *, ino_t, int, struct vnode **, int);
void	process_deferred_inactive(struct mount *mp);

/*
 * Flags to ffs_vgetf
 */
#define	FFSV_FORCEINSMQ	0x0001

/*
 * Flags to ffs_reload
 */
#define	FFSR_FORCE	0x0001
#define	FFSR_UNSUSPEND	0x0002

/*
 * Request standard superblock location in ffs_sbget
 */
#define	STDSB			-1	/* Fail if check-hash is bad */
#define	STDSB_NOHASHFAIL	-2	/* Ignore check-hash failure */

/*
 * Definitions for TRIM interface
 *
 * Special keys and recommended hash table size
 */
#define	NOTRIM_KEY	1	/* never written, so don't call trim for it */
#define	SINGLETON_KEY	2	/* only block being freed, so trim it now */
#define	FIRST_VALID_KEY	3	/* first valid key describing a block range */
#define	MAXTRIMIO	1024	/* maximum expected outstanding trim requests */

extern struct vop_vector ffs_vnodeops1;
extern struct vop_vector ffs_fifoops1;
extern struct vop_vector ffs_vnodeops2;
extern struct vop_vector ffs_fifoops2;

/*
 * Soft update function prototypes.
 */

int	softdep_check_suspend(struct mount *, struct vnode *,
	  int, int, int, int);
void	softdep_get_depcounts(struct mount *, int *, int *);
void	softdep_initialize(void);
void	softdep_uninitialize(void);
int	softdep_mount(struct vnode *, struct mount *, struct fs *,
	    struct ucred *);
void	softdep_unmount(struct mount *);
int	softdep_move_dependencies(struct buf *, struct buf *);
int	softdep_flushworklist(struct mount *, int *, struct thread *);
int	softdep_flushfiles(struct mount *, int, struct thread *);
void	softdep_update_inodeblock(struct inode *, struct buf *, int);
void	softdep_load_inodeblock(struct inode *);
void	softdep_freefile(struct vnode *, ino_t, int);
int	softdep_request_cleanup(struct fs *, struct vnode *,
	    struct ucred *, int);
void	softdep_setup_freeblocks(struct inode *, off_t, int);
void	softdep_setup_inomapdep(struct buf *, struct inode *, ino_t, int);
void	softdep_setup_blkmapdep(struct buf *, struct mount *, ufs2_daddr_t,
	    int, int);
void	softdep_setup_allocdirect(struct inode *, ufs_lbn_t, ufs2_daddr_t,
	    ufs2_daddr_t, long, long, struct buf *);
void	softdep_setup_allocext(struct inode *, ufs_lbn_t, ufs2_daddr_t,
	    ufs2_daddr_t, long, long, struct buf *);
void	softdep_setup_allocindir_meta(struct buf *, struct inode *,
	    struct buf *, int, ufs2_daddr_t);
void	softdep_setup_allocindir_page(struct inode *, ufs_lbn_t,
	    struct buf *, int, ufs2_daddr_t, ufs2_daddr_t, struct buf *);
void	softdep_setup_blkfree(struct mount *, struct buf *, ufs2_daddr_t, int,
	    struct workhead *);
void	softdep_setup_inofree(struct mount *, struct buf *, ino_t,
	    struct workhead *);
void	softdep_setup_sbupdate(struct ufsmount *, struct fs *, struct buf *);
void	softdep_fsync_mountdev(struct vnode *);
int	softdep_sync_metadata(struct vnode *);
int	softdep_sync_buf(struct vnode *, struct buf *, int);
int     softdep_fsync(struct vnode *);
int	softdep_prealloc(struct vnode *, int);
int	softdep_journal_lookup(struct mount *, struct vnode **);
void	softdep_journal_freeblocks(struct inode *, struct ucred *, off_t, int);
void	softdep_journal_fsync(struct inode *);
void	softdep_buf_append(struct buf *, struct workhead *);
void	softdep_inode_append(struct inode *, struct ucred *, struct workhead *);
void	softdep_freework(struct workhead *);


/*
 * Things to request flushing in softdep_request_cleanup()
 */
#define	FLUSH_INODES		1
#define	FLUSH_INODES_WAIT	2
#define	FLUSH_BLOCKS		3
#define	FLUSH_BLOCKS_WAIT	4
/*
 * Flag to ffs_syncvnode() to request flushing of data only,
 * but skip the ffs_update() on the inode itself. Used to avoid
 * deadlock when flushing snapshot inodes while holding snaplk.
 */
#define	NO_INO_UPDT		0x00000001
/*
 * Request data sync only from ffs_syncvnode(), not touching even more
 * metadata than NO_INO_UPDT.
 */
#define	DATA_ONLY		0x00000002

int	ffs_rdonly(struct inode *);

TAILQ_HEAD(snaphead, inode);

struct snapdata {
	LIST_ENTRY(snapdata) sn_link;
	struct snaphead sn_head;
	daddr_t sn_listsize;
	daddr_t *sn_blklist;
	struct lock sn_lock;
};

#endif /* _KERNEL */

#endif /* !_UFS_FFS_EXTERN_H */
