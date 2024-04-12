/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Secure Encrypted Virtualization (SEV) driver interface
 *
 * Copyright (C) 2016-2017 Advanced Micro Devices, Inc.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 *
 * SEV API spec is available at https://developer.amd.com/sev
 */

#ifndef __PSP_SEV_H__
#define __PSP_SEV_H__

#include <uapi/linux/psp-sev.h>

#define SEV_FW_BLOB_MAX_SIZE	0x4000	/* 16KB */

/**
 * SEV platform state
 */
enum sev_state {
	SEV_STATE_UNINIT		= 0x0,
	SEV_STATE_INIT			= 0x1,
	SEV_STATE_WORKING		= 0x2,

	SEV_STATE_MAX
};

/**
 * SEV platform and guest management commands
 */
enum sev_cmd {
	/* platform commands */
	SEV_CMD_INIT			= 0x001,
	SEV_CMD_SHUTDOWN		= 0x002,
	SEV_CMD_FACTORY_RESET		= 0x003,
	SEV_CMD_PLATFORM_STATUS		= 0x004,
	SEV_CMD_PEK_GEN			= 0x005,
	SEV_CMD_PEK_CSR			= 0x006,
	SEV_CMD_PEK_CERT_IMPORT		= 0x007,
	SEV_CMD_PDH_CERT_EXPORT		= 0x008,
	SEV_CMD_PDH_GEN			= 0x009,
	SEV_CMD_DF_FLUSH		= 0x00A,
	SEV_CMD_DOWNLOAD_FIRMWARE	= 0x00B,
	SEV_CMD_GET_ID			= 0x00C,
	SEV_CMD_INIT_EX                 = 0x00D,

	/* Guest commands */
	SEV_CMD_DECOMMISSION		= 0x020,
	SEV_CMD_ACTIVATE		= 0x021,
	SEV_CMD_DEACTIVATE		= 0x022,
	SEV_CMD_GUEST_STATUS		= 0x023,

	/* Guest launch commands */
	SEV_CMD_LAUNCH_START		= 0x030,
	SEV_CMD_LAUNCH_UPDATE_DATA	= 0x031,
	SEV_CMD_LAUNCH_UPDATE_VMSA	= 0x032,
	SEV_CMD_LAUNCH_MEASURE		= 0x033,
	SEV_CMD_LAUNCH_UPDATE_SECRET	= 0x034,
	SEV_CMD_LAUNCH_FINISH		= 0x035,
	SEV_CMD_ATTESTATION_REPORT	= 0x036,

	/* Guest migration commands (outgoing) */
	SEV_CMD_SEND_START		= 0x040,
	SEV_CMD_SEND_UPDATE_DATA	= 0x041,
	SEV_CMD_SEND_UPDATE_VMSA	= 0x042,
	SEV_CMD_SEND_FINISH		= 0x043,
	SEV_CMD_SEND_CANCEL		= 0x044,

	/* Guest migration commands (incoming) */
	SEV_CMD_RECEIVE_START		= 0x050,
	SEV_CMD_RECEIVE_UPDATE_DATA	= 0x051,
	SEV_CMD_RECEIVE_UPDATE_VMSA	= 0x052,
	SEV_CMD_RECEIVE_FINISH		= 0x053,

	/* Guest debug commands */
	SEV_CMD_DBG_DECRYPT		= 0x060,
	SEV_CMD_DBG_ENCRYPT		= 0x061,

