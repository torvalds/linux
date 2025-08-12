/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef __LIBIE_ADMINQ_H
#define __LIBIE_ADMINQ_H

#include <linux/build_bug.h>
#include <linux/types.h>

#define LIBIE_CHECK_STRUCT_LEN(n, X)	\
	static_assert((n) == sizeof(struct X))
#define LIBIE_AQ_MAX_BUF_LEN 4096

/**
 * struct libie_aqc_generic - Generic structure used in adminq communication
 * @param0: generic parameter high 32bit
 * @param1: generic parameter lower 32bit
 * @addr_high: generic address high 32bit
 * @addr_low: generic address lower 32bit
 */
struct libie_aqc_generic {
	__le32 param0;
	__le32 param1;
	__le32 addr_high;
	__le32 addr_low;
};
LIBIE_CHECK_STRUCT_LEN(16, libie_aqc_generic);

/**
 * struct libie_aqc_get_ver -  Used in command get version (direct 0x0001)
 * @rom_ver: rom version
 * @fw_build: number coressponding to firmware build
 * @fw_branch: branch identifier of firmware version
 * @fw_major: major number of firmware version
 * @fw_minor: minor number of firmware version
 * @fw_patch: patch of firmware version
 * @api_branch: brancch identifier of API version
 * @api_major: major number of API version
 * @api_minor: minor number of API version
 * @api_patch: patch of API version
 */
struct libie_aqc_get_ver {
	__le32	rom_ver;
	__le32	fw_build;
	u8	fw_branch;
	u8	fw_major;
	u8	fw_minor;
	u8	fw_patch;
	u8	api_branch;
	u8	api_major;
	u8	api_minor;
	u8	api_patch;
};
LIBIE_CHECK_STRUCT_LEN(16, libie_aqc_get_ver);

/**
 * struct libie_aqc_driver_ver - Used in command send driver version
 *				 (indirect 0x0002)
 * @major_ver: driver major version
 * @minor_ver: driver minor version
 * @build_ver: driver build version
 * @subbuild_ver: driver subbuild version
 * @reserved: for feature use
 * @addr_high: high part of response address buff
 * @addr_low: low part of response address buff
 */
struct libie_aqc_driver_ver {
	u8	major_ver;
	u8	minor_ver;
	u8	build_ver;
	u8	subbuild_ver;
	u8	reserved[4];
	__le32	addr_high;
	__le32	addr_low;
};
LIBIE_CHECK_STRUCT_LEN(16, libie_aqc_driver_ver);

enum libie_aq_res_id {
	LIBIE_AQC_RES_ID_NVM				= 1,
	LIBIE_AQC_RES_ID_SDP				= 2,
	LIBIE_AQC_RES_ID_CHNG_LOCK			= 3,
	LIBIE_AQC_RES_ID_GLBL_LOCK			= 4,
};

enum libie_aq_res_access_type {
	LIBIE_AQC_RES_ACCESS_READ			= 1,
	LIBIE_AQC_RES_ACCESS_WRITE			= 2,
};

#define LIBIE_AQ_RES_NVM_READ_DFLT_TIMEOUT_MS		3000
#define LIBIE_AQ_RES_NVM_WRITE_DFLT_TIMEOUT_MS		180000
#define LIBIE_AQ_RES_CHNG_LOCK_DFLT_TIMEOUT_MS		1000
#define LIBIE_AQ_RES_GLBL_LOCK_DFLT_TIMEOUT_MS		3000

#define LIBIE_AQ_RES_GLBL_SUCCESS			0
#define LIBIE_AQ_RES_GLBL_IN_PROG			1
#define LIBIE_AQ_RES_GLBL_DONE				2

