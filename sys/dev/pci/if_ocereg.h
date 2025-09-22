/*	$OpenBSD: if_ocereg.h,v 1.8 2022/01/09 05:42:54 jsg Exp $	*/

/*-
 * Copyright (C) 2012 Emulex
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Emulex Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * freebsd-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#define OCE_BAR_CFG			0x10
#define OCE_BAR_CFG_BE2			0x14
#define OCE_BAR_CSR			0x18
#define OCE_BAR_DB			0x20

/* MPU semaphore */
#define	MPU_EP_SEM_BE			0x0ac
#define	MPU_EP_SEM_XE201		0x400
#define MPU_EP_SEMAPHORE(sc) \
	((IS_BE(sc)) ? MPU_EP_SEM_BE : MPU_EP_SEM_XE201)
#define	 MPU_EP_SEM_STAGE_MASK		0xffff
#define	 MPU_EP_SEM_ERROR		(1<<31)

#define	PCI_INTR_CTRL			0xfc
#define	 HOSTINTR_MASK			(1<<29)

/* POST status reg struct */
#define	POST_STAGE_POWER_ON_RESET	0x00
#define	POST_STAGE_AWAITING_HOST_RDY	0x01
#define	POST_STAGE_HOST_RDY		0x02
#define	POST_STAGE_CHIP_RESET		0x03
#define	POST_STAGE_ARMFW_READY		0xc000
#define	POST_STAGE_ARMFW_UE		0xf000

/* DOORBELL registers */
#define	PD_TXULP_DB			0x0060
#define	PD_RXULP_DB			0x0100
#define	PD_CQ_DB			0x0120
#define	PD_EQ_DB			0x0120	/* same as CQ */
#define	 PD_EQ_DB_EVENT			 (1<<10)
#define	PD_MQ_DB			0x0140
#define	PD_MPU_MBOX_DB			0x0160

/* Hardware Address types */
#define	MAC_ADDRESS_TYPE_STORAGE	0x0	/* (Storage MAC Address) */
#define	MAC_ADDRESS_TYPE_NETWORK	0x1	/* (Network MAC Address) */
#define	MAC_ADDRESS_TYPE_PD		0x2	/* (Protection Domain MAC Addr) */
#define	MAC_ADDRESS_TYPE_MANAGEMENT	0x3	/* (Management MAC Address) */
#define	MAC_ADDRESS_TYPE_FCOE		0x4	/* (FCoE MAC Address) */

/* CREATE_IFACE capability and cap_en flags */
#define MBX_RX_IFACE_RSS		0x000004
#define MBX_RX_IFACE_PROMISC		0x000008
#define MBX_RX_IFACE_BROADCAST		0x000010
#define MBX_RX_IFACE_UNTAGGED		0x000020
#define MBX_RX_IFACE_VLAN_PROMISC	0x000080
#define MBX_RX_IFACE_VLAN		0x000100
#define MBX_RX_IFACE_MCAST_PROMISC	0x000200
#define MBX_RX_IFACE_PASS_L2_ERR	0x000400
#define MBX_RX_IFACE_PASS_L3L4_ERR	0x000800
#define MBX_RX_IFACE_MCAST		0x001000
#define MBX_RX_IFACE_MCAST_HASH		0x002000
#define MBX_RX_IFACE_HDS		0x004000
#define MBX_RX_IFACE_DIRECTED		0x008000
#define MBX_RX_IFACE_VMQ		0x010000
#define MBX_RX_IFACE_NETQ		0x020000
#define MBX_RX_IFACE_QGROUPS		0x040000
#define MBX_RX_IFACE_LSO		0x080000
#define MBX_RX_IFACE_LRO		0x100000

#define	ASYNC_EVENT_CODE_LINK_STATE	0x1
#define	ASYNC_EVENT_LINK_UP		0x1
#define	ASYNC_EVENT_LINK_DOWN		0x0
#define ASYNC_EVENT_GRP5		0x5
#define ASYNC_EVENT_PVID_STATE		0x3
#define VLAN_VID_MASK			0x0FFF

/* port link_status */
#define	ASYNC_EVENT_LOGICAL		0x02

/* Logical Link Status */
#define	NTWK_LOGICAL_LINK_DOWN		0
#define	NTWK_LOGICAL_LINK_UP		1

/* max SGE per mbx */
#define	MAX_MBX_SGE			19

/* Max multicast filter size */
#define OCE_MAX_MC_FILTER_SIZE		32

/* PCI SLI (Service Level Interface) capabilities register */
#define OCE_INTF_REG_OFFSET		0x58
#define OCE_INTF_VALID_SIG		6	/* register's signature */
#define OCE_INTF_FUNC_RESET_REQD	1
#define OCE_INTF_HINT1_NOHINT		0
#define OCE_INTF_HINT1_SEMAINIT		1
#define OCE_INTF_HINT1_STATCTRL		2
#define OCE_INTF_IF_TYPE_0		0
#define OCE_INTF_IF_TYPE_1		1
#define OCE_INTF_IF_TYPE_2		2
#define OCE_INTF_IF_TYPE_3		3
#define OCE_INTF_SLI_REV3		3	/* not supported by driver */
#define OCE_INTF_SLI_REV4		4	/* driver supports SLI-4 */
#define OCE_INTF_PHYS_FUNC		0
#define OCE_INTF_VIRT_FUNC		1
#define OCE_INTF_FAMILY_BE2		0	/* not supported by driver */
#define OCE_INTF_FAMILY_BE3		1	/* driver supports BE3 */
#define OCE_INTF_FAMILY_A0_CHIP		0xA	/* Lancer A0 chip (supported) */
#define OCE_INTF_FAMILY_B0_CHIP		0xB	/* Lancer B0 chip (future) */

#define	NIC_WQE_SIZE			16

#define	NIC_WQ_TYPE_FORWARDING		0x01
#define	NIC_WQ_TYPE_STANDARD		0x02
#define	NIC_WQ_TYPE_LOW_LATENCY		0x04

#define OCE_TXP_SW_SZ			48

#define OCE_SLI_FUNCTION(reg)		((reg) & 0x1)
#define OCE_SLI_REVISION(reg)		(((reg) >> 4) & 0xf)
#define OCE_SLI_FAMILY(reg)		(((reg) >> 8) & 0xf)
#define OCE_SLI_IFTYPE(reg)		(((reg) >> 12) & 0xf)
#define OCE_SLI_HINT1(reg)		(((reg) >> 16) & 0xff)
#define OCE_SLI_HINT2(reg)		(((reg) >> 24) & 0x1f)
#define OCE_SLI_SIGNATURE(reg)		(((reg) >> 29) & 0x7)

#define PD_MPU_MBOX_DB_READY		(1<<0)
#define PD_MPU_MBOX_DB_HI		(1<<1)
#define PD_MPU_MBOX_DB_ADDR_SHIFT	2

struct oce_pa {
	uint64_t		addr;
} __packed;

struct oce_sge {
	uint64_t		addr;
	uint32_t		length;
} __packed;

struct mbx_hdr {
	uint8_t			opcode;
	uint8_t			subsys;
	uint8_t			port;
	uint8_t			domain;
	uint32_t		timeout;
	uint32_t		length;
	uint8_t			version;
#define  OCE_MBX_VER_V2		 0x0002
#define  OCE_MBX_VER_V1		 0x0001
#define  OCE_MBX_VER_V0		 0x0000
	uint8_t			_rsvd[3];
} __packed;

/* payload can contain an SGL or an embedded array of upto 59 dwords */
#define OCE_MBX_PAYLOAD			(59 * 4)

struct oce_mbx {
	uint32_t		flags;
#define  OCE_MBX_F_EMBED	 (1<<0)
#define  OCE_MBX_F_SGE		 (1<<3)
	uint32_t		payload_length;
	uint32_t		tag[2];
	uint32_t		_rsvd;
	union {
		struct oce_sge	sgl[MAX_MBX_SGE];
		uint8_t		data[OCE_MBX_PAYLOAD];
	} pld;
} __packed;

/* completion queue entry for MQ */
struct oce_mq_cqe {
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			/* dw0 */
			uint32_t extended_status:16;
			uint32_t completion_status:16;
			/* dw1 dw2 */
			uint32_t mq_tag[2];
			/* dw3 */
			uint32_t valid:1;
			uint32_t async_event:1;
			uint32_t hpi_buffer_cmpl:1;
			uint32_t completed:1;
			uint32_t consumed:1;
			uint32_t rsvd0:3;
			uint32_t async_type:8;
			uint32_t event_type:8;
			uint32_t rsvd1:8;
#else
			/* dw0 */
			uint32_t completion_status:16;
			uint32_t extended_status:16;
			/* dw1 dw2 */
			uint32_t mq_tag[2];
			/* dw3 */
			uint32_t rsvd1:8;
			uint32_t event_type:8;
			uint32_t async_type:8;
			uint32_t rsvd0:3;
			uint32_t consumed:1;
			uint32_t completed:1;
			uint32_t hpi_buffer_cmpl:1;
			uint32_t async_event:1;
			uint32_t valid:1;
#endif
		} s;
		uint32_t dw[4];
	} u0;
} __packed;

#define	MQ_CQE_VALID(_cqe)		((_cqe)->u0.dw[3])
#define	MQ_CQE_INVALIDATE(_cqe)		((_cqe)->u0.dw[3] = 0)

/* Mailbox Completion Status Codes */
enum MBX_COMPLETION_STATUS {
	MBX_CQE_STATUS_SUCCESS = 0x00,
	MBX_CQE_STATUS_INSUFFICIENT_PRIVILEDGES = 0x01,
	MBX_CQE_STATUS_INVALID_PARAMETER = 0x02,
	MBX_CQE_STATUS_INSUFFICIENT_RESOURCES = 0x03,
	MBX_CQE_STATUS_QUEUE_FLUSHING = 0x04,
	MBX_CQE_STATUS_DMA_FAILED = 0x05
};

struct oce_async_cqe_link_state {
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			/* dw0 */
			uint8_t speed;
			uint8_t duplex;
			uint8_t link_status;
			uint8_t phy_port;
			/* dw1 */
			uint16_t qos_link_speed;
			uint8_t rsvd0;
			uint8_t fault;
			/* dw2 */
			uint32_t event_tag;
			/* dw3 */
			uint32_t valid:1;
			uint32_t async_event:1;
			uint32_t rsvd2:6;
			uint32_t event_type:8;
			uint32_t event_code:8;
			uint32_t rsvd1:8;
#else
			/* dw0 */
			uint8_t phy_port;
			uint8_t link_status;
			uint8_t duplex;
			uint8_t speed;
			/* dw1 */
			uint8_t fault;
			uint8_t rsvd0;
			uint16_t qos_link_speed;
			/* dw2 */
			uint32_t event_tag;
			/* dw3 */
			uint32_t rsvd1:8;
			uint32_t event_code:8;
			uint32_t event_type:8;
			uint32_t rsvd2:6;
			uint32_t async_event:1;
			uint32_t valid:1;
#endif
		} s;
		uint32_t dw[4];
	} u0;
} __packed;

/* PVID aync event */
struct oce_async_event_grp5_pvid_state {
	uint8_t enabled;
	uint8_t rsvd0;
	uint16_t tag;
	uint32_t event_tag;
	uint32_t rsvd1;
	uint32_t code;
} __packed;

