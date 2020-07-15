/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef BTRFS_VOLUMES_H
#define BTRFS_VOLUMES_H

#include <linux/bio.h>
#include <linux/sort.h>
#include <linux/btrfs.h>
#include "async-thread.h"

#define BTRFS_MAX_DATA_CHUNK_SIZE	(10ULL * SZ_1G)

extern struct mutex uuid_mutex;

#define BTRFS_STRIPE_LEN	SZ_64K

struct btrfs_io_geometry {
	/* remaining bytes before crossing a stripe */
	u64 len;
	/* offset of logical address in chunk */
	u64 offset;
	/* length of single IO stripe */
	u64 stripe_len;
	/* number of stripe where address falls */
	u64 stripe_nr;
	/* offset of address in stripe */
	u64 stripe_offset;
	/* offset of raid56 stripe into the chunk */
	u64 raid56_stripe_offset;
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

#define BTRFS_DEV_STATE_WRITEABLE	(0)
#define BTRFS_DEV_STATE_IN_FS_METADATA	(1)
#define BTRFS_DEV_STATE_MISSING		(2)
#define BTRFS_DEV_STATE_REPLACE_TGT	(3)
#define BTRFS_DEV_STATE_FLUSH_SENT	(4)

struct btrfs_device {
	struct list_head dev_list; /* device_list_mutex */
	struct list_head dev_alloc_list; /* chunk mutex */
	struct list_head post_commit_list; /* chunk mutex */
	struct btrfs_fs_devices *fs_devices;
	struct btrfs_fs_info *fs_info;

	struct rcu_string *name;

	u64 generation;

	struct block_device *bdev;

	/* the mode sent to blkdev_get */
	fmode_t mode;

	unsigned long dev_state;
	blk_status_t last_flush_error;

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
	 * and protected by chunk mutex
	 */
	u64 commit_total_bytes;

	/* bytes used on the current transaction */
	u64 commit_bytes_used;

	/* for sending down flush barriers */
	struct bio *flush_bio;
	struct completion flush_wait;

	/* per-device scrub information */
	struct scrub_ctx *scrub_ctx;

	/* readahead state */
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

	struct extent_io_tree alloc_state;

	struct completion kobj_unregister;
	/* For sysfs/FSID/devinfo/devid/ */
	struct kobject devid_kobj;
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
#elif BITS_PER_LONG==32 && defined(CONFIG_PREEMPTION)
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

enum btrfs_chunk_allocation_policy {
	BTRFS_CHUNK_ALLOC_REGULAR,
};

struct btrfs_fs_devices {
	u8 fsid[BTRFS_FSID_SIZE]; /* FS specific uuid */
	u8 metadata_uuid[BTRFS_FSID_SIZE];
	bool fsid_change;
	struct list_head fs_list;

	u64 num_devices;
	u64 open_devices;
	u64 rw_devices;
	u64 missing_devices;
	u64 total_rw_bytes;
	u64 total_devices;

	/* Highest generation number of seen devices */
	u64 latest_generation;

	struct block_device *latest_bdev;

	/* all of the devices in the FS, protected by a mutex
	 * so we can safely walk it to write out the supers without
	 * worrying about add/remove by the multi-device code.
	 * Scrubbing super can kick off supers writing by holding
	 * this mutex lock.
	 */
	struct mutex device_list_mutex;

	/* List of all devices, protected by device_list_mutex */
	struct list_head devices;

	/*
	 * Devices which can satisfy space allocation. Protected by
	 * chunk_mutex
	 */
	struct list_head alloc_list;

	struct btrfs_fs_devices *seed;
	bool seeding;

	int opened;

	/* set when we find or add a device that doesn't have the
	 * nonrot flag set
	 */
	bool rotating;

	struct btrfs_fs_info *fs_info;
	/* sysfs kobjects */
	struct kobject fsid_kobj;
	struct kobject *devices_kobj;
	struct kobject *devinfo_kobj;
	struct completion kobj_unregister;

	enum btrfs_chunk_allocation_policy chunk_alloc_policy;
};

#define BTRFS_BIO_INLINE_CSUM_SIZE	64

#define BTRFS_MAX_DEVS(info) ((BTRFS_MAX_ITEM_SIZE(info)	\
			- sizeof(struct btrfs_chunk))		\
			/ sizeof(struct btrfs_stripe) + 1)

