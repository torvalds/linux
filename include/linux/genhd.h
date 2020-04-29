/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_GENHD_H
#define _LINUX_GENHD_H

/*
 * 	genhd.h Copyright (C) 1992 Drew Eckhardt
 *	Generic hard disk header file by  
 * 		Drew Eckhardt
 *
 *		<drew@colorado.edu>
 */

#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/percpu-refcount.h>
#include <linux/uuid.h>
#include <linux/blk_types.h>
#include <asm/local.h>

#ifdef CONFIG_BLOCK

#define dev_to_disk(device)	container_of((device), struct gendisk, part0.__dev)
#define dev_to_part(device)	container_of((device), struct hd_struct, __dev)
#define disk_to_dev(disk)	(&(disk)->part0.__dev)
#define part_to_dev(part)	(&((part)->__dev))

extern struct device_type part_type;
extern struct class block_class;

#define DISK_MAX_PARTS			256
#define DISK_NAME_LEN			32

#include <linux/major.h>
#include <linux/device.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/workqueue.h>

struct disk_stats {
	u64 nsecs[NR_STAT_GROUPS];
	unsigned long sectors[NR_STAT_GROUPS];
	unsigned long ios[NR_STAT_GROUPS];
	unsigned long merges[NR_STAT_GROUPS];
	unsigned long io_ticks;
	local_t in_flight[2];
};

#define PARTITION_META_INFO_VOLNAMELTH	64
/*
 * Enough for the string representation of any kind of UUID plus NULL.
 * EFI UUID is 36 characters. MSDOS UUID is 11 characters.
 */
#define PARTITION_META_INFO_UUIDLTH	(UUID_STRING_LEN + 1)

struct partition_meta_info {
	char uuid[PARTITION_META_INFO_UUIDLTH];
	u8 volname[PARTITION_META_INFO_VOLNAMELTH];
};

struct hd_struct {
	sector_t start_sect;
	/*
	 * nr_sects is protected by sequence counter. One might extend a
	 * partition while IO is happening to it and update of nr_sects
	 * can be non-atomic on 32bit machines with 64bit sector_t.
	 */
	sector_t nr_sects;
	seqcount_t nr_sects_seq;
	sector_t alignment_offset;
	unsigned int discard_alignment;
	struct device __dev;
	struct kobject *holder_dir;
	int policy, partno;
	struct partition_meta_info *info;
#ifdef CONFIG_FAIL_MAKE_REQUEST
	int make_it_fail;
#endif
	unsigned long stamp;
#ifdef	CONFIG_SMP
	struct disk_stats __percpu *dkstats;
#else
	struct disk_stats dkstats;
#endif
	struct percpu_ref ref;
	struct rcu_work rcu_work;
};

/**
 * DOC: genhd capability flags
 *
 * ``GENHD_FL_REMOVABLE`` (0x0001): indicates that the block device
 * gives access to removable media.
 * When set, the device remains present even when media is not
 * inserted.
 * Must not be set for devices which are removed entirely when the
 * media is removed.
 *
 * ``GENHD_FL_CD`` (0x0008): the block device is a CD-ROM-style
 * device.
 * Affects responses to the ``CDROM_GET_CAPABILITY`` ioctl.
 *
 * ``GENHD_FL_UP`` (0x0010): indicates that the block device is "up",
 * with a similar meaning to network interfaces.
 *
 * ``GENHD_FL_SUPPRESS_PARTITION_INFO`` (0x0020): don't include
 * partition information in ``/proc/partitions`` or in the output of
 * printk_all_partitions().
 * Used for the null block device and some MMC devices.
 *
 * ``GENHD_FL_EXT_DEVT`` (0x0040): the driver supports extended
 * dynamic ``dev_t``, i.e. it wants extended device numbers
 * (``BLOCK_EXT_MAJOR``).
 * This affects the maximum number of partitions.
 *
 * ``GENHD_FL_NATIVE_CAPACITY`` (0x0080): based on information in the
 * partition table, the device's capacity has been extended to its
 * native capacity; i.e. the device has hidden capacity used by one
 * of the partitions (this is a flag used so that native capacity is
 * only ever unlocked once).
 *
 * ``GENHD_FL_BLOCK_EVENTS_ON_EXCL_WRITE`` (0x0100): event polling is
 * blocked whenever a writer holds an exclusive lock.
 *
 * ``GENHD_FL_NO_PART_SCAN`` (0x0200): partition scanning is disabled.
 * Used for loop devices in their default settings and some MMC
 * devices.
 *
 * ``GENHD_FL_HIDDEN`` (0x0400): the block device is hidden; it
 * doesn't produce events, doesn't appear in sysfs, and doesn't have
 * an associated ``bdev``.
 * Implies ``GENHD_FL_SUPPRESS_PARTITION_INFO`` and
 * ``GENHD_FL_NO_PART_SCAN``.
 * Used for multipath devices.
 */
