/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2016 Qlogic Corporation
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
/*
 * File: ql_hw.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */
#ifndef _QL_HW_H_
#define _QL_HW_H_

/*
 * PCIe Registers; Direct Mapped; Offsets from BAR0
 */

/*
 * Register offsets for QLE8030
 */

/*
 * Firmware Mailbox Registers
 *	0 thru 511; offsets 0x800 thru 0xFFC; 32bits each
 */
#define Q8_FW_MBOX0			0x00000800
#define Q8_FW_MBOX511			0x00000FFC

/*
 * Host Mailbox Registers
 *	0 thru 511; offsets 0x000 thru 0x7FC; 32bits each
 */
#define Q8_HOST_MBOX0			0x00000000
#define Q8_HOST_MBOX511			0x000007FC

#define Q8_MBOX_INT_ENABLE		0x00001000
#define Q8_MBOX_INT_MASK_MSIX		0x00001200
#define Q8_MBOX_INT_LEGACY		0x00003010

#define Q8_HOST_MBOX_CNTRL		0x00003038
#define Q8_FW_MBOX_CNTRL		0x0000303C

#define Q8_PEG_HALT_STATUS1		0x000034A8
#define Q8_PEG_HALT_STATUS2		0x000034AC
#define Q8_FIRMWARE_HEARTBEAT		0x000034B0

#define Q8_FLASH_LOCK_ID		0x00003500
#define Q8_DRIVER_LOCK_ID		0x00003504
#define Q8_FW_CAPABILITIES		0x00003528

#define Q8_FW_VER_MAJOR			0x00003550
#define Q8_FW_VER_MINOR			0x00003554
#define Q8_FW_VER_SUB			0x00003558

#define Q8_BOOTLD_ADDR			0x0000355C
#define Q8_BOOTLD_SIZE			0x00003560

#define Q8_FW_IMAGE_ADDR		0x00003564
#define Q8_FW_BUILD_NUMBER		0x00003568
#define Q8_FW_IMAGE_VALID		0x000035FC

#define Q8_CMDPEG_STATE			0x00003650

#define Q8_LINK_STATE			0x00003698
#define Q8_LINK_STATE_2			0x0000369C

#define Q8_LINK_SPEED_0			0x000036E0
#define Q8_LINK_SPEED_1			0x000036E4
#define Q8_LINK_SPEED_2			0x000036E8
#define Q8_LINK_SPEED_3			0x000036EC

#define Q8_MAX_LINK_SPEED_0		0x000036F0
#define Q8_MAX_LINK_SPEED_1		0x000036F4
#define Q8_MAX_LINK_SPEED_2		0x000036F8
#define Q8_MAX_LINK_SPEED_3		0x000036FC

#define Q8_ASIC_TEMPERATURE		0x000037B4

/*
 * CRB Window Registers
 *	0 thru 15; offsets 0x3800 thru 0x383C; 32bits each
 */
#define Q8_CRB_WINDOW_PF0		0x00003800
#define Q8_CRB_WINDOW_PF15		0x0000383C

#define Q8_FLASH_LOCK			0x00003850
#define Q8_FLASH_UNLOCK			0x00003854

#define Q8_DRIVER_LOCK			0x00003868
#define Q8_DRIVER_UNLOCK		0x0000386C

#define Q8_LEGACY_INT_PTR		0x000038C0
#define Q8_LEGACY_INT_TRIG		0x000038C4
#define Q8_LEGACY_INT_MASK		0x000038C8

#define Q8_WILD_CARD			0x000038F0
#define Q8_INFORMANT			0x000038FC

/*
 * Ethernet Interface Specific Registers
 */
#define Q8_DRIVER_OP_MODE		0x00003570
#define Q8_API_VERSION			0x0000356C
#define Q8_NPAR_STATE			0x0000359C

/*
 * End of PCIe Registers; Direct Mapped; Offsets from BAR0
 */

/*
 * Indirect Registers
 */
#define Q8_LED_DUAL_0			0x28084C80
#define Q8_LED_SINGLE_0			0x28084C90

#define Q8_LED_DUAL_1			0x28084CA0
#define Q8_LED_SINGLE_1			0x28084CB0

#define Q8_LED_DUAL_2			0x28084CC0
#define Q8_LED_SINGLE_2			0x28084CD0

#define Q8_LED_DUAL_3			0x28084CE0
#define Q8_LED_SINGLE_3			0x28084CF0

#define Q8_GPIO_1			0x28084D00
#define Q8_GPIO_2			0x28084D10
#define Q8_GPIO_3			0x28084D20
#define Q8_GPIO_4			0x28084D40
#define Q8_GPIO_5			0x28084D50
#define Q8_GPIO_6			0x28084D60
#define Q8_GPIO_7			0x42100060
#define Q8_GPIO_8			0x42100064

#define Q8_FLASH_SPI_STATUS		0x2808E010
#define Q8_FLASH_SPI_CONTROL		0x2808E014

#define Q8_FLASH_STATUS			0x42100004
#define Q8_FLASH_CONTROL		0x42110004
#define Q8_FLASH_ADDRESS		0x42110008
#define Q8_FLASH_WR_DATA		0x4211000C
#define Q8_FLASH_RD_DATA		0x42110018

#define Q8_FLASH_DIRECT_WINDOW		0x42110030
#define Q8_FLASH_DIRECT_DATA		0x42150000

#define Q8_MS_CNTRL			0x41000090

#define Q8_MS_ADDR_LO			0x41000094
#define Q8_MS_ADDR_HI			0x41000098

#define Q8_MS_WR_DATA_0_31		0x410000A0
#define Q8_MS_WR_DATA_32_63		0x410000A4
#define Q8_MS_WR_DATA_64_95		0x410000B0
#define Q8_MS_WR_DATA_96_127		0x410000B4

#define Q8_MS_RD_DATA_0_31		0x410000A8
#define Q8_MS_RD_DATA_32_63		0x410000AC
#define Q8_MS_RD_DATA_64_95		0x410000B8
#define Q8_MS_RD_DATA_96_127		0x410000BC

#define Q8_CRB_PEG_0			0x3400003c
#define Q8_CRB_PEG_1			0x3410003c
#define Q8_CRB_PEG_2			0x3420003c
#define Q8_CRB_PEG_3			0x3430003c
#define Q8_CRB_PEG_4			0x34B0003c

/*
 * Macros for reading and writing registers
 */

#if defined(__i386__) || defined(__amd64__)
#define Q8_MB()    __asm volatile("mfence" ::: "memory")
#define Q8_WMB()   __asm volatile("sfence" ::: "memory")
#define Q8_RMB()   __asm volatile("lfence" ::: "memory")
#else
#define Q8_MB()
#define Q8_WMB()
#define Q8_RMB()
#endif

#define READ_REG32(ha, reg) bus_read_4((ha->pci_reg), reg)

#define WRITE_REG32(ha, reg, val) \
	{\
		bus_write_4((ha->pci_reg), reg, val);\
		bus_read_4((ha->pci_reg), reg);\
	}

#define Q8_NUM_MBOX	512

#define Q8_MAX_NUM_MULTICAST_ADDRS	1022
#define Q8_MAC_ADDR_LEN			6

/*
 * Firmware Interface
 */

/*
 * Command Response Interface - Commands
 */

#define Q8_MBX_CONFIG_IP_ADDRESS		0x0001
#define Q8_MBX_CONFIG_INTR			0x0002
#define Q8_MBX_MAP_INTR_SRC			0x0003
#define Q8_MBX_MAP_SDS_TO_RDS			0x0006
#define Q8_MBX_CREATE_RX_CNTXT			0x0007
#define Q8_MBX_DESTROY_RX_CNTXT			0x0008
#define Q8_MBX_CREATE_TX_CNTXT			0x0009
#define Q8_MBX_DESTROY_TX_CNTXT			0x000A
#define Q8_MBX_ADD_RX_RINGS			0x000B
#define Q8_MBX_CONFIG_LRO_FLOW			0x000C
#define Q8_MBX_CONFIG_MAC_LEARNING		0x000D
#define Q8_MBX_GET_STATS			0x000F
#define Q8_MBX_GENERATE_INTR			0x0011
#define Q8_MBX_SET_MAX_MTU			0x0012
#define Q8_MBX_MAC_ADDR_CNTRL			0x001F
#define Q8_MBX_GET_PCI_CONFIG			0x0020
#define Q8_MBX_GET_NIC_PARTITION		0x0021
#define Q8_MBX_SET_NIC_PARTITION		0x0022
#define Q8_MBX_QUERY_WOL_CAP			0x002C
#define Q8_MBX_SET_WOL_CONFIG			0x002D
#define Q8_MBX_GET_MINIDUMP_TMPLT_SIZE		0x002F
#define Q8_MBX_GET_MINIDUMP_TMPLT		0x0030
#define Q8_MBX_GET_FW_DCBX_CAPS			0x0034
#define Q8_MBX_QUERY_DCBX_SETTINGS		0x0035
#define Q8_MBX_CONFIG_RSS			0x0041
#define Q8_MBX_CONFIG_RSS_TABLE			0x0042
#define Q8_MBX_CONFIG_INTR_COALESCE		0x0043
#define Q8_MBX_CONFIG_LED			0x0044
#define Q8_MBX_CONFIG_MAC_ADDR			0x0045
#define Q8_MBX_CONFIG_STATISTICS		0x0046
#define Q8_MBX_CONFIG_LOOPBACK			0x0047
#define Q8_MBX_LINK_EVENT_REQ			0x0048
#define Q8_MBX_CONFIG_MAC_RX_MODE		0x0049
#define Q8_MBX_CONFIG_FW_LRO			0x004A
#define Q8_MBX_HW_CONFIG			0x004C
#define Q8_MBX_INIT_NIC_FUNC			0x0060
#define Q8_MBX_STOP_NIC_FUNC			0x0061
#define Q8_MBX_IDC_REQ				0x0062
#define Q8_MBX_IDC_ACK				0x0063
#define Q8_MBX_SET_PORT_CONFIG			0x0066
#define Q8_MBX_GET_PORT_CONFIG			0x0067
#define Q8_MBX_GET_LINK_STATUS			0x0068



