/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 */

#ifndef __UAPI_LINUX_NSM_H
#define __UAPI_LINUX_NSM_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define NSM_MAGIC		0x0A

#define NSM_REQUEST_MAX_SIZE	0x1000
#define NSM_RESPONSE_MAX_SIZE	0x3000

struct nsm_iovec {
	__u64 addr; /* Virtual address of target buffer */
	__u64 len;  /* Length of target buffer */
};

/* Raw NSM message. Only available with CAP_SYS_ADMIN. */
struct nsm_raw {
	/* Request from user */
	struct nsm_iovec request;
	/* Response to user */
	struct nsm_iovec response;
};
#define NSM_IOCTL_RAW		_IOWR(NSM_MAGIC, 0x0, struct nsm_raw)

#endif /* __UAPI_LINUX_NSM_H */
