/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
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
 *
 */

/****************************************************************************
 *
 * Name:        mcp_public.h
 *
 * Description: MCP public data
 *
 * Created:     13/01/2013 yanivr
 *
 ****************************************************************************/

#ifndef MCP_PUBLIC_H
#define MCP_PUBLIC_H

#define VF_MAX_STATIC 192	/* In case of AH */

#define MCP_GLOB_PATH_MAX	2
#define MCP_PORT_MAX		2	/* Global */
#define MCP_GLOB_PORT_MAX	4	/* Global */
#define MCP_GLOB_FUNC_MAX	16	/* Global */

typedef u32 offsize_t;      /* In DWORDS !!! */
/* Offset from the beginning of the MCP scratchpad */
#define OFFSIZE_OFFSET_OFFSET	0
#define OFFSIZE_OFFSET_MASK	0x0000ffff
/* Size of specific element (not the whole array if any) */
#define OFFSIZE_SIZE_OFFSET	16
#define OFFSIZE_SIZE_MASK	0xffff0000

/* SECTION_OFFSET is calculating the offset in bytes out of offsize */
#define SECTION_OFFSET(_offsize)	((((_offsize & OFFSIZE_OFFSET_MASK) >> OFFSIZE_OFFSET_OFFSET) << 2))

/* SECTION_SIZE is calculating the size in bytes out of offsize */
#define SECTION_SIZE(_offsize)		(((_offsize & OFFSIZE_SIZE_MASK) >> OFFSIZE_SIZE_OFFSET) << 2)

/* SECTION_ADDR returns the GRC addr of a section, given offsize and index within section */
#define SECTION_ADDR(_offsize, idx) 	(MCP_REG_SCRATCH + SECTION_OFFSET(_offsize) + (SECTION_SIZE(_offsize) * idx))

/* SECTION_OFFSIZE_ADDR returns the GRC addr to the offsize address. Use offsetof, since the OFFSETUP collide with the firmware definition */
#define SECTION_OFFSIZE_ADDR(_pub_base, _section) (_pub_base + offsetof(struct mcp_public_data, sections[_section]))
/* PHY configuration */
struct eth_phy_cfg {
	u32 speed;      /* 0 = autoneg, 1000/10000/20000/25000/40000/50000/100000 */
#define ETH_SPEED_AUTONEG   0
#define ETH_SPEED_SMARTLINQ  0x8 /* deprecated - use link_modes field instead */

	u32 pause;      /* bitmask */
#define ETH_PAUSE_NONE		0x0
#define ETH_PAUSE_AUTONEG	0x1
#define ETH_PAUSE_RX		0x2
#define ETH_PAUSE_TX		0x4

	u32 adv_speed;      /* Default should be the speed_cap_mask */
	u32 loopback_mode;
#define ETH_LOOPBACK_NONE		 (0)
#define ETH_LOOPBACK_INT_PHY		 (1) /* Serdes loopback. In AH, it refers to Near End */
#define ETH_LOOPBACK_EXT_PHY		 (2) /* External PHY Loopback */
#define ETH_LOOPBACK_EXT		 (3) /* External Loopback (Require loopback plug) */
#define ETH_LOOPBACK_MAC		 (4) /* MAC Loopback - not supported */
#define ETH_LOOPBACK_CNIG_AH_ONLY_0123	 (5) /* Port to itself */
#define ETH_LOOPBACK_CNIG_AH_ONLY_2301	 (6) /* Port to Port */
#define ETH_LOOPBACK_PCS_AH_ONLY	 (7) /* PCS loopback (TX to RX) */
#define ETH_LOOPBACK_REVERSE_MAC_AH_ONLY (8) /* Loop RX packet from PCS to TX */
#define ETH_LOOPBACK_INT_PHY_FEA_AH_ONLY (9) /* Remote Serdes Loopback (RX to TX) */

	u32 eee_cfg;
#define EEE_CFG_EEE_ENABLED	(1<<0)	/* EEE is enabled (configuration). Refer to eee_status->active for negotiated status */
#define EEE_CFG_TX_LPI		(1<<1)
#define EEE_CFG_ADV_SPEED_1G	(1<<2)
#define EEE_CFG_ADV_SPEED_10G	(1<<3)
#define EEE_TX_TIMER_USEC_MASK	(0xfffffff0)
#define EEE_TX_TIMER_USEC_OFFSET	4
#define EEE_TX_TIMER_USEC_BALANCED_TIME		(0xa00)
#define EEE_TX_TIMER_USEC_AGGRESSIVE_TIME	(0x100)
#define EEE_TX_TIMER_USEC_LATENCY_TIME		(0x6000)

	u32 link_modes; /* Additional link modes */
#define LINK_MODE_SMARTLINQ_ENABLE		0x1  /* XXX Deprecate */
};

struct port_mf_cfg {

	u32 dynamic_cfg;    /* device control channel */
#define PORT_MF_CFG_OV_TAG_MASK              0x0000ffff
#define PORT_MF_CFG_OV_TAG_OFFSET             0
#define PORT_MF_CFG_OV_TAG_DEFAULT         PORT_MF_CFG_OV_TAG_MASK

	u32 reserved[1];
};

/* DO NOT add new fields in the middle
 * MUST be synced with struct pmm_stats_map
 */
struct eth_stats {
	u64 r64;        /* 0x00 (Offset 0x00 ) RX 64-byte frame counter*/
	u64 r127;       /* 0x01 (Offset 0x08 ) RX 65 to 127 byte frame counter*/
	u64 r255;       /* 0x02 (Offset 0x10 ) RX 128 to 255 byte frame counter*/
	u64 r511;       /* 0x03 (Offset 0x18 ) RX 256 to 511 byte frame counter*/
	u64 r1023;      /* 0x04 (Offset 0x20 ) RX 512 to 1023 byte frame counter*/
	u64 r1518;      /* 0x05 (Offset 0x28 ) RX 1024 to 1518 byte frame counter */
	union {
		struct { /* bb */
			u64 r1522;      /* 0x06 (Offset 0x30 ) RX 1519 to 1522 byte VLAN-tagged frame counter */
			u64 r2047;      /* 0x07 (Offset 0x38 ) RX 1519 to 2047 byte frame counter*/
			u64 r4095;      /* 0x08 (Offset 0x40 ) RX 2048 to 4095 byte frame counter*/
			u64 r9216;      /* 0x09 (Offset 0x48 ) RX 4096 to 9216 byte frame counter*/
			u64 r16383;     /* 0x0A (Offset 0x50 ) RX 9217 to 16383 byte frame counter */
			
		} bb0;
		struct { /* ah */
			u64 unused1;     
			u64 r1519_to_max; /* 0x07 (Offset 0x38 ) RX 1519 to max byte frame counter*/
			u64 unused2;     
			u64 unused3;     
			u64 unused4;
		} ah0;
	} u0;
	u64 rfcs;       /* 0x0F (Offset 0x58 ) RX FCS error frame counter*/
	u64 rxcf;       /* 0x10 (Offset 0x60 ) RX control frame counter*/
	u64 rxpf;       /* 0x11 (Offset 0x68 ) RX pause frame counter*/
	u64 rxpp;       /* 0x12 (Offset 0x70 ) RX PFC frame counter*/
	u64 raln;       /* 0x16 (Offset 0x78 ) RX alignment error counter*/
	u64 rfcr;       /* 0x19 (Offset 0x80 ) RX false carrier counter */
	u64 rovr;       /* 0x1A (Offset 0x88 ) RX oversized frame counter*/
	u64 rjbr;       /* 0x1B (Offset 0x90 ) RX jabber frame counter */
	u64 rund;       /* 0x34 (Offset 0x98 ) RX undersized frame counter */
	u64 rfrg;       /* 0x35 (Offset 0xa0 ) RX fragment counter */
	u64 t64;        /* 0x40 (Offset 0xa8 ) TX 64-byte frame counter */
	u64 t127;       /* 0x41 (Offset 0xb0 ) TX 65 to 127 byte frame counter */
	u64 t255;       /* 0x42 (Offset 0xb8 ) TX 128 to 255 byte frame counter*/
	u64 t511;       /* 0x43 (Offset 0xc0 ) TX 256 to 511 byte frame counter*/
	u64 t1023;      /* 0x44 (Offset 0xc8 ) TX 512 to 1023 byte frame counter*/
	u64 t1518;      /* 0x45 (Offset 0xd0 ) TX 1024 to 1518 byte frame counter */
	union {
		struct { /* bb */
			u64 t2047;      /* 0x47 (Offset 0xd8 ) TX 1519 to 2047 byte frame counter */
			u64 t4095;      /* 0x48 (Offset 0xe0 ) TX 2048 to 4095 byte frame counter */
			u64 t9216;      /* 0x49 (Offset 0xe8 ) TX 4096 to 9216 byte frame counter */
			u64 t16383;     /* 0x4A (Offset 0xf0 ) TX 9217 to 16383 byte frame counter */
		} bb1;
		struct { /* ah */
			u64 t1519_to_max; /* 0x47 (Offset 0xd8 ) TX 1519 to max byte frame counter */
			u64 unused6;
			u64 unused7;
			u64 unused8;
		} ah1;
	} u1;
	u64 txpf;       /* 0x50 (Offset 0xf8 ) TX pause frame counter */
	u64 txpp;       /* 0x51 (Offset 0x100) TX PFC frame counter */
	union {
		struct { /* bb */
			u64 tlpiec;     /* 0x6C (Offset 0x108) Transmit Logical Type LLFC message counter */
			u64 tncl;       /* 0x6E (Offset 0x110) Transmit Total Collision Counter */
		} bb2;
		struct { /* ah */
			u64 unused9;    
			u64 unused10;    
		} ah2;
	} u2;
	u64 rbyte;      /* 0x3d (Offset 0x118) RX byte counter */
	u64 rxuca;      /* 0x0c (Offset 0x120) RX UC frame counter */
	u64 rxmca;      /* 0x0d (Offset 0x128) RX MC frame counter */
	u64 rxbca;      /* 0x0e (Offset 0x130) RX BC frame counter */
	u64 rxpok;      /* 0x22 (Offset 0x138) RX good frame (good CRC, not oversized, no ERROR) */
	u64 tbyte;      /* 0x6f (Offset 0x140) TX byte counter */
	u64 txuca;      /* 0x4d (Offset 0x148) TX UC frame counter */
	u64 txmca;      /* 0x4e (Offset 0x150) TX MC frame counter */
	u64 txbca;      /* 0x4f (Offset 0x158) TX BC frame counter */
	u64 txcf;       /* 0x54 (Offset 0x160) TX control frame counter */
	/* HSI - Cannot add more stats to this struct. If needed, then need to open new struct */
};

struct brb_stats {
	u64 brb_truncate[8];
	u64 brb_discard[8];
};

struct port_stats {
	struct brb_stats brb;
	struct eth_stats eth;
};

/*-----+-----------------------------------------------------------------------------
 * Chip | Number and       | Ports in| Ports in|2 PHY-s |# of ports|# of engines
 *      | rate of physical | team #1 | team #2 |are used|per path  | (paths) enabled
 *      | ports            |         |         |        |          |
 *======+==================+=========+=========+========+==========+=================
 * BB   | 1x100G           | This is special mode, where there are actually 2 HW func
 * BB   | 2x10/20Gbps      | 0,1     | NA      |  No    | 1        | 1
 * BB   | 2x40 Gbps        | 0,1     | NA      |  Yes   | 1        | 1
 * BB   | 2x50Gbps         | 0,1     | NA      |  No    | 1        | 1
 * BB   | 4x10Gbps         | 0,2     | 1,3     |  No    | 1/2      | 1,2 (2 is optional)
 * BB   | 4x10Gbps         | 0,1     | 2,3     |  No    | 1/2      | 1,2 (2 is optional)
 * BB   | 4x10Gbps         | 0,3     | 1,2     |  No    | 1/2      | 1,2 (2 is optional)
 * BB   | 4x10Gbps         | 0,1,2,3 | NA      |  No    | 1        | 1
 * AH   | 2x10/20Gbps      | 0,1     | NA      |  NA    | 1        | NA
 * AH   | 4x10Gbps         | 0,1     | 2,3     |  NA    | 2        | NA
 * AH   | 4x10Gbps         | 0,2     | 1,3     |  NA    | 2        | NA
 * AH   | 4x10Gbps         | 0,3     | 1,2     |  NA    | 2        | NA
 * AH   | 4x10Gbps         | 0,1,2,3 | NA      |  NA    | 1        | NA
 *======+==================+=========+=========+========+==========+===================
 */

#define CMT_TEAM0 0
#define CMT_TEAM1 1
#define CMT_TEAM_MAX 2

struct couple_mode_teaming {
	u8 port_cmt[MCP_GLOB_PORT_MAX];
#define PORT_CMT_IN_TEAM            (1<<0)

#define PORT_CMT_PORT_ROLE          (1<<1)
#define PORT_CMT_PORT_INACTIVE      (0<<1)
#define PORT_CMT_PORT_ACTIVE        (1<<1)

#define PORT_CMT_TEAM_MASK          (1<<2)
#define PORT_CMT_TEAM0              (0<<2)
#define PORT_CMT_TEAM1              (1<<2)
};

/**************************************
 *     LLDP and DCBX HSI structures
 **************************************/
#define LLDP_CHASSIS_ID_STAT_LEN 	4
#define LLDP_PORT_ID_STAT_LEN 		4
#define DCBX_MAX_APP_PROTOCOL		32
#define MAX_SYSTEM_LLDP_TLV_DATA    	32  /* In dwords. 128 in bytes*/
#define MAX_TLV_BUFFER			128 /* In dwords. 512 in bytes*/
typedef enum _lldp_agent_e {
	LLDP_NEAREST_BRIDGE = 0,
	LLDP_NEAREST_NON_TPMR_BRIDGE,
	LLDP_NEAREST_CUSTOMER_BRIDGE,
	LLDP_MAX_LLDP_AGENTS
} lldp_agent_e;

struct lldp_config_params_s {
	u32 config;
#define LLDP_CONFIG_TX_INTERVAL_MASK        0x000000ff
#define LLDP_CONFIG_TX_INTERVAL_OFFSET       0
#define LLDP_CONFIG_HOLD_MASK               0x00000f00
#define LLDP_CONFIG_HOLD_OFFSET              8
#define LLDP_CONFIG_MAX_CREDIT_MASK         0x0000f000
#define LLDP_CONFIG_MAX_CREDIT_OFFSET        12
#define LLDP_CONFIG_ENABLE_RX_MASK          0x40000000
#define LLDP_CONFIG_ENABLE_RX_OFFSET         30
#define LLDP_CONFIG_ENABLE_TX_MASK          0x80000000
#define LLDP_CONFIG_ENABLE_TX_OFFSET         31
	/* Holds local Chassis ID TLV header, subtype and 9B of payload.
	   If firtst byte is 0, then we will use default chassis ID */
	u32 local_chassis_id[LLDP_CHASSIS_ID_STAT_LEN];
	/* Holds local Port ID TLV header, subtype and 9B of payload.
	   If firtst byte is 0, then we will use default port ID */
	u32 local_port_id[LLDP_PORT_ID_STAT_LEN];
};

struct lldp_status_params_s {
	u32 prefix_seq_num;
	u32 status; /* TBD */
	/* Holds remote Chassis ID TLV header, subtype and 9B of payload. */
	u32 peer_chassis_id[LLDP_CHASSIS_ID_STAT_LEN];
	/* Holds remote Port ID TLV header, subtype and 9B of payload. */
	u32 peer_port_id[LLDP_PORT_ID_STAT_LEN];
	u32 suffix_seq_num;
};