	/* SNP specific commands */
	SEV_CMD_SNP_INIT		= 0x081,
	SEV_CMD_SNP_SHUTDOWN		= 0x082,
	SEV_CMD_SNP_PLATFORM_STATUS	= 0x083,
	SEV_CMD_SNP_DF_FLUSH		= 0x084,
	SEV_CMD_SNP_INIT_EX		= 0x085,
	SEV_CMD_SNP_SHUTDOWN_EX		= 0x086,
	SEV_CMD_SNP_DECOMMISSION	= 0x090,
	SEV_CMD_SNP_ACTIVATE		= 0x091,
	SEV_CMD_SNP_GUEST_STATUS	= 0x092,
	SEV_CMD_SNP_GCTX_CREATE		= 0x093,
	SEV_CMD_SNP_GUEST_REQUEST	= 0x094,
	SEV_CMD_SNP_ACTIVATE_EX		= 0x095,
	SEV_CMD_SNP_LAUNCH_START	= 0x0A0,
	SEV_CMD_SNP_LAUNCH_UPDATE	= 0x0A1,
	SEV_CMD_SNP_LAUNCH_FINISH	= 0x0A2,
	SEV_CMD_SNP_DBG_DECRYPT		= 0x0B0,
	SEV_CMD_SNP_DBG_ENCRYPT		= 0x0B1,
	SEV_CMD_SNP_PAGE_SWAP_OUT	= 0x0C0,
	SEV_CMD_SNP_PAGE_SWAP_IN	= 0x0C1,
	SEV_CMD_SNP_PAGE_MOVE		= 0x0C2,
	SEV_CMD_SNP_PAGE_MD_INIT	= 0x0C3,
	SEV_CMD_SNP_PAGE_SET_STATE	= 0x0C6,
	SEV_CMD_SNP_PAGE_RECLAIM	= 0x0C7,
	SEV_CMD_SNP_PAGE_UNSMASH	= 0x0C8,
	SEV_CMD_SNP_CONFIG		= 0x0C9,
	SEV_CMD_SNP_DOWNLOAD_FIRMWARE_EX = 0x0CA,
	SEV_CMD_SNP_COMMIT		= 0x0CB,
	SEV_CMD_SNP_VLEK_LOAD		= 0x0CD,

	SEV_CMD_MAX,
};

/**
 * struct sev_data_init - INIT command parameters
 *
 * @flags: processing flags
 * @tmr_address: system physical address used for SEV-ES
 * @tmr_len: len of tmr_address
 */
struct sev_data_init {
	u32 flags;			/* In */
	u32 reserved;			/* In */
	u64 tmr_address;		/* In */
	u32 tmr_len;			/* In */
} __packed;

/**
 * struct sev_data_init_ex - INIT_EX command parameters
 *
 * @length: len of the command buffer read by the PSP
 * @flags: processing flags
 * @tmr_address: system physical address used for SEV-ES
 * @tmr_len: len of tmr_address
 * @nv_address: system physical address used for PSP NV storage
 * @nv_len: len of nv_address
 */
struct sev_data_init_ex {
	u32 length;                     /* In */
	u32 flags;                      /* In */
	u64 tmr_address;                /* In */
	u32 tmr_len;                    /* In */
	u32 reserved;                   /* In */
	u64 nv_address;                 /* In/Out */
	u32 nv_len;                     /* In */
} __packed;

#define SEV_INIT_FLAGS_SEV_ES	0x01

/**
 * struct sev_data_pek_csr - PEK_CSR command parameters
 *
 * @address: PEK certificate chain
 * @len: len of certificate
 */
struct sev_data_pek_csr {
	u64 address;				/* In */
	u32 len;				/* In/Out */
} __packed;

/**
 * struct sev_data_cert_import - PEK_CERT_IMPORT command parameters
 *
 * @pek_address: PEK certificate chain
 * @pek_len: len of PEK certificate
 * @oca_address: OCA certificate chain
 * @oca_len: len of OCA certificate
 */
struct sev_data_pek_cert_import {
	u64 pek_cert_address;			/* In */
	u32 pek_cert_len;			/* In */
	u32 reserved;				/* In */
	u64 oca_cert_address;			/* In */
	u32 oca_cert_len;			/* In */
} __packed;

/**
 * struct sev_data_download_firmware - DOWNLOAD_FIRMWARE command parameters
 *
 * @address: physical address of firmware image
 * @len: len of the firmware image
 */
struct sev_data_download_firmware {
	u64 address;				/* In */
	u32 len;				/* In */
} __packed;

/**
 * struct sev_data_get_id - GET_ID command parameters
 *
 * @address: physical address of region to place unique CPU ID(s)
 * @len: len of the region
 */
struct sev_data_get_id {
	u64 address;				/* In */
	u32 len;				/* In/Out */
} __packed;
/**
 * struct sev_data_pdh_cert_export - PDH_CERT_EXPORT command parameters
 *
 * @pdh_address: PDH certificate address
 * @pdh_len: len of PDH certificate
 * @cert_chain_address: PDH certificate chain
 * @cert_chain_len: len of PDH certificate chain
 */
