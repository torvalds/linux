/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2013 Emulex
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

/* $FreeBSD$ */

#include <sys/types.h>

#undef _BIG_ENDIAN /* TODO */
#pragma pack(1)

#define	OC_CNA_GEN2			0x2
#define	OC_CNA_GEN3			0x3
#define	DEVID_TIGERSHARK		0x700
#define	DEVID_TOMCAT			0x710

/* PCI CSR offsets */
#define	PCICFG_F1_CSR			0x0	/* F1 for NIC */
#define	PCICFG_SEMAPHORE		0xbc
#define	PCICFG_SOFT_RESET		0x5c
#define	PCICFG_UE_STATUS_HI_MASK	0xac
#define	PCICFG_UE_STATUS_LO_MASK	0xa8
#define	PCICFG_ONLINE0			0xb0
#define	PCICFG_ONLINE1			0xb4
#define	INTR_EN				0x20000000
#define	IMAGE_TRANSFER_SIZE		(32 * 1024)	/* 32K at a time */


/********* UE Status and Mask Registers ***/
#define PCICFG_UE_STATUS_LOW                    0xA0
#define PCICFG_UE_STATUS_HIGH                   0xA4
#define PCICFG_UE_STATUS_LOW_MASK               0xA8

/* Lancer SLIPORT registers */
#define SLIPORT_STATUS_OFFSET           0x404
#define SLIPORT_CONTROL_OFFSET          0x408
#define SLIPORT_ERROR1_OFFSET           0x40C
#define SLIPORT_ERROR2_OFFSET           0x410
#define PHYSDEV_CONTROL_OFFSET          0x414

#define SLIPORT_STATUS_ERR_MASK         0x80000000
#define SLIPORT_STATUS_DIP_MASK         0x02000000
#define SLIPORT_STATUS_RN_MASK          0x01000000
#define SLIPORT_STATUS_RDY_MASK         0x00800000
#define SLI_PORT_CONTROL_IP_MASK        0x08000000
#define PHYSDEV_CONTROL_FW_RESET_MASK   0x00000002
#define PHYSDEV_CONTROL_DD_MASK         0x00000004
#define PHYSDEV_CONTROL_INP_MASK        0x40000000

#define SLIPORT_ERROR_NO_RESOURCE1      0x2
#define SLIPORT_ERROR_NO_RESOURCE2      0x9
/* CSR register offsets */
#define	MPU_EP_CONTROL			0
#define	MPU_EP_SEMAPHORE_BE3		0xac
#define	MPU_EP_SEMAPHORE_XE201		0x400
#define	MPU_EP_SEMAPHORE_SH		0x94
#define	PCICFG_INTR_CTRL		0xfc
#define	HOSTINTR_MASK			(1 << 29)
#define	HOSTINTR_PFUNC_SHIFT		26
#define	HOSTINTR_PFUNC_MASK		7

/* POST status reg struct */
#define	POST_STAGE_POWER_ON_RESET	0x00
#define	POST_STAGE_AWAITING_HOST_RDY	0x01
#define	POST_STAGE_HOST_RDY		0x02
#define	POST_STAGE_CHIP_RESET		0x03
#define	POST_STAGE_ARMFW_READY		0xc000
#define	POST_STAGE_ARMFW_UE		0xf000

/* DOORBELL registers */
#define	PD_RXULP_DB			0x0100
#define	PD_TXULP_DB			0x0060
#define	DB_RQ_ID_MASK			0x3FF

#define	PD_CQ_DB			0x0120
#define	PD_EQ_DB			PD_CQ_DB
#define	PD_MPU_MBOX_DB			0x0160
#define	PD_MQ_DB			0x0140

#define DB_OFFSET			0xc0
#define DB_LRO_RQ_ID_MASK		0x7FF

/* EQE completion types */
#define	EQ_MINOR_CODE_COMPLETION 	0x00
#define	EQ_MINOR_CODE_OTHER		0x01
#define	EQ_MAJOR_CODE_COMPLETION 	0x00

/* Link Status field values */
#define	PHY_LINK_FAULT_NONE		0x0
#define	PHY_LINK_FAULT_LOCAL		0x01
#define	PHY_LINK_FAULT_REMOTE		0x02

#define	PHY_LINK_SPEED_ZERO		0x0	/* No link */
#define	PHY_LINK_SPEED_10MBPS		0x1	/* (10 Mbps) */
#define	PHY_LINK_SPEED_100MBPS		0x2	/* (100 Mbps) */
#define	PHY_LINK_SPEED_1GBPS		0x3	/* (1 Gbps) */
#define	PHY_LINK_SPEED_10GBPS		0x4	/* (10 Gbps) */

#define	PHY_LINK_DUPLEX_NONE		0x0
#define	PHY_LINK_DUPLEX_HALF		0x1
#define	PHY_LINK_DUPLEX_FULL		0x2

#define	NTWK_PORT_A			0x0	/* (Port A) */
#define	NTWK_PORT_B			0x1	/* (Port B) */

#define	PHY_LINK_SPEED_ZERO			0x0	/* (No link.) */
#define	PHY_LINK_SPEED_10MBPS		0x1	/* (10 Mbps) */
#define	PHY_LINK_SPEED_100MBPS		0x2	/* (100 Mbps) */
#define	PHY_LINK_SPEED_1GBPS		0x3	/* (1 Gbps) */
#define	PHY_LINK_SPEED_10GBPS		0x4	/* (10 Gbps) */

/* Hardware Address types */
#define	MAC_ADDRESS_TYPE_STORAGE	0x0	/* (Storage MAC Address) */
#define	MAC_ADDRESS_TYPE_NETWORK	0x1	/* (Network MAC Address) */
#define	MAC_ADDRESS_TYPE_PD		0x2	/* (Protection Domain MAC Addr) */
#define	MAC_ADDRESS_TYPE_MANAGEMENT	0x3	/* (Management MAC Address) */
#define	MAC_ADDRESS_TYPE_FCOE		0x4	/* (FCoE MAC Address) */

/* CREATE_IFACE capability and cap_en flags */
#define MBX_RX_IFACE_FLAGS_RSS		0x4
#define MBX_RX_IFACE_FLAGS_PROMISCUOUS	0x8
#define MBX_RX_IFACE_FLAGS_BROADCAST	0x10
#define MBX_RX_IFACE_FLAGS_UNTAGGED	0x20
#define MBX_RX_IFACE_FLAGS_VLAN_PROMISCUOUS	0x80
#define MBX_RX_IFACE_FLAGS_VLAN		0x100
#define MBX_RX_IFACE_FLAGS_MCAST_PROMISCUOUS	0x200
#define MBX_RX_IFACE_FLAGS_PASS_L2_ERR	0x400
#define MBX_RX_IFACE_FLAGS_PASS_L3L4_ERR	0x800
#define MBX_RX_IFACE_FLAGS_MULTICAST	0x1000
#define MBX_RX_IFACE_RX_FILTER_IF_MULTICAST_HASH 0x2000
#define MBX_RX_IFACE_FLAGS_HDS		0x4000
#define MBX_RX_IFACE_FLAGS_DIRECTED	0x8000
#define MBX_RX_IFACE_FLAGS_VMQ		0x10000
#define MBX_RX_IFACE_FLAGS_NETQ		0x20000
#define MBX_RX_IFACE_FLAGS_QGROUPS	0x40000
#define MBX_RX_IFACE_FLAGS_LSO		0x80000
#define MBX_RX_IFACE_FLAGS_LRO		0x100000

#define	MQ_RING_CONTEXT_SIZE_16		0x5	/* (16 entries) */
#define	MQ_RING_CONTEXT_SIZE_32		0x6	/* (32 entries) */
#define	MQ_RING_CONTEXT_SIZE_64		0x7	/* (64 entries) */
#define	MQ_RING_CONTEXT_SIZE_128	0x8	/* (128 entries) */

#define	MBX_DB_READY_BIT		0x1
#define	MBX_DB_HI_BIT			0x2
#define	ASYNC_EVENT_CODE_LINK_STATE	0x1
#define	ASYNC_EVENT_LINK_UP		0x1
#define	ASYNC_EVENT_LINK_DOWN		0x0
#define ASYNC_EVENT_GRP5		0x5
#define ASYNC_EVENT_CODE_DEBUG		0x6
#define ASYNC_EVENT_PVID_STATE		0x3
#define ASYNC_EVENT_OS2BMC		0x5
#define ASYNC_EVENT_DEBUG_QNQ		0x1
#define ASYNC_EVENT_CODE_SLIPORT	0x11
#define VLAN_VID_MASK			0x0FFF

/* port link_status */
#define	ASYNC_EVENT_LOGICAL		0x02

/* Logical Link Status */
#define	NTWK_LOGICAL_LINK_DOWN		0
#define	NTWK_LOGICAL_LINK_UP		1

/* Rx filter bits */
#define	NTWK_RX_FILTER_IP_CKSUM 	0x1
#define	NTWK_RX_FILTER_TCP_CKSUM	0x2
#define	NTWK_RX_FILTER_UDP_CKSUM	0x4
#define	NTWK_RX_FILTER_STRIP_CRC	0x8

/* max SGE per mbx */
#define	MAX_MBX_SGE			19

/* Max multicast filter size*/
#define OCE_MAX_MC_FILTER_SIZE		64

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

#define	NIC_WQE_SIZE	16
#define	NIC_UNICAST	0x00
#define	NIC_MULTICAST	0x01
#define	NIC_BROADCAST	0x02

#define	NIC_HDS_NO_SPLIT	0x00
#define	NIC_HDS_SPLIT_L3PL	0x01
#define	NIC_HDS_SPLIT_L4PL	0x02

#define	NIC_WQ_TYPE_FORWARDING		0x01
#define	NIC_WQ_TYPE_STANDARD		0x02
#define	NIC_WQ_TYPE_LOW_LATENCY		0x04

#define OCE_RESET_STATS		1
#define OCE_RETAIN_STATS	0
#define OCE_TXP_SW_SZ		48

typedef union pci_sli_intf_u {
	uint32_t dw0;
	struct {
#ifdef _BIG_ENDIAN
		uint32_t sli_valid:3;
		uint32_t sli_hint2:5;
		uint32_t sli_hint1:8;
		uint32_t sli_if_type:4;
		uint32_t sli_family:4;
		uint32_t sli_rev:4;
		uint32_t rsv0:3;
		uint32_t sli_func_type:1;
#else
		uint32_t sli_func_type:1;
		uint32_t rsv0:3;
		uint32_t sli_rev:4;
		uint32_t sli_family:4;
		uint32_t sli_if_type:4;
		uint32_t sli_hint1:8;
		uint32_t sli_hint2:5;
		uint32_t sli_valid:3;
#endif
	} bits;
} pci_sli_intf_t;



