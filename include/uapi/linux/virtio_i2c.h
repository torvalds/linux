/* SPDX-License-Identifier: GPL-2.0-or-later WITH Linux-syscall-note */
/*
 * Definitions for virtio I2C Adpter
 *
 * Copyright (c) 2021 Intel Corporation. All rights reserved.
 */

#ifndef _UAPI_LINUX_VIRTIO_I2C_H
#define _UAPI_LINUX_VIRTIO_I2C_H

#include <linux/const.h>
#include <linux/types.h>

/* The bit 0 of the @virtio_i2c_out_hdr.@flags, used to group the requests */
#define VIRTIO_I2C_FLAGS_FAIL_NEXT	_BITUL(0)

/**
 * struct virtio_i2c_out_hdr - the virtio I2C message OUT header
 * @addr: the controlled device address
 * @padding: used to pad to full dword
 * @flags: used for feature extensibility
 */
struct virtio_i2c_out_hdr {
	__le16 addr;
	__le16 padding;
	__le32 flags;
};

/**
 * struct virtio_i2c_in_hdr - the virtio I2C message IN header
 * @status: the processing result from the backend
 */
struct virtio_i2c_in_hdr {
	__u8 status;
};

/* The final status written by the device */
#define VIRTIO_I2C_MSG_OK	0
#define VIRTIO_I2C_MSG_ERR	1

#endif /* _UAPI_LINUX_VIRTIO_I2C_H */
