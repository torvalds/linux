/* SPDX-License-Identifier: (GPL-2.0 OR Linux-OpenIB) OR BSD-2-Clause */
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#ifndef _PDS_CORE_IF_H_
#define _PDS_CORE_IF_H_

#define PCI_VENDOR_ID_PENSANDO			0x1dd8
#define PCI_DEVICE_ID_PENSANDO_CORE_PF		0x100c
#define PCI_DEVICE_ID_VIRTIO_NET_TRANS		0x1000
#define PCI_DEVICE_ID_PENSANDO_IONIC_ETH_VF	0x1003
#define PCI_DEVICE_ID_PENSANDO_VDPA_VF		0x100b
#define PDS_CORE_BARS_MAX			4
#define PDS_CORE_PCI_BAR_DBELL			1

/* Bar0 */
#define PDS_CORE_DEV_INFO_SIGNATURE		0x44455649 /* 'DEVI' */
#define PDS_CORE_BAR0_SIZE			0x8000
#define PDS_CORE_BAR0_DEV_INFO_REGS_OFFSET	0x0000
#define PDS_CORE_BAR0_DEV_CMD_REGS_OFFSET	0x0800
#define PDS_CORE_BAR0_DEV_CMD_DATA_REGS_OFFSET	0x0c00
#define PDS_CORE_BAR0_INTR_STATUS_OFFSET	0x1000
#define PDS_CORE_BAR0_INTR_CTRL_OFFSET		0x2000
#define PDS_CORE_DEV_CMD_DONE			0x00000001

#define PDS_CORE_DEVCMD_TIMEOUT			5

#define PDS_CORE_CLIENT_ID			0
#define PDS_CORE_ASIC_TYPE_CAPRI		0

/*
 * enum pds_core_cmd_opcode - Device commands
 */
enum pds_core_cmd_opcode {
	/* Core init */
	PDS_CORE_CMD_NOP		= 0,
	PDS_CORE_CMD_IDENTIFY		= 1,
	PDS_CORE_CMD_RESET		= 2,
	PDS_CORE_CMD_INIT		= 3,

	PDS_CORE_CMD_FW_DOWNLOAD	= 4,
	PDS_CORE_CMD_FW_CONTROL		= 5,

	PDS_CORE_CMD_GET_COMPONENT_INFO	= 6,
	PDS_CORE_CMD_SEND_PKG_DATA	= 7,
	PDS_CORE_CMD_SEND_COMPONENT_TBL	= 8,
	PDS_CORE_CMD_SEND_COMPONENT	= 9,
	PDS_CORE_CMD_FINALIZE_UPDATE	= 10,
	PDS_CORE_CMD_MATCH_RECORD_DESC	= 11,
	PDS_CORE_CMD_HOST_MEM		= 12,

	/* SR/IOV commands */
	PDS_CORE_CMD_VF_GETATTR		= 60,
	PDS_CORE_CMD_VF_SETATTR		= 61,
	PDS_CORE_CMD_VF_CTRL		= 62,

	/* Add commands before this line */
	PDS_CORE_CMD_MAX,
	PDS_CORE_CMD_COUNT
};

/*
 * enum pds_core_status_code - Device command return codes
 */
enum pds_core_status_code {
	PDS_RC_SUCCESS	= 0,	/* Success */
	PDS_RC_EVERSION	= 1,	/* Incorrect version for request */
	PDS_RC_EOPCODE	= 2,	/* Invalid cmd opcode */
	PDS_RC_EIO	= 3,	/* I/O error */
	PDS_RC_EPERM	= 4,	/* Permission denied */
	PDS_RC_EQID	= 5,	/* Bad qid */
	PDS_RC_EQTYPE	= 6,	/* Bad qtype */
	PDS_RC_ENOENT	= 7,	/* No such element */
	PDS_RC_EINTR	= 8,	/* operation interrupted */
	PDS_RC_EAGAIN	= 9,	/* Try again */
	PDS_RC_ENOMEM	= 10,	/* Out of memory */
	PDS_RC_EFAULT	= 11,	/* Bad address */
	PDS_RC_EBUSY	= 12,	/* Device or resource busy */
	PDS_RC_EEXIST	= 13,	/* object already exists */
	PDS_RC_EINVAL	= 14,	/* Invalid argument */
	PDS_RC_ENOSPC	= 15,	/* No space left or alloc failure */
	PDS_RC_ERANGE	= 16,	/* Parameter out of range */
	PDS_RC_BAD_ADDR	= 17,	/* Descriptor contains a bad ptr */
	PDS_RC_DEV_CMD	= 18,	/* Device cmd attempted on AdminQ */
	PDS_RC_ENOSUPP	= 19,	/* Operation not supported */
	PDS_RC_ERROR	= 29,	/* Generic error */
	PDS_RC_ERDMA	= 30,	/* Generic RDMA error */
	PDS_RC_EVFID	= 31,	/* VF ID does not exist */
	PDS_RC_BAD_FW	= 32,	/* FW file is invalid or corrupted */
	PDS_RC_ECLIENT	= 33,   /* No such client id */
	PDS_RC_BAD_PCI	= 255,  /* Broken PCI when reading status */
};

