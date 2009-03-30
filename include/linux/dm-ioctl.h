/*
 * Copyright (C) 2001 - 2003 Sistina Software (UK) Limited.
 * Copyright (C) 2004 - 2005 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the LGPL.
 */

#ifndef _LINUX_DM_IOCTL_V4_H
#define _LINUX_DM_IOCTL_V4_H

#include <linux/types.h>

#define DM_DIR "mapper"		/* Slashes not supported */
#define DM_MAX_TYPE_NAME 16
#define DM_NAME_LEN 128
#define DM_UUID_LEN 129

/*
 * A traditional ioctl interface for the device mapper.
 *
 * Each device can have two tables associated with it, an
 * 'active' table which is the one currently used by io passing
 * through the device, and an 'inactive' one which is a table
 * that is being prepared as a replacement for the 'active' one.
 *
 * DM_VERSION:
 * Just get the version information for the ioctl interface.
 *
 * DM_REMOVE_ALL:
 * Remove all dm devices, destroy all tables.  Only really used
 * for debug.
 *
 * DM_LIST_DEVICES:
 * Get a list of all the dm device names.
 *
 * DM_DEV_CREATE:
 * Create a new device, neither the 'active' or 'inactive' table
 * slots will be filled.  The device will be in suspended state
 * after creation, however any io to the device will get errored
 * since it will be out-of-bounds.
 *
 * DM_DEV_REMOVE:
 * Remove a device, destroy any tables.
 *
 * DM_DEV_RENAME:
 * Rename a device.
 *
 * DM_SUSPEND:
 * This performs both suspend and resume, depending which flag is
 * passed in.
 * Suspend: This command will not return until all pending io to
 * the device has completed.  Further io will be deferred until
 * the device is resumed.
 * Resume: It is no longer an error to issue this command on an
 * unsuspended device.  If a table is present in the 'inactive'
 * slot, it will be moved to the active slot, then the old table
 * from the active slot will be _destroyed_.  Finally the device
 * is resumed.
 *
 * DM_DEV_STATUS:
 * Retrieves the status for the table in the 'active' slot.
 *
 * DM_DEV_WAIT:
 * Wait for a significant event to occur to the device.  This
 * could either be caused by an event triggered by one of the
 * targets of the table in the 'active' slot, or a table change.
 *
 * DM_TABLE_LOAD:
 * Load a table into the 'inactive' slot for the device.  The
 * device does _not_ need to be suspended prior to this command.
 *
 * DM_TABLE_CLEAR:
 * Destroy any table in the 'inactive' slot (ie. abort).
 *
 * DM_TABLE_DEPS:
 * Return a set of device dependencies for the 'active' table.
 *
 * DM_TABLE_STATUS:
 * Return the targets status for the 'active' table.
 *
 * DM_TARGET_MSG:
 * Pass a message string to the target at a specific offset of a device.
 *
 * DM_DEV_SET_GEOMETRY:
 * Set the geometry of a device by passing in a string in this format:
 *
 * "cylinders heads sectors_per_track start_sector"
 *
 * Beware that CHS geometry is nearly obsolete and only provided
 * for compatibility with dm devices that can be booted by a PC
 * BIOS.  See struct hd_geometry for range limits.  Also note that
 * the geometry is erased if the device size changes.
 */

/*
 * All ioctl arguments consist of a single chunk of memory, with
 * this structure at the start.  If a uuid is specified any
 * lookup (eg. for a DM_INFO) will be done on that, *not* the
 * name.
 */
struct dm_ioctl {
	/*
	 * The version number is made up of three parts:
	 * major - no backward or forward compatibility,
	 * minor - only backwards compatible,
	 * patch - both backwards and forwards compatible.
	 *
	 * All clients of the ioctl interface should fill in the
	 * version number of the interface that they were
	 * compiled with.
	 *
	 * All recognised ioctl commands (ie. those that don't
	 * return -ENOTTY) fill out this field, even if the
	 * command failed.
	 */
	__u32 version[3];	/* in/out */
	__u32 data_size;	/* total size of data passed in
				 * including this struct */

	__u32 data_start;	/* offset to start of data
				 * relative to start of this struct */

	__u32 target_count;	/* in/out */
	__s32 open_count;	/* out */
	__u32 flags;		/* in/out */
	__u32 event_nr;      	/* in/out */
	__u32 padding;

	__u64 dev;		/* in/out */

	char name[DM_NAME_LEN];	/* device name */
	char uuid[DM_UUID_LEN];	/* unique identifier for
				 * the block device */
	char data[7];		/* padding or data */
};

/*
 * Used to specify tables.  These structures appear after the
 * dm_ioctl.
 */
struct dm_target_spec {
	__u64 sector_start;
	__u64 length;
	__s32 status;		/* used when reading from kernel only */

	/*
	 * Location of the next dm_target_spec.
	 * - When specifying targets on a DM_TABLE_LOAD command, this value is
	 *   the number of bytes from the start of the "current" dm_target_spec
	 *   to the start of the "next" dm_target_spec.
	 * - When retrieving targets on a DM_TABLE_STATUS command, this value
	 *   is the number of bytes from the start of the first dm_target_spec
	 *   (that follows the dm_ioctl struct) to the start of the "next"
	 *   dm_target_spec.
	 */
	__u32 next;

	char target_type[DM_MAX_TYPE_NAME];

	/*
	 * Parameter string starts immediately after this object.
	 * Be careful to add padding after string to ensure correct
	 * alignment of subsequent dm_target_spec.
	 */
};

/*
 * Used to retrieve the target dependencies.
 */
