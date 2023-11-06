/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef BTRFS_VOLUMES_H
#define BTRFS_VOLUMES_H

#include <linux/sort.h>
#include <linux/btrfs.h>
#include "async-thread.h"
#include "messages.h"
#include "tree-checker.h"
#include "rcu-string.h"

#define BTRFS_MAX_DATA_CHUNK_SIZE	(10ULL * SZ_1G)

extern struct mutex uuid_mutex;

#define BTRFS_STRIPE_LEN		SZ_64K
#define BTRFS_STRIPE_LEN_SHIFT		(16)
#define BTRFS_STRIPE_LEN_MASK		(BTRFS_STRIPE_LEN - 1)

static_assert(const_ilog2(BTRFS_STRIPE_LEN) == BTRFS_STRIPE_LEN_SHIFT);

/* Used by sanity check for btrfs_raid_types. */
#define const_ffs(n) (__builtin_ctzll(n) + 1)

/*
 * The conversion from BTRFS_BLOCK_GROUP_* bits to btrfs_raid_type requires
 * RAID0 always to be the lowest profile bit.
 * Although it's part of on-disk format and should never change, do extra
 * compile-time sanity checks.
 */
static_assert(const_ffs(BTRFS_BLOCK_GROUP_RAID0) <
	      const_ffs(BTRFS_BLOCK_GROUP_PROFILE_MASK & ~BTRFS_BLOCK_GROUP_RAID0));
static_assert(const_ilog2(BTRFS_BLOCK_GROUP_RAID0) >
	      ilog2(BTRFS_BLOCK_GROUP_TYPE_MASK));

/* ilog2() can handle both constants and variables */
#define BTRFS_BG_FLAG_TO_INDEX(profile)					\
	ilog2((profile) >> (ilog2(BTRFS_BLOCK_GROUP_RAID0) - 1))

enum btrfs_raid_types {
	/* SINGLE is the special one as it doesn't have on-disk bit. */
	BTRFS_RAID_SINGLE  = 0,

	BTRFS_RAID_RAID0   = BTRFS_BG_FLAG_TO_INDEX(BTRFS_BLOCK_GROUP_RAID0),
	BTRFS_RAID_RAID1   = BTRFS_BG_FLAG_TO_INDEX(BTRFS_BLOCK_GROUP_RAID1),
	BTRFS_RAID_DUP	   = BTRFS_BG_FLAG_TO_INDEX(BTRFS_BLOCK_GROUP_DUP),
	BTRFS_RAID_RAID10  = BTRFS_BG_FLAG_TO_INDEX(BTRFS_BLOCK_GROUP_RAID10),
	BTRFS_RAID_RAID5   = BTRFS_BG_FLAG_TO_INDEX(BTRFS_BLOCK_GROUP_RAID5),
	BTRFS_RAID_RAID6   = BTRFS_BG_FLAG_TO_INDEX(BTRFS_BLOCK_GROUP_RAID6),
	BTRFS_RAID_RAID1C3 = BTRFS_BG_FLAG_TO_INDEX(BTRFS_BLOCK_GROUP_RAID1C3),
	BTRFS_RAID_RAID1C4 = BTRFS_BG_FLAG_TO_INDEX(BTRFS_BLOCK_GROUP_RAID1C4),

	BTRFS_NR_RAID_TYPES
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
#define BTRFS_DEV_STATE_NO_READA	(5)

struct btrfs_zoned_device_info;

struct btrfs_device {
	struct list_head dev_list; /* device_list_mutex */
	struct list_head dev_alloc_list; /* chunk mutex */
	struct list_head post_commit_list; /* chunk mutex */
	struct btrfs_fs_devices *fs_devices;
	struct btrfs_fs_info *fs_info;

	struct rcu_string __rcu *name;

	u64 generation;

	struct block_device *bdev;

	struct btrfs_zoned_device_info *zone_info;

	/* block device holder for blkdev_get/put */
	void *holder;

	/*
	 * Device's major-minor number. Must be set even if the device is not
	 * opened (bdev == NULL), unless the device is missing.
	 */
	dev_t devt;
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

	/* Bio used for flushing device barriers */
	struct bio flush_bio;
	struct completion flush_wait;

	/* per-device scrub information */
	struct scrub_ctx *scrub_ctx;

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