/* physical address structure to be used in MBX */
struct phys_addr {
	/* dw0 */
	uint32_t lo;
	/* dw1 */
	uint32_t hi;
};



typedef union pcicfg_intr_ctl_u {
	uint32_t dw0;
	struct {
#ifdef _BIG_ENDIAN
		uint32_t winselect:2;
		uint32_t hostintr:1;
		uint32_t pfnum:3;
		uint32_t vf_cev_int_line_en:1;
		uint32_t winaddr:23;
		uint32_t membarwinen:1;
#else
		uint32_t membarwinen:1;
		uint32_t winaddr:23;
		uint32_t vf_cev_int_line_en:1;
		uint32_t pfnum:3;
		uint32_t hostintr:1;
		uint32_t winselect:2;
#endif
	} bits;
} pcicfg_intr_ctl_t;




typedef union pcicfg_semaphore_u {
	uint32_t dw0;
	struct {
#ifdef _BIG_ENDIAN
		uint32_t rsvd:31;
		uint32_t lock:1;
#else
		uint32_t lock:1;
		uint32_t rsvd:31;
#endif
	} bits;
} pcicfg_semaphore_t;




typedef union pcicfg_soft_reset_u {
	uint32_t dw0;
	struct {
#ifdef _BIG_ENDIAN
		uint32_t nec_ll_rcvdetect:8;
		uint32_t dbg_all_reqs_62_49:14;
		uint32_t scratchpad0:1;
		uint32_t exception_oe:1;
		uint32_t soft_reset:1;
		uint32_t rsvd0:7;
#else
		uint32_t rsvd0:7;
		uint32_t soft_reset:1;
		uint32_t exception_oe:1;
		uint32_t scratchpad0:1;
		uint32_t dbg_all_reqs_62_49:14;
		uint32_t nec_ll_rcvdetect:8;
#endif
	} bits;
} pcicfg_soft_reset_t;




typedef union pcicfg_online1_u {
	uint32_t dw0;
	struct {
#ifdef _BIG_ENDIAN
		uint32_t host8_online:1;
		uint32_t host7_online:1;
		uint32_t host6_online:1;
		uint32_t host5_online:1;
		uint32_t host4_online:1;
		uint32_t host3_online:1;
		uint32_t host2_online:1;
		uint32_t ipc_online:1;
		uint32_t arm_online:1;
		uint32_t txp_online:1;
		uint32_t xaui_online:1;
		uint32_t rxpp_online:1;
		uint32_t txpb_online:1;
		uint32_t rr_online:1;
		uint32_t pmem_online:1;
		uint32_t pctl1_online:1;
		uint32_t pctl0_online:1;
		uint32_t pcs1online_online:1;
		uint32_t mpu_iram_online:1;
		uint32_t pcs0online_online:1;
		uint32_t mgmt_mac_online:1;
		uint32_t lpcmemhost_online:1;
#else
		uint32_t lpcmemhost_online:1;
		uint32_t mgmt_mac_online:1;
		uint32_t pcs0online_online:1;
		uint32_t mpu_iram_online:1;
		uint32_t pcs1online_online:1;
		uint32_t pctl0_online:1;
		uint32_t pctl1_online:1;
		uint32_t pmem_online:1;
		uint32_t rr_online:1;
		uint32_t txpb_online:1;
		uint32_t rxpp_online:1;
		uint32_t xaui_online:1;
		uint32_t txp_online:1;
		uint32_t arm_online:1;
		uint32_t ipc_online:1;
		uint32_t host2_online:1;
		uint32_t host3_online:1;
		uint32_t host4_online:1;
		uint32_t host5_online:1;
		uint32_t host6_online:1;
		uint32_t host7_online:1;
		uint32_t host8_online:1;
#endif
	} bits;
} pcicfg_online1_t;



typedef union mpu_ep_semaphore_u {
	uint32_t dw0;
	struct {
#ifdef _BIG_ENDIAN
		uint32_t error:1;
		uint32_t backup_fw:1;
		uint32_t iscsi_no_ip:1;
		uint32_t iscsi_ip_conflict:1;
		uint32_t option_rom_installed:1;
		uint32_t iscsi_drv_loaded:1;
		uint32_t rsvd0:10;
		uint32_t stage:16;
#else
		uint32_t stage:16;
		uint32_t rsvd0:10;
		uint32_t iscsi_drv_loaded:1;
		uint32_t option_rom_installed:1;
		uint32_t iscsi_ip_conflict:1;
		uint32_t iscsi_no_ip:1;
		uint32_t backup_fw:1;
		uint32_t error:1;
#endif
	} bits;
} mpu_ep_semaphore_t;




typedef union mpu_ep_control_u {
	uint32_t dw0;
	struct {
#ifdef _BIG_ENDIAN
		uint32_t cpu_reset:1;
		uint32_t rsvd1:15;
		uint32_t ep_ram_init_status:1;
		uint32_t rsvd0:12;
		uint32_t m2_rxpbuf:1;
		uint32_t m1_rxpbuf:1;
		uint32_t m0_rxpbuf:1;
#else
		uint32_t m0_rxpbuf:1;
		uint32_t m1_rxpbuf:1;
		uint32_t m2_rxpbuf:1;
		uint32_t rsvd0:12;
		uint32_t ep_ram_init_status:1;
		uint32_t rsvd1:15;
		uint32_t cpu_reset:1;
#endif
	} bits;
} mpu_ep_control_t;




/* RX doorbell */
typedef union pd_rxulp_db_u {
	uint32_t dw0;
	struct {
#ifdef _BIG_ENDIAN
		uint32_t num_posted:8;
		uint32_t invalidate:1;
		uint32_t rsvd1:13;
		uint32_t qid:10;
#else
		uint32_t qid:10;
		uint32_t rsvd1:13;
		uint32_t invalidate:1;
		uint32_t num_posted:8;
#endif
	} bits;
} pd_rxulp_db_t;


/* TX doorbell */
typedef union pd_txulp_db_u {
	uint32_t dw0;
	struct {
#ifdef _BIG_ENDIAN
		uint32_t rsvd1:2;
		uint32_t num_posted:14;
		uint32_t rsvd0:6;
		uint32_t qid:10;
#else
		uint32_t qid:10;
		uint32_t rsvd0:6;
		uint32_t num_posted:14;
		uint32_t rsvd1:2;
#endif
	} bits;
} pd_txulp_db_t;

/* CQ doorbell */
typedef union cq_db_u {
	uint32_t dw0;
	struct {
#ifdef _BIG_ENDIAN
		uint32_t rsvd1:2;
		uint32_t rearm:1;
		uint32_t num_popped:13;
		uint32_t rsvd0:5;
		uint32_t event:1;
		uint32_t qid:10;
#else
		uint32_t qid:10;
		uint32_t event:1;
		uint32_t rsvd0:5;
		uint32_t num_popped:13;
		uint32_t rearm:1;
		uint32_t rsvd1:2;
#endif
	} bits;
} cq_db_t;

/* EQ doorbell */
typedef union eq_db_u {
	uint32_t dw0;
	struct {
#ifdef _BIG_ENDIAN
		uint32_t rsvd1:2;
		uint32_t rearm:1;
		uint32_t num_popped:13;
		uint32_t rsvd0:5;
		uint32_t event:1;
		uint32_t clrint:1;
		uint32_t qid:9;
#else
		uint32_t qid:9;
		uint32_t clrint:1;
		uint32_t event:1;
		uint32_t rsvd0:5;
		uint32_t num_popped:13;
		uint32_t rearm:1;
		uint32_t rsvd1:2;
#endif
	} bits;
} eq_db_t;

/* bootstrap mbox doorbell */
typedef union pd_mpu_mbox_db_u {
	uint32_t dw0;
	struct {
#ifdef _BIG_ENDIAN
		uint32_t address:30;
		uint32_t hi:1;
		uint32_t ready:1;
#else
		uint32_t ready:1;
		uint32_t hi:1;
		uint32_t address:30;
#endif
	} bits;
} pd_mpu_mbox_db_t;

/* MQ ring doorbell */
typedef union pd_mq_db_u {
	uint32_t dw0;
	struct {
#ifdef _BIG_ENDIAN
		uint32_t rsvd1:2;
		uint32_t num_posted:14;
		uint32_t rsvd0:5;
		uint32_t mq_id:11;
#else
		uint32_t mq_id:11;
		uint32_t rsvd0:5;
		uint32_t num_posted:14;
		uint32_t rsvd1:2;
#endif
	} bits;
} pd_mq_db_t;

/*
 * Event Queue Entry
 */
struct oce_eqe {
	uint32_t evnt;
};

/* MQ scatter gather entry. Array of these make an SGL */
struct oce_mq_sge {
	uint32_t pa_lo;
	uint32_t pa_hi;
	uint32_t length;
};

/*
 * payload can contain an SGL or an embedded array of upto 59 dwords
 */
struct oce_mbx_payload {
	union {
		union {
			struct oce_mq_sge sgl[MAX_MBX_SGE];
			uint32_t embedded[59];
		} u1;
		uint32_t dw[59];
	} u0;
};

/*
 * MQ MBX structure
 */
struct oce_mbx {
	union {
		struct {
#ifdef _BIG_ENDIAN
			uint32_t special:8;
			uint32_t rsvd1:16;
			uint32_t sge_count:5;
			uint32_t rsvd0:2;
			uint32_t embedded:1;
#else
			uint32_t embedded:1;
			uint32_t rsvd0:2;
			uint32_t sge_count:5;
			uint32_t rsvd1:16;
			uint32_t special:8;
#endif
		} s;
		uint32_t dw0;
	} u0;

	uint32_t payload_length;
	uint32_t tag[2];
	uint32_t rsvd2[1];
	struct oce_mbx_payload payload;
};

