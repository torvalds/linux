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

#include "../nfs4_fs.h"
#include "../pnfs.h"
#include "../netns.h"

#define PAGE_CACHE_SECTORS (PAGE_SIZE >> SECTOR_SHIFT)
#define PAGE_CACHE_SECTOR_SHIFT (PAGE_SHIFT - SECTOR_SHIFT)
#define SECTOR_SIZE (1 << SECTOR_SHIFT)

struct pnfs_block_dev;

#define PNFS_BLOCK_MAX_UUIDS	4
#define PNFS_BLOCK_MAX_DEVICES	64

/*
 * Random upper cap for the uuid length to avoid unbounded allocation.
 * Not actually limited by the protocol.
 */
#define PNFS_BLOCK_UUID_LEN	128

struct pnfs_block_volume {
	enum pnfs_block_volume_type	type;
	union {
		struct {
			int		len;
			int		nr_sigs;
			struct {
				u64		offset;
				u32		sig_len;
				u8		sig[PNFS_BLOCK_UUID_LEN];
			} sigs[PNFS_BLOCK_MAX_UUIDS];
		} simple;
		struct {
			u64		start;
			u64		len;
			u32		volume;
		} slice;
		struct {
			u32		volumes_count;
			u32		volumes[PNFS_BLOCK_MAX_DEVICES];
		} concat;
		struct {
			u64		chunk_size;
			u32		volumes_count;
			u32		volumes[PNFS_BLOCK_MAX_DEVICES];
		} stripe;
		struct {
			enum scsi_code_set		code_set;
			enum scsi_designator_type	designator_type;
			int				designator_len;
			u8				designator[256];
			u64				pr_key;
		} scsi;
	};
};

struct pnfs_block_dev_map {
	u64			start;
	u64			len;
	u64			disk_offset;
	struct block_device		*bdev;
};

struct pnfs_block_dev {
	struct nfs4_deviceid_node	node;

	u64				start;
	u64				len;

	u32				nr_children;
	struct pnfs_block_dev		*children;
	u64				chunk_size;

	struct file			*bdev_file;
	u64				disk_offset;

	u64				pr_key;
	bool				pr_registered;

	bool (*map)(struct pnfs_block_dev *dev, u64 offset,
			struct pnfs_block_dev_map *map);
};

/* sector_t fields are all in 512-byte sectors */
struct pnfs_block_extent {
	union {
		struct rb_node	be_node;
		struct list_head be_list;
	};
	struct nfs4_deviceid_node *be_device;
	sector_t	be_f_offset;	/* the starting offset in the file */
	sector_t	be_length;	/* the size of the extent */
	sector_t	be_v_offset;	/* the starting offset in the volume */
	enum pnfs_block_extent_state be_state;	/* the state of this extent */
#define EXTENT_WRITTEN		1
#define EXTENT_COMMITTING	2
	unsigned int	be_tag;
};

struct pnfs_block_layout {
	struct pnfs_layout_hdr	bl_layout;
	struct rb_root		bl_ext_rw;
	struct rb_root		bl_ext_ro;
	spinlock_t		bl_ext_lock;   /* Protects list manipulation */
	bool			bl_scsi_layout;
	u64			bl_lwb;
};

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

/* dev.c */
struct nfs4_deviceid_node *bl_alloc_deviceid_node(struct nfs_server *server,
		struct pnfs_device *pdev, gfp_t gfp_mask);
void bl_free_deviceid_node(struct nfs4_deviceid_node *d);

/* extent_tree.c */
int ext_tree_insert(struct pnfs_block_layout *bl,
		struct pnfs_block_extent *new);
int ext_tree_remove(struct pnfs_block_layout *bl, bool rw, sector_t start,
		sector_t end);
int ext_tree_mark_written(struct pnfs_block_layout *bl, sector_t start,
		sector_t len, u64 lwb);
bool ext_tree_lookup(struct pnfs_block_layout *bl, sector_t isect,
		struct pnfs_block_extent *ret, bool rw);
int ext_tree_prepare_commit(struct nfs4_layoutcommit_args *arg);
void ext_tree_mark_committed(struct nfs4_layoutcommit_args *arg, int status);

/* rpc_pipefs.c */
dev_t bl_resolve_deviceid(struct nfs_server *server,
		struct pnfs_block_volume *b, gfp_t gfp_mask);
int __init bl_init_pipefs(void);
void bl_cleanup_pipefs(void);

#endif /* FS_NFS_NFS4BLOCKLAYOUT_H */
