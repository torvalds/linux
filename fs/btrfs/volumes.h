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
#include <linux/btrfs.h>
#include "async-thread.h"

extern struct mutex uuid_mutex;

#define BTRFS_STRIPE_LEN	(64 * 1024)

struct buffer_head;
struct btrfs_pending_bios {
	struct bio *head;
	struct bio *tail;
};

/*
 * Use sequence counter to get consistent device stat data on
 * 32-bit processors.
 */
#if BITS_PER_LONG==32 && defined(CONFIG_SMP)
#include <linux/seqlock.h>
#define __BTRFS_NEED_DEVICE_DATA_ORDERED
#define btrfs_device_data_ordered_init(device)	\
	seqcount_init(&device->data_seqcount)
#else
#define btrfs_device_data_ordered_init(device) do { } while (0)
#endif

struct btrfs_device {
	struct list_head dev_list;
	struct list_head dev_alloc_list;
	struct btrfs_fs_devices *fs_devices;

	struct btrfs_root *dev_root;

	struct rcu_string *name;

	u64 generation;

	spinlock_t io_lock ____cacheline_aligned;
	int running_pending;
	/* regular prio bios */
	struct btrfs_pending_bios pending_bios;
	/* WRITE_SYNC bios */
	struct btrfs_pending_bios pending_sync_bios;

	struct block_device *bdev;

	/* the mode sent to blkdev_get */
	fmode_t mode;

	int writeable;
	int in_fs_metadata;
	int missing;
	int can_discard;
	int is_tgtdev_for_dev_replace;

#ifdef __BTRFS_NEED_DEVICE_DATA_ORDERED
	seqcount_t data_seqcount;
#endif

	/* the internal btrfs device id */
	u64 devid;

	/* size of the device in memory */
	u64 total_bytes;

	/* size of the device on disk */
	u64 disk_total_bytes;

	/* bytes used */
	u64 bytes_used;

	/* optimal io alignment for this device */
	u32 io_align;

	/* optimal io width for this device */
	u32 io_width;
	/* type and info about this device */
	u64 type;

	/* minimal io size for this device */
	u32 sector_size;

	/* physical drive uuid (or lvm uuid) */
	u8 uuid[BTRFS_UUID_SIZE];

	/*
	 * size of the device on the current transaction
	 *
	 * This variant is update when committing the transaction,
	 * and protected by device_list_mutex
	 */
	u64 commit_total_bytes;

	/* bytes used on the current transaction */
	u64 commit_bytes_used;
	/*
	 * used to manage the device which is resized
	 *
	 * It is protected by chunk_lock.
	 */
	struct list_head resized_list;

	/* for sending down flush barriers */
	int nobarriers;
	struct bio *flush_bio;
	struct completion flush_wait;

	/* per-device scrub information */
	struct scrub_ctx *scrub_device;

	struct btrfs_work work;
	struct rcu_head rcu;
	struct work_struct rcu_work;

	/* readahead state */
	spinlock_t reada_lock;
	atomic_t reada_in_flight;
	u64 reada_next;
	struct reada_zone *reada_curr_zone;
	struct radix_tree_root reada_zones;
	struct radix_tree_root reada_extents;

	/* disk I/O failure stats. For detailed description refer to
	 * enum btrfs_dev_stat_values in ioctl.h */
	int dev_stats_valid;

	/* Counter to record the change of device stats */
	atomic_t dev_stats_ccnt;
	atomic_t dev_stat_values[BTRFS_DEV_STAT_VALUES_MAX];
};

/*
 * If we read those variants at the context of their own lock, we needn't
 * use the following helpers, reading them directly is safe.
 */
#if BITS_PER_LONG==32 && defined(CONFIG_SMP)
#define BTRFS_DEVICE_GETSET_FUNCS(name)					\
static inline u64							\
btrfs_device_get_##name(const struct btrfs_device *dev)			\
{									\
	u64 size;							\
	unsigned int seq;						\
									\
	do {								\
		seq = read_seqcount_begin(&dev->data_seqcount);		\
		size = dev->name;					\
	} while (read_seqcount_retry(&dev->data_seqcount, seq));	\
	return size;							\
}									\
									\
