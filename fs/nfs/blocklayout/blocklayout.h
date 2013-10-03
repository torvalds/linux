/*
 *  linux/fs/nfs/blocklayout/blocklayout.h
 *
 *  Module for the NFSv4.1 pNFS block layout driver.
 *
 *  Copyright (c) 2006 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson <andros@citi.umich.edu>
 *  Fred Isaman <iisaman@umich.edu>
 *
 * permission is granted to use, copy, create derivative works and
 * redistribute this software and such derivative works for any purpose,
 * so long as the name of the university of michigan is not used in
 * any advertising or publicity pertaining to the use or distribution
 * of this software without specific, written prior authorization.  if
 * the above copyright notice or any other identification of the
 * university of michigan is included in any copy of any portion of
 * this software, then the disclaimer below must also be included.
 *
 * this software is provided as is, without representation from the
 * university of michigan as to its fitness for any purpose, and without
 * warranty by the university of michigan of any kind, either express
 * or implied, including without limitation the implied warranties of
 * merchantability and fitness for a particular purpose.  the regents
 * of the university of michigan shall not be liable for any damages,
 * including special, indirect, incidental, or consequential damages,
 * with respect to any claim arising out or in connection with the use
 * of the software, even if it has been or is hereafter advised of the
 * possibility of such damages.
 */
#ifndef FS_NFS_NFS4BLOCKLAYOUT_H
#define FS_NFS_NFS4BLOCKLAYOUT_H

#include <linux/device-mapper.h>
#include <linux/nfs_fs.h>
#include <linux/sunrpc/rpc_pipe_fs.h>

#include "../pnfs.h"
#include "../netns.h"

#define PAGE_CACHE_SECTORS (PAGE_CACHE_SIZE >> SECTOR_SHIFT)
#define PAGE_CACHE_SECTOR_SHIFT (PAGE_CACHE_SHIFT - SECTOR_SHIFT)
#define SECTOR_SIZE (1 << SECTOR_SHIFT)

struct block_mount_id {
	spinlock_t			bm_lock;    /* protects list */
	struct list_head		bm_devlist; /* holds pnfs_block_dev */
};

struct pnfs_block_dev {
	struct list_head		bm_node;
	struct nfs4_deviceid		bm_mdevid;    /* associated devid */
	struct block_device		*bm_mdev;     /* meta device itself */
	struct net			*net;
};

enum exstate4 {
	PNFS_BLOCK_READWRITE_DATA	= 0,
	PNFS_BLOCK_READ_DATA		= 1,
	PNFS_BLOCK_INVALID_DATA		= 2, /* mapped, but data is invalid */
	PNFS_BLOCK_NONE_DATA		= 3  /* unmapped, it's a hole */
};

#define MY_MAX_TAGS (15) /* tag bitnums used must be less than this */

struct my_tree {
	sector_t		mtt_step_size;	/* Internal sector alignment */
	struct list_head	mtt_stub; /* Should be a radix tree */
};

struct pnfs_inval_markings {
	spinlock_t	im_lock;
	struct my_tree	im_tree;	/* Sectors that need LAYOUTCOMMIT */
	sector_t	im_block_size;	/* Server blocksize in sectors */
	struct list_head im_extents;	/* Short extents for INVAL->RW conversion */
};

struct pnfs_inval_tracking {
	struct list_head it_link;
	int		 it_sector;
	int		 it_tags;
};

/* sector_t fields are all in 512-byte sectors */
struct pnfs_block_extent {
	struct kref	be_refcnt;
	struct list_head be_node;	/* link into lseg list */
	struct nfs4_deviceid be_devid;  /* FIXME: could use device cache instead */
	struct block_device *be_mdev;
	sector_t	be_f_offset;	/* the starting offset in the file */
	sector_t	be_length;	/* the size of the extent */
	sector_t	be_v_offset;	/* the starting offset in the volume */
	enum exstate4	be_state;	/* the state of this extent */
	struct pnfs_inval_markings *be_inval; /* tracks INVAL->RW transition */
};

/* Shortened extent used by LAYOUTCOMMIT */
struct pnfs_block_short_extent {
	struct list_head bse_node;
	struct nfs4_deviceid bse_devid;
	struct block_device *bse_mdev;
	sector_t	bse_f_offset;	/* the starting offset in the file */
	sector_t	bse_length;	/* the size of the extent */
};

static inline void
BL_INIT_INVAL_MARKS(struct pnfs_inval_markings *marks, sector_t blocksize)
{
	spin_lock_init(&marks->im_lock);
	INIT_LIST_HEAD(&marks->im_tree.mtt_stub);
	INIT_LIST_HEAD(&marks->im_extents);
	marks->im_block_size = blocksize;
	marks->im_tree.mtt_step_size = min((sector_t)PAGE_CACHE_SECTORS,
					   blocksize);
}

