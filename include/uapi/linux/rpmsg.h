/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2016, Linaro Ltd.
 */

#ifndef _UAPI_RPMSG_H_
#define _UAPI_RPMSG_H_

#include <linux/ioctl.h>
#include <linux/types.h>

#define RPMSG_ADDR_ANY		0xFFFFFFFF

/**
 * struct rpmsg_endpoint_info - endpoint info representation
 * @name: name of service
 * @src: local address. To set to RPMSG_ADDR_ANY if not used.
 * @dst: destination address. To set to RPMSG_ADDR_ANY if not used.
 */
struct rpmsg_endpoint_info {
	char name[32];
	__u32 src;
	__u32 dst;
};

/**
 * Instantiate a new rmpsg char device endpoint.
 */
#define RPMSG_CREATE_EPT_IOCTL	_IOW(0xb5, 0x1, struct rpmsg_endpoint_info)

/**
 * Destroy a rpmsg char device endpoint created by the RPMSG_CREATE_EPT_IOCTL.
 */
#define RPMSG_DESTROY_EPT_IOCTL	_IO(0xb5, 0x2)

/**
 * Instantiate a new local rpmsg service device.
 */
#define RPMSG_CREATE_DEV_IOCTL	_IOW(0xb5, 0x3, struct rpmsg_endpoint_info)

/**
 * Release a local rpmsg device.
 */
#define RPMSG_RELEASE_DEV_IOCTL	_IOW(0xb5, 0x4, struct rpmsg_endpoint_info)

/**
 * Get the flow control state of the remote rpmsg char device.
 */
#define RPMSG_GET_OUTGOING_FLOWCONTROL _IOR(0xb5, 0x5, int)

/**
 * Set the flow control state of the local rpmsg char device.
 */
#define RPMSG_SET_INCOMING_FLOWCONTROL _IOR(0xb5, 0x6, int)

#endif