/**
 * struct libie_aqc_req_res - Request resource ownership
 * @res_id: resource ID (look at enum definition above)
 * @access_type: read or write (enum definition above)
 * @timeout: Upon successful completion, FW writes this value and driver is
 * expected to release resource before timeout. This value is provided in
 * milliseconds.
 * @res_number: for SDP, this is the pin ID of the SDP
 * @status: status only used for LIBIE_AQC_RES_ID_GLBL_LOCK, for others reserved
 * @reserved: reserved for future use
 *
 * Used in commands:
 * request resource ownership (direct 0x0008)
 * request resource ownership (direct 0x0009)
 */
struct libie_aqc_req_res {
	__le16	res_id;
	__le16	access_type;

	__le32	timeout;
	__le32	res_number;
	__le16	status;
	u8	reserved[2];
};
LIBIE_CHECK_STRUCT_LEN(16, libie_aqc_req_res);

/**
 * struct libie_aqc_list_caps - Getting capabilities
 * @cmd_flags: command flags
 * @pf_index: index of PF to get caps from
 * @reserved: reserved for future use
 * @count: number of capabilities records
 * @addr_high: high part of response address buff
 * @addr_low: low part of response address buff
 *
 * Used in commands:
 * get function capabilities (indirect 0x000A)
 * get device capabilities (indirect 0x000B)
 */
struct libie_aqc_list_caps {
	u8	cmd_flags;
	u8	pf_index;
	u8	reserved[2];
	__le32	count;
	__le32	addr_high;
	__le32	addr_low;
};
LIBIE_CHECK_STRUCT_LEN(16, libie_aqc_list_caps);

/* Device/Function buffer entry, repeated per reported capability */
#define LIBIE_AQC_CAPS_SWITCH_MODE			0x0001
#define LIBIE_AQC_CAPS_MNG_MODE				0x0002
#define LIBIE_AQC_CAPS_NPAR_ACTIVE			0x0003
#define LIBIE_AQC_CAPS_OS2BMC_CAP			0x0004
#define LIBIE_AQC_CAPS_VALID_FUNCTIONS			0x0005
#define LIBIE_AQC_MAX_VALID_FUNCTIONS			0x8
#define LIBIE_AQC_CAPS_SRIOV				0x0012
#define LIBIE_AQC_CAPS_VF				0x0013
#define LIBIE_AQC_CAPS_VMDQ				0x0014
#define LIBIE_AQC_CAPS_8021QBG				0x0015
#define LIBIE_AQC_CAPS_8021QBR				0x0016
#define LIBIE_AQC_CAPS_VSI				0x0017
#define LIBIE_AQC_CAPS_DCB				0x0018
#define LIBIE_AQC_CAPS_FCOE				0x0021
#define LIBIE_AQC_CAPS_ISCSI				0x0022
#define LIBIE_AQC_CAPS_RSS				0x0040
#define LIBIE_AQC_CAPS_RXQS				0x0041
#define LIBIE_AQC_CAPS_TXQS				0x0042
#define LIBIE_AQC_CAPS_MSIX				0x0043
#define LIBIE_AQC_CAPS_VF_MSIX				0x0044
#define LIBIE_AQC_CAPS_FD				0x0045
#define LIBIE_AQC_CAPS_1588				0x0046
#define LIBIE_AQC_CAPS_MAX_MTU				0x0047
#define LIBIE_AQC_CAPS_NVM_VER				0x0048
#define LIBIE_AQC_CAPS_PENDING_NVM_VER			0x0049
#define LIBIE_AQC_CAPS_OROM_VER				0x004A
#define LIBIE_AQC_CAPS_PENDING_OROM_VER			0x004B
#define LIBIE_AQC_CAPS_NET_VER				0x004C
#define LIBIE_AQC_CAPS_PENDING_NET_VER			0x004D
#define LIBIE_AQC_CAPS_RDMA				0x0051
#define LIBIE_AQC_CAPS_LED				0x0061
#define LIBIE_AQC_CAPS_SDP				0x0062
#define LIBIE_AQC_CAPS_MDIO				0x0063
#define LIBIE_AQC_CAPS_WSR_PROT				0x0064
#define LIBIE_AQC_CAPS_SENSOR_READING			0x0067
#define LIBIE_AQC_INLINE_IPSEC				0x0070
#define LIBIE_AQC_CAPS_NUM_ENABLED_PORTS		0x0072
#define LIBIE_AQC_CAPS_PCIE_RESET_AVOIDANCE		0x0076
#define LIBIE_AQC_CAPS_POST_UPDATE_RESET_RESTRICT	0x0077
#define LIBIE_AQC_CAPS_NVM_MGMT				0x0080
#define LIBIE_AQC_CAPS_EXT_TOPO_DEV_IMG0		0x0081
#define LIBIE_AQC_CAPS_EXT_TOPO_DEV_IMG1		0x0082
#define LIBIE_AQC_CAPS_EXT_TOPO_DEV_IMG2		0x0083
#define LIBIE_AQC_CAPS_EXT_TOPO_DEV_IMG3		0x0084
#define LIBIE_AQC_CAPS_TX_SCHED_TOPO_COMP_MODE		0x0085
#define LIBIE_AQC_CAPS_NAC_TOPOLOGY			0x0087
#define LIBIE_AQC_CAPS_FW_LAG_SUPPORT			0x0092
#define LIBIE_AQC_BIT_ROCEV2_LAG			BIT(0)
#define LIBIE_AQC_BIT_SRIOV_LAG				BIT(1)
#define LIBIE_AQC_BIT_SRIOV_AA_LAG			BIT(2)
#define LIBIE_AQC_CAPS_FLEX10				0x00F1
#define LIBIE_AQC_CAPS_CEM				0x00F2