/*
 * Mailbox Command Response
 */
#define Q8_MBX_RSP_SUCCESS			0x0001
#define Q8_MBX_RSP_RESPONSE_FAILURE		0x0002
#define Q8_MBX_RSP_NO_CARD_CRB			0x0003
#define Q8_MBX_RSP_NO_CARD_MEM			0x0004
#define Q8_MBX_RSP_NO_CARD_RSRC			0x0005
#define Q8_MBX_RSP_INVALID_ARGS			0x0006
#define Q8_MBX_RSP_INVALID_ACTION		0x0007
#define Q8_MBX_RSP_INVALID_STATE		0x0008
#define Q8_MBX_RSP_NOT_SUPPORTED		0x0009
#define Q8_MBX_RSP_NOT_PERMITTED		0x000A
#define Q8_MBX_RSP_NOT_READY			0x000B
#define Q8_MBX_RSP_DOES_NOT_EXIST		0x000C
#define Q8_MBX_RSP_ALREADY_EXISTS		0x000D
#define Q8_MBX_RSP_BAD_SIGNATURE		0x000E
#define Q8_MBX_RSP_CMD_NOT_IMPLEMENTED		0x000F
#define Q8_MBX_RSP_CMD_INVALID			0x0010
#define Q8_MBX_RSP_TIMEOUT			0x0011
#define Q8_MBX_RSP_CMD_FAILED			0x0012
#define Q8_MBX_RSP_FATAL_TEMP			0x0013
#define Q8_MBX_RSP_MAX_EXCEEDED			0x0014
#define Q8_MBX_RSP_UNSPECIFIED			0x0015
#define Q8_MBX_RSP_INTR_CREATE_FAILED		0x0017
#define Q8_MBX_RSP_INTR_DELETE_FAILED		0x0018
#define Q8_MBX_RSP_INTR_INVALID_OP		0x0019
#define Q8_MBX_RSP_IDC_INTRMD_RSP		0x001A

#define Q8_MBX_CMD_VERSION	(0x2 << 13)
#define Q8_MBX_RSP_STATUS(x) (((!(x >> 9)) || ((x >> 9) == 1)) ? 0: (x >> 9))
/*
 * Configure IP Address
 */
typedef struct _q80_config_ip_addr {
	uint16_t	opcode;
	uint16_t 	count_version;

	uint8_t		cmd;
#define		Q8_MBX_CONFIG_IP_ADD_IP	0x1
#define		Q8_MBX_CONFIG_IP_DEL_IP	0x2

	uint8_t		ip_type;
#define		Q8_MBX_CONFIG_IP_V4	0x0
#define		Q8_MBX_CONFIG_IP_V6	0x1

	uint16_t	rsrvd;
	union {
		struct {
			uint32_t addr;
			uint32_t rsrvd[3];
		} ipv4;
		uint8_t	ipv6_addr[16];
	} u;
} __packed q80_config_ip_addr_t;

typedef struct _q80_config_ip_addr_rsp {
        uint16_t	opcode;
        uint16_t	regcnt_status;
} __packed q80_config_ip_addr_rsp_t;

/*
 * Configure Interrupt Command
 */
typedef struct _q80_intr {
	uint8_t		cmd_type;
#define		Q8_MBX_CONFIG_INTR_CREATE	0x1
#define		Q8_MBX_CONFIG_INTR_DELETE	0x2
#define		Q8_MBX_CONFIG_INTR_TYPE_LINE	(0x1 << 4)
#define		Q8_MBX_CONFIG_INTR_TYPE_MSI_X	(0x3 << 4)

	uint8_t		rsrvd;
	uint16_t	msix_index;
} __packed q80_intr_t;

#define Q8_MAX_INTR_VECTORS	16
typedef struct _q80_config_intr {
	uint16_t	opcode;
	uint16_t 	count_version;
	uint8_t		nentries;
	uint8_t		rsrvd[3];
	q80_intr_t	intr[Q8_MAX_INTR_VECTORS];
} __packed q80_config_intr_t;

typedef struct _q80_intr_rsp {
	uint8_t		status;
	uint8_t		cmd;
	uint16_t	intr_id;
	uint32_t	intr_src;
} q80_intr_rsp_t;

typedef struct _q80_config_intr_rsp {
        uint16_t	opcode;
        uint16_t	regcnt_status;
	uint8_t		nentries;
	uint8_t		rsrvd[3];
	q80_intr_rsp_t	intr[Q8_MAX_INTR_VECTORS];
} __packed q80_config_intr_rsp_t;

/*
 * Configure LRO Flow Command
 */
typedef struct _q80_config_lro_flow {
	uint16_t	opcode;
	uint16_t 	count_version;

	uint8_t		cmd;
#define Q8_MBX_CONFIG_LRO_FLOW_ADD	0x01
#define Q8_MBX_CONFIG_LRO_FLOW_DELETE	0x02

	uint8_t		type_ts;
#define Q8_MBX_CONFIG_LRO_FLOW_IPV4		0x00
#define Q8_MBX_CONFIG_LRO_FLOW_IPV6		0x01
#define Q8_MBX_CONFIG_LRO_FLOW_TS_ABSENT	0x00
#define Q8_MBX_CONFIG_LRO_FLOW_TS_PRESENT	0x02

	uint16_t	rsrvd;
	union {
		struct {
			uint32_t addr;
			uint32_t rsrvd[3];
		} ipv4;
		uint8_t	ipv6_addr[16];
	} dst;
	union {
		struct {
			uint32_t addr;
			uint32_t rsrvd[3];
		} ipv4;
		uint8_t	ipv6_addr[16];
	} src;
	uint16_t	dst_port;
	uint16_t	src_port;
} __packed q80_config_lro_flow_t;

typedef struct _q80_config_lro_flow_rsp {
        uint16_t	opcode;
        uint16_t	regcnt_status;
} __packed q80_config_lro_flow_rsp_t;

typedef struct _q80_set_max_mtu {
	uint16_t	opcode;
	uint16_t 	count_version;
	uint32_t	cntxt_id;
	uint32_t	mtu;
} __packed q80_set_max_mtu_t;

typedef struct _q80_set_max_mtu_rsp {
        uint16_t	opcode;
        uint16_t	regcnt_status;
} __packed q80_set_max_mtu_rsp_t;

/*
 * Configure RSS 
 */
typedef struct _q80_config_rss {
	uint16_t	opcode;
	uint16_t 	count_version;

	uint16_t	cntxt_id;
	uint16_t	rsrvd;

	uint8_t		hash_type;
#define Q8_MBX_RSS_HASH_TYPE_IPV4_IP		(0x1 << 4)
#define Q8_MBX_RSS_HASH_TYPE_IPV4_TCP		(0x2 << 4)
#define Q8_MBX_RSS_HASH_TYPE_IPV4_TCP_IP	(0x3 << 4)
#define Q8_MBX_RSS_HASH_TYPE_IPV6_IP		(0x1 << 6)
#define Q8_MBX_RSS_HASH_TYPE_IPV6_TCP		(0x2 << 6)
#define Q8_MBX_RSS_HASH_TYPE_IPV6_TCP_IP	(0x3 << 6)

	uint8_t		flags;
#define Q8_MBX_RSS_FLAGS_ENABLE_RSS		(0x1)
#define Q8_MBX_RSS_FLAGS_USE_IND_TABLE		(0x2)
#define Q8_MBX_RSS_FLAGS_TYPE_CRSS		(0x4)

	uint16_t	indtbl_mask;
#define Q8_MBX_RSS_INDTBL_MASK			0x7F
#define Q8_MBX_RSS_FLAGS_MULTI_RSS_VALID	0x8000

	uint32_t	multi_rss;
#define Q8_MBX_RSS_MULTI_RSS_ENGINE_ASSIGN	BIT_30
#define Q8_MBX_RSS_USE_MULTI_RSS_ENGINES	BIT_31

	uint64_t	rss_key[5];
} __packed q80_config_rss_t;

typedef struct _q80_config_rss_rsp {
        uint16_t	opcode;
        uint16_t	regcnt_status;
} __packed q80_config_rss_rsp_t;

/*
 * Configure RSS Indirection Table
 */
#define Q8_RSS_IND_TBL_SIZE	40
#define Q8_RSS_IND_TBL_MIN_IDX	0
#define Q8_RSS_IND_TBL_MAX_IDX	127

typedef struct _q80_config_rss_ind_table {
	uint16_t	opcode;
	uint16_t 	count_version;
	uint8_t		start_idx;
	uint8_t		end_idx;
	uint16_t 	cntxt_id;
	uint8_t		ind_table[Q8_RSS_IND_TBL_SIZE];
} __packed q80_config_rss_ind_table_t;