struct dcbx_ets_feature {
	u32 flags;
#define DCBX_ETS_ENABLED_MASK                   0x00000001
#define DCBX_ETS_ENABLED_OFFSET                  0
#define DCBX_ETS_WILLING_MASK                   0x00000002
#define DCBX_ETS_WILLING_OFFSET                  1
#define DCBX_ETS_ERROR_MASK                     0x00000004
#define DCBX_ETS_ERROR_OFFSET                    2
#define DCBX_ETS_CBS_MASK                       0x00000008
#define DCBX_ETS_CBS_OFFSET                      3
#define DCBX_ETS_MAX_TCS_MASK                   0x000000f0
#define DCBX_ETS_MAX_TCS_OFFSET                  4
#define DCBX_OOO_TC_MASK			0x00000f00
#define DCBX_OOO_TC_OFFSET                 	8
	/* Entries in tc table are orginized that the left most is pri 0, right most is prio 7 */
	u32  pri_tc_tbl[1];
/* Fixed TCP OOO TC usage is deprecated and used only for driver backward compatibility */
#define DCBX_TCP_OOO_TC				(4)
#define DCBX_TCP_OOO_K2_4PORT_TC		(3)

#define NIG_ETS_ISCSI_OOO_CLIENT_OFFSET		(DCBX_TCP_OOO_TC + 1)
#define DCBX_CEE_STRICT_PRIORITY		0xf
	/* Entries in tc table are orginized that the left most is pri 0, right most is prio 7 */
	u32  tc_bw_tbl[2];
	/* Entries in tc table are orginized that the left most is pri 0, right most is prio 7 */
	u32  tc_tsa_tbl[2];
#define DCBX_ETS_TSA_STRICT			0
#define DCBX_ETS_TSA_CBS			1
#define DCBX_ETS_TSA_ETS			2
};

struct dcbx_app_priority_entry {
	u32 entry;
#define DCBX_APP_PRI_MAP_MASK       0x000000ff
#define DCBX_APP_PRI_MAP_OFFSET      0
#define DCBX_APP_PRI_0              0x01
#define DCBX_APP_PRI_1              0x02
#define DCBX_APP_PRI_2              0x04
#define DCBX_APP_PRI_3              0x08
#define DCBX_APP_PRI_4              0x10
#define DCBX_APP_PRI_5              0x20
#define DCBX_APP_PRI_6              0x40
#define DCBX_APP_PRI_7              0x80
#define DCBX_APP_SF_MASK            0x00000300
#define DCBX_APP_SF_OFFSET           8
#define DCBX_APP_SF_ETHTYPE         0
#define DCBX_APP_SF_PORT            1
#define DCBX_APP_SF_IEEE_MASK       0x0000f000
#define DCBX_APP_SF_IEEE_OFFSET      12
#define DCBX_APP_SF_IEEE_RESERVED   0
#define DCBX_APP_SF_IEEE_ETHTYPE    1
#define DCBX_APP_SF_IEEE_TCP_PORT   2
#define DCBX_APP_SF_IEEE_UDP_PORT   3
#define DCBX_APP_SF_IEEE_TCP_UDP_PORT 4

#define DCBX_APP_PROTOCOL_ID_MASK   0xffff0000
#define DCBX_APP_PROTOCOL_ID_OFFSET  16
};


/* FW structure in BE */
struct dcbx_app_priority_feature {
	u32 flags;
#define DCBX_APP_ENABLED_MASK           0x00000001
#define DCBX_APP_ENABLED_OFFSET          0
#define DCBX_APP_WILLING_MASK           0x00000002
#define DCBX_APP_WILLING_OFFSET          1
#define DCBX_APP_ERROR_MASK             0x00000004
#define DCBX_APP_ERROR_OFFSET            2
	/* Not in use
	#define DCBX_APP_DEFAULT_PRI_MASK       0x00000f00
	#define DCBX_APP_DEFAULT_PRI_OFFSET      8
	*/
#define DCBX_APP_MAX_TCS_MASK           0x0000f000
#define DCBX_APP_MAX_TCS_OFFSET          12
#define DCBX_APP_NUM_ENTRIES_MASK       0x00ff0000
#define DCBX_APP_NUM_ENTRIES_OFFSET      16
	struct dcbx_app_priority_entry  app_pri_tbl[DCBX_MAX_APP_PROTOCOL];
};

/* FW structure in BE */
struct dcbx_features {
	/* PG feature */
	struct dcbx_ets_feature ets;
	/* PFC feature */
	u32 pfc;
#define DCBX_PFC_PRI_EN_BITMAP_MASK             0x000000ff
#define DCBX_PFC_PRI_EN_BITMAP_OFFSET            0
#define DCBX_PFC_PRI_EN_BITMAP_PRI_0            0x01
#define DCBX_PFC_PRI_EN_BITMAP_PRI_1            0x02
#define DCBX_PFC_PRI_EN_BITMAP_PRI_2            0x04
#define DCBX_PFC_PRI_EN_BITMAP_PRI_3            0x08
#define DCBX_PFC_PRI_EN_BITMAP_PRI_4            0x10
#define DCBX_PFC_PRI_EN_BITMAP_PRI_5            0x20
#define DCBX_PFC_PRI_EN_BITMAP_PRI_6            0x40
#define DCBX_PFC_PRI_EN_BITMAP_PRI_7            0x80

#define DCBX_PFC_FLAGS_MASK                     0x0000ff00
#define DCBX_PFC_FLAGS_OFFSET                    8
#define DCBX_PFC_CAPS_MASK                      0x00000f00
#define DCBX_PFC_CAPS_OFFSET                     8
#define DCBX_PFC_MBC_MASK                       0x00004000
#define DCBX_PFC_MBC_OFFSET                      14
#define DCBX_PFC_WILLING_MASK                   0x00008000
#define DCBX_PFC_WILLING_OFFSET                  15
#define DCBX_PFC_ENABLED_MASK                   0x00010000
#define DCBX_PFC_ENABLED_OFFSET                  16
#define DCBX_PFC_ERROR_MASK                     0x00020000
#define DCBX_PFC_ERROR_OFFSET                    17

	/* APP feature */
	struct dcbx_app_priority_feature app;
};

struct dcbx_local_params {
	u32 config;
#define DCBX_CONFIG_VERSION_MASK            0x00000007
#define DCBX_CONFIG_VERSION_OFFSET           0
#define DCBX_CONFIG_VERSION_DISABLED        0
#define DCBX_CONFIG_VERSION_IEEE            1
#define DCBX_CONFIG_VERSION_CEE             2
#define DCBX_CONFIG_VERSION_DYNAMIC         (DCBX_CONFIG_VERSION_IEEE | DCBX_CONFIG_VERSION_CEE)
#define DCBX_CONFIG_VERSION_STATIC          4

	u32 flags;
	struct dcbx_features features;
};

struct dcbx_mib {
	u32 prefix_seq_num;
	u32 flags;
	/*
	#define DCBX_CONFIG_VERSION_MASK            0x00000007
	#define DCBX_CONFIG_VERSION_OFFSET           0
	#define DCBX_CONFIG_VERSION_DISABLED        0
	#define DCBX_CONFIG_VERSION_IEEE            1
	#define DCBX_CONFIG_VERSION_CEE             2
	#define DCBX_CONFIG_VERSION_STATIC          4
	*/
	struct dcbx_features features;
	u32 suffix_seq_num;
};

struct lldp_system_tlvs_buffer_s {
	u32 flags;
#define LLDP_SYSTEM_TLV_VALID_MASK		0x1
#define LLDP_SYSTEM_TLV_VALID_OFFSET		0
/* This bit defines if system TLVs are instead of mandatory TLVS or in
 * addition to them. Set 1 for replacing mandatory TLVs
 */
#define LLDP_SYSTEM_TLV_MANDATORY_MASK		0x2
#define LLDP_SYSTEM_TLV_MANDATORY_OFFSET	1
#define LLDP_SYSTEM_TLV_LENGTH_MASK		0xffff0000
#define LLDP_SYSTEM_TLV_LENGTH_OFFSET		16	
	u32 data[MAX_SYSTEM_LLDP_TLV_DATA];
};

/* Since this struct is written by MFW and read by driver need to add
 * sequence guards (as in case of DCBX MIB)
 */
struct lldp_received_tlvs_s {
	u32 prefix_seq_num;
	u32 length;
	u32 tlvs_buffer[MAX_TLV_BUFFER];
	u32 suffix_seq_num;
};

struct dcb_dscp_map {
	u32 flags;
#define DCB_DSCP_ENABLE_MASK			0x1
#define DCB_DSCP_ENABLE_OFFSET			0
#define DCB_DSCP_ENABLE				1
	u32 dscp_pri_map[8];
	/* the map structure is the following:
	   each u32 is split into 4 bits chunks, each chunk holds priority for respective dscp
	   Lowest dscp is at lsb
			    31          28          24          20          16          12          8           4           0
	   dscp_pri_map[0]: | dscp7 pri | dscp6 pri | dscp5 pri | dscp4 pri | dscp3 pri | dscp2 pri | dscp1 pri | dscp0 pri |
	   dscp_pri_map[1]: | dscp15 pri| dscp14 pri| dscp13 pri| dscp12 pri| dscp11 pri| dscp10 pri| dscp9 pri | dscp8 pri |
	   etc.*/
};

struct mcp_val64 {
	u32 lo;
	u32 hi;
};

/* generic_idc_msg_t to be used for inter driver communication.
 * source_pf specifies the originating PF that sent messages to all target PFs
 * msg contains 64 bit value of the message - opaque to the MFW 
 */ 
struct generic_idc_msg_s {
	u32 source_pf;
	struct mcp_val64 msg;
};

/**************************************
 *     Attributes commands
 **************************************/

enum _attribute_commands_e {
	ATTRIBUTE_CMD_READ = 0,
	ATTRIBUTE_CMD_WRITE,
	ATTRIBUTE_CMD_READ_CLEAR,
	ATTRIBUTE_CMD_CLEAR,
	ATTRIBUTE_NUM_OF_COMMANDS
};

/**************************************/
/*                                    */
/*     P U B L I C      G L O B A L   */
/*                                    */
/**************************************/
struct public_global {
	u32 max_path;       /* 32bit is wasty, but this will be used often */
	u32 max_ports;      /* (Global) 32bit is wasty, but this will be used often */
#define MODE_1P	1		/* TBD - NEED TO THINK OF A BETTER NAME */
#define MODE_2P	2
#define MODE_3P	3
#define MODE_4P	4
	u32 debug_mb_offset;
	u32 phymod_dbg_mb_offset;
	struct couple_mode_teaming cmt;
	s32 internal_temperature; /* Temperature in Celcius (-255C / +255C), measured every second. */
	u32 mfw_ver;
	u32 running_bundle_id;
	s32 external_temperature;
	u32 mdump_reason;
#define MDUMP_REASON_INTERNAL_ERROR	(1 << 0)
#define MDUMP_REASON_EXTERNAL_TRIGGER	(1 << 1)
#define MDUMP_REASON_DUMP_AGED		(1 << 2)
	u32 ext_phy_upgrade_fw;
#define EXT_PHY_FW_UPGRADE_STATUS_MASK		(0x0000ffff)
#define EXT_PHY_FW_UPGRADE_STATUS_OFFSET		(0)
#define EXT_PHY_FW_UPGRADE_STATUS_IN_PROGRESS	(1)
#define EXT_PHY_FW_UPGRADE_STATUS_FAILED	(2)
#define EXT_PHY_FW_UPGRADE_STATUS_SUCCESS	(3)
#define EXT_PHY_FW_UPGRADE_TYPE_MASK		(0xffff0000)
#define EXT_PHY_FW_UPGRADE_TYPE_OFFSET		(16)

	u8 runtime_port_swap_map[MODE_4P];
	u32 data_ptr;
	u32 data_size;
};

/**************************************/
/*                                    */
/*     P U B L I C      P A T H       */
/*                                    */
/**************************************/

/****************************************************************************
 * Shared Memory 2 Region                                                   *
 ****************************************************************************/
/* The fw_flr_ack is actually built in the following way:                   */
/* 8 bit:  PF ack                                                           */
/* 128 bit: VF ack                                                           */
/* 8 bit:  ios_dis_ack                                                      */
/* In order to maintain endianity in the mailbox hsi, we want to keep using */
/* u32. The fw must have the VF right after the PF since this is how it     */
/* access arrays(it expects always the VF to reside after the PF, and that  */
/* makes the calculation much easier for it. )                              */
/* In order to answer both limitations, and keep the struct small, the code */
/* will abuse the structure defined here to achieve the actual partition    */
/* above                                                                    */
/****************************************************************************/
struct fw_flr_mb {
	u32 aggint;
	u32 opgen_addr;
	u32 accum_ack;      /* 0..15:PF, 16..207:VF, 256..271:IOV_DIS */
#define ACCUM_ACK_PF_BASE	0
#define ACCUM_ACK_PF_SHIFT	0

#define ACCUM_ACK_VF_BASE	8
#define ACCUM_ACK_VF_SHIFT	3

#define ACCUM_ACK_IOV_DIS_BASE	256
#define ACCUM_ACK_IOV_DIS_SHIFT	8

};

struct public_path {
	struct fw_flr_mb flr_mb;
	/*
	 * mcp_vf_disabled is set by the MCP to indicate the driver about VFs
	 * which were disabled/flred
	 */
	u32 mcp_vf_disabled[VF_MAX_STATIC / 32];    /* 0x003c */

	u32 process_kill; /* Reset on mcp reset, and incremented for eveny process kill event. */
#define PROCESS_KILL_COUNTER_MASK		0x0000ffff
#define PROCESS_KILL_COUNTER_OFFSET		0
#define PROCESS_KILL_GLOB_AEU_BIT_MASK		0xffff0000
#define PROCESS_KILL_GLOB_AEU_BIT_OFFSET		16
#define GLOBAL_AEU_BIT(aeu_reg_id, aeu_bit) (aeu_reg_id*32 + aeu_bit)
};

/**************************************/
/*                                    */
/*     P U B L I C      P O R T       */
/*                                    */
/**************************************/
#define FC_NPIV_WWPN_SIZE 8
#define FC_NPIV_WWNN_SIZE 8
struct dci_npiv_settings {
	u8 npiv_wwpn[FC_NPIV_WWPN_SIZE];
	u8 npiv_wwnn[FC_NPIV_WWNN_SIZE];
};

struct dci_fc_npiv_cfg {
	/* hdr used internally by the MFW */
	u32 hdr;
	u32 num_of_npiv;
};

#define MAX_NUMBER_NPIV 64
struct dci_fc_npiv_tbl {
	struct dci_fc_npiv_cfg fc_npiv_cfg;
	struct dci_npiv_settings settings[MAX_NUMBER_NPIV];
};

/****************************************************************************
 * Driver <-> FW Mailbox                                                    *
 ****************************************************************************/

struct public_port {
	u32 validity_map;   /* 0x0 (4*2 = 0x8) */

	/* validity bits */
#define MCP_VALIDITY_PCI_CFG                    0x00100000
#define MCP_VALIDITY_MB                         0x00200000
#define MCP_VALIDITY_DEV_INFO                   0x00400000
#define MCP_VALIDITY_RESERVED                   0x00000007

	/* One licensing bit should be set */
#define MCP_VALIDITY_LIC_KEY_IN_EFFECT_MASK     0x00000038	/* yaniv - tbd ? license */
#define MCP_VALIDITY_LIC_MANUF_KEY_IN_EFFECT    0x00000008
#define MCP_VALIDITY_LIC_UPGRADE_KEY_IN_EFFECT  0x00000010
#define MCP_VALIDITY_LIC_NO_KEY_IN_EFFECT       0x00000020

	/* Active MFW */
#define MCP_VALIDITY_ACTIVE_MFW_UNKNOWN         0x00000000
#define MCP_VALIDITY_ACTIVE_MFW_MASK            0x000001c0
#define MCP_VALIDITY_ACTIVE_MFW_NCSI            0x00000040
#define MCP_VALIDITY_ACTIVE_MFW_NONE            0x000001c0