/**
 * struct libie_aqc_list_caps_elem - Getting list of caps elements
 * @cap: one from the defines list above
 * @major_ver: major version
 * @minor_ver: minor version
 * @number: number of resources described by this capability
 * @logical_id: logical ID, only meaningful for some types of resources
 * @phys_id: physical ID, only meaningful for some types of resources
 * @rsvd1: reserved for future use
 * @rsvd2: reserved for future use
 */
struct libie_aqc_list_caps_elem {
	__le16	cap;

	u8	major_ver;
	u8	minor_ver;
	__le32	number;
	__le32	logical_id;
	__le32	phys_id;
	__le64	rsvd1;
	__le64	rsvd2;
};
LIBIE_CHECK_STRUCT_LEN(32, libie_aqc_list_caps_elem);

/* Admin Queue command opcodes */
enum libie_adminq_opc {
	/* FW Logging Commands */
	libie_aqc_opc_fw_logs_config			= 0xFF30,
	libie_aqc_opc_fw_logs_register			= 0xFF31,
	libie_aqc_opc_fw_logs_query			= 0xFF32,
	libie_aqc_opc_fw_logs_event			= 0xFF33,
};

enum libie_aqc_fw_logging_mod {
	LIBIE_AQC_FW_LOG_ID_GENERAL = 0,
	LIBIE_AQC_FW_LOG_ID_CTRL,
	LIBIE_AQC_FW_LOG_ID_LINK,
	LIBIE_AQC_FW_LOG_ID_LINK_TOPO,
	LIBIE_AQC_FW_LOG_ID_DNL,
	LIBIE_AQC_FW_LOG_ID_I2C,
	LIBIE_AQC_FW_LOG_ID_SDP,
	LIBIE_AQC_FW_LOG_ID_MDIO,
	LIBIE_AQC_FW_LOG_ID_ADMINQ,
	LIBIE_AQC_FW_LOG_ID_HDMA,
	LIBIE_AQC_FW_LOG_ID_LLDP,
	LIBIE_AQC_FW_LOG_ID_DCBX,
	LIBIE_AQC_FW_LOG_ID_DCB,
	LIBIE_AQC_FW_LOG_ID_XLR,
	LIBIE_AQC_FW_LOG_ID_NVM,
	LIBIE_AQC_FW_LOG_ID_AUTH,
	LIBIE_AQC_FW_LOG_ID_VPD,
	LIBIE_AQC_FW_LOG_ID_IOSF,
	LIBIE_AQC_FW_LOG_ID_PARSER,
	LIBIE_AQC_FW_LOG_ID_SW,
	LIBIE_AQC_FW_LOG_ID_SCHEDULER,
	LIBIE_AQC_FW_LOG_ID_TXQ,
	LIBIE_AQC_FW_LOG_ID_RSVD,
	LIBIE_AQC_FW_LOG_ID_POST,
	LIBIE_AQC_FW_LOG_ID_WATCHDOG,
	LIBIE_AQC_FW_LOG_ID_TASK_DISPATCH,
	LIBIE_AQC_FW_LOG_ID_MNG,
	LIBIE_AQC_FW_LOG_ID_SYNCE,
	LIBIE_AQC_FW_LOG_ID_HEALTH,
	LIBIE_AQC_FW_LOG_ID_TSDRV,
	LIBIE_AQC_FW_LOG_ID_PFREG,
	LIBIE_AQC_FW_LOG_ID_MDLVER,
	LIBIE_AQC_FW_LOG_ID_MAX
};