struct sev_data_pdh_cert_export {
	u64 pdh_cert_address;			/* In */
	u32 pdh_cert_len;			/* In/Out */
	u32 reserved;				/* In */
	u64 cert_chain_address;			/* In */
	u32 cert_chain_len;			/* In/Out */
} __packed;

/**
 * struct sev_data_decommission - DECOMMISSION command parameters
 *
 * @handle: handle of the VM to decommission
 */
struct sev_data_decommission {
	u32 handle;				/* In */
} __packed;

/**
 * struct sev_data_activate - ACTIVATE command parameters
 *
 * @handle: handle of the VM to activate
 * @asid: asid assigned to the VM
 */
struct sev_data_activate {
	u32 handle;				/* In */
	u32 asid;				/* In */
} __packed;

/**
 * struct sev_data_deactivate - DEACTIVATE command parameters
 *
 * @handle: handle of the VM to deactivate
 */
struct sev_data_deactivate {
	u32 handle;				/* In */
} __packed;

/**
 * struct sev_data_guest_status - SEV GUEST_STATUS command parameters
 *
 * @handle: handle of the VM to retrieve status
 * @policy: policy information for the VM
 * @asid: current ASID of the VM
 * @state: current state of the VM
 */
struct sev_data_guest_status {
	u32 handle;				/* In */
	u32 policy;				/* Out */
	u32 asid;				/* Out */
	u8 state;				/* Out */
} __packed;

/**
 * struct sev_data_launch_start - LAUNCH_START command parameters
 *
 * @handle: handle assigned to the VM
 * @policy: guest launch policy
 * @dh_cert_address: physical address of DH certificate blob
 * @dh_cert_len: len of DH certificate blob
 * @session_address: physical address of session parameters
 * @session_len: len of session parameters
 */
struct sev_data_launch_start {
	u32 handle;				/* In/Out */
	u32 policy;				/* In */
	u64 dh_cert_address;			/* In */
	u32 dh_cert_len;			/* In */
	u32 reserved;				/* In */
	u64 session_address;			/* In */
	u32 session_len;			/* In */
} __packed;

/**
 * struct sev_data_launch_update_data - LAUNCH_UPDATE_DATA command parameter
 *
 * @handle: handle of the VM to update
 * @len: len of memory to be encrypted
 * @address: physical address of memory region to encrypt
 */
struct sev_data_launch_update_data {
	u32 handle;				/* In */
	u32 reserved;
	u64 address;				/* In */
	u32 len;				/* In */
} __packed;

/**
 * struct sev_data_launch_update_vmsa - LAUNCH_UPDATE_VMSA command
 *
 * @handle: handle of the VM
 * @address: physical address of memory region to encrypt
 * @len: len of memory region to encrypt
 */
struct sev_data_launch_update_vmsa {
	u32 handle;				/* In */
	u32 reserved;
	u64 address;				/* In */
	u32 len;				/* In */
} __packed;

/**
 * struct sev_data_launch_measure - LAUNCH_MEASURE command parameters
 *
 * @handle: handle of the VM to process
 * @address: physical address containing the measurement blob
 * @len: len of measurement blob
 */
struct sev_data_launch_measure {
	u32 handle;				/* In */
	u32 reserved;
	u64 address;				/* In */
	u32 len;				/* In/Out */
} __packed;

/**
 * struct sev_data_launch_secret - LAUNCH_SECRET command parameters
 *
 * @handle: handle of the VM to process
 * @hdr_address: physical address containing the packet header
 * @hdr_len: len of packet header
 * @guest_address: system physical address of guest memory region
 * @guest_len: len of guest_paddr
 * @trans_address: physical address of transport memory buffer
 * @trans_len: len of transport memory buffer
 */
struct sev_data_launch_secret {
	u32 handle;				/* In */
	u32 reserved1;
	u64 hdr_address;			/* In */
	u32 hdr_len;				/* In */
	u32 reserved2;
	u64 guest_address;			/* In */
	u32 guest_len;				/* In */
	u32 reserved3;
	u64 trans_address;			/* In */
	u32 trans_len;				/* In */
} __packed;

/**
 * struct sev_data_launch_finish - LAUNCH_FINISH command parameters
 *
 * @handle: handle of the VM to process
 */
