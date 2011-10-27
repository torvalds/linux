/*
 * Freescale hypervisor ioctl and kernel interface
 *
 * Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * This software is provided by Freescale Semiconductor "as is" and any
 * express or implied warranties, including, but not limited to, the implied
 * warranties of merchantability and fitness for a particular purpose are
 * disclaimed. In no event shall Freescale Semiconductor be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential damages
 * (including, but not limited to, procurement of substitute goods or services;
 * loss of use, data, or profits; or business interruption) however caused and
 * on any theory of liability, whether in contract, strict liability, or tort
 * (including negligence or otherwise) arising in any way out of the use of this
 * software, even if advised of the possibility of such damage.
 *
 * This file is used by the Freescale hypervisor management driver.  It can
 * also be included by applications that need to communicate with the driver
 * via the ioctl interface.
 */

#ifndef FSL_HYPERVISOR_H
#define FSL_HYPERVISOR_H

#include <linux/types.h>

/**
 * struct fsl_hv_ioctl_restart - restart a partition
 * @ret: return error code from the hypervisor
 * @partition: the ID of the partition to restart, or -1 for the
 *             calling partition
 *
 * Used by FSL_HV_IOCTL_PARTITION_RESTART
 */
struct fsl_hv_ioctl_restart {
	__u32 ret;
	__u32 partition;
};

/**
 * struct fsl_hv_ioctl_status - get a partition's status
 * @ret: return error code from the hypervisor
 * @partition: the ID of the partition to query, or -1 for the
 *             calling partition
 * @status: The returned status of the partition
 *
 * Used by FSL_HV_IOCTL_PARTITION_GET_STATUS
 *
 * Values of 'status':
 *    0 = Stopped
 *    1 = Running
 *    2 = Starting
 *    3 = Stopping
 */
struct fsl_hv_ioctl_status {
	__u32 ret;
	__u32 partition;
	__u32 status;
};

/**
 * struct fsl_hv_ioctl_start - start a partition
 * @ret: return error code from the hypervisor
 * @partition: the ID of the partition to control
 * @entry_point: The offset within the guest IMA to start execution
 * @load: If non-zero, reload the partition's images before starting
 *
 * Used by FSL_HV_IOCTL_PARTITION_START
 */
struct fsl_hv_ioctl_start {
	__u32 ret;
	__u32 partition;
	__u32 entry_point;
	__u32 load;
};

/**
 * struct fsl_hv_ioctl_stop - stop a partition
 * @ret: return error code from the hypervisor
 * @partition: the ID of the partition to stop, or -1 for the calling
 *             partition
 *
 * Used by FSL_HV_IOCTL_PARTITION_STOP
 */
struct fsl_hv_ioctl_stop {
	__u32 ret;
	__u32 partition;
};

/**
 * struct fsl_hv_ioctl_memcpy - copy memory between partitions
 * @ret: return error code from the hypervisor
 * @source: the partition ID of the source partition, or -1 for this
 *          partition
 * @target: the partition ID of the target partition, or -1 for this
 *          partition
 * @reserved: reserved, must be set to 0
 * @local_addr: user-space virtual address of a buffer in the local
 *              partition
 * @remote_addr: guest physical address of a buffer in the
 *           remote partition
 * @count: the number of bytes to copy.  Both the local and remote
 *         buffers must be at least 'count' bytes long
 *
 * Used by FSL_HV_IOCTL_MEMCPY
 *
 * The 'local' partition is the partition that calls this ioctl.  The
 * 'remote' partition is a different partition.  The data is copied from
 * the 'source' paritition' to the 'target' partition.
 *
 * The buffer in the remote partition must be guest physically
 * contiguous.
 *
 * This ioctl does not support copying memory between two remote
 * partitions or within the same partition, so either 'source' or
 * 'target' (but not both) must be -1.  In other words, either
 *
 *      source == local and target == remote
 * or
 *      source == remote and target == local
 */
