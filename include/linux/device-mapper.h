/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2001 Sistina Software (UK) Limited.
 * Copyright (C) 2004-2008 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the LGPL.
 */

#ifndef _LINUX_DEVICE_MAPPER_H
#define _LINUX_DEVICE_MAPPER_H

#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/dm-ioctl.h>
#include <linux/math64.h>
#include <linux/ratelimit.h>

struct dm_dev;
struct dm_target;
struct dm_table;
struct dm_report_zones_args;
struct mapped_device;
struct bio_vec;
enum dax_access_mode;

/*
 * Type of table, mapped_device's mempool and request_queue
 */
enum dm_queue_mode {
	DM_TYPE_NONE		 = 0,
	DM_TYPE_BIO_BASED	 = 1,
	DM_TYPE_REQUEST_BASED	 = 2,
	DM_TYPE_DAX_BIO_BASED	 = 3,
};

typedef enum { STATUSTYPE_INFO, STATUSTYPE_TABLE, STATUSTYPE_IMA } status_type_t;

union map_info {
	void *ptr;
};

/*
 * In the constructor the target parameter will already have the
 * table, type, begin and len fields filled in.
 */
typedef int (*dm_ctr_fn) (struct dm_target *target,
			  unsigned int argc, char **argv);

/*
 * The destructor doesn't need to free the dm_target, just
 * anything hidden ti->private.
 */
typedef void (*dm_dtr_fn) (struct dm_target *ti);

/*
 * The map function must return:
 * < 0: error
 * = 0: The target will handle the io by resubmitting it later
 * = 1: simple remap complete
 * = 2: The target wants to push back the io
 */
typedef int (*dm_map_fn) (struct dm_target *ti, struct bio *bio);
typedef int (*dm_clone_and_map_request_fn) (struct dm_target *ti,
					    struct request *rq,
					    union map_info *map_context,
					    struct request **clone);
typedef void (*dm_release_clone_request_fn) (struct request *clone,
					     union map_info *map_context);

/*
 * Returns:
 * < 0 : error (currently ignored)
 * 0   : ended successfully
 * 1   : for some reason the io has still not completed (eg,
 *       multipath target might want to requeue a failed io).
 * 2   : The target wants to push back the io
 */
typedef int (*dm_endio_fn) (struct dm_target *ti,
			    struct bio *bio, blk_status_t *error);
typedef int (*dm_request_endio_fn) (struct dm_target *ti,
				    struct request *clone, blk_status_t error,
				    union map_info *map_context);

typedef void (*dm_presuspend_fn) (struct dm_target *ti);
typedef void (*dm_presuspend_undo_fn) (struct dm_target *ti);
typedef void (*dm_postsuspend_fn) (struct dm_target *ti);
typedef int (*dm_preresume_fn) (struct dm_target *ti);
typedef void (*dm_resume_fn) (struct dm_target *ti);

typedef void (*dm_status_fn) (struct dm_target *ti, status_type_t status_type,
			      unsigned int status_flags, char *result, unsigned int maxlen);

typedef int (*dm_message_fn) (struct dm_target *ti, unsigned int argc, char **argv,
			      char *result, unsigned int maxlen);

typedef int (*dm_prepare_ioctl_fn) (struct dm_target *ti, struct block_device **bdev);

#ifdef CONFIG_BLK_DEV_ZONED
typedef int (*dm_report_zones_fn) (struct dm_target *ti,
				   struct dm_report_zones_args *args,
				   unsigned int nr_zones);
#else
/*
 * Define dm_report_zones_fn so that targets can assign to NULL if
 * CONFIG_BLK_DEV_ZONED disabled. Otherwise each target needs to do
 * awkward #ifdefs in their target_type, etc.
 */
typedef int (*dm_report_zones_fn) (struct dm_target *dummy);
#endif

/*
 * These iteration functions are typically used to check (and combine)
 * properties of underlying devices.
 * E.g. Does at least one underlying device support flush?
 *      Does any underlying device not support WRITE_SAME?
 *
 * The callout function is called once for each contiguous section of
 * an underlying device.  State can be maintained in *data.
 * Return non-zero to stop iterating through any further devices.
 */