union oce_mq_ext_ctx {
	uint32_t dw[6];
	struct {
#if _BYTE_ORDER == BIG_ENDIAN
		/* dw0 */
		uint32_t dw4rsvd1:16;
		uint32_t num_pages:16;
		/* dw1 */
		uint32_t async_evt_bitmap;
		/* dw2 */
		uint32_t cq_id:10;
		uint32_t dw5rsvd2:2;
		uint32_t ring_size:4;
		uint32_t dw5rsvd1:16;
		/* dw3 */
		uint32_t valid:1;
		uint32_t dw6rsvd1:31;
		/* dw4 */
		uint32_t dw7rsvd1:21;
		uint32_t async_cq_id:10;
		uint32_t async_cq_valid:1;
#else
		/* dw0 */
		uint32_t num_pages:16;
		uint32_t dw4rsvd1:16;
		/* dw1 */
		uint32_t async_evt_bitmap;
		/* dw2 */
		uint32_t dw5rsvd1:16;
		uint32_t ring_size:4;
		uint32_t dw5rsvd2:2;
		uint32_t cq_id:10;
		/* dw3 */
		uint32_t dw6rsvd1:31;
		uint32_t valid:1;
		/* dw4 */
		uint32_t async_cq_valid:1;
		uint32_t async_cq_id:10;
		uint32_t dw7rsvd1:21;
#endif
		/* dw5 */
		uint32_t dw8rsvd1;
	} v0;
} __packed;

/* MQ mailbox structure */
struct oce_bmbx {
	struct oce_mbx mbx;
	struct oce_mq_cqe cqe;
} __packed;

/* MBXs sub system codes */
enum SUBSYS_CODES {
	SUBSYS_RSVD = 0,
	SUBSYS_COMMON = 1,
	SUBSYS_COMMON_ISCSI = 2,
	SUBSYS_NIC = 3,
	SUBSYS_TOE = 4,
	SUBSYS_PXE_UNDI = 5,
	SUBSYS_ISCSI_INI = 6,
	SUBSYS_ISCSI_TGT = 7,
	SUBSYS_MILI_PTL = 8,
	SUBSYS_MILI_TMD = 9,
	SUBSYS_RDMA = 10,
	SUBSYS_LOWLEVEL = 11,
	SUBSYS_LRO = 13,
	SUBSYS_DCBX = 15,
	SUBSYS_DIAG = 16,
	SUBSYS_VENDOR = 17
};

/* common ioctl opcodes */
enum COMMON_SUBSYS_OPCODES {
/* These opcodes are common to both networking and storage PCI functions
 * They are used to reserve resources and configure CNA. These opcodes
 * all use the SUBSYS_COMMON subsystem code.
 */
	OPCODE_COMMON_QUERY_IFACE_MAC = 1,
	OPCODE_COMMON_SET_IFACE_MAC = 2,
	OPCODE_COMMON_SET_IFACE_MULTICAST = 3,
	OPCODE_COMMON_CONFIG_IFACE_VLAN = 4,
	OPCODE_COMMON_QUERY_LINK_CONFIG = 5,
	OPCODE_COMMON_READ_FLASHROM = 6,
	OPCODE_COMMON_WRITE_FLASHROM = 7,
	OPCODE_COMMON_QUERY_MAX_MBX_BUFFER_SIZE = 8,
	OPCODE_COMMON_CREATE_CQ = 12,
	OPCODE_COMMON_CREATE_EQ = 13,
	OPCODE_COMMON_CREATE_MQ = 21,
	OPCODE_COMMON_GET_QOS = 27,
	OPCODE_COMMON_SET_QOS = 28,
	OPCODE_COMMON_READ_EPROM = 30,
	OPCODE_COMMON_GET_CNTL_ATTRIBUTES = 32,
	OPCODE_COMMON_NOP = 33,
	OPCODE_COMMON_SET_IFACE_RX_FILTER = 34,
	OPCODE_COMMON_GET_FW_VERSION = 35,
	OPCODE_COMMON_SET_FLOW_CONTROL = 36,
	OPCODE_COMMON_GET_FLOW_CONTROL = 37,
	OPCODE_COMMON_SET_FRAME_SIZE = 39,
	OPCODE_COMMON_MODIFY_EQ_DELAY = 41,
	OPCODE_COMMON_CREATE_IFACE = 50,
	OPCODE_COMMON_DESTROY_IFACE = 51,
	OPCODE_COMMON_MODIFY_MSI_MESSAGES = 52,
	OPCODE_COMMON_DESTROY_MQ = 53,
	OPCODE_COMMON_DESTROY_CQ = 54,
	OPCODE_COMMON_DESTROY_EQ = 55,
	OPCODE_COMMON_UPLOAD_TCP = 56,
	OPCODE_COMMON_SET_NTWK_LINK_SPEED = 57,
	OPCODE_COMMON_QUERY_FIRMWARE_CONFIG = 58,
	OPCODE_COMMON_ADD_IFACE_MAC = 59,
	OPCODE_COMMON_DEL_IFACE_MAC = 60,
	OPCODE_COMMON_FUNCTION_RESET = 61,
	OPCODE_COMMON_SET_PHYSICAL_LINK_CONFIG = 62,
	OPCODE_COMMON_GET_BOOT_CONFIG = 66,
	OPCPDE_COMMON_SET_BOOT_CONFIG = 67,
	OPCODE_COMMON_SET_BEACON_CONFIG = 69,
	OPCODE_COMMON_GET_BEACON_CONFIG = 70,
	OPCODE_COMMON_GET_PHYSICAL_LINK_CONFIG = 71,
	OPCODE_COMMON_GET_OEM_ATTRIBUTES = 76,
	OPCODE_COMMON_GET_PORT_NAME = 77,
	OPCODE_COMMON_GET_CONFIG_SIGNATURE = 78,
	OPCODE_COMMON_SET_CONFIG_SIGNATURE = 79,
	OPCODE_COMMON_SET_LOGICAL_LINK_CONFIG = 80,
	OPCODE_COMMON_GET_BE_CONFIGURATION_RESOURCES = 81,
	OPCODE_COMMON_SET_BE_CONFIGURATION_RESOURCES = 82,
	OPCODE_COMMON_GET_RESET_NEEDED = 84,
	OPCODE_COMMON_GET_SERIAL_NUMBER = 85,
	OPCODE_COMMON_GET_NCSI_CONFIG = 86,
	OPCODE_COMMON_SET_NCSI_CONFIG = 87,
	OPCODE_COMMON_CREATE_MQ_EXT = 90,
	OPCODE_COMMON_SET_FUNCTION_PRIVILEGES = 100,
	OPCODE_COMMON_SET_VF_PORT_TYPE = 101,
	OPCODE_COMMON_GET_PHY_CONFIG = 102,
	OPCODE_COMMON_SET_FUNCTIONAL_CAPS = 103,
	OPCODE_COMMON_GET_ADAPTER_ID = 110,
	OPCODE_COMMON_GET_UPGRADE_FEATURES = 111,
	OPCODE_COMMON_GET_INSTALLED_FEATURES = 112,
	OPCODE_COMMON_GET_AVAIL_PERSONALITIES = 113,
	OPCODE_COMMON_GET_CONFIG_PERSONALITIES = 114,
	OPCODE_COMMON_SEND_ACTIVATION = 115,
	OPCODE_COMMON_RESET_LICENSES = 116,
	OPCODE_COMMON_GET_CNTL_ADDL_ATTRIBUTES = 121,
	OPCODE_COMMON_QUERY_TCB = 144,
	OPCODE_COMMON_ADD_IFACE_QUEUE_FILTER = 145,
	OPCODE_COMMON_DEL_IFACE_QUEUE_FILTER = 146,
	OPCODE_COMMON_GET_IFACE_MAC_LIST = 147,
	OPCODE_COMMON_SET_IFACE_MAC_LIST = 148,
	OPCODE_COMMON_MODIFY_CQ = 149,
	OPCODE_COMMON_GET_IFACE_VLAN_LIST = 150,
	OPCODE_COMMON_SET_IFACE_VLAN_LIST = 151,
	OPCODE_COMMON_GET_HSW_CONFIG = 152,
	OPCODE_COMMON_SET_HSW_CONFIG = 153,
	OPCODE_COMMON_GET_RESOURCE_EXTENT_INFO = 154,
	OPCODE_COMMON_GET_ALLOCATED_RESOURCE_EXTENTS = 155,
	OPCODE_COMMON_ALLOC_RESOURCE_EXTENTS = 156,
	OPCODE_COMMON_DEALLOC_RESOURCE_EXTENTS = 157,
	OPCODE_COMMON_SET_DIAG_REGISTERS = 158,
	OPCODE_COMMON_GET_FUNCTION_CONFIG = 160,
	OPCODE_COMMON_GET_PROFILE_CAPACITIES = 161,
	OPCODE_COMMON_GET_MR_PROFILE_CAPACITIES = 162,
	OPCODE_COMMON_SET_MR_PROFILE_CAPACITIES = 163,
	OPCODE_COMMON_GET_PROFILE_CONFIG = 164,
	OPCODE_COMMON_SET_PROFILE_CONFIG = 165,
	OPCODE_COMMON_GET_PROFILE_LIST = 166,
	OPCODE_COMMON_GET_ACTIVE_PROFILE = 167,
	OPCODE_COMMON_SET_ACTIVE_PROFILE = 168,
	OPCODE_COMMON_GET_FUNCTION_PRIVILEGES = 170,
	OPCODE_COMMON_READ_OBJECT = 171,
	OPCODE_COMMON_WRITE_OBJECT = 172
};

/* [05] OPCODE_COMMON_QUERY_LINK_CONFIG */
struct mbx_query_common_link_config {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t rsvd0;
		} req;

		struct {
			/* dw 0 */
			uint8_t physical_port;
			uint8_t mac_duplex;
			uint8_t mac_speed;
			uint8_t mac_fault;
			/* dw 1 */
			uint8_t mgmt_mac_duplex;
			uint8_t mgmt_mac_speed;
			uint16_t qos_link_speed;
			uint32_t logical_link_status;
		} rsp;
	} params;
} __packed;

/* [57] OPCODE_COMMON_SET_LINK_SPEED */
struct mbx_set_common_link_speed {
	struct mbx_hdr hdr;
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			uint8_t rsvd0;
			uint8_t mac_speed;
			uint8_t virtual_port;
			uint8_t physical_port;
#else
			uint8_t physical_port;
			uint8_t virtual_port;
			uint8_t mac_speed;
			uint8_t rsvd0;
#endif
		} req;

		struct {
			uint32_t rsvd0;
		} rsp;

		uint32_t dw;
	} params;
} __packed;

struct mac_address_format {
	uint16_t size_of_struct;
	uint8_t mac_addr[6];
} __packed;

/* [01] OPCODE_COMMON_QUERY_IFACE_MAC */
struct mbx_query_common_iface_mac {
	struct mbx_hdr hdr;
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			uint16_t if_id;
			uint8_t permanent;
			uint8_t type;
#else
			uint8_t type;
			uint8_t permanent;
			uint16_t if_id;
#endif

		} req;

		struct {
			struct mac_address_format mac;
		} rsp;
	} params;
} __packed;