struct sev_data_launch_finish {
	u32 handle;				/* In */
} __packed;

/**
 * struct sev_data_send_start - SEND_START command parameters
 *
 * @handle: handle of the VM to process
 * @policy: policy information for the VM
 * @pdh_cert_address: physical address containing PDH certificate
 * @pdh_cert_len: len of PDH certificate
 * @plat_certs_address: physical address containing platform certificate
 * @plat_certs_len: len of platform certificate
 * @amd_certs_address: physical address containing AMD certificate
 * @amd_certs_len: len of AMD certificate
 * @session_address: physical address containing Session data
 * @session_len: len of session data
 */
struct sev_data_send_start {
	u32 handle;				/* In */
	u32 policy;				/* Out */
	u64 pdh_cert_address;			/* In */
	u32 pdh_cert_len;			/* In */
	u32 reserved1;
	u64 plat_certs_address;			/* In */
	u32 plat_certs_len;			/* In */
	u32 reserved2;
	u64 amd_certs_address;			/* In */
	u32 amd_certs_len;			/* In */
	u32 reserved3;
	u64 session_address;			/* In */
	u32 session_len;			/* In/Out */
} __packed;

/**
 * struct sev_data_send_update - SEND_UPDATE_DATA command
 *
 * @handle: handle of the VM to process
 * @hdr_address: physical address containing packet header
 * @hdr_len: len of packet header
 * @guest_address: physical address of guest memory region to send
 * @guest_len: len of guest memory region to send
 * @trans_address: physical address of host memory region
 * @trans_len: len of host memory region
 */
struct sev_data_send_update_data {
	u32 handle;				/* In */
	u32 reserved1;
	u64 hdr_address;			/* In */
	u32 hdr_len;				/* In/Out */
	u32 reserved2;
	u64 guest_address;			/* In */
	u32 guest_len;				/* In */
	u32 reserved3;
	u64 trans_address;			/* In */
	u32 trans_len;				/* In */
} __packed;

/**
 * struct sev_data_send_update - SEND_UPDATE_VMSA command
 *
 * @handle: handle of the VM to process
 * @hdr_address: physical address containing packet header
 * @hdr_len: len of packet header
 * @guest_address: physical address of guest memory region to send
 * @guest_len: len of guest memory region to send
 * @trans_address: physical address of host memory region
 * @trans_len: len of host memory region
 */
struct sev_data_send_update_vmsa {
	u32 handle;				/* In */
	u64 hdr_address;			/* In */
	u32 hdr_len;				/* In/Out */
	u32 reserved2;
	u64 guest_address;			/* In */
	u32 guest_len;				/* In */
	u32 reserved3;
	u64 trans_address;			/* In */
	u32 trans_len;				/* In */
} __packed;

/**
 * struct sev_data_send_finish - SEND_FINISH command parameters
 *
 * @handle: handle of the VM to process
 */
struct sev_data_send_finish {
	u32 handle;				/* In */
} __packed;

/**
 * struct sev_data_send_cancel - SEND_CANCEL command parameters
 *
 * @handle: handle of the VM to process
 */
struct sev_data_send_cancel {
	u32 handle;				/* In */
} __packed;

/**
 * struct sev_data_receive_start - RECEIVE_START command parameters
 *
 * @handle: handle of the VM to perform receive operation
 * @pdh_cert_address: system physical address containing PDH certificate blob
 * @pdh_cert_len: len of PDH certificate blob
 * @session_address: system physical address containing session blob
 * @session_len: len of session blob
 */
struct sev_data_receive_start {
	u32 handle;				/* In/Out */
	u32 policy;				/* In */
	u64 pdh_cert_address;			/* In */
	u32 pdh_cert_len;			/* In */
	u32 reserved1;
	u64 session_address;			/* In */
	u32 session_len;			/* In */
} __packed;

/**
 * struct sev_data_receive_update_data - RECEIVE_UPDATE_DATA command parameters
 *
 * @handle: handle of the VM to update
 * @hdr_address: physical address containing packet header blob
 * @hdr_len: len of packet header
 * @guest_address: system physical address of guest memory region
 * @guest_len: len of guest memory region
 * @trans_address: system physical address of transport buffer
 * @trans_len: len of transport buffer
 */