/* completion queue entry for MQ */
struct oce_mq_cqe {
	union {
		struct {
#ifdef _BIG_ENDIAN
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
};

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
#ifdef _BIG_ENDIAN
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
};

/* OS2BMC async event */
struct oce_async_evt_grp5_os2bmc {
	union {
		struct {
			uint32_t lrn_enable:1;
			uint32_t lrn_disable:1;
			uint32_t mgmt_enable:1;
			uint32_t mgmt_disable:1;
			uint32_t rsvd0:12;
			uint32_t vlan_tag:16;
			uint32_t arp_filter:1;
			uint32_t dhcp_client_filt:1;
			uint32_t dhcp_server_filt:1;
			uint32_t net_bios_filt:1;
			uint32_t rsvd1:3;
			uint32_t bcast_filt:1;
			uint32_t ipv6_nbr_filt:1;
			uint32_t ipv6_ra_filt:1;
			uint32_t ipv6_ras_filt:1;
			uint32_t rsvd2[4];
			uint32_t mcast_filt:1;
			uint32_t rsvd3:16;
			uint32_t evt_tag;
			uint32_t dword3;
		} s;
		uint32_t dword[4];
	} u;
};

/* PVID aync event */
struct oce_async_event_grp5_pvid_state {
	uint8_t enabled;
	uint8_t rsvd0;
	uint16_t tag;
	uint32_t event_tag;
	uint32_t rsvd1;
	uint32_t code;
};

/* async event indicating outer VLAN tag in QnQ */
struct oce_async_event_qnq {
        uint8_t valid;       /* Indicates if outer VLAN is valid */
        uint8_t rsvd0;
        uint16_t vlan_tag;
        uint32_t event_tag;
        uint8_t rsvd1[4];
	uint32_t code;
} ;


typedef union oce_mq_ext_ctx_u {
	uint32_t dw[6];
	struct {
		#ifdef _BIG_ENDIAN
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
	        struct {
	#ifdef _BIG_ENDIAN
                /* dw0 */
                uint32_t cq_id:16;
                uint32_t num_pages:16;
                /* dw1 */
                uint32_t async_evt_bitmap;
                /* dw2 */
                uint32_t dw5rsvd2:12;
                uint32_t ring_size:4;
                uint32_t async_cq_id:16;
                /* dw3 */
                uint32_t valid:1;
                uint32_t dw6rsvd1:31;
                /* dw4 */
		uint32_t dw7rsvd1:31;
                uint32_t async_cq_valid:1;
        #else
                /* dw0 */
                uint32_t num_pages:16;
                uint32_t cq_id:16;
                /* dw1 */
                uint32_t async_evt_bitmap;
                /* dw2 */
                uint32_t async_cq_id:16;
                uint32_t ring_size:4;
                uint32_t dw5rsvd2:12;
                /* dw3 */
                uint32_t dw6rsvd1:31;
                uint32_t valid:1;
                /* dw4 */
                uint32_t async_cq_valid:1;
                uint32_t dw7rsvd1:31;
        #endif
                /* dw5 */
                uint32_t dw8rsvd1;
        } v1;

} oce_mq_ext_ctx_t;


/* MQ mailbox structure */
struct oce_bmbx {
	struct oce_mbx mbx;
	struct oce_mq_cqe cqe;
};

/* ---[ MBXs start here ]---------------------------------------------- */
/* MBXs sub system codes */
enum MBX_SUBSYSTEM_CODES {
	MBX_SUBSYSTEM_RSVD = 0,
	MBX_SUBSYSTEM_COMMON = 1,
	MBX_SUBSYSTEM_COMMON_ISCSI = 2,
	MBX_SUBSYSTEM_NIC = 3,
	MBX_SUBSYSTEM_TOE = 4,
	MBX_SUBSYSTEM_PXE_UNDI = 5,
	MBX_SUBSYSTEM_ISCSI_INI = 6,
	MBX_SUBSYSTEM_ISCSI_TGT = 7,
	MBX_SUBSYSTEM_MILI_PTL = 8,
	MBX_SUBSYSTEM_MILI_TMD = 9,
	MBX_SUBSYSTEM_RDMA = 10,
	MBX_SUBSYSTEM_LOWLEVEL = 11,
	MBX_SUBSYSTEM_LRO = 13,
	IOCBMBX_SUBSYSTEM_DCBX = 15,
	IOCBMBX_SUBSYSTEM_DIAG = 16,
	IOCBMBX_SUBSYSTEM_VENDOR = 17
};

/* common ioctl opcodes */
enum COMMON_SUBSYSTEM_OPCODES {
/* These opcodes are common to both networking and storage PCI functions
 * They are used to reserve resources and configure CNA. These opcodes
 * all use the MBX_SUBSYSTEM_COMMON subsystem code.
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
	OPCODE_COMMON_READ_TRANSRECEIVER_DATA = 73,
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

/* common ioctl header */
#define OCE_MBX_VER_V2	0x0002		/* Version V2 mailbox command */
#define OCE_MBX_VER_V1	0x0001		/* Version V1 mailbox command */
#define OCE_MBX_VER_V0	0x0000		/* Version V0 mailbox command */
struct mbx_hdr {
	union {
		uint32_t dw[4];
		struct {
		#ifdef _BIG_ENDIAN
			/* dw 0 */
			uint32_t domain:8;
			uint32_t port_number:8;
			uint32_t subsystem:8;
			uint32_t opcode:8;
			/* dw 1 */
			uint32_t timeout;
			/* dw 2 */
			uint32_t request_length;
			/* dw 3 */
			uint32_t rsvd0:24;
			uint32_t version:8;
		#else
			/* dw 0 */
			uint32_t opcode:8;
			uint32_t subsystem:8;
			uint32_t port_number:8;
			uint32_t domain:8;
			/* dw 1 */
			uint32_t timeout;
			/* dw 2 */
			uint32_t request_length;
			/* dw 3 */
			uint32_t version:8;
			uint32_t rsvd0:24;
		#endif
		} req;
		struct {
		#ifdef _BIG_ENDIAN
			/* dw 0 */
			uint32_t domain:8;
			uint32_t rsvd0:8;
			uint32_t subsystem:8;
			uint32_t opcode:8;
			/* dw 1 */
			uint32_t rsvd1:16;
			uint32_t additional_status:8;
			uint32_t status:8;
		#else
			/* dw 0 */
			uint32_t opcode:8;
			uint32_t subsystem:8;
			uint32_t rsvd0:8;
			uint32_t domain:8;
			/* dw 1 */
			uint32_t status:8;
			uint32_t additional_status:8;
			uint32_t rsvd1:16;
		#endif
			uint32_t rsp_length;
			uint32_t actual_rsp_length;
		} rsp;
	} u0;
};
#define	OCE_BMBX_RHDR_SZ 20
#define	OCE_MBX_RRHDR_SZ sizeof (struct mbx_hdr)
#define	OCE_MBX_ADDL_STATUS(_MHDR) ((_MHDR)->u0.rsp.additional_status)
#define	OCE_MBX_STATUS(_MHDR) ((_MHDR)->u0.rsp.status)

/* [05] OPCODE_COMMON_QUERY_LINK_CONFIG_V1 */
struct mbx_query_common_link_config {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t rsvd0;
		} req;

		struct {
		#ifdef _BIG_ENDIAN
			uint32_t physical_port_fault:8;
			uint32_t physical_port_speed:8;
			uint32_t link_duplex:8;
			uint32_t pt:2;
			uint32_t port_number:6;

			uint16_t qos_link_speed;
			uint16_t rsvd0;

			uint32_t rsvd1:21;
			uint32_t phys_fcv:1;
			uint32_t phys_rxf:1;
			uint32_t phys_txf:1;
			uint32_t logical_link_status:8;
		#else
			uint32_t port_number:6;
			uint32_t pt:2;
			uint32_t link_duplex:8;
			uint32_t physical_port_speed:8;
			uint32_t physical_port_fault:8;

			uint16_t rsvd0;
			uint16_t qos_link_speed;

			uint32_t logical_link_status:8;
			uint32_t phys_txf:1;
			uint32_t phys_rxf:1;
			uint32_t phys_fcv:1;
			uint32_t rsvd1:21;
		#endif
		} rsp;
	} params;
};

/* [57] OPCODE_COMMON_SET_LINK_SPEED */
struct mbx_set_common_link_speed {
	struct mbx_hdr hdr;
	union {
		struct {
#ifdef _BIG_ENDIAN
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
};

struct mac_address_format {
	uint16_t size_of_struct;
	uint8_t mac_addr[6];
};

/* [01] OPCODE_COMMON_QUERY_IFACE_MAC */
struct mbx_query_common_iface_mac {
	struct mbx_hdr hdr;
	union {
		struct {
#ifdef _BIG_ENDIAN
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
};

/* [02] OPCODE_COMMON_SET_IFACE_MAC */
struct mbx_set_common_iface_mac {
	struct mbx_hdr hdr;
	union {
		struct {
#ifdef _BIG_ENDIAN
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
};

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
};

struct qinq_vlan {
#ifdef _BIG_ENDIAN
	uint16_t inner;
	uint16_t outer;
#else
	uint16_t outer;
	uint16_t inner;
#endif
};

struct normal_vlan {
	uint16_t vtag;
};

struct ntwk_if_vlan_tag {
	union {
		struct normal_vlan normal;
		struct qinq_vlan qinq;
	} u0;
};

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
};

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
};

/* event queue context structure */
struct oce_eq_ctx {
#ifdef _BIG_ENDIAN
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
};

/* [13] OPCODE_COMMON_CREATE_EQ */
struct mbx_create_common_eq {
	struct mbx_hdr hdr;
	union {
		struct {
			struct oce_eq_ctx ctx;
			struct phys_addr pages[8];
		} req;

		struct {
			uint16_t eq_id;
			uint16_t rsvd0;
		} rsp;
	} params;
};

/* [55] OPCODE_COMMON_DESTROY_EQ */
struct mbx_destroy_common_eq {
	struct mbx_hdr hdr;
	union {
		struct {
#ifdef _BIG_ENDIAN
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
};

/* SLI-4 CQ context - use version V0 for B3, version V2 for Lancer */
typedef union oce_cq_ctx_u {
	uint32_t dw[5];
	struct {
	#ifdef _BIG_ENDIAN
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
	#ifdef _BIG_ENDIAN
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
		uint32_t eq_id:16;
		uint32_t dw6rsvd1:15;
		uint32_t armed:1;
		/* dw7 */
		uint32_t cqe_count:16;
		uint32_t dw7rsvd1:16;
	#endif
		/* dw8 */
		uint32_t dw8rsvd1;
	} v2;
} oce_cq_ctx_t;

/* [12] OPCODE_COMMON_CREATE_CQ */
struct mbx_create_common_cq {
	struct mbx_hdr hdr;
	union {
		struct {
			oce_cq_ctx_t cq_ctx;
			struct phys_addr pages[4];
		} req;