	u32 link_status;
#define LINK_STATUS_LINK_UP				0x00000001
#define LINK_STATUS_SPEED_AND_DUPLEX_MASK		0x0000001e
#define LINK_STATUS_SPEED_AND_DUPLEX_1000THD		(1<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_1000TFD		(2<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_10G		(3<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_20G		(4<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_40G		(5<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_50G		(6<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_100G		(7<<1)
#define LINK_STATUS_SPEED_AND_DUPLEX_25G		(8<<1)
#define LINK_STATUS_AUTO_NEGOTIATE_ENABLED		0x00000020
#define LINK_STATUS_AUTO_NEGOTIATE_COMPLETE		0x00000040
#define LINK_STATUS_PARALLEL_DETECTION_USED		0x00000080
#define LINK_STATUS_PFC_ENABLED				0x00000100
#define LINK_STATUS_LINK_PARTNER_1000TFD_CAPABLE	0x00000200
#define LINK_STATUS_LINK_PARTNER_1000THD_CAPABLE	0x00000400
#define LINK_STATUS_LINK_PARTNER_10G_CAPABLE		0x00000800
#define LINK_STATUS_LINK_PARTNER_20G_CAPABLE		0x00001000
#define LINK_STATUS_LINK_PARTNER_40G_CAPABLE		0x00002000
#define LINK_STATUS_LINK_PARTNER_50G_CAPABLE		0x00004000
#define LINK_STATUS_LINK_PARTNER_100G_CAPABLE		0x00008000
#define LINK_STATUS_LINK_PARTNER_25G_CAPABLE		0x00010000
#define LINK_STATUS_LINK_PARTNER_FLOW_CONTROL_MASK	0x000C0000
#define LINK_STATUS_LINK_PARTNER_NOT_PAUSE_CAPABLE	(0<<18)
#define LINK_STATUS_LINK_PARTNER_SYMMETRIC_PAUSE	(1<<18)
#define LINK_STATUS_LINK_PARTNER_ASYMMETRIC_PAUSE	(2<<18)
#define LINK_STATUS_LINK_PARTNER_BOTH_PAUSE		(3<<18)
#define LINK_STATUS_SFP_TX_FAULT			0x00100000
#define LINK_STATUS_TX_FLOW_CONTROL_ENABLED		0x00200000
#define LINK_STATUS_RX_FLOW_CONTROL_ENABLED		0x00400000
#define LINK_STATUS_RX_SIGNAL_PRESENT			0x00800000
#define LINK_STATUS_MAC_LOCAL_FAULT			0x01000000
#define LINK_STATUS_MAC_REMOTE_FAULT			0x02000000
#define LINK_STATUS_UNSUPPORTED_SPD_REQ			0x04000000
#define LINK_STATUS_FEC_MODE_MASK			0x38000000
#define LINK_STATUS_FEC_MODE_NONE			(0<<27)
#define LINK_STATUS_FEC_MODE_FIRECODE_CL74		(1<<27)
#define LINK_STATUS_FEC_MODE_RS_CL91			(2<<27)
#define LINK_STATUS_EXT_PHY_LINK_UP			0x40000000

	u32 link_status1;
#define LP_PRESENCE_STATUS_OFFSET	0 
#define LP_PRESENCE_STATUS_MASK		0x3
#define LP_PRESENCE_UNKNOWN		0x0
#define LP_PRESENCE_PROBING		0x1
#define	LP_PRESENT			0x2
#define	LP_NOT_PRESENT			0x3

	u32 ext_phy_fw_version;
	u32 drv_phy_cfg_addr;   /* Points to struct eth_phy_cfg (For READ-ONLY) */

	u32 port_stx;

	u32 stat_nig_timer;

	struct port_mf_cfg port_mf_config;
	struct port_stats stats;

	u32 media_type;
#define	MEDIA_UNSPECIFIED		0x0
#define	MEDIA_SFPP_10G_FIBER	0x1	/* Use MEDIA_MODULE_FIBER instead */
#define	MEDIA_XFP_FIBER			0x2	/* Use MEDIA_MODULE_FIBER instead */
#define	MEDIA_DA_TWINAX			0x3
#define	MEDIA_BASE_T			0x4
#define MEDIA_SFP_1G_FIBER		0x5	/* Use MEDIA_MODULE_FIBER instead */
#define MEDIA_MODULE_FIBER		0x6
#define	MEDIA_KR				0xf0
#define	MEDIA_NOT_PRESENT		0xff

	u32 lfa_status;
#define LFA_LINK_FLAP_REASON_OFFSET		0
#define LFA_LINK_FLAP_REASON_MASK		0x000000ff
#define LFA_NO_REASON					(0<<0)
#define LFA_LINK_DOWN					(1<<0)
#define LFA_FORCE_INIT					(1<<1)
#define LFA_LOOPBACK_MISMATCH				(1<<2)
#define LFA_SPEED_MISMATCH				(1<<3)
#define LFA_FLOW_CTRL_MISMATCH				(1<<4)
#define LFA_ADV_SPEED_MISMATCH				(1<<5)
#define LFA_EEE_MISMATCH				(1<<6)
#define LFA_LINK_MODES_MISMATCH			(1<<7)
#define LINK_FLAP_AVOIDANCE_COUNT_OFFSET	8
#define LINK_FLAP_AVOIDANCE_COUNT_MASK		0x0000ff00
#define LINK_FLAP_COUNT_OFFSET			16
#define LINK_FLAP_COUNT_MASK			0x00ff0000

	u32 link_change_count;

	/* LLDP params */
	struct lldp_config_params_s lldp_config_params[LLDP_MAX_LLDP_AGENTS];  // offset: 536 bytes?
	struct lldp_status_params_s lldp_status_params[LLDP_MAX_LLDP_AGENTS];
	struct lldp_system_tlvs_buffer_s system_lldp_tlvs_buf;

	/* DCBX related MIB */
	struct dcbx_local_params local_admin_dcbx_mib;
	struct dcbx_mib remote_dcbx_mib;
	struct dcbx_mib operational_dcbx_mib;

	/* FC_NPIV table offset & size in NVRAM value of 0 means not present */
	u32 fc_npiv_nvram_tbl_addr;
#define NPIV_TBL_INVALID_ADDR			0xFFFFFFFF

	u32 fc_npiv_nvram_tbl_size;
	u32 transceiver_data;
#define ETH_TRANSCEIVER_STATE_MASK	      	0x000000FF
#define ETH_TRANSCEIVER_STATE_OFFSET	      	0x0
#define ETH_TRANSCEIVER_STATE_UNPLUGGED			0x00
#define ETH_TRANSCEIVER_STATE_PRESENT			0x01
#define ETH_TRANSCEIVER_STATE_VALID			0x03
#define ETH_TRANSCEIVER_STATE_UPDATING			0x08
#define ETH_TRANSCEIVER_TYPE_MASK		0x0000FF00
#define ETH_TRANSCEIVER_TYPE_OFFSET		0x8
#define ETH_TRANSCEIVER_TYPE_NONE			0x00
#define ETH_TRANSCEIVER_TYPE_UNKNOWN			0xFF
#define ETH_TRANSCEIVER_TYPE_1G_PCC			0x01 /* 1G Passive copper cable */
#define ETH_TRANSCEIVER_TYPE_1G_ACC			0x02 /* 1G Active copper cable  */
#define ETH_TRANSCEIVER_TYPE_1G_LX			0x03
#define ETH_TRANSCEIVER_TYPE_1G_SX			0x04
#define ETH_TRANSCEIVER_TYPE_10G_SR			0x05
#define ETH_TRANSCEIVER_TYPE_10G_LR			0x06
#define ETH_TRANSCEIVER_TYPE_10G_LRM			0x07
#define ETH_TRANSCEIVER_TYPE_10G_ER			0x08
#define ETH_TRANSCEIVER_TYPE_10G_PCC			0x09 /* 10G Passive copper cable */
#define ETH_TRANSCEIVER_TYPE_10G_ACC			0x0a /* 10G Active copper cable  */
#define ETH_TRANSCEIVER_TYPE_XLPPI			0x0b
#define ETH_TRANSCEIVER_TYPE_40G_LR4			0x0c
#define ETH_TRANSCEIVER_TYPE_40G_SR4			0x0d
#define ETH_TRANSCEIVER_TYPE_40G_CR4			0x0e
#define ETH_TRANSCEIVER_TYPE_100G_AOC			0x0f /* Active optical cable */
#define ETH_TRANSCEIVER_TYPE_100G_SR4			0x10
#define ETH_TRANSCEIVER_TYPE_100G_LR4			0x11
#define ETH_TRANSCEIVER_TYPE_100G_ER4			0x12
#define ETH_TRANSCEIVER_TYPE_100G_ACC			0x13 /* Active copper cable */
#define ETH_TRANSCEIVER_TYPE_100G_CR4			0x14
#define ETH_TRANSCEIVER_TYPE_4x10G_SR			0x15
#define ETH_TRANSCEIVER_TYPE_25G_CA_N			0x16 /* 25G Passive copper cable - short */
#define ETH_TRANSCEIVER_TYPE_25G_ACC_S			0x17 /* 25G Active copper cable  - short */
#define ETH_TRANSCEIVER_TYPE_25G_CA_S			0x18 /* 25G Passive copper cable - medium */
#define ETH_TRANSCEIVER_TYPE_25G_ACC_M			0x19 /* 25G Active copper cable  - medium */
#define ETH_TRANSCEIVER_TYPE_25G_CA_L			0x1a /* 25G Passive copper cable - long */
#define ETH_TRANSCEIVER_TYPE_25G_ACC_L			0x1b /* 25G Active copper cable  - long */
#define ETH_TRANSCEIVER_TYPE_25G_SR			0x1c
#define ETH_TRANSCEIVER_TYPE_25G_LR			0x1d
#define ETH_TRANSCEIVER_TYPE_25G_AOC			0x1e
#define ETH_TRANSCEIVER_TYPE_4x10G			0x1f
#define ETH_TRANSCEIVER_TYPE_4x25G_CR			0x20
#define ETH_TRANSCEIVER_TYPE_1000BASET			0x21
#define ETH_TRANSCEIVER_TYPE_10G_BASET			0x22
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_SR	0x30
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_CR	0x31
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_LR	0x32
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_SR	0x33
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_CR	0x34
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_LR	0x35
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_AOC	0x36
	u32 wol_info;
	u32 wol_pkt_len;
	u32 wol_pkt_details;
	struct dcb_dscp_map dcb_dscp_map;

	u32 eee_status;
#define EEE_ACTIVE_BIT		(1<<0)		/* Set when EEE negotiation is complete. */

#define EEE_LD_ADV_STATUS_MASK	0x000000f0	/* Shows the Local Device EEE capabilities */
#define EEE_LD_ADV_STATUS_OFFSET	4
	#define EEE_1G_ADV	(1<<1)
	#define EEE_10G_ADV	(1<<2)
#define	EEE_LP_ADV_STATUS_MASK	0x00000f00      /* Same values as in EEE_LD_ADV, but for Link Parter */
#define EEE_LP_ADV_STATUS_OFFSET	8

#define EEE_SUPPORTED_SPEED_MASK	0x0000f000	/* Supported speeds for EEE */
#define EEE_SUPPORTED_SPEED_OFFSET	12
	#define EEE_1G_SUPPORTED	(1 << 1)
	#define EEE_10G_SUPPORTED	(1 << 2)

	u32 eee_remote;	/* Used for EEE in LLDP */
#define EEE_REMOTE_TW_TX_MASK	0x0000ffff
#define EEE_REMOTE_TW_TX_OFFSET	0
#define EEE_REMOTE_TW_RX_MASK	0xffff0000
#define EEE_REMOTE_TW_RX_OFFSET	16

	u32 module_info;
#define ETH_TRANSCEIVER_MONITORING_TYPE_MASK		0x000000FF
#define ETH_TRANSCEIVER_MONITORING_TYPE_OFFSET		0
#define ETH_TRANSCEIVER_ADDR_CHNG_REQUIRED		(1 << 2)
#define ETH_TRANSCEIVER_RCV_PWR_MEASURE_TYPE		(1 << 3)
#define ETH_TRANSCEIVER_EXTERNALLY_CALIBRATED		(1 << 4)
#define ETH_TRANSCEIVER_INTERNALLY_CALIBRATED		(1 << 5)
#define ETH_TRANSCEIVER_HAS_DIAGNOSTIC			(1 << 6)
#define ETH_TRANSCEIVER_IDENT_MASK			0x0000ff00
#define ETH_TRANSCEIVER_IDENT_OFFSET			8

	u32 oem_cfg_port;
#define OEM_CFG_CHANNEL_TYPE_MASK			0x00000003
#define OEM_CFG_CHANNEL_TYPE_OFFSET			0
#define OEM_CFG_CHANNEL_TYPE_VLAN_PARTITION		0x1
#define OEM_CFG_CHANNEL_TYPE_STAGGED			0x2

#define OEM_CFG_SCHED_TYPE_MASK				0x0000000C
#define OEM_CFG_SCHED_TYPE_OFFSET			2
#define OEM_CFG_SCHED_TYPE_ETS				0x1
#define OEM_CFG_SCHED_TYPE_VNIC_BW			0x2

	struct lldp_received_tlvs_s lldp_received_tlvs[LLDP_MAX_LLDP_AGENTS];
	u32 system_lldp_tlvs_buf2[MAX_SYSTEM_LLDP_TLV_DATA];
};

/**************************************/
/*                                    */
/*     P U B L I C      F U N C       */
/*                                    */
/**************************************/

struct public_func {

	u32 iscsi_boot_signature;
	u32 iscsi_boot_block_offset;

	/* MTU size per funciton is needed for the OV feature */
	u32 mtu_size;
	/* 9 entires for the C2S PCP map for each inner VLAN PCP + 1 default */
	/* For PCP values 0-3 use the map lower */
	/* 0xFF000000 - PCP 0, 0x00FF0000 - PCP 1,
	 * 0x0000FF00 - PCP 2, 0x000000FF PCP 3
	 */
	u32 c2s_pcp_map_lower;
	/* For PCP values 4-7 use the map upper */
	/* 0xFF000000 - PCP 4, 0x00FF0000 - PCP 5,
	 * 0x0000FF00 - PCP 6, 0x000000FF PCP 7
	*/
	u32 c2s_pcp_map_upper;

	/* For PCP default value get the MSB byte of the map default */
	u32 c2s_pcp_map_default;

	/* For generic inter driver communication channel messages between PFs via MFW*/
	struct generic_idc_msg_s generic_idc_msg;

	u32 num_of_msix;

	// replace old mf_cfg
	u32 config;
	/* E/R/I/D */
	/* function 0 of each port cannot be hidden */
#define FUNC_MF_CFG_FUNC_HIDE                   0x00000001
#define FUNC_MF_CFG_PAUSE_ON_HOST_RING          0x00000002
#define FUNC_MF_CFG_PAUSE_ON_HOST_RING_OFFSET    0x00000001


#define FUNC_MF_CFG_PROTOCOL_MASK               0x000000f0
#define FUNC_MF_CFG_PROTOCOL_OFFSET              4
#define FUNC_MF_CFG_PROTOCOL_ETHERNET           0x00000000
#define FUNC_MF_CFG_PROTOCOL_ISCSI              0x00000010
#define FUNC_MF_CFG_PROTOCOL_FCOE               0x00000020
#define FUNC_MF_CFG_PROTOCOL_ROCE               0x00000030
#define FUNC_MF_CFG_PROTOCOL_MAX                0x00000030

	/* MINBW, MAXBW */
	/* value range - 0..100, increments in 1 %  */
#define FUNC_MF_CFG_MIN_BW_MASK                 0x0000ff00
#define FUNC_MF_CFG_MIN_BW_OFFSET                8
#define FUNC_MF_CFG_MIN_BW_DEFAULT              0x00000000
#define FUNC_MF_CFG_MAX_BW_MASK                 0x00ff0000
#define FUNC_MF_CFG_MAX_BW_OFFSET                16
#define FUNC_MF_CFG_MAX_BW_DEFAULT              0x00640000

	/*RDMA PROTOCL*/
#define FUNC_MF_CFG_RDMA_PROTOCOL_MASK          0x03000000
#define FUNC_MF_CFG_RDMA_PROTOCOL_OFFSET         24
#define FUNC_MF_CFG_RDMA_PROTOCOL_NONE         	0x00000000
#define FUNC_MF_CFG_RDMA_PROTOCOL_ROCE          0x01000000
#define FUNC_MF_CFG_RDMA_PROTOCOL_IWARP         0x02000000
	/*for future support*/
#define FUNC_MF_CFG_RDMA_PROTOCOL_BOTH          0x03000000

#define FUNC_MF_CFG_BOOT_MODE_MASK		0x0C000000
#define FUNC_MF_CFG_BOOT_MODE_OFFSET		26
#define FUNC_MF_CFG_BOOT_MODE_BIOS_CTRL		0x00000000
#define FUNC_MF_CFG_BOOT_MODE_DISABLED		0x04000000
#define FUNC_MF_CFG_BOOT_MODE_ENABLED		0x08000000