#define GENHD_FL_REMOVABLE			0x0001
/* 2 is unused (used to be GENHD_FL_DRIVERFS) */
/* 4 is unused (used to be GENHD_FL_MEDIA_CHANGE_NOTIFY) */
#define GENHD_FL_CD				0x0008
#define GENHD_FL_UP				0x0010
#define GENHD_FL_SUPPRESS_PARTITION_INFO	0x0020
#define GENHD_FL_EXT_DEVT			0x0040
#define GENHD_FL_NATIVE_CAPACITY		0x0080
#define GENHD_FL_BLOCK_EVENTS_ON_EXCL_WRITE	0x0100
#define GENHD_FL_NO_PART_SCAN			0x0200
#define GENHD_FL_HIDDEN				0x0400

enum {
	DISK_EVENT_MEDIA_CHANGE			= 1 << 0, /* media changed */
	DISK_EVENT_EJECT_REQUEST		= 1 << 1, /* eject requested */
};

enum {
	/* Poll even if events_poll_msecs is unset */
	DISK_EVENT_FLAG_POLL			= 1 << 0,
	/* Forward events to udev */
	DISK_EVENT_FLAG_UEVENT			= 1 << 1,
};

struct disk_part_tbl {
	struct rcu_head rcu_head;
	int len;
	struct hd_struct __rcu *last_lookup;
	struct hd_struct __rcu *part[];
};

struct disk_events;
struct badblocks;

#if defined(CONFIG_BLK_DEV_INTEGRITY)

struct blk_integrity {
	const struct blk_integrity_profile	*profile;
	unsigned char				flags;
	unsigned char				tuple_size;
	unsigned char				interval_exp;
	unsigned char				tag_size;
};

#endif	/* CONFIG_BLK_DEV_INTEGRITY */

struct gendisk {
	/* major, first_minor and minors are input parameters only,
	 * don't use directly.  Use disk_devt() and disk_max_parts().
	 */
	int major;			/* major number of driver */
	int first_minor;
	int minors;                     /* maximum number of minors, =1 for
                                         * disks that can't be partitioned. */

	char disk_name[DISK_NAME_LEN];	/* name of major driver */

	unsigned short events;		/* supported events */
	unsigned short event_flags;	/* flags related to event processing */

	/* Array of pointers to partitions indexed by partno.
	 * Protected with matching bdev lock but stat and other
	 * non-critical accesses use RCU.  Always access through
	 * helpers.
	 */
	struct disk_part_tbl __rcu *part_tbl;
	struct hd_struct part0;

	const struct block_device_operations *fops;
	struct request_queue *queue;
	void *private_data;

	int flags;
	struct rw_semaphore lookup_sem;
	struct kobject *slave_dir;

	struct timer_rand_state *random;
	atomic_t sync_io;		/* RAID */
	struct disk_events *ev;
#ifdef  CONFIG_BLK_DEV_INTEGRITY
	struct kobject integrity_kobj;
#endif	/* CONFIG_BLK_DEV_INTEGRITY */
	int node_id;
	struct badblocks *bb;
	struct lockdep_map lockdep_map;
};

static inline struct gendisk *part_to_disk(struct hd_struct *part)
{
	if (likely(part)) {
		if (part->partno)
			return dev_to_disk(part_to_dev(part)->parent);
		else
			return dev_to_disk(part_to_dev(part));
	}
	return NULL;
}

static inline int disk_max_parts(struct gendisk *disk)
{
	if (disk->flags & GENHD_FL_EXT_DEVT)
		return DISK_MAX_PARTS;
	return disk->minors;
}

static inline bool disk_part_scan_enabled(struct gendisk *disk)
{
	return disk_max_parts(disk) > 1 &&
		!(disk->flags & GENHD_FL_NO_PART_SCAN);
}

static inline dev_t disk_devt(struct gendisk *disk)
{
	return MKDEV(disk->major, disk->first_minor);
}

static inline dev_t part_devt(struct hd_struct *part)
{
	return part_to_dev(part)->devt;
}

extern struct hd_struct *__disk_get_part(struct gendisk *disk, int partno);
extern struct hd_struct *disk_get_part(struct gendisk *disk, int partno);

