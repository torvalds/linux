/*
 * Copyright (C) 2003 Sistina Software
 * Copyright (C) 2004-2008 Red Hat, Inc. All rights reserved.
 *
 * Device-Mapper dirty region log.
 *
 * This file is released under the LGPL.
 */

#ifndef _LINUX_DM_DIRTY_LOG
#define _LINUX_DM_DIRTY_LOG

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/device-mapper.h>

typedef sector_t region_t;

struct dm_dirty_log_type;

struct dm_dirty_log {
	struct dm_dirty_log_type *type;
	void *context;
};

struct dm_dirty_log_type {
	const char *name;
	struct module *module;

	int (*ctr)(struct dm_dirty_log *log, struct dm_target *ti,
		   unsigned argc, char **argv);
	void (*dtr)(struct dm_dirty_log *log);

	/*
	 * There are times when we don't want the log to touch
	 * the disk.
	 */
	int (*presuspend)(struct dm_dirty_log *log);
	int (*postsuspend)(struct dm_dirty_log *log);
	int (*resume)(struct dm_dirty_log *log);

	/*
	 * Retrieves the smallest size of region that the log can
	 * deal with.
	 */
	uint32_t (*get_region_size)(struct dm_dirty_log *log);

	/*
	 * A predicate to say whether a region is clean or not.
	 * May block.
	 */
	int (*is_clean)(struct dm_dirty_log *log, region_t region);

	/*
	 *  Returns: 0, 1, -EWOULDBLOCK, < 0
	 *
	 * A predicate function to check the area given by
	 * [sector, sector + len) is in sync.
	 *
	 * If -EWOULDBLOCK is returned the state of the region is
	 * unknown, typically this will result in a read being
	 * passed to a daemon to deal with, since a daemon is
	 * allowed to block.
	 */
	int (*in_sync)(struct dm_dirty_log *log, region_t region,
		       int can_block);

	/*
	 * Flush the current log state (eg, to disk).  This
	 * function may block.
	 */
	int (*flush)(struct dm_dirty_log *log);

	/*
	 * Mark an area as clean or dirty.  These functions may
	 * block, though for performance reasons blocking should
	 * be extremely rare (eg, allocating another chunk of
	 * memory for some reason).
	 */
	void (*mark_region)(struct dm_dirty_log *log, region_t region);
	void (*clear_region)(struct dm_dirty_log *log, region_t region);

	/*
	 * Returns: <0 (error), 0 (no region), 1 (region)
	 *
	 * The mirrord will need perform recovery on regions of
	 * the mirror that are in the NOSYNC state.  This
	 * function asks the log to tell the caller about the
	 * next region that this machine should recover.
	 *
	 * Do not confuse this function with 'in_sync()', one
	 * tells you if an area is synchronised, the other
	 * assigns recovery work.
	*/
	int (*get_resync_work)(struct dm_dirty_log *log, region_t *region);

	/*
	 * This notifies the log that the resync status of a region
	 * has changed.  It also clears the region from the recovering
	 * list (if present).
	 */
	void (*set_region_sync)(struct dm_dirty_log *log,
				region_t region, int in_sync);

	/*
	 * Returns the number of regions that are in sync.
	 */
	region_t (*get_sync_count)(struct dm_dirty_log *log);

	/*
	 * Support function for mirror status requests.
	 */
	int (*status)(struct dm_dirty_log *log, status_type_t status_type,
		      char *result, unsigned maxlen);
};

int dm_dirty_log_type_register(struct dm_dirty_log_type *type);
int dm_dirty_log_type_unregister(struct dm_dirty_log_type *type);

/*
 * Make sure you use these two functions, rather than calling
 * type->constructor/destructor() directly.
 */
struct dm_dirty_log *dm_dirty_log_create(const char *type_name,
					 struct dm_target *ti,
					 unsigned argc, char **argv);
void dm_dirty_log_destroy(struct dm_dirty_log *log);

#endif	/* __KERNEL__ */
#endif	/* _LINUX_DM_DIRTY_LOG_H */