/* [02] OPCODE_COMMON_SET_IFACE_MAC */
struct mbx_set_common_iface_mac {
	struct mbx_hdr hdr;
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			/* dw 0 */
			uint16_t if_id;
			uint8_t invalidate;
			uint8_t type;
#else
			/* dw 0 */
			uint8_t type;
			uint8_t invalidate;
			uint16_t if_id;
#endif
			/* dw 1 */
			struct mac_address_format mac;
		} req;

		struct {
			uint32_t rsvd0;
		} rsp;

		uint32_t dw[2];
	} params;
} __packed;

/* [03] OPCODE_COMMON_SET_IFACE_MULTICAST */
struct mbx_set_common_iface_multicast {
	struct mbx_hdr hdr;
	union {
		struct {
			/* dw 0 */
			uint16_t num_mac;
			uint8_t promiscuous;
			uint8_t if_id;
			/* dw 1-48 */
			struct {
				uint8_t byte[6];
			} mac[32];

		} req;

		struct {
			uint32_t rsvd0;
		} rsp;

		uint32_t dw[49];
	} params;
} __packed;

struct qinq_vlan {
#if _BYTE_ORDER == BIG_ENDIAN
	uint16_t inner;
	uint16_t outer;
#else
	uint16_t outer;
	uint16_t inner;
#endif
} __packed;

struct normal_vlan {
	uint16_t vtag;
} __packed;

struct ntwk_if_vlan_tag {
	union {
		struct normal_vlan normal;
		struct qinq_vlan qinq;
	} u0;
} __packed;

/* [50] OPCODE_COMMON_CREATE_IFACE */
struct mbx_create_common_iface {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t version;
			uint32_t cap_flags;
			uint32_t enable_flags;
			uint8_t mac_addr[6];
			uint8_t rsvd0;
			uint8_t mac_invalid;
			struct ntwk_if_vlan_tag vlan_tag;
		} req;

		struct {
			uint32_t if_id;
			uint32_t pmac_id;
		} rsp;
		uint32_t dw[4];
	} params;
} __packed;

/* [51] OPCODE_COMMON_DESTROY_IFACE */
struct mbx_destroy_common_iface {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t if_id;
		} req;

		struct {
			uint32_t rsvd0;
		} rsp;

		uint32_t dw;
	} params;
} __packed;

/*
 * Event Queue Entry
 */
struct oce_eqe {
	uint32_t evnt;
} __packed;

/* event queue context structure */
struct oce_eq_ctx {
#if _BYTE_ORDER == BIG_ENDIAN
	uint32_t dw4rsvd1:16;
	uint32_t num_pages:16;

	uint32_t size:1;
	uint32_t dw5rsvd2:1;
	uint32_t valid:1;
	uint32_t dw5rsvd1:29;

	uint32_t armed:1;
	uint32_t dw6rsvd2:2;
	uint32_t count:3;
	uint32_t dw6rsvd1:26;

	uint32_t dw7rsvd2:9;
	uint32_t delay_mult:10;
	uint32_t dw7rsvd1:13;

	uint32_t dw8rsvd1;
#else
	uint32_t num_pages:16;
	uint32_t dw4rsvd1:16;

	uint32_t dw5rsvd1:29;
	uint32_t valid:1;
	uint32_t dw5rsvd2:1;
	uint32_t size:1;

	uint32_t dw6rsvd1:26;
	uint32_t count:3;
	uint32_t dw6rsvd2:2;
	uint32_t armed:1;

	uint32_t dw7rsvd1:13;
	uint32_t delay_mult:10;
	uint32_t dw7rsvd2:9;

	uint32_t dw8rsvd1;
#endif
} __packed;

/* [13] OPCODE_COMMON_CREATE_EQ */
struct mbx_create_common_eq {
	struct mbx_hdr hdr;
	union {
		struct {
			struct oce_eq_ctx ctx;
			struct oce_pa pages[8];
		} req;

		struct {
			uint16_t eq_id;
			uint16_t rsvd0;
		} rsp;
	} params;
} __packed;

/* [55] OPCODE_COMMON_DESTROY_EQ */
struct mbx_destroy_common_eq {
	struct mbx_hdr hdr;
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			uint16_t rsvd0;
			uint16_t id;
#else
			uint16_t id;
			uint16_t rsvd0;
#endif
		} req;

		struct {
			uint32_t rsvd0;
		} rsp;
	} params;
} __packed;

/* SLI-4 CQ context - use version V0 for B3, version V2 for Lancer */
union oce_cq_ctx {
	uint32_t dw[5];
	struct {
#if _BYTE_ORDER == BIG_ENDIAN
		/* dw4 */
		uint32_t dw4rsvd1:16;
		uint32_t num_pages:16;
		/* dw5 */
		uint32_t eventable:1;
		uint32_t dw5rsvd3:1;
		uint32_t valid:1;
		uint32_t count:2;
		uint32_t dw5rsvd2:12;
		uint32_t nodelay:1;
		uint32_t coalesce_wm:2;
		uint32_t dw5rsvd1:12;
		/* dw6 */
		uint32_t armed:1;
		uint32_t dw6rsvd2:1;
		uint32_t eq_id:8;
		uint32_t dw6rsvd1:22;
#else
		/* dw4 */
		uint32_t num_pages:16;
		uint32_t dw4rsvd1:16;
		/* dw5 */
		uint32_t dw5rsvd1:12;
		uint32_t coalesce_wm:2;
		uint32_t nodelay:1;
		uint32_t dw5rsvd2:12;
		uint32_t count:2;
		uint32_t valid:1;
		uint32_t dw5rsvd3:1;
		uint32_t eventable:1;
		/* dw6 */
		uint32_t dw6rsvd1:22;
		uint32_t eq_id:8;
		uint32_t dw6rsvd2:1;
		uint32_t armed:1;
#endif
		/* dw7 */
		uint32_t dw7rsvd1;
		/* dw8 */
		uint32_t dw8rsvd1;
	} v0;
	struct {
#if _BYTE_ORDER == BIG_ENDIAN
		/* dw4 */
		uint32_t dw4rsvd1:8;
		uint32_t page_size:8;
		uint32_t num_pages:16;
		/* dw5 */
		uint32_t eventable:1;
		uint32_t dw5rsvd3:1;
		uint32_t valid:1;
		uint32_t count:2;
		uint32_t dw5rsvd2:11;
		uint32_t autovalid:1;
		uint32_t nodelay:1;
		uint32_t coalesce_wm:2;
		uint32_t dw5rsvd1:12;
		/* dw6 */
		uint32_t armed:1;
		uint32_t dw6rsvd1:15;
		uint32_t eq_id:16;
		/* dw7 */
		uint32_t dw7rsvd1:16;
		uint32_t cqe_count:16;
#else
		/* dw4 */
		uint32_t num_pages:16;
		uint32_t page_size:8;
		uint32_t dw4rsvd1:8;
		/* dw5 */
		uint32_t dw5rsvd1:12;
		uint32_t coalesce_wm:2;
		uint32_t nodelay:1;
		uint32_t autovalid:1;
		uint32_t dw5rsvd2:11;
		uint32_t count:2;
		uint32_t valid:1;
		uint32_t dw5rsvd3:1;
		uint32_t eventable:1;
		/* dw6 */
		uint32_t eq_id:8;
		uint32_t dw6rsvd1:15;
		uint32_t armed:1;
		/* dw7 */
		uint32_t cqe_count:16;
		uint32_t dw7rsvd1:16;
#endif
		/* dw8 */
		uint32_t dw8rsvd1;
	} v2;
} __packed;

/* [12] OPCODE_COMMON_CREATE_CQ */
struct mbx_create_common_cq {
	struct mbx_hdr hdr;
	union {
		struct {
			union oce_cq_ctx cq_ctx;
			struct oce_pa pages[4];
		} req;

		struct {
			uint16_t cq_id;
			uint16_t rsvd0;
		} rsp;
	} params;
} __packed;

/* [54] OPCODE_COMMON_DESTROY_CQ */
struct mbx_destroy_common_cq {
	struct mbx_hdr hdr;
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			uint16_t rsvd0;
			uint16_t id;
#else
			uint16_t id;
			uint16_t rsvd0;
#endif
		} req;

		struct {
			uint32_t rsvd0;
		} rsp;
	} params;
} __packed;

union oce_mq_ctx {
	uint32_t dw[5];
	struct {
#if _BYTE_ORDER == BIG_ENDIAN
		/* dw4 */
		uint32_t dw4rsvd1:16;
		uint32_t num_pages:16;
		/* dw5 */
		uint32_t cq_id:10;
		uint32_t dw5rsvd2:2;
		uint32_t ring_size:4;
		uint32_t dw5rsvd1:16;
		/* dw6 */
		uint32_t valid:1;
		uint32_t dw6rsvd1:31;
		/* dw7 */
		uint32_t dw7rsvd1:21;
		uint32_t async_cq_id:10;
		uint32_t async_cq_valid:1;
#else
		/* dw4 */
		uint32_t num_pages:16;
		uint32_t dw4rsvd1:16;
		/* dw5 */
		uint32_t dw5rsvd1:16;
		uint32_t ring_size:4;
		uint32_t dw5rsvd2:2;
		uint32_t cq_id:10;
		/* dw6 */
		uint32_t dw6rsvd1:31;
		uint32_t valid:1;
		/* dw7 */
		uint32_t async_cq_valid:1;
		uint32_t async_cq_id:10;
		uint32_t dw7rsvd1:21;
#endif
		/* dw8 */
		uint32_t dw8rsvd1;
	} v0;
} __packed;

/**
 * @brief [21] OPCODE_COMMON_CREATE_MQ
 * A MQ must be at least 16 entries deep (corresponding to 1 page) and
 * at most 128 entries deep (corresponding to 8 pages).
 */
struct mbx_create_common_mq {
	struct mbx_hdr hdr;
	union {
		struct {
			union oce_mq_ctx context;
			struct oce_pa pages[8];
		} req;

		struct {
			uint32_t mq_id:16;
			uint32_t rsvd0:16;
		} rsp;
	} params;
} __packed;

struct mbx_create_common_mq_ex {
	struct mbx_hdr hdr;
	union {
		struct {
			union oce_mq_ext_ctx context;
			struct oce_pa pages[8];
		} req;

		struct {
			uint32_t mq_id:16;
			uint32_t rsvd0:16;
		} rsp;
	} params;
} __packed;

/* [53] OPCODE_COMMON_DESTROY_MQ */
struct mbx_destroy_common_mq {
	struct mbx_hdr hdr;
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			uint16_t rsvd0;
			uint16_t id;
#else
			uint16_t id;
			uint16_t rsvd0;
#endif
		} req;

		struct {
			uint32_t rsvd0;
		} rsp;
	} params;
} __packed;

/* [35] OPCODE_COMMON_GET_ FW_VERSION */
struct mbx_get_common_fw_version {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t rsvd0;
		} req;

		struct {
			uint8_t fw_ver_str[32];
			uint8_t fw_on_flash_ver_str[32];
		} rsp;
	} params;
} __packed;

/* [52] OPCODE_COMMON_CEV_MODIFY_MSI_MESSAGES */
struct mbx_common_cev_modify_msi_messages {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t num_msi_msgs;
		} req;

		struct {
			uint32_t rsvd0;
		} rsp;
	} params;
} __packed;

/* [36] OPCODE_COMMON_SET_FLOW_CONTROL */
/* [37] OPCODE_COMMON_GET_FLOW_CONTROL */
struct mbx_common_get_set_flow_control {
	struct mbx_hdr hdr;
#if _BYTE_ORDER == BIG_ENDIAN
	uint16_t tx_flow_control;
	uint16_t rx_flow_control;
#else
	uint16_t rx_flow_control;
	uint16_t tx_flow_control;
#endif
} __packed;

