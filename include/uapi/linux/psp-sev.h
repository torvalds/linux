/*
 * Userspace interface for AMD Secure Encrypted Virtualization (SEV)
 * platform management commands.
 *
 * Copyright (C) 2016-2017 Advanced Micro Devices, Inc.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 *
 * SEV spec 0.14 is available at:
 * http://support.amd.com/TechDocs/55766_SEV-KM%20API_Specification.pdf
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PSP_SEV_USER_H__
#define __PSP_SEV_USER_H__

#include <linux/types.h>

/**
 * SEV platform commands
 */
enum {
	SEV_FACTORY_RESET = 0,
	SEV_PLATFORM_STATUS,
	SEV_PEK_GEN,
	SEV_PEK_CSR,
	SEV_PDH_GEN,
	SEV_PDH_CERT_EXPORT,
	SEV_PEK_CERT_IMPORT,

	SEV_MAX,
};

/**
 * SEV Firmware status code
 */
typedef enum {
	SEV_RET_SUCCESS = 0,
	SEV_RET_INVALID_PLATFORM_STATE,
	SEV_RET_INVALID_GUEST_STATE,
	SEV_RET_INAVLID_CONFIG,
	SEV_RET_INVALID_len,
	SEV_RET_ALREADY_OWNED,
	SEV_RET_INVALID_CERTIFICATE,
	SEV_RET_POLICY_FAILURE,
	SEV_RET_INACTIVE,
	SEV_RET_INVALID_ADDRESS,
	SEV_RET_BAD_SIGNATURE,
	SEV_RET_BAD_MEASUREMENT,
	SEV_RET_ASID_OWNED,
	SEV_RET_INVALID_ASID,
	SEV_RET_WBINVD_REQUIRED,
	SEV_RET_DFFLUSH_REQUIRED,
	SEV_RET_INVALID_GUEST,
	SEV_RET_INVALID_COMMAND,
	SEV_RET_ACTIVE,
	SEV_RET_HWSEV_RET_PLATFORM,
	SEV_RET_HWSEV_RET_UNSAFE,
	SEV_RET_UNSUPPORTED,
	SEV_RET_MAX,
} sev_ret_code;

/**
 * struct sev_user_data_status - PLATFORM_STATUS command parameters
 *
 * @major: major API version
 * @minor: minor API version
 * @state: platform state
 * @flags: platform config flags
 * @build: firmware build id for API version
 * @guest_count: number of active guests
 */
struct sev_user_data_status {
	__u8 api_major;				/* Out */
	__u8 api_minor;				/* Out */
	__u8 state;				/* Out */
	__u32 flags;				/* Out */
	__u8 build;				/* Out */
	__u32 guest_count;			/* Out */
} __packed;

/**
 * struct sev_user_data_pek_csr - PEK_CSR command parameters
 *
 * @address: PEK certificate chain
 * @length: length of certificate
 */
struct sev_user_data_pek_csr {
	__u64 address;				/* In */
	__u32 length;				/* In/Out */
} __packed;

/**
 * struct sev_user_data_cert_import - PEK_CERT_IMPORT command parameters
 *
 * @pek_address: PEK certificate chain
 * @pek_len: length of PEK certificate
 * @oca_address: OCA certificate chain
 * @oca_len: length of OCA certificate
 */
struct sev_user_data_pek_cert_import {
	__u64 pek_cert_address;			/* In */
	__u32 pek_cert_len;			/* In */
	__u64 oca_cert_address;			/* In */
	__u32 oca_cert_len;			/* In */
} __packed;

/**
 * struct sev_user_data_pdh_cert_export - PDH_CERT_EXPORT command parameters
 *
 * @pdh_address: PDH certificate address
 * @pdh_len: length of PDH certificate
 * @cert_chain_address: PDH certificate chain
 * @cert_chain_len: length of PDH certificate chain
 */
struct sev_user_data_pdh_cert_export {
	__u64 pdh_cert_address;			/* In */
	__u32 pdh_cert_len;			/* In/Out */
	__u64 cert_chain_address;		/* In */
	__u32 cert_chain_len;			/* In/Out */
} __packed;

/**
 * struct sev_issue_cmd - SEV ioctl parameters
 *
 * @cmd: SEV commands to execute
 * @opaque: pointer to the command structure
 * @error: SEV FW return code on failure
 */
struct sev_issue_cmd {
	__u32 cmd;				/* In */
	__u64 data;				/* In */
	__u32 error;				/* Out */
} __packed;

#define SEV_IOC_TYPE		'S'
#define SEV_ISSUE_CMD	_IOWR(SEV_IOC_TYPE, 0x0, struct sev_issue_cmd)

#endif /* __PSP_USER_SEV_H */
