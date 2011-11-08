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
#include <linux/ratelimit.h>

struct dm_dev;
struct dm_target;
struct dm_table;
struct mapped_device;
struct bio_vec;

typedef enum { STATUSTYPE_INFO, STATUSTYPE_TABLE } status_type_t;

union map_info {
	void *ptr;
	unsigned long long ll;
	unsigned target_request_nr;
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
typedef int (*dm_map_fn) (struct dm_target *ti, struct bio *bio,
			  union map_info *map_context);
typedef int (*dm_map_request_fn) (struct dm_target *ti, struct request *clone,
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
			    struct bio *bio, int error,
			    union map_info *map_context);
typedef int (*dm_request_endio_fn) (struct dm_target *ti,
				    struct request *clone, int error,
				    union map_info *map_context);

typedef void (*dm_flush_fn) (struct dm_target *ti);
typedef void (*dm_presuspend_fn) (struct dm_target *ti);
typedef void (*dm_postsuspend_fn) (struct dm_target *ti);
typedef int (*dm_preresume_fn) (struct dm_target *ti);
typedef void (*dm_resume_fn) (struct dm_target *ti);

typedef int (*dm_status_fn) (struct dm_target *ti, status_type_t status_type,
			     char *result, unsigned int maxlen);

typedef int (*dm_message_fn) (struct dm_target *ti, unsigned argc, char **argv);

typedef int (*dm_ioctl_fn) (struct dm_target *ti, unsigned int cmd,
			    unsigned long arg);

typedef int (*dm_merge_fn) (struct dm_target *ti, struct bvec_merge_data *bvm,
			    struct bio_vec *biovec, int max_size);

typedef int (*iterate_devices_callout_fn) (struct dm_target *ti,
					   struct dm_dev *dev,
					   sector_t start, sector_t len,
					   void *data);

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

void dm_error(const char *message);

/*
 * Combine device limits.
 */
int dm_set_device_limits(struct dm_target *ti, struct dm_dev *dev,
			 sector_t start, sector_t len, void *data);

struct dm_dev {
	struct block_device *bdev;
	fmode_t mode;
	char name[16];
};

/*
 * Constructors should call these functions to ensure destination devices
 * are opened/closed correctly.
 */
int dm_get_device(struct dm_target *ti, const char *path, fmode_t mode,
						 struct dm_dev **result);
void dm_put_device(struct dm_target *ti, struct dm_dev *d);

/*
 * Information about a target type
 */

struct target_type {
	uint64_t features;
	const char *name;
	struct module *module;
	unsigned version[3];
	dm_ctr_fn ctr;
	dm_dtr_fn dtr;
	dm_map_fn map;
	dm_map_request_fn map_rq;
	dm_endio_fn end_io;
	dm_request_endio_fn rq_end_io;
	dm_flush_fn flush;
	dm_presuspend_fn presuspend;
	dm_postsuspend_fn postsuspend;
	dm_preresume_fn preresume;
	dm_resume_fn resume;
	dm_status_fn status;
	dm_message_fn message;
	dm_ioctl_fn ioctl;
	dm_merge_fn merge;
	dm_busy_fn busy;
	dm_iterate_devices_fn iterate_devices;
	dm_io_hints_fn io_hints;

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

struct dm_target {
	struct dm_table *table;
	struct target_type *type;

	/* target limits */
	sector_t begin;
	sector_t len;

	/* Always a power of 2 */
	sector_t split_io;

	/*
	 * A number of zero-length barrier requests that will be submitted
	 * to the target for the purpose of flushing cache.
	 *
	 * The request number will be placed in union map_info->target_request_nr.
	 * It is a responsibility of the target driver to remap these requests
	 * to the real underlying devices.
	 */
	unsigned num_flush_requests;

	/*
	 * The number of discard requests that will be submitted to the
	 * target.  map_info->request_nr is used just like num_flush_requests.
	 */
	unsigned num_discard_requests;

	/* target specific data */
	void *private;

	/* Used to provide an error string from the ctr */
	char *error;