struct oce_phy_info {
	uint16_t phy_type;
	uint16_t interface_type;
	uint32_t misc_params;
	uint16_t ext_phy_details;
	uint16_t rsvd;
	uint16_t auto_speeds_supported;
	uint16_t fixed_speeds_supported;
	uint32_t future_use[2];
} __packed;

struct mbx_common_phy_info {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t rsvd0[4];
		} req;
		struct {
			struct oce_phy_info phy_info;
		} rsp;
	} params;
} __packed;

/*Lancer firmware*/

struct mbx_lancer_common_write_object {
	union {
		struct {
			struct	 mbx_hdr hdr;
			uint32_t write_length: 24;
			uint32_t rsvd: 7;
			uint32_t eof: 1;
			uint32_t write_offset;
			uint8_t  object_name[104];
			uint32_t descriptor_count;
			uint32_t buffer_length;
			uint32_t address_lower;
			uint32_t address_upper;
		} req;
		struct {
			uint8_t  opcode;
			uint8_t  subsystem;
			uint8_t  rsvd1[2];
			uint8_t  status;
			uint8_t  additional_status;
			uint8_t  rsvd2[2];
			uint32_t response_length;
			uint32_t actual_response_length;
			uint32_t actual_write_length;
		} rsp;
	} params;
} __packed;

/**
 * @brief MBX Common Query Firmware Config
 * This command retrieves firmware configuration parameters and adapter
 * resources available to the driver originating the request. The firmware
 * configuration defines supported protocols by the installed adapter firmware.
 * This includes which ULP processors support the specified protocols and
 * the number of TCP connections allowed for that protocol.
 */
struct mbx_common_query_fw_config {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t rsvd0[30];
		} req;

		struct {
			uint32_t config_number;
			uint32_t asic_revision;
			uint32_t port_id;	/* used for stats retrieval */
			uint32_t function_mode;
			struct {

				uint32_t ulp_mode;
				uint32_t nic_wqid_base;
				uint32_t nic_wq_tot;
				uint32_t toe_wqid_base;
				uint32_t toe_wq_tot;
				uint32_t toe_rqid_base;
				uint32_t toe_rqid_tot;
				uint32_t toe_defrqid_base;
				uint32_t toe_defrqid_count;
				uint32_t lro_rqid_base;
				uint32_t lro_rqid_tot;
				uint32_t iscsi_icd_base;
				uint32_t iscsi_icd_count;
			} ulp[2];
			uint32_t function_caps;
			uint32_t cqid_base;
			uint32_t cqid_tot;
			uint32_t eqid_base;
			uint32_t eqid_tot;
		} rsp;
	} params;
} __packed;

enum CQFW_CONFIG_NUMBER {
	FCN_NIC_ISCSI_Initiator = 0x0,
	FCN_ISCSI_Target = 0x3,
	FCN_FCoE = 0x7,
	FCN_ISCSI_Initiator_Target = 0x9,
	FCN_NIC_RDMA_TOE = 0xA,
	FCN_NIC_RDMA_FCoE = 0xB,
	FCN_NIC_RDMA_iSCSI = 0xC,
	FCN_NIC_iSCSI_FCoE = 0xD
};

/**
 * @brief Function Capabilities
 * This field contains the flags indicating the capabilities of
 * the SLI Host’s PCI function.
 */
enum CQFW_FUNCTION_CAPABILITIES {
	FNC_UNCLASSIFIED_STATS = 0x1,
	FNC_RSS = 0x2,
	FNC_PROMISCUOUS = 0x4,
	FNC_LEGACY_MODE = 0x8,
	FNC_HDS = 0x4000,
	FNC_VMQ = 0x10000,
	FNC_NETQ = 0x20000,
	FNC_QGROUPS = 0x40000,
	FNC_LRO = 0x100000,
	FNC_VLAN_OFFLOAD = 0x800000
};

enum CQFW_ULP_MODES_SUPPORTED {
	ULP_TOE_MODE = 0x1,
	ULP_NIC_MODE = 0x2,
	ULP_RDMA_MODE = 0x4,
	ULP_ISCSI_INI_MODE = 0x10,
	ULP_ISCSI_TGT_MODE = 0x20,
	ULP_FCOE_INI_MODE = 0x40,
	ULP_FCOE_TGT_MODE = 0x80,
	ULP_DAL_MODE = 0x100,
	ULP_LRO_MODE = 0x200
};

/**
 * @brief Function Modes Supported
 * Valid function modes (or protocol-types) supported on the SLI-Host’s
 * PCIe function.  This field is a logical OR of the following values:
 */
enum CQFW_FUNCTION_MODES_SUPPORTED {
	FNM_TOE_MODE = 0x1,		/* TCP offload supported */
	FNM_NIC_MODE = 0x2,		/* Raw Ethernet supported */
	FNM_RDMA_MODE = 0x4,		/* RDMA protocol supported */
	FNM_VM_MODE = 0x8,		/* Virtual Machines supported  */
	FNM_ISCSI_INI_MODE = 0x10,	/* iSCSI initiator supported */
	FNM_ISCSI_TGT_MODE = 0x20,	/* iSCSI target plus initiator */
	FNM_FCOE_INI_MODE = 0x40,	/* FCoE Initiator supported */
	FNM_FCOE_TGT_MODE = 0x80,	/* FCoE target supported */
	FNM_DAL_MODE = 0x100,		/* DAL supported */
	FNM_LRO_MODE = 0x200,		/* LRO supported */
	FNM_FLEX10_MODE = 0x400,	/* QinQ, FLEX-10 or VNIC */
	FNM_NCSI_MODE = 0x800,		/* NCSI supported */
	FNM_IPV6_MODE = 0x1000,		/* IPV6 stack enabled */
	FNM_BE2_COMPAT_MODE = 0x2000,	/* BE2 compatibility (BE3 disable)*/
	FNM_INVALID_MODE = 0x8000,	/* Invalid */
	FNM_BE3_COMPAT_MODE = 0x10000,	/* BE3 features */
	FNM_VNIC_MODE = 0x20000,	/* Set when IBM vNIC mode is set */
	FNM_VNTAG_MODE = 0x40000, 	/* Set when VNTAG mode is set */
	FNM_UMC_MODE = 0x1000000,	/* Set when UMC mode is set */
	FNM_UMC_DEF_EN = 0x100000,	/* Set when UMC Default is set */
	FNM_ONE_GB_EN = 0x200000,	/* Set when 1GB Default is set */
	FNM_VNIC_DEF_VALID = 0x400000,	/* Set when VNIC_DEF_EN is valid */
	FNM_VNIC_DEF_EN = 0x800000	/* Set when VNIC Default enabled */
};

struct mbx_common_config_vlan {
	struct mbx_hdr hdr;
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			uint8_t num_vlans;
			uint8_t untagged;
			uint8_t promisc;
			uint8_t if_id;
#else
			uint8_t if_id;
			uint8_t promisc;
			uint8_t untagged;
			uint8_t num_vlans;
#endif
			union {
				struct normal_vlan normal_vlans[64];
				struct qinq_vlan qinq_vlans[32];
			} tags;
		} req;

		struct {
			uint32_t rsvd;
		} rsp;
	} params;
} __packed;

struct iface_rx_filter_ctx {
	uint32_t global_flags_mask;
	uint32_t global_flags;
	uint32_t iface_flags_mask;
	uint32_t iface_flags;
	uint32_t if_id;
	#define IFACE_RX_NUM_MCAST_MAX		64
	uint32_t num_mcast;
	struct mbx_mcast_addr {
		uint8_t byte[6];
	} mac[IFACE_RX_NUM_MCAST_MAX];
} __packed;

/* [34] OPCODE_COMMON_SET_IFACE_RX_FILTER */
struct mbx_set_common_iface_rx_filter {
	struct mbx_hdr hdr;
	union {
		struct iface_rx_filter_ctx req;
		struct iface_rx_filter_ctx rsp;
	} params;
} __packed;

/* [41] OPCODE_COMMON_MODIFY_EQ_DELAY */
struct mbx_modify_common_eq_delay {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t num_eq;
			struct {
				uint32_t eq_id;
				uint32_t phase;
				uint32_t dm;
			} delay[8];
		} req;

		struct {
			uint32_t rsvd0;
		} rsp;
	} params;
} __packed;

/* [59] OPCODE_ADD_COMMON_IFACE_MAC */
struct mbx_add_common_iface_mac {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t if_id;
			uint8_t mac_address[6];
			uint8_t rsvd0[2];
		} req;
		struct {
			uint32_t pmac_id;
		} rsp;
	} params;
} __packed;

/* [60] OPCODE_DEL_COMMON_IFACE_MAC */
struct mbx_del_common_iface_mac {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t if_id;
			uint32_t pmac_id;
		} req;
		struct {
			uint32_t rsvd0;
		} rsp;
	} params;
} __packed;

/* [8] OPCODE_QUERY_COMMON_MAX_MBX_BUFFER_SIZE */
struct mbx_query_common_max_mbx_buffer_size {
	struct mbx_hdr hdr;
	struct {
		uint32_t max_ioctl_bufsz;
	} rsp;
} __packed;

/* [61] OPCODE_COMMON_FUNCTION_RESET */
struct ioctl_common_function_reset {
	struct mbx_hdr hdr;
} __packed;

/* [80] OPCODE_COMMON_FUNCTION_LINK_CONFIG */
struct mbx_common_func_link_cfg {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t enable;
		} req;
		struct {
			uint32_t rsvd0;
		} rsp;
	} params;
} __packed;

/* [103] OPCODE_COMMON_SET_FUNCTIONAL_CAPS */
#define CAP_SW_TIMESTAMPS	2
#define CAP_BE3_NATIVE_ERX_API	4

struct mbx_common_set_function_cap {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t valid_capability_flags;
			uint32_t capability_flags;
			uint8_t  sbz[212];
		} req;
		struct {
			uint32_t valid_capability_flags;
			uint32_t capability_flags;
			uint8_t  sbz[212];
		} rsp;
	} params;
} __packed;
struct mbx_lowlevel_test_loopback_mode {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t loopback_type;
			uint32_t num_pkts;
			uint64_t pattern;
			uint32_t src_port;
			uint32_t dest_port;
			uint32_t pkt_size;
		}req;
		struct {
			uint32_t    status;
			uint32_t    num_txfer;
			uint32_t    num_rx;
			uint32_t    miscomp_off;
			uint32_t    ticks_compl;
		}rsp;
	} params;
} __packed;

struct mbx_lowlevel_set_loopback_mode {
	struct mbx_hdr hdr;
	union {
		struct {
			uint8_t src_port;
			uint8_t dest_port;
			uint8_t loopback_type;
			uint8_t loopback_state;
		} req;
		struct {
			uint8_t rsvd0[4];
		} rsp;
	} params;
} __packed;

enum LOWLEVEL_SUBSYS_OPCODES {
/* Opcodes used for lowlevel functions common to many subsystems.
 * Some of these opcodes are used for diagnostic functions only.
 * These opcodes use the SUBSYS_LOWLEVEL subsystem code.
 */
	OPCODE_LOWLEVEL_TEST_LOOPBACK = 18,
	OPCODE_LOWLEVEL_SET_LOOPBACK_MODE = 19,
	OPCODE_LOWLEVEL_GET_LOOPBACK_MODE = 20
};

