/*
 * Copyright (C) 2006-2009 Red Hat, Inc.
 *
 * This file is released under the LGPL.
 */

#ifndef __DM_LOG_USERSPACE_H__
#define __DM_LOG_USERSPACE_H__

#include <linux/dm-ioctl.h> /* For DM_UUID_LEN */

/*
 * The device-mapper userspace log module consists of a kernel component and
 * a user-space component.  The kernel component implements the API defined
 * in dm-dirty-log.h.  Its purpose is simply to pass the parameters and
 * return values of those API functions between kernel and user-space.
 *
 * Below are defined the 'request_types' - DM_ULOG_CTR, DM_ULOG_DTR, etc.
 * These request types represent the different functions in the device-mapper
 * dirty log API.  Each of these is described in more detail below.
 *
 * The user-space program must listen for requests from the kernel (representing
 * the various API functions) and process them.
 *
 * User-space begins by setting up the communication link (error checking
 * removed for clarity):
 *	fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
 *	addr.nl_family = AF_NETLINK;
 *	addr.nl_groups = CN_IDX_DM;
 *	addr.nl_pid = 0;
 *	r = bind(fd, (struct sockaddr *) &addr, sizeof(addr));
 *	opt = addr.nl_groups;
 *	setsockopt(fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &opt, sizeof(opt));
 *
 * User-space will then wait to receive requests form the kernel, which it
 * will process as described below.  The requests are received in the form,
 * ((struct dm_ulog_request) + (additional data)).  Depending on the request
 * type, there may or may not be 'additional data'.  In the descriptions below,
 * you will see 'Payload-to-userspace' and 'Payload-to-kernel'.  The
 * 'Payload-to-userspace' is what the kernel sends in 'additional data' as
 * necessary parameters to complete the request.  The 'Payload-to-kernel' is
 * the 'additional data' returned to the kernel that contains the necessary
 * results of the request.  The 'data_size' field in the dm_ulog_request
 * structure denotes the availability and amount of payload data.
 */

/*
 * DM_ULOG_CTR corresponds to (found in dm-dirty-log.h):
 * int (*ctr)(struct dm_dirty_log *log, struct dm_target *ti,
 *	      unsigned argc, char **argv);
 *
 * Payload-to-userspace:
 *	A single string containing all the argv arguments separated by ' 's
 * Payload-to-kernel:
 *	None.  ('data_size' in the dm_ulog_request struct should be 0.)
 *
 * The UUID contained in the dm_ulog_request structure is the reference that
 * will be used by all request types to a specific log.  The constructor must
 * record this assotiation with instance created.
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field and
 * 'data_size' appropriately.
 */
#define DM_ULOG_CTR                    1

/*
 * DM_ULOG_DTR corresponds to (found in dm-dirty-log.h):
 * void (*dtr)(struct dm_dirty_log *log);
 *
 * Payload-to-userspace:
 *	A single string containing all the argv arguments separated by ' 's
 * Payload-to-kernel:
 *	None.  ('data_size' in the dm_ulog_request struct should be 0.)
 *
 * The UUID contained in the dm_ulog_request structure is all that is
 * necessary to identify the log instance being destroyed.  There is no
 * payload data.
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field and clearing
 * 'data_size' appropriately.
 */
#define DM_ULOG_DTR                    2

/*
 * DM_ULOG_PRESUSPEND corresponds to (found in dm-dirty-log.h):
 * int (*presuspend)(struct dm_dirty_log *log);
 *
 * Payload-to-userspace:
 *	None.
 * Payload-to-kernel:
 *	None.
 *
 * The UUID contained in the dm_ulog_request structure is all that is
 * necessary to identify the log instance being presuspended.  There is no
 * payload data.
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field and
 * 'data_size' appropriately.
 */
#define DM_ULOG_PRESUSPEND             3

/*
 * DM_ULOG_POSTSUSPEND corresponds to (found in dm-dirty-log.h):
 * int (*postsuspend)(struct dm_dirty_log *log);
 *
 * Payload-to-userspace:
 *	None.
 * Payload-to-kernel:
 *	None.
 *
 * The UUID contained in the dm_ulog_request structure is all that is
 * necessary to identify the log instance being postsuspended.  There is no
 * payload data.
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field and
 * 'data_size' appropriately.
 */
#define DM_ULOG_POSTSUSPEND            4

/*
 * DM_ULOG_RESUME corresponds to (found in dm-dirty-log.h):
 * int (*resume)(struct dm_dirty_log *log);
 *
 * Payload-to-userspace:
 *	None.
 * Payload-to-kernel:
 *	None.
 *
 * The UUID contained in the dm_ulog_request structure is all that is
 * necessary to identify the log instance being resumed.  There is no
 * payload data.
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field and
 * 'data_size' appropriately.
 */