		struct {
			uint16_t cq_id;
			uint16_t rsvd0;
		} rsp;
	} params;
};

/* [54] OPCODE_COMMON_DESTROY_CQ */
struct mbx_destroy_common_cq {
	struct mbx_hdr hdr;
	union {
		struct {
#ifdef _BIG_ENDIAN
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
};

typedef union oce_mq_ctx_u {
	uint32_t dw[5];
	struct {
	#ifdef _BIG_ENDIAN
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
} oce_mq_ctx_t;

/**
 * @brief [21] OPCODE_COMMON_CREATE_MQ
 * A MQ must be at least 16 entries deep (corresponding to 1 page) and
 * at most 128 entries deep (corresponding to 8 pages).
 */
struct mbx_create_common_mq {
	struct mbx_hdr hdr;
	union {
		struct {
			oce_mq_ctx_t context;
			struct phys_addr pages[8];
		} req;

		struct {
			uint32_t mq_id:16;
			uint32_t rsvd0:16;
		} rsp;
	} params;
};

struct mbx_create_common_mq_ex {
	struct mbx_hdr hdr;
	union {
		struct {
			oce_mq_ext_ctx_t context;
			struct phys_addr pages[8];
		} req;

		struct {
			uint32_t mq_id:16;
			uint32_t rsvd0:16;
		} rsp;
	} params;
};



/* [53] OPCODE_COMMON_DESTROY_MQ */
struct mbx_destroy_common_mq {
	struct mbx_hdr hdr;
	union {
		struct {
#ifdef _BIG_ENDIAN
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
};

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
};

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
};

/* [36] OPCODE_COMMON_SET_FLOW_CONTROL */
/* [37] OPCODE_COMMON_GET_FLOW_CONTROL */
struct mbx_common_get_set_flow_control {
	struct mbx_hdr hdr;
#ifdef _BIG_ENDIAN
	uint16_t tx_flow_control;
	uint16_t rx_flow_control;
#else
	uint16_t rx_flow_control;
	uint16_t tx_flow_control;
#endif
};

enum e_flash_opcode {
	MGMT_FLASHROM_OPCODE_FLASH = 1,
	MGMT_FLASHROM_OPCODE_SAVE = 2
};

/* [06]	OPCODE_READ_COMMON_FLASHROM */
/* [07]	OPCODE_WRITE_COMMON_FLASHROM */

struct mbx_common_read_write_flashrom {
	struct mbx_hdr hdr;
	uint32_t flash_op_code;
	uint32_t flash_op_type;
	uint32_t data_buffer_size;
	uint32_t data_offset;
	uint8_t  data_buffer[32768];	/* + IMAGE_TRANSFER_SIZE */
	uint8_t  rsvd[4];
};

struct oce_phy_info {
	uint16_t phy_type;
	uint16_t interface_type;
	uint32_t misc_params;
	uint16_t ext_phy_details;
	uint16_t rsvd;
	uint16_t auto_speeds_supported;
	uint16_t fixed_speeds_supported;
	uint32_t future_use[2];
};

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
};

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
};

/**
 * @brief MBX Common Quiery Firmaware Config
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
};

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
 * @brief Function Capabilites
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
#ifdef _BIG_ENDIAN
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
};

typedef struct iface_rx_filter_ctx {
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
} iface_rx_filter_ctx_t;

/* [34] OPCODE_COMMON_SET_IFACE_RX_FILTER */
struct mbx_set_common_iface_rx_filter {
	struct mbx_hdr hdr;
	union {
		iface_rx_filter_ctx_t req;
		iface_rx_filter_ctx_t rsp;
	} params;
};

struct be_set_eqd {
	uint32_t eq_id;
	uint32_t phase;
	uint32_t dm;
};

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
};

/* [32] OPCODE_COMMON_GET_CNTL_ATTRIBUTES */

struct mgmt_hba_attr {
	int8_t   flashrom_ver_str[32];
	int8_t   manufac_name[32];
	uint32_t supp_modes;
	int8_t   seeprom_ver_lo;
	int8_t   seeprom_ver_hi;
	int8_t   rsvd0[2];
	uint32_t ioctl_data_struct_ver;
	uint32_t ep_fw_data_struct_ver;
	uint8_t  ncsi_ver_str[12];
	uint32_t def_ext_to;
	int8_t   cntl_mod_num[32];
	int8_t   cntl_desc[64];
	int8_t   cntl_ser_num[32];
	int8_t   ip_ver_str[32];
	int8_t   fw_ver_str[32];
	int8_t   bios_ver_str[32];
	int8_t   redboot_ver_str[32];
	int8_t   drv_ver_str[32];
	int8_t   fw_on_flash_ver_str[32];
	uint32_t funcs_supp;
	uint16_t max_cdblen;
	uint8_t  asic_rev;
	uint8_t  gen_guid[16];
	uint8_t  hba_port_count;
	uint16_t default_link_down_timeout;
	uint8_t  iscsi_ver_min_max;
	uint8_t  multifunc_dev;
	uint8_t  cache_valid;
	uint8_t  hba_status;
	uint8_t  max_domains_supp;
	uint8_t  phy_port;
	uint32_t fw_post_status;
	uint32_t hba_mtu[8];
	uint8_t  iSCSI_feat;
	uint8_t  asic_gen;
	uint8_t  future_u8[2];
	uint32_t future_u32[3];
};

struct mgmt_cntl_attr {
	struct    mgmt_hba_attr hba_attr;
	uint16_t  pci_vendor_id;
	uint16_t  pci_device_id;
	uint16_t  pci_sub_vendor_id;
	uint16_t  pci_sub_system_id;
	uint8_t   pci_bus_num;
	uint8_t   pci_dev_num;
	uint8_t   pci_func_num;
	uint8_t   interface_type;
	uint64_t  unique_id;
	uint8_t   netfilters;
	uint8_t   rsvd0[3];
	uint32_t  future_u32[4];
};

struct mbx_common_get_cntl_attr {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t rsvd0;
		} req;
		struct {
			struct mgmt_cntl_attr cntl_attr_info;
		} rsp;
	} params;
};

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
};

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
};

/* [8] OPCODE_QUERY_COMMON_MAX_MBX_BUFFER_SIZE */
struct mbx_query_common_max_mbx_buffer_size {
	struct mbx_hdr hdr;
	struct {
		uint32_t max_ioctl_bufsz;
	} rsp;
};

/* [61] OPCODE_COMMON_FUNCTION_RESET */
struct ioctl_common_function_reset {
	struct mbx_hdr hdr;
};

/* [73] OPCODE_COMMON_READ_TRANSRECEIVER_DATA */
struct mbx_read_common_transrecv_data {
	struct mbx_hdr hdr;
	union {
		struct {
			uint32_t    page_num;
			uint32_t    port;
		} req;
		struct {
			uint32_t    page_num;
			uint32_t    port;
			uint32_t    page_data[32];
		} rsp;
	} params;

};

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
};

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
};
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
};

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
};
#define MAX_RESC_DESC				256
#define RESC_DESC_SIZE				88
#define ACTIVE_PROFILE				2
#define NIC_RESC_DESC_TYPE_V0			0x41
#define NIC_RESC_DESC_TYPE_V1			0x51
/* OPCODE_COMMON_GET_FUNCTION_CONFIG */
struct mbx_common_get_func_config {
	struct mbx_hdr hdr;
	union {
		struct {
			uint8_t rsvd;
			uint8_t type;
			uint16_t rsvd1;
		} req;
		struct {
			uint32_t desc_count;
			uint8_t resources[MAX_RESC_DESC * RESC_DESC_SIZE];
		} rsp;
	} params;
};


/* OPCODE_COMMON_GET_PROFILE_CONFIG */

struct mbx_common_get_profile_config {
	struct mbx_hdr hdr;
	union {
		struct {
			uint8_t rsvd;
			uint8_t type;
			uint16_t rsvd1;
		} req;
		struct {
			uint32_t desc_count;
			uint8_t resources[MAX_RESC_DESC * RESC_DESC_SIZE];
		} rsp;
	} params;
};

struct oce_nic_resc_desc {
	uint8_t desc_type;
	uint8_t desc_len;
	uint8_t rsvd1;
	uint8_t flags;
	uint8_t vf_num;
	uint8_t rsvd2;
	uint8_t pf_num;
	uint8_t rsvd3;
	uint16_t unicast_mac_count;
	uint8_t rsvd4[6];
	uint16_t mcc_count;
	uint16_t vlan_count;
	uint16_t mcast_mac_count;
	uint16_t txq_count;
	uint16_t rq_count;
	uint16_t rssq_count;
	uint16_t lro_count;
	uint16_t cq_count;
	uint16_t toe_conn_count;
	uint16_t eq_count;
	uint32_t rsvd5;
	uint32_t cap_flags;
	uint8_t link_param;
	uint8_t rsvd6[3];
	uint32_t bw_min;
	uint32_t bw_max;
	uint8_t acpi_params;
	uint8_t wol_param;
	uint16_t rsvd7;
	uint32_t rsvd8[7];

};


struct flash_file_hdr {
	uint8_t  sign[52];
	uint8_t  ufi_version[4];
	uint32_t file_len;
	uint32_t cksum;
	uint32_t antidote;
	uint32_t num_imgs;
	uint8_t  build[24];
	uint8_t  asic_type_rev;
	uint8_t  rsvd[31];
};

struct image_hdr {
	uint32_t imageid;
	uint32_t imageoffset;
	uint32_t imagelength;
	uint32_t image_checksum;
	uint8_t  image_version[32];
};

struct flash_section_hdr {
	uint32_t format_rev;
	uint32_t cksum;
	uint32_t antidote;
	uint32_t num_images;
	uint8_t  id_string[128];
	uint32_t rsvd[4];
};

struct flash_section_entry {
	uint32_t type;
	uint32_t offset;
	uint32_t pad_size;
	uint32_t image_size;
	uint32_t cksum;
	uint32_t entry_point;
	uint32_t rsvd0;
	uint32_t rsvd1;
	uint8_t  ver_data[32];
};

struct flash_sec_info {
	uint8_t cookie[32];
	struct  flash_section_hdr fsec_hdr;
	struct  flash_section_entry fsec_entry[32];
};