enum LLDP_SUBSYS_OPCODES {
/* Opcodes used for LLDP subsystem for configuring the LLDP state machines. */
	OPCODE_LLDP_GET_CFG = 1,
	OPCODE_LLDP_SET_CFG = 2,
	OPCODE_LLDP_GET_STATS = 3
};

enum DCBX_SUBSYS_OPCODES {
/* Opcodes used for DCBX. */
	OPCODE_DCBX_GET_CFG = 1,
	OPCODE_DCBX_SET_CFG = 2,
	OPCODE_DCBX_GET_MIB_INFO = 3,
	OPCODE_DCBX_GET_DCBX_MODE = 4,
	OPCODE_DCBX_SET_MODE = 5
};

enum DMTF_SUBSYS_OPCODES {
/* Opcodes used for DCBX subsystem. */
	OPCODE_DMTF_EXEC_CLP_CMD = 1
};

enum DIAG_SUBSYS_OPCODES {
/* Opcodes used for diag functions common to many subsystems. */
	OPCODE_DIAG_RUN_DMA_TEST = 1,
	OPCODE_DIAG_RUN_MDIO_TEST = 2,
	OPCODE_DIAG_RUN_NLB_TEST = 3,
	OPCODE_DIAG_RUN_ARM_TIMER_TEST = 4,
	OPCODE_DIAG_GET_MAC = 5
};

enum VENDOR_SUBSYS_OPCODES {
/* Opcodes used for Vendor subsystem. */
	OPCODE_VENDOR_SLI = 1
};

/* Management Status Codes */
enum MGMT_STATUS_SUCCESS {
	MGMT_SUCCESS = 0,
	MGMT_FAILED = 1,
	MGMT_ILLEGAL_REQUEST = 2,
	MGMT_ILLEGAL_FIELD = 3,
	MGMT_INSUFFICIENT_BUFFER = 4,
	MGMT_UNAUTHORIZED_REQUEST = 5,
	MGMT_INVALID_ISNS_ADDRESS = 10,
	MGMT_INVALID_IPADDR = 11,
	MGMT_INVALID_GATEWAY = 12,
	MGMT_INVALID_SUBNETMASK = 13,
	MGMT_INVALID_TARGET_IPADDR = 16,
	MGMT_TGTTBL_FULL = 20,
	MGMT_FLASHROM_SAVE_FAILED = 23,
	MGMT_IOCTLHANDLE_ALLOC_FAILED = 27,
	MGMT_INVALID_SESSION = 31,
	MGMT_INVALID_CONNECTION = 32,
	MGMT_BTL_PATH_EXCEEDS_OSM_LIMIT = 33,
	MGMT_BTL_TGTID_EXCEEDS_OSM_LIMIT = 34,
	MGMT_BTL_PATH_TGTID_OCCUPIED = 35,
	MGMT_BTL_NO_FREE_SLOT_PATH = 36,
	MGMT_BTL_NO_FREE_SLOT_TGTID = 37,
	MGMT_POLL_IOCTL_TIMEOUT = 40,
	MGMT_ERROR_ACITISCSI = 41,
	MGMT_BUFFER_SIZE_EXCEED_OSM_OR_OS_LIMIT = 43,
	MGMT_REBOOT_REQUIRED = 44,
	MGMT_INSUFFICIENT_TIMEOUT = 45,
	MGMT_IPADDR_NOT_SET = 46,
	MGMT_IPADDR_DUP_DETECTED = 47,
	MGMT_CANT_REMOVE_LAST_CONNECTION = 48,
	MGMT_TARGET_BUSY = 49,
	MGMT_TGT_ERR_LISTEN_SOCKET = 50,
	MGMT_TGT_ERR_BIND_SOCKET = 51,
	MGMT_TGT_ERR_NO_SOCKET = 52,
	MGMT_TGT_ERR_ISNS_COMM_FAILED = 55,
	MGMT_CANNOT_DELETE_BOOT_TARGET = 56,
	MGMT_TGT_PORTAL_MODE_IN_LISTEN = 57,
	MGMT_FCF_IN_USE = 58 ,
	MGMT_NO_CQE = 59,
	MGMT_TARGET_NOT_FOUND = 65,
	MGMT_NOT_SUPPORTED = 66,
	MGMT_NO_FCF_RECORDS = 67,
	MGMT_FEATURE_NOT_SUPPORTED = 68,
	MGMT_VPD_FUNCTION_OUT_OF_RANGE = 69,
	MGMT_VPD_FUNCTION_TYPE_INCORRECT = 70,
	MGMT_INVALID_NON_EMBEDDED_WRB = 71,
	MGMT_OOR = 100,
	MGMT_INVALID_PD = 101,
	MGMT_STATUS_PD_INUSE = 102,
	MGMT_INVALID_CQ = 103,
	MGMT_INVALID_QP = 104,
	MGMT_INVALID_STAG = 105,
	MGMT_ORD_EXCEEDS = 106,
	MGMT_IRD_EXCEEDS = 107,
	MGMT_SENDQ_WQE_EXCEEDS = 108,
	MGMT_RECVQ_RQE_EXCEEDS = 109,
	MGMT_SGE_SEND_EXCEEDS = 110,
	MGMT_SGE_WRITE_EXCEEDS = 111,
	MGMT_SGE_RECV_EXCEEDS = 112,
	MGMT_INVALID_STATE_CHANGE = 113,
	MGMT_MW_BOUND = 114,
	MGMT_INVALID_VA = 115,
	MGMT_INVALID_LENGTH = 116,
	MGMT_INVALID_FBO = 117,
	MGMT_INVALID_ACC_RIGHTS = 118,
	MGMT_INVALID_PBE_SIZE = 119,
	MGMT_INVALID_PBL_ENTRY = 120,
	MGMT_INVALID_PBL_OFFSET = 121,
	MGMT_ADDR_NON_EXIST = 122,
	MGMT_INVALID_VLANID = 123,
	MGMT_INVALID_MTU = 124,
	MGMT_INVALID_BACKLOG = 125,
	MGMT_CONNECTION_INPROGRESS = 126,
	MGMT_INVALID_RQE_SIZE = 127,
	MGMT_INVALID_RQE_ENTRY = 128
};

/* Additional Management Status Codes */
enum MGMT_ADDI_STATUS {
	MGMT_ADDI_NO_STATUS = 0,
	MGMT_ADDI_INVALID_IPTYPE = 1,
	MGMT_ADDI_TARGET_HANDLE_NOT_FOUND = 9,
	MGMT_ADDI_SESSION_HANDLE_NOT_FOUND = 10,
	MGMT_ADDI_CONNECTION_HANDLE_NOT_FOUND = 11,
	MGMT_ADDI_ACTIVE_SESSIONS_PRESENT = 16,
	MGMT_ADDI_SESSION_ALREADY_OPENED = 17,
	MGMT_ADDI_SESSION_ALREADY_CLOSED = 18,
	MGMT_ADDI_DEST_HOST_UNREACHABLE = 19,
	MGMT_ADDI_LOGIN_IN_PROGRESS = 20,
	MGMT_ADDI_TCP_CONNECT_FAILED = 21,
	MGMT_ADDI_INSUFFICIENT_RESOURCES = 22,
	MGMT_ADDI_LINK_DOWN = 23,
	MGMT_ADDI_DHCP_ERROR = 24,
	MGMT_ADDI_CONNECTION_OFFLOADED = 25,
	MGMT_ADDI_CONNECTION_NOT_OFFLOADED = 26,
	MGMT_ADDI_CONNECTION_UPLOAD_IN_PROGRESS = 27,
	MGMT_ADDI_REQUEST_REJECTED = 28,
	MGMT_ADDI_INVALID_SUBSYSTEM = 29,
	MGMT_ADDI_INVALID_OPCODE = 30,
	MGMT_ADDI_INVALID_MAXCONNECTION_PARAM = 31,
	MGMT_ADDI_INVALID_KEY = 32,
	MGMT_ADDI_INVALID_DOMAIN = 35,
	MGMT_ADDI_LOGIN_INITIATOR_ERROR = 43,
	MGMT_ADDI_LOGIN_AUTHENTICATION_ERROR = 44,
	MGMT_ADDI_LOGIN_AUTHORIZATION_ERROR = 45,
	MGMT_ADDI_LOGIN_NOT_FOUND = 46,
	MGMT_ADDI_LOGIN_TARGET_REMOVED = 47,
	MGMT_ADDI_LOGIN_UNSUPPORTED_VERSION = 48,
	MGMT_ADDI_LOGIN_TOO_MANY_CONNECTIONS = 49,
	MGMT_ADDI_LOGIN_MISSING_PARAMETER = 50,
	MGMT_ADDI_LOGIN_NO_SESSION_SPANNING = 51,
	MGMT_ADDI_LOGIN_SESSION_TYPE_NOT_SUPPORTED = 52,
	MGMT_ADDI_LOGIN_SESSION_DOES_NOT_EXIST = 53,
	MGMT_ADDI_LOGIN_INVALID_DURING_LOGIN = 54,
	MGMT_ADDI_LOGIN_TARGET_ERROR = 55,
	MGMT_ADDI_LOGIN_SERVICE_UNAVAILABLE = 56,
	MGMT_ADDI_LOGIN_OUT_OF_RESOURCES = 57,
	MGMT_ADDI_SAME_CHAP_SECRET = 58,
	MGMT_ADDI_INVALID_SECRET_LENGTH = 59,
	MGMT_ADDI_DUPLICATE_ENTRY = 60,
	MGMT_ADDI_SETTINGS_MODIFIED_REBOOT_REQD = 63,
	MGMT_ADDI_INVALID_EXTENDED_TIMEOUT = 64,
	MGMT_ADDI_INVALID_INTERFACE_HANDLE = 65,
	MGMT_ADDI_ERR_VLAN_ON_DEF_INTERFACE = 66,
	MGMT_ADDI_INTERFACE_DOES_NOT_EXIST = 67,
	MGMT_ADDI_INTERFACE_ALREADY_EXISTS = 68,
	MGMT_ADDI_INVALID_VLAN_RANGE = 69,
	MGMT_ADDI_ERR_SET_VLAN = 70,
	MGMT_ADDI_ERR_DEL_VLAN = 71,
	MGMT_ADDI_CANNOT_DEL_DEF_INTERFACE = 72,
	MGMT_ADDI_DHCP_REQ_ALREADY_PENDING = 73,
	MGMT_ADDI_TOO_MANY_INTERFACES = 74,
	MGMT_ADDI_INVALID_REQUEST = 75
};

enum NIC_SUBSYS_OPCODES {
/**
 * @brief NIC Subsystem Opcodes (see Network SLI-4 manual >= Rev4, v21-2)
 * These opcodes are used for configuring the Ethernet interfaces.
 * These opcodes all use the SUBSYS_NIC subsystem code.
 */
	OPCODE_NIC_CONFIG_RSS = 1,
	OPCODE_NIC_CONFIG_ACPI = 2,
	OPCODE_NIC_CONFIG_PROMISCUOUS = 3,
	OPCODE_NIC_GET_STATS = 4,
	OPCODE_NIC_CREATE_WQ = 7,
	OPCODE_NIC_CREATE_RQ = 8,
	OPCODE_NIC_DELETE_WQ = 9,
	OPCODE_NIC_DELETE_RQ = 10,
	OPCODE_NIC_CONFIG_ACPI_WOL_MAGIC = 12,
	OPCODE_NIC_GET_NETWORK_STATS = 13,
	OPCODE_NIC_CREATE_HDS_RQ = 16,
	OPCODE_NIC_DELETE_HDS_RQ = 17,
	OPCODE_NIC_GET_PPORT_STATS = 18,
	OPCODE_NIC_GET_VPORT_STATS = 19,
	OPCODE_NIC_GET_QUEUE_STATS = 20
};

