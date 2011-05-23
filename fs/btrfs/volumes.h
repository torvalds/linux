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
#include <linux/sort.h>
#include "async-thread.h"

#define BTRFS_STRIPE_LEN	(64 * 1024)

struct buffer_head;
struct btrfs_pending_bios {
	struct bio *head;
	struct bio *tail;
};

struct btrfs_device {
	struct list_head dev_list;
	struct list_head dev_alloc_list;
	struct btrfs_fs_devices *fs_devices;
	struct btrfs_root *dev_root;

	/* regular prio bios */
	struct btrfs_pending_bios pending_bios;
	/* WRITE_SYNC bios */
	struct btrfs_pending_bios pending_sync_bios;

	int running_pending;
	u64 generation;

	int writeable;
	int in_fs_metadata;
	int missing;

	spinlock_t io_lock;

	struct block_device *bdev;

	/* the mode sent to blkdev_get */
	fmode_t mode;

	char *name;

	/* the internal btrfs device id */
	u64 devid;

	/* size of the device */
	u64 total_bytes;

	/* size of the disk */
	u64 disk_total_bytes;

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

	/* per-device scrub information */
	struct scrub_dev *scrub_device;

	struct btrfs_work work;
	struct rcu_head rcu;
	struct work_struct rcu_work;
};

struct btrfs_fs_devices {
	u8 fsid[BTRFS_FSID_SIZE]; /* FS specific uuid */

	/* the device with this id has the most recent copy of the super */
	u64 latest_devid;
	u64 latest_trans;
	u64 num_devices;
	u64 open_devices;
	u64 rw_devices;
	u64 missing_devices;
	u64 total_rw_bytes;
	struct block_device *latest_bdev;

	/* all of the devices in the FS, protected by a mutex
	 * so we can safely walk it to write out the supers without
	 * worrying about add/remove by the multi-device code
	 */
	struct mutex device_list_mutex;
	struct list_head devices;

	/* devices not currently being allocated */
	struct list_head alloc_list;
	struct list_head list;

	struct btrfs_fs_devices *seed;
	int seeding;

	int opened;

	/* set when we find or add a device that doesn't have the
	 * nonrot flag set
	 */
	int rotating;
};

struct btrfs_bio_stripe {
	struct btrfs_device *dev;
	u64 physical;
	u64 length; /* only used for discard mappings */
};

struct btrfs_multi_bio {
	atomic_t stripes_pending;
	bio_end_io_t *end_io;
	struct bio *orig_bio;
	void *private;
	atomic_t error;
	int max_errors;
	int num_stripes;
	struct btrfs_bio_stripe stripes[];
};

struct btrfs_device_info {
	struct btrfs_device *dev;
	u64 dev_offset;
	u64 max_avail;
	u64 total_avail;
};

struct map_lookup {
	u64 type;
	int io_align;
	int io_width;
	int stripe_len;
	int sector_size;
	int num_stripes;
	int sub_stripes;
	struct btrfs_bio_stripe stripes[];
};

#define map_lookup_size(n) (sizeof(struct map_lookup) + \
			    (sizeof(struct btrfs_bio_stripe) * (n)))

int btrfs_account_dev_extents_size(struct btrfs_device *device, u64 start,
				   u64 end, u64 *length);

#define btrfs_multi_bio_size(n) (sizeof(struct btrfs_multi_bio) + \
			    (sizeof(struct btrfs_bio_stripe) * (n)))

int btrfs_alloc_dev_extent(struct btrfs_trans_handle *trans,
			   struct btrfs_device *device,
			   u64 chunk_tree, u64 chunk_objectid,
			   u64 chunk_offset, u64 start, u64 num_bytes);
int btrfs_map_block(struct btrfs_mapping_tree *map_tree, int rw,
		    u64 logical, u64 *length,
		    struct btrfs_multi_bio **multi_ret, int mirror_num);
int btrfs_rmap_block(struct btrfs_mapping_tree *map_tree,
		     u64 chunk_start, u64 physical, u64 devid,
		     u64 **logical, int *naddrs, int *stripe_len);
int btrfs_read_sys_array(struct btrfs_root *root);
int btrfs_read_chunk_tree(struct btrfs_root *root);
int btrfs_alloc_chunk(struct btrfs_trans_handle *trans,
		      struct btrfs_root *extent_root, u64 type);
void btrfs_mapping_init(struct btrfs_mapping_tree *tree);
void btrfs_mapping_tree_free(struct btrfs_mapping_tree *tree);
int btrfs_map_bio(struct btrfs_root *root, int rw, struct bio *bio,
		  int mirror_num, int async_submit);
int btrfs_open_devices(struct btrfs_fs_devices *fs_devices,
		       fmode_t flags, void *holder);
int btrfs_scan_one_device(const char *path, fmode_t flags, void *holder,
			  struct btrfs_fs_devices **fs_devices_ret);
int btrfs_close_devices(struct btrfs_fs_devices *fs_devices);
int btrfs_close_extra_devices(struct btrfs_fs_devices *fs_devices);
int btrfs_add_device(struct btrfs_trans_handle *trans,
		     struct btrfs_root *root,
		     struct btrfs_device *device);
int btrfs_rm_device(struct btrfs_root *root, char *device_path);
int btrfs_cleanup_fs_uuids(void);
int btrfs_num_copies(struct btrfs_mapping_tree *map_tree, u64 logical, u64 len);
int btrfs_grow_device(struct btrfs_trans_handle *trans,
		      struct btrfs_device *device, u64 new_size);
struct btrfs_device *btrfs_find_device(struct btrfs_root *root, u64 devid,
				       u8 *uuid, u8 *fsid);
int btrfs_shrink_device(struct btrfs_device *device, u64 new_size);
int btrfs_init_new_device(struct btrfs_root *root, char *path);
int btrfs_balance(struct btrfs_root *dev_root);
int btrfs_chunk_readonly(struct btrfs_root *root, u64 chunk_offset);
int find_free_dev_extent(struct btrfs_trans_handle *trans,
			 struct btrfs_device *device, u64 num_bytes,
			 u64 *start, u64 *max_avail);
#endif