enum LOWLEVEL_SUBSYSTEM_OPCODES {
/* Opcodes used for lowlevel functions common to many subystems.
 * Some of these opcodes are used for diagnostic functions only.
 * These opcodes use the MBX_SUBSYSTEM_LOWLEVEL subsystem code.
 */
	OPCODE_LOWLEVEL_TEST_LOOPBACK = 18,
	OPCODE_LOWLEVEL_SET_LOOPBACK_MODE = 19,
	OPCODE_LOWLEVEL_GET_LOOPBACK_MODE = 20
};

enum LLDP_SUBSYSTEM_OPCODES {
/* Opcodes used for LLDP susbsytem for configuring the LLDP state machines. */
	OPCODE_LLDP_GET_CFG = 1,
	OPCODE_LLDP_SET_CFG = 2,
	OPCODE_LLDP_GET_STATS = 3
};

enum DCBX_SUBSYSTEM_OPCODES {
/* Opcodes used for DCBX. */
	OPCODE_DCBX_GET_CFG = 1,
	OPCODE_DCBX_SET_CFG = 2,
	OPCODE_DCBX_GET_MIB_INFO = 3,
	OPCODE_DCBX_GET_DCBX_MODE = 4,
	OPCODE_DCBX_SET_MODE = 5
};

enum DMTF_SUBSYSTEM_OPCODES {
/* Opcodes used for DCBX subsystem. */
	OPCODE_DMTF_EXEC_CLP_CMD = 1
};

enum DIAG_SUBSYSTEM_OPCODES {
/* Opcodes used for diag functions common to many subsystems. */
	OPCODE_DIAG_RUN_DMA_TEST = 1,
	OPCODE_DIAG_RUN_MDIO_TEST = 2,
	OPCODE_DIAG_RUN_NLB_TEST = 3,
	OPCODE_DIAG_RUN_ARM_TIMER_TEST = 4,
	OPCODE_DIAG_GET_MAC = 5
};

enum VENDOR_SUBSYSTEM_OPCODES {
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

enum NIC_SUBSYSTEM_OPCODES {
/**
 * @brief NIC Subsystem Opcodes (see Network SLI-4 manual >= Rev4, v21-2)
 * These opcodes are used for configuring the Ethernet interfaces.
 * These opcodes all use the MBX_SUBSYSTEM_NIC subsystem code.
 */
	NIC_CONFIG_RSS = 1,
	NIC_CONFIG_ACPI = 2,
	NIC_CONFIG_PROMISCUOUS = 3,
	NIC_GET_STATS = 4,
	NIC_CREATE_WQ = 7,
	NIC_CREATE_RQ = 8,
	NIC_DELETE_WQ = 9,
	NIC_DELETE_RQ = 10,
	NIC_CONFIG_ACPI_WOL_MAGIC = 12,
	NIC_GET_NETWORK_STATS = 13,
	NIC_CREATE_HDS_RQ = 16,
	NIC_DELETE_HDS_RQ = 17,
	NIC_GET_PPORT_STATS = 18,
	NIC_GET_VPORT_STATS = 19,
	NIC_GET_QUEUE_STATS = 20
};

/* Hash option flags for RSS enable */
enum RSS_ENABLE_FLAGS {
	RSS_ENABLE_NONE 	= 0x0,	/* (No RSS) */
	RSS_ENABLE_IPV4 	= 0x1,	/* (IPV4 HASH enabled ) */
	RSS_ENABLE_TCP_IPV4 	= 0x2,	/* (TCP IPV4 Hash enabled) */
	RSS_ENABLE_IPV6 	= 0x4,	/* (IPV6 HASH enabled) */
	RSS_ENABLE_TCP_IPV6 	= 0x8,	/* (TCP IPV6 HASH */
	RSS_ENABLE_UDP_IPV4	= 0x10, /* UDP IPV4 HASH */
	RSS_ENABLE_UDP_IPV6	= 0x20  /* UDP IPV6 HASH */
};
#define RSS_ENABLE (RSS_ENABLE_IPV4 | RSS_ENABLE_TCP_IPV4)
#define RSS_DISABLE RSS_ENABLE_NONE

/* NIC header WQE */
struct oce_nic_hdr_wqe {
	union {
		struct {
#ifdef _BIG_ENDIAN
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
			uint32_t mgmt:1;
			uint32_t lso6:1;
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
			uint32_t lso6:1;
			uint32_t mgmt:1;
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
};

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
};

/* Ethernet Tx Completion Descriptor */
struct oce_nic_tx_cqe {
	union {
		struct {
#ifdef _BIG_ENDIAN
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
};
#define	WQ_CQE_VALID(_cqe)  (_cqe->u0.dw[3])
#define	WQ_CQE_INVALIDATE(_cqe)  (_cqe->u0.dw[3] = 0)

/* Receive Queue Entry (RQE) */
struct oce_nic_rqe {
	union {
		struct {
			uint32_t frag_pa_hi;
			uint32_t frag_pa_lo;
		} s;
		uint32_t dw[2];
	} u0;
};

/* NIC Receive CQE */
struct oce_nic_rx_cqe {
	union {
		struct {
#ifdef _BIG_ENDIAN
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
};
/* NIC Receive CQE_v1 */
struct oce_nic_rx_cqe_v1 {
	union {
		struct {
#ifdef _BIG_ENDIAN
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
			uint32_t hds_hdr_size:
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
};

#define	RQ_CQE_VALID_MASK  0x80
#define	RQ_CQE_VALID(_cqe) (_cqe->u0.dw[2])
#define	RQ_CQE_INVALIDATE(_cqe) (_cqe->u0.dw[2] = 0)

struct mbx_config_nic_promiscuous {
	struct mbx_hdr hdr;
	union {
		struct {
#ifdef _BIG_ENDIAN
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
};

typedef	union oce_wq_ctx_u {
		uint32_t dw[17];
		struct {
#ifdef _BIG_ENDIAN
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
#ifdef _BIG_ENDIAN
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
} oce_wq_ctx_t;

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
			struct phys_addr pages[8];

		} req;

		struct {
			uint16_t wq_id;
			uint16_t rid;
			uint32_t db_offset;
			uint8_t tc_id;
			uint8_t rsvd0[3];
		} rsp;
	} params;
};

/* [09] NIC_DELETE_WQ */
struct mbx_delete_nic_wq {
	/* dw0 - dw3 */
	struct mbx_hdr hdr;
	union {
		struct {
#ifdef _BIG_ENDIAN
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
};



struct mbx_create_nic_rq {
	struct mbx_hdr hdr;
	union {
		struct {
			uint16_t cq_id;
			uint8_t frag_size;
			uint8_t num_pages;
			struct phys_addr pages[2];
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
};



/* [10] NIC_DELETE_RQ */
struct mbx_delete_nic_rq {
	/* dw0 - dw3 */
	struct mbx_hdr hdr;
	union {
		struct {
#ifdef _BIG_ENDIAN
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
};




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
};


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
};

struct oce_port_rxf_stats_v2 {
        uint32_t rsvd0[10];
        uint32_t roce_bytes_received_lsd;
        uint32_t roce_bytes_received_msd;
        uint32_t rsvd1[5];
        uint32_t roce_frames_received;
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
        uint32_t rsvd2[10];
        uint32_t rx_ip_checksum_errs;
        uint32_t rx_tcp_checksum_errs;
        uint32_t rx_udp_checksum_errs;
        uint32_t rsvd3[7];
        uint32_t rx_switched_unicast_packets;
        uint32_t rx_switched_multicast_packets;
        uint32_t rx_switched_broadcast_packets;
        uint32_t rsvd4[3];
        uint32_t tx_pauseframes;
        uint32_t tx_priority_pauseframes;
        uint32_t tx_controlframes;
        uint32_t rsvd5[10];
        uint32_t rxpp_fifo_overflow_drop;
        uint32_t rx_input_fifo_overflow_drop;
        uint32_t pmem_fifo_overflow_drop;
        uint32_t jabber_events;
        uint32_t rsvd6[3];
        uint32_t rx_drops_payload_size;
        uint32_t rx_drops_clipped_header;
        uint32_t rx_drops_crc;
        uint32_t roce_drops_payload_len;
        uint32_t roce_drops_crc;
        uint32_t rsvd7[19];
};


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
};

struct oce_rxf_stats_v2 {
        struct oce_port_rxf_stats_v2 port[4];
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
        uint32_t rsvd2[35];
};

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
};

struct oce_erx_stats_v2 {
        uint32_t rx_drops_no_fragments[136];
        uint32_t rsvd[3];
};

struct oce_erx_stats_v1 {
	uint32_t rx_drops_no_fragments[68];
	uint32_t rsvd[4];
};


struct oce_erx_stats_v0 {
	uint32_t rx_drops_no_fragments[44];
	uint32_t rsvd[4];
};

struct oce_pmem_stats {
	uint32_t eth_red_drops;
	uint32_t rsvd[5];
};

struct oce_hw_stats_v2 {
        struct oce_rxf_stats_v2 rxf;
        uint32_t rsvd0[OCE_TXP_SW_SZ];
        struct oce_erx_stats_v2 erx;
        struct oce_pmem_stats pmem;
        uint32_t rsvd1[18];
};


struct oce_hw_stats_v1 {
	struct oce_rxf_stats_v1 rxf;
	uint32_t rsvd0[OCE_TXP_SW_SZ];
	struct oce_erx_stats_v1 erx;
	struct oce_pmem_stats pmem;
	uint32_t rsvd1[18];
};

struct oce_hw_stats_v0 {
	struct oce_rxf_stats_v0 rxf;
	uint32_t rsvd[48];
	struct oce_erx_stats_v0 erx;
	struct oce_pmem_stats pmem;
};

#define MBX_GET_NIC_STATS(version)				\
	struct mbx_get_nic_stats_v##version { 			\
	struct mbx_hdr hdr; 					\
	union { 						\
		struct { 					\
			uint32_t rsvd0; 			\
		} req; 						\
		union { 					\
			struct oce_hw_stats_v##version stats; 	\
		} rsp; 						\
	} params; 						\
}  

MBX_GET_NIC_STATS(0);
MBX_GET_NIC_STATS(1);
MBX_GET_NIC_STATS(2);

/* [18(0x12)] NIC_GET_PPORT_STATS */
struct pport_stats {
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
};

struct mbx_get_pport_stats {
	/* dw0 - dw3 */
	struct mbx_hdr hdr;
	union {
		struct {
			/* dw4 */
#ifdef _BIG_ENDIAN
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
			struct pport_stats pps;
			uint32_t pport_stats[164 - 4 + 1];
		} rsp;
	} params;
};

/* [19(0x13)] NIC_GET_VPORT_STATS */
struct vport_stats {
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
};
struct mbx_get_vport_stats {
	/* dw0 - dw3 */
	struct mbx_hdr hdr;
	union {
		struct {
			/* dw4 */
#ifdef _BIG_ENDIAN
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
			struct vport_stats vps;
			uint32_t vport_stats[75 - 4 + 1];
		} rsp;
	} params;
};

/**
 * @brief	[20(0x14)] NIC_GET_QUEUE_STATS
 * The significant difference between vPort and Queue statistics is
 * the packet byte counters.
 */
struct queue_stats {
	uint64_t packets;
	uint64_t bytes;
	uint64_t errors;
	uint64_t drops;
	uint64_t buffer_errors;		/* rsvd when tx */
};

#define QUEUE_TYPE_WQ		0
#define QUEUE_TYPE_RQ		1
#define QUEUE_TYPE_HDS_RQ	1	/* same as RQ */

struct mbx_get_queue_stats {
	/* dw0 - dw3 */
	struct mbx_hdr hdr;
	union {
		struct {
			/* dw4 */
#ifdef _BIG_ENDIAN
			uint32_t reset_stats:8;
			uint32_t queue_type:8;
			uint32_t queue_id:16;
#else
			uint32_t queue_id:16;
			uint32_t queue_type:8;
			uint32_t reset_stats:8;
#endif
		} req;