#define DM_ULOG_RESUME                 5

/*
 * DM_ULOG_GET_REGION_SIZE corresponds to (found in dm-dirty-log.h):
 * uint32_t (*get_region_size)(struct dm_dirty_log *log);
 *
 * Payload-to-userspace:
 *	None.
 * Payload-to-kernel:
 *	uint64_t - contains the region size
 *
 * The region size is something that was determined at constructor time.
 * It is returned in the payload area and 'data_size' is set to
 * reflect this.
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field appropriately.
 */
#define DM_ULOG_GET_REGION_SIZE        6

/*
 * DM_ULOG_IS_CLEAN corresponds to (found in dm-dirty-log.h):
 * int (*is_clean)(struct dm_dirty_log *log, region_t region);
 *
 * Payload-to-userspace:
 *	uint64_t - the region to get clean status on
 * Payload-to-kernel:
 *	int64_t  - 1 if clean, 0 otherwise
 *
 * Payload is sizeof(uint64_t) and contains the region for which the clean
 * status is being made.
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - filling the payload with 0 (not clean) or
 * 1 (clean), setting 'data_size' and 'error' appropriately.
 */
#define DM_ULOG_IS_CLEAN               7

/*
 * DM_ULOG_IN_SYNC corresponds to (found in dm-dirty-log.h):
 * int (*in_sync)(struct dm_dirty_log *log, region_t region,
 *		  int can_block);
 *
 * Payload-to-userspace:
 *	uint64_t - the region to get sync status on
 * Payload-to-kernel:
 *	int64_t - 1 if in-sync, 0 otherwise
 *
 * Exactly the same as 'is_clean' above, except this time asking "has the
 * region been recovered?" vs. "is the region not being modified?"
 */
#define DM_ULOG_IN_SYNC                8

/*
 * DM_ULOG_FLUSH corresponds to (found in dm-dirty-log.h):
 * int (*flush)(struct dm_dirty_log *log);
 *
 * Payload-to-userspace:
 *	None.
 * Payload-to-kernel:
 *	None.
 *
 * No incoming or outgoing payload.  Simply flush log state to disk.
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field and clearing
 * 'data_size' appropriately.
 */
#define DM_ULOG_FLUSH                  9

/*
 * DM_ULOG_MARK_REGION corresponds to (found in dm-dirty-log.h):
 * void (*mark_region)(struct dm_dirty_log *log, region_t region);
 *
 * Payload-to-userspace:
 *	uint64_t [] - region(s) to mark
 * Payload-to-kernel:
 *	None.
 *
 * Incoming payload contains the one or more regions to mark dirty.
 * The number of regions contained in the payload can be determined from
 * 'data_size/sizeof(uint64_t)'.
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field and clearing
 * 'data_size' appropriately.
 */
#define DM_ULOG_MARK_REGION           10

/*
 * DM_ULOG_CLEAR_REGION corresponds to (found in dm-dirty-log.h):
 * void (*clear_region)(struct dm_dirty_log *log, region_t region);
 *
 * Payload-to-userspace:
 *	uint64_t [] - region(s) to clear
 * Payload-to-kernel:
 *	None.
 *
 * Incoming payload contains the one or more regions to mark clean.
 * The number of regions contained in the payload can be determined from
 * 'data_size/sizeof(uint64_t)'.
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field and clearing
 * 'data_size' appropriately.
 */
#define DM_ULOG_CLEAR_REGION          11

/*
 * DM_ULOG_GET_RESYNC_WORK corresponds to (found in dm-dirty-log.h):
 * int (*get_resync_work)(struct dm_dirty_log *log, region_t *region);
 *
 * Payload-to-userspace:
 *	None.
 * Payload-to-kernel:
 *	{
 *		int64_t i; -- 1 if recovery necessary, 0 otherwise
 *		uint64_t r; -- The region to recover if i=1
 *	}
 * 'data_size' should be set appropriately.
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field appropriately.
 */
#define DM_ULOG_GET_RESYNC_WORK       12

/*
 * DM_ULOG_SET_REGION_SYNC corresponds to (found in dm-dirty-log.h):
 * void (*set_region_sync)(struct dm_dirty_log *log,
 *			   region_t region, int in_sync);
 *
 * Payload-to-userspace:
 *	{
 *		uint64_t - region to set sync state on
 *		int64_t  - 0 if not-in-sync, 1 if in-sync
 *	}
 * Payload-to-kernel:
 *	None.
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field and clearing
 * 'data_size' appropriately.
 */