/* NIC header WQE */
struct oce_nic_hdr_wqe {
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			/* dw0 */
			uint32_t rsvd0;

			/* dw1 */
			uint32_t last_seg_udp_len:14;
			uint32_t rsvd1:18;

			/* dw2 */
			uint32_t lso_mss:14;
			uint32_t num_wqe:5;
			uint32_t rsvd4:2;
			uint32_t vlan:1;
			uint32_t lso:1;
			uint32_t tcpcs:1;
			uint32_t udpcs:1;
			uint32_t ipcs:1;
			uint32_t rsvd3:1;
			uint32_t rsvd2:1;
			uint32_t forward:1;
			uint32_t crc:1;
			uint32_t event:1;
			uint32_t complete:1;

			/* dw3 */
			uint32_t vlan_tag:16;
			uint32_t total_length:16;
#else
			/* dw0 */
			uint32_t rsvd0;

			/* dw1 */
			uint32_t rsvd1:18;
			uint32_t last_seg_udp_len:14;

			/* dw2 */
			uint32_t complete:1;
			uint32_t event:1;
			uint32_t crc:1;
			uint32_t forward:1;
			uint32_t rsvd2:1;
			uint32_t rsvd3:1;
			uint32_t ipcs:1;
			uint32_t udpcs:1;
			uint32_t tcpcs:1;
			uint32_t lso:1;
			uint32_t vlan:1;
			uint32_t rsvd4:2;
			uint32_t num_wqe:5;
			uint32_t lso_mss:14;

			/* dw3 */
			uint32_t total_length:16;
			uint32_t vlan_tag:16;
#endif
		} s;
		uint32_t dw[4];
	} u0;
} __packed;

/* NIC fragment WQE */
struct oce_nic_frag_wqe {
	union {
		struct {
			/* dw0 */
			uint32_t frag_pa_hi;
			/* dw1 */
			uint32_t frag_pa_lo;
			/* dw2 */
			uint32_t rsvd0;
			uint32_t frag_len;
		} s;
		uint32_t dw[4];
	} u0;
} __packed;

/* Ethernet Tx Completion Descriptor */
struct oce_nic_tx_cqe {
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			/* dw 0 */
			uint32_t status:4;
			uint32_t rsvd0:8;
			uint32_t port:2;
			uint32_t ct:2;
			uint32_t wqe_index:16;
			/* dw 1 */
			uint32_t rsvd1:5;
			uint32_t cast_enc:2;
			uint32_t lso:1;
			uint32_t nwh_bytes:8;
			uint32_t user_bytes:16;
			/* dw 2 */
			uint32_t rsvd2;
			/* dw 3 */
			uint32_t valid:1;
			uint32_t rsvd3:4;
			uint32_t wq_id:11;
			uint32_t num_pkts:16;
#else
			/* dw 0 */
			uint32_t wqe_index:16;
			uint32_t ct:2;
			uint32_t port:2;
			uint32_t rsvd0:8;
			uint32_t status:4;
			/* dw 1 */
			uint32_t user_bytes:16;
			uint32_t nwh_bytes:8;
			uint32_t lso:1;
			uint32_t cast_enc:2;
			uint32_t rsvd1:5;
			/* dw 2 */
			uint32_t rsvd2;
			/* dw 3 */
			uint32_t num_pkts:16;
			uint32_t wq_id:11;
			uint32_t rsvd3:4;
			uint32_t valid:1;
#endif
		} s;
		uint32_t dw[4];
	} u0;
} __packed;
#define	WQ_CQE_VALID(_cqe)		((_cqe)->u0.dw[3])
#define	WQ_CQE_INVALIDATE(_cqe)		((_cqe)->u0.dw[3] = 0)

/* Receive Queue Entry (RQE) */
struct oce_nic_rqe {
	union {
		struct {
			uint32_t frag_pa_hi;
			uint32_t frag_pa_lo;
		} s;
		uint32_t dw[2];
	} u0;
} __packed;

/* NIC Receive CQE */
struct oce_nic_rx_cqe {
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			/* dw 0 */
			uint32_t ip_options:1;
			uint32_t port:1;
			uint32_t pkt_size:14;
			uint32_t vlan_tag:16;
			/* dw 1 */
			uint32_t num_fragments:3;
			uint32_t switched:1;
			uint32_t ct:2;
			uint32_t frag_index:10;
			uint32_t rsvd0:1;
			uint32_t vlan_tag_present:1;
			uint32_t mac_dst:6;
			uint32_t ip_ver:1;
			uint32_t l4_cksum_pass:1;
			uint32_t ip_cksum_pass:1;
			uint32_t udpframe:1;
			uint32_t tcpframe:1;
			uint32_t ipframe:1;
			uint32_t rss_hp:1;
			uint32_t error:1;
			/* dw 2 */
			uint32_t valid:1;
			uint32_t hds_type:2;
			uint32_t lro_pkt:1;
			uint32_t rsvd4:1;
			uint32_t hds_hdr_size:12;
			uint32_t hds_hdr_frag_index:10;
			uint32_t rss_bank:1;
			uint32_t qnq:1;
			uint32_t pkt_type:2;
			uint32_t rss_flush:1;
			/* dw 3 */
			uint32_t rss_hash_value;
#else
			/* dw 0 */
			uint32_t vlan_tag:16;
			uint32_t pkt_size:14;
			uint32_t port:1;
			uint32_t ip_options:1;
			/* dw 1 */
			uint32_t error:1;
			uint32_t rss_hp:1;
			uint32_t ipframe:1;
			uint32_t tcpframe:1;
			uint32_t udpframe:1;
			uint32_t ip_cksum_pass:1;
			uint32_t l4_cksum_pass:1;
			uint32_t ip_ver:1;
			uint32_t mac_dst:6;
			uint32_t vlan_tag_present:1;
			uint32_t rsvd0:1;
			uint32_t frag_index:10;
			uint32_t ct:2;
			uint32_t switched:1;
			uint32_t num_fragments:3;
			/* dw 2 */
			uint32_t rss_flush:1;
			uint32_t pkt_type:2;
			uint32_t qnq:1;
			uint32_t rss_bank:1;
			uint32_t hds_hdr_frag_index:10;
			uint32_t hds_hdr_size:12;
			uint32_t rsvd4:1;
			uint32_t lro_pkt:1;
			uint32_t hds_type:2;
			uint32_t valid:1;
			/* dw 3 */
			uint32_t rss_hash_value;
#endif
		} s;
		uint32_t dw[4];
	} u0;
} __packed;

/* NIC Receive CQE_v1 */
struct oce_nic_rx_cqe_v1 {
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			/* dw 0 */
			uint32_t ip_options:1;
			uint32_t vlan_tag_present:1;
			uint32_t pkt_size:14;
			uint32_t vlan_tag:16;
			/* dw 1 */
			uint32_t num_fragments:3;
			uint32_t switched:1;
			uint32_t ct:2;
			uint32_t frag_index:10;
			uint32_t rsvd0:1;
			uint32_t mac_dst:7;
			uint32_t ip_ver:1;
			uint32_t l4_cksum_pass:1;
			uint32_t ip_cksum_pass:1;
			uint32_t udpframe:1;
			uint32_t tcpframe:1;
			uint32_t ipframe:1;
			uint32_t rss_hp:1;
			uint32_t error:1;
			/* dw 2 */
			uint32_t valid:1;
			uint32_t rsvd4:13;
			uint32_t hds_hdr_size:2;
			uint32_t hds_hdr_frag_index:8;
			uint32_t vlantag:1;
			uint32_t port:2;
			uint32_t rss_bank:1;
			uint32_t qnq:1;
			uint32_t pkt_type:2;
			uint32_t rss_flush:1;
			/* dw 3 */
			uint32_t rss_hash_value;
	#else
			/* dw 0 */
			uint32_t vlan_tag:16;
			uint32_t pkt_size:14;
			uint32_t vlan_tag_present:1;
			uint32_t ip_options:1;
			/* dw 1 */
			uint32_t error:1;
			uint32_t rss_hp:1;
			uint32_t ipframe:1;
			uint32_t tcpframe:1;
			uint32_t udpframe:1;
			uint32_t ip_cksum_pass:1;
			uint32_t l4_cksum_pass:1;
			uint32_t ip_ver:1;
			uint32_t mac_dst:7;
			uint32_t rsvd0:1;
			uint32_t frag_index:10;
			uint32_t ct:2;
			uint32_t switched:1;
			uint32_t num_fragments:3;
			/* dw 2 */
			uint32_t rss_flush:1;
			uint32_t pkt_type:2;
			uint32_t qnq:1;
			uint32_t rss_bank:1;
			uint32_t port:2;
			uint32_t vlantag:1;
			uint32_t hds_hdr_frag_index:8;
			uint32_t hds_hdr_size:2;
			uint32_t rsvd4:13;
			uint32_t valid:1;
			/* dw 3 */
			uint32_t rss_hash_value;
#endif
		} s;
		uint32_t dw[4];
	} u0;
} __packed;

#define	RQ_CQE_VALID(_cqe)		((_cqe)->u0.dw[2])
#define	RQ_CQE_INVALIDATE(_cqe)		((_cqe)->u0.dw[2] = 0)

struct mbx_config_nic_promiscuous {
	struct mbx_hdr hdr;
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			uint16_t rsvd0;
			uint8_t port1_promisc;
			uint8_t port0_promisc;
#else
			uint8_t port0_promisc;
			uint8_t port1_promisc;
			uint16_t rsvd0;
#endif
		} req;

		struct {
			uint32_t rsvd0;
		} rsp;
	} params;
} __packed;

union oce_wq_ctx {
		uint32_t dw[17];
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			/* dw4 */
			uint32_t dw4rsvd2:8;
			uint32_t nic_wq_type:8;
			uint32_t dw4rsvd1:8;
			uint32_t num_pages:8;
			/* dw5 */
			uint32_t dw5rsvd2:12;
			uint32_t wq_size:4;
			uint32_t dw5rsvd1:16;
			/* dw6 */
			uint32_t valid:1;
			uint32_t dw6rsvd1:31;
			/* dw7 */
			uint32_t dw7rsvd1:16;
			uint32_t cq_id:16;
#else
			/* dw4 */
			uint32_t num_pages:8;
#if 0
			uint32_t dw4rsvd1:8;
#else
/* PSP: this workaround is not documented: fill 0x01 for ulp_mask */
			uint32_t ulp_mask:8;
#endif
			uint32_t nic_wq_type:8;
			uint32_t dw4rsvd2:8;
			/* dw5 */
			uint32_t dw5rsvd1:16;
			uint32_t wq_size:4;
			uint32_t dw5rsvd2:12;
			/* dw6 */
			uint32_t dw6rsvd1:31;
			uint32_t valid:1;
			/* dw7 */
			uint32_t cq_id:16;
			uint32_t dw7rsvd1:16;
#endif
			/* dw8 - dw20 */
			uint32_t dw8_20rsvd1[13];
		} v0;
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			/* dw4 */
			uint32_t dw4rsvd2:8;
			uint32_t nic_wq_type:8;
			uint32_t dw4rsvd1:8;
			uint32_t num_pages:8;
			/* dw5 */
			uint32_t dw5rsvd2:12;
			uint32_t wq_size:4;
			uint32_t iface_id:16;
			/* dw6 */
			uint32_t valid:1;
			uint32_t dw6rsvd1:31;
			/* dw7 */
			uint32_t dw7rsvd1:16;
			uint32_t cq_id:16;