/**
 * struct pds_core_drv_identity - Driver identity information
 * @drv_type:         Driver type (enum pds_core_driver_type)
 * @os_dist:          OS distribution, numeric format
 * @os_dist_str:      OS distribution, string format
 * @kernel_ver:       Kernel version, numeric format
 * @kernel_ver_str:   Kernel version, string format
 * @driver_ver_str:   Driver version, string format
 */
struct pds_core_drv_identity {
	__le32 drv_type;
	__le32 os_dist;
	char   os_dist_str[128];
	__le32 kernel_ver;
	char   kernel_ver_str[32];
	char   driver_ver_str[32];
};

/**
 * enum pds_core_dev_capability - Device capabilities
 * @PDS_CORE_DEV_CAP_PLDM_FW_UPDATE: Device only supports FW update via PLDM
 * @PDS_CORE_DEV_CAP_HOST_MEM: Device supports host memory for fw use
 */
enum pds_core_dev_capability {
	PDS_CORE_DEV_CAP_PLDM_FW_UPDATE = BIT(0),
	PDS_CORE_DEV_CAP_HOST_MEM = BIT(1),
};

#define PDS_DEV_TYPE_MAX	16
/**
 * struct pds_core_dev_identity - Device identity information
 * @version:	      Version of device identify
 * @type:	      Identify type (0 for now)
 * @state:	      Device state
 * @rsvd:	      Word boundary padding
 * @nlifs:	      Number of LIFs provisioned
 * @nintrs:	      Number of interrupts provisioned
 * @ndbpgs_per_lif:   Number of doorbell pages per LIF
 * @intr_coal_mult:   Interrupt coalescing multiplication factor
 *		      Scale user-supplied interrupt coalescing
 *		      value in usecs to device units using:
 *		      device units = usecs * mult / div
 * @intr_coal_div:    Interrupt coalescing division factor
 *		      Scale user-supplied interrupt coalescing
 *		      value in usecs to device units using:
 *		      device units = usecs * mult / div
 * @vif_types:        How many of each VIF device type is supported
 * @max_fw_slots:     Number of firmware components reported by device
 *		      only supported on version >= PDS_CORE_IDENTITY_VERSION_2
 * @rsvd2:	      Word boundary padding
 * @capabilities:     Device capabilities
 *		      only supported on version >= PDS_CORE_IDENTITY_VERSION_2
 */
struct pds_core_dev_identity {
	u8     version;
	u8     type;
	u8     state;
	u8     rsvd;
	__le32 nlifs;
	__le32 nintrs;
	__le32 ndbpgs_per_lif;
	__le32 intr_coal_mult;
	__le32 intr_coal_div;
	__le16 vif_types[PDS_DEV_TYPE_MAX];
	__le16 max_fw_slots;
	u8     rsvd2[6];
	__le64 capabilities;
};

#define PDS_CORE_IDENTITY_VERSION_1	1
#define PDS_CORE_IDENTITY_VERSION_2	2

/**
 * struct pds_core_dev_identify_cmd - Driver/device identify command
 * @opcode:	Opcode PDS_CORE_CMD_IDENTIFY
 * @ver:	Highest version of identify supported by driver
 *
 * Expects to find driver identification info (struct pds_core_drv_identity)
 * in cmd_regs->data.  Driver should keep the devcmd interface locked
 * while preparing the driver info.
 */
struct pds_core_dev_identify_cmd {
	u8 opcode;
	u8 ver;
};

/**
 * struct pds_core_dev_identify_comp - Device identify command completion
 * @status:	Status of the command (enum pds_core_status_code)
 * @ver:	Version of identify returned by device
 *
 * Device identification info (struct pds_core_dev_identity) can be found
 * in cmd_regs->data.  Driver should keep the devcmd interface locked
 * while reading the results.
 */
struct pds_core_dev_identify_comp {
	u8 status;
	u8 ver;
};

/**
 * struct pds_core_dev_reset_cmd - Device reset command
 * @opcode:	Opcode PDS_CORE_CMD_RESET
 *
 * Resets and clears all LIFs, VDevs, and VIFs on the device.
 */
struct pds_core_dev_reset_cmd {
	u8 opcode;
};

/**
 * struct pds_core_dev_reset_comp - Reset command completion
 * @status:	Status of the command (enum pds_core_status_code)
 */
struct pds_core_dev_reset_comp {
	u8 status;
};

/*
 * struct pds_core_dev_init_data - Pointers and info needed for the Core
 * initialization PDS_CORE_CMD_INIT command.  The in and out structs are
 * overlays on the pds_core_dev_cmd_regs.data space for passing data down
 * to the firmware on init, and then returning initialization results.
 */
struct pds_core_dev_init_data_in {
	__le64 adminq_q_base;
	__le64 adminq_cq_base;
	__le64 notifyq_cq_base;
	__le32 flags;
	__le16 intr_index;
	u8     adminq_ring_size;
	u8     notifyq_ring_size;
};

struct pds_core_dev_init_data_out {
	__le32 core_hw_index;
	__le32 adminq_hw_index;
	__le32 notifyq_hw_index;
	u8     adminq_hw_type;
	u8     notifyq_hw_type;
};

