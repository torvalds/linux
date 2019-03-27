/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf
 * Copyright (c) 2008, 2009 Reinoud Zandijk
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * From: NetBSD: nilfs_subr.h,v 1.1 2009/07/18 16:31:42 reinoud
 *
 * $FreeBSD$
 */

#ifndef _FS_NANDFS_NANDFS_SUBR_H_
#define _FS_NANDFS_NANDFS_SUBR_H_

struct nandfs_mdt;

struct nandfs_alloc_request
{
	uint64_t	entrynum;
	struct buf	*bp_desc;
	struct buf	*bp_bitmap;
	struct buf	*bp_entry;
};

/* Segment creation */
void nandfs_wakeup_wait_sync(struct nandfs_device *, int);
int nandfs_segment_constructor(struct nandfsmount *, int);
int nandfs_sync_file(struct vnode *);

/* Basic calculators */
uint64_t nandfs_get_segnum_of_block(struct nandfs_device *, nandfs_daddr_t);
void nandfs_get_segment_range(struct nandfs_device *, uint64_t, uint64_t *,
    uint64_t *);
void nandfs_calc_mdt_consts(struct nandfs_device *, struct nandfs_mdt *, int);

/* Log reading / volume helpers */
int nandfs_search_super_root(struct nandfs_device *);

/* Reading */
int nandfs_dev_bread(struct nandfs_device *, nandfs_daddr_t, struct ucred *,
    int, struct buf **);
int nandfs_bread(struct nandfs_node *, nandfs_lbn_t, struct ucred *, int,
    struct buf **);
int nandfs_bread_meta(struct nandfs_node *, nandfs_lbn_t, struct ucred *, int,
    struct buf **);
int nandfs_bdestroy(struct nandfs_node *, nandfs_daddr_t);
int nandfs_bcreate(struct nandfs_node *, nandfs_lbn_t, struct ucred *, int,
    struct buf **);
int nandfs_bcreate_meta(struct nandfs_node *, nandfs_lbn_t, struct ucred *,
    int, struct buf **);
int nandfs_bread_create(struct nandfs_node *, nandfs_lbn_t, struct ucred *,
    int, struct buf **);

/* vtop operations */
int nandfs_vtop(struct nandfs_node *, nandfs_daddr_t, nandfs_daddr_t *);

/* Node action implementators */
int nandfs_vinit(struct vnode *, uint64_t);
int nandfs_get_node(struct nandfsmount *, uint64_t, struct nandfs_node **);
int nandfs_get_node_raw(struct nandfs_device *, struct nandfsmount *, uint64_t,
    struct nandfs_inode *, struct nandfs_node **);
void nandfs_dispose_node(struct nandfs_node **);

void nandfs_itimes(struct vnode *);
int nandfs_lookup_name_in_dir(struct vnode *, const char *, int, uint64_t *,
    int *, uint64_t *);
int nandfs_create_node(struct vnode *, struct vnode **, struct vattr *,
    struct componentname *);
void nandfs_delete_node(struct nandfs_node *);

int nandfs_chsize(struct vnode *, u_quad_t, struct ucred *);
int nandfs_dir_detach(struct nandfsmount *, struct nandfs_node *,
    struct nandfs_node *, struct componentname *);
int nandfs_dir_attach(struct nandfsmount *, struct nandfs_node *,
    struct nandfs_node *, struct vattr *, struct componentname *);

int nandfs_dirty_buf(struct buf *, int);
int nandfs_dirty_buf_meta(struct buf *, int);
int nandfs_fs_full(struct nandfs_device *);
void nandfs_undirty_buf_fsdev(struct nandfs_device *, struct buf *);
void nandfs_undirty_buf(struct buf *);

void nandfs_clear_buf(struct buf *);
void nandfs_buf_set(struct buf *, uint32_t);
void nandfs_buf_clear(struct buf *, uint32_t);
int nandfs_buf_check(struct buf *, uint32_t);

int  nandfs_find_free_entry(struct nandfs_mdt *, struct nandfs_node *,
    struct nandfs_alloc_request *);
int  nandfs_find_entry(struct nandfs_mdt *, struct nandfs_node *,
    struct nandfs_alloc_request *);
int  nandfs_alloc_entry(struct nandfs_mdt *, struct nandfs_alloc_request *);
void nandfs_abort_entry(struct nandfs_alloc_request *);
int  nandfs_free_entry(struct nandfs_mdt *, struct nandfs_alloc_request *);
int nandfs_get_entry_block(struct nandfs_mdt *, struct nandfs_node *,
    struct nandfs_alloc_request *, uint32_t *, int);

/* Inode management. */
int  nandfs_node_create(struct nandfsmount *, struct nandfs_node **, uint16_t);
int nandfs_node_destroy(struct nandfs_node *);
int nandfs_node_update(struct nandfs_node *);
int nandfs_get_node_entry(struct nandfsmount *, struct nandfs_inode **,
    uint64_t, struct buf **);
void nandfs_mdt_trans_blk(struct nandfs_mdt *, uint64_t, uint64_t *,
    uint64_t *, nandfs_lbn_t *, uint32_t *);

/* vblock management */
void nandfs_mdt_trans(struct nandfs_mdt *, uint64_t, nandfs_lbn_t *, uint32_t *);
int nandfs_vblock_alloc(struct nandfs_device *, nandfs_daddr_t *);
int nandfs_vblock_end(struct nandfs_device *, nandfs_daddr_t);
int nandfs_vblock_assign(struct nandfs_device *, nandfs_daddr_t,
    nandfs_lbn_t);
int nandfs_vblock_free(struct nandfs_device *, nandfs_daddr_t);

