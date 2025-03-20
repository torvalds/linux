/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright(c) Advanced Micro Devices, Inc */

/*
 * fwctl interface info for pds_fwctl
 */

#ifndef _UAPI_FWCTL_PDS_H_
#define _UAPI_FWCTL_PDS_H_

#include <linux/types.h>

/**
 * struct fwctl_info_pds
 * @uctx_caps:  bitmap of firmware capabilities
 *
 * Return basic information about the FW interface available.
 */
struct fwctl_info_pds {
	__u32 uctx_caps;
};

/**
 * enum pds_fwctl_capabilities
 * @PDS_FWCTL_QUERY_CAP: firmware can be queried for information
 * @PDS_FWCTL_SEND_CAP:  firmware can be sent commands
 */
enum pds_fwctl_capabilities {
	PDS_FWCTL_QUERY_CAP = 0,
	PDS_FWCTL_SEND_CAP,
};
#endif /* _UAPI_FWCTL_PDS_H_ */