/**
 * struct pds_core_dev_init_cmd - Core device initialize
 * @opcode:          opcode PDS_CORE_CMD_INIT
 *
 * Initializes the core device and sets up the AdminQ and NotifyQ.
 * Expects to find initialization data (struct pds_core_dev_init_data_in)
 * in cmd_regs->data.  Driver should keep the devcmd interface locked
 * while preparing the driver info.
 */
struct pds_core_dev_init_cmd {
	u8     opcode;
};

/**
 * struct pds_core_dev_init_comp - Core init completion
 * @status:     Status of the command (enum pds_core_status_code)
 *
 * Initialization result data (struct pds_core_dev_init_data_in)
 * is found in cmd_regs->data.
 */
struct pds_core_dev_init_comp {
	u8     status;
};

/**
 * struct pds_core_fw_download_cmd - Firmware download command
 * @opcode:     opcode
 * @rsvd:	Word boundary padding
 * @addr:       DMA address of the firmware buffer
 * @offset:     offset of the firmware buffer within the full image
 * @length:     number of valid bytes in the firmware buffer
 */
struct pds_core_fw_download_cmd {
	u8     opcode;
	u8     rsvd[3];
	__le32 offset;
	__le64 addr;
	__le32 length;
};

/**
 * struct pds_core_fw_download_comp - Firmware download completion
 * @status:     Status of the command (enum pds_core_status_code)
 */
struct pds_core_fw_download_comp {
	u8     status;
};

/**
 * enum pds_core_fw_control_oper - FW control operations
 * @PDS_CORE_FW_INSTALL_ASYNC:     Install firmware asynchronously
 * @PDS_CORE_FW_INSTALL_STATUS:    Firmware installation status
 * @PDS_CORE_FW_ACTIVATE_ASYNC:    Activate firmware asynchronously
 * @PDS_CORE_FW_ACTIVATE_STATUS:   Firmware activate status
 * @PDS_CORE_FW_UPDATE_CLEANUP:    Cleanup any firmware update leftovers
 * @PDS_CORE_FW_GET_BOOT:          Return current active firmware slot
 * @PDS_CORE_FW_SET_BOOT:          Set active firmware slot for next boot
 * @PDS_CORE_FW_GET_LIST:          Return list of installed firmware images
 */
enum pds_core_fw_control_oper {
	PDS_CORE_FW_INSTALL_ASYNC          = 0,
	PDS_CORE_FW_INSTALL_STATUS         = 1,
	PDS_CORE_FW_ACTIVATE_ASYNC         = 2,
	PDS_CORE_FW_ACTIVATE_STATUS        = 3,
	PDS_CORE_FW_UPDATE_CLEANUP         = 4,
	PDS_CORE_FW_GET_BOOT               = 5,
	PDS_CORE_FW_SET_BOOT               = 6,
	PDS_CORE_FW_GET_LIST               = 7,
};

/**
 * enum pds_core_fw_slot - Firmware slot identifiers
 * @PDS_CORE_FW_SLOT_INVALID: Let firmware select slot based on package metadata
 * @PDS_CORE_FW_SLOT_A:       Primary firmware slot A
 * @PDS_CORE_FW_SLOT_B:       Primary firmware slot B
 * @PDS_CORE_FW_SLOT_GOLD:    Gold/recovery firmware slot
 * @PDS_CORE_FW_SLOT_MAX:     Sentinel value indicating no slot resolved
 */
enum pds_core_fw_slot {
	PDS_CORE_FW_SLOT_INVALID    = 0,
	PDS_CORE_FW_SLOT_A	    = 1,
	PDS_CORE_FW_SLOT_B          = 2,
	PDS_CORE_FW_SLOT_GOLD       = 3,
	PDS_CORE_FW_SLOT_MAX        = 0xff,
};

/**
 * struct pds_core_fw_control_cmd - Firmware control command
 * @opcode:    opcode
 * @rsvd:      Word boundary padding
 * @oper:      firmware control operation (enum pds_core_fw_control_oper)
 * @slot:      slot to operate on (enum pds_core_fw_slot)
 */
struct pds_core_fw_control_cmd {
	u8  opcode;
	u8  rsvd[3];
	u8  oper;
	u8  slot;
};

/**
 * struct pds_core_fw_control_comp - Firmware control copletion
 * @status:	Status of the command (enum pds_core_status_code)
 * @rsvd:	Word alignment space
 * @slot:	Slot number (enum pds_core_fw_slot)
 * @rsvd1:	Struct padding
 * @color:	Color bit
 */
struct pds_core_fw_control_comp {
	u8     status;
	u8     rsvd[3];
	u8     slot;
	u8     rsvd1[10];
	u8     color;
};

struct pds_core_fw_name_info {
#define PDS_CORE_FWSLOT_BUFLEN		8
#define PDS_CORE_FWVERS_BUFLEN		32
	char   slotname[PDS_CORE_FWSLOT_BUFLEN];
	char   fw_version[PDS_CORE_FWVERS_BUFLEN];
};

struct pds_core_fw_list_info {
#define PDS_CORE_FWVERS_LIST_LEN	16
	u8 num_fw_slots;
	struct pds_core_fw_name_info fw_names[PDS_CORE_FWVERS_LIST_LEN];
} __packed;