struct sev_data_receive_update_data {
	u32 handle;				/* In */
	u32 reserved1;
	u64 hdr_address;			/* In */
	u32 hdr_len;				/* In */
	u32 reserved2;
	u64 guest_address;			/* In */
	u32 guest_len;				/* In */
	u32 reserved3;
	u64 trans_address;			/* In */
	u32 trans_len;				/* In */
} __packed;

/**
 * struct sev_data_receive_update_vmsa - RECEIVE_UPDATE_VMSA command parameters
 *
 * @handle: handle of the VM to update
 * @hdr_address: physical address containing packet header blob
 * @hdr_len: len of packet header
 * @guest_address: system physical address of guest memory region
 * @guest_len: len of guest memory region
 * @trans_address: system physical address of transport buffer
 * @trans_len: len of transport buffer
 */
struct sev_data_receive_update_vmsa {
	u32 handle;				/* In */
	u32 reserved1;
	u64 hdr_address;			/* In */
	u32 hdr_len;				/* In */
	u32 reserved2;
	u64 guest_address;			/* In */
	u32 guest_len;				/* In */
	u32 reserved3;
	u64 trans_address;			/* In */
	u32 trans_len;				/* In */
} __packed;

/**
 * struct sev_data_receive_finish - RECEIVE_FINISH command parameters
 *
 * @handle: handle of the VM to finish
 */
struct sev_data_receive_finish {
	u32 handle;				/* In */
} __packed;

/**
 * struct sev_data_dbg - DBG_ENCRYPT/DBG_DECRYPT command parameters
 *
 * @handle: handle of the VM to perform debug operation
 * @src_addr: source address of data to operate on
 * @dst_addr: destination address of data to operate on
 * @len: len of data to operate on
 */
struct sev_data_dbg {
	u32 handle;				/* In */
	u32 reserved;
	u64 src_addr;				/* In */
	u64 dst_addr;				/* In */
	u32 len;				/* In */
} __packed;

/**
 * struct sev_data_attestation_report - SEV_ATTESTATION_REPORT command parameters
 *
 * @handle: handle of the VM
 * @mnonce: a random nonce that will be included in the report.
 * @address: physical address where the report will be copied.
 * @len: length of the physical buffer.
 */
struct sev_data_attestation_report {
	u32 handle;				/* In */
	u32 reserved;
	u64 address;				/* In */
	u8 mnonce[16];				/* In */
	u32 len;				/* In/Out */
} __packed;

/**
 * struct sev_data_snp_download_firmware - SNP_DOWNLOAD_FIRMWARE command params
 *
 * @address: physical address of firmware image
 * @len: length of the firmware image
 */
struct sev_data_snp_download_firmware {
	u64 address;				/* In */
	u32 len;				/* In */
} __packed;

/**
 * struct sev_data_snp_activate - SNP_ACTIVATE command params
 *
 * @gctx_paddr: system physical address guest context page
 * @asid: ASID to bind to the guest
 */
struct sev_data_snp_activate {
	u64 gctx_paddr;				/* In */
	u32 asid;				/* In */
} __packed;

/**
 * struct sev_data_snp_addr - generic SNP command params
 *
 * @address: physical address of generic data param
 */
struct sev_data_snp_addr {
	u64 address;				/* In/Out */
} __packed;

/**
 * struct sev_data_snp_launch_start - SNP_LAUNCH_START command params
 *
 * @gctx_paddr: system physical address of guest context page
 * @policy: guest policy
 * @ma_gctx_paddr: system physical address of migration agent
 * @ma_en: the guest is associated with a migration agent
 * @imi_en: launch flow is launching an IMI (Incoming Migration Image) for the
 *          purpose of guest-assisted migration.
 * @rsvd: reserved
 * @gosvw: guest OS-visible workarounds, as defined by hypervisor
 */
struct sev_data_snp_launch_start {
	u64 gctx_paddr;				/* In */
	u64 policy;				/* In */
	u64 ma_gctx_paddr;			/* In */
	u32 ma_en:1;				/* In */
	u32 imi_en:1;				/* In */
	u32 rsvd:30;
	u8 gosvw[16];				/* In */
} __packed;

