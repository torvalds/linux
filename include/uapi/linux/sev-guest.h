/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Userspace interface for AMD SEV and SNP guest driver.
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 *
 * SEV API specification is available at: https://developer.amd.com/sev/
 */

#ifndef __UAPI_LINUX_SEV_GUEST_H_
#define __UAPI_LINUX_SEV_GUEST_H_

#include <linux/types.h>

struct snp_report_req {
	/* user data that should be included in the report */
	__u8 user_data[64];

	/* The vmpl level to be included in the report */
	__u32 vmpl;

	/* Must be zero filled */
	__u8 rsvd[28];
};

struct snp_report_resp {
	/* response data, see SEV-SNP spec for the format */
	__u8 data[4000];
};

struct snp_guest_request_ioctl {
	/* message version number (must be non-zero) */
	__u8 msg_version;

	/* Request and response structure address */
	__u64 req_data;
	__u64 resp_data;

	/* firmware error code on failure (see psp-sev.h) */
	__u64 fw_err;
};

#define SNP_GUEST_REQ_IOC_TYPE	'S'

/* Get SNP attestation report */
#define SNP_GET_REPORT _IOWR(SNP_GUEST_REQ_IOC_TYPE, 0x0, struct snp_guest_request_ioctl)

#endif /* __UAPI_LINUX_SEV_GUEST_H_ */