enum pds_core_vf_attr {
	PDS_CORE_VF_ATTR_SPOOFCHK	= 1,
	PDS_CORE_VF_ATTR_TRUST		= 2,
	PDS_CORE_VF_ATTR_MAC		= 3,
	PDS_CORE_VF_ATTR_LINKSTATE	= 4,
	PDS_CORE_VF_ATTR_VLAN		= 5,
	PDS_CORE_VF_ATTR_RATE		= 6,
	PDS_CORE_VF_ATTR_STATSADDR	= 7,
};

/**
 * enum pds_core_vf_link_status - Virtual Function link status
 * @PDS_CORE_VF_LINK_STATUS_AUTO:   Use link state of the uplink
 * @PDS_CORE_VF_LINK_STATUS_UP:     Link always up
 * @PDS_CORE_VF_LINK_STATUS_DOWN:   Link always down
 */
enum pds_core_vf_link_status {
	PDS_CORE_VF_LINK_STATUS_AUTO = 0,
	PDS_CORE_VF_LINK_STATUS_UP   = 1,
	PDS_CORE_VF_LINK_STATUS_DOWN = 2,
};

/**
 * struct pds_core_vf_setattr_cmd - Set VF attributes on the NIC
 * @opcode:     Opcode
 * @attr:       Attribute type (enum pds_core_vf_attr)
 * @vf_index:   VF index
 * @macaddr:	mac address
 * @vlanid:	vlan ID
 * @maxrate:	max Tx rate in Mbps
 * @spoofchk:	enable address spoof checking
 * @trust:	enable VF trust
 * @linkstate:	set link up or down
 * @stats:	stats addr struct
 * @stats.pa:	set DMA address for VF stats
 * @stats.len:	length of VF stats space
 * @pad:	force union to specific size
 */
struct pds_core_vf_setattr_cmd {
	u8     opcode;
	u8     attr;
	__le16 vf_index;
	union {
		u8     macaddr[6];
		__le16 vlanid;
		__le32 maxrate;
		u8     spoofchk;
		u8     trust;
		u8     linkstate;
		struct {
			__le64 pa;
			__le32 len;
		} stats;
		u8     pad[60];
	} __packed;
};

struct pds_core_vf_setattr_comp {
	u8     status;
	u8     attr;
	__le16 vf_index;
	__le16 comp_index;
	u8     rsvd[9];
	u8     color;
};

/**
 * struct pds_core_vf_getattr_cmd - Get VF attributes from the NIC
 * @opcode:     Opcode
 * @attr:       Attribute type (enum pds_core_vf_attr)
 * @vf_index:   VF index
 */
struct pds_core_vf_getattr_cmd {
	u8     opcode;
	u8     attr;
	__le16 vf_index;
};

struct pds_core_vf_getattr_comp {
	u8     status;
	u8     attr;
	__le16 vf_index;
	union {
		u8     macaddr[6];
		__le16 vlanid;
		__le32 maxrate;
		u8     spoofchk;
		u8     trust;
		u8     linkstate;
		__le64 stats_pa;
		u8     pad[11];
	} __packed;
	u8     color;
};

enum pds_core_vf_ctrl_opcode {
	PDS_CORE_VF_CTRL_START_ALL	= 0,
	PDS_CORE_VF_CTRL_START		= 1,
};

/**
 * struct pds_core_vf_ctrl_cmd - VF control command
 * @opcode:         Opcode for the command
 * @ctrl_opcode:    VF control operation type
 * @vf_index:       VF Index. It is unused if op START_ALL is used.
 */

struct pds_core_vf_ctrl_cmd {
	u8	opcode;
	u8	ctrl_opcode;
	__le16	vf_index;
};

/**
 * struct pds_core_vf_ctrl_comp - VF_CTRL command completion.
 * @status:     Status of the command (enum pds_core_status_code)
 */
struct pds_core_vf_ctrl_comp {
	u8	status;
};

/**
 * struct pds_core_send_pkg_data_cmd - Send package data command
 * @opcode: Opcode PDS_CORE_CMD_SEND_PKG_DATA
 * @ver: Driver's max support version of this command
 * @total_len: Total length of the package data
 * @offset: Offset in the package data, non-zero if multiple commands are
 *	    needed for sending the package data
 * @data_len: Length of data stored at data_pa
 * @data_pa: Data physical address for DMA to device
 *
 * The package data may be too large to store in a single buffer, so multiple
 * PDS_CORE_CMD_SEND_PKG_DATA devcmds may be needed.
 */
struct pds_core_send_pkg_data_cmd {
	u8 opcode;
	u8 ver;
	__le16 total_len;
	__le16 offset;
	__le16 data_len;
	__le64 data_pa;
};

/**
 * struct pds_core_send_pkg_data_comp - Send package data completion
 * @status: Status of the command (enum pds_core_status_code)
 * @ver: Device's max supported version of this command
 * @rsvd: Word boundary padding
 */
struct pds_core_send_pkg_data_comp {
	u8 status;
	u8 ver;
	u8 rsvd[2];
};