	u32 status;
#define FUNC_STATUS_VIRTUAL_LINK_UP		0x00000001
#define FUNC_STATUS_LOGICAL_LINK_UP		0x00000002
#define FUNC_STATUS_FORCED_LINK	 		0x00000004

	u32 mac_upper;      /* MAC */
#define FUNC_MF_CFG_UPPERMAC_MASK               0x0000ffff
#define FUNC_MF_CFG_UPPERMAC_OFFSET              0
#define FUNC_MF_CFG_UPPERMAC_DEFAULT            FUNC_MF_CFG_UPPERMAC_MASK
	u32 mac_lower;
#define FUNC_MF_CFG_LOWERMAC_DEFAULT            0xffffffff

	u32 fcoe_wwn_port_name_upper;
	u32 fcoe_wwn_port_name_lower;

	u32 fcoe_wwn_node_name_upper;
	u32 fcoe_wwn_node_name_lower;

	u32 ovlan_stag;     /* tags */
#define FUNC_MF_CFG_OV_STAG_MASK              0x0000ffff
#define FUNC_MF_CFG_OV_STAG_OFFSET             0
#define FUNC_MF_CFG_OV_STAG_DEFAULT           FUNC_MF_CFG_OV_STAG_MASK

	u32 pf_allocation; /* vf per pf */

	u32 preserve_data; /* Will be used bt CCM */

	u32 driver_last_activity_ts;

	/*
	 * drv_ack_vf_disabled is set by the PF driver to ack handled disabled
	 * VFs
	 */
	u32 drv_ack_vf_disabled[VF_MAX_STATIC / 32];    /* 0x0044 */

	u32 drv_id;
#define DRV_ID_PDA_COMP_VER_MASK	0x0000ffff
#define DRV_ID_PDA_COMP_VER_OFFSET	0

#define LOAD_REQ_HSI_VERSION		2
#define DRV_ID_MCP_HSI_VER_MASK		0x00ff0000
#define DRV_ID_MCP_HSI_VER_OFFSET	16
#define DRV_ID_MCP_HSI_VER_CURRENT	(LOAD_REQ_HSI_VERSION << DRV_ID_MCP_HSI_VER_OFFSET)

#define DRV_ID_DRV_TYPE_MASK		0x7f000000
#define DRV_ID_DRV_TYPE_OFFSET		24
#define DRV_ID_DRV_TYPE_UNKNOWN		(0 << DRV_ID_DRV_TYPE_OFFSET)
#define DRV_ID_DRV_TYPE_LINUX		(1 << DRV_ID_DRV_TYPE_OFFSET)
#define DRV_ID_DRV_TYPE_WINDOWS		(2 << DRV_ID_DRV_TYPE_OFFSET)
#define DRV_ID_DRV_TYPE_DIAG		(3 << DRV_ID_DRV_TYPE_OFFSET)
#define DRV_ID_DRV_TYPE_PREBOOT		(4 << DRV_ID_DRV_TYPE_OFFSET)
#define DRV_ID_DRV_TYPE_SOLARIS		(5 << DRV_ID_DRV_TYPE_OFFSET)
#define DRV_ID_DRV_TYPE_VMWARE		(6 << DRV_ID_DRV_TYPE_OFFSET)
#define DRV_ID_DRV_TYPE_FREEBSD		(7 << DRV_ID_DRV_TYPE_OFFSET)
#define DRV_ID_DRV_TYPE_AIX		(8 << DRV_ID_DRV_TYPE_OFFSET)

#define DRV_ID_DRV_TYPE_OS 			(DRV_ID_DRV_TYPE_LINUX | DRV_ID_DRV_TYPE_WINDOWS | \
									 DRV_ID_DRV_TYPE_SOLARIS | DRV_ID_DRV_TYPE_VMWARE | \
									 DRV_ID_DRV_TYPE_FREEBSD | DRV_ID_DRV_TYPE_AIX)

#define DRV_ID_DRV_INIT_HW_MASK		0x80000000
#define DRV_ID_DRV_INIT_HW_OFFSET	31
#define DRV_ID_DRV_INIT_HW_FLAG		(1 << DRV_ID_DRV_INIT_HW_OFFSET)

	u32 oem_cfg_func;
#define OEM_CFG_FUNC_TC_MASK			0x0000000F
#define OEM_CFG_FUNC_TC_OFFSET			0
#define OEM_CFG_FUNC_TC_0			0x0
#define OEM_CFG_FUNC_TC_1			0x1
#define OEM_CFG_FUNC_TC_2			0x2
#define OEM_CFG_FUNC_TC_3			0x3
#define OEM_CFG_FUNC_TC_4			0x4
#define OEM_CFG_FUNC_TC_5			0x5
#define OEM_CFG_FUNC_TC_6			0x6
#define OEM_CFG_FUNC_TC_7			0x7

#define OEM_CFG_FUNC_HOST_PRI_CTRL_MASK		0x00000030
#define OEM_CFG_FUNC_HOST_PRI_CTRL_OFFSET	4
#define OEM_CFG_FUNC_HOST_PRI_CTRL_VNIC		0x1
#define OEM_CFG_FUNC_HOST_PRI_CTRL_OS		0x2
};

/**************************************/
/*                                    */
/*     P U B L I C       M B          */
/*                                    */
/**************************************/
/* This is the only section that the driver can write to, and each */
/* Basically each driver request to set feature parameters,
 * will be done using a different command, which will be linked
 * to a specific data structure from the union below.
 * For huge strucuture, the common blank structure should be used.
 */

struct mcp_mac {
	u32 mac_upper;      /* Upper 16 bits are always zeroes */
	u32 mac_lower;
};

struct mcp_file_att {
	u32 nvm_start_addr;
	u32 len;
};

struct bist_nvm_image_att {
	u32 return_code;
	u32 image_type;		/* Image type */
	u32 nvm_start_addr;	/* NVM address of the image */
	u32 len;		/* Include CRC */
};

#define MCP_DRV_VER_STR_SIZE 16
#define MCP_DRV_VER_STR_SIZE_DWORD (MCP_DRV_VER_STR_SIZE / sizeof(u32))
#define MCP_DRV_NVM_BUF_LEN 32
struct drv_version_stc {
	u32 version;
	u8 name[MCP_DRV_VER_STR_SIZE - 4];
};

/* statistics for ncsi */
struct lan_stats_stc {
	u64 ucast_rx_pkts;
	u64 ucast_tx_pkts;
	u32 fcs_err;
	u32 rserved;
};

struct fcoe_stats_stc {
	u64 rx_pkts;
	u64 tx_pkts;
	u32 fcs_err;
	u32 login_failure;
};

struct iscsi_stats_stc {
	u64 rx_pdus;
	u64 tx_pdus;
	u64 rx_bytes;
	u64 tx_bytes;
};

struct rdma_stats_stc {
	u64 rx_pkts;
	u64 tx_pkts;
	u64 rx_bytes;
	u64 tx_bytes;
};

struct ocbb_data_stc {
	u32 ocbb_host_addr;
	u32 ocsd_host_addr;
	u32 ocsd_req_update_interval;
};

#define MAX_NUM_OF_SENSORS			7
#define MFW_SENSOR_LOCATION_INTERNAL		1
#define MFW_SENSOR_LOCATION_EXTERNAL		2
#define MFW_SENSOR_LOCATION_SFP			3

#define SENSOR_LOCATION_OFFSET			0
#define SENSOR_LOCATION_MASK			0x000000ff
#define THRESHOLD_HIGH_OFFSET			8
#define THRESHOLD_HIGH_MASK			0x0000ff00
#define CRITICAL_TEMPERATURE_OFFSET		16
#define CRITICAL_TEMPERATURE_MASK		0x00ff0000
#define CURRENT_TEMP_OFFSET			24
#define CURRENT_TEMP_MASK			0xff000000
struct temperature_status_stc {
	u32 num_of_sensors;
	u32 sensor[MAX_NUM_OF_SENSORS];
};

/* crash dump configuration header */
struct mdump_config_stc {
	u32 version;
	u32 config;
	u32 epoc;
	u32 num_of_logs;
	u32 valid_logs;
};

enum resource_id_enum {
	RESOURCE_NUM_SB_E			=	0,
	RESOURCE_NUM_L2_QUEUE_E		=	1,
	RESOURCE_NUM_VPORT_E		=	2,
	RESOURCE_NUM_VMQ_E			=	3,
	RESOURCE_FACTOR_NUM_RSS_PF_E	=	4,  /* Not a real resource!! it's a factor used to calculate others */
	RESOURCE_FACTOR_RSS_PER_VF_E	=	5,  /* Not a real resource!! it's a factor used to calculate others */
	RESOURCE_NUM_RL_E			=	6,
	RESOURCE_NUM_PQ_E			=	7,
	RESOURCE_NUM_VF_E			=	8,
	RESOURCE_VFC_FILTER_E		=	9,
	RESOURCE_ILT_E				=	10,
	RESOURCE_CQS_E				=	11,
	RESOURCE_GFT_PROFILES_E		=	12,
	RESOURCE_NUM_TC_E			=	13,
	RESOURCE_NUM_RSS_ENGINES_E	=	14,
	RESOURCE_LL2_QUEUE_E		=	15,
	RESOURCE_RDMA_STATS_QUEUE_E	=	16,
	RESOURCE_BDQ_E				=	17,
	RESOURCE_MAX_NUM,
	RESOURCE_NUM_INVALID		=	0xFFFFFFFF
};

/* Resource ID is to be filled by the driver in the MB request
 * Size, offset & flags to be filled by the MFW in the MB response
 */
struct resource_info {
	enum resource_id_enum res_id;
	u32 size; /* number of allocated resources */
	u32 offset; /* Offset of the 1st resource */
	u32 vf_size;
	u32 vf_offset;
	u32 flags;
#define RESOURCE_ELEMENT_STRICT (1 << 0)
};

struct mcp_wwn {
	u32 wwn_upper;
	u32 wwn_lower;
};

#define DRV_ROLE_NONE		0
#define DRV_ROLE_PREBOOT	1
#define DRV_ROLE_OS		2
#define DRV_ROLE_KDUMP		3

struct load_req_stc {
	u32 drv_ver_0;
	u32 drv_ver_1;
	u32 fw_ver;
	u32 misc0;
#define LOAD_REQ_ROLE_MASK		0x000000FF
#define LOAD_REQ_ROLE_OFFSET		0
#define LOAD_REQ_LOCK_TO_MASK		0x0000FF00
#define LOAD_REQ_LOCK_TO_OFFSET		8
#define LOAD_REQ_LOCK_TO_DEFAULT	0
#define LOAD_REQ_LOCK_TO_NONE		255
#define LOAD_REQ_FORCE_MASK		0x000F0000
#define LOAD_REQ_FORCE_OFFSET		16
#define LOAD_REQ_FORCE_NONE		0
#define LOAD_REQ_FORCE_PF		1
#define LOAD_REQ_FORCE_ALL		2
#define LOAD_REQ_FLAGS0_MASK		0x00F00000
#define LOAD_REQ_FLAGS0_OFFSET		20
#define LOAD_REQ_FLAGS0_AVOID_RESET	(0x1 << 0)
};

struct load_rsp_stc {
	u32 drv_ver_0;
	u32 drv_ver_1;
	u32 fw_ver;
	u32 misc0;
#define LOAD_RSP_ROLE_MASK		0x000000FF
#define LOAD_RSP_ROLE_OFFSET		0
#define LOAD_RSP_HSI_MASK		0x0000FF00
#define LOAD_RSP_HSI_OFFSET		8
#define LOAD_RSP_FLAGS0_MASK		0x000F0000
#define LOAD_RSP_FLAGS0_OFFSET		16
#define LOAD_RSP_FLAGS0_DRV_EXISTS	(0x1 << 0)
};

struct mdump_retain_data_stc {
	u32 valid;
	u32 epoch;
	u32 pf;
	u32 status;
};

struct attribute_cmd_write_stc {
	u32 val;
	u32 mask;
	u32 offset;
};

struct lldp_stats_stc {
	u32 tx_frames_total;
	u32 rx_frames_total;
	u32 rx_frames_discarded;
	u32 rx_age_outs;
};

union drv_union_data {
	struct mcp_mac wol_mac; /* UNLOAD_DONE */

	/* This configuration should be set by the driver for the LINK_SET command. */
	struct eth_phy_cfg drv_phy_cfg;

	struct mcp_val64 val64; /* For PHY / AVS commands */

	u8 raw_data[MCP_DRV_NVM_BUF_LEN];

	struct mcp_file_att file_att;

	u32 ack_vf_disabled[VF_MAX_STATIC / 32];

	struct drv_version_stc drv_version;

	struct lan_stats_stc lan_stats;
	struct fcoe_stats_stc fcoe_stats;
	struct iscsi_stats_stc iscsi_stats;
	struct rdma_stats_stc rdma_stats;
	struct ocbb_data_stc ocbb_info;
	struct temperature_status_stc temp_info;
	struct resource_info resource;
	struct bist_nvm_image_att nvm_image_att;
	struct mdump_config_stc mdump_config;
	struct mcp_mac lldp_mac;
	struct mcp_wwn fcoe_fabric_name;
	u32 dword;

	struct load_req_stc load_req;
	struct load_rsp_stc load_rsp;
	struct mdump_retain_data_stc mdump_retain;
	struct attribute_cmd_write_stc attribute_cmd_write;
	struct lldp_stats_stc lldp_stats;
	/* ... */
};

struct public_drv_mb {

