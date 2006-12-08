/*
 * Copyright (C) 2001 Sistina Software (UK) Limited.
 * Copyright (C) 2004 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the LGPL.
 */

#ifndef _LINUX_DEVICE_MAPPER_H
#define _LINUX_DEVICE_MAPPER_H

#ifdef __KERNEL__

struct dm_target;
struct dm_table;
struct dm_dev;
struct mapped_device;

typedef enum { STATUSTYPE_INFO, STATUSTYPE_TABLE } status_type_t;

union map_info {
	void *ptr;
	unsigned long long ll;
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
 */
typedef int (*dm_map_fn) (struct dm_target *ti, struct bio *bio,
			  union map_info *map_context);

/*
 * Returns:
 * < 0 : error (currently ignored)
 * 0   : ended successfully
 * 1   : for some reason the io has still not completed (eg,
 *       multipath target might want to requeue a failed io).
 */
typedef int (*dm_endio_fn) (struct dm_target *ti,
			    struct bio *bio, int error,
			    union map_info *map_context);

typedef void (*dm_flush_fn) (struct dm_target *ti);
typedef void (*dm_presuspend_fn) (struct dm_target *ti);
typedef void (*dm_postsuspend_fn) (struct dm_target *ti);
typedef int (*dm_preresume_fn) (struct dm_target *ti);
typedef void (*dm_resume_fn) (struct dm_target *ti);

typedef int (*dm_status_fn) (struct dm_target *ti, status_type_t status_type,
			     char *result, unsigned int maxlen);

typedef int (*dm_message_fn) (struct dm_target *ti, unsigned argc, char **argv);

typedef int (*dm_ioctl_fn) (struct dm_target *ti, struct inode *inode,
			    struct file *filp, unsigned int cmd,
			    unsigned long arg);

void dm_error(const char *message);

/*
 * Combine device limits.
 */
void dm_set_device_limits(struct dm_target *ti, struct block_device *bdev);

/*
 * Constructors should call these functions to ensure destination devices
 * are opened/closed correctly.
 * FIXME: too many arguments.
 */
int dm_get_device(struct dm_target *ti, const char *path, sector_t start,
		  sector_t len, int mode, struct dm_dev **result);
void dm_put_device(struct dm_target *ti, struct dm_dev *d);

/*
 * Information about a target type
 */
struct target_type {
	const char *name;
	struct module *module;
	unsigned version[3];
	dm_ctr_fn ctr;
	dm_dtr_fn dtr;
	dm_map_fn map;
	dm_endio_fn end_io;
	dm_flush_fn flush;
	dm_presuspend_fn presuspend;
	dm_postsuspend_fn postsuspend;
	dm_preresume_fn preresume;
	dm_resume_fn resume;
	dm_status_fn status;
	dm_message_fn message;
	dm_ioctl_fn ioctl;
};

struct io_restrictions {
	unsigned int		max_sectors;
	unsigned short		max_phys_segments;
	unsigned short		max_hw_segments;
	unsigned short		hardsect_size;
	unsigned int		max_segment_size;
	unsigned long		seg_boundary_mask;
	unsigned char		no_cluster; /* inverted so that 0 is default */
};

struct dm_target {
	struct dm_table *table;
	struct target_type *type;

	/* target limits */
	sector_t begin;
	sector_t len;

	/* FIXME: turn this into a mask, and merge with io_restrictions */
	/* Always a power of 2 */
	sector_t split_io;

	/*
	 * These are automatically filled in by
	 * dm_table_get_device.
	 */
	struct io_restrictions limits;

	/* target specific data */
	void *private;

	/* Used to provide an error string from the ctr */
	char *error;
};

int dm_register_target(struct target_type *t);
int dm_unregister_target(struct target_type *t);


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

/*
 * Info functions.
 */
const char *dm_device_name(struct mapped_device *md);
struct gendisk *dm_disk(struct mapped_device *md);
int dm_suspended(struct mapped_device *md);

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
int dm_table_create(struct dm_table **result, int mode,
		    unsigned num_targets, struct mapped_device *md);

/*
 * Then call this once for each target.
 */
int dm_table_add_target(struct dm_table *t, const char *type,
			sector_t start, sector_t len, char *params);

/*
 * Finally call this to make the table ready for use.
 */
int dm_table_complete(struct dm_table *t);

/*
 * Table reference counting.
 */
struct dm_table *dm_get_table(struct mapped_device *md);
void dm_table_get(struct dm_table *t);
void dm_table_put(struct dm_table *t);

/*
 * Queries
 */
sector_t dm_table_get_size(struct dm_table *t);
unsigned int dm_table_get_num_targets(struct dm_table *t);
int dm_table_get_mode(struct dm_table *t);
struct mapped_device *dm_table_get_md(struct dm_table *t);

/*
 * Trigger an event.
 */
void dm_table_event(struct dm_table *t);

/*
 * The device must be suspended before calling this method.
 */
int dm_swap_table(struct mapped_device *md, struct dm_table *t);

/*
 * Prepare a table for a device that will error all I/O.
 * To make it active, call dm_suspend(), dm_swap_table() then dm_resume().
 */
int dm_create_error_table(struct dm_table **result, struct mapped_device *md);

#endif	/* __KERNEL__ */
#endif	/* _LINUX_DEVICE_MAPPER_H */