typedef struct _q80_config_rss_ind_table_rsp {
        uint16_t	opcode;
        uint16_t	regcnt_status;
} __packed q80_config_rss_ind_table_rsp_t;

/*
 * Configure Interrupt Coalescing and Generation
 */
typedef struct _q80_config_intr_coalesc {
	uint16_t	opcode;
	uint16_t 	count_version;
        uint16_t	flags;
#define Q8_MBX_INTRC_FLAGS_RCV		1
#define Q8_MBX_INTRC_FLAGS_XMT		2
#define Q8_MBX_INTRC_FLAGS_PERIODIC	(1 << 3)

        uint16_t	cntxt_id;
        uint16_t	max_pkts;
        uint16_t	max_mswait;
        uint8_t		timer_type;
#define Q8_MBX_INTRC_TIMER_NONE			0
#define Q8_MBX_INTRC_TIMER_SINGLE		1
#define Q8_MBX_INTRC_TIMER_PERIODIC		2

        uint16_t	sds_ring_mask;

        uint8_t		rsrvd;
        uint32_t	ms_timeout;
} __packed q80_config_intr_coalesc_t;

typedef struct _q80_config_intr_coalesc_rsp {
        uint16_t	opcode;
        uint16_t	regcnt_status;
} __packed q80_config_intr_coalesc_rsp_t;

/*
 * Configure MAC Address
 */
#define Q8_ETHER_ADDR_LEN		6
typedef struct _q80_mac_addr {
	uint8_t		addr[Q8_ETHER_ADDR_LEN];
	uint16_t	vlan_tci;
} __packed q80_mac_addr_t;

#define Q8_MAX_MAC_ADDRS	64

typedef struct _q80_config_mac_addr {
	uint16_t	opcode;
	uint16_t 	count_version;
	uint8_t		cmd;
#define Q8_MBX_CMAC_CMD_ADD_MAC_ADDR	1
#define Q8_MBX_CMAC_CMD_DEL_MAC_ADDR	2

#define Q8_MBX_CMAC_CMD_CAM_BOTH	(0x0 << 6)
#define Q8_MBX_CMAC_CMD_CAM_INGRESS	(0x1 << 6)
#define Q8_MBX_CMAC_CMD_CAM_EGRESS	(0x2 << 6)

	uint8_t		nmac_entries;
	uint16_t	cntxt_id;
	q80_mac_addr_t	mac_addr[Q8_MAX_MAC_ADDRS];
} __packed q80_config_mac_addr_t;

typedef struct _q80_config_mac_addr_rsp {
        uint16_t	opcode;
        uint16_t	regcnt_status;
	uint8_t		cmd;
	uint8_t		nmac_entries;
	uint16_t	cntxt_id;
	uint32_t	status[Q8_MAX_MAC_ADDRS];
} __packed q80_config_mac_addr_rsp_t;

/*
 * Configure MAC Receive Mode
 */
typedef struct _q80_config_mac_rcv_mode {
	uint16_t	opcode;
	uint16_t 	count_version;

	uint8_t		mode;
#define Q8_MBX_MAC_RCV_PROMISC_ENABLE	0x1
#define Q8_MBX_MAC_ALL_MULTI_ENABLE	0x2

	uint8_t		rsrvd;
	uint16_t	cntxt_id;
} __packed q80_config_mac_rcv_mode_t;

typedef struct _q80_config_mac_rcv_mode_rsp {
        uint16_t	opcode;
        uint16_t	regcnt_status;
} __packed q80_config_mac_rcv_mode_rsp_t;

/*
 * Configure Firmware Controlled LRO
 */
typedef struct _q80_config_fw_lro {
	uint16_t	opcode;
	uint16_t 	count_version;

	uint8_t		flags;
#define Q8_MBX_FW_LRO_IPV4                     0x1
#define Q8_MBX_FW_LRO_IPV6                     0x2
#define Q8_MBX_FW_LRO_IPV4_WO_DST_IP_CHK       0x4
#define Q8_MBX_FW_LRO_IPV6_WO_DST_IP_CHK       0x8
#define Q8_MBX_FW_LRO_LOW_THRESHOLD            0x10

	uint8_t		rsrvd;
	uint16_t	cntxt_id;

	uint16_t	low_threshold;
	uint16_t	rsrvd0;
} __packed q80_config_fw_lro_t;

typedef struct _q80_config_fw_lro_rsp {
        uint16_t	opcode;
        uint16_t	regcnt_status;
} __packed q80_config_fw_lro_rsp_t;

/*
 * Minidump mailbox commands
 */
typedef struct _q80_config_md_templ_size {
	uint16_t	opcode;
	uint16_t	count_version;
} __packed q80_config_md_templ_size_t;

typedef struct _q80_config_md_templ_size_rsp {
	uint16_t	opcode;
	uint16_t	regcnt_status;
	uint32_t	rsrvd;
	uint32_t	templ_size;
	uint32_t	templ_version;
} __packed q80_config_md_templ_size_rsp_t;

typedef struct _q80_config_md_templ_cmd {
	uint16_t	opcode;
	uint16_t	count_version;
	uint64_t	buf_addr; /* physical address of buffer */
	uint32_t	buff_size;
	uint32_t	offset;
} __packed q80_config_md_templ_cmd_t;

typedef struct _q80_config_md_templ_cmd_rsp {
	uint16_t	opcode;
	uint16_t	regcnt_status;
	uint32_t	rsrvd;
	uint32_t	templ_size;
	uint32_t	buff_size;
	uint32_t	offset;
} __packed q80_config_md_templ_cmd_rsp_t;

/*
 * Hardware Configuration Commands
 */

typedef struct _q80_hw_config {
       uint16_t        opcode;
       uint16_t        count_version;
#define Q8_HW_CONFIG_SET_MDIO_REG_COUNT                0x06
#define Q8_HW_CONFIG_GET_MDIO_REG_COUNT                0x05
#define Q8_HW_CONFIG_SET_CAM_SEARCH_MODE_COUNT 0x03
#define Q8_HW_CONFIG_GET_CAM_SEARCH_MODE_COUNT 0x02
#define Q8_HW_CONFIG_SET_TEMP_THRESHOLD_COUNT  0x03
#define Q8_HW_CONFIG_GET_TEMP_THRESHOLD_COUNT  0x02
#define Q8_HW_CONFIG_GET_ECC_COUNTS_COUNT      0x02

       uint32_t        cmd;
#define Q8_HW_CONFIG_SET_MDIO_REG              0x01
#define Q8_HW_CONFIG_GET_MDIO_REG              0x02
#define Q8_HW_CONFIG_SET_CAM_SEARCH_MODE       0x03
#define Q8_HW_CONFIG_GET_CAM_SEARCH_MODE       0x04
#define Q8_HW_CONFIG_SET_TEMP_THRESHOLD                0x07
#define Q8_HW_CONFIG_GET_TEMP_THRESHOLD                0x08
#define Q8_HW_CONFIG_GET_ECC_COUNTS            0x0A

       union {
               struct {
                       uint32_t phys_port_number;
                       uint32_t phy_dev_addr;
                       uint32_t reg_addr;
                       uint32_t data;
               } set_mdio;

               struct {
                       uint32_t phys_port_number;
                       uint32_t phy_dev_addr;
                       uint32_t reg_addr;
               } get_mdio;

               struct {
                       uint32_t mode;
#define Q8_HW_CONFIG_CAM_SEARCH_MODE_INTERNAL  0x1
#define Q8_HW_CONFIG_CAM_SEARCH_MODE_AUTO      0x2

               } set_cam_search_mode;

               struct {
                       uint32_t value;
               } set_temp_threshold;
       } u;
} __packed q80_hw_config_t;

typedef struct _q80_hw_config_rsp {
        uint16_t       opcode;
        uint16_t       regcnt_status;

       union {
               struct {
                       uint32_t value;
               } get_mdio;

               struct {
                       uint32_t mode;
               } get_cam_search_mode;

               struct {
                       uint32_t temp_warn;
                       uint32_t curr_temp;
                       uint32_t osc_ring_rate;
                       uint32_t core_voltage;
               } get_temp_threshold;

               struct {
                       uint32_t ddr_ecc_error_count;
                       uint32_t ocm_ecc_error_count;
                       uint32_t l2_dcache_ecc_error_count;
                       uint32_t l2_icache_ecc_error_count;
                       uint32_t eport_ecc_error_count;
               } get_ecc_counts;
       } u;
} __packed q80_hw_config_rsp_t;

/*
 * Link Event Request Command
 */
typedef struct _q80_link_event {
	uint16_t	opcode;
	uint16_t 	count_version;
	uint8_t		cmd;
#define Q8_LINK_EVENT_CMD_STOP_PERIODIC	0
#define Q8_LINK_EVENT_CMD_ENABLE_ASYNC	1

	uint8_t		flags;
#define Q8_LINK_EVENT_FLAGS_SEND_RSP	1

	uint16_t	cntxt_id;
} __packed q80_link_event_t;

typedef struct _q80_link_event_rsp {
        uint16_t	opcode;
        uint16_t	regcnt_status;
} __packed q80_link_event_rsp_t;

/*
 * Get Statistics Command
 */