	u32 drv_mb_header;
#define DRV_MSG_CODE_MASK                       0xffff0000
#define DRV_MSG_CODE_LOAD_REQ                   0x10000000
#define DRV_MSG_CODE_LOAD_DONE                  0x11000000
#define DRV_MSG_CODE_INIT_HW                    0x12000000
#define DRV_MSG_CODE_CANCEL_LOAD_REQ            0x13000000
#define DRV_MSG_CODE_UNLOAD_REQ		        0x20000000
#define DRV_MSG_CODE_UNLOAD_DONE                0x21000000
#define DRV_MSG_CODE_INIT_PHY			0x22000000
	/* Params - FORCE - Reinitialize the link regardless of LFA */
	/*        - DONT_CARE - Don't flap the link if up */
#define DRV_MSG_CODE_LINK_RESET			0x23000000

#define DRV_MSG_CODE_SET_LLDP                   0x24000000
#define DRV_MSG_CODE_REGISTER_LLDP_TLVS_RX      0x24100000
#define DRV_MSG_CODE_SET_DCBX                   0x25000000
	/* OneView feature driver HSI*/
#define DRV_MSG_CODE_OV_UPDATE_CURR_CFG		0x26000000
#define DRV_MSG_CODE_OV_UPDATE_BUS_NUM		0x27000000
#define DRV_MSG_CODE_OV_UPDATE_BOOT_PROGRESS	0x28000000
#define DRV_MSG_CODE_OV_UPDATE_STORM_FW_VER	0x29000000
#define DRV_MSG_CODE_NIG_DRAIN			0x30000000
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE	0x31000000
#define DRV_MSG_CODE_BW_UPDATE_ACK		0x32000000
#define DRV_MSG_CODE_OV_UPDATE_MTU		0x33000000
#define DRV_MSG_GET_RESOURCE_ALLOC_MSG		0x34000000 /* DRV_MB Param: driver version supp, FW_MB param: MFW version supp, data: struct resource_info */
#define DRV_MSG_SET_RESOURCE_VALUE_MSG		0x35000000
#define DRV_MSG_CODE_OV_UPDATE_WOL		0x38000000
#define DRV_MSG_CODE_OV_UPDATE_ESWITCH_MODE	0x39000000
#define DRV_MSG_CODE_S_TAG_UPDATE_ACK		0x3b000000
#define DRV_MSG_CODE_OEM_UPDATE_FCOE_CVID	0x3c000000
#define DRV_MSG_CODE_OEM_UPDATE_FCOE_FABRIC_NAME	0x3d000000
#define DRV_MSG_CODE_OEM_UPDATE_BOOT_CFG	0x3e000000
#define DRV_MSG_CODE_OEM_RESET_TO_DEFAULT	0x3f000000
#define DRV_MSG_CODE_OV_GET_CURR_CFG		0x40000000
#define DRV_MSG_CODE_GET_OEM_UPDATES		0x41000000
#define DRV_MSG_CODE_GET_LLDP_STATS		0x42000000
#define DRV_MSG_CODE_GET_PPFID_BITMAP		0x43000000 /* params [31:8] - reserved, [7:0] - bitmap */

#define DRV_MSG_CODE_INITIATE_FLR_DEPRECATED    0x02000000 /*deprecated don't use*/
#define DRV_MSG_CODE_INITIATE_PF_FLR            0x02010000
#define DRV_MSG_CODE_VF_DISABLED_DONE           0xc0000000
#define DRV_MSG_CODE_CFG_VF_MSIX                0xc0010000
#define DRV_MSG_CODE_CFG_PF_VFS_MSIX            0xc0020000
#define DRV_MSG_CODE_NVM_PUT_FILE_BEGIN		0x00010000 /* Param is either DRV_MB_PARAM_NVM_PUT_FILE_BEGIN_MFW/IMAGE */
#define DRV_MSG_CODE_NVM_PUT_FILE_DATA		0x00020000 /* Param should be set to the transaction size (up to 64 bytes) */
#define DRV_MSG_CODE_NVM_GET_FILE_ATT		0x00030000 /* MFW will place the file offset and len in file_att struct */
#define DRV_MSG_CODE_NVM_READ_NVRAM		0x00050000 /* Read 32bytes of nvram data. Param is [0:23] ??? Offset [24:31] ??? Len in Bytes*/
#define DRV_MSG_CODE_NVM_WRITE_NVRAM		0x00060000 /* Writes up to 32Bytes to nvram. Param is [0:23] ??? Offset [24:31] ??? Len in Bytes. In case this address is in the range of secured file in secured mode, the operation will fail */
#define DRV_MSG_CODE_NVM_DEL_FILE		0x00080000 /* Delete a file from nvram. Param is image_type. */
#define DRV_MSG_CODE_MCP_RESET			0x00090000 /* Reset MCP when no NVM operation is going on, and no drivers are loaded. In case operation succeed, MCP will not ack back. */
#define DRV_MSG_CODE_SET_SECURE_MODE		0x000a0000 /* Temporary command to set secure mode, where the param is 0 (None secure) / 1 (Secure) / 2 (Full-Secure) */
#define DRV_MSG_CODE_PHY_RAW_READ		0x000b0000 /* Param: [0:15] - Address, [16:18] - lane# (0/1/2/3 - for single lane, 4/5 - for dual lanes, 6 - for all lanes, [28] - PMD reg, [29] - select port, [30:31] - port*/
#define DRV_MSG_CODE_PHY_RAW_WRITE		0x000c0000 /* Param: [0:15] - Address, [16:18] - lane# (0/1/2/3 - for single lane, 4/5 - for dual lanes, 6 - for all lanes, [28] - PMD reg, [29] - select port, [30:31] - port */
#define DRV_MSG_CODE_PHY_CORE_READ		0x000d0000 /* Param: [0:15] - Address, [30:31] - port */
#define DRV_MSG_CODE_PHY_CORE_WRITE		0x000e0000 /* Param: [0:15] - Address, [30:31] - port */
#define DRV_MSG_CODE_SET_VERSION		0x000f0000 /* Param: [0:3] - version, [4:15] - name (null terminated) */
#define DRV_MSG_CODE_MCP_HALT			0x00100000 /* Halts the MCP. To resume MCP, user will need to use MCP_REG_CPU_STATE/MCP_REG_CPU_MODE registers. */
#define DRV_MSG_CODE_SET_VMAC                   0x00110000 /* Set virtual mac address, params [31:6] - reserved, [5:4] - type, [3:0] - func, drv_data[7:0] - MAC/WWNN/WWPN */
#define DRV_MSG_CODE_GET_VMAC                   0x00120000 /* Set virtual mac address, params [31:6] - reserved, [5:4] - type, [3:0] - func, drv_data[7:0] - MAC/WWNN/WWPN */
#define DRV_MSG_CODE_VMAC_TYPE_OFFSET		4
#define DRV_MSG_CODE_VMAC_TYPE_MASK		0x30
#define DRV_MSG_CODE_VMAC_TYPE_MAC              1
#define DRV_MSG_CODE_VMAC_TYPE_WWNN             2
#define DRV_MSG_CODE_VMAC_TYPE_WWPN             3

#define DRV_MSG_CODE_GET_STATS                  0x00130000 /* Get statistics from pf, params [31:4] - reserved, [3:0] - stats type */
#define DRV_MSG_CODE_STATS_TYPE_LAN             1
#define DRV_MSG_CODE_STATS_TYPE_FCOE            2
#define DRV_MSG_CODE_STATS_TYPE_ISCSI           3
#define DRV_MSG_CODE_STATS_TYPE_RDMA  		4
#define DRV_MSG_CODE_PMD_DIAG_DUMP 		0x00140000 /* Host shall provide buffer and size for MFW  */
#define DRV_MSG_CODE_PMD_DIAG_EYE  		0x00150000 /* Host shall provide buffer and size for MFW  */
#define DRV_MSG_CODE_TRANSCEIVER_READ		0x00160000 /* Param: [0:1] - Port, [2:7] - read size, [8:15] - I2C address, [16:31] - offset */
#define DRV_MSG_CODE_TRANSCEIVER_WRITE		0x00170000 /* Param: [0:1] - Port, [2:7] - write size, [8:15] - I2C address, [16:31] - offset */
#define DRV_MSG_CODE_OCBB_DATA			0x00180000 /* indicate OCBB related information */
#define DRV_MSG_CODE_SET_BW			0x00190000 /* Set function BW, params[15:8] - min, params[7:0] - max */
#define BW_MAX_MASK				0x000000ff
#define BW_MAX_OFFSET				0
#define BW_MIN_MASK				0x0000ff00
#define BW_MIN_OFFSET				8

#define DRV_MSG_CODE_MASK_PARITIES		0x001a0000 /* When param is set to 1, all parities will be masked(disabled). When params are set to 0, parities will be unmasked again. */
#define DRV_MSG_CODE_INDUCE_FAILURE		0x001b0000 /* param[0] - Simulate fan failure,  param[1] - simulate over temp. */
#define DRV_MSG_FAN_FAILURE_TYPE		(1 << 0)
#define DRV_MSG_TEMPERATURE_FAILURE_TYPE	(1 << 1)
#define DRV_MSG_CODE_GPIO_READ			0x001c0000 /* Param: [0:15] - gpio number */
#define DRV_MSG_CODE_GPIO_WRITE			0x001d0000 /* Param: [0:15] - gpio number, [16:31] - gpio value */
#define DRV_MSG_CODE_BIST_TEST			0x001e0000      /* Param: [0:7] - test enum, [8:15] - image index, [16:31] - reserved */
#define DRV_MSG_CODE_GET_TEMPERATURE            0x001f0000
#define DRV_MSG_CODE_SET_LED_MODE		0x00200000 /* Set LED mode  params :0 operational, 1 LED turn ON, 2 LED turn OFF */
#define DRV_MSG_CODE_TIMESTAMP                  0x00210000 /* drv_data[7:0] - EPOC in seconds, drv_data[15:8] - driver version (MAJ MIN BUILD SUB) */
#define DRV_MSG_CODE_EMPTY_MB			0x00220000 /* This is an empty mailbox just return OK*/

#define DRV_MSG_CODE_RESOURCE_CMD		0x00230000 /* Param[0:4] - resource number (0-31), Param[5:7] - opcode, param[15:8] - age */

#define RESOURCE_CMD_REQ_RESC_MASK		0x0000001F
#define RESOURCE_CMD_REQ_RESC_OFFSET		0
#define RESOURCE_CMD_REQ_OPCODE_MASK		0x000000E0
#define RESOURCE_CMD_REQ_OPCODE_OFFSET		5
#define RESOURCE_OPCODE_REQ			1 /* request resource ownership with default aging */
#define RESOURCE_OPCODE_REQ_WO_AGING        	2 /* request resource ownership without aging */
#define RESOURCE_OPCODE_REQ_W_AGING        	3 /* request resource ownership with specific aging timer (in seconds) */
#define RESOURCE_OPCODE_RELEASE			4 /* release resource */
#define RESOURCE_OPCODE_FORCE_RELEASE		5 /* force resource release */
#define RESOURCE_CMD_REQ_AGE_MASK		0x0000FF00
#define RESOURCE_CMD_REQ_AGE_OFFSET		8

#define RESOURCE_CMD_RSP_OWNER_MASK		0x000000FF
#define RESOURCE_CMD_RSP_OWNER_OFFSET		0
#define RESOURCE_CMD_RSP_OPCODE_MASK		0x00000700
#define RESOURCE_CMD_RSP_OPCODE_OFFSET		8
#define RESOURCE_OPCODE_GNT			1 /* resource is free and granted to requester */
#define RESOURCE_OPCODE_BUSY			2 /* resource is busy, param[7:0] indicates owner as follow 0-15 = PF0-15, 16 = MFW, 17 = diag over serial */
#define RESOURCE_OPCODE_RELEASED		3 /* indicate release request was acknowledged */
#define RESOURCE_OPCODE_RELEASED_PREVIOUS	4 /* indicate release request was previously received by other owner */
#define RESOURCE_OPCODE_WRONG_OWNER		5 /* indicate wrong owner during release */
#define RESOURCE_OPCODE_UNKNOWN_CMD		255

#define RESOURCE_DUMP				0 /* dedicate resource 0 for dump */

#define DRV_MSG_CODE_GET_MBA_VERSION		0x00240000 /* Get MBA version */
#define DRV_MSG_CODE_MDUMP_CMD			0x00250000 /* Send crash dump commands with param[3:0] - opcode */
#define MDUMP_DRV_PARAM_OPCODE_MASK		0x0000000f
#define DRV_MSG_CODE_MDUMP_ACK			0x01 /* acknowledge reception of error indication */
#define DRV_MSG_CODE_MDUMP_SET_VALUES		0x02 /* set epoc and personality as follow: drv_data[3:0] - epoch, drv_data[7:4] - personality */
#define DRV_MSG_CODE_MDUMP_TRIGGER		0x03 /* trigger crash dump procedure */
#define DRV_MSG_CODE_MDUMP_GET_CONFIG		0x04 /* Request valid logs and config words */
#define DRV_MSG_CODE_MDUMP_SET_ENABLE		0x05 /* Set triggers mask. drv_mb_param should indicate (bitwise) which trigger enabled */
#define DRV_MSG_CODE_MDUMP_CLEAR_LOGS		0x06 /* Clear all logs */
#define DRV_MSG_CODE_MDUMP_GET_RETAIN		0x07 /* Get retained data */
#define DRV_MSG_CODE_MDUMP_CLR_RETAIN		0x08 /* Clear retain data */
#define DRV_MSG_CODE_MEM_ECC_EVENTS		0x00260000 /* Param: None */
#define DRV_MSG_CODE_GPIO_INFO			0x00270000 /* Param: [0:15] - gpio number */
#define DRV_MSG_CODE_EXT_PHY_READ		0x00280000 /* Value will be placed in union */
#define DRV_MSG_CODE_EXT_PHY_WRITE		0x00290000 /* Value shoud be placed in union */
#define DRV_MB_PARAM_ADDR_OFFSET			0
#define DRV_MB_PARAM_ADDR_MASK			0x0000FFFF
#define DRV_MB_PARAM_DEVAD_OFFSET		16
#define DRV_MB_PARAM_DEVAD_MASK			0x001F0000
#define DRV_MB_PARAM_PORT_OFFSET			21
#define DRV_MB_PARAM_PORT_MASK			0x00600000
#define DRV_MSG_CODE_EXT_PHY_FW_UPGRADE		0x002a0000
#define DRV_MSG_CODE_GET_PF_RDMA_PROTOCOL	0x002b0000
#define DRV_MSG_CODE_SET_LLDP_MAC			0x002c0000
#define DRV_MSG_CODE_GET_LLDP_MAC			0x002d0000
#define DRV_MSG_CODE_OS_WOL					0x002e0000

#define DRV_MSG_CODE_GET_TLV_DONE  	        0x002f0000 /* Param: None */
#define DRV_MSG_CODE_FEATURE_SUPPORT 		0x00300000 /* Param: Set DRV_MB_PARAM_FEATURE_SUPPORT_* */
#define DRV_MSG_CODE_GET_MFW_FEATURE_SUPPORT	0x00310000 /* return FW_MB_PARAM_FEATURE_SUPPORT_*  */

#define DRV_MSG_CODE_READ_WOL_REG			0X00320000
#define DRV_MSG_CODE_WRITE_WOL_REG			0X00330000
#define DRV_MSG_CODE_GET_WOL_BUFFER			0X00340000
#define DRV_MSG_CODE_ATTRIBUTE  		0x00350000 /* Param: [0:23] Attribute key, [24:31] Attribute sub command */

#define DRV_MSG_CODE_ENCRYPT_PASSWORD		0x00360000 /* Param: Password len. Union: Plain Password */
#define DRV_MSG_CODE_GET_ENGINE_CONFIG		0x00370000 /* Param: None */

	/*    Pmbus commands	*/
#define DRV_MSG_CODE_PMBUS_READ			0x00380000 /* Param: [0:7] - Cmd, [8:9] - len */
#define DRV_MSG_CODE_PMBUS_WRITE		0x00390000 /* Param: [0:7] - Cmd, [8:9] - len, [16:31] -data*/

#define DRV_MB_PARAM_PMBUS_CMD_OFFSET		0
#define DRV_MB_PARAM_PMBUS_CMD_MASK		0xFF
#define DRV_MB_PARAM_PMBUS_LEN_OFFSET		8
#define DRV_MB_PARAM_PMBUS_LEN_MASK		0x300
#define DRV_MB_PARAM_PMBUS_DATA_OFFSET		16
#define DRV_MB_PARAM_PMBUS_DATA_MASK		0xFFFF0000

#define DRV_MSG_CODE_GENERIC_IDC		0x003a0000

#define DRV_MSG_SEQ_NUMBER_MASK				0x0000ffff

	u32 drv_mb_param;
	/* UNLOAD_REQ params */
#define DRV_MB_PARAM_UNLOAD_WOL_UNKNOWN         0x00000000
#define DRV_MB_PARAM_UNLOAD_WOL_MCP		0x00000001
#define DRV_MB_PARAM_UNLOAD_WOL_DISABLED        0x00000002
#define DRV_MB_PARAM_UNLOAD_WOL_ENABLED         0x00000003

	/* UNLOAD_DONE_params */
#define DRV_MB_PARAM_UNLOAD_NON_D3_POWER        0x00000001

	/* INIT_PHY params */
#define DRV_MB_PARAM_INIT_PHY_FORCE		0x00000001
#define DRV_MB_PARAM_INIT_PHY_DONT_CARE		0x00000002