/* Set FW Logging configuration (indirect 0xFF30)
 * Register for FW Logging (indirect 0xFF31)
 * Query FW Logging (indirect 0xFF32)
 * FW Log Event (indirect 0xFF33)
 */
#define LIBIE_AQC_FW_LOG_CONF_UART_EN		BIT(0)
#define LIBIE_AQC_FW_LOG_CONF_AQ_EN		BIT(1)
#define LIBIE_AQC_FW_LOG_QUERY_REGISTERED	BIT(2)
#define LIBIE_AQC_FW_LOG_CONF_SET_VALID		BIT(3)
#define LIBIE_AQC_FW_LOG_AQ_REGISTER		BIT(0)
#define LIBIE_AQC_FW_LOG_AQ_QUERY		BIT(2)

#define LIBIE_AQC_FW_LOG_MIN_RESOLUTION		1
#define LIBIE_AQC_FW_LOG_MAX_RESOLUTION		128

struct libie_aqc_fw_log {
	u8 cmd_flags;

	u8 rsp_flag;
	__le16 fw_rt_msb;
	union {
		struct {
			__le32 fw_rt_lsb;
		} sync;
		struct {
			__le16 log_resolution;
			__le16 mdl_cnt;
		} cfg;
	} ops;
	__le32 addr_high;
	__le32 addr_low;
};

/* Response Buffer for:
 *    Set Firmware Logging Configuration (0xFF30)
 *    Query FW Logging (0xFF32)
 */
struct libie_aqc_fw_log_cfg_resp {
	__le16 module_identifier;
	u8 log_level;
	u8 rsvd0;
};

/**
 * struct libie_aq_desc - Admin Queue (AQ) descriptor
 * @flags: LIBIE_AQ_FLAG_* flags
 * @opcode: AQ command opcode
 * @datalen: length in bytes of indirect/external data buffer
 * @retval: return value from firmware
 * @cookie_high: opaque data high-half
 * @cookie_low: opaque data low-half
 * @params: command-specific parameters
 *
 * Descriptor format for commands the driver posts on the Admin Transmit Queue
 * (ATQ). The firmware writes back onto the command descriptor and returns
 * the result of the command. Asynchronous events that are not an immediate
 * result of the command are written to the Admin Receive Queue (ARQ) using
 * the same descriptor format. Descriptors are in little-endian notation with
 * 32-bit words.
 */