#define DM_ULOG_SET_REGION_SYNC       13

/*
 * DM_ULOG_GET_SYNC_COUNT corresponds to (found in dm-dirty-log.h):
 * region_t (*get_sync_count)(struct dm_dirty_log *log);
 *
 * Payload-to-userspace:
 *	None.
 * Payload-to-kernel:
 *	uint64_t - the number of in-sync regions
 *
 * No incoming payload.  Kernel-bound payload contains the number of
 * regions that are in-sync (in a size_t).
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field and
 * 'data_size' appropriately.
 */
#define DM_ULOG_GET_SYNC_COUNT        14

/*
 * DM_ULOG_STATUS_INFO corresponds to (found in dm-dirty-log.h):
 * int (*status)(struct dm_dirty_log *log, STATUSTYPE_INFO,
 *		 char *result, unsigned maxlen);
 *
 * Payload-to-userspace:
 *	None.
 * Payload-to-kernel:
 *	Character string containing STATUSTYPE_INFO
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field and
 * 'data_size' appropriately.
 */
#define DM_ULOG_STATUS_INFO           15

/*
 * DM_ULOG_STATUS_TABLE corresponds to (found in dm-dirty-log.h):
 * int (*status)(struct dm_dirty_log *log, STATUSTYPE_TABLE,
 *		 char *result, unsigned maxlen);
 *
 * Payload-to-userspace:
 *	None.
 * Payload-to-kernel:
 *	Character string containing STATUSTYPE_TABLE
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field and
 * 'data_size' appropriately.
 */
#define DM_ULOG_STATUS_TABLE          16

/*
 * DM_ULOG_IS_REMOTE_RECOVERING corresponds to (found in dm-dirty-log.h):
 * int (*is_remote_recovering)(struct dm_dirty_log *log, region_t region);
 *
 * Payload-to-userspace:
 *	uint64_t - region to determine recovery status on
 * Payload-to-kernel:
 *	{
 *		int64_t is_recovering;  -- 0 if no, 1 if yes
 *		uint64_t in_sync_hint;  -- lowest region still needing resync
 *	}
 *
 * When the request has been processed, user-space must return the
 * dm_ulog_request to the kernel - setting the 'error' field and
 * 'data_size' appropriately.
 */
#define DM_ULOG_IS_REMOTE_RECOVERING  17

/*
 * (DM_ULOG_REQUEST_MASK & request_type) to get the request type
 *
 * Payload-to-userspace:
 *	A single string containing all the argv arguments separated by ' 's
 * Payload-to-kernel:
 *	None.  ('data_size' in the dm_ulog_request struct should be 0.)
 *
 * We are reserving 8 bits of the 32-bit 'request_type' field for the
 * various request types above.  The remaining 24-bits are currently
 * set to zero and are reserved for future use and compatibility concerns.
 *
 * User-space should always use DM_ULOG_REQUEST_TYPE to acquire the
 * request type from the 'request_type' field to maintain forward compatibility.
 */
#define DM_ULOG_REQUEST_MASK 0xFF
#define DM_ULOG_REQUEST_TYPE(request_type) \
	(DM_ULOG_REQUEST_MASK & (request_type))

/*
 * DM_ULOG_REQUEST_VERSION is incremented when there is a
 * change to the way information is passed between kernel
 * and userspace.  This could be a structure change of
 * dm_ulog_request or a change in the way requests are
 * issued/handled.  Changes are outlined here:
 *	version 1:  Initial implementation
 */
#define DM_ULOG_REQUEST_VERSION 1

struct dm_ulog_request {
	/*
	 * The local unique identifier (luid) and the universally unique
	 * identifier (uuid) are used to tie a request to a specific
	 * mirror log.  A single machine log could probably make due with
	 * just the 'luid', but a cluster-aware log must use the 'uuid' and
	 * the 'luid'.  The uuid is what is required for node to node
	 * communication concerning a particular log, but the 'luid' helps
	 * differentiate between logs that are being swapped and have the
	 * same 'uuid'.  (Think "live" and "inactive" device-mapper tables.)
	 */
	uint64_t luid;
	char uuid[DM_UUID_LEN];
	char padding[3];        /* Padding because DM_UUID_LEN = 129 */

	uint32_t version;       /* See DM_ULOG_REQUEST_VERSION */
	int32_t error;          /* Used to report back processing errors */

	uint32_t seq;           /* Sequence number for request */
	uint32_t request_type;  /* DM_ULOG_* defined above */
	uint32_t data_size;     /* How much data (not including this struct) */

	char data[0];
};

#endif /* __DM_LOG_USERSPACE_H__ */