#else
			/* dw4 */
			uint32_t num_pages:8;
			uint32_t dw4rsvd1:8;
			uint32_t nic_wq_type:8;
			uint32_t dw4rsvd2:8;
			/* dw5 */
			uint32_t iface_id:16;
			uint32_t wq_size:4;
			uint32_t dw5rsvd2:12;
			/* dw6 */
			uint32_t dw6rsvd1:31;
			uint32_t valid:1;
			/* dw7 */
			uint32_t cq_id:16;
			uint32_t dw7rsvd1:16;
#endif
			/* dw8 - dw20 */
			uint32_t dw8_20rsvd1[13];
		} v1;
} __packed;

/**
 * @brief [07] NIC_CREATE_WQ
 * @note
 * Lancer requires an InterfaceID to be specified with every WQ. This
 * is the basis for NIC IOV where the Interface maps to a vPort and maps
 * to both Tx and Rx sides.
 */
#define OCE_WQ_TYPE_FORWARDING	0x1	/* wq forwards pkts to TOE */
#define OCE_WQ_TYPE_STANDARD	0x2	/* wq sends network pkts */
struct mbx_create_nic_wq {
	struct mbx_hdr hdr;
	union {
		struct {
			uint8_t num_pages;
			uint8_t ulp_num;
			uint16_t nic_wq_type;
			uint16_t if_id;
			uint8_t wq_size;
			uint8_t rsvd1;
			uint32_t rsvd2;
			uint16_t cq_id;
			uint16_t rsvd3;
			uint32_t rsvd4[13];
			struct oce_pa pages[8];
		} req;

		struct {
			uint16_t wq_id;
			uint16_t rid;
			uint32_t db_offset;
			uint8_t tc_id;
			uint8_t rsvd0[3];
		} rsp;
	} params;
} __packed;

/* [09] NIC_DELETE_WQ */
struct mbx_delete_nic_wq {
	/* dw0 - dw3 */
	struct mbx_hdr hdr;
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			/* dw4 */
			uint16_t rsvd0;
			uint16_t wq_id;
#else
			/* dw4 */
			uint16_t wq_id;
			uint16_t rsvd0;
#endif
		} req;
		struct {
			uint32_t rsvd0;
		} rsp;
	} params;
} __packed;

struct mbx_create_nic_rq {
	struct mbx_hdr hdr;
	union {
		struct {
			uint16_t cq_id;
			uint8_t frag_size;
			uint8_t num_pages;
			struct oce_pa pages[2];
			uint32_t if_id;
			uint16_t max_frame_size;
			uint16_t page_size;
			uint32_t is_rss_queue;
		} req;

		struct {
			uint16_t rq_id;
			uint8_t rss_cpuid;
			uint8_t rsvd0;
		} rsp;
	} params;
} __packed;

/* [10] NIC_DELETE_RQ */
struct mbx_delete_nic_rq {
	/* dw0 - dw3 */
	struct mbx_hdr hdr;
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			/* dw4 */
			uint16_t bypass_flush;
			uint16_t rq_id;
#else
			/* dw4 */
			uint16_t rq_id;
			uint16_t bypass_flush;
#endif
		} req;

		struct {
			/* dw4 */
			uint32_t rsvd0;
		} rsp;
	} params;
} __packed;

struct oce_port_rxf_stats_v0 {
	uint32_t rx_bytes_lsd;			/* dword 0*/
	uint32_t rx_bytes_msd;			/* dword 1*/
	uint32_t rx_total_frames;		/* dword 2*/
	uint32_t rx_unicast_frames;		/* dword 3*/
	uint32_t rx_multicast_frames;		/* dword 4*/
	uint32_t rx_broadcast_frames;		/* dword 5*/
	uint32_t rx_crc_errors;			/* dword 6*/
	uint32_t rx_alignment_symbol_errors;	/* dword 7*/
	uint32_t rx_pause_frames;		/* dword 8*/
	uint32_t rx_control_frames;		/* dword 9*/
	uint32_t rx_in_range_errors;		/* dword 10*/
	uint32_t rx_out_range_errors;		/* dword 11*/
	uint32_t rx_frame_too_long;		/* dword 12*/
	uint32_t rx_address_match_errors;	/* dword 13*/
	uint32_t rx_vlan_mismatch;		/* dword 14*/
	uint32_t rx_dropped_too_small;		/* dword 15*/
	uint32_t rx_dropped_too_short;		/* dword 16*/
	uint32_t rx_dropped_header_too_small;	/* dword 17*/
	uint32_t rx_dropped_tcp_length;		/* dword 18*/
	uint32_t rx_dropped_runt;		/* dword 19*/
	uint32_t rx_64_byte_packets;		/* dword 20*/
	uint32_t rx_65_127_byte_packets;	/* dword 21*/
	uint32_t rx_128_256_byte_packets;	/* dword 22*/
	uint32_t rx_256_511_byte_packets;	/* dword 23*/
	uint32_t rx_512_1023_byte_packets;	/* dword 24*/
	uint32_t rx_1024_1518_byte_packets;	/* dword 25*/
	uint32_t rx_1519_2047_byte_packets;	/* dword 26*/
	uint32_t rx_2048_4095_byte_packets;	/* dword 27*/
	uint32_t rx_4096_8191_byte_packets;	/* dword 28*/
	uint32_t rx_8192_9216_byte_packets;	/* dword 29*/
	uint32_t rx_ip_checksum_errs;		/* dword 30*/
	uint32_t rx_tcp_checksum_errs;		/* dword 31*/
	uint32_t rx_udp_checksum_errs;		/* dword 32*/
	uint32_t rx_non_rss_packets;		/* dword 33*/
	uint32_t rx_ipv4_packets;		/* dword 34*/
	uint32_t rx_ipv6_packets;		/* dword 35*/
	uint32_t rx_ipv4_bytes_lsd;		/* dword 36*/
	uint32_t rx_ipv4_bytes_msd;		/* dword 37*/
	uint32_t rx_ipv6_bytes_lsd;		/* dword 38*/
	uint32_t rx_ipv6_bytes_msd;		/* dword 39*/
	uint32_t rx_chute1_packets;		/* dword 40*/
	uint32_t rx_chute2_packets;		/* dword 41*/
	uint32_t rx_chute3_packets;		/* dword 42*/
	uint32_t rx_management_packets;		/* dword 43*/
	uint32_t rx_switched_unicast_packets;	/* dword 44*/
	uint32_t rx_switched_multicast_packets;	/* dword 45*/
	uint32_t rx_switched_broadcast_packets;	/* dword 46*/
	uint32_t tx_bytes_lsd;			/* dword 47*/
	uint32_t tx_bytes_msd;			/* dword 48*/
	uint32_t tx_unicastframes;		/* dword 49*/
	uint32_t tx_multicastframes;		/* dword 50*/
	uint32_t tx_broadcastframes;		/* dword 51*/
	uint32_t tx_pauseframes;		/* dword 52*/
	uint32_t tx_controlframes;		/* dword 53*/
	uint32_t tx_64_byte_packets;		/* dword 54*/
	uint32_t tx_65_127_byte_packets;	/* dword 55*/
	uint32_t tx_128_256_byte_packets;	/* dword 56*/
	uint32_t tx_256_511_byte_packets;	/* dword 57*/
	uint32_t tx_512_1023_byte_packets;	/* dword 58*/
	uint32_t tx_1024_1518_byte_packets;	/* dword 59*/
	uint32_t tx_1519_2047_byte_packets;	/* dword 60*/
	uint32_t tx_2048_4095_byte_packets;	/* dword 61*/
	uint32_t tx_4096_8191_byte_packets;	/* dword 62*/
	uint32_t tx_8192_9216_byte_packets;	/* dword 63*/
	uint32_t rxpp_fifo_overflow_drop;	/* dword 64*/
	uint32_t rx_input_fifo_overflow_drop;	/* dword 65*/
} __packed;

struct oce_rxf_stats_v0 {
	struct oce_port_rxf_stats_v0 port[2];
	uint32_t rx_drops_no_pbuf;		/* dword 132*/
	uint32_t rx_drops_no_txpb;		/* dword 133*/
	uint32_t rx_drops_no_erx_descr;		/* dword 134*/
	uint32_t rx_drops_no_tpre_descr;	/* dword 135*/
	uint32_t management_rx_port_packets;	/* dword 136*/
	uint32_t management_rx_port_bytes;	/* dword 137*/
	uint32_t management_rx_port_pause_frames;/* dword 138*/
	uint32_t management_rx_port_errors;	/* dword 139*/
	uint32_t management_tx_port_packets;	/* dword 140*/
	uint32_t management_tx_port_bytes;	/* dword 141*/
	uint32_t management_tx_port_pause;	/* dword 142*/
	uint32_t management_rx_port_rxfifo_overflow; /* dword 143*/
	uint32_t rx_drops_too_many_frags;	/* dword 144*/
	uint32_t rx_drops_invalid_ring;		/* dword 145*/
	uint32_t forwarded_packets;		/* dword 146*/
	uint32_t rx_drops_mtu;			/* dword 147*/
	uint32_t rsvd0[7];
	uint32_t port0_jabber_events;
	uint32_t port1_jabber_events;
	uint32_t rsvd1[6];
} __packed;

struct oce_port_rxf_stats_v1 {
	uint32_t rsvd0[12];
	uint32_t rx_crc_errors;
	uint32_t rx_alignment_symbol_errors;
	uint32_t rx_pause_frames;
	uint32_t rx_priority_pause_frames;
	uint32_t rx_control_frames;
	uint32_t rx_in_range_errors;
	uint32_t rx_out_range_errors;
	uint32_t rx_frame_too_long;
	uint32_t rx_address_match_errors;
	uint32_t rx_dropped_too_small;
	uint32_t rx_dropped_too_short;
	uint32_t rx_dropped_header_too_small;
	uint32_t rx_dropped_tcp_length;
	uint32_t rx_dropped_runt;
	uint32_t rsvd1[10];
	uint32_t rx_ip_checksum_errs;
	uint32_t rx_tcp_checksum_errs;
	uint32_t rx_udp_checksum_errs;
	uint32_t rsvd2[7];
	uint32_t rx_switched_unicast_packets;
	uint32_t rx_switched_multicast_packets;
	uint32_t rx_switched_broadcast_packets;
	uint32_t rsvd3[3];
	uint32_t tx_pauseframes;
	uint32_t tx_priority_pauseframes;
	uint32_t tx_controlframes;
	uint32_t rsvd4[10];
	uint32_t rxpp_fifo_overflow_drop;
	uint32_t rx_input_fifo_overflow_drop;
	uint32_t pmem_fifo_overflow_drop;
	uint32_t jabber_events;
	uint32_t rsvd5[3];
} __packed;