typedef int (*iterate_devices_callout_fn) (struct dm_target *ti,
					   struct dm_dev *dev,
					   sector_t start, sector_t len,
					   void *data);

/*
 * This function must iterate through each section of device used by the
 * target until it encounters a non-zero return code, which it then returns.
 * Returns zero if no callout returned non-zero.
 */
typedef int (*dm_iterate_devices_fn) (struct dm_target *ti,
				      iterate_devices_callout_fn fn,
				      void *data);

typedef void (*dm_io_hints_fn) (struct dm_target *ti,
				struct queue_limits *limits);

/*
 * Returns:
 *    0: The target can handle the next I/O immediately.
 *    1: The target can't handle the next I/O immediately.
 */
typedef int (*dm_busy_fn) (struct dm_target *ti);

/*
 * Returns:
 *  < 0 : error
 * >= 0 : the number of bytes accessible at the address
 */
typedef long (*dm_dax_direct_access_fn) (struct dm_target *ti, pgoff_t pgoff,
		long nr_pages, enum dax_access_mode node, void **kaddr,
		pfn_t *pfn);
typedef int (*dm_dax_zero_page_range_fn)(struct dm_target *ti, pgoff_t pgoff,
		size_t nr_pages);

/*
 * Returns:
 * != 0 : number of bytes transferred
 * 0    : recovery write failed
 */
typedef size_t (*dm_dax_recovery_write_fn)(struct dm_target *ti, pgoff_t pgoff,
		void *addr, size_t bytes, struct iov_iter *i);

void dm_error(const char *message);

struct dm_dev {
	struct block_device *bdev;
	struct file *bdev_file;
	struct dax_device *dax_dev;
	blk_mode_t mode;
	char name[16];
};

/*
 * Constructors should call these functions to ensure destination devices
 * are opened/closed correctly.
 */
int dm_get_device(struct dm_target *ti, const char *path, blk_mode_t mode,
		  struct dm_dev **result);
void dm_put_device(struct dm_target *ti, struct dm_dev *d);

/*
 * Helper function for getting devices
 */
int dm_devt_from_path(const char *path, dev_t *dev_p);

/*
 * Information about a target type
 */

struct target_type {
	uint64_t features;
	const char *name;
	struct module *module;
	unsigned int version[3];
	dm_ctr_fn ctr;
	dm_dtr_fn dtr;
	dm_map_fn map;
	dm_clone_and_map_request_fn clone_and_map_rq;
	dm_release_clone_request_fn release_clone_rq;
	dm_endio_fn end_io;
	dm_request_endio_fn rq_end_io;
	dm_presuspend_fn presuspend;
	dm_presuspend_undo_fn presuspend_undo;
	dm_postsuspend_fn postsuspend;
	dm_preresume_fn preresume;
	dm_resume_fn resume;
	dm_status_fn status;
	dm_message_fn message;
	dm_prepare_ioctl_fn prepare_ioctl;
	dm_report_zones_fn report_zones;
	dm_busy_fn busy;
	dm_iterate_devices_fn iterate_devices;
	dm_io_hints_fn io_hints;
	dm_dax_direct_access_fn direct_access;
	dm_dax_zero_page_range_fn dax_zero_page_range;
	dm_dax_recovery_write_fn dax_recovery_write;

	/* For internal device-mapper use. */
	struct list_head list;
};

/*
 * Target features
 */

/*
 * Any table that contains an instance of this target must have only one.
 */
#define DM_TARGET_SINGLETON		0x00000001
#define dm_target_needs_singleton(type)	((type)->features & DM_TARGET_SINGLETON)

/*
 * Indicates that a target does not support read-only devices.
 */
#define DM_TARGET_ALWAYS_WRITEABLE	0x00000002
#define dm_target_always_writeable(type) \
		((type)->features & DM_TARGET_ALWAYS_WRITEABLE)

/*
 * Any device that contains a table with an instance of this target may never
 * have tables containing any different target type.
 */
#define DM_TARGET_IMMUTABLE		0x00000004
#define dm_target_is_immutable(type)	((type)->features & DM_TARGET_IMMUTABLE)