/**
 * struct pds_core_component_tbl - Component table details
 * @comparison_stamp: Comparison stamp used for component version checks
 * @classification: Vendor specific classification info
 * @identifier: Component's ID
 * @transfer_flag: Part of the component table this request represents
 * @version_str_type: The types of strings used
 * @version_str_len: Length of @version_str
 * @version_str: Component version information
 */
struct pds_core_component_tbl {
	__le32 comparison_stamp;
	__le16 classification;
	__le16 identifier;
	u8     transfer_flag;
	u8     version_str_type;
	u8     version_str_len;
	u8     version_str[];
};

/**
 * struct pds_core_send_component_tbl_cmd - Send component table command
 * @opcode: Opcode PDS_CORE_CMD_SEND_COMPONENT_TBL
 * @ver: Driver's max support version of this command
 * @slot_id: enum pds_core_fw_slot
 * @rsvd: Word boundary padding
 *
 * Expects to find component table info (struct pds_core_component_tbl)
 * in cmd_regs->data.  Driver should keep the devcmd interface locked
 * while preparing the component table info.
 */
struct pds_core_send_component_tbl_cmd {
	u8 opcode;
	u8 ver;
	u8 slot_id;
	u8 rsvd;
};

enum pds_core_component_resp_code {
	PDS_CORE_COMPONENT_VALID = 0x0,
	PDS_CORE_COMPONENT_STAMP_IDENTICAL = 0x1,
	PDS_CORE_COMPONENT_STAMP_LOWER = 0x2,
	PDS_CORE_COMPONENT_STAMP_OR_VERSION_INVALID = 0x3,
	PDS_CORE_COMPONENT_CONFLICT = 0x4,
	PDS_CORE_COMPONENT_PREREQS_NOT_MET = 0x5,
	PDS_CORE_COMPONENT_NOT_SUPPORTED = 0x6,
	PDS_CORE_COMPONENT_FW_TYPE_INVALID = 0xd0,
};

/**
 * struct pds_core_send_component_tbl_comp - Send component table completion
 * @status: Status of the command (enum pds_core_status_code)
 * @ver: Device's max supported version of this command
 * @completion_code: Component completion code
 * @response: Component response
 * @response_code: Component response code
 * @slot_id: Actual slot_id of the component (enum pds_core_fw_slot)
 * @rsvd: Word boundary padding
 */
struct pds_core_send_component_tbl_comp {
	u8 status;
	u8 ver;
	u8 completion_code;
	u8 response;
	u8 response_code;
	u8 slot_id;
	u8 rsvd[2];
};

/**
 * enum pds_core_send_component_op - PDS_CORE_CMD_SEND_COMPONENT operation
 * @PDS_CORE_SEND_COMPONENT_START: Initial operation to start transfer
 * @PDS_CORE_SEND_COMPONENT_STATUS: Subsequent calls to check on status
 */
enum pds_core_send_component_op {
	PDS_CORE_SEND_COMPONENT_START = 0,
	PDS_CORE_SEND_COMPONENT_STATUS = 1,
};

#define PDS_CORE_FW_COMPONENT_ID_INVALID 0xFFFF
/**
 * struct pds_core_flash_component - Component details
 * @comparison_stamp: Comparison stamp used for component version checks
 * @image_size: Component image size
 * @classification: Vendor specific classification info
 * @identifier: Component's ID
 * @options: Component options
 * @rsvd: Word boundary padding
 * @version_str_type: The types of strings used
 * @version_str_len: Length of @version_str
 * @version_str: Component version information
 */
struct pds_core_flash_component {
	__le32 comparison_stamp;
	__le32 image_size;
	__le16 classification;
	__le16 identifier;
	__le16 options;
	u8 rsvd[3];
	u8 version_str_type;
	u8 version_str_len;
	u8 version_str[];
};

/**
 * struct pds_core_send_component_cmd - Send component command
 * @opcode: Opcode PDS_CORE_CMD_SEND_COMPONENT
 * @ver: Driver's max supported version of this command
 * @slot_id: enum pds_core_fw_slot
 * @operation: enum pds_core_send_component_op
 * @offset: Offset into the component, non-zero if multiple commands
 *	    are needed for a single component
 * @data_len: Length of this part of the component stored at @data_pa
 * @rsvd: Word boundary padding
 * @data_pa: DMA address of the component
 *
 * A component may be too large to store in a single buffer, so multiple
 * PDS_CORE_CMD_SEND_COMPONENT devcmds may be needed.
 *
 * Expects to find flash component info (struct pds_core_flash_component)
 * in cmd_regs->data. Driver should keep the devcmd interface locked
 * while preparing and sending the flash component info.
 */
struct pds_core_send_component_cmd {
	u8 opcode;
	u8 ver;
	u8 slot_id;
	u8 operation;
	__le32 offset;
	__le32 data_len;
	u8 rsvd[4];
	__le64 data_pa;
};

/**
 * struct pds_core_send_component_comp - Send component completion
 * @status: Status of the command (enum pds_core_status_code)
 * @ver: Device's max supported version of this command
 * @completion_code: Completion code
 * @compat_response: Compatibility response (0 = Component can be updated)
 * @compat_response_code: Compatibility response code
 * @rsvd: Word boundary padding
 */