typedef struct _q80_rcv_stats {
	uint64_t	total_bytes;
	uint64_t	total_pkts;
	uint64_t	lro_pkt_count;
	uint64_t	sw_pkt_count;
	uint64_t	ip_chksum_err;
	uint64_t	pkts_wo_acntxts;
	uint64_t	pkts_dropped_no_sds_card;
	uint64_t	pkts_dropped_no_sds_host;
	uint64_t	oversized_pkts;
	uint64_t	pkts_dropped_no_rds;
	uint64_t	unxpctd_mcast_pkts;
	uint64_t	re1_fbq_error;
	uint64_t	invalid_mac_addr;
	uint64_t	rds_prime_trys;
	uint64_t	rds_prime_success;
	uint64_t	lro_flows_added;
	uint64_t	lro_flows_deleted;
	uint64_t	lro_flows_active;
	uint64_t	pkts_droped_unknown;
	uint64_t	pkts_cnt_oversized;
} __packed q80_rcv_stats_t;

typedef struct _q80_xmt_stats {
	uint64_t	total_bytes;
	uint64_t	total_pkts;
	uint64_t	errors;
	uint64_t	pkts_dropped;
	uint64_t	switch_pkts;
	uint64_t	num_buffers;
} __packed q80_xmt_stats_t;

typedef struct _q80_mac_stats {
	uint64_t	xmt_frames;
	uint64_t	xmt_bytes;
	uint64_t	xmt_mcast_pkts;
	uint64_t	xmt_bcast_pkts;
	uint64_t	xmt_pause_frames;
	uint64_t	xmt_cntrl_pkts;
	uint64_t	xmt_pkt_lt_64bytes;
	uint64_t	xmt_pkt_lt_127bytes;
	uint64_t	xmt_pkt_lt_255bytes;
	uint64_t	xmt_pkt_lt_511bytes;
	uint64_t	xmt_pkt_lt_1023bytes;
	uint64_t	xmt_pkt_lt_1518bytes;
	uint64_t	xmt_pkt_gt_1518bytes;
	uint64_t	rsrvd0[3];
	uint64_t	rcv_frames;
	uint64_t	rcv_bytes;
	uint64_t	rcv_mcast_pkts;
	uint64_t	rcv_bcast_pkts;
	uint64_t	rcv_pause_frames;
	uint64_t	rcv_cntrl_pkts;
	uint64_t	rcv_pkt_lt_64bytes;
	uint64_t	rcv_pkt_lt_127bytes;
	uint64_t	rcv_pkt_lt_255bytes;
	uint64_t	rcv_pkt_lt_511bytes;
	uint64_t	rcv_pkt_lt_1023bytes;
	uint64_t	rcv_pkt_lt_1518bytes;
	uint64_t	rcv_pkt_gt_1518bytes;
	uint64_t	rsrvd1[3];
	uint64_t	rcv_len_error;
	uint64_t	rcv_len_small;
	uint64_t	rcv_len_large;
	uint64_t	rcv_jabber;
	uint64_t	rcv_dropped;
	uint64_t	fcs_error;
	uint64_t	align_error;
	uint64_t	eswitched_frames;
	uint64_t	eswitched_bytes;
	uint64_t	eswitched_mcast_frames;
	uint64_t	eswitched_bcast_frames;
	uint64_t	eswitched_ucast_frames;
	uint64_t	eswitched_err_free_frames;
	uint64_t	eswitched_err_free_bytes;
} __packed q80_mac_stats_t;

typedef struct _q80_get_stats {
	uint16_t	opcode;
	uint16_t 	count_version;

	uint32_t 	cmd;
#define Q8_GET_STATS_CMD_CLEAR		0x01
#define Q8_GET_STATS_CMD_RCV		0x00
#define Q8_GET_STATS_CMD_XMT		0x02
#define Q8_GET_STATS_CMD_TYPE_CNTXT	0x00
#define Q8_GET_STATS_CMD_TYPE_MAC	0x04
#define Q8_GET_STATS_CMD_TYPE_FUNC	0x08
#define Q8_GET_STATS_CMD_TYPE_VPORT	0x0C
#define Q8_GET_STATS_CMD_TYPE_ALL      (0x7 << 2)

} __packed q80_get_stats_t;

typedef struct _q80_get_stats_rsp {
        uint16_t	opcode;
        uint16_t	regcnt_status;
	uint32_t 	cmd;
	union {
		q80_rcv_stats_t rcv;
		q80_xmt_stats_t xmt;
		q80_mac_stats_t mac;
	} u;
} __packed q80_get_stats_rsp_t;

typedef struct _q80_get_mac_rcv_xmt_stats_rsp {
	uint16_t	opcode;
	uint16_t	regcnt_status;
	uint32_t	cmd;
	q80_mac_stats_t mac;
	q80_rcv_stats_t rcv;
	q80_xmt_stats_t xmt;
} __packed q80_get_mac_rcv_xmt_stats_rsp_t;

/*
 * Init NIC Function
 * Used to Register DCBX Configuration Change AEN
 */
typedef struct _q80_init_nic_func {
        uint16_t        opcode;
        uint16_t        count_version;

        uint32_t        options;
#define Q8_INIT_NIC_REG_IDC_AEN		0x01
#define Q8_INIT_NIC_REG_DCBX_CHNG_AEN	0x02
#define Q8_INIT_NIC_REG_SFP_CHNG_AEN	0x04

} __packed q80_init_nic_func_t;

typedef struct _q80_init_nic_func_rsp {
        uint16_t        opcode;
        uint16_t        regcnt_status;
} __packed q80_init_nic_func_rsp_t;

/*
 * Stop NIC Function
 * Used to DeRegister DCBX Configuration Change AEN
 */
typedef struct _q80_stop_nic_func {
        uint16_t        opcode;
        uint16_t        count_version;

        uint32_t        options;
#define Q8_STOP_NIC_DEREG_DCBX_CHNG_AEN 0x02
#define Q8_STOP_NIC_DEREG_SFP_CHNG_AEN	0x04

} __packed q80_stop_nic_func_t;

typedef struct _q80_stop_nic_func_rsp {
        uint16_t        opcode;
        uint16_t        regcnt_status;
} __packed q80_stop_nic_func_rsp_t;

/*
 * Query Firmware DCBX Capabilities
 */
typedef struct _q80_query_fw_dcbx_caps {
        uint16_t        opcode;
        uint16_t        count_version;
} __packed q80_query_fw_dcbx_caps_t;

typedef struct _q80_query_fw_dcbx_caps_rsp {
        uint16_t        opcode;
        uint16_t        regcnt_status;

        uint32_t        dcbx_caps;
#define Q8_QUERY_FW_DCBX_CAPS_TSA               0x00000001
#define Q8_QUERY_FW_DCBX_CAPS_ETS               0x00000002
#define Q8_QUERY_FW_DCBX_CAPS_DCBX_CEE_1_01     0x00000004
#define Q8_QUERY_FW_DCBX_CAPS_DCBX_IEEE_1_0     0x00000008
#define Q8_QUERY_FW_DCBX_MAX_TC_MASK            0x00F00000
#define Q8_QUERY_FW_DCBX_MAX_ETS_TC_MASK        0x0F000000
#define Q8_QUERY_FW_DCBX_MAX_PFC_TC_MASK        0xF0000000

} __packed q80_query_fw_dcbx_caps_rsp_t;

/*
 * IDC Ack Cmd
 */

typedef struct _q80_idc_ack {
	uint16_t	opcode;
	uint16_t	count_version;

	uint32_t	aen_mb1;
	uint32_t	aen_mb2;
	uint32_t	aen_mb3;
	uint32_t	aen_mb4;

} __packed q80_idc_ack_t;

typedef struct _q80_idc_ack_rsp {
	uint16_t	opcode;
	uint16_t	regcnt_status;
} __packed q80_idc_ack_rsp_t;


/*
 * Set Port Configuration command
 * Used to set Ethernet Standard Pause values
 */

typedef struct _q80_set_port_cfg {
	uint16_t	opcode;
	uint16_t	count_version;

	uint32_t	cfg_bits;

#define Q8_PORT_CFG_BITS_LOOPBACK_MODE_MASK	(0x7 << 1)
#define Q8_PORT_CFG_BITS_LOOPBACK_MODE_NONE	(0x0 << 1)
#define Q8_PORT_CFG_BITS_LOOPBACK_MODE_HSS	(0x2 << 1)
#define Q8_PORT_CFG_BITS_LOOPBACK_MODE_PHY	(0x3 << 1)
#define Q8_PORT_CFG_BITS_LOOPBACK_MODE_EXT	(0x4 << 1)

#define Q8_VALID_LOOPBACK_MODE(mode) \
             (((mode) == Q8_PORT_CFG_BITS_LOOPBACK_MODE_NONE) || \
		(((mode) >= Q8_PORT_CFG_BITS_LOOPBACK_MODE_HSS) && \
		 ((mode) <= Q8_PORT_CFG_BITS_LOOPBACK_MODE_EXT)))

#define Q8_PORT_CFG_BITS_DCBX_ENABLE		BIT_4

#define Q8_PORT_CFG_BITS_PAUSE_CFG_MASK		(0x3 << 5)
#define Q8_PORT_CFG_BITS_PAUSE_DISABLED		(0x0 << 5)
#define Q8_PORT_CFG_BITS_PAUSE_STD		(0x1 << 5)
#define Q8_PORT_CFG_BITS_PAUSE_PPM		(0x2 << 5)

#define Q8_PORT_CFG_BITS_LNKCAP_10MB		BIT_8
#define Q8_PORT_CFG_BITS_LNKCAP_100MB		BIT_9
#define Q8_PORT_CFG_BITS_LNKCAP_1GB		BIT_10
#define Q8_PORT_CFG_BITS_LNKCAP_10GB		BIT_11

#define Q8_PORT_CFG_BITS_AUTONEG		BIT_15
#define Q8_PORT_CFG_BITS_XMT_DISABLE		BIT_17
#define Q8_PORT_CFG_BITS_FEC_RQSTD		BIT_18
#define Q8_PORT_CFG_BITS_EEE_RQSTD		BIT_19

#define Q8_PORT_CFG_BITS_STDPAUSE_DIR_MASK	(0x3 << 20)
#define Q8_PORT_CFG_BITS_STDPAUSE_XMT_RCV	(0x0 << 20)
#define Q8_PORT_CFG_BITS_STDPAUSE_XMT		(0x1 << 20)
#define Q8_PORT_CFG_BITS_STDPAUSE_RCV		(0x2 << 20)

} __packed q80_set_port_cfg_t;

