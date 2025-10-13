/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Userspace interface for AMD Secure Encrypted Virtualization (SEV)
 * platform management commands.
 *
 * Copyright (C) 2016-2017 Advanced Micro Devices, Inc.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 *
 * SEV API specification is available at: https://developer.amd.com/sev/
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
	SEV_GET_ID,	/* This command is deprecated, use SEV_GET_ID2 */
	SEV_GET_ID2,
	SNP_PLATFORM_STATUS,
	SNP_COMMIT,
	SNP_SET_CONFIG,
	SNP_VLEK_LOAD,

	SEV_MAX,
};

/**
 * SEV Firmware status code
 */
typedef enum {
	/*
	 * This error code is not in the SEV spec. Its purpose is to convey that
	 * there was an error that prevented the SEV firmware from being called.
	 * The SEV API error codes are 16 bits, so the -1 value will not overlap
	 * with possible values from the specification.
	 */
	SEV_RET_NO_FW_CALL = -1,
	SEV_RET_SUCCESS = 0,
	SEV_RET_INVALID_PLATFORM_STATE,
	SEV_RET_INVALID_GUEST_STATE,
	SEV_RET_INAVLID_CONFIG,
	SEV_RET_INVALID_CONFIG = SEV_RET_INAVLID_CONFIG,
	SEV_RET_INVALID_LEN,
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
	SEV_RET_INVALID_PARAM,
	SEV_RET_RESOURCE_LIMIT,
	SEV_RET_SECURE_DATA_INVALID,
	SEV_RET_INVALID_PAGE_SIZE          = 0x0019,
	SEV_RET_INVALID_PAGE_STATE         = 0x001A,
	SEV_RET_INVALID_MDATA_ENTRY        = 0x001B,
	SEV_RET_INVALID_PAGE_OWNER         = 0x001C,
	SEV_RET_AEAD_OFLOW                 = 0x001D,
	SEV_RET_EXIT_RING_BUFFER           = 0x001F,
	SEV_RET_RMP_INIT_REQUIRED          = 0x0020,
	SEV_RET_BAD_SVN                    = 0x0021,
	SEV_RET_BAD_VERSION                = 0x0022,
	SEV_RET_SHUTDOWN_REQUIRED          = 0x0023,
	SEV_RET_UPDATE_FAILED              = 0x0024,
	SEV_RET_RESTORE_REQUIRED           = 0x0025,
	SEV_RET_RMP_INITIALIZATION_FAILED  = 0x0026,
	SEV_RET_INVALID_KEY                = 0x0027,
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

#define SEV_STATUS_FLAGS_CONFIG_ES	0x0100

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
 * struct sev_user_data_get_id - GET_ID command parameters (deprecated)
 *
 * @socket1: Buffer to pass unique ID of first socket
 * @socket2: Buffer to pass unique ID of second socket
 */
struct sev_user_data_get_id {
	__u8 socket1[64];			/* Out */
	__u8 socket2[64];			/* Out */
} __packed;

/**
 * struct sev_user_data_get_id2 - GET_ID command parameters
 * @address: Buffer to store unique ID
 * @length: length of the unique ID
 */
struct sev_user_data_get_id2 {
	__u64 address;				/* In */
	__u32 length;				/* In/Out */
} __packed;

/**
 * struct sev_user_data_snp_status - SNP status
 *
 * @api_major: API major version
 * @api_minor: API minor version
 * @state: current platform state
 * @is_rmp_initialized: whether RMP is initialized or not
 * @rsvd: reserved
 * @build_id: firmware build id for the API version
 * @mask_chip_id: whether chip id is present in attestation reports or not
 * @mask_chip_key: whether attestation reports are signed or not
 * @vlek_en: VLEK (Version Loaded Endorsement Key) hashstick is loaded
 * @feature_info: whether SNP_FEATURE_INFO command is available
 * @rapl_dis: whether RAPL is disabled
 * @ciphertext_hiding_cap: whether platform has ciphertext hiding capability
 * @ciphertext_hiding_en: whether ciphertext hiding is enabled
 * @rsvd1: reserved
 * @guest_count: the number of guest currently managed by the firmware
 * @current_tcb_version: current TCB version
 * @reported_tcb_version: reported TCB version
 */
struct sev_user_data_snp_status {
	__u8 api_major;			/* Out */
	__u8 api_minor;			/* Out */
	__u8 state;			/* Out */
	__u8 is_rmp_initialized:1;	/* Out */
	__u8 rsvd:7;
	__u32 build_id;			/* Out */
	__u32 mask_chip_id:1;		/* Out */
	__u32 mask_chip_key:1;		/* Out */
	__u32 vlek_en:1;		/* Out */
	__u32 feature_info:1;		/* Out */
	__u32 rapl_dis:1;		/* Out */
	__u32 ciphertext_hiding_cap:1;	/* Out */
	__u32 ciphertext_hiding_en:1;	/* Out */
	__u32 rsvd1:25;
	__u32 guest_count;		/* Out */
	__u64 current_tcb_version;	/* Out */
	__u64 reported_tcb_version;	/* Out */
} __packed;

/**
 * struct sev_user_data_snp_config - system wide configuration value for SNP.
 *
 * @reported_tcb: the TCB version to report in the guest attestation report.
 * @mask_chip_id: whether chip id is present in attestation reports or not
 * @mask_chip_key: whether attestation reports are signed or not
 * @rsvd: reserved
 * @rsvd1: reserved
 */
struct sev_user_data_snp_config {
	__u64 reported_tcb  ;   /* In */
	__u32 mask_chip_id:1;   /* In */
	__u32 mask_chip_key:1;  /* In */
	__u32 rsvd:30;          /* In */
	__u8 rsvd1[52];
} __packed;

/**
 * struct sev_data_snp_vlek_load - SNP_VLEK_LOAD structure
 *
 * @len: length of the command buffer read by the PSP
 * @vlek_wrapped_version: version of wrapped VLEK hashstick (Must be 0h)
 * @rsvd: reserved
 * @vlek_wrapped_address: address of a wrapped VLEK hashstick
 *                        (struct sev_user_data_snp_wrapped_vlek_hashstick)
 */
struct sev_user_data_snp_vlek_load {
	__u32 len;				/* In */
	__u8 vlek_wrapped_version;		/* In */
	__u8 rsvd[3];				/* In */
	__u64 vlek_wrapped_address;		/* In */
} __packed;

/**
 * struct sev_user_data_snp_vlek_wrapped_vlek_hashstick - Wrapped VLEK data
 *
 * @data: Opaque data provided by AMD KDS (as described in SEV-SNP Firmware ABI
 *        1.54, SNP_VLEK_LOAD)
 */
struct sev_user_data_snp_wrapped_vlek_hashstick {
	__u8 data[432];				/* In */
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
