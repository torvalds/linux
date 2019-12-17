// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_DIR2_H__
#define __XFS_DIR2_H__

#include "xfs_da_format.h"
#include "xfs_da_btree.h"

struct xfs_da_args;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;
struct xfs_dir2_sf_hdr;
struct xfs_dir2_sf_entry;
struct xfs_dir2_data_hdr;
struct xfs_dir2_data_entry;
struct xfs_dir2_data_unused;
struct xfs_dir3_icfree_hdr;
struct xfs_dir3_icleaf_hdr;

extern struct xfs_name	xfs_name_dotdot;

/*
 * Convert inode mode to directory entry filetype
 */
extern unsigned char xfs_mode_to_ftype(int mode);

/*
 * Generic directory interface routines
 */
extern void xfs_dir_startup(void);
extern int xfs_da_mount(struct xfs_mount *mp);
extern void xfs_da_unmount(struct xfs_mount *mp);

extern int xfs_dir_isempty(struct xfs_inode *dp);
extern int xfs_dir_init(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_inode *pdp);
extern int xfs_dir_createname(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_name *name, xfs_ino_t inum,
				xfs_extlen_t tot);
extern int xfs_dir_lookup(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_name *name, xfs_ino_t *inum,
				struct xfs_name *ci_name);
extern int xfs_dir_removename(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_name *name, xfs_ino_t ino,
				xfs_extlen_t tot);
extern bool xfs_dir2_sf_replace_needblock(struct xfs_inode *dp,
				xfs_ino_t inum);
extern int xfs_dir_replace(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_name *name, xfs_ino_t inum,
				xfs_extlen_t tot);
extern int xfs_dir_canenter(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_name *name);

/*
 * Direct call from the bmap code, bypassing the generic directory layer.
 */
extern int xfs_dir2_sf_to_block(struct xfs_da_args *args);

/*
 * Interface routines used by userspace utilities
 */
extern int xfs_dir2_isblock(struct xfs_da_args *args, int *r);
extern int xfs_dir2_isleaf(struct xfs_da_args *args, int *r);
extern int xfs_dir2_shrink_inode(struct xfs_da_args *args, xfs_dir2_db_t db,
				struct xfs_buf *bp);

extern void xfs_dir2_data_freescan(struct xfs_mount *mp,
		struct xfs_dir2_data_hdr *hdr, int *loghead);
extern void xfs_dir2_data_log_entry(struct xfs_da_args *args,
		struct xfs_buf *bp, struct xfs_dir2_data_entry *dep);
extern void xfs_dir2_data_log_header(struct xfs_da_args *args,
		struct xfs_buf *bp);
extern void xfs_dir2_data_log_unused(struct xfs_da_args *args,
		struct xfs_buf *bp, struct xfs_dir2_data_unused *dup);
extern void xfs_dir2_data_make_free(struct xfs_da_args *args,
		struct xfs_buf *bp, xfs_dir2_data_aoff_t offset,
		xfs_dir2_data_aoff_t len, int *needlogp, int *needscanp);
extern int xfs_dir2_data_use_free(struct xfs_da_args *args,
		struct xfs_buf *bp, struct xfs_dir2_data_unused *dup,
		xfs_dir2_data_aoff_t offset, xfs_dir2_data_aoff_t len,
		int *needlogp, int *needscanp);

extern struct xfs_dir2_data_free *xfs_dir2_data_freefind(
		struct xfs_dir2_data_hdr *hdr, struct xfs_dir2_data_free *bf,
		struct xfs_dir2_data_unused *dup);

extern int xfs_dir_ino_validate(struct xfs_mount *mp, xfs_ino_t ino);

extern const struct xfs_buf_ops xfs_dir3_block_buf_ops;
extern const struct xfs_buf_ops xfs_dir3_leafn_buf_ops;
extern const struct xfs_buf_ops xfs_dir3_leaf1_buf_ops;
extern const struct xfs_buf_ops xfs_dir3_free_buf_ops;
extern const struct xfs_buf_ops xfs_dir3_data_buf_ops;

/*
 * Directory offset/block conversion functions.
 *
 * DB blocks here are logical directory block numbers, not filesystem blocks.
 */

/*
 * Convert dataptr to byte in file space
 */
static inline xfs_dir2_off_t
xfs_dir2_dataptr_to_byte(xfs_dir2_dataptr_t dp)
{
	return (xfs_dir2_off_t)dp << XFS_DIR2_DATA_ALIGN_LOG;
}

/*
 * Convert byte in file space to dataptr.  It had better be aligned.
 */
static inline xfs_dir2_dataptr_t
xfs_dir2_byte_to_dataptr(xfs_dir2_off_t by)
{
	return (xfs_dir2_dataptr_t)(by >> XFS_DIR2_DATA_ALIGN_LOG);
}

/*
 * Convert byte in space to (DB) block
 */