typedef struct _q80_set_port_cfg_rsp {
	uint16_t	opcode;
	uint16_t	regcnt_status;
} __packed q80_set_port_cfg_rsp_t;

/*
 * Get Port Configuration Command
 */

typedef struct _q80_get_port_cfg {
	uint16_t	opcode;
	uint16_t	count_version;
} __packed q80_get_port_cfg_t;

typedef struct _q80_get_port_cfg_rsp {
	uint16_t	opcode;
	uint16_t	regcnt_status;

	uint32_t	cfg_bits; /* same as in q80_set_port_cfg_t */

	uint8_t		phys_port_type;
	uint8_t		rsvd[3];
} __packed q80_get_port_cfg_rsp_t;

/*
 * Get Link Status Command
 * Used to get current PAUSE values for the port
 */

typedef struct _q80_get_link_status {
        uint16_t        opcode;
        uint16_t        count_version;
} __packed q80_get_link_status_t;

typedef struct _q80_get_link_status_rsp {
        uint16_t        opcode;
        uint16_t        regcnt_status;

	uint32_t	cfg_bits;
#define Q8_GET_LINK_STAT_CFG_BITS_LINK_UP		BIT_0

#define Q8_GET_LINK_STAT_CFG_BITS_LINK_SPEED_MASK	(0x7 << 3)
#define Q8_GET_LINK_STAT_CFG_BITS_LINK_SPEED_UNKNOWN	(0x0 << 3)
#define Q8_GET_LINK_STAT_CFG_BITS_LINK_SPEED_10MB	(0x1 << 3)
#define Q8_GET_LINK_STAT_CFG_BITS_LINK_SPEED_100MB	(0x2 << 3)
#define Q8_GET_LINK_STAT_CFG_BITS_LINK_SPEED_1GB	(0x3 << 3)
#define Q8_GET_LINK_STAT_CFG_BITS_LINK_SPEED_10GB	(0x4 << 3)

#define Q8_GET_LINK_STAT_CFG_BITS_PAUSE_CFG_MASK	(0x3 << 6)
#define Q8_GET_LINK_STAT_CFG_BITS_PAUSE_CFG_DISABLE	(0x0 << 6)
#define Q8_GET_LINK_STAT_CFG_BITS_PAUSE_CFG_STD		(0x1 << 6)
#define Q8_GET_LINK_STAT_CFG_BITS_PAUSE_CFG_PPM		(0x2 << 6)

#define Q8_GET_LINK_STAT_CFG_BITS_LOOPBACK_MASK		(0x7 << 8)
#define Q8_GET_LINK_STAT_CFG_BITS_LOOPBACK_NONE		(0x0 << 6)
#define Q8_GET_LINK_STAT_CFG_BITS_LOOPBACK_HSS		(0x2 << 6)
#define Q8_GET_LINK_STAT_CFG_BITS_LOOPBACK_PHY		(0x3 << 6)

#define Q8_GET_LINK_STAT_CFG_BITS_FEC_ENABLED		BIT_12
#define Q8_GET_LINK_STAT_CFG_BITS_EEE_ENABLED		BIT_13

#define Q8_GET_LINK_STAT_CFG_BITS_STDPAUSE_DIR_MASK	(0x3 << 20)
#define Q8_GET_LINK_STAT_CFG_BITS_STDPAUSE_NONE		(0x0 << 20)
#define Q8_GET_LINK_STAT_CFG_BITS_STDPAUSE_XMT		(0x1 << 20)
#define Q8_GET_LINK_STAT_CFG_BITS_STDPAUSE_RCV		(0x2 << 20)
#define Q8_GET_LINK_STAT_CFG_BITS_STDPAUSE_XMT_RCV	(0x3 << 20)

	uint32_t	link_state;
#define Q8_GET_LINK_STAT_LOSS_OF_SIGNAL			BIT_0
#define Q8_GET_LINK_STAT_PORT_RST_DONE			BIT_3
#define Q8_GET_LINK_STAT_PHY_LINK_DOWN			BIT_4
#define Q8_GET_LINK_STAT_PCS_LINK_DOWN			BIT_5
#define Q8_GET_LINK_STAT_MAC_LOCAL_FAULT		BIT_6
#define Q8_GET_LINK_STAT_MAC_REMOTE_FAULT		BIT_7
#define Q8_GET_LINK_STAT_XMT_DISABLED			BIT_9
#define Q8_GET_LINK_STAT_SFP_XMT_FAULT			BIT_10

	uint32_t	sfp_info;
#define Q8_GET_LINK_STAT_SFP_TRNCVR_MASK		0x3
#define Q8_GET_LINK_STAT_SFP_TRNCVR_NOT_EXPECTED	0x0
#define Q8_GET_LINK_STAT_SFP_TRNCVR_NONE		0x1
#define Q8_GET_LINK_STAT_SFP_TRNCVR_INVALID		0x2
#define Q8_GET_LINK_STAT_SFP_TRNCVR_VALID		0x3

#define Q8_GET_LINK_STAT_SFP_ADDTL_INFO_MASK		(0x3 << 2)
#define Q8_GET_LINK_STAT_SFP_ADDTL_INFO_UNREC_TRSVR	(0x0 << 2)
#define Q8_GET_LINK_STAT_SFP_ADDTL_INFO_NOT_QLOGIC	(0x1 << 2)
#define Q8_GET_LINK_STAT_SFP_ADDTL_INFO_SPEED_FAILED	(0x2 << 2)
#define Q8_GET_LINK_STAT_SFP_ADDTL_INFO_ACCESS_ERROR	(0x3 << 2)

#define Q8_GET_LINK_STAT_SFP_MOD_TYPE_MASK		(0x1F << 4)
#define Q8_GET_LINK_STAT_SFP_MOD_NONE			(0x00 << 4)
#define Q8_GET_LINK_STAT_SFP_MOD_10GBLRM		(0x01 << 4)
#define Q8_GET_LINK_STAT_SFP_MOD_10GBLR			(0x02 << 4)
#define Q8_GET_LINK_STAT_SFP_MOD_10GBSR			(0x03 << 4)
#define Q8_GET_LINK_STAT_SFP_MOD_10GBC_P		(0x04 << 4)
#define Q8_GET_LINK_STAT_SFP_MOD_10GBC_AL		(0x05 << 4)
#define Q8_GET_LINK_STAT_SFP_MOD_10GBC_PL		(0x06 << 4)
#define Q8_GET_LINK_STAT_SFP_MOD_1GBSX			(0x07 << 4)
#define Q8_GET_LINK_STAT_SFP_MOD_1GBLX			(0x08 << 4)
#define Q8_GET_LINK_STAT_SFP_MOD_1GBCX			(0x09 << 4)
#define Q8_GET_LINK_STAT_SFP_MOD_1GBT			(0x0A << 4)
#define Q8_GET_LINK_STAT_SFP_MOD_1GBC_PL		(0x0B << 4)
#define Q8_GET_LINK_STAT_SFP_MOD_UNKNOWN		(0x0F << 4)

#define Q8_GET_LINK_STAT_SFP_MULTI_RATE_MOD		BIT_9
#define Q8_GET_LINK_STAT_SFP_XMT_FAULT			BIT_10
#define Q8_GET_LINK_STAT_SFP_COPPER_CBL_LENGTH_MASK	(0xFF << 16)

} __packed q80_get_link_status_rsp_t;


/*
 * Transmit Related Definitions
 */
/* Max# of TX Rings per Tx Create Cntxt Mbx Cmd*/
#define MAX_TCNTXT_RINGS           8

/*
 * Transmit Context - Q8_CMD_CREATE_TX_CNTXT Command Configuration Data
 */

typedef struct _q80_rq_tx_ring {
	uint64_t	paddr;
	uint64_t	tx_consumer;
	uint16_t	nentries;
	uint16_t	intr_id;
	uint8_t 	intr_src_bit;
	uint8_t 	rsrvd[3];
} __packed q80_rq_tx_ring_t;