	/*
	 * Set if this target needs to receive discards regardless of
	 * whether or not its underlying devices have support.
	 */
	unsigned discards_supported:1;

	/*
	 * Set if this target does not return zeroes on discarded blocks.
	 */
	unsigned discard_zeroes_data_unsupported:1;
};

/* Each target can link one of these into the table */
struct dm_target_callbacks {
	struct list_head list;
	int (*congested_fn) (struct dm_target_callbacks *, int);
};

int dm_register_target(struct target_type *t);
void dm_unregister_target(struct target_type *t);

/*
 * Target argument parsing.
 */
struct dm_arg_set {
	unsigned argc;
	char **argv;
};

/*
 * The minimum and maximum value of a numeric argument, together with
 * the error message to use if the number is found to be outside that range.
 */
struct dm_arg {
	unsigned min;
	unsigned max;
	char *error;
};

/*
 * Validate the next argument, either returning it as *value or, if invalid,
 * returning -EINVAL and setting *error.
 */
int dm_read_arg(struct dm_arg *arg, struct dm_arg_set *arg_set,
		unsigned *value, char **error);

/*
 * Process the next argument as the start of a group containing between
 * arg->min and arg->max further arguments. Either return the size as
 * *num_args or, if invalid, return -EINVAL and set *error.
 */
int dm_read_arg_group(struct dm_arg *arg, struct dm_arg_set *arg_set,
		      unsigned *num_args, char **error);

/*
 * Return the current argument and shift to the next.
 */
const char *dm_shift_arg(struct dm_arg_set *as);

/*
 * Move through num_args arguments.
 */
void dm_consume_args(struct dm_arg_set *as, unsigned num_args);

/*-----------------------------------------------------------------
 * Functions for creating and manipulating mapped devices.
 * Drop the reference with dm_put when you finish with the object.
 *---------------------------------------------------------------*/

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
void dm_put(struct mapped_device *md);

/*
 * An arbitrary pointer may be stored alongside a mapped device.
 */
void dm_set_mdptr(struct mapped_device *md, void *ptr);
void *dm_get_mdptr(struct mapped_device *md);

/*
 * A device can still be used while suspended, but I/O is deferred.
 */
int dm_suspend(struct mapped_device *md, unsigned suspend_flags);
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
int dm_noflush_suspending(struct dm_target *ti);
union map_info *dm_get_mapinfo(struct bio *bio);
union map_info *dm_get_rq_mapinfo(struct request *rq);

/*
 * Geometry functions.
 */
int dm_get_geometry(struct mapped_device *md, struct hd_geometry *geo);
int dm_set_geometry(struct mapped_device *md, struct hd_geometry *geo);


/*-----------------------------------------------------------------
 * Functions for manipulating device-mapper tables.
 *---------------------------------------------------------------*/

/*
 * First create an empty table.
 */
int dm_table_create(struct dm_table **result, fmode_t mode,
		    unsigned num_targets, struct mapped_device *md);

/*
 * Then call this once for each target.
 */
int dm_table_add_target(struct dm_table *t, const char *type,
			sector_t start, sector_t len, char *params);

/*
 * Target_ctr should call this if it needs to add any callbacks.
 */
void dm_table_add_target_callbacks(struct dm_table *t, struct dm_target_callbacks *cb);

/*
 * Finally call this to make the table ready for use.
 */
int dm_table_complete(struct dm_table *t);

/*
 * Table reference counting.
 */
struct dm_table *dm_get_live_table(struct mapped_device *md);
void dm_table_get(struct dm_table *t);
void dm_table_put(struct dm_table *t);

/*
 * Queries
 */
sector_t dm_table_get_size(struct dm_table *t);
unsigned int dm_table_get_num_targets(struct dm_table *t);
fmode_t dm_table_get_mode(struct dm_table *t);
struct mapped_device *dm_table_get_md(struct dm_table *t);

/*
 * Trigger an event.
 */
void dm_table_event(struct dm_table *t);

/*
 * The device must be suspended before calling this method.
 * Returns the previous table, which the caller must destroy.
 */
struct dm_table *dm_swap_table(struct mapped_device *md,
			       struct dm_table *t);

/*
 * A wrapper around vmalloc.
 */
void *dm_vcalloc(unsigned long nmemb, unsigned long elem_size);

/*-----------------------------------------------------------------
 * Macros.
 *---------------------------------------------------------------*/
#define DM_NAME "device-mapper"

#ifdef CONFIG_PRINTK
extern struct ratelimit_state dm_ratelimit_state;

#define dm_ratelimit()	__ratelimit(&dm_ratelimit_state)
#else
#define dm_ratelimit()	0
#endif

#define DMCRIT(f, arg...) \
	printk(KERN_CRIT DM_NAME ": " DM_MSG_PREFIX ": " f "\n", ## arg)

#define DMERR(f, arg...) \
	printk(KERN_ERR DM_NAME ": " DM_MSG_PREFIX ": " f "\n", ## arg)
#define DMERR_LIMIT(f, arg...) \
	do { \
		if (dm_ratelimit())	\
			printk(KERN_ERR DM_NAME ": " DM_MSG_PREFIX ": " \
			       f "\n", ## arg); \
	} while (0)

#define DMWARN(f, arg...) \
	printk(KERN_WARNING DM_NAME ": " DM_MSG_PREFIX ": " f "\n", ## arg)
#define DMWARN_LIMIT(f, arg...) \
	do { \
		if (dm_ratelimit())	\
			printk(KERN_WARNING DM_NAME ": " DM_MSG_PREFIX ": " \
			       f "\n", ## arg); \
	} while (0)

#define DMINFO(f, arg...) \
	printk(KERN_INFO DM_NAME ": " DM_MSG_PREFIX ": " f "\n", ## arg)
#define DMINFO_LIMIT(f, arg...) \
	do { \
		if (dm_ratelimit())	\
			printk(KERN_INFO DM_NAME ": " DM_MSG_PREFIX ": " f \
			       "\n", ## arg); \
	} while (0)

#ifdef CONFIG_DM_DEBUG
#  define DMDEBUG(f, arg...) \
	printk(KERN_DEBUG DM_NAME ": " DM_MSG_PREFIX " DEBUG: " f "\n", ## arg)
#  define DMDEBUG_LIMIT(f, arg...) \
	do { \
		if (dm_ratelimit())	\
			printk(KERN_DEBUG DM_NAME ": " DM_MSG_PREFIX ": " f \
			       "\n", ## arg); \
	} while (0)
#else
#  define DMDEBUG(f, arg...) do {} while (0)
#  define DMDEBUG_LIMIT(f, arg...) do {} while (0)
#endif

#define DMEMIT(x...) sz += ((sz >= maxlen) ? \
			  0 : scnprintf(result + sz, maxlen - sz, x))

#define SECTOR_SHIFT 9

/*
 * Definitions of return values from target end_io function.
 */
#define DM_ENDIO_INCOMPLETE	1
#define DM_ENDIO_REQUEUE	2

/*
 * Definitions of return values from target map function.
 */
#define DM_MAPIO_SUBMITTED	0
#define DM_MAPIO_REMAPPED	1
#define DM_MAPIO_REQUEUE	DM_ENDIO_REQUEUE

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

#define dm_array_too_big(fixed, obj, num) \
	((num) > (UINT_MAX - (fixed)) / (obj))

/*
 * Sector offset taken relative to the start of the target instead of
 * relative to the start of the device.
 */
#define dm_target_offset(ti, sector) ((sector) - (ti)->begin)

static inline sector_t to_sector(unsigned long n)
{
	return (n >> SECTOR_SHIFT);
}

static inline unsigned long to_bytes(sector_t n)
{
	return (n << SECTOR_SHIFT);
}

/*-----------------------------------------------------------------
 * Helper for block layer and dm core operations
 *---------------------------------------------------------------*/
void dm_dispatch_request(struct request *rq);
void dm_requeue_unmapped_request(struct request *rq);
void dm_kill_unmapped_request(struct request *rq, int error);
int dm_underlying_device_busy(struct request_queue *q);

#endif	/* _LINUX_DEVICE_MAPPER_H */