		union {
			struct queue_stats qs;
			uint32_t queue_stats[13 - 4 + 1];
		} rsp;
	} params;
};


/* [01] NIC_CONFIG_RSS */
#define OCE_HASH_TBL_SZ	10
#define OCE_CPU_TBL_SZ	128
#define OCE_FLUSH	1	/* RSS flush completion per CQ port */
struct mbx_config_nic_rss {
	struct mbx_hdr hdr;
	union {
		struct {
#ifdef _BIG_ENDIAN
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
};
	

#pragma pack()


typedef uint32_t oce_stat_t;		/* statistic counter */

enum OCE_RXF_PORT_STATS {
	RXF_RX_BYTES_LSD,
	RXF_RX_BYTES_MSD,
	RXF_RX_TOTAL_FRAMES,
	RXF_RX_UNICAST_FRAMES,
	RXF_RX_MULTICAST_FRAMES,
	RXF_RX_BROADCAST_FRAMES,
	RXF_RX_CRC_ERRORS,
	RXF_RX_ALIGNMENT_SYMBOL_ERRORS,
	RXF_RX_PAUSE_FRAMES,
	RXF_RX_CONTROL_FRAMES,
	RXF_RX_IN_RANGE_ERRORS,
	RXF_RX_OUT_RANGE_ERRORS,
	RXF_RX_FRAME_TOO_LONG,
	RXF_RX_ADDRESS_MATCH_ERRORS,
	RXF_RX_VLAN_MISMATCH,
	RXF_RX_DROPPED_TOO_SMALL,
	RXF_RX_DROPPED_TOO_SHORT,
	RXF_RX_DROPPED_HEADER_TOO_SMALL,
	RXF_RX_DROPPED_TCP_LENGTH,
	RXF_RX_DROPPED_RUNT,
	RXF_RX_64_BYTE_PACKETS,
	RXF_RX_65_127_BYTE_PACKETS,
	RXF_RX_128_256_BYTE_PACKETS,
	RXF_RX_256_511_BYTE_PACKETS,
	RXF_RX_512_1023_BYTE_PACKETS,
	RXF_RX_1024_1518_BYTE_PACKETS,
	RXF_RX_1519_2047_BYTE_PACKETS,
	RXF_RX_2048_4095_BYTE_PACKETS,
	RXF_RX_4096_8191_BYTE_PACKETS,
	RXF_RX_8192_9216_BYTE_PACKETS,
	RXF_RX_IP_CHECKSUM_ERRS,
	RXF_RX_TCP_CHECKSUM_ERRS,
	RXF_RX_UDP_CHECKSUM_ERRS,
	RXF_RX_NON_RSS_PACKETS,
	RXF_RX_IPV4_PACKETS,
	RXF_RX_IPV6_PACKETS,
	RXF_RX_IPV4_BYTES_LSD,
	RXF_RX_IPV4_BYTES_MSD,
	RXF_RX_IPV6_BYTES_LSD,
	RXF_RX_IPV6_BYTES_MSD,
	RXF_RX_CHUTE1_PACKETS,
	RXF_RX_CHUTE2_PACKETS,
	RXF_RX_CHUTE3_PACKETS,
	RXF_RX_MANAGEMENT_PACKETS,
	RXF_RX_SWITCHED_UNICAST_PACKETS,
	RXF_RX_SWITCHED_MULTICAST_PACKETS,
	RXF_RX_SWITCHED_BROADCAST_PACKETS,
	RXF_TX_BYTES_LSD,
	RXF_TX_BYTES_MSD,
	RXF_TX_UNICAST_FRAMES,
	RXF_TX_MULTICAST_FRAMES,
	RXF_TX_BROADCAST_FRAMES,
	RXF_TX_PAUSE_FRAMES,
	RXF_TX_CONTROL_FRAMES,
	RXF_TX_64_BYTE_PACKETS,
	RXF_TX_65_127_BYTE_PACKETS,
	RXF_TX_128_256_BYTE_PACKETS,
	RXF_TX_256_511_BYTE_PACKETS,
	RXF_TX_512_1023_BYTE_PACKETS,
	RXF_TX_1024_1518_BYTE_PACKETS,
	RXF_TX_1519_2047_BYTE_PACKETS,
	RXF_TX_2048_4095_BYTE_PACKETS,
	RXF_TX_4096_8191_BYTE_PACKETS,
	RXF_TX_8192_9216_BYTE_PACKETS,
	RXF_RX_FIFO_OVERFLOW,
	RXF_RX_INPUT_FIFO_OVERFLOW,
	RXF_PORT_STATS_N_WORDS
};

enum OCE_RXF_ADDL_STATS {
	RXF_RX_DROPS_NO_PBUF,
	RXF_RX_DROPS_NO_TXPB,
	RXF_RX_DROPS_NO_ERX_DESCR,
	RXF_RX_DROPS_NO_TPRE_DESCR,
	RXF_MANAGEMENT_RX_PORT_PACKETS,
	RXF_MANAGEMENT_RX_PORT_BYTES,
	RXF_MANAGEMENT_RX_PORT_PAUSE_FRAMES,
	RXF_MANAGEMENT_RX_PORT_ERRORS,
	RXF_MANAGEMENT_TX_PORT_PACKETS,
	RXF_MANAGEMENT_TX_PORT_BYTES,
	RXF_MANAGEMENT_TX_PORT_PAUSE,
	RXF_MANAGEMENT_RX_PORT_RXFIFO_OVERFLOW,
	RXF_RX_DROPS_TOO_MANY_FRAGS,
	RXF_RX_DROPS_INVALID_RING,
	RXF_FORWARDED_PACKETS,
	RXF_RX_DROPS_MTU,
	RXF_ADDL_STATS_N_WORDS
};

enum OCE_TX_CHUTE_PORT_STATS {
	CTPT_XMT_IPV4_PKTS,
	CTPT_XMT_IPV4_LSD,
	CTPT_XMT_IPV4_MSD,
	CTPT_XMT_IPV6_PKTS,
	CTPT_XMT_IPV6_LSD,
	CTPT_XMT_IPV6_MSD,
	CTPT_REXMT_IPV4_PKTs,
	CTPT_REXMT_IPV4_LSD,
	CTPT_REXMT_IPV4_MSD,
	CTPT_REXMT_IPV6_PKTs,
	CTPT_REXMT_IPV6_LSD,
	CTPT_REXMT_IPV6_MSD,
	CTPT_N_WORDS,
};

enum OCE_RX_ERR_STATS {
	RX_DROPS_NO_FRAGMENTS_0,
	RX_DROPS_NO_FRAGMENTS_1,
	RX_DROPS_NO_FRAGMENTS_2,
	RX_DROPS_NO_FRAGMENTS_3,
	RX_DROPS_NO_FRAGMENTS_4,
	RX_DROPS_NO_FRAGMENTS_5,
	RX_DROPS_NO_FRAGMENTS_6,
	RX_DROPS_NO_FRAGMENTS_7,
	RX_DROPS_NO_FRAGMENTS_8,
	RX_DROPS_NO_FRAGMENTS_9,
	RX_DROPS_NO_FRAGMENTS_10,
	RX_DROPS_NO_FRAGMENTS_11,
	RX_DROPS_NO_FRAGMENTS_12,
	RX_DROPS_NO_FRAGMENTS_13,
	RX_DROPS_NO_FRAGMENTS_14,
	RX_DROPS_NO_FRAGMENTS_15,
	RX_DROPS_NO_FRAGMENTS_16,
	RX_DROPS_NO_FRAGMENTS_17,
	RX_DROPS_NO_FRAGMENTS_18,
	RX_DROPS_NO_FRAGMENTS_19,
	RX_DROPS_NO_FRAGMENTS_20,
	RX_DROPS_NO_FRAGMENTS_21,
	RX_DROPS_NO_FRAGMENTS_22,
	RX_DROPS_NO_FRAGMENTS_23,
	RX_DROPS_NO_FRAGMENTS_24,
	RX_DROPS_NO_FRAGMENTS_25,
	RX_DROPS_NO_FRAGMENTS_26,
	RX_DROPS_NO_FRAGMENTS_27,
	RX_DROPS_NO_FRAGMENTS_28,
	RX_DROPS_NO_FRAGMENTS_29,
	RX_DROPS_NO_FRAGMENTS_30,
	RX_DROPS_NO_FRAGMENTS_31,
	RX_DROPS_NO_FRAGMENTS_32,
	RX_DROPS_NO_FRAGMENTS_33,
	RX_DROPS_NO_FRAGMENTS_34,
	RX_DROPS_NO_FRAGMENTS_35,
	RX_DROPS_NO_FRAGMENTS_36,
	RX_DROPS_NO_FRAGMENTS_37,
	RX_DROPS_NO_FRAGMENTS_38,
	RX_DROPS_NO_FRAGMENTS_39,
	RX_DROPS_NO_FRAGMENTS_40,
	RX_DROPS_NO_FRAGMENTS_41,
	RX_DROPS_NO_FRAGMENTS_42,
	RX_DROPS_NO_FRAGMENTS_43,
	RX_DEBUG_WDMA_SENT_HOLD,
	RX_DEBUG_WDMA_PBFREE_SENT_HOLD,
	RX_DEBUG_WDMA_0B_PBFREE_SENT_HOLD,
	RX_DEBUG_PMEM_PBUF_DEALLOC,
	RX_ERRORS_N_WORDS
};

enum OCE_PMEM_ERR_STATS {
	PMEM_ETH_RED_DROPS,
	PMEM_LRO_RED_DROPS,
	PMEM_ULP0_RED_DROPS,
	PMEM_ULP1_RED_DROPS,
	PMEM_GLOBAL_RED_DROPS,
	PMEM_ERRORS_N_WORDS
};

/**
 * @brief Statistics for a given Physical Port
 * These satisfy all the required BE2 statistics and also the
 * following MIB objects:
 * 
 * RFC 2863 - The Interfaces Group MIB
 * RFC 2819 - Remote Network Monitoring Management Information Base (RMON)
 * RFC 3635 - Managed Objects for the Ethernet-like Interface Types
 * RFC 4502 - Remote Network Monitoring Mgmt Information Base Ver-2 (RMON2)
 * 
 */
enum OCE_PPORT_STATS {
	PPORT_TX_PKTS = 0,
	PPORT_TX_UNICAST_PKTS = 2,
	PPORT_TX_MULTICAST_PKTS = 4,
	PPORT_TX_BROADCAST_PKTS = 6,
	PPORT_TX_BYTES = 8,
	PPORT_TX_UNICAST_BYTES = 10,
	PPORT_TX_MULTICAST_BYTES = 12,
	PPORT_TX_BROADCAST_BYTES = 14,
	PPORT_TX_DISCARDS = 16,
	PPORT_TX_ERRORS = 18,
	PPORT_TX_PAUSE_FRAMES = 20,
	PPORT_TX_PAUSE_ON_FRAMES = 22,
	PPORT_TX_PAUSE_OFF_FRAMES = 24,
	PPORT_TX_INTERNAL_MAC_ERRORS = 26,
	PPORT_TX_CONTROL_FRAMES = 28,
	PPORT_TX_PKTS_64_BYTES = 30,
	PPORT_TX_PKTS_65_TO_127_BYTES = 32,
	PPORT_TX_PKTS_128_TO_255_BYTES = 34,
	PPORT_TX_PKTS_256_TO_511_BYTES = 36,
	PPORT_TX_PKTS_512_TO_1023_BYTES = 38,
	PPORT_TX_PKTS_1024_TO_1518_BYTES = 40,
	PPORT_TX_PKTS_1519_TO_2047_BYTES = 42,
	PPORT_TX_PKTS_2048_TO_4095_BYTES = 44,
	PPORT_TX_PKTS_4096_TO_8191_BYTES = 46,
	PPORT_TX_PKTS_8192_TO_9216_BYTES = 48,
	PPORT_TX_LSO_PKTS = 50,
	PPORT_RX_PKTS = 52,
	PPORT_RX_UNICAST_PKTS = 54,
	PPORT_RX_MULTICAST_PKTS = 56,
	PPORT_RX_BROADCAST_PKTS = 58,
	PPORT_RX_BYTES = 60,
	PPORT_RX_UNICAST_BYTES = 62,
	PPORT_RX_MULTICAST_BYTES = 64,
	PPORT_RX_BROADCAST_BYTES = 66,
	PPORT_RX_UNKNOWN_PROTOS = 68,
	PPORT_RESERVED_WORD69 = 69,
	PPORT_RX_DISCARDS = 70,
	PPORT_RX_ERRORS = 72,
	PPORT_RX_CRC_ERRORS = 74,
	PPORT_RX_ALIGNMENT_ERRORS = 76,
	PPORT_RX_SYMBOL_ERRORS = 78,
	PPORT_RX_PAUSE_FRAMES = 80,
	PPORT_RX_PAUSE_ON_FRAMES = 82,
	PPORT_RX_PAUSE_OFF_FRAMES = 84,
	PPORT_RX_FRAMES_TOO_LONG = 86,
	PPORT_RX_INTERNAL_MAC_ERRORS = 88,
	PPORT_RX_UNDERSIZE_PKTS = 90,
	PPORT_RX_OVERSIZE_PKTS = 91,
	PPORT_RX_FRAGMENT_PKTS = 92,
	PPORT_RX_JABBERS = 93,
	PPORT_RX_CONTROL_FRAMES = 94,
	PPORT_RX_CONTROL_FRAMES_UNK_OPCODE = 96,
	PPORT_RX_IN_RANGE_ERRORS = 98,
	PPORT_RX_OUT_OF_RANGE_ERRORS = 99,
	PPORT_RX_ADDRESS_MATCH_ERRORS = 100,
	PPORT_RX_VLAN_MISMATCH_ERRORS = 101,
	PPORT_RX_DROPPED_TOO_SMALL = 102,
	PPORT_RX_DROPPED_TOO_SHORT = 103,
	PPORT_RX_DROPPED_HEADER_TOO_SMALL = 104,
	PPORT_RX_DROPPED_INVALID_TCP_LENGTH = 105,
	PPORT_RX_DROPPED_RUNT = 106,
	PPORT_RX_IP_CHECKSUM_ERRORS = 107,
	PPORT_RX_TCP_CHECKSUM_ERRORS = 108,
	PPORT_RX_UDP_CHECKSUM_ERRORS = 109,
	PPORT_RX_NON_RSS_PKTS = 110,
	PPORT_RESERVED_WORD111 = 111,
	PPORT_RX_IPV4_PKTS = 112,
	PPORT_RX_IPV6_PKTS = 114,
	PPORT_RX_IPV4_BYTES = 116,
	PPORT_RX_IPV6_BYTES = 118,
	PPORT_RX_NIC_PKTS = 120,
	PPORT_RX_TCP_PKTS = 122,
	PPORT_RX_ISCSI_PKTS = 124,
	PPORT_RX_MANAGEMENT_PKTS = 126,
	PPORT_RX_SWITCHED_UNICAST_PKTS = 128,
	PPORT_RX_SWITCHED_MULTICAST_PKTS = 130,
	PPORT_RX_SWITCHED_BROADCAST_PKTS = 132,
	PPORT_NUM_FORWARDS = 134,
	PPORT_RX_FIFO_OVERFLOW = 136,
	PPORT_RX_INPUT_FIFO_OVERFLOW = 137,
	PPORT_RX_DROPS_TOO_MANY_FRAGS = 138,
	PPORT_RX_DROPS_INVALID_QUEUE = 140,
	PPORT_RESERVED_WORD141 = 141,
	PPORT_RX_DROPS_MTU = 142,
	PPORT_RX_PKTS_64_BYTES = 144,
	PPORT_RX_PKTS_65_TO_127_BYTES = 146,
	PPORT_RX_PKTS_128_TO_255_BYTES = 148,
	PPORT_RX_PKTS_256_TO_511_BYTES = 150,
	PPORT_RX_PKTS_512_TO_1023_BYTES = 152,
	PPORT_RX_PKTS_1024_TO_1518_BYTES = 154,
	PPORT_RX_PKTS_1519_TO_2047_BYTES = 156,
	PPORT_RX_PKTS_2048_TO_4095_BYTES = 158,
	PPORT_RX_PKTS_4096_TO_8191_BYTES = 160,
	PPORT_RX_PKTS_8192_TO_9216_BYTES = 162,
	PPORT_N_WORDS = 164
};

/**
 * @brief Statistics for a given Virtual Port (vPort)
 * The following describes the vPort statistics satisfying
 * requirements of Linux/VMWare netdev statistics and
 * Microsoft Windows Statistics along with other Operating Systems.
 */
enum OCE_VPORT_STATS {
	VPORT_TX_PKTS = 0,
	VPORT_TX_UNICAST_PKTS = 2,
	VPORT_TX_MULTICAST_PKTS = 4,
	VPORT_TX_BROADCAST_PKTS = 6,
	VPORT_TX_BYTES = 8,
	VPORT_TX_UNICAST_BYTES = 10,
	VPORT_TX_MULTICAST_BYTES = 12,
	VPORT_TX_BROADCAST_BYTES = 14,
	VPORT_TX_DISCARDS = 16,
	VPORT_TX_ERRORS = 18,
	VPORT_TX_PKTS_64_BYTES = 20,
	VPORT_TX_PKTS_65_TO_127_BYTES = 22,
	VPORT_TX_PKTS_128_TO_255_BYTES = 24,
	VPORT_TX_PKTS_256_TO_511_BYTES = 26,
	VPORT_TX_PKTS_512_TO_1023_BYTEs = 28,
	VPORT_TX_PKTS_1024_TO_1518_BYTEs = 30,
	VPORT_TX_PKTS_1519_TO_9699_BYTEs = 32,
	VPORT_TX_PKTS_OVER_9699_BYTES = 34,
	VPORT_RX_PKTS = 36,
	VPORT_RX_UNICAST_PKTS = 38,
	VPORT_RX_MULTICAST_PKTS = 40,
	VPORT_RX_BROADCAST_PKTS = 42,
	VPORT_RX_BYTES = 44,
	VPORT_RX_UNICAST_BYTES = 46,
	VPORT_RX_MULTICAST_BYTES = 48,
	VPORT_RX_BROADCAST_BYTES = 50,
	VPORT_RX_DISCARDS = 52,
	VPORT_RX_ERRORS = 54,
	VPORT_RX_PKTS_64_BYTES = 56,
	VPORT_RX_PKTS_65_TO_127_BYTES = 58,
	VPORT_RX_PKTS_128_TO_255_BYTES = 60,
	VPORT_RX_PKTS_256_TO_511_BYTES = 62,
	VPORT_RX_PKTS_512_TO_1023_BYTEs = 64,
	VPORT_RX_PKTS_1024_TO_1518_BYTEs = 66,
	VPORT_RX_PKTS_1519_TO_9699_BYTEs = 68,
	VPORT_RX_PKTS_OVER_9699_BYTES = 70,
	VPORT_N_WORDS = 72
};

/**
 * @brief Statistics for a given queue (NIC WQ, RQ, or HDS RQ)
 * This set satisfies requirements of VMQare NetQueue and Microsoft VMQ
 */
enum OCE_QUEUE_TX_STATS {
	QUEUE_TX_PKTS = 0,
	QUEUE_TX_BYTES = 2,
	QUEUE_TX_ERRORS = 4,
	QUEUE_TX_DROPS = 6,
	QUEUE_TX_N_WORDS = 8
};

enum OCE_QUEUE_RX_STATS {
	QUEUE_RX_PKTS = 0,
	QUEUE_RX_BYTES = 2,
	QUEUE_RX_ERRORS = 4,
	QUEUE_RX_DROPS = 6,
	QUEUE_RX_BUFFER_ERRORS = 8,
	QUEUE_RX_N_WORDS = 10
};

/* HW LRO structures */
struct mbx_nic_query_lro_capabilities {
        struct mbx_hdr hdr;
        union {
                struct {
                        uint32_t rsvd[6];
                } req;
                struct {
#ifdef _BIG_ENDIAN
                        uint32_t lro_flags;
                        uint16_t lro_rq_cnt;
                        uint16_t plro_max_offload;
                        uint32_t rsvd[4];
#else
                        uint32_t lro_flags;
                        uint16_t plro_max_offload;
                        uint16_t lro_rq_cnt;
                        uint32_t rsvd[4];
#endif
                } rsp;
        } params;
};

struct mbx_nic_set_iface_lro_config {
        struct mbx_hdr hdr;
        union {
                struct {
#ifdef _BIG_ENDIAN
                        uint32_t lro_flags;
                        uint32_t iface_id;
                        uint32_t max_clsc_byte_cnt;
                        uint32_t max_clsc_seg_cnt;
                        uint32_t max_clsc_usec_delay;
                        uint32_t min_clsc_frame_byte_cnt;
                        uint32_t rsvd[2];
#else
                        uint32_t lro_flags;
                        uint32_t iface_id;
                        uint32_t max_clsc_byte_cnt;
                        uint32_t max_clsc_seg_cnt;
                        uint32_t max_clsc_usec_delay;
                        uint32_t min_clsc_frame_byte_cnt;
                        uint32_t rsvd[2];
#endif
                } req;
                struct {
#ifdef _BIG_ENDIAN
                        uint32_t lro_flags;
                        uint32_t rsvd[7];
#else
                        uint32_t lro_flags;
                        uint32_t rsvd[7];
#endif
                } rsp;
        } params;
};


struct mbx_create_nic_rq_v2 {
        struct mbx_hdr hdr;
        union {
                struct {
#ifdef _BIG_ENDIAN
                        uint8_t  num_pages;
                        uint8_t  frag_size;
                        uint16_t cq_id;