typedef struct _q80_rq_tx_cntxt {
	uint16_t		opcode;
	uint16_t 		count_version;

	uint32_t		cap0;
#define Q8_TX_CNTXT_CAP0_BASEFW		(1 << 0)
#define Q8_TX_CNTXT_CAP0_LSO		(1 << 6)
#define Q8_TX_CNTXT_CAP0_TC		(1 << 25)

	uint32_t		cap1;
	uint32_t		cap2;
	uint32_t		cap3;
	uint8_t			ntx_rings;
	uint8_t			traffic_class; /* bits 8-10; others reserved */
	uint16_t		tx_vpid;
	q80_rq_tx_ring_t	tx_ring[MAX_TCNTXT_RINGS];
} __packed q80_rq_tx_cntxt_t;

typedef struct _q80_rsp_tx_ring {
	uint32_t		prod_index;
	uint16_t		cntxt_id;
	uint8_t			state;
	uint8_t			rsrvd;
} q80_rsp_tx_ring_t;

typedef struct _q80_rsp_tx_cntxt {
        uint16_t                opcode;
        uint16_t                regcnt_status;
	uint8_t			ntx_rings;
        uint8_t                 phy_port;
        uint8_t                 virt_port;
	uint8_t                 rsrvd;
	q80_rsp_tx_ring_t	tx_ring[MAX_TCNTXT_RINGS];
} __packed q80_rsp_tx_cntxt_t;

typedef struct _q80_tx_cntxt_destroy {
        uint16_t        opcode;
	uint16_t 	count_version;
        uint32_t        cntxt_id;
} __packed q80_tx_cntxt_destroy_t;

typedef struct _q80_tx_cntxt_destroy_rsp {
	uint16_t	opcode;
	uint16_t	regcnt_status;
} __packed q80_tx_cntxt_destroy_rsp_t;

/*
 * Transmit Command Descriptor
 * These commands are issued on the Transmit Ring associated with a Transmit
 * context
 */
typedef struct _q80_tx_cmd {
	uint8_t		tcp_hdr_off;	/* TCP Header Offset */
	uint8_t		ip_hdr_off;	/* IP Header Offset */
	uint16_t	flags_opcode;	/* Bits 0-6: flags; 7-12: opcode */

	/* flags field */
#define Q8_TX_CMD_FLAGS_MULTICAST	0x01
#define Q8_TX_CMD_FLAGS_LSO_TSO		0x02
#define Q8_TX_CMD_FLAGS_VLAN_TAGGED	0x10
#define Q8_TX_CMD_FLAGS_HW_VLAN_ID	0x40

	/* opcode field */
#define Q8_TX_CMD_OP_XMT_UDP_CHKSUM_IPV6	(0xC << 7)
#define Q8_TX_CMD_OP_XMT_TCP_CHKSUM_IPV6	(0xB << 7)
#define Q8_TX_CMD_OP_XMT_TCP_LSO_IPV6		(0x6 << 7)
#define Q8_TX_CMD_OP_XMT_TCP_LSO		(0x5 << 7)
#define Q8_TX_CMD_OP_XMT_UDP_CHKSUM		(0x3 << 7)
#define Q8_TX_CMD_OP_XMT_TCP_CHKSUM		(0x2 << 7)
#define Q8_TX_CMD_OP_XMT_ETHER			(0x1 << 7)

	uint8_t		n_bufs;		/* # of data segs in data buffer */
	uint8_t		data_len_lo;	/* data length lower 8 bits */
	uint16_t	data_len_hi;	/* data length upper 16 bits */

	uint64_t	buf2_addr;	/* buffer 2 address */

	uint16_t	rsrvd0;
	uint16_t	mss;		/* MSS for this packet */
	uint8_t		cntxtid;	/* Bits 7-4: ContextId; 3-0: reserved */

#define Q8_TX_CMD_PORT_CNXTID(c_id) ((c_id & 0xF) << 4)

	uint8_t		total_hdr_len;	/* MAC+IP+TCP Header Length for LSO */
	uint16_t	rsrvd1;

	uint64_t	buf3_addr;	/* buffer 3 address */
	uint64_t	buf1_addr;	/* buffer 1 address */

	uint16_t	buf1_len;	/* length of buffer 1 */
	uint16_t	buf2_len;	/* length of buffer 2 */
	uint16_t	buf3_len;	/* length of buffer 3 */
	uint16_t	buf4_len;	/* length of buffer 4 */

	uint64_t	buf4_addr;	/* buffer 4 address */

	uint32_t	rsrvd2;
	uint16_t	rsrvd3;
	uint16_t	vlan_tci;	/* VLAN TCI when hw tagging is enabled*/

} __packed q80_tx_cmd_t; /* 64 bytes */

#define Q8_TX_CMD_MAX_SEGMENTS		4
#define Q8_TX_CMD_TSO_ALIGN		2
#define Q8_TX_MAX_NON_TSO_SEGS		62


/*
 * Receive Related Definitions
 */
#define MAX_RDS_RING_SETS	8 /* Max# of Receive Descriptor Rings */

#ifdef QL_ENABLE_ISCSI_TLV
#define MAX_SDS_RINGS           32 /* Max# of Status Descriptor Rings */
#define NUM_TX_RINGS		(MAX_SDS_RINGS * 2)
#else
#define MAX_SDS_RINGS           32 /* Max# of Status Descriptor Rings */
#define NUM_TX_RINGS		MAX_SDS_RINGS
#endif /* #ifdef QL_ENABLE_ISCSI_TLV */
#define MAX_RDS_RINGS           MAX_SDS_RINGS /* Max# of Rcv Descriptor Rings */


typedef struct _q80_rq_sds_ring {
	uint64_t paddr; /* physical addr of status ring in system memory */
	uint64_t hdr_split1;
	uint64_t hdr_split2;
	uint16_t size; /* number of entries in status ring */
	uint16_t hdr_split1_size;
	uint16_t hdr_split2_size;
	uint16_t hdr_split_count;
	uint16_t intr_id;
	uint8_t  intr_src_bit;
	uint8_t  rsrvd[5];
} __packed q80_rq_sds_ring_t; /* 10 32bit words */

typedef struct _q80_rq_rds_ring {
	uint64_t paddr_std;	/* physical addr of rcv ring in system memory */
	uint64_t paddr_jumbo;	/* physical addr of rcv ring in system memory */
	uint16_t std_bsize;
	uint16_t std_nentries;
	uint16_t jumbo_bsize;
	uint16_t jumbo_nentries;
} __packed q80_rq_rds_ring_t; /* 6 32bit words */

#define MAX_RCNTXT_SDS_RINGS	8

typedef struct _q80_rq_rcv_cntxt {
	uint16_t		opcode;
	uint16_t 		count_version;
	uint32_t		cap0;
#define Q8_RCV_CNTXT_CAP0_BASEFW	(1 << 0)
#define Q8_RCV_CNTXT_CAP0_MULTI_RDS	(1 << 1)
#define Q8_RCV_CNTXT_CAP0_LRO		(1 << 5)
#define Q8_RCV_CNTXT_CAP0_HW_LRO	(1 << 10)
#define Q8_RCV_CNTXT_CAP0_VLAN_ALIGN	(1 << 14)
#define Q8_RCV_CNTXT_CAP0_RSS		(1 << 15)
#define Q8_RCV_CNTXT_CAP0_MSFT_RSS	(1 << 16)
#define Q8_RCV_CNTXT_CAP0_SGL_JUMBO	(1 << 18)
#define Q8_RCV_CNTXT_CAP0_SGL_LRO	(1 << 19)
#define Q8_RCV_CNTXT_CAP0_SINGLE_JUMBO	(1 << 26)

	uint32_t		cap1;
	uint32_t		cap2;
	uint32_t		cap3;
	uint8_t 		nrds_sets_rings;
	uint8_t 		nsds_rings;
	uint16_t		rds_producer_mode;
#define Q8_RCV_CNTXT_RDS_PROD_MODE_UNIQUE	0
#define Q8_RCV_CNTXT_RDS_PROD_MODE_SHARED	1

	uint16_t		rcv_vpid;
	uint16_t		rsrvd0;
	uint32_t		rsrvd1;
	q80_rq_sds_ring_t	sds[MAX_RCNTXT_SDS_RINGS];
	q80_rq_rds_ring_t	rds[MAX_RDS_RING_SETS];
} __packed q80_rq_rcv_cntxt_t;

typedef struct _q80_rsp_rds_ring {
	uint32_t prod_std;
	uint32_t prod_jumbo;
} __packed q80_rsp_rds_ring_t; /* 8 bytes */

typedef struct _q80_rsp_rcv_cntxt {
	uint16_t		opcode;
	uint16_t		regcnt_status;
	uint8_t 		nrds_sets_rings;
	uint8_t 		nsds_rings;
	uint16_t		cntxt_id;
	uint8_t			state;
	uint8_t			num_funcs;
	uint8_t			phy_port;
	uint8_t			virt_port;
	uint32_t		sds_cons[MAX_RCNTXT_SDS_RINGS];
	q80_rsp_rds_ring_t	rds[MAX_RDS_RING_SETS];		
} __packed q80_rsp_rcv_cntxt_t;

typedef struct _q80_rcv_cntxt_destroy {
	uint16_t	opcode;
	uint16_t 	count_version;
	uint32_t	cntxt_id;
} __packed q80_rcv_cntxt_destroy_t;