/* SNP support page type */
enum {
	SNP_PAGE_TYPE_NORMAL		= 0x1,
	SNP_PAGE_TYPE_VMSA		= 0x2,
	SNP_PAGE_TYPE_ZERO		= 0x3,
	SNP_PAGE_TYPE_UNMEASURED	= 0x4,
	SNP_PAGE_TYPE_SECRET		= 0x5,
	SNP_PAGE_TYPE_CPUID		= 0x6,

	SNP_PAGE_TYPE_MAX
};

/**
 * struct sev_data_snp_launch_update - SNP_LAUNCH_UPDATE command params
 *
 * @gctx_paddr: system physical address of guest context page
 * @page_size: page size 0 indicates 4K and 1 indicates 2MB page
 * @page_type: encoded page type
 * @imi_page: indicates that this page is part of the IMI (Incoming Migration
 *            Image) of the guest
 * @rsvd: reserved
 * @rsvd2: reserved
 * @address: system physical address of destination page to encrypt
 * @rsvd3: reserved
 * @vmpl1_perms: VMPL permission mask for VMPL1
 * @vmpl2_perms: VMPL permission mask for VMPL2
 * @vmpl3_perms: VMPL permission mask for VMPL3
 * @rsvd4: reserved
 */
struct sev_data_snp_launch_update {
	u64 gctx_paddr;				/* In */
	u32 page_size:1;			/* In */
	u32 page_type:3;			/* In */
	u32 imi_page:1;				/* In */
	u32 rsvd:27;
	u32 rsvd2;
	u64 address;				/* In */
	u32 rsvd3:8;
	u32 vmpl1_perms:8;			/* In */
	u32 vmpl2_perms:8;			/* In */
	u32 vmpl3_perms:8;			/* In */
	u32 rsvd4;
} __packed;

/**
 * struct sev_data_snp_launch_finish - SNP_LAUNCH_FINISH command params
 *
 * @gctx_paddr: system physical address of guest context page
 * @id_block_paddr: system physical address of ID block
 * @id_auth_paddr: system physical address of ID block authentication structure
 * @id_block_en: indicates whether ID block is present
 * @auth_key_en: indicates whether author key is present in authentication structure
 * @rsvd: reserved
 * @host_data: host-supplied data for guest, not interpreted by firmware
 */
struct sev_data_snp_launch_finish {
	u64 gctx_paddr;
	u64 id_block_paddr;
	u64 id_auth_paddr;
	u8 id_block_en:1;
	u8 auth_key_en:1;
	u64 rsvd:62;
	u8 host_data[32];
} __packed;

/**
 * struct sev_data_snp_guest_status - SNP_GUEST_STATUS command params
 *
 * @gctx_paddr: system physical address of guest context page
 * @address: system physical address of guest status page
 */
struct sev_data_snp_guest_status {
	u64 gctx_paddr;
	u64 address;
} __packed;

/**
 * struct sev_data_snp_page_reclaim - SNP_PAGE_RECLAIM command params
 *
 * @paddr: system physical address of page to be claimed. The 0th bit in the
 *         address indicates the page size. 0h indicates 4KB and 1h indicates
 *         2MB page.
 */
struct sev_data_snp_page_reclaim {
	u64 paddr;
} __packed;

/**
 * struct sev_data_snp_page_unsmash - SNP_PAGE_UNSMASH command params
 *
 * @paddr: system physical address of page to be unsmashed. The 0th bit in the
 *         address indicates the page size. 0h indicates 4 KB and 1h indicates
 *         2 MB page.
 */
struct sev_data_snp_page_unsmash {
	u64 paddr;
} __packed;

/**
 * struct sev_data_snp_dbg - DBG_ENCRYPT/DBG_DECRYPT command parameters
 *
 * @gctx_paddr: system physical address of guest context page
 * @src_addr: source address of data to operate on
 * @dst_addr: destination address of data to operate on
 */
struct sev_data_snp_dbg {
	u64 gctx_paddr;				/* In */
	u64 src_addr;				/* In */
	u64 dst_addr;				/* In */
} __packed;

/**
 * struct sev_data_snp_guest_request - SNP_GUEST_REQUEST command params
 *
 * @gctx_paddr: system physical address of guest context page
 * @req_paddr: system physical address of request page
 * @res_paddr: system physical address of response page
 */