#define BTRFS_MAX_DEVS_SYS_CHUNK ((BTRFS_SYSTEM_CHUNK_ARRAY_SIZE	\
				- 2 * sizeof(struct btrfs_disk_key)	\
				- 2 * sizeof(struct btrfs_chunk))	\
				/ sizeof(struct btrfs_stripe) + 1)

/*
 * we need the mirror number and stripe index to be passed around
 * the call chain while we are processing end_io (especially errors).
 * Really, what we need is a btrfs_bio structure that has this info
 * and is properly sized with its stripe array, but we're not there
 * quite yet.  We have our own btrfs bioset, and all of the bios
 * we allocate are actually btrfs_io_bios.  We'll cram as much of
 * struct btrfs_bio as we can into this over time.
 */
struct btrfs_io_bio {
	unsigned int mirror_num;
	struct btrfs_device *device;
	u64 logical;
	u8 *csum;
	u8 csum_inline[BTRFS_BIO_INLINE_CSUM_SIZE];
	struct bvec_iter iter;
	/*
	 * This member must come last, bio_alloc_bioset will allocate enough
	 * bytes for entire btrfs_io_bio but relies on bio being last.
	 */
	struct bio bio;
};

static inline struct btrfs_io_bio *btrfs_io_bio(struct bio *bio)
{
	return container_of(bio, struct btrfs_io_bio, bio);
}

static inline void btrfs_io_bio_free_csum(struct btrfs_io_bio *io_bio)
{
	if (io_bio->csum != io_bio->csum_inline) {
		kfree(io_bio->csum);
		io_bio->csum = NULL;
	}
}

struct btrfs_bio_stripe {
	struct btrfs_device *dev;
	u64 physical;
	u64 length; /* only used for discard mappings */
};

struct btrfs_bio {
	refcount_t refs;
	atomic_t stripes_pending;
	struct btrfs_fs_info *fs_info;
	u64 map_type; /* get from map_lookup->type */
	bio_end_io_t *end_io;
	struct bio *orig_bio;
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
	u8 sub_stripes;		/* sub_stripes info for map */
	u8 dev_stripes;		/* stripes per dev */
	u8 devs_max;		/* max devs to use */
	u8 devs_min;		/* min devs needed */
	u8 tolerated_failures;	/* max tolerated fail devs */
	u8 devs_increment;	/* ndevs has to be a multiple of this */
	u8 ncopies;		/* how many copies to data has */
	u8 nparity;		/* number of stripes worth of bytes to store
				 * parity information */
	u8 mindev_error;	/* error code if min devs requisite is unmet */
	const char raid_name[8]; /* name of the raid */
	u64 bg_flag;		/* block group flag of the raid */
};

extern const struct btrfs_raid_attr btrfs_raid_array[BTRFS_NR_RAID_TYPES];

struct map_lookup {
	u64 type;
	int io_align;
	int io_width;
	u64 stripe_len;
	int num_stripes;
	int sub_stripes;
	int verified_stripes; /* For mount time dev extent verification */
	struct btrfs_bio_stripe stripes[];
};

#define map_lookup_size(n) (sizeof(struct map_lookup) + \
			    (sizeof(struct btrfs_bio_stripe) * (n)))

struct btrfs_balance_args;
struct btrfs_balance_progress;
struct btrfs_balance_control {
	struct btrfs_balance_args data;
	struct btrfs_balance_args meta;
	struct btrfs_balance_args sys;

	u64 flags;

	struct btrfs_balance_progress stat;
};

enum btrfs_map_op {
	BTRFS_MAP_READ,
	BTRFS_MAP_WRITE,
	BTRFS_MAP_DISCARD,
	BTRFS_MAP_GET_READ_MIRRORS,
};

static inline enum btrfs_map_op btrfs_op(struct bio *bio)
{
	switch (bio_op(bio)) {
	case REQ_OP_DISCARD:
		return BTRFS_MAP_DISCARD;
	case REQ_OP_WRITE:
		return BTRFS_MAP_WRITE;
	default:
		WARN_ON_ONCE(1);
		fallthrough;
	case REQ_OP_READ:
		return BTRFS_MAP_READ;
	}
}

