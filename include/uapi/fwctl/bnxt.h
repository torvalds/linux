/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2026, Broadcom Inc
 */

#ifndef _UAPI_FWCTL_BNXT_H_
#define _UAPI_FWCTL_BNXT_H_

#include <linux/types.h>

enum fwctl_bnxt_commands {
	FWCTL_BNXT_INLINE_COMMANDS = 0,
	FWCTL_BNXT_QUERY_COMMANDS,
	FWCTL_BNXT_SEND_COMMANDS,
};

/**
 * struct fwctl_info_bnxt - ioctl(FWCTL_INFO) out_device_data
 * @uctx_caps: The command capabilities driver accepts.
 *
 * Return basic information about the FW interface available.
 */
struct fwctl_info_bnxt {
	__u32 uctx_caps;
};
#endif
