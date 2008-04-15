/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef __BTRFS_VOLUMES_
#define __BTRFS_VOLUMES_

#include <linux/bio.h>

struct buffer_head;
struct btrfs_device {
	struct list_head dev_list;
	struct btrfs_root *dev_root;
	struct buffer_head *pending_io;
	int barriers;
	spinlock_t io_lock;

	struct block_device *bdev;

	u64 total_ios;

	char *name;

	/* the internal btrfs device id */
	u64 devid;

	/* size of the device */
	u64 total_bytes;

	/* bytes used */
	u64 bytes_used;

	/* optimal io alignment for this device */
	u32 io_align;

	/* optimal io width for this device */
	u32 io_width;

	/* minimal io size for this device */
	u32 sector_size;

	/* type and info about this device */
	u64 type;

	/* physical drive uuid (or lvm uuid) */
	u8 uuid[BTRFS_UUID_SIZE];
};

struct btrfs_fs_devices {
	u8 fsid[BTRFS_FSID_SIZE]; /* FS specific uuid */

	/* the device with this id has the most recent coyp of the super */
	u64 latest_devid;
	u64 latest_trans;
	u64 lowest_devid;
	u64 num_devices;
	struct block_device *latest_bdev;
	struct block_device *lowest_bdev;
	struct list_head devices;
	struct list_head list;
};

struct btrfs_bio_stripe {
	struct btrfs_device *dev;
	u64 physical;
};

struct btrfs_multi_bio {
	atomic_t stripes_pending;
	bio_end_io_t *end_io;
	void *private;
	int error;
	int num_stripes;
	struct btrfs_bio_stripe stripes[];
};

#define btrfs_multi_bio_size(n) (sizeof(struct btrfs_multi_bio) + \
			    (sizeof(struct btrfs_bio_stripe) * (n)))

int btrfs_alloc_dev_extent(struct btrfs_trans_handle *trans,
			   struct btrfs_device *device,
			   u64 chunk_tree, u64 chunk_objectid,
			   u64 chunk_offset,
			   u64 num_bytes, u64 *start);
int btrfs_map_block(struct btrfs_mapping_tree *map_tree, int rw,
		    u64 logical, u64 *length,
		    struct btrfs_multi_bio **multi_ret, int mirror_num);
int btrfs_read_sys_array(struct btrfs_root *root);
int btrfs_read_chunk_tree(struct btrfs_root *root);
int btrfs_alloc_chunk(struct btrfs_trans_handle *trans,
		      struct btrfs_root *extent_root, u64 *start,
		      u64 *num_bytes, u64 type);
void btrfs_mapping_init(struct btrfs_mapping_tree *tree);
void btrfs_mapping_tree_free(struct btrfs_mapping_tree *tree);
int btrfs_map_bio(struct btrfs_root *root, int rw, struct bio *bio,
		  int mirror_num);
int btrfs_read_super_device(struct btrfs_root *root, struct extent_buffer *buf);
int btrfs_open_devices(struct btrfs_fs_devices *fs_devices,
		       int flags, void *holder);
int btrfs_scan_one_device(const char *path, int flags, void *holder,
			  struct btrfs_fs_devices **fs_devices_ret);
int btrfs_close_devices(struct btrfs_fs_devices *fs_devices);
int btrfs_add_device(struct btrfs_trans_handle *trans,
		     struct btrfs_root *root,
		     struct btrfs_device *device);
int btrfs_cleanup_fs_uuids(void);
int btrfs_num_copies(struct btrfs_mapping_tree *map_tree, u64 logical, u64 len);
#endif