                        uint32_t if_id;

                        uint16_t page_size;
                        uint16_t max_frame_size;

                        uint16_t rsvd;
                        uint16_t pd_id;

                        uint16_t rsvd1;
                        uint16_t rq_flags;

                        uint16_t hds_fixed_offset;
                        uint8_t hds_start;
                        uint8_t hds_frag;

                        uint16_t hds_backfill_size;
                        uint16_t hds_frag_size;

                        uint32_t rbq_id;

                        uint32_t rsvd2[8];

                        struct phys_addr pages[2];
#else
                        uint16_t cq_id;
                        uint8_t  frag_size;
                        uint8_t  num_pages;

                        uint32_t if_id;

                        uint16_t max_frame_size;
                        uint16_t page_size;

                        uint16_t pd_id;
                        uint16_t rsvd;

                        uint16_t rq_flags;
                        uint16_t rsvd1;

                        uint8_t hds_frag;
                        uint8_t hds_start;
                        uint16_t hds_fixed_offset;

                        uint16_t hds_frag_size;
                        uint16_t hds_backfill_size;

                        uint32_t rbq_id;

                        uint32_t rsvd2[8];

                        struct phys_addr pages[2];
#endif
                } req;
                struct {
#ifdef _BIG_ENDIAN
                        uint8_t rsvd0;
                        uint8_t rss_cpuid;
                        uint16_t rq_id;