struct sev_data_snp_guest_request {
	u64 gctx_paddr;				/* In */
	u64 req_paddr;				/* In */
	u64 res_paddr;				/* In */
} __packed;

/**
 * struct sev_data_snp_init_ex - SNP_INIT_EX structure
 *
 * @init_rmp: indicate that the RMP should be initialized.
 * @list_paddr_en: indicate that list_paddr is valid
 * @rsvd: reserved
 * @rsvd1: reserved
 * @list_paddr: system physical address of range list
 * @rsvd2: reserved
 */
struct sev_data_snp_init_ex {
	u32 init_rmp:1;
	u32 list_paddr_en:1;
	u32 rsvd:30;
	u32 rsvd1;
	u64 list_paddr;
	u8  rsvd2[48];
} __packed;

/**
 * struct sev_data_range - RANGE structure
 *
 * @base: system physical address of first byte of range
 * @page_count: number of 4KB pages in this range
 * @rsvd: reserved
 */
struct sev_data_range {
	u64 base;
	u32 page_count;
	u32 rsvd;
} __packed;

/**
 * struct sev_data_range_list - RANGE_LIST structure
 *
 * @num_elements: number of elements in RANGE_ARRAY
 * @rsvd: reserved
 * @ranges: array of num_elements of type RANGE
 */
struct sev_data_range_list {
	u32 num_elements;
	u32 rsvd;
	struct sev_data_range ranges[];
} __packed;

/**
 * struct sev_data_snp_shutdown_ex - SNP_SHUTDOWN_EX structure
 *
 * @len: length of the command buffer read by the PSP
 * @iommu_snp_shutdown: Disable enforcement of SNP in the IOMMU
 * @rsvd1: reserved
 */
struct sev_data_snp_shutdown_ex {
	u32 len;
	u32 iommu_snp_shutdown:1;
	u32 rsvd1:31;
} __packed;

/**
 * struct sev_platform_init_args
 *
 * @error: SEV firmware error code
 * @probe: True if this is being called as part of CCP module probe, which
 *  will defer SEV_INIT/SEV_INIT_EX firmware initialization until needed
 *  unless psp_init_on_probe module param is set
 */
struct sev_platform_init_args {
	int error;
	bool probe;
};

/**
 * struct sev_data_snp_commit - SNP_COMMIT structure
 *
 * @len: length of the command buffer read by the PSP
 */
struct sev_data_snp_commit {
	u32 len;
} __packed;

#ifdef CONFIG_CRYPTO_DEV_SP_PSP

/**
 * sev_platform_init - perform SEV INIT command
 *
 * @args: struct sev_platform_init_args to pass in arguments
 *
 * Returns:
 * 0 if the SEV successfully processed the command
 * -%ENODEV    if the SEV device is not available
 * -%ENOTSUPP  if the SEV does not support SEV
 * -%ETIMEDOUT if the SEV command timed out
 * -%EIO       if the SEV returned a non-zero return code
 */
int sev_platform_init(struct sev_platform_init_args *args);

/**
 * sev_platform_status - perform SEV PLATFORM_STATUS command
 *
 * @status: sev_user_data_status structure to be processed
 * @error: SEV command return code
 *
 * Returns:
 * 0 if the SEV successfully processed the command
 * -%ENODEV    if the SEV device is not available
 * -%ENOTSUPP  if the SEV does not support SEV
 * -%ETIMEDOUT if the SEV command timed out
 * -%EIO       if the SEV returned a non-zero return code
 */
int sev_platform_status(struct sev_user_data_status *status, int *error);

/**
 * sev_issue_cmd_external_user - issue SEV command by other driver with a file
 * handle.
 *
 * This function can be used by other drivers to issue a SEV command on
 * behalf of userspace. The caller must pass a valid SEV file descriptor
 * so that we know that it has access to SEV device.
 *
 * @filep - SEV device file pointer
 * @cmd - command to issue
 * @data - command buffer
 * @error: SEV command return code
 *
 * Returns:
 * 0 if the SEV successfully processed the command
 * -%ENODEV    if the SEV device is not available
 * -%ENOTSUPP  if the SEV does not support SEV
 * -%ETIMEDOUT if the SEV command timed out
 * -%EIO       if the SEV returned a non-zero return code
 * -%EINVAL    if the SEV file descriptor is not valid
 */
