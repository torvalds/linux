/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC Host driver.
 *
 */
#ifndef _MIC_IOCTL_H_
#define _MIC_IOCTL_H_

#include <linux/types.h>

/*
 * mic_copy - MIC virtio descriptor copy.
 *
 * @iov: An array of IOVEC structures containing user space buffers.
 * @iovcnt: Number of IOVEC structures in iov.
 * @vr_idx: The vring index.
 * @update_used: A non zero value results in used index being updated.
 * @out_len: The aggregate of the total length written to or read from
 *	the virtio device.
 */
struct mic_copy_desc {
#ifdef __KERNEL__
	struct iovec __user *iov;
#else
	struct iovec *iov;
#endif
	int iovcnt;
	__u8 vr_idx;
	__u8 update_used;
	__u32 out_len;
};

/*
 * Add a new virtio device
 * The (struct mic_device_desc *) pointer points to a device page entry
 *	for the virtio device consisting of:
 *	- struct mic_device_desc
 *	- struct mic_vqconfig (num_vq of these)
 *	- host and guest features
 *	- virtio device config space
 * The total size referenced by the pointer should equal the size returned
 * by desc_size() in mic_common.h
 */
#define MIC_VIRTIO_ADD_DEVICE _IOWR('s', 1, struct mic_device_desc *)

/*
 * Copy the number of entries in the iovec and update the used index
 * if requested by the user.
 */
#define MIC_VIRTIO_COPY_DESC	_IOWR('s', 2, struct mic_copy_desc *)

/*
 * Notify virtio device of a config change
 * The (__u8 *) pointer points to config space values for the device
 * as they should be written into the device page. The total size
 * referenced by the pointer should equal the config_len field of struct
 * mic_device_desc.
 */
#define MIC_VIRTIO_CONFIG_CHANGE _IOWR('s', 5, __u8 *)

#endif