/*
 * Indicates that a target may replace any target; even immutable targets.
 * .map, .map_rq, .clone_and_map_rq and .release_clone_rq are all defined.
 */
#define DM_TARGET_WILDCARD		0x00000008
#define dm_target_is_wildcard(type)	((type)->features & DM_TARGET_WILDCARD)

/*
 * A target implements own bio data integrity.
 */
#define DM_TARGET_INTEGRITY		0x00000010
#define dm_target_has_integrity(type)	((type)->features & DM_TARGET_INTEGRITY)

/*
 * A target passes integrity data to the lower device.
 */
#define DM_TARGET_PASSES_INTEGRITY	0x00000020
#define dm_target_passes_integrity(type) ((type)->features & DM_TARGET_PASSES_INTEGRITY)

/*
 * Indicates support for zoned block devices:
 * - DM_TARGET_ZONED_HM: the target also supports host-managed zoned
 *   block devices but does not support combining different zoned models.
 * - DM_TARGET_MIXED_ZONED_MODEL: the target supports combining multiple
 *   devices with different zoned models.
 */
#ifdef CONFIG_BLK_DEV_ZONED
#define DM_TARGET_ZONED_HM		0x00000040
#define dm_target_supports_zoned_hm(type) ((type)->features & DM_TARGET_ZONED_HM)
#else
#define DM_TARGET_ZONED_HM		0x00000000
#define dm_target_supports_zoned_hm(type) (false)
#endif

/*
 * A target handles REQ_NOWAIT
 */
#define DM_TARGET_NOWAIT		0x00000080
#define dm_target_supports_nowait(type) ((type)->features & DM_TARGET_NOWAIT)

/*
 * A target supports passing through inline crypto support.
 */
#define DM_TARGET_PASSES_CRYPTO		0x00000100
#define dm_target_passes_crypto(type) ((type)->features & DM_TARGET_PASSES_CRYPTO)

#ifdef CONFIG_BLK_DEV_ZONED
#define DM_TARGET_MIXED_ZONED_MODEL	0x00000200
#define dm_target_supports_mixed_zoned_model(type) \
	((type)->features & DM_TARGET_MIXED_ZONED_MODEL)
#else
#define DM_TARGET_MIXED_ZONED_MODEL	0x00000000
#define dm_target_supports_mixed_zoned_model(type) (false)
#endif

struct dm_target {
	struct dm_table *table;
	struct target_type *type;

	/* target limits */
	sector_t begin;
	sector_t len;

	/* If non-zero, maximum size of I/O submitted to a target. */
	uint32_t max_io_len;

	/*
	 * A number of zero-length barrier bios that will be submitted
	 * to the target for the purpose of flushing cache.
	 *
	 * The bio number can be accessed with dm_bio_get_target_bio_nr.
	 * It is a responsibility of the target driver to remap these bios
	 * to the real underlying devices.
	 */
	unsigned int num_flush_bios;

	/*
	 * The number of discard bios that will be submitted to the target.
	 * The bio number can be accessed with dm_bio_get_target_bio_nr.
	 */
	unsigned int num_discard_bios;

	/*
	 * The number of secure erase bios that will be submitted to the target.
	 * The bio number can be accessed with dm_bio_get_target_bio_nr.
	 */
	unsigned int num_secure_erase_bios;

	/*
	 * The number of WRITE ZEROES bios that will be submitted to the target.
	 * The bio number can be accessed with dm_bio_get_target_bio_nr.
	 */
	unsigned int num_write_zeroes_bios;

	/*
	 * The minimum number of extra bytes allocated in each io for the
	 * target to use.
	 */
	unsigned int per_io_data_size;

	/* target specific data */
	void *private;

	/* Used to provide an error string from the ctr */
	char *error;

	/*
	 * Set if this target needs to receive flushes regardless of
	 * whether or not its underlying devices have support.
	 */
	bool flush_supported:1;

	/*
	 * Set if this target needs to receive discards regardless of
	 * whether or not its underlying devices have support.
	 */
	bool discards_supported:1;