typedef struct _q80_rcv_cntxt_destroy_rsp {
	uint16_t	opcode;
	uint16_t	regcnt_status;
} __packed q80_rcv_cntxt_destroy_rsp_t;


/*
 * Add Receive Rings
 */
typedef struct _q80_rq_add_rcv_rings {
	uint16_t		opcode;
	uint16_t		count_version;
	uint8_t			nrds_sets_rings;
	uint8_t			nsds_rings;
	uint16_t		cntxt_id;
	q80_rq_sds_ring_t	sds[MAX_RCNTXT_SDS_RINGS];
	q80_rq_rds_ring_t	rds[MAX_RDS_RING_SETS];
} __packed q80_rq_add_rcv_rings_t;

typedef struct _q80_rsp_add_rcv_rings {
	uint16_t		opcode;
	uint16_t		regcnt_status;
	uint8_t			nrds_sets_rings;
	uint8_t			nsds_rings;
	uint16_t		cntxt_id;
	uint32_t		sds_cons[MAX_RCNTXT_SDS_RINGS];
	q80_rsp_rds_ring_t	rds[MAX_RDS_RING_SETS];		
} __packed q80_rsp_add_rcv_rings_t;

/*
 * Map Status Ring to Receive Descriptor Set
 */

#define MAX_SDS_TO_RDS_MAP      16

typedef struct _q80_sds_rds_map_e {
        uint8_t sds_ring;
        uint8_t rsrvd0;
        uint8_t rds_ring;
        uint8_t rsrvd1;
} __packed q80_sds_rds_map_e_t;

typedef struct _q80_rq_map_sds_to_rds {
        uint16_t                opcode;
        uint16_t                count_version;
        uint16_t                cntxt_id;
        uint16_t                num_rings;
        q80_sds_rds_map_e_t     sds_rds[MAX_SDS_TO_RDS_MAP];
} __packed q80_rq_map_sds_to_rds_t;


typedef struct _q80_rsp_map_sds_to_rds {
        uint16_t                opcode;
        uint16_t                regcnt_status;
        uint16_t                cntxt_id;
        uint16_t                num_rings;
        q80_sds_rds_map_e_t     sds_rds[MAX_SDS_TO_RDS_MAP];
} __packed q80_rsp_map_sds_to_rds_t;


/*
 * Receive Descriptor corresponding to each entry in the receive ring
 */
typedef struct _q80_rcv_desc {
	uint16_t handle;
	uint16_t rsrvd;
	uint32_t buf_size; /* buffer size in bytes */
	uint64_t buf_addr; /* physical address of buffer */
} __packed q80_recv_desc_t;

/*
 * Status Descriptor corresponding to each entry in the Status ring
 */
typedef struct _q80_stat_desc {
	uint64_t data[2];
} __packed q80_stat_desc_t;

/*
 * definitions for data[0] field of Status Descriptor
 */
#define Q8_STAT_DESC_RSS_HASH(data)		(data & 0xFFFFFFFF)
#define Q8_STAT_DESC_TOTAL_LENGTH(data)		((data >> 32) & 0x3FFF)
#define Q8_STAT_DESC_TOTAL_LENGTH_SGL_RCV(data)	((data >> 32) & 0xFFFF)
#define Q8_STAT_DESC_HANDLE(data)		((data >> 48) & 0xFFFF)
/*
 * definitions for data[1] field of Status Descriptor
 */

#define Q8_STAT_DESC_OPCODE(data)		((data >> 42) & 0xF)
#define		Q8_STAT_DESC_OPCODE_RCV_PKT		0x01
#define		Q8_STAT_DESC_OPCODE_LRO_PKT		0x02
#define		Q8_STAT_DESC_OPCODE_SGL_LRO		0x04
#define		Q8_STAT_DESC_OPCODE_SGL_RCV		0x05
#define		Q8_STAT_DESC_OPCODE_CONT		0x06

/*
 * definitions for data[1] field of Status Descriptor for standard frames
 * status descriptor opcode equals 0x04
 */
#define Q8_STAT_DESC_STATUS(data)		((data >> 39) & 0x0007)
#define		Q8_STAT_DESC_STATUS_CHKSUM_NOT_DONE	0x00
#define		Q8_STAT_DESC_STATUS_NO_CHKSUM		0x01
#define		Q8_STAT_DESC_STATUS_CHKSUM_OK		0x02
#define		Q8_STAT_DESC_STATUS_CHKSUM_ERR		0x03

#define Q8_STAT_DESC_VLAN(data)			((data >> 47) & 1)
#define Q8_STAT_DESC_VLAN_ID(data)		((data >> 48) & 0xFFFF)

#define Q8_STAT_DESC_PROTOCOL(data)		((data >> 44) & 0x000F)
#define Q8_STAT_DESC_L2_OFFSET(data)		((data >> 48) & 0x001F)
#define Q8_STAT_DESC_COUNT(data)		((data >> 37) & 0x0007)

/*
 * definitions for data[0-1] fields of Status Descriptor for LRO
 * status descriptor opcode equals 0x04
 */

/* definitions for data[1] field */
#define Q8_LRO_STAT_DESC_SEQ_NUM(data)		(uint32_t)(data)

/*
 * definitions specific to opcode 0x04 data[1]
 */
#define	Q8_STAT_DESC_COUNT_SGL_LRO(data)	((data >> 13) & 0x0007)
#define Q8_SGL_LRO_STAT_L2_OFFSET(data)         ((data >> 16) & 0xFF)
#define Q8_SGL_LRO_STAT_L4_OFFSET(data)         ((data >> 24) & 0xFF)
#define Q8_SGL_LRO_STAT_TS(data)                ((data >> 40) & 0x1)
#define Q8_SGL_LRO_STAT_PUSH_BIT(data)          ((data >> 41) & 0x1)


/*
 * definitions specific to opcode 0x05 data[1]
 */
#define	Q8_STAT_DESC_COUNT_SGL_RCV(data)	((data >> 37) & 0x0003)

/*
 * definitions for opcode 0x06
 */
/* definitions for data[0] field */
#define Q8_SGL_STAT_DESC_HANDLE1(data)          (data & 0xFFFF)
#define Q8_SGL_STAT_DESC_HANDLE2(data)          ((data >> 16) & 0xFFFF)
#define Q8_SGL_STAT_DESC_HANDLE3(data)          ((data >> 32) & 0xFFFF)
#define Q8_SGL_STAT_DESC_HANDLE4(data)          ((data >> 48) & 0xFFFF)

/* definitions for data[1] field */
#define Q8_SGL_STAT_DESC_HANDLE5(data)          (data & 0xFFFF)
#define Q8_SGL_STAT_DESC_HANDLE6(data)          ((data >> 16) & 0xFFFF)
#define Q8_SGL_STAT_DESC_NUM_HANDLES(data)      ((data >> 32) & 0x7)
#define Q8_SGL_STAT_DESC_HANDLE7(data)          ((data >> 48) & 0xFFFF)

/** Driver Related Definitions Begin **/

#define TX_SMALL_PKT_SIZE	128 /* size in bytes of small packets */

/* The number of descriptors should be a power of 2 */
#define NUM_TX_DESCRIPTORS		1024
#define NUM_STATUS_DESCRIPTORS		1024


#define NUM_RX_DESCRIPTORS	2048

/*
 * structure describing various dma buffers
 */

typedef struct qla_dmabuf {
        volatile struct {
                uint32_t        tx_ring		:1,
                                rds_ring	:1,
                                sds_ring	:1,
				minidump	:1;
        } flags;

        qla_dma_t               tx_ring;
        qla_dma_t               rds_ring[MAX_RDS_RINGS];
        qla_dma_t               sds_ring[MAX_SDS_RINGS];
	qla_dma_t		minidump;
} qla_dmabuf_t;

typedef struct _qla_sds {
        q80_stat_desc_t *sds_ring_base; /* start of sds ring */
        uint32_t        sdsr_next; /* next entry in SDS ring to process */
        struct lro_ctrl lro;
        void            *rxb_free;
        uint32_t        rx_free;
        volatile uint32_t rcv_active;
	uint32_t	sds_consumer;
	uint64_t	intr_count;
	uint64_t	spurious_intr_count;
} qla_sds_t;

#define Q8_MAX_LRO_CONT_DESC    7
#define Q8_MAX_HANDLES_LRO      (1 + (Q8_MAX_LRO_CONT_DESC * 7))
#define Q8_MAX_HANDLES_NON_LRO  8

typedef struct _qla_sgl_rcv {
        uint16_t        pkt_length;
        uint16_t        num_handles;
        uint16_t        chksum_status;
        uint32_t        rss_hash;
        uint16_t        rss_hash_flags;
        uint16_t        vlan_tag;
        uint16_t        handle[Q8_MAX_HANDLES_NON_LRO];
} qla_sgl_rcv_t;

typedef struct _qla_sgl_lro {
        uint16_t        flags;
#define Q8_LRO_COMP_TS          0x1
#define Q8_LRO_COMP_PUSH_BIT    0x2
        uint16_t        l2_offset;
        uint16_t        l4_offset;

        uint16_t        payload_length;
        uint16_t        num_handles;
        uint32_t        rss_hash;
        uint16_t        rss_hash_flags;
        uint16_t        vlan_tag;
        uint16_t        handle[Q8_MAX_HANDLES_LRO];
} qla_sgl_lro_t;

typedef union {
        qla_sgl_rcv_t   rcv;
        qla_sgl_lro_t   lro;
} qla_sgl_comp_t;