void btrfs_get_bbio(struct btrfs_bio *bbio);
void btrfs_put_bbio(struct btrfs_bio *bbio);
int btrfs_map_block(struct btrfs_fs_info *fs_info, enum btrfs_map_op op,
		    u64 logical, u64 *length,
		    struct btrfs_bio **bbio_ret, int mirror_num);
int btrfs_map_sblock(struct btrfs_fs_info *fs_info, enum btrfs_map_op op,
		     u64 logical, u64 *length,
		     struct btrfs_bio **bbio_ret);
int btrfs_get_io_geometry(struct btrfs_fs_info *fs_info, enum btrfs_map_op op,
		u64 logical, u64 len, struct btrfs_io_geometry *io_geom);
int btrfs_read_sys_array(struct btrfs_fs_info *fs_info);
int btrfs_read_chunk_tree(struct btrfs_fs_info *fs_info);
int btrfs_alloc_chunk(struct btrfs_trans_handle *trans, u64 type);
void btrfs_mapping_tree_free(struct extent_map_tree *tree);
blk_status_t btrfs_map_bio(struct btrfs_fs_info *fs_info, struct bio *bio,
			   int mirror_num);
int btrfs_open_devices(struct btrfs_fs_devices *fs_devices,
		       fmode_t flags, void *holder);
struct btrfs_device *btrfs_scan_one_device(const char *path,
					   fmode_t flags, void *holder);
int btrfs_forget_devices(const char *path);
void btrfs_close_devices(struct btrfs_fs_devices *fs_devices);
void btrfs_free_extra_devids(struct btrfs_fs_devices *fs_devices, int step);
void btrfs_assign_next_active_device(struct btrfs_device *device,
				     struct btrfs_device *this_dev);
struct btrfs_device *btrfs_find_device_by_devspec(struct btrfs_fs_info *fs_info,
						  u64 devid,
						  const char *devpath);
struct btrfs_device *btrfs_alloc_device(struct btrfs_fs_info *fs_info,
					const u64 *devid,
					const u8 *uuid);
void btrfs_free_device(struct btrfs_device *device);
int btrfs_rm_device(struct btrfs_fs_info *fs_info,
		    const char *device_path, u64 devid);
void __exit btrfs_cleanup_fs_uuids(void);
int btrfs_num_copies(struct btrfs_fs_info *fs_info, u64 logical, u64 len);
int btrfs_grow_device(struct btrfs_trans_handle *trans,
		      struct btrfs_device *device, u64 new_size);
struct btrfs_device *btrfs_find_device(struct btrfs_fs_devices *fs_devices,
				       u64 devid, u8 *uuid, u8 *fsid, bool seed);
int btrfs_shrink_device(struct btrfs_device *device, u64 new_size);
int btrfs_init_new_device(struct btrfs_fs_info *fs_info, const char *path);
int btrfs_balance(struct btrfs_fs_info *fs_info,
		  struct btrfs_balance_control *bctl,
		  struct btrfs_ioctl_balance_args *bargs);
void btrfs_describe_block_groups(u64 flags, char *buf, u32 size_buf);
int btrfs_resume_balance_async(struct btrfs_fs_info *fs_info);
int btrfs_recover_balance(struct btrfs_fs_info *fs_info);
int btrfs_pause_balance(struct btrfs_fs_info *fs_info);
int btrfs_cancel_balance(struct btrfs_fs_info *fs_info);
int btrfs_create_uuid_tree(struct btrfs_fs_info *fs_info);
int btrfs_uuid_scan_kthread(void *data);
int btrfs_chunk_readonly(struct btrfs_fs_info *fs_info, u64 chunk_offset);
int find_free_dev_extent(struct btrfs_device *device, u64 num_bytes,
			 u64 *start, u64 *max_avail);
void btrfs_dev_stat_inc_and_print(struct btrfs_device *dev, int index);
int btrfs_get_dev_stats(struct btrfs_fs_info *fs_info,
			struct btrfs_ioctl_get_dev_stats *stats);