static inline xfs_dir2_db_t
xfs_dir2_byte_to_db(struct xfs_da_geometry *geo, xfs_dir2_off_t by)
{
	return (xfs_dir2_db_t)(by >> geo->blklog);
}

/*
 * Convert dataptr to a block number
 */
static inline xfs_dir2_db_t
xfs_dir2_dataptr_to_db(struct xfs_da_geometry *geo, xfs_dir2_dataptr_t dp)
{
	return xfs_dir2_byte_to_db(geo, xfs_dir2_dataptr_to_byte(dp));
}

/*
 * Convert byte in space to offset in a block
 */
static inline xfs_dir2_data_aoff_t
xfs_dir2_byte_to_off(struct xfs_da_geometry *geo, xfs_dir2_off_t by)
{
	return (xfs_dir2_data_aoff_t)(by & (geo->blksize - 1));
}

/*
 * Convert dataptr to a byte offset in a block
 */
static inline xfs_dir2_data_aoff_t
xfs_dir2_dataptr_to_off(struct xfs_da_geometry *geo, xfs_dir2_dataptr_t dp)
{
	return xfs_dir2_byte_to_off(geo, xfs_dir2_dataptr_to_byte(dp));
}

/*
 * Convert block and offset to byte in space
 */
static inline xfs_dir2_off_t
xfs_dir2_db_off_to_byte(struct xfs_da_geometry *geo, xfs_dir2_db_t db,
			xfs_dir2_data_aoff_t o)
{
	return ((xfs_dir2_off_t)db << geo->blklog) + o;
}

/*
 * Convert block (DB) to block (dablk)
 */
static inline xfs_dablk_t
xfs_dir2_db_to_da(struct xfs_da_geometry *geo, xfs_dir2_db_t db)
{
	return (xfs_dablk_t)(db << (geo->blklog - geo->fsblog));
}

/*
 * Convert byte in space to (DA) block
 */
static inline xfs_dablk_t
xfs_dir2_byte_to_da(struct xfs_da_geometry *geo, xfs_dir2_off_t by)
{
	return xfs_dir2_db_to_da(geo, xfs_dir2_byte_to_db(geo, by));
}

/*
 * Convert block and offset to dataptr
 */
static inline xfs_dir2_dataptr_t
xfs_dir2_db_off_to_dataptr(struct xfs_da_geometry *geo, xfs_dir2_db_t db,
			   xfs_dir2_data_aoff_t o)
{
	return xfs_dir2_byte_to_dataptr(xfs_dir2_db_off_to_byte(geo, db, o));
}

/*
 * Convert block (dablk) to block (DB)
 */
static inline xfs_dir2_db_t
xfs_dir2_da_to_db(struct xfs_da_geometry *geo, xfs_dablk_t da)
{
	return (xfs_dir2_db_t)(da >> (geo->blklog - geo->fsblog));
}

/*
 * Convert block (dablk) to byte offset in space
 */
static inline xfs_dir2_off_t
xfs_dir2_da_to_byte(struct xfs_da_geometry *geo, xfs_dablk_t da)
{
	return xfs_dir2_db_off_to_byte(geo, xfs_dir2_da_to_db(geo, da), 0);
}

/*
 * Directory tail pointer accessor functions. Based on block geometry.
 */
static inline struct xfs_dir2_block_tail *
xfs_dir2_block_tail_p(struct xfs_da_geometry *geo, struct xfs_dir2_data_hdr *hdr)
{
	return ((struct xfs_dir2_block_tail *)
		((char *)hdr + geo->blksize)) - 1;
}

static inline struct xfs_dir2_leaf_tail *
xfs_dir2_leaf_tail_p(struct xfs_da_geometry *geo, struct xfs_dir2_leaf *lp)
{
	return (struct xfs_dir2_leaf_tail *)
		((char *)lp + geo->blksize -
		  sizeof(struct xfs_dir2_leaf_tail));
}

/*
 * The Linux API doesn't pass down the total size of the buffer
 * we read into down to the filesystem.  With the filldir concept
 * it's not needed for correct information, but the XFS dir2 leaf
 * code wants an estimate of the buffer size to calculate it's
 * readahead window and size the buffers used for mapping to
 * physical blocks.
 *
 * Try to give it an estimate that's good enough, maybe at some
 * point we can change the ->readdir prototype to include the
 * buffer size.  For now we use the current glibc buffer size.
 * musl libc hardcodes 2k and dietlibc uses PAGE_SIZE.
 */
#define XFS_READDIR_BUFSIZE	(32768)

unsigned char xfs_dir3_get_dtype(struct xfs_mount *mp, uint8_t filetype);
unsigned int xfs_dir3_data_end_offset(struct xfs_da_geometry *geo,
		struct xfs_dir2_data_hdr *hdr);
bool xfs_dir2_namecheck(const void *name, size_t length);

#endif	/* __XFS_DIR2_H__ */