	/* LLDP / DCBX params*/
	/* To be used with SET_LLDP command */
#define DRV_MB_PARAM_LLDP_SEND_MASK		0x00000001
#define DRV_MB_PARAM_LLDP_SEND_OFFSET		0
	/* To be used with SET_LLDP and REGISTER_LLDP_TLVS_RX commands */
#define DRV_MB_PARAM_LLDP_AGENT_MASK		0x00000006
#define DRV_MB_PARAM_LLDP_AGENT_OFFSET       	1
	/* To be used with REGISTER_LLDP_TLVS_RX command */
#define DRV_MB_PARAM_LLDP_TLV_RX_VALID_MASK	0x00000001
#define DRV_MB_PARAM_LLDP_TLV_RX_VALID_OFFSET	0
#define DRV_MB_PARAM_LLDP_TLV_RX_TYPE_MASK	0x000007f0
#define DRV_MB_PARAM_LLDP_TLV_RX_TYPE_OFFSET	4
	/* To be used with SET_DCBX command */
#define DRV_MB_PARAM_DCBX_NOTIFY_MASK		0x00000008
#define DRV_MB_PARAM_DCBX_NOTIFY_OFFSET		3

#define DRV_MB_PARAM_NIG_DRAIN_PERIOD_MS_MASK	0x000000FF
#define DRV_MB_PARAM_NIG_DRAIN_PERIOD_MS_OFFSET	0

#define DRV_MB_PARAM_NVM_PUT_FILE_BEGIN_MFW	0x1
#define DRV_MB_PARAM_NVM_PUT_FILE_BEGIN_IMAGE	0x2

#define DRV_MB_PARAM_NVM_OFFSET_OFFSET		0
#define DRV_MB_PARAM_NVM_OFFSET_MASK		0x00FFFFFF
#define DRV_MB_PARAM_NVM_LEN_OFFSET		24
#define DRV_MB_PARAM_NVM_LEN_MASK		0xFF000000

#define DRV_MB_PARAM_PHY_ADDR_OFFSET		0
#define DRV_MB_PARAM_PHY_ADDR_MASK		0x1FF0FFFF
#define DRV_MB_PARAM_PHY_LANE_OFFSET		16
#define DRV_MB_PARAM_PHY_LANE_MASK		0x000F0000
#define DRV_MB_PARAM_PHY_SELECT_PORT_OFFSET	29
#define DRV_MB_PARAM_PHY_SELECT_PORT_MASK	0x20000000
#define DRV_MB_PARAM_PHY_PORT_OFFSET		30
#define DRV_MB_PARAM_PHY_PORT_MASK		0xc0000000

#define DRV_MB_PARAM_PHYMOD_LANE_OFFSET		0
#define DRV_MB_PARAM_PHYMOD_LANE_MASK		0x000000FF
#define DRV_MB_PARAM_PHYMOD_SIZE_OFFSET		8
#define DRV_MB_PARAM_PHYMOD_SIZE_MASK		0x000FFF00
	/* configure vf MSIX params BB */
#define DRV_MB_PARAM_CFG_VF_MSIX_VF_ID_OFFSET	0
#define DRV_MB_PARAM_CFG_VF_MSIX_VF_ID_MASK		0x000000FF
#define DRV_MB_PARAM_CFG_VF_MSIX_SB_NUM_OFFSET	8
#define DRV_MB_PARAM_CFG_VF_MSIX_SB_NUM_MASK	0x0000FF00
	/* configure vf MSIX for PF params AH*/
#define DRV_MB_PARAM_CFG_PF_VFS_MSIX_SB_NUM_OFFSET	0
#define DRV_MB_PARAM_CFG_PF_VFS_MSIX_SB_NUM_MASK	0x000000FF

	/* OneView configuration parametres */
#define DRV_MB_PARAM_OV_CURR_CFG_OFFSET		0
#define DRV_MB_PARAM_OV_CURR_CFG_MASK		0x0000000F
#define DRV_MB_PARAM_OV_CURR_CFG_NONE		0
#define DRV_MB_PARAM_OV_CURR_CFG_OS			1
#define DRV_MB_PARAM_OV_CURR_CFG_VENDOR_SPEC	2
#define DRV_MB_PARAM_OV_CURR_CFG_OTHER		3
#define DRV_MB_PARAM_OV_CURR_CFG_VC_CLP		4
#define DRV_MB_PARAM_OV_CURR_CFG_CNU		5
#define DRV_MB_PARAM_OV_CURR_CFG_DCI		6
#define DRV_MB_PARAM_OV_CURR_CFG_HII		7

#define DRV_MB_PARAM_OV_UPDATE_BOOT_PROG_OFFSET					0
#define DRV_MB_PARAM_OV_UPDATE_BOOT_PROG_MASK					0x000000FF
#define DRV_MB_PARAM_OV_UPDATE_BOOT_PROG_NONE					(1 << 0)
#define DRV_MB_PARAM_OV_UPDATE_BOOT_PROG_ISCSI_IP_ACQUIRED		(1 << 1)
#define DRV_MB_PARAM_OV_UPDATE_BOOT_PROG_FCOE_FABRIC_LOGIN_SUCCESS	(1 << 1)
#define DRV_MB_PARAM_OV_UPDATE_BOOT_PROG_TRARGET_FOUND			(1 << 2)
#define DRV_MB_PARAM_OV_UPDATE_BOOT_PROG_ISCSI_CHAP_SUCCESS		(1 << 3)
#define DRV_MB_PARAM_OV_UPDATE_BOOT_PROG_FCOE_LUN_FOUND			(1 << 3)
#define DRV_MB_PARAM_OV_UPDATE_BOOT_PROG_LOGGED_INTO_TGT 		(1 << 4)
#define DRV_MB_PARAM_OV_UPDATE_BOOT_PROG_IMG_DOWNLOADED			(1 << 5)
#define DRV_MB_PARAM_OV_UPDATE_BOOT_PROG_OS_HANDOFF				(1 << 6)
#define DRV_MB_PARAM_OV_UPDATE_BOOT_COMPLETED 					0

#define DRV_MB_PARAM_OV_PCI_BUS_NUM_OFFSET    	0
#define DRV_MB_PARAM_OV_PCI_BUS_NUM_MASK		0x000000FF

#define DRV_MB_PARAM_OV_STORM_FW_VER_OFFSET    		0
#define DRV_MB_PARAM_OV_STORM_FW_VER_MASK			0xFFFFFFFF
#define DRV_MB_PARAM_OV_STORM_FW_VER_MAJOR_MASK		0xFF000000
#define DRV_MB_PARAM_OV_STORM_FW_VER_MINOR_MASK		0x00FF0000
#define DRV_MB_PARAM_OV_STORM_FW_VER_BUILD_MASK		0x0000FF00
#define DRV_MB_PARAM_OV_STORM_FW_VER_DROP_MASK		0x000000FF

#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_OFFSET		0
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_MASK		0xF
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_UNKNOWN		0x1
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_NOT_LOADED	0x2 /* Not Installed*/
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_LOADING		0x3
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_DISABLED	0x4 /* installed but disabled by user/admin/OS */
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_ACTIVE		0x5 /* installed and active */

#define DRV_MB_PARAM_OV_MTU_SIZE_OFFSET    	0
#define DRV_MB_PARAM_OV_MTU_SIZE_MASK		0xFFFFFFFF

#define DRV_MB_PARAM_WOL_MASK		(DRV_MB_PARAM_WOL_DEFAULT | 	\
					 DRV_MB_PARAM_WOL_DISABLED |	\
					 DRV_MB_PARAM_WOL_ENABLED)
#define DRV_MB_PARAM_WOL_DEFAULT	DRV_MB_PARAM_UNLOAD_WOL_MCP
#define DRV_MB_PARAM_WOL_DISABLED	DRV_MB_PARAM_UNLOAD_WOL_DISABLED
#define DRV_MB_PARAM_WOL_ENABLED	DRV_MB_PARAM_UNLOAD_WOL_ENABLED

#define DRV_MB_PARAM_ESWITCH_MODE_MASK	(DRV_MB_PARAM_ESWITCH_MODE_NONE | \
					 DRV_MB_PARAM_ESWITCH_MODE_VEB |   \
					 DRV_MB_PARAM_ESWITCH_MODE_VEPA)
#define DRV_MB_PARAM_ESWITCH_MODE_NONE	0x0
#define DRV_MB_PARAM_ESWITCH_MODE_VEB	0x1
#define DRV_MB_PARAM_ESWITCH_MODE_VEPA	0x2

#define DRV_MB_PARAM_FCOE_CVID_MASK	0xFFF
#define DRV_MB_PARAM_FCOE_CVID_OFFSET	0

#define DRV_MB_PARAM_DUMMY_OEM_UPDATES_MASK	0x1
#define DRV_MB_PARAM_DUMMY_OEM_UPDATES_OFFSET	0

#define DRV_MB_PARAM_LLDP_STATS_AGENT_MASK	0xFF
#define DRV_MB_PARAM_LLDP_STATS_AGENT_OFFSET	0

#define DRV_MB_PARAM_SET_LED_MODE_OPER		0x0
#define DRV_MB_PARAM_SET_LED_MODE_ON		0x1
#define DRV_MB_PARAM_SET_LED_MODE_OFF		0x2

#define DRV_MB_PARAM_TRANSCEIVER_PORT_OFFSET		0
#define DRV_MB_PARAM_TRANSCEIVER_PORT_MASK		0x00000003
#define DRV_MB_PARAM_TRANSCEIVER_SIZE_OFFSET		2
#define DRV_MB_PARAM_TRANSCEIVER_SIZE_MASK		0x000000FC
#define DRV_MB_PARAM_TRANSCEIVER_I2C_ADDRESS_OFFSET	8
#define DRV_MB_PARAM_TRANSCEIVER_I2C_ADDRESS_MASK	0x0000FF00
#define DRV_MB_PARAM_TRANSCEIVER_OFFSET_OFFSET		16
#define DRV_MB_PARAM_TRANSCEIVER_OFFSET_MASK		0xFFFF0000

#define DRV_MB_PARAM_GPIO_NUMBER_OFFSET		0
#define DRV_MB_PARAM_GPIO_NUMBER_MASK		0x0000FFFF
#define DRV_MB_PARAM_GPIO_VALUE_OFFSET		16
#define DRV_MB_PARAM_GPIO_VALUE_MASK		0xFFFF0000
#define DRV_MB_PARAM_GPIO_DIRECTION_OFFSET	16
#define DRV_MB_PARAM_GPIO_DIRECTION_MASK	0x00FF0000
#define DRV_MB_PARAM_GPIO_CTRL_OFFSET		24
#define DRV_MB_PARAM_GPIO_CTRL_MASK		0xFF000000

	/* Resource Allocation params - Driver version support*/
#define DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR_MASK	0xFFFF0000
#define DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR_OFFSET		16
#define DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR_MASK	0x0000FFFF
#define DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR_OFFSET		0

#define DRV_MB_PARAM_BIST_UNKNOWN_TEST		0
#define DRV_MB_PARAM_BIST_REGISTER_TEST		1
#define DRV_MB_PARAM_BIST_CLOCK_TEST		2
#define DRV_MB_PARAM_BIST_NVM_TEST_NUM_IMAGES		3
#define DRV_MB_PARAM_BIST_NVM_TEST_IMAGE_BY_INDEX	4

#define DRV_MB_PARAM_BIST_RC_UNKNOWN		0
#define DRV_MB_PARAM_BIST_RC_PASSED		1
#define DRV_MB_PARAM_BIST_RC_FAILED		2
#define DRV_MB_PARAM_BIST_RC_INVALID_PARAMETER		3

#define DRV_MB_PARAM_BIST_TEST_INDEX_OFFSET      0
#define DRV_MB_PARAM_BIST_TEST_INDEX_MASK       0x000000FF
#define DRV_MB_PARAM_BIST_TEST_IMAGE_INDEX_OFFSET      8
#define DRV_MB_PARAM_BIST_TEST_IMAGE_INDEX_MASK       0x0000FF00

#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_MASK      0x0000FFFF
#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_OFFSET    0
#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_SMARTLINQ 0x00000001 /* driver supports SmartLinQ parameter */
#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_EEE       0x00000002 /* driver supports EEE parameter */
#define DRV_MB_PARAM_FEATURE_SUPPORT_FUNC_MASK      0xFFFF0000
#define DRV_MB_PARAM_FEATURE_SUPPORT_FUNC_OFFSET     16
#define DRV_MB_PARAM_FEATURE_SUPPORT_FUNC_VLINK     0x00010000 /* driver supports virtual link parameter */
	/* Driver attributes params */
#define DRV_MB_PARAM_ATTRIBUTE_KEY_OFFSET		 0
#define DRV_MB_PARAM_ATTRIBUTE_KEY_MASK		0x00FFFFFF
#define DRV_MB_PARAM_ATTRIBUTE_CMD_OFFSET		24
#define DRV_MB_PARAM_ATTRIBUTE_CMD_MASK		0xFF000000

	u32 fw_mb_header;
#define FW_MSG_CODE_MASK                        0xffff0000
#define FW_MSG_CODE_UNSUPPORTED			0x00000000
#define FW_MSG_CODE_DRV_LOAD_ENGINE		0x10100000
#define FW_MSG_CODE_DRV_LOAD_PORT               0x10110000
#define FW_MSG_CODE_DRV_LOAD_FUNCTION           0x10120000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_PDA        0x10200000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_HSI_1      0x10210000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_DIAG       0x10220000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_HSI        0x10230000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_REQUIRES_FORCE 0x10300000
#define FW_MSG_CODE_DRV_LOAD_REFUSED_REJECT     0x10310000
#define FW_MSG_CODE_DRV_LOAD_DONE               0x11100000
#define FW_MSG_CODE_DRV_UNLOAD_ENGINE           0x20110000
#define FW_MSG_CODE_DRV_UNLOAD_PORT             0x20120000
#define FW_MSG_CODE_DRV_UNLOAD_FUNCTION         0x20130000
#define FW_MSG_CODE_DRV_UNLOAD_DONE             0x21100000
#define FW_MSG_CODE_INIT_PHY_DONE		0x21200000
#define FW_MSG_CODE_INIT_PHY_ERR_INVALID_ARGS	0x21300000
#define FW_MSG_CODE_LINK_RESET_DONE		0x23000000
#define FW_MSG_CODE_SET_LLDP_DONE               0x24000000
#define FW_MSG_CODE_SET_LLDP_UNSUPPORTED_AGENT  0x24010000
#define FW_MSG_CODE_REGISTER_LLDP_TLVS_RX_DONE  0x24100000
#define FW_MSG_CODE_SET_DCBX_DONE               0x25000000
#define FW_MSG_CODE_UPDATE_CURR_CFG_DONE        0x26000000
#define FW_MSG_CODE_UPDATE_BUS_NUM_DONE         0x27000000
#define FW_MSG_CODE_UPDATE_BOOT_PROGRESS_DONE   0x28000000
#define FW_MSG_CODE_UPDATE_STORM_FW_VER_DONE    0x29000000
#define FW_MSG_CODE_UPDATE_DRIVER_STATE_DONE    0x31000000
#define FW_MSG_CODE_DRV_MSG_CODE_BW_UPDATE_DONE 0x32000000
#define FW_MSG_CODE_DRV_MSG_CODE_MTU_SIZE_DONE  0x33000000
#define FW_MSG_CODE_RESOURCE_ALLOC_OK           0x34000000
#define FW_MSG_CODE_RESOURCE_ALLOC_UNKNOWN      0x35000000
#define FW_MSG_CODE_RESOURCE_ALLOC_DEPRECATED   0x36000000
#define FW_MSG_CODE_RESOURCE_ALLOC_GEN_ERR      0x37000000
#define FW_MSG_CODE_UPDATE_WOL_DONE		0x38000000
#define FW_MSG_CODE_UPDATE_ESWITCH_MODE_DONE	0x39000000
#define FW_MSG_CODE_UPDATE_ERR			0x3a010000
#define FW_MSG_CODE_UPDATE_PARAM_ERR		0x3a020000
#define FW_MSG_CODE_UPDATE_NOT_ALLOWED		0x3a030000
#define FW_MSG_CODE_S_TAG_UPDATE_ACK_DONE	0x3b000000
#define FW_MSG_CODE_UPDATE_FCOE_CVID_DONE	0x3c000000
#define FW_MSG_CODE_UPDATE_FCOE_FABRIC_NAME_DONE	0x3d000000
#define FW_MSG_CODE_UPDATE_BOOT_CFG_DONE	0x3e000000
#define FW_MSG_CODE_RESET_TO_DEFAULT_ACK	0x3f000000
#define FW_MSG_CODE_OV_GET_CURR_CFG_DONE	0x40000000
#define FW_MSG_CODE_GET_OEM_UPDATES_DONE	0x41000000
#define FW_MSG_CODE_GET_LLDP_STATS_DONE		0x42000000
#define FW_MSG_CODE_GET_LLDP_STATS_ERROR	0x42010000