enum extentclass4 {
	RW_EXTENT       = 0, /* READWRTE and INVAL */
	RO_EXTENT       = 1, /* READ and NONE */
	EXTENT_LISTS    = 2,
};

static inline int bl_choose_list(enum exstate4 state)
{
	if (state == PNFS_BLOCK_READ_DATA || state == PNFS_BLOCK_NONE_DATA)
		return RO_EXTENT;
	else
		return RW_EXTENT;
}

struct pnfs_block_layout {
	struct pnfs_layout_hdr bl_layout;
	struct pnfs_inval_markings bl_inval; /* tracks INVAL->RW transition */
	spinlock_t		bl_ext_lock;   /* Protects list manipulation */
	struct list_head	bl_extents[EXTENT_LISTS]; /* R and RW extents */
	struct list_head	bl_commit;	/* Needs layout commit */
	struct list_head	bl_committing;	/* Layout committing */
	unsigned int		bl_count;	/* entries in bl_commit */
	sector_t		bl_blocksize;  /* Server blocksize in sectors */
};

#define BLK_ID(lo) ((struct block_mount_id *)(NFS_SERVER(lo->plh_inode)->pnfs_ld_data))

static inline struct pnfs_block_layout *
BLK_LO2EXT(struct pnfs_layout_hdr *lo)
{
	return container_of(lo, struct pnfs_block_layout, bl_layout);
}

static inline struct pnfs_block_layout *
BLK_LSEG2EXT(struct pnfs_layout_segment *lseg)
{
	return BLK_LO2EXT(lseg->pls_layout);
}

struct bl_pipe_msg {
	struct rpc_pipe_msg msg;
	wait_queue_head_t *bl_wq;
};

struct bl_msg_hdr {
	u8  type;
	u16 totallen; /* length of entire message, including hdr itself */
};

#define BL_DEVICE_UMOUNT               0x0 /* Umount--delete devices */
#define BL_DEVICE_MOUNT                0x1 /* Mount--create devices*/
#define BL_DEVICE_REQUEST_INIT         0x0 /* Start request */
#define BL_DEVICE_REQUEST_PROC         0x1 /* User level process succeeds */
#define BL_DEVICE_REQUEST_ERR          0x2 /* User level process fails */

/* blocklayoutdev.c */
ssize_t bl_pipe_downcall(struct file *, const char __user *, size_t);
void bl_pipe_destroy_msg(struct rpc_pipe_msg *);
void nfs4_blkdev_put(struct block_device *bdev);
struct pnfs_block_dev *nfs4_blk_decode_device(struct nfs_server *server,
						struct pnfs_device *dev);
int nfs4_blk_process_layoutget(struct pnfs_layout_hdr *lo,
				struct nfs4_layoutget_res *lgr, gfp_t gfp_flags);

/* blocklayoutdm.c */
void bl_free_block_dev(struct pnfs_block_dev *bdev);

/* extents.c */
struct pnfs_block_extent *
bl_find_get_extent(struct pnfs_block_layout *bl, sector_t isect,
		struct pnfs_block_extent **cow_read);
int bl_mark_sectors_init(struct pnfs_inval_markings *marks,
			     sector_t offset, sector_t length);
void bl_put_extent(struct pnfs_block_extent *be);
struct pnfs_block_extent *bl_alloc_extent(void);
int bl_is_sector_init(struct pnfs_inval_markings *marks, sector_t isect);
int encode_pnfs_block_layoutupdate(struct pnfs_block_layout *bl,
				   struct xdr_stream *xdr,
				   const struct nfs4_layoutcommit_args *arg);
void clean_pnfs_block_layoutupdate(struct pnfs_block_layout *bl,
				   const struct nfs4_layoutcommit_args *arg,
				   int status);
int bl_add_merge_extent(struct pnfs_block_layout *bl,
			 struct pnfs_block_extent *new);
int bl_mark_for_commit(struct pnfs_block_extent *be,
			sector_t offset, sector_t length,
			struct pnfs_block_short_extent *new);
int bl_push_one_short_extent(struct pnfs_inval_markings *marks);
struct pnfs_block_short_extent *
bl_pop_one_short_extent(struct pnfs_inval_markings *marks);
void bl_free_short_extents(struct pnfs_inval_markings *marks, int num_to_free);

#endif /* FS_NFS_NFS4BLOCKLAYOUT_H */