	/* Bandwidth limit for scrub, in bytes */
	u64 scrub_speed_max;
};

/*
 * Block group or device which contains an active swapfile. Used for preventing
 * unsafe operations while a swapfile is active.
 *
 * These are sorted on (ptr, inode) (note that a block group or device can
 * contain more than one swapfile). We compare the pointer values because we
 * don't actually care what the object is, we just need a quick check whether
 * the object exists in the rbtree.
 */
struct btrfs_swapfile_pin {
	struct rb_node node;
	void *ptr;
	struct inode *inode;
	/*
	 * If true, ptr points to a struct btrfs_block_group. Otherwise, ptr
	 * points to a struct btrfs_device.
	 */
	bool is_block_group;
	/*
	 * Only used when 'is_block_group' is true and it is the number of
	 * extents used by a swapfile for this block group ('ptr' field).
	 */
	int bg_extent_count;
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
	BTRFS_CHUNK_ALLOC_ZONED,
};

/*
 * Read policies for mirrored block group profiles, read picks the stripe based
 * on these policies.
 */
enum btrfs_read_policy {
	/* Use process PID to choose the stripe */
	BTRFS_READ_POLICY_PID,
	BTRFS_NR_READ_POLICY,
};

struct btrfs_fs_devices {
	u8 fsid[BTRFS_FSID_SIZE]; /* FS specific uuid */

	/*
	 * UUID written into the btree blocks:
	 *
	 * - If metadata_uuid != fsid then super block must have
	 *   BTRFS_FEATURE_INCOMPAT_METADATA_UUID flag set.
	 *
	 * - Following shall be true at all times:
	 *   - metadata_uuid == btrfs_header::fsid
	 *   - metadata_uuid == btrfs_dev_item::fsid
	 */
	u8 metadata_uuid[BTRFS_FSID_SIZE];

	struct list_head fs_list;

	/*
	 * Number of devices under this fsid including missing and
	 * replace-target device and excludes seed devices.
	 */
	u64 num_devices;

	/*
	 * The number of devices that successfully opened, including
	 * replace-target, excludes seed devices.
	 */
	u64 open_devices;

	/* The number of devices that are under the chunk allocation list. */
	u64 rw_devices;

	/* Count of missing devices under this fsid excluding seed device. */
	u64 missing_devices;
	u64 total_rw_bytes;

	/*
	 * Count of devices from btrfs_super_block::num_devices for this fsid,
	 * which includes the seed device, excludes the transient replace-target
	 * device.
	 */
	u64 total_devices;

	/* Highest generation number of seen devices */
	u64 latest_generation;

	/*
	 * The mount device or a device with highest generation after removal
	 * or replace.
	 */
	struct btrfs_device *latest_dev;

	/*
	 * All of the devices in the filesystem, protected by a mutex so we can
	 * safely walk it to write out the super blocks without worrying about
	 * adding/removing by the multi-device code. Scrubbing super block can
	 * kick off supers writing by holding this mutex lock.
	 */
	struct mutex device_list_mutex;

	/* List of all devices, protected by device_list_mutex */
	struct list_head devices;

	/* Devices which can satisfy space allocation. Protected by * chunk_mutex. */
	struct list_head alloc_list;

	struct list_head seed_list;

	/* Count fs-devices opened. */
	int opened;

	/* Set when we find or add a device that doesn't have the nonrot flag set. */
	bool rotating;
	/* Devices support TRIM/discard commands. */
	bool discardable;
	bool fsid_change;
	/* The filesystem is a seed filesystem. */
	bool seeding;

	struct btrfs_fs_info *fs_info;
	/* sysfs kobjects */
	struct kobject fsid_kobj;
	struct kobject *devices_kobj;
	struct kobject *devinfo_kobj;
	struct completion kobj_unregister;

	enum btrfs_chunk_allocation_policy chunk_alloc_policy;

	/* Policy used to read the mirrored stripes. */
	enum btrfs_read_policy read_policy;
};

#define BTRFS_MAX_DEVS(info) ((BTRFS_MAX_ITEM_SIZE(info)	\
			- sizeof(struct btrfs_chunk))		\
			/ sizeof(struct btrfs_stripe) + 1)