#define FW_MSG_CODE_NIG_DRAIN_DONE              0x30000000
#define FW_MSG_CODE_VF_DISABLED_DONE            0xb0000000
#define FW_MSG_CODE_DRV_CFG_VF_MSIX_DONE        0xb0010000
#define FW_MSG_CODE_FLR_ACK                     0x02000000
#define FW_MSG_CODE_FLR_NACK                    0x02100000
#define FW_MSG_CODE_SET_DRIVER_DONE		0x02200000
#define FW_MSG_CODE_SET_VMAC_SUCCESS            0x02300000
#define FW_MSG_CODE_SET_VMAC_FAIL               0x02400000

#define FW_MSG_CODE_NVM_OK			0x00010000
#define FW_MSG_CODE_NVM_INVALID_MODE		0x00020000
#define FW_MSG_CODE_NVM_PREV_CMD_WAS_NOT_FINISHED	0x00030000
#define FW_MSG_CODE_NVM_FAILED_TO_ALLOCATE_PAGE	0x00040000
#define FW_MSG_CODE_NVM_INVALID_DIR_FOUND	0x00050000
#define FW_MSG_CODE_NVM_PAGE_NOT_FOUND		0x00060000
#define FW_MSG_CODE_NVM_FAILED_PARSING_BNDLE_HEADER 0x00070000
#define FW_MSG_CODE_NVM_FAILED_PARSING_IMAGE_HEADER 0x00080000
#define FW_MSG_CODE_NVM_PARSING_OUT_OF_SYNC  	0x00090000
#define FW_MSG_CODE_NVM_FAILED_UPDATING_DIR 	0x000a0000
#define FW_MSG_CODE_NVM_FAILED_TO_FREE_PAGE 	0x000b0000
#define FW_MSG_CODE_NVM_FILE_NOT_FOUND 		0x000c0000
#define FW_MSG_CODE_NVM_OPERATION_FAILED 	0x000d0000
#define FW_MSG_CODE_NVM_FAILED_UNALIGNED 	0x000e0000
#define FW_MSG_CODE_NVM_BAD_OFFSET	 	0x000f0000
#define FW_MSG_CODE_NVM_BAD_SIGNATURE 		0x00100000
#define FW_MSG_CODE_NVM_FILE_READ_ONLY 		0x00200000
#define FW_MSG_CODE_NVM_UNKNOWN_FILE 		0x00300000
#define FW_MSG_CODE_NVM_PUT_FILE_FINISH_OK	0x00400000
#define FW_MSG_CODE_MCP_RESET_REJECT		0x00600000 /* MFW reject "mcp reset" command if one of the drivers is up */
#define FW_MSG_CODE_NVM_FAILED_CALC_HASH	0x00310000
#define FW_MSG_CODE_NVM_PUBLIC_KEY_MISSING	0x00320000
#define FW_MSG_CODE_NVM_INVALID_PUBLIC_KEY	0x00330000

#define FW_MSG_CODE_PHY_OK			0x00110000
#define FW_MSG_CODE_PHY_ERROR			0x00120000
#define FW_MSG_CODE_SET_SECURE_MODE_ERROR	0x00130000
#define FW_MSG_CODE_SET_SECURE_MODE_OK		0x00140000
#define FW_MSG_MODE_PHY_PRIVILEGE_ERROR		0x00150000
#define FW_MSG_CODE_OK				0x00160000
#define FW_MSG_CODE_ERROR			0x00170000
#define FW_MSG_CODE_LED_MODE_INVALID		0x00170000
#define FW_MSG_CODE_PHY_DIAG_OK			0x00160000
#define FW_MSG_CODE_PHY_DIAG_ERROR		0x00170000
#define FW_MSG_CODE_INIT_HW_FAILED_TO_ALLOCATE_PAGE	0x00040000
#define FW_MSG_CODE_INIT_HW_FAILED_BAD_STATE    0x00170000
#define FW_MSG_CODE_INIT_HW_FAILED_TO_SET_WINDOW 0x000d0000
#define FW_MSG_CODE_INIT_HW_FAILED_NO_IMAGE	0x000c0000
#define FW_MSG_CODE_INIT_HW_FAILED_VERSION_MISMATCH	0x00100000
#define FW_MSG_CODE_TRANSCEIVER_DIAG_OK		0x00160000
#define FW_MSG_CODE_TRANSCEIVER_DIAG_ERROR	0x00170000
#define FW_MSG_CODE_TRANSCEIVER_NOT_PRESENT	0x00020000
#define FW_MSG_CODE_TRANSCEIVER_BAD_BUFFER_SIZE	0x000f0000
#define FW_MSG_CODE_GPIO_OK			0x00160000
#define FW_MSG_CODE_GPIO_DIRECTION_ERR		0x00170000
#define FW_MSG_CODE_GPIO_CTRL_ERR		0x00020000
#define FW_MSG_CODE_GPIO_INVALID	 	0x000f0000
#define FW_MSG_CODE_GPIO_INVALID_VALUE		0x00050000
#define FW_MSG_CODE_BIST_TEST_INVALID	 	0x000f0000
#define FW_MSG_CODE_EXTPHY_INVALID_IMAGE_HEADER	0x00700000
#define FW_MSG_CODE_EXTPHY_INVALID_PHY_TYPE	0x00710000
#define FW_MSG_CODE_EXTPHY_OPERATION_FAILED	0x00720000
#define FW_MSG_CODE_EXTPHY_NO_PHY_DETECTED	0x00730000
#define FW_MSG_CODE_RECOVERY_MODE		0x00740000

	/* mdump related response codes */
#define FW_MSG_CODE_MDUMP_NO_IMAGE_FOUND	0x00010000
#define FW_MSG_CODE_MDUMP_ALLOC_FAILED		0x00020000
#define FW_MSG_CODE_MDUMP_INVALID_CMD		0x00030000
#define FW_MSG_CODE_MDUMP_IN_PROGRESS		0x00040000
#define FW_MSG_CODE_MDUMP_WRITE_FAILED		0x00050000

#define FW_MSG_CODE_OS_WOL_SUPPORTED		0x00800000
#define FW_MSG_CODE_OS_WOL_NOT_SUPPORTED	0x00810000

#define FW_MSG_CODE_WOL_READ_WRITE_OK		0x00820000
#define FW_MSG_CODE_WOL_READ_WRITE_INVALID_VAL	0x00830000
#define FW_MSG_CODE_WOL_READ_WRITE_INVALID_ADDR	0x00840000
#define FW_MSG_CODE_WOL_READ_BUFFER_OK		0x00850000
#define FW_MSG_CODE_WOL_READ_BUFFER_INVALID_VAL	0x00860000


#define FW_MSG_CODE_DRV_CFG_PF_VFS_MSIX_DONE   	 0x00870000
#define FW_MSG_CODE_DRV_CFG_PF_VFS_MSIX_BAD_ASIC 0x00880000

#define FW_MSG_SEQ_NUMBER_MASK                  0x0000ffff

#define FW_MSG_CODE_ATTRIBUTE_INVALID_KEY	0x00020000
#define FW_MSG_CODE_ATTRIBUTE_INVALID_CMD	0x00030000

#define FW_MSG_CODE_IDC_BUSY			0x00010000

	u32 fw_mb_param;
/* Resource Allocation params - MFW version support */
#define FW_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR_MASK	0xFFFF0000
#define FW_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR_OFFSET		16
#define FW_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR_MASK	0x0000FFFF
#define FW_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR_OFFSET		0

/* get pf rdma protocol command response */
#define FW_MB_PARAM_GET_PF_RDMA_NONE		0x0
#define FW_MB_PARAM_GET_PF_RDMA_ROCE		0x1
#define FW_MB_PARAM_GET_PF_RDMA_IWARP		0x2
#define FW_MB_PARAM_GET_PF_RDMA_BOTH		0x3

/* get MFW feature support response */
#define FW_MB_PARAM_FEATURE_SUPPORT_SMARTLINQ    0x00000001 /* MFW supports SmartLinQ */
#define FW_MB_PARAM_FEATURE_SUPPORT_EEE          0x00000002 /* MFW supports EEE */
#define FW_MB_PARAM_FEATURE_SUPPORT_DRV_LOAD_TO  0x00000004 /* MFW supports DRV_LOAD Timeout */
#define FW_MB_PARAM_FEATURE_SUPPORT_LP_PRES_DET	 0x00000008 /* MFW supports early detection of LP Presence */
#define FW_MB_PARAM_FEATURE_SUPPORT_RELAXED_ORD	 0x00000010 /* MFW supports relaxed ordering setting */
#define FW_MB_PARAM_FEATURE_SUPPORT_VLINK        0x00010000 /* MFW supports virtual link */

#define FW_MB_PARAM_LOAD_DONE_DID_EFUSE_ERROR	(1<<0)

#define FW_MB_PARAM_OEM_UPDATE_MASK		0xFF
#define FW_MB_PARAM_OEM_UPDATE_OFFSET		0
#define FW_MB_PARAM_OEM_UPDATE_BW		0x01
#define FW_MB_PARAM_OEM_UPDATE_S_TAG		0x02
#define FW_MB_PARAM_OEM_UPDATE_CFG		0x04

#define FW_MB_PARAM_ENG_CFG_FIR_AFFIN_VALID_MASK   0x00000001
#define FW_MB_PARAM_ENG_CFG_FIR_AFFIN_VALID_OFFSET 0
#define FW_MB_PARAM_ENG_CFG_FIR_AFFIN_VALUE_MASK   0x00000002
#define FW_MB_PARAM_ENG_CFG_FIR_AFFIN_VALUE_OFFSET 1
#define FW_MB_PARAM_ENG_CFG_L2_AFFIN_VALID_MASK    0x00000004
#define FW_MB_PARAM_ENG_CFG_L2_AFFIN_VALID_OFFSET  2
#define FW_MB_PARAM_ENG_CFG_L2_AFFIN_VALUE_MASK    0x00000008
#define FW_MB_PARAM_ENG_CFG_L2_AFFIN_VALUE_OFFSET  3

#define FW_MB_PARAM_PPFID_BITMAP_MASK   0xFF
#define FW_MB_PARAM_PPFID_BITMAP_OFFSET    0

	u32 drv_pulse_mb;
#define DRV_PULSE_SEQ_MASK                      0x00007fff
#define DRV_PULSE_SYSTEM_TIME_MASK              0xffff0000
	/*
	 * The system time is in the format of
	 * (year-2001)*12*32 + month*32 + day.
	 */
#define DRV_PULSE_ALWAYS_ALIVE                  0x00008000
	/*
	 * Indicate to the firmware not to go into the
	 * OS-absent when it is not getting driver pulse.
	 * This is used for debugging as well for PXE(MBA).
	 */

	u32 mcp_pulse_mb;
#define MCP_PULSE_SEQ_MASK                      0x00007fff
#define MCP_PULSE_ALWAYS_ALIVE                  0x00008000
	/* Indicates to the driver not to assert due to lack
	 * of MCP response */
#define MCP_EVENT_MASK                          0xffff0000
#define MCP_EVENT_OTHER_DRIVER_RESET_REQ        0x00010000

	/* The union data is used by the driver to pass parameters to the scratchpad. */
	union drv_union_data union_data;

};

/* MFW - DRV MB */
/**********************************************************************
 * Description
 *   Incremental Aggregative
 *   8-bit MFW counter per message
 *   8-bit ack-counter per message
 * Capabilities
 *   Provides up to 256 aggregative message per type
 *   Provides 4 message types in dword
 *   Message type pointers to byte offset
 *   Backward Compatibility by using sizeof for the counters.
 *   No lock requires for 32bit messages
 * Limitations:
 * In case of messages greater than 32bit, a dedicated mechanism(e.g lock)
 * is required to prevent data corruption.
 **********************************************************************/
enum MFW_DRV_MSG_TYPE {
	MFW_DRV_MSG_LINK_CHANGE,
	MFW_DRV_MSG_FLR_FW_ACK_FAILED,
	MFW_DRV_MSG_VF_DISABLED,
	MFW_DRV_MSG_LLDP_DATA_UPDATED,
	MFW_DRV_MSG_DCBX_REMOTE_MIB_UPDATED,
	MFW_DRV_MSG_DCBX_OPERATIONAL_MIB_UPDATED,
	MFW_DRV_MSG_ERROR_RECOVERY,
	MFW_DRV_MSG_BW_UPDATE,
	MFW_DRV_MSG_S_TAG_UPDATE,
	MFW_DRV_MSG_GET_LAN_STATS,
	MFW_DRV_MSG_GET_FCOE_STATS,
	MFW_DRV_MSG_GET_ISCSI_STATS,
	MFW_DRV_MSG_GET_RDMA_STATS,
	MFW_DRV_MSG_FAILURE_DETECTED,
	MFW_DRV_MSG_TRANSCEIVER_STATE_CHANGE,
	MFW_DRV_MSG_CRITICAL_ERROR_OCCURRED,
	MFW_DRV_MSG_EEE_NEGOTIATION_COMPLETE,
	MFW_DRV_MSG_GET_TLV_REQ,
	MFW_DRV_MSG_OEM_CFG_UPDATE,
	MFW_DRV_MSG_LLDP_RECEIVED_TLVS_UPDATED,
	MFW_DRV_MSG_GENERIC_IDC,	/* Generic Inter Driver Communication message */
	MFW_DRV_MSG_MAX
};

#define MFW_DRV_MSG_MAX_DWORDS(msgs)	(((msgs - 1) >> 2) + 1)
#define MFW_DRV_MSG_DWORD(msg_id)	(msg_id >> 2)
#define MFW_DRV_MSG_OFFSET(msg_id)	((msg_id & 0x3) << 3)
#define MFW_DRV_MSG_MASK(msg_id)	(0xff << MFW_DRV_MSG_OFFSET(msg_id))

#ifdef BIG_ENDIAN		/* Like MFW */
#define DRV_ACK_MSG(msg_p, msg_id) (u8)((u8*)msg_p)[msg_id]++;
#else
#define DRV_ACK_MSG(msg_p, msg_id) (u8)((u8*)msg_p)[((msg_id & ~3) | ((~msg_id) & 3))]++;
#endif

#define MFW_DRV_UPDATE(shmem_func, msg_id)	(u8)((u8*)(MFW_MB_P(shmem_func)->msg))[msg_id]++;

struct public_mfw_mb {
	u32 sup_msgs;       /* Assigend with MFW_DRV_MSG_MAX */
	u32 msg[MFW_DRV_MSG_MAX_DWORDS(MFW_DRV_MSG_MAX)];   /* Incremented by the MFW */
	u32 ack[MFW_DRV_MSG_MAX_DWORDS(MFW_DRV_MSG_MAX)];   /* Incremented by the driver */
};

/**************************************/
/*                                    */
/*     P U B L I C       D A T A      */
/*                                    */
/**************************************/
enum public_sections {
	PUBLIC_DRV_MB,      /* Points to the first drv_mb of path0 */
	PUBLIC_MFW_MB,      /* Points to the first mfw_mb of path0 */
	PUBLIC_GLOBAL,
	PUBLIC_PATH,
	PUBLIC_PORT,
	PUBLIC_FUNC,
	PUBLIC_MAX_SECTIONS
};

struct drv_ver_info_stc {
	u32 ver;
	u8 name[32];
};

/* Runtime data needs about 1/2K. We use 2K to be on the safe side.
 * Please make sure data does not exceed this size.
 */
#define NUM_RUNTIME_DWORDS 16
struct drv_init_hw_stc {
	u32 init_hw_bitmask[NUM_RUNTIME_DWORDS];
	u32 init_hw_data[NUM_RUNTIME_DWORDS * 32];
};

struct mcp_public_data {
	/* The sections fields is an array */
	u32 num_sections;
	offsize_t sections[PUBLIC_MAX_SECTIONS];
	struct public_drv_mb drv_mb[MCP_GLOB_FUNC_MAX];
	struct public_mfw_mb mfw_mb[MCP_GLOB_FUNC_MAX];
	struct public_global global;
	struct public_path path[MCP_GLOB_PATH_MAX];
	struct public_port port[MCP_GLOB_PORT_MAX];
	struct public_func func[MCP_GLOB_FUNC_MAX];
};

#define I2C_TRANSCEIVER_ADDR	0xa0
#define MAX_I2C_TRANSACTION_SIZE	16
#define MAX_I2C_TRANSCEIVER_PAGE_SIZE	256

