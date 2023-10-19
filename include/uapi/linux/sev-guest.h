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

struct snp_derived_key_req {
	__u32 root_key_select;
	__u32 rsvd;
	__u64 guest_field_select;
	__u32 vmpl;
	__u32 guest_svn;
	__u64 tcb_version;
};

struct snp_derived_key_resp {
	/* response data, see SEV-SNP spec for the format */
	__u8 data[64];
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

struct snp_ext_report_req {
	struct snp_report_req data;

	/* where to copy the certificate blob */
	__u64 certs_address;

	/* length of the certificate blob */
	__u32 certs_len;
};

#define SNP_GUEST_REQ_IOC_TYPE	'S'

/* Get SNP attestation report */
#define SNP_GET_REPORT _IOWR(SNP_GUEST_REQ_IOC_TYPE, 0x0, struct snp_guest_request_ioctl)

/* Get a derived key from the root */
#define SNP_GET_DERIVED_KEY _IOWR(SNP_GUEST_REQ_IOC_TYPE, 0x1, struct snp_guest_request_ioctl)

/* Get SNP extended report as defined in the GHCB specification version 2. */
#define SNP_GET_EXT_REPORT _IOWR(SNP_GUEST_REQ_IOC_TYPE, 0x2, struct snp_guest_request_ioctl)

#endif /* __UAPI_LINUX_SEV_GUEST_H_ */