struct pds_core_send_component_comp {
	u8 status;
	u8 ver;
	u8 completion_code;
	u8 compat_response;
	u8 compat_response_code;
	u8 rsvd[3];
};

/**
 * enum pds_core_fw_component_type - Firmware component type
 * @PDS_CORE_FW_TYPE_UNKNOWN: Unknown component type
 * @PDS_CORE_FW_TYPE_MAIN: Main firmware
 * @PDS_CORE_FW_TYPE_BOOT: Boot loader
 * @PDS_CORE_FW_TYPE_CPLD: CPLD firmware
 * @PDS_CORE_FW_TYPE_SECURE: Secure firmware
 * @PDS_CORE_FW_TYPE_FPGA: FPGA configuration
 * @PDS_CORE_FW_TYPE_SUC_MAIN: System Unit Controller firmware
 * @PDS_CORE_FW_TYPE_SUC_BOOT: System Unit Controller bootloader
 * @PDS_CORE_FW_TYPE_UBOOT: U-Boot bootloader
 *
 * Gold/recovery variants are identified by slot_id == PDS_CORE_FW_SLOT_GOLD
 * and reported with a ".gold" suffix (e.g., fw.gold).
 */
enum pds_core_fw_component_type {
	PDS_CORE_FW_TYPE_UNKNOWN   = 0,
	PDS_CORE_FW_TYPE_MAIN      = 1,
	PDS_CORE_FW_TYPE_BOOT      = 2,
	PDS_CORE_FW_TYPE_CPLD      = 3,
	PDS_CORE_FW_TYPE_SECURE    = 4,
	PDS_CORE_FW_TYPE_FPGA      = 5,
	PDS_CORE_FW_TYPE_SUC_MAIN  = 6,
	PDS_CORE_FW_TYPE_SUC_BOOT  = 7,
	PDS_CORE_FW_TYPE_UBOOT     = 8,
};

/**
 * enum pds_core_component_info_flags - Component info flags
 * @PDS_CORE_FW_COMPONENT_INFO_F_RUNNING: Component is currently running
 * @PDS_CORE_FW_COMPONENT_INFO_F_STARTUP: Component version on next FW boot
 * @PDS_CORE_FW_COMPONENT_INFO_F_FIXED: Component is fixed and cannot be updated
 * @PDS_CORE_FW_COMPONENT_INFO_F_UPDATE_BY_NAME: Component can be updated
 *	by name
 */
enum pds_core_component_info_flags {
	PDS_CORE_FW_COMPONENT_INFO_F_RUNNING = BIT(0),
	PDS_CORE_FW_COMPONENT_INFO_F_STARTUP = BIT(1),
	PDS_CORE_FW_COMPONENT_INFO_F_FIXED = BIT(2),
	PDS_CORE_FW_COMPONENT_INFO_F_UPDATE_BY_NAME = BIT(3),
};

/**
 * struct pds_core_fw_component_info - GET_COMPONENT_INFO entry
 * @name: Component's name
 * @component_type: enum pds_core_fw_component_type
 * @rsvd: Word boundary padding
 * @flags: enum pds_core_component_info_flags
 * @identifier: Component's identifier
 * @slot_id: Component's slot identifier
 * @version: Component's version
 */
struct pds_core_fw_component_info {
#define PDS_CORE_FW_COMPONENT_NAME_BUFLEN 24
	char name[PDS_CORE_FW_COMPONENT_NAME_BUFLEN];
	u8 component_type;
	u8 rsvd[3];
	__le16 flags;
	u8 identifier;
	u8 slot_id;
#define PDS_CORE_FW_COMPONENT_VER_BUFLEN 32
	char version[PDS_CORE_FW_COMPONENT_VER_BUFLEN];
};

#define PDS_CORE_FW_COMPONENT_LIST_LEN	((PDS_PAGE_SIZE - 8) / \
		sizeof(struct pds_core_fw_component_info))

/**
 * struct pds_core_component_list_info - GET_COMPONENT_INFO completion data
 * @num_components: Number of valid components
 * @rsvd: Word boundary padding
 * @info: List of valid components
 */
struct pds_core_component_list_info {
	u8 num_components;
	u8 rsvd[7];
	struct pds_core_fw_component_info info[PDS_CORE_FW_COMPONENT_LIST_LEN];
};

/**
 * struct pds_core_get_component_info_cmd - GET_COMPONENT_INFO command
 * @opcode: PDS_CORE_CMD_GET_COMPONENT_INFO
 * @ver: Driver's max supported version of this command
 * @data_len: Length of data at data_pa
 * @rsvd: Word boundary padding
 * @data_pa: DMA address of data
 *
 * FW populates struct pds_core_component_list_info pointed to by @data_pa
 */
struct pds_core_get_component_info_cmd {
	u8 opcode;
	u8 ver;
	__le16 data_len;
	u8 rsvd[4];
	__le64 data_pa;
};

/**
 * struct pds_core_get_component_info_comp - GET_COMPONENT_INFO completion
 * @status: enum pds_core_status_code
 * @ver: Device's max supported version of this command
 * @rsvd: Word boundary padding
 */
struct pds_core_get_component_info_comp {
	u8 status;
	u8 ver;
	u8 rsvd[2];
};

