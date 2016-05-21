/*
 * Copyright (C) 2012 Google, Inc.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _UAPI_LINUX_SYNC_H
#define _UAPI_LINUX_SYNC_H

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * struct sync_merge_data - data passed to merge ioctl
 * @name:	name of new fence
 * @fd2:	file descriptor of second fence
 * @fence:	returns the fd of the new fence to userspace
 * @flags:	merge_data flags
 * @pad:	padding for 64-bit alignment, should always be zero
 */
struct sync_merge_data {
	char	name[32];
	__s32	fd2;
	__s32	fence;
	__u32	flags;
	__u32	pad;
};

/**
 * struct sync_fence_info - detailed fence information
 * @obj_name:		name of parent sync_timeline
* @driver_name:	name of driver implementing the parent
* @status:		status of the fence 0:active 1:signaled <0:error
 * @flags:		fence_info flags
 * @timestamp_ns:	timestamp of status change in nanoseconds
 */
struct sync_fence_info {
	char	obj_name[32];
	char	driver_name[32];
	__s32	status;
	__u32	flags;
	__u64	timestamp_ns;
};

/**
 * struct sync_file_info - data returned from fence info ioctl
 * @name:	name of fence
 * @status:	status of fence. 1: signaled 0:active <0:error
 * @flags:	sync_file_info flags
 * @num_fences	number of fences in the sync_file
 * @pad:	padding for 64-bit alignment, should always be zero
 * @sync_fence_info: pointer to array of structs sync_fence_info with all
 *		 fences in the sync_file
 */
struct sync_file_info {
	char	name[32];
	__s32	status;
	__u32	flags;
	__u32	num_fences;
	__u32	pad;

	__u64	sync_fence_info;
};

#define SYNC_IOC_MAGIC		'>'

/**
 * Opcodes  0, 1 and 2 were burned during a API change to avoid users of the
 * old API to get weird errors when trying to handling sync_files. The API
 * change happened during the de-stage of the Sync Framework when there was
 * no upstream users available.
 */

/**
 * DOC: SYNC_IOC_MERGE - merge two fences
 *
 * Takes a struct sync_merge_data.  Creates a new fence containing copies of
 * the sync_pts in both the calling fd and sync_merge_data.fd2.  Returns the
 * new fence's fd in sync_merge_data.fence
 */
#define SYNC_IOC_MERGE		_IOWR(SYNC_IOC_MAGIC, 3, struct sync_merge_data)

/**
 * DOC: SYNC_IOC_FENCE_INFO - get detailed information on a fence
 *
 * Takes a struct sync_file_info_data with extra space allocated for pt_info.
 * Caller should write the size of the buffer into len.  On return, len is
 * updated to reflect the total size of the sync_file_info_data including
 * pt_info.
 *
 * pt_info is a buffer containing sync_pt_infos for every sync_pt in the fence.
 * To iterate over the sync_pt_infos, use the sync_pt_info.len field.
 */
#define SYNC_IOC_FILE_INFO	_IOWR(SYNC_IOC_MAGIC, 4, struct sync_file_info)

#endif /* _UAPI_LINUX_SYNC_H */