struct libie_aq_desc {
	__le16	flags;
	__le16	opcode;
	__le16	datalen;
	__le16	retval;
	__le32	cookie_high;
	__le32	cookie_low;
	union {
		u8	raw[16];
		struct	libie_aqc_generic generic;
		struct	libie_aqc_get_ver get_ver;
		struct	libie_aqc_driver_ver driver_ver;
		struct	libie_aqc_req_res res_owner;
		struct	libie_aqc_list_caps get_cap;
		struct	libie_aqc_fw_log fw_log;
	} params;
};
LIBIE_CHECK_STRUCT_LEN(32, libie_aq_desc);

/* FW defined boundary for a large buffer, 4k >= Large buffer > 512 bytes */
#define LIBIE_AQ_LG_BUF				512

/* Flags sub-structure
 * |0  |1  |2  |3  |4  |5  |6  |7  |8  |9  |10 |11 |12 |13 |14 |15 |
 * |DD |CMP|ERR|VFE| * *  RESERVED * * |LB |RD |VFC|BUF|SI |EI |FE |
 */
#define LIBIE_AQ_FLAG_DD			BIT(0)	/* 0x1    */
#define LIBIE_AQ_FLAG_CMP			BIT(1)	/* 0x2    */
#define LIBIE_AQ_FLAG_ERR			BIT(2)	/* 0x4    */
#define LIBIE_AQ_FLAG_VFE			BIT(3)	/* 0x8    */
#define LIBIE_AQ_FLAG_LB			BIT(9)	/* 0x200  */
#define LIBIE_AQ_FLAG_RD			BIT(10)	/* 0x400  */
#define LIBIE_AQ_FLAG_VFC			BIT(11) /* 0x800  */
#define LIBIE_AQ_FLAG_BUF			BIT(12)	/* 0x1000 */
#define LIBIE_AQ_FLAG_SI			BIT(13)	/* 0x2000 */
#define LIBIE_AQ_FLAG_EI			BIT(14)	/* 0x4000 */
#define LIBIE_AQ_FLAG_FE			BIT(15)	/* 0x8000 */

/* error codes */
enum libie_aq_err {
	LIBIE_AQ_RC_OK		= 0,  /* Success */
	LIBIE_AQ_RC_EPERM	= 1,  /* Operation not permitted */
	LIBIE_AQ_RC_ENOENT	= 2,  /* No such element */
	LIBIE_AQ_RC_ESRCH	= 3,  /* Bad opcode */
	LIBIE_AQ_RC_EIO		= 5,  /* I/O error */
	LIBIE_AQ_RC_EAGAIN	= 8,  /* Try again */
	LIBIE_AQ_RC_ENOMEM	= 9,  /* Out of memory */
	LIBIE_AQ_RC_EACCES	= 10, /* Permission denied */
	LIBIE_AQ_RC_EBUSY	= 12, /* Device or resource busy */
	LIBIE_AQ_RC_EEXIST	= 13, /* Object already exists */
	LIBIE_AQ_RC_EINVAL	= 14, /* Invalid argument */
	LIBIE_AQ_RC_ENOSPC	= 16, /* No space left or allocation failure */
	LIBIE_AQ_RC_ENOSYS	= 17, /* Function not implemented */
	LIBIE_AQ_RC_EMODE	= 21, /* Op not allowed in current dev mode */
	LIBIE_AQ_RC_ENOSEC	= 24, /* Missing security manifest */
	LIBIE_AQ_RC_EBADSIG	= 25, /* Bad RSA signature */
	LIBIE_AQ_RC_ESVN	= 26, /* SVN number prohibits this package */
	LIBIE_AQ_RC_EBADMAN	= 27, /* Manifest hash mismatch */
	LIBIE_AQ_RC_EBADBUF	= 28, /* Buffer hash mismatches manifest */
};

static inline void *libie_aq_raw(struct libie_aq_desc *desc)
{
	return &desc->params.raw;
}

const char *libie_aq_str(enum libie_aq_err err);

#endif /* __LIBIE_ADMINQ_H */