#define QL_FRAME_HDR_SIZE (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN +\
		sizeof (struct ip6_hdr) + sizeof (struct tcphdr) + 16)

typedef struct _qla_hw_tx_cntxt {
	q80_tx_cmd_t    *tx_ring_base;
	bus_addr_t	tx_ring_paddr;

	volatile uint32_t *tx_cons; /* tx consumer shadow reg */
	bus_addr_t      tx_cons_paddr;

	volatile uint32_t txr_free; /* # of free entries in tx ring */
	volatile uint32_t txr_next; /* # next available tx ring entry */
	volatile uint32_t txr_comp; /* index of last tx entry completed */

	uint32_t        tx_prod_reg;
	uint16_t	tx_cntxt_id;

} qla_hw_tx_cntxt_t;

typedef struct _qla_mcast {
	uint16_t	rsrvd;
	uint8_t		addr[ETHER_ADDR_LEN];
} __packed qla_mcast_t; 

typedef struct _qla_rdesc {
        volatile uint32_t prod_std;
        volatile uint32_t prod_jumbo;
        volatile uint32_t rx_next; /* next standard rcv ring to arm fw */
        volatile int32_t  rx_in; /* next standard rcv ring to add mbufs */
	uint64_t count;
	uint64_t lro_pkt_count;
	uint64_t lro_bytes;
} qla_rdesc_t;

typedef struct _qla_flash_desc_table {
	uint32_t	flash_valid;
	uint16_t	flash_ver;
	uint16_t	flash_len;
	uint16_t	flash_cksum;
	uint16_t	flash_unused;
	uint8_t		flash_model[16];
	uint16_t	flash_manuf;
	uint16_t	flash_id;
	uint8_t		flash_flag;
	uint8_t		erase_cmd;
	uint8_t		alt_erase_cmd;
	uint8_t		write_enable_cmd;
	uint8_t		write_enable_bits;
	uint8_t		write_statusreg_cmd;
	uint8_t		unprotected_sec_cmd;
	uint8_t		read_manuf_cmd;
	uint32_t	block_size;
	uint32_t	alt_block_size;
	uint32_t	flash_size;
	uint32_t	write_enable_data;
	uint8_t		readid_addr_len;
	uint8_t		write_disable_bits;
	uint8_t		read_dev_id_len;
	uint8_t		chip_erase_cmd;
	uint16_t	read_timeo;
	uint8_t		protected_sec_cmd;
	uint8_t		resvd[65];
} __packed qla_flash_desc_table_t;

/*
 * struct for storing hardware specific information for a given interface
 */
typedef struct _qla_hw {
	struct {
		uint32_t
			unicast_mac	:1,
			bcast_mac	:1,
			init_tx_cnxt	:1,
			init_rx_cnxt	:1,
			init_intr_cnxt	:1,
			fdt_valid	:1;
	} flags;


	volatile uint16_t	link_speed;
	volatile uint16_t	cable_length;
	volatile uint32_t	cable_oui;
	volatile uint8_t	link_up;
	volatile uint8_t	module_type;
	volatile uint8_t	link_faults;
	volatile uint8_t	loopback_mode;
	volatile uint8_t	fduplex;
	volatile uint8_t	autoneg;

	volatile uint8_t	mac_rcv_mode;

	volatile uint32_t	max_mtu;

	uint8_t		mac_addr[ETHER_ADDR_LEN];

	uint32_t	num_sds_rings;
	uint32_t	num_rds_rings;
	uint32_t	num_tx_rings;

        qla_dmabuf_t	dma_buf;
	
	/* Transmit Side */

	qla_hw_tx_cntxt_t tx_cntxt[NUM_TX_RINGS];

	/* Receive Side */

	uint16_t	rcv_cntxt_id;

	uint32_t	mbx_intr_mask_offset;

	uint16_t	intr_id[MAX_SDS_RINGS];
	uint32_t	intr_src[MAX_SDS_RINGS];

	qla_sds_t	sds[MAX_SDS_RINGS]; 
	uint32_t	mbox[Q8_NUM_MBOX];
	qla_rdesc_t	rds[MAX_RDS_RINGS];		

	uint32_t	rds_pidx_thres;
	uint32_t	sds_cidx_thres;

	uint32_t	rcv_intr_coalesce;
	uint32_t	xmt_intr_coalesce;

	/* Immediate Completion */
	volatile uint32_t imd_compl;
	volatile uint32_t aen_mb0;
	volatile uint32_t aen_mb1;
	volatile uint32_t aen_mb2;
	volatile uint32_t aen_mb3;
	volatile uint32_t aen_mb4;

	/* multicast address list */
	uint32_t	nmcast;
	qla_mcast_t	mcast[Q8_MAX_NUM_MULTICAST_ADDRS];
	uint8_t		mac_addr_arr[(Q8_MAX_MAC_ADDRS * ETHER_ADDR_LEN)];

	/* reset sequence */
#define Q8_MAX_RESET_SEQ_IDX	16
	uint32_t	rst_seq[Q8_MAX_RESET_SEQ_IDX];
	uint32_t	rst_seq_idx;

	/* heart beat register value */
	uint32_t	hbeat_value;
	uint32_t	health_count;
	uint32_t	hbeat_failure;

	uint32_t	max_tx_segs;
	uint32_t	min_lro_pkt_size;
	
	uint32_t        enable_hw_lro;
	uint32_t        enable_soft_lro;
	uint32_t        enable_9kb;

	uint32_t	user_pri_nic;
	uint32_t	user_pri_iscsi;

	/* Flash Descriptor Table */
	qla_flash_desc_table_t fdt;

	/* stats */
	q80_mac_stats_t mac;
	q80_rcv_stats_t rcv;
	q80_xmt_stats_t xmt[NUM_TX_RINGS];

	/* Minidump Related */
	uint32_t	mdump_init;
	uint32_t	mdump_done;
	uint32_t	mdump_active;
	uint32_t	mdump_capture_mask;
	uint32_t	mdump_start_seq_index;
	void		*mdump_buffer;
	uint32_t	mdump_buffer_size;
	void		*mdump_template;
	uint32_t	mdump_template_size;
	uint64_t	mdump_usec_ts;

#define Q8_MBX_COMP_MSECS	(19)
	uint64_t	mbx_comp_msecs[Q8_MBX_COMP_MSECS];
	/* driver state related */
	void		*drvr_state;

	/* slow path trace */
	uint32_t	sp_log_stop_events;
#define Q8_SP_LOG_STOP_HBEAT_FAILURE		0x001
#define Q8_SP_LOG_STOP_TEMP_FAILURE		0x002
#define Q8_SP_LOG_STOP_HW_INIT_FAILURE		0x004
#define Q8_SP_LOG_STOP_IF_START_FAILURE		0x008
#define Q8_SP_LOG_STOP_ERR_RECOVERY_FAILURE	0x010

	uint32_t	sp_log_stop;
	uint32_t	sp_log_index;
	uint32_t	sp_log_num_entries;
	void		*sp_log;
} qla_hw_t;

#define QL_UPDATE_RDS_PRODUCER_INDEX(ha, prod_reg, val) \
		bus_write_4((ha->pci_reg), prod_reg, val);

#define QL_UPDATE_TX_PRODUCER_INDEX(ha, val, i) \
		WRITE_REG32(ha, ha->hw.tx_cntxt[i].tx_prod_reg, val)

#define QL_UPDATE_SDS_CONSUMER_INDEX(ha, i, val) \
	bus_write_4((ha->pci_reg), (ha->hw.sds[i].sds_consumer), val);

#define QL_ENABLE_INTERRUPTS(ha, i) \
		bus_write_4((ha->pci_reg), (ha->hw.intr_src[i]), 0);

#define QL_BUFFER_ALIGN                16


/*
 * Flash Configuration 
 */
#define Q8_BOARD_CONFIG_OFFSET		0x370000
#define Q8_BOARD_CONFIG_LENGTH		0x2000

#define Q8_BOARD_CONFIG_MAC0_LO		0x400

#define Q8_FDT_LOCK_MAGIC_ID		0x00FD00FD
#define Q8_FDT_FLASH_ADDR_VAL		0xFD009F
#define Q8_FDT_FLASH_CTRL_VAL		0x3F
#define Q8_FDT_MASK_VAL			0xFF

#define Q8_WR_ENABLE_FL_ADDR		0xFD0100
#define Q8_WR_ENABLE_FL_CTRL		0x5

#define Q8_ERASE_LOCK_MAGIC_ID		0x00EF00EF
#define Q8_ERASE_FL_ADDR_MASK		0xFD0300
#define Q8_ERASE_FL_CTRL_MASK		0x3D

#define Q8_WR_FL_LOCK_MAGIC_ID		0xABCDABCD
#define Q8_WR_FL_ADDR_MASK		0x800000
#define Q8_WR_FL_CTRL_MASK		0x3D

#define QL_FDT_OFFSET			0x3F0000
#define Q8_FLASH_SECTOR_SIZE		0x10000

/*
 * Off Chip Memory Access
 */

typedef struct _q80_offchip_mem_val {
        uint32_t data_lo;
        uint32_t data_hi;
        uint32_t data_ulo;
        uint32_t data_uhi;
} q80_offchip_mem_val_t;

#endif /* #ifndef _QL_HW_H_ */