/* OCBB definitions */
enum tlvs {
	/* Category 1: Device Properties */
	DRV_TLV_CLP_STR,
	DRV_TLV_CLP_STR_CTD,
	/* Category 6: Device Configuration */
	DRV_TLV_SCSI_TO,
	DRV_TLV_R_T_TOV,
	DRV_TLV_R_A_TOV,
	DRV_TLV_E_D_TOV,
	DRV_TLV_CR_TOV,
	DRV_TLV_BOOT_TYPE,
	/* Category 8: Port Configuration */
	DRV_TLV_NPIV_ENABLED,
	/* Category 10: Function Configuration */
	DRV_TLV_FEATURE_FLAGS,
	DRV_TLV_LOCAL_ADMIN_ADDR,
	DRV_TLV_ADDITIONAL_MAC_ADDR_1,
	DRV_TLV_ADDITIONAL_MAC_ADDR_2,
	DRV_TLV_LSO_MAX_OFFLOAD_SIZE,
	DRV_TLV_LSO_MIN_SEGMENT_COUNT,
	DRV_TLV_PROMISCUOUS_MODE,
	DRV_TLV_TX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_RX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_NUM_OF_NET_QUEUE_VMQ_CFG,
	DRV_TLV_FLEX_NIC_OUTER_VLAN_ID,
	DRV_TLV_OS_DRIVER_STATES,
	DRV_TLV_PXE_BOOT_PROGRESS,
	/* Category 12: FC/FCoE Configuration */
	DRV_TLV_NPIV_STATE,
	DRV_TLV_NUM_OF_NPIV_IDS,
	DRV_TLV_SWITCH_NAME,
	DRV_TLV_SWITCH_PORT_NUM,
	DRV_TLV_SWITCH_PORT_ID,
	DRV_TLV_VENDOR_NAME,
	DRV_TLV_SWITCH_MODEL,
	DRV_TLV_SWITCH_FW_VER,
	DRV_TLV_QOS_PRIORITY_PER_802_1P,
	DRV_TLV_PORT_ALIAS,
	DRV_TLV_PORT_STATE,
	DRV_TLV_FIP_TX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_FCOE_RX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_LINK_FAILURE_COUNT,
	DRV_TLV_FCOE_BOOT_PROGRESS,
	/* Category 13: iSCSI Configuration */
	DRV_TLV_TARGET_LLMNR_ENABLED,
	DRV_TLV_HEADER_DIGEST_FLAG_ENABLED,
	DRV_TLV_DATA_DIGEST_FLAG_ENABLED,
	DRV_TLV_AUTHENTICATION_METHOD,
	DRV_TLV_ISCSI_BOOT_TARGET_PORTAL,
	DRV_TLV_MAX_FRAME_SIZE,
	DRV_TLV_PDU_TX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_PDU_RX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_ISCSI_BOOT_PROGRESS,
	/* Category 20: Device Data */
	DRV_TLV_PCIE_BUS_RX_UTILIZATION,
	DRV_TLV_PCIE_BUS_TX_UTILIZATION,
	DRV_TLV_DEVICE_CPU_CORES_UTILIZATION,
	DRV_TLV_LAST_VALID_DCC_TLV_RECEIVED,
	DRV_TLV_NCSI_RX_BYTES_RECEIVED,
	DRV_TLV_NCSI_TX_BYTES_SENT,
	/* Category 22: Base Port Data */
	DRV_TLV_RX_DISCARDS,
	DRV_TLV_RX_ERRORS,
	DRV_TLV_TX_ERRORS,
	DRV_TLV_TX_DISCARDS,
	DRV_TLV_RX_FRAMES_RECEIVED,
	DRV_TLV_TX_FRAMES_SENT,
	/* Category 23: FC/FCoE Port Data */
	DRV_TLV_RX_BROADCAST_PACKETS,
	DRV_TLV_TX_BROADCAST_PACKETS,
	/* Category 28: Base Function Data */
	DRV_TLV_NUM_OFFLOADED_CONNECTIONS_TCP_IPV4,
	DRV_TLV_NUM_OFFLOADED_CONNECTIONS_TCP_IPV6,
	DRV_TLV_TX_DESCRIPTOR_QUEUE_AVG_DEPTH,
	DRV_TLV_RX_DESCRIPTORS_QUEUE_AVG_DEPTH,
	DRV_TLV_PF_RX_FRAMES_RECEIVED,
	DRV_TLV_RX_BYTES_RECEIVED,
	DRV_TLV_PF_TX_FRAMES_SENT,
	DRV_TLV_TX_BYTES_SENT,
	DRV_TLV_IOV_OFFLOAD,
	DRV_TLV_PCI_ERRORS_CAP_ID,
	DRV_TLV_UNCORRECTABLE_ERROR_STATUS,
	DRV_TLV_UNCORRECTABLE_ERROR_MASK,
	DRV_TLV_CORRECTABLE_ERROR_STATUS,
	DRV_TLV_CORRECTABLE_ERROR_MASK,
	DRV_TLV_PCI_ERRORS_AECC_REGISTER,
	DRV_TLV_TX_QUEUES_EMPTY,
	DRV_TLV_RX_QUEUES_EMPTY,
	DRV_TLV_TX_QUEUES_FULL,
	DRV_TLV_RX_QUEUES_FULL,
	/* Category 29: FC/FCoE Function Data */
	DRV_TLV_FCOE_TX_DESCRIPTOR_QUEUE_AVG_DEPTH,
	DRV_TLV_FCOE_RX_DESCRIPTORS_QUEUE_AVG_DEPTH,
	DRV_TLV_FCOE_RX_FRAMES_RECEIVED,
	DRV_TLV_FCOE_RX_BYTES_RECEIVED,
	DRV_TLV_FCOE_TX_FRAMES_SENT,
	DRV_TLV_FCOE_TX_BYTES_SENT,
	DRV_TLV_CRC_ERROR_COUNT,
	DRV_TLV_CRC_ERROR_1_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_1_TIMESTAMP,
	DRV_TLV_CRC_ERROR_2_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_2_TIMESTAMP,
	DRV_TLV_CRC_ERROR_3_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_3_TIMESTAMP,
	DRV_TLV_CRC_ERROR_4_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_4_TIMESTAMP,
	DRV_TLV_CRC_ERROR_5_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_5_TIMESTAMP,
	DRV_TLV_LOSS_OF_SYNC_ERROR_COUNT,
	DRV_TLV_LOSS_OF_SIGNAL_ERRORS,
	DRV_TLV_PRIMITIVE_SEQUENCE_PROTOCOL_ERROR_COUNT,
	DRV_TLV_DISPARITY_ERROR_COUNT,
	DRV_TLV_CODE_VIOLATION_ERROR_COUNT,
	DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_1,
	DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_2,
	DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_3,
	DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_4,
	DRV_TLV_LAST_FLOGI_TIMESTAMP,
	DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_1,
	DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_2,
	DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_3,
	DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_4,
	DRV_TLV_LAST_FLOGI_ACC_TIMESTAMP,
	DRV_TLV_LAST_FLOGI_RJT,
	DRV_TLV_LAST_FLOGI_RJT_TIMESTAMP,
	DRV_TLV_FDISCS_SENT_COUNT,
	DRV_TLV_FDISC_ACCS_RECEIVED,
	DRV_TLV_FDISC_RJTS_RECEIVED,
	DRV_TLV_PLOGI_SENT_COUNT,
	DRV_TLV_PLOGI_ACCS_RECEIVED,
	DRV_TLV_PLOGI_RJTS_RECEIVED,
	DRV_TLV_PLOGI_1_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_1_TIMESTAMP,
	DRV_TLV_PLOGI_2_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_2_TIMESTAMP,
	DRV_TLV_PLOGI_3_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_3_TIMESTAMP,
	DRV_TLV_PLOGI_4_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_4_TIMESTAMP,
	DRV_TLV_PLOGI_5_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_5_TIMESTAMP,
	DRV_TLV_PLOGI_1_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_1_ACC_TIMESTAMP,
	DRV_TLV_PLOGI_2_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_2_ACC_TIMESTAMP,
	DRV_TLV_PLOGI_3_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_3_ACC_TIMESTAMP,
	DRV_TLV_PLOGI_4_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_4_ACC_TIMESTAMP,
	DRV_TLV_PLOGI_5_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_5_ACC_TIMESTAMP,
	DRV_TLV_LOGOS_ISSUED,
	DRV_TLV_LOGO_ACCS_RECEIVED,
	DRV_TLV_LOGO_RJTS_RECEIVED,
	DRV_TLV_LOGO_1_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_1_TIMESTAMP,
	DRV_TLV_LOGO_2_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_2_TIMESTAMP,
	DRV_TLV_LOGO_3_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_3_TIMESTAMP,
	DRV_TLV_LOGO_4_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_4_TIMESTAMP,
	DRV_TLV_LOGO_5_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_5_TIMESTAMP,
	DRV_TLV_LOGOS_RECEIVED,
	DRV_TLV_ACCS_ISSUED,
	DRV_TLV_PRLIS_ISSUED,
	DRV_TLV_ACCS_RECEIVED,
	DRV_TLV_ABTS_SENT_COUNT,
	DRV_TLV_ABTS_ACCS_RECEIVED,
	DRV_TLV_ABTS_RJTS_RECEIVED,
	DRV_TLV_ABTS_1_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_1_TIMESTAMP,
	DRV_TLV_ABTS_2_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_2_TIMESTAMP,
	DRV_TLV_ABTS_3_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_3_TIMESTAMP,
	DRV_TLV_ABTS_4_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_4_TIMESTAMP,
	DRV_TLV_ABTS_5_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_5_TIMESTAMP,
	DRV_TLV_RSCNS_RECEIVED,
	DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_1,
	DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_2,
	DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_3,
	DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_4,
	DRV_TLV_LUN_RESETS_ISSUED,
	DRV_TLV_ABORT_TASK_SETS_ISSUED,
	DRV_TLV_TPRLOS_SENT,
	DRV_TLV_NOS_SENT_COUNT,
	DRV_TLV_NOS_RECEIVED_COUNT,
	DRV_TLV_OLS_COUNT,
	DRV_TLV_LR_COUNT,
	DRV_TLV_LRR_COUNT,
	DRV_TLV_LIP_SENT_COUNT,
	DRV_TLV_LIP_RECEIVED_COUNT,
	DRV_TLV_EOFA_COUNT,
	DRV_TLV_EOFNI_COUNT,
	DRV_TLV_SCSI_STATUS_CHECK_CONDITION_COUNT,
	DRV_TLV_SCSI_STATUS_CONDITION_MET_COUNT,
	DRV_TLV_SCSI_STATUS_BUSY_COUNT,
	DRV_TLV_SCSI_STATUS_INTERMEDIATE_COUNT,
	DRV_TLV_SCSI_STATUS_INTERMEDIATE_CONDITION_MET_COUNT,
	DRV_TLV_SCSI_STATUS_RESERVATION_CONFLICT_COUNT,
	DRV_TLV_SCSI_STATUS_TASK_SET_FULL_COUNT,
	DRV_TLV_SCSI_STATUS_ACA_ACTIVE_COUNT,
	DRV_TLV_SCSI_STATUS_TASK_ABORTED_COUNT,
	DRV_TLV_SCSI_CHECK_CONDITION_1_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_1_TIMESTAMP,
	DRV_TLV_SCSI_CHECK_CONDITION_2_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_2_TIMESTAMP,
	DRV_TLV_SCSI_CHECK_CONDITION_3_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_3_TIMESTAMP,
	DRV_TLV_SCSI_CHECK_CONDITION_4_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_4_TIMESTAMP,
	DRV_TLV_SCSI_CHECK_CONDITION_5_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_5_TIMESTAMP,
	/* Category 30: iSCSI Function Data */
	DRV_TLV_PDU_TX_DESCRIPTOR_QUEUE_AVG_DEPTH,
	DRV_TLV_PDU_RX_DESCRIPTORS_QUEUE_AVG_DEPTH,
	DRV_TLV_ISCSI_PDU_RX_FRAMES_RECEIVED,
	DRV_TLV_ISCSI_PDU_RX_BYTES_RECEIVED,
	DRV_TLV_ISCSI_PDU_TX_FRAMES_SENT,
	DRV_TLV_ISCSI_PDU_TX_BYTES_SENT
};

#define I2C_DEV_ADDR_A2                   0xa2
#define SFP_EEPROM_A2_TEMPERATURE_ADDR            0x60
#define SFP_EEPROM_A2_TEMPERATURE_SIZE            2
#define SFP_EEPROM_A2_VCC_ADDR                    0x62
#define SFP_EEPROM_A2_VCC_SIZE                    2
#define SFP_EEPROM_A2_TX_BIAS_ADDR                0x64
#define SFP_EEPROM_A2_TX_BIAS_SIZE                2
#define SFP_EEPROM_A2_TX_POWER_ADDR               0x66
#define SFP_EEPROM_A2_TX_POWER_SIZE               2
#define SFP_EEPROM_A2_RX_POWER_ADDR               0x68
#define SFP_EEPROM_A2_RX_POWER_SIZE               2

#define I2C_DEV_ADDR_A0                   0xa0
#define QSFP_EEPROM_A0_TEMPERATURE_ADDR            0x16
#define QSFP_EEPROM_A0_TEMPERATURE_SIZE            2
#define QSFP_EEPROM_A0_VCC_ADDR                    0x1a
#define QSFP_EEPROM_A0_VCC_SIZE                    2
#define QSFP_EEPROM_A0_TX1_BIAS_ADDR               0x2a
#define QSFP_EEPROM_A0_TX1_BIAS_SIZE               2
#define QSFP_EEPROM_A0_TX1_POWER_ADDR              0x32
#define QSFP_EEPROM_A0_TX1_POWER_SIZE              2
#define QSFP_EEPROM_A0_RX1_POWER_ADDR              0x22
#define QSFP_EEPROM_A0_RX1_POWER_SIZE              2

/**************************************
 *     eDiag NETWORK Mode (DON)
 **************************************/

#define ETH_DON_TYPE           0x0911    /* NETWORK Mode for QeDiag */
#define ETH_DON_TRACE_TYPE    0x0912    /* NETWORK Mode Continous Trace */

#define	DON_RESP_UNKNOWN_CMD_ID	    0x10	/* Response Error */

/* Op Codes, Response is Op Code+1 */

#define	DON_REG_READ_REQ_CMD_ID			0x11
#define	DON_REG_WRITE_REQ_CMD_ID		0x22
#define	DON_CHALLENGE_REQ_CMD_ID		0x33
#define	DON_NVM_READ_REQ_CMD_ID			0x44
#define	DON_BLOCK_READ_REQ_CMD_ID		0x55

#define	DON_MFW_MODE_TRACE_CONTINUOUS_ID	0x70

#if defined(MFW) || defined(DIAG) || defined(WINEDIAG)

#ifndef UEFI
#if defined(_MSC_VER)
#pragma pack(push,1)
#else
#pragma pack(1)
#endif
#endif

typedef struct {
	u8 dst_addr[6];
	u8 src_addr[6];
	u16 ether_type;

	/* DON Message data starts here, after L2 header 		*/
	/* Do not change alignment to keep backward compatability	*/
	u16 cmd_id;			/* Op code and response code */

	union {
		struct {		/* DON Commands */
			u32 address;
			u32 val;
			u32 resp_status;
		};
		struct {		/* DON Traces	*/
			u16 mcp_clock;		/* MCP Clock in MHz		*/
			u16 trace_size;		/* Trace size in bytes		*/

			u32 seconds;		/* Seconds since last reset     */
			u32 ticks;		/* Timestamp (NOW)		*/
		};
	};
	union {
		u8 digest[32]; /* SHA256 */
		u8 data[32];
		/* u32 dword[8]; */
	};
} don_packet_t;

#ifndef UEFI
#if defined(_MSC_VER)
#pragma pack(pop)
#else
#pragma pack(0)
#endif
#endif		/* #ifndef UEFI */

#endif		/* #if defined(MFW) || defined(DIAG) || defined(WINEDIAG) */

#endif				/* MCP_PUBLIC_H */
