/*-
 *  modified for EXT2FS support in Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
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
 *	@(#)ffs_extern.h	8.3 (Berkeley) 4/16/94
 * $FreeBSD$
 */

#ifndef _FS_EXT2FS_EXT2_EXTERN_H_
#define	_FS_EXT2FS_EXT2_EXTERN_H_

struct ext2fs_dinode;
struct ext2fs_direct_2;
struct ext2fs_direct_tail;
struct ext2fs_searchslot;
struct indir;
struct inode;
struct mount;
struct vfsconf;
struct vnode;

int	ext2_add_entry(struct vnode *, struct ext2fs_direct_2 *);
int	ext2_alloc(struct inode *, daddr_t, e4fs_daddr_t, int,
	    struct ucred *, e4fs_daddr_t *);
e4fs_daddr_t ext2_alloc_meta(struct inode *ip);
int	ext2_balloc(struct inode *,
	    e2fs_lbn_t, int, struct ucred *, struct buf **, int);
int	ext2_blkatoff(struct vnode *, off_t, char **, struct buf **);
void	ext2_blkfree(struct inode *,  e4fs_daddr_t, long);
e4fs_daddr_t	ext2_blkpref(struct inode *, e2fs_lbn_t, int, e2fs_daddr_t *,
	    e2fs_daddr_t);
int	ext2_bmap(struct vop_bmap_args *);
int	ext2_bmaparray(struct vnode *, daddr_t, daddr_t *, int *, int *);
int	ext4_bmapext(struct vnode *, int32_t, int64_t *, int *, int *);
void	ext2_clusteracct(struct m_ext2fs *, char *, int, e4fs_daddr_t, int);
void	ext2_dirbad(struct inode *ip, doff_t offset, char *how);
void	ext2_fserr(struct m_ext2fs *, uid_t, char *);
int	ext2_ei2i(struct ext2fs_dinode *, struct inode *);
int	ext2_getlbns(struct vnode *, daddr_t, struct indir *, int *);
int	ext2_i2ei(struct inode *, struct ext2fs_dinode *);
void	ext2_itimes(struct vnode *vp);
int	ext2_reallocblks(struct vop_reallocblks_args *);
int	ext2_reclaim(struct vop_reclaim_args *);
int	ext2_truncate(struct vnode *, off_t, int, struct ucred *, struct thread *);
int	ext2_update(struct vnode *, int);
int	ext2_valloc(struct vnode *, int, struct ucred *, struct vnode **);
int	ext2_vfree(struct vnode *, ino_t, int);
int	ext2_vinit(struct mount *, struct vop_vector *, struct vnode **vpp);
int	ext2_lookup(struct vop_cachedlookup_args *);
int	ext2_readdir(struct vop_readdir_args *);
#ifdef EXT2FS_DEBUG
void	ext2_print_inode(struct inode *);
#endif
int	ext2_direnter(struct inode *, 
		struct vnode *, struct componentname *);
int	ext2_dirremove(struct vnode *, struct componentname *);
int	ext2_dirrewrite(struct inode *,
		struct inode *, struct componentname *);
int	ext2_dirempty(struct inode *, ino_t, struct ucred *);
int	ext2_checkpath(struct inode *, struct inode *, struct ucred *);
int	ext2_cg_has_sb(struct m_ext2fs *fs, int cg);
uint64_t	ext2_cg_number_gdb(struct m_ext2fs *fs, int cg);
int	ext2_inactive(struct vop_inactive_args *);
int	ext2_htree_add_entry(struct vnode *, struct ext2fs_direct_2 *,
	    struct componentname *);
int	ext2_htree_create_index(struct vnode *, struct componentname *,
	    struct ext2fs_direct_2 *);
int	ext2_htree_has_idx(struct inode *);
int	ext2_htree_hash(const char *, int, uint32_t *, int, uint32_t *,
	    uint32_t *);
int	ext2_htree_lookup(struct inode *, const char *, int, struct buf **,
	    int *, doff_t *, doff_t *, doff_t *, struct ext2fs_searchslot *);
int	ext2_search_dirblock(struct inode *, void *, int *, const char *, int,
	    int *, doff_t *, doff_t *, doff_t *, struct ext2fs_searchslot *);
uint32_t	e2fs_gd_get_ndirs(struct ext2_gd *gd);
uint64_t	e2fs_gd_get_b_bitmap(struct ext2_gd *);
uint64_t	e2fs_gd_get_i_bitmap(struct ext2_gd *);
uint64_t	e2fs_gd_get_i_tables(struct ext2_gd *);
void	ext2_sb_csum_set_seed(struct m_ext2fs *);
int	ext2_sb_csum_verify(struct m_ext2fs *);
void	ext2_sb_csum_set(struct m_ext2fs *);
int	ext2_extattr_blk_csum_verify(struct inode *, struct buf *);
void	ext2_extattr_blk_csum_set(struct inode *, struct buf *);
int	ext2_dir_blk_csum_verify(struct inode *, struct buf *);
struct ext2fs_direct_tail	*ext2_dirent_get_tail(struct inode *ip,
    struct ext2fs_direct_2 *ep);
void	ext2_dirent_csum_set(struct inode *, struct ext2fs_direct_2 *);
int	ext2_dirent_csum_verify(struct inode *ip, struct ext2fs_direct_2 *ep);
void	ext2_dx_csum_set(struct inode *, struct ext2fs_direct_2 *);
int	ext2_dx_csum_verify(struct inode *ip, struct ext2fs_direct_2 *ep);
int	ext2_extent_blk_csum_verify(struct inode *, void *);
void	ext2_extent_blk_csum_set(struct inode *, void *);
void	ext2_init_dirent_tail(struct ext2fs_direct_tail *);
int	ext2_is_dirent_tail(struct inode *, struct ext2fs_direct_2 *);
int	ext2_gd_i_bitmap_csum_verify(struct m_ext2fs *, int, struct buf *);
void	ext2_gd_i_bitmap_csum_set(struct m_ext2fs *, int, struct buf *);
int	ext2_gd_b_bitmap_csum_verify(struct m_ext2fs *, int, struct buf *);
void	ext2_gd_b_bitmap_csum_set(struct m_ext2fs *, int, struct buf *);
int	ext2_ei_csum_verify(struct inode *, struct ext2fs_dinode *);
void	ext2_ei_csum_set(struct inode *, struct ext2fs_dinode *);
int	ext2_gd_csum_verify(struct m_ext2fs *, struct cdev *);
void	ext2_gd_csum_set(struct m_ext2fs *);

/* Flags to low-level allocation routines.
 * The low 16-bits are reserved for IO_ flags from vnode.h.
 */
#define	BA_CLRBUF	0x00010000	/* Clear invalid areas of buffer. */
#define	BA_SEQMASK	0x7F000000	/* Bits holding seq heuristic. */
#define	BA_SEQSHIFT	24
#define	BA_SEQMAX	0x7F

extern struct vop_vector ext2_vnodeops;
extern struct vop_vector ext2_fifoops;

#endif	/* !_FS_EXT2FS_EXT2_EXTERN_H_ */