struct oce_rxf_stats_v1 {
	struct oce_port_rxf_stats_v1 port[4];
	uint32_t rsvd0[2];
	uint32_t rx_drops_no_pbuf;
	uint32_t rx_drops_no_txpb;
	uint32_t rx_drops_no_erx_descr;
	uint32_t rx_drops_no_tpre_descr;
	uint32_t rsvd1[6];
	uint32_t rx_drops_too_many_frags;
	uint32_t rx_drops_invalid_ring;
	uint32_t forwarded_packets;
	uint32_t rx_drops_mtu;
	uint32_t rsvd2[14];
} __packed;

struct oce_erx_stats_v1 {
	uint32_t rx_drops_no_fragments[68];
	uint32_t rsvd[4];
} __packed;


struct oce_erx_stats_v0 {
	uint32_t rx_drops_no_fragments[44];
	uint32_t rsvd[4];
} __packed;

struct oce_pmem_stats {
	uint32_t eth_red_drops;
	uint32_t rsvd[5];
} __packed;

struct oce_hw_stats_v1 {
	struct oce_rxf_stats_v1 rxf;
	uint32_t rsvd0[OCE_TXP_SW_SZ];
	struct oce_erx_stats_v1 erx;
	struct oce_pmem_stats pmem;
	uint32_t rsvd1[18];
} __packed;

struct oce_hw_stats_v0 {
	struct oce_rxf_stats_v0 rxf;
	uint32_t rsvd[48];
	struct oce_erx_stats_v0 erx;
	struct oce_pmem_stats pmem;
} __packed;

struct mbx_get_nic_stats_v0 {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t rsvd0;
		} req;

		union {
			struct oce_hw_stats_v0 stats;
		} rsp;
	} params;
} __packed;

struct mbx_get_nic_stats {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t rsvd0;
		} req;

		struct {
			struct oce_hw_stats_v1 stats;
		} rsp;
	} params;
} __packed;

/* [18(0x12)] NIC_GET_PPORT_STATS */
struct oce_pport_stats {
	uint64_t tx_pkts;
	uint64_t tx_unicast_pkts;
	uint64_t tx_multicast_pkts;
	uint64_t tx_broadcast_pkts;
	uint64_t tx_bytes;
	uint64_t tx_unicast_bytes;
	uint64_t tx_multicast_bytes;
	uint64_t tx_broadcast_bytes;
	uint64_t tx_discards;
	uint64_t tx_errors;
	uint64_t tx_pause_frames;
	uint64_t tx_pause_on_frames;
	uint64_t tx_pause_off_frames;
	uint64_t tx_internal_mac_errors;
	uint64_t tx_control_frames;
	uint64_t tx_pkts_64_bytes;
	uint64_t tx_pkts_65_to_127_bytes;
	uint64_t tx_pkts_128_to_255_bytes;
	uint64_t tx_pkts_256_to_511_bytes;
	uint64_t tx_pkts_512_to_1023_bytes;
	uint64_t tx_pkts_1024_to_1518_bytes;
	uint64_t tx_pkts_1519_to_2047_bytes;
	uint64_t tx_pkts_2048_to_4095_bytes;
	uint64_t tx_pkts_4096_to_8191_bytes;
	uint64_t tx_pkts_8192_to_9216_bytes;
	uint64_t tx_lso_pkts;
	uint64_t rx_pkts;
	uint64_t rx_unicast_pkts;
	uint64_t rx_multicast_pkts;
	uint64_t rx_broadcast_pkts;
	uint64_t rx_bytes;
	uint64_t rx_unicast_bytes;
	uint64_t rx_multicast_bytes;
	uint64_t rx_broadcast_bytes;
	uint32_t rx_unknown_protos;
	uint32_t reserved_word69;
	uint64_t rx_discards;
	uint64_t rx_errors;
	uint64_t rx_crc_errors;
	uint64_t rx_alignment_errors;
	uint64_t rx_symbol_errors;
	uint64_t rx_pause_frames;
	uint64_t rx_pause_on_frames;
	uint64_t rx_pause_off_frames;
	uint64_t rx_frames_too_long;
	uint64_t rx_internal_mac_errors;
	uint32_t rx_undersize_pkts;
	uint32_t rx_oversize_pkts;
	uint32_t rx_fragment_pkts;
	uint32_t rx_jabbers;
	uint64_t rx_control_frames;
	uint64_t rx_control_frames_unknown_opcode;
	uint32_t rx_in_range_errors;
	uint32_t rx_out_of_range_errors;
	uint32_t rx_address_match_errors;
	uint32_t rx_vlan_mismatch_errors;
	uint32_t rx_dropped_too_small;
	uint32_t rx_dropped_too_short;
	uint32_t rx_dropped_header_too_small;
	uint32_t rx_dropped_invalid_tcp_length;
	uint32_t rx_dropped_runt;
	uint32_t rx_ip_checksum_errors;
	uint32_t rx_tcp_checksum_errors;
	uint32_t rx_udp_checksum_errors;
	uint32_t rx_non_rss_pkts;
	uint64_t reserved_word111;
	uint64_t rx_ipv4_pkts;
	uint64_t rx_ipv6_pkts;
	uint64_t rx_ipv4_bytes;
	uint64_t rx_ipv6_bytes;
	uint64_t rx_nic_pkts;
	uint64_t rx_tcp_pkts;
	uint64_t rx_iscsi_pkts;
	uint64_t rx_management_pkts;
	uint64_t rx_switched_unicast_pkts;
	uint64_t rx_switched_multicast_pkts;
	uint64_t rx_switched_broadcast_pkts;
	uint64_t num_forwards;
	uint32_t rx_fifo_overflow;
	uint32_t rx_input_fifo_overflow;
	uint64_t rx_drops_too_many_frags;
	uint32_t rx_drops_invalid_queue;
	uint32_t reserved_word141;
	uint64_t rx_drops_mtu;
	uint64_t rx_pkts_64_bytes;
	uint64_t rx_pkts_65_to_127_bytes;
	uint64_t rx_pkts_128_to_255_bytes;
	uint64_t rx_pkts_256_to_511_bytes;
	uint64_t rx_pkts_512_to_1023_bytes;
	uint64_t rx_pkts_1024_to_1518_bytes;
	uint64_t rx_pkts_1519_to_2047_bytes;
	uint64_t rx_pkts_2048_to_4095_bytes;
	uint64_t rx_pkts_4096_to_8191_bytes;
	uint64_t rx_pkts_8192_to_9216_bytes;
} __packed;

struct mbx_get_pport_stats {
	/* dw0 - dw3 */
	struct mbx_hdr hdr;
	union {
		struct {
			/* dw4 */
#if _BYTE_ORDER == BIG_ENDIAN
			uint32_t reset_stats:8;
			uint32_t rsvd0:8;
			uint32_t port_number:16;
#else
			uint32_t port_number:16;
			uint32_t rsvd0:8;
			uint32_t reset_stats:8;
#endif
		} req;

		union {
			struct oce_pport_stats pps;
			uint32_t pport_stats[164 - 4 + 1];
		} rsp;
	} params;
} __packed;

/* [19(0x13)] NIC_GET_VPORT_STATS */
struct oce_vport_stats {
	uint64_t tx_pkts;
	uint64_t tx_unicast_pkts;
	uint64_t tx_multicast_pkts;
	uint64_t tx_broadcast_pkts;
	uint64_t tx_bytes;
	uint64_t tx_unicast_bytes;
	uint64_t tx_multicast_bytes;
	uint64_t tx_broadcast_bytes;
	uint64_t tx_discards;
	uint64_t tx_errors;
	uint64_t tx_pkts_64_bytes;
	uint64_t tx_pkts_65_to_127_bytes;
	uint64_t tx_pkts_128_to_255_bytes;
	uint64_t tx_pkts_256_to_511_bytes;
	uint64_t tx_pkts_512_to_1023_bytes;
	uint64_t tx_pkts_1024_to_1518_bytes;
	uint64_t tx_pkts_1519_to_9699_bytes;
	uint64_t tx_pkts_over_9699_bytes;
	uint64_t rx_pkts;
	uint64_t rx_unicast_pkts;
	uint64_t rx_multicast_pkts;
	uint64_t rx_broadcast_pkts;
	uint64_t rx_bytes;
	uint64_t rx_unicast_bytes;
	uint64_t rx_multicast_bytes;
	uint64_t rx_broadcast_bytes;
	uint64_t rx_discards;
	uint64_t rx_errors;
	uint64_t rx_pkts_64_bytes;
	uint64_t rx_pkts_65_to_127_bytes;
	uint64_t rx_pkts_128_to_255_bytes;
	uint64_t rx_pkts_256_to_511_bytes;
	uint64_t rx_pkts_512_to_1023_bytes;
	uint64_t rx_pkts_1024_to_1518_bytes;
	uint64_t rx_pkts_1519_to_9699_bytes;
	uint64_t rx_pkts_gt_9699_bytes;
} __packed;
struct mbx_get_vport_stats {
	/* dw0 - dw3 */
	struct mbx_hdr hdr;
	union {
		struct {
			/* dw4 */
#if _BYTE_ORDER == BIG_ENDIAN
			uint32_t reset_stats:8;
			uint32_t rsvd0:8;
			uint32_t vport_number:16;
#else
			uint32_t vport_number:16;
			uint32_t rsvd0:8;
			uint32_t reset_stats:8;
#endif
		} req;

		union {
			struct oce_vport_stats vps;
			uint32_t vport_stats[75 - 4 + 1];
		} rsp;
	} params;
} __packed;

/* Hash option flags for RSS enable */
enum RSS_ENABLE_FLAGS {
	RSS_ENABLE_NONE 	= 0x0,	/* (No RSS) */
	RSS_ENABLE_IPV4 	= 0x1,	/* (IPV4 HASH enabled ) */
	RSS_ENABLE_TCP_IPV4 	= 0x2,	/* (TCP IPV4 Hash enabled) */
	RSS_ENABLE_IPV6 	= 0x4,	/* (IPV6 HASH enabled) */
	RSS_ENABLE_TCP_IPV6 	= 0x8	/* (TCP IPV6 HASH */
};

/* [01] NIC_CONFIG_RSS */
#define OCE_HASH_TBL_SZ		10
#define OCE_CPU_TBL_SZ		128
#define OCE_FLUSH		1	/* RSS flush completion per CQ port */
struct mbx_config_nic_rss {
	struct mbx_hdr hdr;
	union {
		struct {
#if _BYTE_ORDER == BIG_ENDIAN
			uint32_t if_id;
			uint16_t cpu_tbl_sz_log2;
			uint16_t enable_rss;
			uint32_t hash[OCE_HASH_TBL_SZ];
			uint8_t cputable[OCE_CPU_TBL_SZ];
			uint8_t rsvd[3];
			uint8_t flush;
#else
			uint32_t if_id;
			uint16_t enable_rss;
			uint16_t cpu_tbl_sz_log2;
			uint32_t hash[OCE_HASH_TBL_SZ];
			uint8_t cputable[OCE_CPU_TBL_SZ];
			uint8_t flush;
			uint8_t rsvd[3];
#endif
		} req;
		struct {
			uint8_t rsvd[3];
			uint8_t rss_bank;
		} rsp;
	} params;
} __packed;