	/*
	 * Automatically set by dm-core if this target supports
	 * REQ_OP_ZONE_RESET_ALL. Otherwise, this operation will be emulated
	 * using REQ_OP_ZONE_RESET. Target drivers must not set this manually.
	 */
	bool zone_reset_all_supported:1;

	/*
	 * Set if this target requires that discards be split on
	 * 'max_discard_sectors' boundaries.
	 */
	bool max_discard_granularity:1;

	/*
	 * Set if we need to limit the number of in-flight bios when swapping.
	 */
	bool limit_swap_bios:1;

	/*
	 * Set if this target implements a zoned device and needs emulation of
	 * zone append operations using regular writes.
	 */
	bool emulate_zone_append:1;

	/*
	 * Set if the target will submit IO using dm_submit_bio_remap()
	 * after returning DM_MAPIO_SUBMITTED from its map function.
	 */
	bool accounts_remapped_io:1;

	/*
	 * Set if the target will submit the DM bio without first calling
	 * bio_set_dev(). NOTE: ideally a target should _not_ need this.
	 */
	bool needs_bio_set_dev:1;

	/*
	 * Set if the target supports flush optimization. If all the targets in
	 * a table have flush_bypasses_map set, the dm core will not send
	 * flushes to the targets via a ->map method. It will iterate over
	 * dm_table->devices and send flushes to the devices directly. This
	 * optimization reduces the number of flushes being sent when multiple
	 * targets in a table use the same underlying device.
	 *
	 * This optimization may be enabled on targets that just pass the
	 * flushes to the underlying devices without performing any other
	 * actions on the flush request. Currently, dm-linear and dm-stripe
	 * support it.
	 */
	bool flush_bypasses_map:1;

	/*
	 * Set if the target calls bio_integrity_alloc on bios received
	 * in the map method.
	 */
	bool mempool_needs_integrity:1;
};

void *dm_per_bio_data(struct bio *bio, size_t data_size);
struct bio *dm_bio_from_per_bio_data(void *data, size_t data_size);
unsigned int dm_bio_get_target_bio_nr(const struct bio *bio);

u64 dm_start_time_ns_from_clone(struct bio *bio);

int dm_register_target(struct target_type *t);
void dm_unregister_target(struct target_type *t);

/*
 * Target argument parsing.
 */
struct dm_arg_set {
	unsigned int argc;
	char **argv;
};

/*
 * The minimum and maximum value of a numeric argument, together with
 * the error message to use if the number is found to be outside that range.
 */
struct dm_arg {
	unsigned int min;
	unsigned int max;
	char *error;
};

/*
 * Validate the next argument, either returning it as *value or, if invalid,
 * returning -EINVAL and setting *error.
 */
int dm_read_arg(const struct dm_arg *arg, struct dm_arg_set *arg_set,
		unsigned int *value, char **error);

/*
 * Process the next argument as the start of a group containing between
 * arg->min and arg->max further arguments. Either return the size as
 * *num_args or, if invalid, return -EINVAL and set *error.
 */
int dm_read_arg_group(const struct dm_arg *arg, struct dm_arg_set *arg_set,
		      unsigned int *num_args, char **error);

/*
 * Return the current argument and shift to the next.
 */
const char *dm_shift_arg(struct dm_arg_set *as);

/*
 * Move through num_args arguments.
 */
void dm_consume_args(struct dm_arg_set *as, unsigned int num_args);

/*
 *----------------------------------------------------------------
 * Functions for creating and manipulating mapped devices.
 * Drop the reference with dm_put when you finish with the object.
 *----------------------------------------------------------------
 */

/*
 * DM_ANY_MINOR chooses the next available minor number.
 */
#define DM_ANY_MINOR (-1)
int dm_create(int minor, struct mapped_device **md);

/*
 * Reference counting for md.
 */
struct mapped_device *dm_get_md(dev_t dev);
void dm_get(struct mapped_device *md);
int dm_hold(struct mapped_device *md);
void dm_put(struct mapped_device *md);

/*
 * An arbitrary pointer may be stored alongside a mapped device.
 */
void dm_set_mdptr(struct mapped_device *md, void *ptr);
void *dm_get_mdptr(struct mapped_device *md);

/*
 * A device can still be used while suspended, but I/O is deferred.
 */