/* Checkpoint management */
int nandfs_get_checkpoint(struct nandfs_device *, struct nandfs_node *,
    uint64_t);
int nandfs_set_checkpoint(struct nandfs_device *, struct nandfs_node *,
    uint64_t, struct nandfs_inode *, uint64_t);

/* Segment management */
int nandfs_alloc_segment(struct nandfs_device *, uint64_t *);
int nandfs_update_segment(struct nandfs_device *, uint64_t, uint32_t);
int nandfs_free_segment(struct nandfs_device *, uint64_t);
int nandfs_clear_segment(struct nandfs_device *, uint64_t);
int nandfs_touch_segment(struct nandfs_device *, uint64_t);
int nandfs_markgc_segment(struct nandfs_device *, uint64_t);

int nandfs_bmap_insert_block(struct nandfs_node *, nandfs_lbn_t, struct buf *);
int nandfs_bmap_update_block(struct nandfs_node *, struct buf *, nandfs_lbn_t);
int nandfs_bmap_update_dat(struct nandfs_node *, nandfs_daddr_t, struct buf *);
int nandfs_bmap_dirty_blocks(struct nandfs_node *, struct buf *, int);
int nandfs_bmap_truncate_mapping(struct nandfs_node *, nandfs_lbn_t,
    nandfs_lbn_t);
int nandfs_bmap_lookup(struct nandfs_node *, nandfs_lbn_t, nandfs_daddr_t *);

/* dirent */
int nandfs_add_dirent(struct vnode *, uint64_t, char *, long, uint8_t);
int nandfs_remove_dirent(struct vnode *, struct nandfs_node *,
    struct componentname *);
int nandfs_update_dirent(struct vnode *, struct nandfs_node *,
    struct nandfs_node *);
int nandfs_init_dir(struct vnode *, uint64_t, uint64_t);
int nandfs_update_parent_dir(struct vnode *, uint64_t);

void nandfs_vblk_set(struct buf *, nandfs_daddr_t);
nandfs_daddr_t nandfs_vblk_get(struct buf *);

void nandfs_inode_init(struct nandfs_inode *, uint16_t);
void nandfs_inode_destroy(struct nandfs_inode *);

/* ioctl */
int nandfs_get_seg_stat(struct nandfs_device *, struct nandfs_seg_stat *);
int nandfs_chng_cpmode(struct nandfs_node *, struct nandfs_cpmode *);
int nandfs_get_cpinfo_ioctl(struct nandfs_node *, struct nandfs_argv *);
int nandfs_delete_cp(struct nandfs_node *, uint64_t start, uint64_t);
int nandfs_make_snap(struct nandfs_device *, uint64_t *);
int nandfs_delete_snap(struct nandfs_device *, uint64_t);
int nandfs_get_cpstat(struct nandfs_node *, struct nandfs_cpstat *);
int nandfs_get_segment_info_ioctl(struct nandfs_device *, struct nandfs_argv *);
int nandfs_get_dat_vinfo_ioctl(struct nandfs_device *, struct nandfs_argv *);
int nandfs_get_dat_bdescs_ioctl(struct nandfs_device *, struct nandfs_argv *);
int nandfs_get_fsinfo(struct nandfsmount *, struct nandfs_fsinfo *);

int nandfs_get_cpinfo(struct nandfs_node *, uint64_t, uint16_t,
    struct nandfs_cpinfo *, uint32_t, uint32_t *);

nandfs_lbn_t nandfs_get_maxfilesize(struct nandfs_device *);

int nandfs_write_superblock(struct nandfs_device *);

extern int nandfs_sync_interval;
extern int nandfs_max_dirty_segs;
extern int nandfs_cps_between_sblocks;

struct buf *nandfs_geteblk(int, int);

void nandfs_dirty_bufs_increment(struct nandfs_device *);
void nandfs_dirty_bufs_decrement(struct nandfs_device *);

int nandfs_start_cleaner(struct nandfs_device *);
int nandfs_stop_cleaner(struct nandfs_device *);

int nandfs_segsum_valid(struct nandfs_segment_summary *);
int nandfs_load_segsum(struct nandfs_device *, nandfs_daddr_t,
    struct nandfs_segment_summary *);
int nandfs_get_segment_info(struct nandfs_device *, struct nandfs_suinfo *,
    uint32_t, uint64_t);
int nandfs_get_segment_info_filter(struct nandfs_device *,
    struct nandfs_suinfo *, uint32_t, uint64_t, uint64_t *, uint32_t, uint32_t);
int nandfs_get_dat_vinfo(struct nandfs_device *, struct nandfs_vinfo *,
    uint32_t);
int nandfs_get_dat_bdescs(struct nandfs_device *, struct nandfs_bdesc *,
    uint32_t);

#define	NANDFS_VBLK_ASSIGNED	1

#define	NANDFS_IS_INDIRECT(bp)	((bp)->b_lblkno < 0)

int nandfs_erase(struct nandfs_device *, off_t, size_t);

#define	NANDFS_VOP_ISLOCKED(vp)	nandfs_vop_islocked((vp))
int nandfs_vop_islocked(struct vnode *vp);

nandfs_daddr_t nandfs_block_to_dblock(struct nandfs_device *, nandfs_lbn_t);

#define DEBUG_MODE
#if defined(DEBUG_MODE)
#define	nandfs_error		panic
#define	nandfs_warning		printf
#elif defined(TEST_MODE)
#define	nandfs_error	printf
#define	nandfs_warning	printf
#else
#define	nandfs_error(...)
#define	nandfs_warning(...)
#endif

#endif	/* !_FS_NANDFS_NANDFS_SUBR_H_ */