void btrfs_init_devices_late(struct btrfs_fs_info *fs_info);
int btrfs_init_dev_stats(struct btrfs_fs_info *fs_info);
int btrfs_run_dev_stats(struct btrfs_trans_handle *trans);
void btrfs_rm_dev_replace_remove_srcdev(struct btrfs_device *srcdev);
void btrfs_rm_dev_replace_free_srcdev(struct btrfs_device *srcdev);
void btrfs_destroy_dev_replace_tgtdev(struct btrfs_device *tgtdev);
int btrfs_is_parity_mirror(struct btrfs_fs_info *fs_info,
			   u64 logical, u64 len);
unsigned long btrfs_full_stripe_len(struct btrfs_fs_info *fs_info,
				    u64 logical);
int btrfs_finish_chunk_alloc(struct btrfs_trans_handle *trans,
			     u64 chunk_offset, u64 chunk_size);
int btrfs_remove_chunk(struct btrfs_trans_handle *trans, u64 chunk_offset);
struct extent_map *btrfs_get_chunk_map(struct btrfs_fs_info *fs_info,
				       u64 logical, u64 length);
void btrfs_release_disk_super(struct btrfs_super_block *super);

static inline void btrfs_dev_stat_inc(struct btrfs_device *dev,
				      int index)
{
	atomic_inc(dev->dev_stat_values + index);
	/*
	 * This memory barrier orders stores updating statistics before stores
	 * updating dev_stats_ccnt.
	 *
	 * It pairs with smp_rmb() in btrfs_run_dev_stats().
	 */
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
	/*
	 * atomic_xchg implies a full memory barriers as per atomic_t.txt:
	 * - RMW operations that have a return value are fully ordered;
	 *
	 * This implicit memory barriers is paired with the smp_rmb in
	 * btrfs_run_dev_stats
	 */
	atomic_inc(&dev->dev_stats_ccnt);
	return ret;
}

static inline void btrfs_dev_stat_set(struct btrfs_device *dev,
				      int index, unsigned long val)
{
	atomic_set(dev->dev_stat_values + index, val);
	/*
	 * This memory barrier orders stores updating statistics before stores
	 * updating dev_stats_ccnt.
	 *
	 * It pairs with smp_rmb() in btrfs_run_dev_stats().
	 */
	smp_mb__before_atomic();
	atomic_inc(&dev->dev_stats_ccnt);
}

/*
 * Convert block group flags (BTRFS_BLOCK_GROUP_*) to btrfs_raid_types, which
 * can be used as index to access btrfs_raid_array[].
 */
static inline enum btrfs_raid_types btrfs_bg_flags_to_raid_index(u64 flags)
{
	if (flags & BTRFS_BLOCK_GROUP_RAID10)
		return BTRFS_RAID_RAID10;
	else if (flags & BTRFS_BLOCK_GROUP_RAID1)
		return BTRFS_RAID_RAID1;
	else if (flags & BTRFS_BLOCK_GROUP_RAID1C3)
		return BTRFS_RAID_RAID1C3;
	else if (flags & BTRFS_BLOCK_GROUP_RAID1C4)
		return BTRFS_RAID_RAID1C4;
	else if (flags & BTRFS_BLOCK_GROUP_DUP)
		return BTRFS_RAID_DUP;
	else if (flags & BTRFS_BLOCK_GROUP_RAID0)
		return BTRFS_RAID_RAID0;
	else if (flags & BTRFS_BLOCK_GROUP_RAID5)
		return BTRFS_RAID_RAID5;
	else if (flags & BTRFS_BLOCK_GROUP_RAID6)
		return BTRFS_RAID_RAID6;

	return BTRFS_RAID_SINGLE; /* BTRFS_BLOCK_GROUP_SINGLE */
}

void btrfs_commit_device_sizes(struct btrfs_transaction *trans);

struct list_head * __attribute_const__ btrfs_get_fs_uuids(void);
void btrfs_set_fs_info_ptr(struct btrfs_fs_info *fs_info);
void btrfs_reset_fs_info_ptr(struct btrfs_fs_info *fs_info);
bool btrfs_check_rw_degradable(struct btrfs_fs_info *fs_info,
					struct btrfs_device *failing_dev);
void btrfs_scratch_superblocks(struct btrfs_fs_info *fs_info,
			       struct block_device *bdev,
			       const char *device_path);

int btrfs_bg_type_to_factor(u64 flags);
const char *btrfs_bg_type_to_raid_name(u64 flags);
int btrfs_verify_dev_extents(struct btrfs_fs_info *fs_info);

#endif