static inline void disk_put_part(struct hd_struct *part)
{
	if (likely(part))
		put_device(part_to_dev(part));
}

/*
 * Smarter partition iterator without context limits.
 */
#define DISK_PITER_REVERSE	(1 << 0) /* iterate in the reverse direction */
#define DISK_PITER_INCL_EMPTY	(1 << 1) /* include 0-sized parts */
#define DISK_PITER_INCL_PART0	(1 << 2) /* include partition 0 */
#define DISK_PITER_INCL_EMPTY_PART0 (1 << 3) /* include empty partition 0 */

struct disk_part_iter {
	struct gendisk		*disk;
	struct hd_struct	*part;
	int			idx;
	unsigned int		flags;
};

extern void disk_part_iter_init(struct disk_part_iter *piter,
				 struct gendisk *disk, unsigned int flags);
extern struct hd_struct *disk_part_iter_next(struct disk_part_iter *piter);
extern void disk_part_iter_exit(struct disk_part_iter *piter);
extern bool disk_has_partitions(struct gendisk *disk);

/* block/genhd.c */
extern void device_add_disk(struct device *parent, struct gendisk *disk,
			    const struct attribute_group **groups);
static inline void add_disk(struct gendisk *disk)
{
	device_add_disk(NULL, disk, NULL);
}
extern void device_add_disk_no_queue_reg(struct device *parent, struct gendisk *disk);
static inline void add_disk_no_queue_reg(struct gendisk *disk)
{
	device_add_disk_no_queue_reg(NULL, disk);
}

extern void del_gendisk(struct gendisk *gp);
extern struct gendisk *get_gendisk(dev_t dev, int *partno);
extern struct block_device *bdget_disk(struct gendisk *disk, int partno);

extern void set_device_ro(struct block_device *bdev, int flag);
extern void set_disk_ro(struct gendisk *disk, int flag);

static inline int get_disk_ro(struct gendisk *disk)
{
	return disk->part0.policy;
}

extern void disk_block_events(struct gendisk *disk);
extern void disk_unblock_events(struct gendisk *disk);
extern void disk_flush_events(struct gendisk *disk, unsigned int mask);
extern void set_capacity_revalidate_and_notify(struct gendisk *disk,
			sector_t size, bool revalidate);
extern unsigned int disk_clear_events(struct gendisk *disk, unsigned int mask);

/* drivers/char/random.c */
extern void add_disk_randomness(struct gendisk *disk) __latent_entropy;
extern void rand_initialize_disk(struct gendisk *disk);

static inline sector_t get_start_sect(struct block_device *bdev)
{
	return bdev->bd_part->start_sect;
}
static inline sector_t get_capacity(struct gendisk *disk)
{
	return disk->part0.nr_sects;
}
static inline void set_capacity(struct gendisk *disk, sector_t size)
{
	disk->part0.nr_sects = size;
}

extern dev_t blk_lookup_devt(const char *name, int partno);

int bdev_disk_changed(struct block_device *bdev, bool invalidate);
int blk_add_partitions(struct gendisk *disk, struct block_device *bdev);
int blk_drop_partitions(struct gendisk *disk, struct block_device *bdev);
extern void printk_all_partitions(void);

extern struct gendisk *__alloc_disk_node(int minors, int node_id);
extern struct kobject *get_disk_and_module(struct gendisk *disk);
extern void put_disk(struct gendisk *disk);
extern void put_disk_and_module(struct gendisk *disk);
extern void blk_register_region(dev_t devt, unsigned long range,
			struct module *module,
			struct kobject *(*probe)(dev_t, int *, void *),
			int (*lock)(dev_t, void *),
			void *data);
extern void blk_unregister_region(dev_t devt, unsigned long range);

#define alloc_disk_node(minors, node_id)				\
({									\
	static struct lock_class_key __key;				\
	const char *__name;						\
	struct gendisk *__disk;						\
									\
	__name = "(gendisk_completion)"#minors"("#node_id")";		\
									\
	__disk = __alloc_disk_node(minors, node_id);			\
									\
	if (__disk)							\
		lockdep_init_map(&__disk->lockdep_map, __name, &__key, 0); \
									\
	__disk;								\
})

#define alloc_disk(minors) alloc_disk_node(minors, NUMA_NO_NODE)

#else /* CONFIG_BLOCK */

static inline void printk_all_partitions(void) { }

static inline dev_t blk_lookup_devt(const char *name, int partno)
{
	dev_t devt = MKDEV(0, 0);
	return devt;
}
#endif /* CONFIG_BLOCK */

#endif /* _LINUX_GENHD_H */