                        uint8_t db_format;
                        uint8_t db_reg_set;
                        uint16_t rsvd1;

                        uint32_t db_offset;

                        uint32_t rsvd2;

                        uint16_t rsvd3;
                        uint16_t rq_flags;

#else
                        uint16_t rq_id;
                        uint8_t rss_cpuid;
                        uint8_t rsvd0;

                        uint16_t rsvd1;
                        uint8_t db_reg_set;
                        uint8_t db_format;

                        uint32_t db_offset;

                        uint32_t rsvd2;

                        uint16_t rq_flags;
                        uint16_t rsvd3;
#endif
                } rsp;

        } params;
};

struct mbx_delete_nic_rq_v1 {
        struct mbx_hdr hdr;
        union {
                struct {
#ifdef _BIG_ENDIAN
                        uint16_t bypass_flush;
                        uint16_t rq_id;
                        uint16_t rsvd;
                        uint16_t rq_flags;
#else
                        uint16_t rq_id;
                        uint16_t bypass_flush;
                        uint16_t rq_flags;
                        uint16_t rsvd;
#endif
                } req;
                struct {
                        uint32_t rsvd[2];
                } rsp;
        } params;
};

struct nic_hwlro_singleton_cqe {
#ifdef _BIG_ENDIAN
        /* dw 0 */
        uint32_t ip_opt:1;
        uint32_t vtp:1;
        uint32_t pkt_size:14;
        uint32_t vlan_tag:16;

        /* dw 1 */
        uint32_t num_frags:3;
        uint32_t rsvd1:3;
        uint32_t frag_index:10;
        uint32_t rsvd:8;
        uint32_t ipv6_frame:1;
        uint32_t l4_cksum_pass:1;
        uint32_t ip_cksum_pass:1;
        uint32_t udpframe:1;
        uint32_t tcpframe:1;
        uint32_t ipframe:1;
        uint32_t rss_hp:1;
        uint32_t error:1;

        /* dw 2 */
        uint32_t valid:1;
        uint32_t cqe_type:2;
        uint32_t debug:7;
        uint32_t rsvd4:6;
        uint32_t data_offset:8;
        uint32_t rsvd3:3;
        uint32_t rss_bank:1;
        uint32_t qnq:1;
        uint32_t rsvd2:3;
        
	/* dw 3 */
        uint32_t rss_hash_value;
#else
        /* dw 0 */
        uint32_t vlan_tag:16;
        uint32_t pkt_size:14;
        uint32_t vtp:1;
        uint32_t ip_opt:1;

        /* dw 1 */
        uint32_t error:1;
        uint32_t rss_hp:1;
        uint32_t ipframe:1;
        uint32_t tcpframe:1;
        uint32_t udpframe:1;
        uint32_t ip_cksum_pass:1;
        uint32_t l4_cksum_pass:1;
        uint32_t ipv6_frame:1;
        uint32_t rsvd:8;
        uint32_t frag_index:10;
        uint32_t rsvd1:3;
        uint32_t num_frags:3;

        /* dw 2 */
        uint32_t rsvd2:3;
        uint32_t qnq:1;
        uint32_t rss_bank:1;
        uint32_t rsvd3:3;
        uint32_t data_offset:8;
        uint32_t rsvd4:6;
        uint32_t debug:7;
        uint32_t cqe_type:2;
        uint32_t valid:1;
 
       /* dw 3 */
        uint32_t rss_hash_value;
#endif
};

struct nic_hwlro_cqe_part1 {
#ifdef _BIG_ENDIAN
        /* dw 0 */
        uint32_t tcp_timestamp_val;

        /* dw 1 */
        uint32_t tcp_timestamp_ecr;

        /* dw 2 */
        uint32_t valid:1;
        uint32_t cqe_type:2;
        uint32_t rsvd3:7;
        uint32_t rss_policy:4;
        uint32_t rsvd2:2;
        uint32_t data_offset:8;
        uint32_t rsvd1:1;
        uint32_t lro_desc:1;
        uint32_t lro_timer_pop:1;
        uint32_t rss_bank:1;
        uint32_t qnq:1;
        uint32_t rsvd:2;
        uint32_t rss_flush:1;

	/* dw 3 */
        uint32_t rss_hash_value;
#else
        /* dw 0 */
        uint32_t tcp_timestamp_val;

        /* dw 1 */
        uint32_t tcp_timestamp_ecr;

        /* dw 2 */
        uint32_t rss_flush:1;
        uint32_t rsvd:2;
        uint32_t qnq:1;
        uint32_t rss_bank:1;
        uint32_t lro_timer_pop:1;
        uint32_t lro_desc:1;
        uint32_t rsvd1:1;
        uint32_t data_offset:8;
        uint32_t rsvd2:2;
        uint32_t rss_policy:4;
        uint32_t rsvd3:7;
        uint32_t cqe_type:2;
        uint32_t valid:1;

        /* dw 3 */
        uint32_t rss_hash_value;
#endif
};

struct nic_hwlro_cqe_part2 {
#ifdef _BIG_ENDIAN
        /* dw 0 */
        uint32_t ip_opt:1;
        uint32_t vtp:1;
        uint32_t pkt_size:14;
        uint32_t vlan_tag:16;

        /* dw 1 */
        uint32_t tcp_window:16;
        uint32_t coalesced_size:16;
        
	/* dw 2 */
        uint32_t valid:1;
        uint32_t cqe_type:2;
        uint32_t rsvd:2;
        uint32_t push:1;
        uint32_t ts_opt:1;
        uint32_t threshold:1;
        uint32_t seg_cnt:8;
        uint32_t frame_lifespan:8;
        uint32_t ipv6_frame:1;
        uint32_t l4_cksum_pass:1;
        uint32_t ip_cksum_pass:1;
        uint32_t udpframe:1;
        uint32_t tcpframe:1;
        uint32_t ipframe:1;
        uint32_t rss_hp:1;
        uint32_t error:1;
        
	/* dw 3 */
        uint32_t tcp_ack_num;
#else
        /* dw 0 */
        uint32_t vlan_tag:16;
        uint32_t pkt_size:14;
        uint32_t vtp:1;
        uint32_t ip_opt:1;

        /* dw 1 */
        uint32_t coalesced_size:16;
        uint32_t tcp_window:16;

        /* dw 2 */
        uint32_t error:1;
        uint32_t rss_hp:1;
        uint32_t ipframe:1;
        uint32_t tcpframe:1;
        uint32_t udpframe:1;
        uint32_t ip_cksum_pass:1;
        uint32_t l4_cksum_pass:1;
        uint32_t ipv6_frame:1;
        uint32_t frame_lifespan:8;
        uint32_t seg_cnt:8;
        uint32_t threshold:1;
        uint32_t ts_opt:1;
        uint32_t push:1;
        uint32_t rsvd:2;
        uint32_t cqe_type:2;
        uint32_t valid:1;

        /* dw 3 */
        uint32_t tcp_ack_num;
#endif
};