#define BTRFS_MAX_DEVS_SYS_CHUNK ((BTRFS_SYSTEM_CHUNK_ARRAY_SIZE	\
				- 2 * sizeof(struct btrfs_disk_key)	\
				- 2 * sizeof(struct btrfs_chunk))	\
				/ sizeof(struct btrfs_stripe) + 1)

struct btrfs_io_stripe {
	struct btrfs_device *dev;
	union {
		/* Block mapping */
		u64 physical;
		/* For the endio handler */
		struct btrfs_io_context *bioc;
	};
};

struct btrfs_discard_stripe {
	struct btrfs_device *dev;
	u64 physical;
	u64 length;
};

/*
 * Context for IO subsmission for device stripe.
 *
 * - Track the unfinished mirrors for mirror based profiles
 *   Mirror based profiles are SINGLE/DUP/RAID1/RAID10.
 *
 * - Contain the logical -> physical mapping info
 *   Used by submit_stripe_bio() for mapping logical bio
 *   into physical device address.
 *
 * - Contain device replace info
 *   Used by handle_ops_on_dev_replace() to copy logical bios
 *   into the new device.
 *
 * - Contain RAID56 full stripe logical bytenrs
 */
struct btrfs_io_context {
	refcount_t refs;
	struct btrfs_fs_info *fs_info;
	u64 map_type; /* get from map_lookup->type */
	struct bio *orig_bio;
	atomic_t error;
	u16 max_errors;

	/*
	 * The total number of stripes, including the extra duplicated
	 * stripe for replace.
	 */
	u16 num_stripes;

	/*
	 * The mirror_num of this bioc.
	 *
	 * This is for reads which use 0 as mirror_num, thus we should return a
	 * valid mirror_num (>0) for the reader.
	 */
	u16 mirror_num;

	/*
	 * The following two members are for dev-replace case only.
	 *
	 * @replace_nr_stripes:	Number of duplicated stripes which need to be
	 *			written to replace target.
	 *			Should be <= 2 (2 for DUP, otherwise <= 1).
	 * @replace_stripe_src:	The array indicates where the duplicated stripes
	 *			are from.
	 *
	 * The @replace_stripe_src[] array is mostly for RAID56 cases.
	 * As non-RAID56 stripes share the same contents of the mapped range,
	 * thus no need to bother where the duplicated ones are from.
	 *
	 * But for RAID56 case, all stripes contain different contents, thus
	 * we need a way to know the mapping.
	 *
	 * There is an example for the two members, using a RAID5 write:
	 *
	 *   num_stripes:	4 (3 + 1 duplicated write)
	 *   stripes[0]:	dev = devid 1, physical = X
	 *   stripes[1]:	dev = devid 2, physical = Y
	 *   stripes[2]:	dev = devid 3, physical = Z
	 *   stripes[3]:	dev = devid 0, physical = Y
	 *
	 * replace_nr_stripes = 1
	 * replace_stripe_src = 1	<- Means stripes[1] is involved in replace.
	 *				   The duplicated stripe index would be
	 *				   (@num_stripes - 1).
	 *
	 * Note, that we can still have cases replace_nr_stripes = 2 for DUP.
	 * In that case, all stripes share the same content, thus we don't
	 * need to bother @replace_stripe_src value at all.
	 */
	u16 replace_nr_stripes;
	s16 replace_stripe_src;
	/*
	 * Logical bytenr of the full stripe start, only for RAID56 cases.
	 *
	 * When this value is set to other than (u64)-1, the stripes[] should
	 * follow this pattern:
	 *
	 * (real_stripes = num_stripes - replace_nr_stripes)
	 * (data_stripes = (is_raid6) ? (real_stripes - 2) : (real_stripes - 1))
	 *
	 * stripes[0]:			The first data stripe
	 * stripes[1]:			The second data stripe
	 * ...
	 * stripes[data_stripes - 1]:	The last data stripe
	 * stripes[data_stripes]:	The P stripe
	 * stripes[data_stripes + 1]:	The Q stripe (only for RAID6).
	 */
	u64 full_stripe_logical;
	struct btrfs_io_stripe stripes[];
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
	int num_stripes;
	int sub_stripes;
	int verified_stripes; /* For mount time dev extent verification */
	struct btrfs_io_stripe stripes[];
};

#define map_lookup_size(n) (sizeof(struct map_lookup) + \
			    (sizeof(struct btrfs_io_stripe) * (n)))