static inline void							\
btrfs_device_set_##name(struct btrfs_device *dev, u64 size)		\
{									\
	preempt_disable();						\
	write_seqcount_begin(&dev->data_seqcount);			\
	dev->name = size;						\
	write_seqcount_end(&dev->data_seqcount);			\
	preempt_enable();						\
}
#elif BITS_PER_LONG==32 && defined(CONFIG_PREEMPT)
#define BTRFS_DEVICE_GETSET_FUNCS(name)					\
static inline u64							\
btrfs_device_get_##name(const struct btrfs_device *dev)			\
{									\
	u64 size;							\
									\
	preempt_disable();						\
	size = dev->name;						\
	preempt_enable();						\
	return size;							\
}									\
									\
static inline void							\
btrfs_device_set_##name(struct btrfs_device *dev, u64 size)		\
{									\
	preempt_disable();						\
	dev->name = size;						\
	preempt_enable();						\
}
#else
#define BTRFS_DEVICE_GETSET_FUNCS(name)					\
static inline u64							\
btrfs_device_get_##name(const struct btrfs_device *dev)			\
{									\
	return dev->name;						\
}									\
									\
static inline void							\
btrfs_device_set_##name(struct btrfs_device *dev, u64 size)		\
{									\
	dev->name = size;						\
}
#endif

BTRFS_DEVICE_GETSET_FUNCS(total_bytes);
BTRFS_DEVICE_GETSET_FUNCS(disk_total_bytes);
BTRFS_DEVICE_GETSET_FUNCS(bytes_used);

struct btrfs_fs_devices {
	u8 fsid[BTRFS_FSID_SIZE]; /* FS specific uuid */

	u64 num_devices;
	u64 open_devices;
	u64 rw_devices;
	u64 missing_devices;
	u64 total_rw_bytes;
	u64 total_devices;
	struct block_device *latest_bdev;

	/* all of the devices in the FS, protected by a mutex
	 * so we can safely walk it to write out the supers without
	 * worrying about add/remove by the multi-device code.
	 * Scrubbing super can kick off supers writing by holding
	 * this mutex lock.
	 */
	struct mutex device_list_mutex;
	struct list_head devices;

	struct list_head resized_devices;
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

#define BTRFS_BIO_INLINE_CSUM_SIZE	64

/*
 * we need the mirror number and stripe index to be passed around
 * the call chain while we are processing end_io (especially errors).
 * Really, what we need is a btrfs_bio structure that has this info
 * and is properly sized with its stripe array, but we're not there
 * quite yet.  We have our own btrfs bioset, and all of the bios
 * we allocate are actually btrfs_io_bios.  We'll cram as much of
 * struct btrfs_bio as we can into this over time.
 */
typedef void (btrfs_io_bio_end_io_t) (struct btrfs_io_bio *bio, int err);
struct btrfs_io_bio {
	unsigned int mirror_num;
	unsigned int stripe_index;
	u64 logical;
	u8 *csum;
	u8 csum_inline[BTRFS_BIO_INLINE_CSUM_SIZE];
	u8 *csum_allocated;
	btrfs_io_bio_end_io_t *end_io;
	struct bio bio;
};

static inline struct btrfs_io_bio *btrfs_io_bio(struct bio *bio)
{
	return container_of(bio, struct btrfs_io_bio, bio);
}

struct btrfs_bio_stripe {
	struct btrfs_device *dev;
	u64 physical;
	u64 length; /* only used for discard mappings */
};

struct btrfs_bio;
typedef void (btrfs_bio_end_io_t) (struct btrfs_bio *bio, int err);

#define BTRFS_BIO_ORIG_BIO_SUBMITTED	(1 << 0)

struct btrfs_bio {
	atomic_t refs;
	atomic_t stripes_pending;
	struct btrfs_fs_info *fs_info;
	u64 map_type; /* get from map_lookup->type */
	bio_end_io_t *end_io;
	struct bio *orig_bio;
	unsigned long flags;
	void *private;
	atomic_t error;
	int max_errors;
	int num_stripes;
	int mirror_num;
	int num_tgtdevs;
	int *tgtdev_map;
	/*
	 * logical block numbers for the start of each stripe
	 * The last one or two are p/q.  These are sorted,
	 * so raid_map[0] is the start of our full stripe
	 */
	u64 *raid_map;
	struct btrfs_bio_stripe stripes[];
};

struct btrfs_device_info {
	struct btrfs_device *dev;
	u64 dev_offset;
	u64 max_avail;
	u64 total_avail;
};

struct btrfs_raid_attr {
	int sub_stripes;	/* sub_stripes info for map */
	int dev_stripes;	/* stripes per dev */
	int devs_max;		/* max devs to use */
	int devs_min;		/* min devs needed */
	int devs_increment;	/* ndevs has to be a multiple of this */
	int ncopies;		/* how many copies to data has */
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

/*
 * Restriper's general type filter
 */
#define BTRFS_BALANCE_DATA		(1ULL << 0)
#define BTRFS_BALANCE_SYSTEM		(1ULL << 1)
#define BTRFS_BALANCE_METADATA		(1ULL << 2)

#define BTRFS_BALANCE_TYPE_MASK		(BTRFS_BALANCE_DATA |	    \
					 BTRFS_BALANCE_SYSTEM |	    \
					 BTRFS_BALANCE_METADATA)

#define BTRFS_BALANCE_FORCE		(1ULL << 3)
#define BTRFS_BALANCE_RESUME		(1ULL << 4)

/*
 * Balance filters
 */
#define BTRFS_BALANCE_ARGS_PROFILES	(1ULL << 0)
#define BTRFS_BALANCE_ARGS_USAGE	(1ULL << 1)
#define BTRFS_BALANCE_ARGS_DEVID	(1ULL << 2)
#define BTRFS_BALANCE_ARGS_DRANGE	(1ULL << 3)
#define BTRFS_BALANCE_ARGS_VRANGE	(1ULL << 4)
#define BTRFS_BALANCE_ARGS_LIMIT	(1ULL << 5)

/*
 * Profile changing flags.  When SOFT is set we won't relocate chunk if
 * it already has the target profile (even though it may be
 * half-filled).
 */
#define BTRFS_BALANCE_ARGS_CONVERT	(1ULL << 8)
#define BTRFS_BALANCE_ARGS_SOFT		(1ULL << 9)

struct btrfs_balance_args;
struct btrfs_balance_progress;
struct btrfs_balance_control {
	struct btrfs_fs_info *fs_info;

