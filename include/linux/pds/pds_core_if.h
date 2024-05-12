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
};

#define PDS_CORE_IDENTITY_VERSION_1	1

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

enum pds_core_fw_slot {
	PDS_CORE_FW_SLOT_INVALID    = 0,
	PDS_CORE_FW_SLOT_A	    = 1,
	PDS_CORE_FW_SLOT_B          = 2,
	PDS_CORE_FW_SLOT_GOLD       = 3,
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