struct btrfs_balance_args;
struct btrfs_balance_progress;
struct btrfs_balance_control {
	struct btrfs_balance_args data;
	struct btrfs_balance_args meta;
	struct btrfs_balance_args sys;

	u64 flags;

	struct btrfs_balance_progress stat;
};

/*
 * Search for a given device by the set parameters
 */
struct btrfs_dev_lookup_args {
	u64 devid;
	u8 *uuid;
	u8 *fsid;
	bool missing;
};

/* We have to initialize to -1 because BTRFS_DEV_REPLACE_DEVID is 0 */
#define BTRFS_DEV_LOOKUP_ARGS_INIT { .devid = (u64)-1 }

#define BTRFS_DEV_LOOKUP_ARGS(name) \
	struct btrfs_dev_lookup_args name = BTRFS_DEV_LOOKUP_ARGS_INIT

enum btrfs_map_op {
	BTRFS_MAP_READ,
	BTRFS_MAP_WRITE,
	BTRFS_MAP_GET_READ_MIRRORS,
};

static inline enum btrfs_map_op btrfs_op(struct bio *bio)
{
	switch (bio_op(bio)) {
	case REQ_OP_WRITE:
	case REQ_OP_ZONE_APPEND:
		return BTRFS_MAP_WRITE;
	default:
		WARN_ON_ONCE(1);
		fallthrough;
	case REQ_OP_READ:
		return BTRFS_MAP_READ;
	}
}

static inline unsigned long btrfs_chunk_item_size(int num_stripes)
{
	ASSERT(num_stripes);
	return sizeof(struct btrfs_chunk) +
		sizeof(struct btrfs_stripe) * (num_stripes - 1);
}

/*
 * Do the type safe converstion from stripe_nr to offset inside the chunk.
 *
 * @stripe_nr is u32, with left shift it can overflow u32 for chunks larger
 * than 4G.  This does the proper type cast to avoid overflow.
 */
static inline u64 btrfs_stripe_nr_to_offset(u32 stripe_nr)
{
	return (u64)stripe_nr << BTRFS_STRIPE_LEN_SHIFT;
}

void btrfs_get_bioc(struct btrfs_io_context *bioc);
void btrfs_put_bioc(struct btrfs_io_context *bioc);
int btrfs_map_block(struct btrfs_fs_info *fs_info, enum btrfs_map_op op,
		    u64 logical, u64 *length,
		    struct btrfs_io_context **bioc_ret,
		    struct btrfs_io_stripe *smap, int *mirror_num_ret,
		    int need_raid_map);
int btrfs_map_repair_block(struct btrfs_fs_info *fs_info,
			   struct btrfs_io_stripe *smap, u64 logical,
			   u32 length, int mirror_num);
struct btrfs_discard_stripe *btrfs_map_discard(struct btrfs_fs_info *fs_info,
					       u64 logical, u64 *length_ret,
					       u32 *num_stripes);
int btrfs_read_sys_array(struct btrfs_fs_info *fs_info);
int btrfs_read_chunk_tree(struct btrfs_fs_info *fs_info);
struct btrfs_block_group *btrfs_create_chunk(struct btrfs_trans_handle *trans,
					    u64 type);
void btrfs_mapping_tree_free(struct extent_map_tree *tree);
int btrfs_open_devices(struct btrfs_fs_devices *fs_devices,
		       blk_mode_t flags, void *holder);
struct btrfs_device *btrfs_scan_one_device(const char *path, blk_mode_t flags);
int btrfs_forget_devices(dev_t devt);
void btrfs_close_devices(struct btrfs_fs_devices *fs_devices);
void btrfs_free_extra_devids(struct btrfs_fs_devices *fs_devices);
void btrfs_assign_next_active_device(struct btrfs_device *device,
				     struct btrfs_device *this_dev);
struct btrfs_device *btrfs_find_device_by_devspec(struct btrfs_fs_info *fs_info,
						  u64 devid,
						  const char *devpath);
int btrfs_get_dev_args_from_path(struct btrfs_fs_info *fs_info,
				 struct btrfs_dev_lookup_args *args,
				 const char *path);
struct btrfs_device *btrfs_alloc_device(struct btrfs_fs_info *fs_info,
					const u64 *devid, const u8 *uuid,
					const char *path);