int dm_suspend(struct mapped_device *md, unsigned int suspend_flags);
int dm_resume(struct mapped_device *md);

/*
 * Event functions.
 */
uint32_t dm_get_event_nr(struct mapped_device *md);
int dm_wait_event(struct mapped_device *md, int event_nr);
uint32_t dm_next_uevent_seq(struct mapped_device *md);
void dm_uevent_add(struct mapped_device *md, struct list_head *elist);

/*
 * Info functions.
 */
const char *dm_device_name(struct mapped_device *md);
int dm_copy_name_and_uuid(struct mapped_device *md, char *name, char *uuid);
struct gendisk *dm_disk(struct mapped_device *md);
int dm_suspended(struct dm_target *ti);
int dm_post_suspending(struct dm_target *ti);
int dm_noflush_suspending(struct dm_target *ti);
void dm_accept_partial_bio(struct bio *bio, unsigned int n_sectors);
void dm_submit_bio_remap(struct bio *clone, struct bio *tgt_clone);
union map_info *dm_get_rq_mapinfo(struct request *rq);

#ifdef CONFIG_BLK_DEV_ZONED
struct dm_report_zones_args {
	struct dm_target *tgt;
	sector_t next_sector;

	void *orig_data;
	report_zones_cb orig_cb;
	unsigned int zone_idx;

	/* must be filled by ->report_zones before calling dm_report_zones_cb */
	sector_t start;
};
int dm_report_zones(struct block_device *bdev, sector_t start, sector_t sector,
		    struct dm_report_zones_args *args, unsigned int nr_zones);
#endif /* CONFIG_BLK_DEV_ZONED */

/*
 * Device mapper functions to parse and create devices specified by the
 * parameter "dm-mod.create="
 */
int __init dm_early_create(struct dm_ioctl *dmi,
			   struct dm_target_spec **spec_array,
			   char **target_params_array);

/*
 * Geometry functions.
 */
int dm_get_geometry(struct mapped_device *md, struct hd_geometry *geo);
int dm_set_geometry(struct mapped_device *md, struct hd_geometry *geo);

/*
 *---------------------------------------------------------------
 * Functions for manipulating device-mapper tables.
 *---------------------------------------------------------------
 */

/*
 * First create an empty table.
 */
int dm_table_create(struct dm_table **result, blk_mode_t mode,
		    unsigned int num_targets, struct mapped_device *md);

/*
 * Then call this once for each target.
 */
int dm_table_add_target(struct dm_table *t, const char *type,
			sector_t start, sector_t len, char *params);

/*
 * Target can use this to set the table's type.
 * Can only ever be called from a target's ctr.
 * Useful for "hybrid" target (supports both bio-based
 * and request-based).
 */
void dm_table_set_type(struct dm_table *t, enum dm_queue_mode type);

/*
 * Finally call this to make the table ready for use.
 */
int dm_table_complete(struct dm_table *t);

/*
 * Destroy the table when finished.
 */
void dm_table_destroy(struct dm_table *t);

/*
 * Target may require that it is never sent I/O larger than len.
 */
int __must_check dm_set_target_max_io_len(struct dm_target *ti, sector_t len);

/*
 * Table reference counting.
 */
struct dm_table *dm_get_live_table(struct mapped_device *md, int *srcu_idx);
void dm_put_live_table(struct mapped_device *md, int srcu_idx);
void dm_sync_table(struct mapped_device *md);

/*
 * Queries
 */
sector_t dm_table_get_size(struct dm_table *t);
blk_mode_t dm_table_get_mode(struct dm_table *t);
struct mapped_device *dm_table_get_md(struct dm_table *t);
const char *dm_table_device_name(struct dm_table *t);

/*
 * Trigger an event.
 */
void dm_table_event(struct dm_table *t);

/*
 * Run the queue for request-based targets.
 */
void dm_table_run_md_queue_async(struct dm_table *t);

/*
 * The device must be suspended before calling this method.
 * Returns the previous table, which the caller must destroy.
 */
struct dm_table *dm_swap_table(struct mapped_device *md,
			       struct dm_table *t);

/*
 * Table blk_crypto_profile functions
 */