/**
 * struct pds_core_finalize_update_cmd - FINALIZE_UPDATE command
 * @opcode: PDS_CORE_CMD_FINALIZE_UPDATE
 * @ver: Driver's max support version of this command
 * @rsvd: Word boundary padding
 *
 * Driver sends at the end of updating all components to finalize the update
 */
struct pds_core_finalize_update_cmd {
	u8 opcode;
	u8 ver;
	u8 rsvd[2];
};

/**
 * struct pds_core_finalize_update_comp - FINALIZE_UPDATE completion
 * @status: enum pds_core_status_code
 * @ver: Device's max supported version of this command
 * @rsvd: Word boundary padding
 */
struct pds_core_finalize_update_comp {
	u8 status;
	u8 ver;
	u8 rsvd[2];
};

/**
 * struct pds_core_match_record_desc_cmd - MATCH_RECORD_DESC command
 * @opcode: PDS_CORE_CMD_MATCH_RECORD_DESC
 * @ver: Driver's max supported version of this command
 * @type: PLDM Descriptor Identifier Type
 * @size: Length of the Descriptor Identifier Value
 * @rsvd: Word boundary padding
 *
 * Expects to find the Descriptor Identifier Data in cmd_regs->data. Driver
 * should keep the devcmd interface locked while preparing and sending this
 * command.
 */
struct pds_core_match_record_desc_cmd {
	u8 opcode;
	u8 ver;
	__le16 type;
	__le16 size;
	u8 rsvd[2];
};

/**
 * struct pds_core_match_record_desc_comp - MATCH_RECORD_DESC completion
 * @status: enum pds_core_status_code
 * @ver: Device's max supported version of this command
 * @match: Whether or not the Record Descriptor matches the device
 * @rsvd: Word boundary padding
 *
 * When status is PDS_RC_SUCCESS, then @match is valid, otherwise it's
 * undefined.
 */
struct pds_core_match_record_desc_comp {
	u8 status;
	u8 ver;
	u8 match;
	u8 rsvd;
};

/**
 * enum pds_core_host_mem_oper - HOST_MEM sub-operations
 * @PDS_CORE_HOST_MEM_GET_COUNT: Query number of memory requests
 * @PDS_CORE_HOST_MEM_QUERY:     Query details of a memory request
 * @PDS_CORE_HOST_MEM_ADD:       Provide allocated memory to firmware
 * @PDS_CORE_HOST_MEM_DEL:       Notify firmware of memory deallocation
 */
enum pds_core_host_mem_oper {
	PDS_CORE_HOST_MEM_GET_COUNT	= 0,
	PDS_CORE_HOST_MEM_QUERY		= 1,
	PDS_CORE_HOST_MEM_ADD		= 2,
	PDS_CORE_HOST_MEM_DEL		= 3,
};

/**
 * struct pds_core_host_mem_cmd - HOST_MEM command
 * @opcode:     Opcode PDS_CORE_CMD_HOST_MEM
 * @oper:       Operation (enum pds_core_host_mem_oper)
 * @index:      Memory request index (GET_COUNT: max_count, QUERY: index)
 * @tag:        Tag for this memory request (ADD/DEL)
 * @reason:     Reason for deletion (DEL only)
 * @rsvd:       Reserved
 * @max_contig: Maximum contiguous memory size (GET_COUNT only)
 * @size:       Size of memory in bytes (ADD only)
 * @buf_pa:     DMA address of memory (ADD only)
 *
 * Unified command for all host memory operations. Fields are reused
 * across operations to minimize opcode space usage.
 */
struct pds_core_host_mem_cmd {
	u8     opcode;
	u8     oper;
	__le16 index;
	__le16 tag;
	u8     reason;
	u8     rsvd;
	__le32 max_contig;
	__le32 size;
	__le64 buf_pa;
};

/**
 * struct pds_core_host_mem_comp - HOST_MEM completion
 * @status:       Status of the command (enum pds_core_status_code)
 * @oper:         Operation that was performed
 * @count:        Number of memory requests (GET_COUNT)
 * @size:         Size of memory request in bytes (QUERY)
 * @tag:          Tag for this memory request (QUERY/DEL)
 * @rsvd:         Reserved
 */
struct pds_core_host_mem_comp {
	u8     status;
	u8     oper;
	__le16 count;
	__le32 size;
	__le16 tag;
	u8     rsvd[6];
};

/*
 * union pds_core_dev_cmd - Overlay of core device command structures
 */
union pds_core_dev_cmd {
	u8     opcode;
	u32    words[16];

	struct pds_core_dev_identify_cmd identify;
	struct pds_core_dev_init_cmd     init;
	struct pds_core_dev_reset_cmd    reset;
	struct pds_core_fw_download_cmd  fw_download;
	struct pds_core_fw_control_cmd   fw_control;

	struct pds_core_vf_setattr_cmd   vf_setattr;
	struct pds_core_vf_getattr_cmd   vf_getattr;
	struct pds_core_vf_ctrl_cmd      vf_ctrl;