void btrfs_put_dev_args_from_path(struct btrfs_dev_lookup_args *args);
int btrfs_rm_device(struct btrfs_fs_info *fs_info,
		    struct btrfs_dev_lookup_args *args,
		    struct block_device **bdev, void **holder);
void __exit btrfs_cleanup_fs_uuids(void);
int btrfs_num_copies(struct btrfs_fs_info *fs_info, u64 logical, u64 len);
int btrfs_grow_device(struct btrfs_trans_handle *trans,
		      struct btrfs_device *device, u64 new_size);
struct btrfs_device *btrfs_find_device(const struct btrfs_fs_devices *fs_devices,
				       const struct btrfs_dev_lookup_args *args);
int btrfs_shrink_device(struct btrfs_device *device, u64 new_size);
int btrfs_init_new_device(struct btrfs_fs_info *fs_info, const char *path);
int btrfs_balance(struct btrfs_fs_info *fs_info,
		  struct btrfs_balance_control *bctl,
		  struct btrfs_ioctl_balance_args *bargs);
void btrfs_describe_block_groups(u64 flags, char *buf, u32 size_buf);
int btrfs_resume_balance_async(struct btrfs_fs_info *fs_info);
int btrfs_recover_balance(struct btrfs_fs_info *fs_info);
int btrfs_pause_balance(struct btrfs_fs_info *fs_info);
int btrfs_relocate_chunk(struct btrfs_fs_info *fs_info, u64 chunk_offset);
int btrfs_cancel_balance(struct btrfs_fs_info *fs_info);
int btrfs_create_uuid_tree(struct btrfs_fs_info *fs_info);
int btrfs_uuid_scan_kthread(void *data);
bool btrfs_chunk_writeable(struct btrfs_fs_info *fs_info, u64 chunk_offset);
int find_free_dev_extent(struct btrfs_device *device, u64 num_bytes,
			 u64 *start, u64 *max_avail);
void btrfs_dev_stat_inc_and_print(struct btrfs_device *dev, int index);
int btrfs_get_dev_stats(struct btrfs_fs_info *fs_info,
			struct btrfs_ioctl_get_dev_stats *stats);
int btrfs_init_devices_late(struct btrfs_fs_info *fs_info);
int btrfs_init_dev_stats(struct btrfs_fs_info *fs_info);
int btrfs_run_dev_stats(struct btrfs_trans_handle *trans);
void btrfs_rm_dev_replace_remove_srcdev(struct btrfs_device *srcdev);
void btrfs_rm_dev_replace_free_srcdev(struct btrfs_device *srcdev);
void btrfs_destroy_dev_replace_tgtdev(struct btrfs_device *tgtdev);
int btrfs_is_parity_mirror(struct btrfs_fs_info *fs_info,
			   u64 logical, u64 len);
unsigned long btrfs_full_stripe_len(struct btrfs_fs_info *fs_info,
				    u64 logical);
u64 btrfs_calc_stripe_length(const struct extent_map *em);
int btrfs_nr_parity_stripes(u64 type);
int btrfs_chunk_alloc_add_chunk_item(struct btrfs_trans_handle *trans,
				     struct btrfs_block_group *bg);
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

static inline const char *btrfs_dev_name(const struct btrfs_device *device)
{
	if (!device || test_bit(BTRFS_DEV_STATE_MISSING, &device->dev_state))
		return "<missing disk>";
	else
		return rcu_str_deref(device->name);
}

void btrfs_commit_device_sizes(struct btrfs_transaction *trans);

struct list_head * __attribute_const__ btrfs_get_fs_uuids(void);
bool btrfs_check_rw_degradable(struct btrfs_fs_info *fs_info,
					struct btrfs_device *failing_dev);
void btrfs_scratch_superblocks(struct btrfs_fs_info *fs_info,
			       struct block_device *bdev,
			       const char *device_path);

enum btrfs_raid_types __attribute_const__ btrfs_bg_flags_to_raid_index(u64 flags);
int btrfs_bg_type_to_factor(u64 flags);
const char *btrfs_bg_type_to_raid_name(u64 flags);
int btrfs_verify_dev_extents(struct btrfs_fs_info *fs_info);
bool btrfs_repair_one_zone(struct btrfs_fs_info *fs_info, u64 logical);

bool btrfs_pinned_by_swapfile(struct btrfs_fs_info *fs_info, void *ptr);

#endif