int sev_issue_cmd_external_user(struct file *filep, unsigned int id,
				void *data, int *error);

/**
 * sev_guest_deactivate - perform SEV DEACTIVATE command
 *
 * @deactivate: sev_data_deactivate structure to be processed
 * @sev_ret: sev command return code
 *
 * Returns:
 * 0 if the sev successfully processed the command
 * -%ENODEV    if the sev device is not available
 * -%ENOTSUPP  if the sev does not support SEV
 * -%ETIMEDOUT if the sev command timed out
 * -%EIO       if the sev returned a non-zero return code
 */
int sev_guest_deactivate(struct sev_data_deactivate *data, int *error);

/**
 * sev_guest_activate - perform SEV ACTIVATE command
 *
 * @activate: sev_data_activate structure to be processed
 * @sev_ret: sev command return code
 *
 * Returns:
 * 0 if the sev successfully processed the command
 * -%ENODEV    if the sev device is not available
 * -%ENOTSUPP  if the sev does not support SEV
 * -%ETIMEDOUT if the sev command timed out
 * -%EIO       if the sev returned a non-zero return code
 */
int sev_guest_activate(struct sev_data_activate *data, int *error);

/**
 * sev_guest_df_flush - perform SEV DF_FLUSH command
 *
 * @sev_ret: sev command return code
 *
 * Returns:
 * 0 if the sev successfully processed the command
 * -%ENODEV    if the sev device is not available
 * -%ENOTSUPP  if the sev does not support SEV
 * -%ETIMEDOUT if the sev command timed out
 * -%EIO       if the sev returned a non-zero return code
 */
int sev_guest_df_flush(int *error);

/**
 * sev_guest_decommission - perform SEV DECOMMISSION command
 *
 * @decommission: sev_data_decommission structure to be processed
 * @sev_ret: sev command return code
 *
 * Returns:
 * 0 if the sev successfully processed the command
 * -%ENODEV    if the sev device is not available
 * -%ENOTSUPP  if the sev does not support SEV
 * -%ETIMEDOUT if the sev command timed out
 * -%EIO       if the sev returned a non-zero return code
 */
int sev_guest_decommission(struct sev_data_decommission *data, int *error);

/**
 * sev_do_cmd - issue an SEV or an SEV-SNP command
 *
 * @cmd: SEV or SEV-SNP firmware command to issue
 * @data: arguments for firmware command
 * @psp_ret: SEV command return code
 *
 * Returns:
 * 0 if the SEV device successfully processed the command
 * -%ENODEV    if the PSP device is not available
 * -%ENOTSUPP  if PSP device does not support SEV
 * -%ETIMEDOUT if the SEV command timed out
 * -%EIO       if PSP device returned a non-zero return code
 */
int sev_do_cmd(int cmd, void *data, int *psp_ret);

void *psp_copy_user_blob(u64 uaddr, u32 len);
void *snp_alloc_firmware_page(gfp_t mask);
void snp_free_firmware_page(void *addr);

#else	/* !CONFIG_CRYPTO_DEV_SP_PSP */

static inline int
sev_platform_status(struct sev_user_data_status *status, int *error) { return -ENODEV; }

static inline int sev_platform_init(struct sev_platform_init_args *args) { return -ENODEV; }

static inline int
sev_guest_deactivate(struct sev_data_deactivate *data, int *error) { return -ENODEV; }

static inline int
sev_guest_decommission(struct sev_data_decommission *data, int *error) { return -ENODEV; }

static inline int
sev_do_cmd(int cmd, void *data, int *psp_ret) { return -ENODEV; }

static inline int
sev_guest_activate(struct sev_data_activate *data, int *error) { return -ENODEV; }

static inline int sev_guest_df_flush(int *error) { return -ENODEV; }

static inline int
sev_issue_cmd_external_user(struct file *filep, unsigned int id, void *data, int *error) { return -ENODEV; }

static inline void *psp_copy_user_blob(u64 __user uaddr, u32 len) { return ERR_PTR(-EINVAL); }

static inline void *snp_alloc_firmware_page(gfp_t mask)
{
	return NULL;
}

static inline void snp_free_firmware_page(void *addr) { }

#endif	/* CONFIG_CRYPTO_DEV_SP_PSP */

#endif	/* __PSP_SEV_H__ */