struct dm_target_deps {
	__u32 count;	/* Array size */
	__u32 padding;	/* unused */
	__u64 dev[0];	/* out */
};

/*
 * Used to get a list of all dm devices.
 */
struct dm_name_list {
	__u64 dev;
	__u32 next;		/* offset to the next record from
				   the _start_ of this */
	char name[0];
};

/*
 * Used to retrieve the target versions
 */
struct dm_target_versions {
        __u32 next;
        __u32 version[3];

        char name[0];
};

/*
 * Used to pass message to a target
 */
struct dm_target_msg {
	__u64 sector;	/* Device sector */

	char message[0];
};

/*
 * If you change this make sure you make the corresponding change
 * to dm-ioctl.c:lookup_ioctl()
 */
enum {
	/* Top level cmds */
	DM_VERSION_CMD = 0,
	DM_REMOVE_ALL_CMD,
	DM_LIST_DEVICES_CMD,

	/* device level cmds */
	DM_DEV_CREATE_CMD,
	DM_DEV_REMOVE_CMD,
	DM_DEV_RENAME_CMD,
	DM_DEV_SUSPEND_CMD,
	DM_DEV_STATUS_CMD,
	DM_DEV_WAIT_CMD,

	/* Table level cmds */
	DM_TABLE_LOAD_CMD,
	DM_TABLE_CLEAR_CMD,
	DM_TABLE_DEPS_CMD,
	DM_TABLE_STATUS_CMD,

	/* Added later */
	DM_LIST_VERSIONS_CMD,
	DM_TARGET_MSG_CMD,
	DM_DEV_SET_GEOMETRY_CMD
};

#define DM_IOCTL 0xfd

#define DM_VERSION       _IOWR(DM_IOCTL, DM_VERSION_CMD, struct dm_ioctl)
#define DM_REMOVE_ALL    _IOWR(DM_IOCTL, DM_REMOVE_ALL_CMD, struct dm_ioctl)
#define DM_LIST_DEVICES  _IOWR(DM_IOCTL, DM_LIST_DEVICES_CMD, struct dm_ioctl)

#define DM_DEV_CREATE    _IOWR(DM_IOCTL, DM_DEV_CREATE_CMD, struct dm_ioctl)
#define DM_DEV_REMOVE    _IOWR(DM_IOCTL, DM_DEV_REMOVE_CMD, struct dm_ioctl)
#define DM_DEV_RENAME    _IOWR(DM_IOCTL, DM_DEV_RENAME_CMD, struct dm_ioctl)
#define DM_DEV_SUSPEND   _IOWR(DM_IOCTL, DM_DEV_SUSPEND_CMD, struct dm_ioctl)
#define DM_DEV_STATUS    _IOWR(DM_IOCTL, DM_DEV_STATUS_CMD, struct dm_ioctl)
#define DM_DEV_WAIT      _IOWR(DM_IOCTL, DM_DEV_WAIT_CMD, struct dm_ioctl)

#define DM_TABLE_LOAD    _IOWR(DM_IOCTL, DM_TABLE_LOAD_CMD, struct dm_ioctl)
#define DM_TABLE_CLEAR   _IOWR(DM_IOCTL, DM_TABLE_CLEAR_CMD, struct dm_ioctl)
#define DM_TABLE_DEPS    _IOWR(DM_IOCTL, DM_TABLE_DEPS_CMD, struct dm_ioctl)
#define DM_TABLE_STATUS  _IOWR(DM_IOCTL, DM_TABLE_STATUS_CMD, struct dm_ioctl)

#define DM_LIST_VERSIONS _IOWR(DM_IOCTL, DM_LIST_VERSIONS_CMD, struct dm_ioctl)

#define DM_TARGET_MSG	 _IOWR(DM_IOCTL, DM_TARGET_MSG_CMD, struct dm_ioctl)
#define DM_DEV_SET_GEOMETRY	_IOWR(DM_IOCTL, DM_DEV_SET_GEOMETRY_CMD, struct dm_ioctl)

#define DM_VERSION_MAJOR	4
#define DM_VERSION_MINOR	14
#define DM_VERSION_PATCHLEVEL	0
#define DM_VERSION_EXTRA	"-ioctl (2008-04-23)"

/* Status bits */
#define DM_READONLY_FLAG	(1 << 0) /* In/Out */
#define DM_SUSPEND_FLAG		(1 << 1) /* In/Out */
#define DM_PERSISTENT_DEV_FLAG	(1 << 3) /* In */

/*
 * Flag passed into ioctl STATUS command to get table information
 * rather than current status.
 */
#define DM_STATUS_TABLE_FLAG	(1 << 4) /* In */

/*
 * Flags that indicate whether a table is present in either of
 * the two table slots that a device has.
 */
#define DM_ACTIVE_PRESENT_FLAG   (1 << 5) /* Out */
#define DM_INACTIVE_PRESENT_FLAG (1 << 6) /* Out */

/*
 * Indicates that the buffer passed in wasn't big enough for the
 * results.
 */
#define DM_BUFFER_FULL_FLAG	(1 << 8) /* Out */

/*
 * This flag is now ignored.
 */
#define DM_SKIP_BDGET_FLAG	(1 << 9) /* In */

/*
 * Set this to avoid attempting to freeze any filesystem when suspending.
 */
#define DM_SKIP_LOCKFS_FLAG	(1 << 10) /* In */

/*
 * Set this to suspend without flushing queued ios.
 */
#define DM_NOFLUSH_FLAG		(1 << 11) /* In */

#endif				/* _LINUX_DM_IOCTL_H */