struct fsl_hv_ioctl_memcpy {
	__u32 ret;
	__u32 source;
	__u32 target;
	__u32 reserved;	/* padding to ensure local_vaddr is aligned */
	__u64 local_vaddr;
	__u64 remote_paddr;
	__u64 count;
};

/**
 * struct fsl_hv_ioctl_doorbell - ring a doorbell
 * @ret: return error code from the hypervisor
 * @doorbell: the handle of the doorbell to ring doorbell
 *
 * Used by FSL_HV_IOCTL_DOORBELL
 */
struct fsl_hv_ioctl_doorbell {
	__u32 ret;
	__u32 doorbell;
};

/**
 * struct fsl_hv_ioctl_prop - get/set a device tree property
 * @ret: return error code from the hypervisor
 * @handle: handle of partition whose tree to access
 * @path: virtual address of path name of node to access
 * @propname: virtual address of name of property to access
 * @propval: virtual address of property data buffer
 * @proplen: Size of property data buffer
 * @reserved: reserved, must be set to 0
 *
 * Used by FSL_HV_IOCTL_DOORBELL
 */
struct fsl_hv_ioctl_prop {
	__u32 ret;
	__u32 handle;
	__u64 path;
	__u64 propname;
	__u64 propval;
	__u32 proplen;
	__u32 reserved;	/* padding to ensure structure is aligned */
};

/* The ioctl type, documented in ioctl-number.txt */
#define FSL_HV_IOCTL_TYPE	0xAF

/* Restart another partition */
#define FSL_HV_IOCTL_PARTITION_RESTART \
	_IOWR(FSL_HV_IOCTL_TYPE, 1, struct fsl_hv_ioctl_restart)

/* Get a partition's status */
#define FSL_HV_IOCTL_PARTITION_GET_STATUS \
	_IOWR(FSL_HV_IOCTL_TYPE, 2, struct fsl_hv_ioctl_status)

/* Boot another partition */
#define FSL_HV_IOCTL_PARTITION_START \
	_IOWR(FSL_HV_IOCTL_TYPE, 3, struct fsl_hv_ioctl_start)

/* Stop this or another partition */
#define FSL_HV_IOCTL_PARTITION_STOP \
	_IOWR(FSL_HV_IOCTL_TYPE, 4, struct fsl_hv_ioctl_stop)

/* Copy data from one partition to another */
#define FSL_HV_IOCTL_MEMCPY \
	_IOWR(FSL_HV_IOCTL_TYPE, 5, struct fsl_hv_ioctl_memcpy)

/* Ring a doorbell */
#define FSL_HV_IOCTL_DOORBELL \
	_IOWR(FSL_HV_IOCTL_TYPE, 6, struct fsl_hv_ioctl_doorbell)

/* Get a property from another guest's device tree */
#define FSL_HV_IOCTL_GETPROP \
	_IOWR(FSL_HV_IOCTL_TYPE, 7, struct fsl_hv_ioctl_prop)

/* Set a property in another guest's device tree */
#define FSL_HV_IOCTL_SETPROP \
	_IOWR(FSL_HV_IOCTL_TYPE, 8, struct fsl_hv_ioctl_prop)

#ifdef __KERNEL__

/**
 * fsl_hv_event_register() - register a callback for failover events
 * @nb: pointer to caller-supplied notifier_block structure
 *
 * This function is called by device drivers to register their callback
 * functions for fail-over events.
 *
 * The caller should allocate a notifier_block object and initialize the
 * 'priority' and 'notifier_call' fields.
 */
int fsl_hv_failover_register(struct notifier_block *nb);

/**
 * fsl_hv_event_unregister() - unregister a callback for failover events
 * @nb: the same 'nb' used in previous fsl_hv_failover_register call
 */
int fsl_hv_failover_unregister(struct notifier_block *nb);

#endif

#endif