	struct pds_core_get_component_info_cmd get_component_info;
	struct pds_core_send_pkg_data_cmd      send_pkg_data;
	struct pds_core_send_component_tbl_cmd send_component_tbl;
	struct pds_core_send_component_cmd     send_component;
	struct pds_core_finalize_update_cmd    finalize_update;
	struct pds_core_match_record_desc_cmd  match_record_desc;
	struct pds_core_host_mem_cmd           host_mem;
};

/*
 * union pds_core_dev_comp - Overlay of core device completion structures
 */
union pds_core_dev_comp {
	u8                                status;
	u8                                bytes[16];

	struct pds_core_dev_identify_comp identify;
	struct pds_core_dev_reset_comp    reset;
	struct pds_core_dev_init_comp     init;
	struct pds_core_fw_download_comp  fw_download;
	struct pds_core_fw_control_comp   fw_control;

	struct pds_core_vf_setattr_comp   vf_setattr;
	struct pds_core_vf_getattr_comp   vf_getattr;
	struct pds_core_vf_ctrl_comp      vf_ctrl;

	struct pds_core_get_component_info_comp get_component_info;
	struct pds_core_send_pkg_data_comp      send_pkg_data;
	struct pds_core_send_component_tbl_comp send_component_tbl;
	struct pds_core_send_component_comp     send_component;
	struct pds_core_finalize_update_comp    finalize_update;
	struct pds_core_match_record_desc_comp  match_record_desc;
	struct pds_core_host_mem_comp           host_mem;
};

/**
 * struct pds_core_dev_hwstamp_regs - Hardware current timestamp registers
 * @tick_low:        Low 32 bits of hardware timestamp
 * @tick_high:       High 32 bits of hardware timestamp
 */
struct pds_core_dev_hwstamp_regs {
	u32    tick_low;
	u32    tick_high;
};

/**
 * struct pds_core_dev_info_regs - Device info register format (read-only)
 * @signature:       Signature value of 0x44455649 ('DEVI')
 * @version:         Current version of info
 * @asic_type:       Asic type
 * @asic_rev:        Asic revision
 * @fw_status:       Firmware status
 *			bit 0   - 1 = fw running
 *			bit 4-7 - 4 bit generation number, changes on fw restart
 * @fw_heartbeat:    Firmware heartbeat counter
 * @serial_num:      Serial number
 * @fw_version:      Firmware version
 * @oprom_regs:      oprom_regs to store oprom debug enable/disable and bmp
 * @rsvd_pad1024:    Struct padding
 * @hwstamp:         Hardware current timestamp registers
 * @rsvd_pad2048:    Struct padding
 */
struct pds_core_dev_info_regs {
#define PDS_CORE_DEVINFO_FWVERS_BUFLEN 32
#define PDS_CORE_DEVINFO_SERIAL_BUFLEN 32
	u32    signature;
	u8     version;
	u8     asic_type;
	u8     asic_rev;
#define PDS_CORE_FW_STS_F_STOPPED	0x00
#define PDS_CORE_FW_STS_F_RUNNING	0x01
#define PDS_CORE_FW_STS_F_GENERATION	0xF0
	u8     fw_status;
	__le32 fw_heartbeat;
	char   fw_version[PDS_CORE_DEVINFO_FWVERS_BUFLEN];
	char   serial_num[PDS_CORE_DEVINFO_SERIAL_BUFLEN];
	u8     oprom_regs[32];     /* reserved */
	u8     rsvd_pad1024[916];
	struct pds_core_dev_hwstamp_regs hwstamp;   /* on 1k boundary */
	u8     rsvd_pad2048[1016];
} __packed;

/**
 * struct pds_core_dev_cmd_regs - Device command register format (read-write)
 * @doorbell:	Device Cmd Doorbell, write-only
 *              Write a 1 to signal device to process cmd
 * @done:	Command completed indicator, poll for completion
 *              bit 0 == 1 when command is complete
 * @cmd:	Opcode-specific command bytes
 * @comp:	Opcode-specific response bytes
 * @rsvd:	Struct padding
 * @data:	Opcode-specific side-data
 */
struct pds_core_dev_cmd_regs {
	u32                     doorbell;
	u32                     done;
	union pds_core_dev_cmd  cmd;
	union pds_core_dev_comp comp;
	u8                      rsvd[48];
	u32                     data[478];
} __packed;

/**
 * struct pds_core_dev_regs - Device register format for bar 0 page 0
 * @info:            Device info registers
 * @devcmd:          Device command registers
 */
struct pds_core_dev_regs {
	struct pds_core_dev_info_regs info;
	struct pds_core_dev_cmd_regs  devcmd;
} __packed;

#ifndef __CHECKER__
static_assert(sizeof(struct pds_core_drv_identity) <= 1912);
static_assert(sizeof(struct pds_core_dev_identity) <= 1912);
static_assert(sizeof(union pds_core_dev_cmd) == 64);
static_assert(sizeof(union pds_core_dev_comp) == 16);
static_assert(sizeof(struct pds_core_dev_info_regs) == 2048);
static_assert(sizeof(struct pds_core_dev_cmd_regs) == 2048);
static_assert(sizeof(struct pds_core_dev_regs) == 4096);
#endif /* __CHECKER__ */

#endif /* _PDS_CORE_IF_H_ */