	struct btrfs_balance_args data;
	struct btrfs_balance_args meta;
	struct btrfs_balance_args sys;

	u64 flags;

	struct btrfs_balance_progress stat;
};

int btrfs_account_dev_extents_size(struct btrfs_device *device, u64 start,
				   u64 end, u64 *length);
void btrfs_get_bbio(struct btrfs_bio *bbio);
void btrfs_put_bbio(struct btrfs_bio *bbio);
int btrfs_map_block(struct btrfs_fs_info *fs_info, int rw,
		    u64 logical, u64 *length,
		    struct btrfs_bio **bbio_ret, int mirror_num);
int btrfs_map_sblock(struct btrfs_fs_info *fs_info, int rw,
		     u64 logical, u64 *length,
		     struct btrfs_bio **bbio_ret, int mirror_num,
		     int need_raid_map);
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
void btrfs_close_extra_devices(struct btrfs_fs_devices *fs_devices, int step);
int btrfs_find_device_missing_or_by_path(struct btrfs_root *root,
					 char *device_path,
					 struct btrfs_device **device);
struct btrfs_device *btrfs_alloc_device(struct btrfs_fs_info *fs_info,
					const u64 *devid,
					const u8 *uuid);
int btrfs_rm_device(struct btrfs_root *root, char *device_path);
void btrfs_cleanup_fs_uuids(void);
int btrfs_num_copies(struct btrfs_fs_info *fs_info, u64 logical, u64 len);
int btrfs_grow_device(struct btrfs_trans_handle *trans,
		      struct btrfs_device *device, u64 new_size);
struct btrfs_device *btrfs_find_device(struct btrfs_fs_info *fs_info, u64 devid,
				       u8 *uuid, u8 *fsid);
int btrfs_shrink_device(struct btrfs_device *device, u64 new_size);
int btrfs_init_new_device(struct btrfs_root *root, char *path);
int btrfs_init_dev_replace_tgtdev(struct btrfs_root *root, char *device_path,
				  struct btrfs_device *srcdev,
				  struct btrfs_device **device_out);
int btrfs_balance(struct btrfs_balance_control *bctl,
		  struct btrfs_ioctl_balance_args *bargs);
int btrfs_resume_balance_async(struct btrfs_fs_info *fs_info);
int btrfs_recover_balance(struct btrfs_fs_info *fs_info);
int btrfs_pause_balance(struct btrfs_fs_info *fs_info);
int btrfs_cancel_balance(struct btrfs_fs_info *fs_info);
int btrfs_create_uuid_tree(struct btrfs_fs_info *fs_info);
int btrfs_check_uuid_tree(struct btrfs_fs_info *fs_info);
int btrfs_chunk_readonly(struct btrfs_root *root, u64 chunk_offset);
int find_free_dev_extent(struct btrfs_trans_handle *trans,
			 struct btrfs_device *device, u64 num_bytes,
			 u64 *start, u64 *max_avail);
void btrfs_dev_stat_inc_and_print(struct btrfs_device *dev, int index);
int btrfs_get_dev_stats(struct btrfs_root *root,
			struct btrfs_ioctl_get_dev_stats *stats);
void btrfs_init_devices_late(struct btrfs_fs_info *fs_info);
int btrfs_init_dev_stats(struct btrfs_fs_info *fs_info);
int btrfs_run_dev_stats(struct btrfs_trans_handle *trans,
			struct btrfs_fs_info *fs_info);
void btrfs_rm_dev_replace_remove_srcdev(struct btrfs_fs_info *fs_info,
					struct btrfs_device *srcdev);
void btrfs_rm_dev_replace_free_srcdev(struct btrfs_fs_info *fs_info,
				      struct btrfs_device *srcdev);
void btrfs_destroy_dev_replace_tgtdev(struct btrfs_fs_info *fs_info,
				      struct btrfs_device *tgtdev);
void btrfs_init_dev_replace_tgtdev_for_resume(struct btrfs_fs_info *fs_info,
					      struct btrfs_device *tgtdev);
int btrfs_scratch_superblock(struct btrfs_device *device);
int btrfs_is_parity_mirror(struct btrfs_mapping_tree *map_tree,
			   u64 logical, u64 len, int mirror_num);
unsigned long btrfs_full_stripe_len(struct btrfs_root *root,
				    struct btrfs_mapping_tree *map_tree,
				    u64 logical);
int btrfs_finish_chunk_alloc(struct btrfs_trans_handle *trans,
				struct btrfs_root *extent_root,
				u64 chunk_offset, u64 chunk_size);
int btrfs_remove_chunk(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, u64 chunk_offset);

static inline int btrfs_dev_stats_dirty(struct btrfs_device *dev)
{
	return atomic_read(&dev->dev_stats_ccnt);
}

static inline void btrfs_dev_stat_inc(struct btrfs_device *dev,
				      int index)
{
	atomic_inc(dev->dev_stat_values + index);
	smp_mb__before_atomic();
	atomic_inc(&dev->dev_stats_ccnt);
}

static inline int btrfs_dev_stat_read(struct btrfs_device *dev,
				      int index)
{
	return atomic_read(dev->dev_stat_values + index);
}

static inline int btrfs_dev_stat_read_and_reset(struct btrfs_device *dev,
						int index)
{
	int ret;

	ret = atomic_xchg(dev->dev_stat_values + index, 0);
	smp_mb__before_atomic();
	atomic_inc(&dev->dev_stats_ccnt);
	return ret;
}

static inline void btrfs_dev_stat_set(struct btrfs_device *dev,
				      int index, unsigned long val)
{
	atomic_set(dev->dev_stat_values + index, val);
	smp_mb__before_atomic();
	atomic_inc(&dev->dev_stats_ccnt);
}

static inline void btrfs_dev_stat_reset(struct btrfs_device *dev,
					int index)
{
	btrfs_dev_stat_set(dev, index, 0);
}

void btrfs_update_commit_device_size(struct btrfs_fs_info *fs_info);
void btrfs_update_commit_device_bytes_used(struct btrfs_root *root,
					struct btrfs_transaction *transaction);

static inline void lock_chunks(struct btrfs_root *root)
{
	mutex_lock(&root->fs_info->chunk_mutex);
}

static inline void unlock_chunks(struct btrfs_root *root)
{
	mutex_unlock(&root->fs_info->chunk_mutex);
}


#endif