void dm_destroy_crypto_profile(struct blk_crypto_profile *profile);

/*
 *---------------------------------------------------------------
 * Macros.
 *---------------------------------------------------------------
 */
#define DM_NAME "device-mapper"

#define DM_FMT(fmt) DM_NAME ": " DM_MSG_PREFIX ": " fmt "\n"

#define DMCRIT(fmt, ...) pr_crit(DM_FMT(fmt), ##__VA_ARGS__)

#define DMERR(fmt, ...) pr_err(DM_FMT(fmt), ##__VA_ARGS__)
#define DMERR_LIMIT(fmt, ...) pr_err_ratelimited(DM_FMT(fmt), ##__VA_ARGS__)
#define DMWARN(fmt, ...) pr_warn(DM_FMT(fmt), ##__VA_ARGS__)
#define DMWARN_LIMIT(fmt, ...) pr_warn_ratelimited(DM_FMT(fmt), ##__VA_ARGS__)
#define DMINFO(fmt, ...) pr_info(DM_FMT(fmt), ##__VA_ARGS__)
#define DMINFO_LIMIT(fmt, ...) pr_info_ratelimited(DM_FMT(fmt), ##__VA_ARGS__)

#define DMDEBUG(fmt, ...) pr_debug(DM_FMT(fmt), ##__VA_ARGS__)
#define DMDEBUG_LIMIT(fmt, ...) pr_debug_ratelimited(DM_FMT(fmt), ##__VA_ARGS__)

#define DMEMIT(x...) (sz += ((sz >= maxlen) ? 0 : scnprintf(result + sz, maxlen - sz, x)))

#define DMEMIT_TARGET_NAME_VERSION(y) \
		DMEMIT("target_name=%s,target_version=%u.%u.%u", \
		       (y)->name, (y)->version[0], (y)->version[1], (y)->version[2])

/**
 * module_dm() - Helper macro for DM targets that don't do anything
 * special in their module_init and module_exit.
 * Each module may only use this macro once, and calling it replaces
 * module_init() and module_exit().
 *
 * @name: DM target's name
 */
#define module_dm(name) \
static int __init dm_##name##_init(void) \
{ \
	return dm_register_target(&(name##_target)); \
} \
module_init(dm_##name##_init) \
static void __exit dm_##name##_exit(void) \
{ \
	dm_unregister_target(&(name##_target)); \
} \
module_exit(dm_##name##_exit)

/*
 * Definitions of return values from target end_io function.
 */
#define DM_ENDIO_DONE		0
#define DM_ENDIO_INCOMPLETE	1
#define DM_ENDIO_REQUEUE	2
#define DM_ENDIO_DELAY_REQUEUE	3

/*
 * Definitions of return values from target map function.
 */
#define DM_MAPIO_SUBMITTED	0
#define DM_MAPIO_REMAPPED	1
#define DM_MAPIO_REQUEUE	DM_ENDIO_REQUEUE
#define DM_MAPIO_DELAY_REQUEUE	DM_ENDIO_DELAY_REQUEUE
#define DM_MAPIO_KILL		4

#define dm_sector_div64(x, y)( \
{ \
	u64 _res; \
	(x) = div64_u64_rem(x, y, &_res); \
	_res; \
} \
)

/*
 * Ceiling(n / sz)
 */
#define dm_div_up(n, sz) (((n) + (sz) - 1) / (sz))

#define dm_sector_div_up(n, sz) ( \
{ \
	sector_t _r = ((n) + (sz) - 1); \
	sector_div(_r, (sz)); \
	_r; \
} \
)

/*
 * ceiling(n / size) * size
 */
#define dm_round_up(n, sz) (dm_div_up((n), (sz)) * (sz))

/*
 * Sector offset taken relative to the start of the target instead of
 * relative to the start of the device.
 */
#define dm_target_offset(ti, sector) ((sector) - (ti)->begin)

static inline sector_t to_sector(unsigned long long n)
{
	return (n >> SECTOR_SHIFT);
}

static inline unsigned long to_bytes(sector_t n)
{
	return (n << SECTOR_SHIFT);
}

#endif	/* _LINUX_DEVICE_MAPPER_H */
