/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef __ETH_COMMON__
#define __ETH_COMMON__

/********************/
/* ETH FW CONSTANTS */
/********************/
#define ETH_CACHE_LINE_SIZE                 64

#define ETH_MAX_RAMROD_PER_CON                          8
#define ETH_TX_BD_PAGE_SIZE_BYTES                       4096
#define ETH_RX_BD_PAGE_SIZE_BYTES                       4096
#define ETH_RX_SGE_PAGE_SIZE_BYTES                      4096
#define ETH_RX_CQE_PAGE_SIZE_BYTES                      4096
#define ETH_RX_NUM_NEXT_PAGE_BDS                        2
#define ETH_RX_NUM_NEXT_PAGE_SGES                       2

#define ETH_TX_MIN_BDS_PER_NON_LSO_PKT                          1
#define ETH_TX_MAX_BDS_PER_NON_LSO_PACKET                       18
#define ETH_TX_MAX_LSO_HDR_NBD                                          4
#define ETH_TX_MIN_BDS_PER_LSO_PKT                                      3
#define ETH_TX_MIN_BDS_PER_TUNN_IPV6_WITH_EXT_PKT       3
#define ETH_TX_MIN_BDS_PER_IPV6_WITH_EXT_PKT            2
#define ETH_TX_MIN_BDS_PER_PKT_W_LOOPBACK_MODE          2
#define ETH_TX_MAX_NON_LSO_PKT_LEN                  (9700 - (4 + 12 + 8))
#define ETH_TX_MAX_LSO_HDR_BYTES                    510

#define ETH_NUM_STATISTIC_COUNTERS                      MAX_NUM_VPORTS

#define ETH_REG_CQE_PBL_SIZE                3

/* num of MAC/VLAN filters */
#define ETH_NUM_MAC_FILTERS                                     512
#define ETH_NUM_VLAN_FILTERS                            512

/* approx. multicast constants */
#define ETH_MULTICAST_BIN_FROM_MAC_SEED     0
#define ETH_MULTICAST_MAC_BINS                          256
#define ETH_MULTICAST_MAC_BINS_IN_REGS          (ETH_MULTICAST_MAC_BINS / 32)

/*  ethernet vport update constants */
#define ETH_FILTER_RULES_COUNT                          10
#define ETH_RSS_IND_TABLE_ENTRIES_NUM           128
#define ETH_RSS_KEY_SIZE_REGS                       10
#define ETH_RSS_ENGINE_NUM_K2               207
#define ETH_RSS_ENGINE_NUM_BB               127

/* TPA constants */
#define ETH_TPA_MAX_AGGS_NUM              64
#define ETH_TPA_CQE_START_SGL_SIZE        3
#define ETH_TPA_CQE_CONT_SGL_SIZE         6
#define ETH_TPA_CQE_END_SGL_SIZE          4

/* Queue Zone sizes */
#define TSTORM_QZONE_SIZE    0
#define MSTORM_QZONE_SIZE    sizeof(struct mstorm_eth_queue_zone)
#define USTORM_QZONE_SIZE    sizeof(struct ustorm_eth_queue_zone)
#define XSTORM_QZONE_SIZE    0
#define YSTORM_QZONE_SIZE    sizeof(struct ystorm_eth_queue_zone)
#define PSTORM_QZONE_SIZE    0

/* Interrupt coalescing TimeSet */
struct coalescing_timeset {
	u8	timeset;
	u8	valid;
};

struct eth_tx_1st_bd_flags {
	u8 bitfields;
#define ETH_TX_1ST_BD_FLAGS_FORCE_VLAN_MODE_MASK  0x1
#define ETH_TX_1ST_BD_FLAGS_FORCE_VLAN_MODE_SHIFT 0
#define ETH_TX_1ST_BD_FLAGS_IP_CSUM_MASK          0x1
#define ETH_TX_1ST_BD_FLAGS_IP_CSUM_SHIFT         1
#define ETH_TX_1ST_BD_FLAGS_L4_CSUM_MASK          0x1
#define ETH_TX_1ST_BD_FLAGS_L4_CSUM_SHIFT         2
#define ETH_TX_1ST_BD_FLAGS_VLAN_INSERTION_MASK   0x1
#define ETH_TX_1ST_BD_FLAGS_VLAN_INSERTION_SHIFT  3
#define ETH_TX_1ST_BD_FLAGS_LSO_MASK              0x1
#define ETH_TX_1ST_BD_FLAGS_LSO_SHIFT             4
#define ETH_TX_1ST_BD_FLAGS_START_BD_MASK         0x1
#define ETH_TX_1ST_BD_FLAGS_START_BD_SHIFT        5
#define ETH_TX_1ST_BD_FLAGS_TUNN_IP_CSUM_MASK     0x1
#define ETH_TX_1ST_BD_FLAGS_TUNN_IP_CSUM_SHIFT    6
#define ETH_TX_1ST_BD_FLAGS_TUNN_L4_CSUM_MASK     0x1
#define ETH_TX_1ST_BD_FLAGS_TUNN_L4_CSUM_SHIFT    7
};

/* The parsing information data fo rthe first tx bd of a given packet. */
struct eth_tx_data_1st_bd {
	__le16				vlan;
	u8				nbds;
	struct eth_tx_1st_bd_flags	bd_flags;
	__le16				fw_use_only;
};

/* The parsing information data for the second tx bd of a given packet. */
struct eth_tx_data_2nd_bd {
	__le16	tunn_ip_size;
	__le16	bitfields;
#define ETH_TX_DATA_2ND_BD_L4_HDR_START_OFFSET_W_MASK     0x1FFF
#define ETH_TX_DATA_2ND_BD_L4_HDR_START_OFFSET_W_SHIFT    0
#define ETH_TX_DATA_2ND_BD_RESERVED0_MASK                 0x7
#define ETH_TX_DATA_2ND_BD_RESERVED0_SHIFT                13
	__le16	bitfields2;
#define ETH_TX_DATA_2ND_BD_TUNN_INNER_L2_HDR_SIZE_W_MASK  0xF
#define ETH_TX_DATA_2ND_BD_TUNN_INNER_L2_HDR_SIZE_W_SHIFT 0
#define ETH_TX_DATA_2ND_BD_TUNN_INNER_ETH_TYPE_MASK       0x3
#define ETH_TX_DATA_2ND_BD_TUNN_INNER_ETH_TYPE_SHIFT      4
#define ETH_TX_DATA_2ND_BD_DEST_PORT_MODE_MASK            0x3
#define ETH_TX_DATA_2ND_BD_DEST_PORT_MODE_SHIFT           6
#define ETH_TX_DATA_2ND_BD_TUNN_TYPE_MASK                 0x3
#define ETH_TX_DATA_2ND_BD_TUNN_TYPE_SHIFT                8
#define ETH_TX_DATA_2ND_BD_TUNN_INNER_IPV6_MASK           0x1
#define ETH_TX_DATA_2ND_BD_TUNN_INNER_IPV6_SHIFT          10
#define ETH_TX_DATA_2ND_BD_IPV6_EXT_MASK                  0x1
#define ETH_TX_DATA_2ND_BD_IPV6_EXT_SHIFT                 11
#define ETH_TX_DATA_2ND_BD_TUNN_IPV6_EXT_MASK             0x1
#define ETH_TX_DATA_2ND_BD_TUNN_IPV6_EXT_SHIFT            12
#define ETH_TX_DATA_2ND_BD_L4_UDP_MASK                    0x1
#define ETH_TX_DATA_2ND_BD_L4_UDP_SHIFT                   13
#define ETH_TX_DATA_2ND_BD_L4_PSEUDO_CSUM_MODE_MASK       0x1
#define ETH_TX_DATA_2ND_BD_L4_PSEUDO_CSUM_MODE_SHIFT      14
#define ETH_TX_DATA_2ND_BD_RESERVED1_MASK                 0x1
#define ETH_TX_DATA_2ND_BD_RESERVED1_SHIFT                15
};

/* Regular ETH Rx FP CQE. */
struct eth_fast_path_rx_reg_cqe {
	u8	type;
	u8	bitfields;
#define ETH_FAST_PATH_RX_REG_CQE_RSS_HASH_TYPE_MASK  0x7
#define ETH_FAST_PATH_RX_REG_CQE_RSS_HASH_TYPE_SHIFT 0
#define ETH_FAST_PATH_RX_REG_CQE_TC_MASK             0xF
#define ETH_FAST_PATH_RX_REG_CQE_TC_SHIFT            3
#define ETH_FAST_PATH_RX_REG_CQE_RESERVED0_MASK      0x1
#define ETH_FAST_PATH_RX_REG_CQE_RESERVED0_SHIFT     7
	__le16				pkt_len;
	struct parsing_and_err_flags	pars_flags;
	__le16				vlan_tag;
	__le32				rss_hash;
	__le16				len_on_bd;
	u8				placement_offset;
	u8				reserved;
	__le16				pbl[ETH_REG_CQE_PBL_SIZE];
	u8				reserved1[10];
};

/* The L4 pseudo checksum mode for Ethernet */
enum eth_l4_pseudo_checksum_mode {
	ETH_L4_PSEUDO_CSUM_CORRECT_LENGTH,
	ETH_L4_PSEUDO_CSUM_ZERO_LENGTH,
	MAX_ETH_L4_PSEUDO_CHECKSUM_MODE
};

struct eth_rx_bd {
	struct regpair addr;
};

/* regular ETH Rx SP CQE */
struct eth_slow_path_rx_cqe {
	u8	type;
	u8	ramrod_cmd_id;
	u8	error_flag;
	u8	reserved[27];
	__le16	echo;
};

/* union for all ETH Rx CQE types */
union eth_rx_cqe {
	struct eth_fast_path_rx_reg_cqe		fast_path_regular;
	struct eth_slow_path_rx_cqe		slow_path;
};

/* ETH Rx CQE type */
enum eth_rx_cqe_type {
	ETH_RX_CQE_TYPE_UNUSED,
	ETH_RX_CQE_TYPE_REGULAR,
	ETH_RX_CQE_TYPE_SLOW_PATH,
	MAX_ETH_RX_CQE_TYPE
};

/* ETH Rx producers data */
struct eth_rx_prod_data {
	__le16	bd_prod;
	__le16	sge_prod;
	__le16	cqe_prod;
	__le16	reserved;
};

/* The first tx bd of a given packet */
struct eth_tx_1st_bd {
	struct regpair			addr;
	__le16				nbytes;
	struct eth_tx_data_1st_bd	data;
};

/* The second tx bd of a given packet */
struct eth_tx_2nd_bd {
	struct regpair			addr;
	__le16				nbytes;
	struct eth_tx_data_2nd_bd	data;
};

/* The parsing information data for the third tx bd of a given packet. */
struct eth_tx_data_3rd_bd {
	__le16	lso_mss;
	u8	bitfields;
#define ETH_TX_DATA_3RD_BD_TCP_HDR_LEN_DW_MASK  0xF
#define ETH_TX_DATA_3RD_BD_TCP_HDR_LEN_DW_SHIFT 0
#define ETH_TX_DATA_3RD_BD_HDR_NBD_MASK         0xF
#define ETH_TX_DATA_3RD_BD_HDR_NBD_SHIFT        4
	u8	resereved0[3];
};

/* The third tx bd of a given packet */
struct eth_tx_3rd_bd {
	struct regpair			addr;
	__le16				nbytes;
	struct eth_tx_data_3rd_bd	data;
};

/* The common non-special TX BD ring element */
struct eth_tx_bd {
	struct regpair	addr;
	__le16		nbytes;
	__le16		reserved0;
	__le32		reserved1;
};

union eth_tx_bd_types {
	struct eth_tx_1st_bd	first_bd;
	struct eth_tx_2nd_bd	second_bd;
	struct eth_tx_3rd_bd	third_bd;
	struct eth_tx_bd	reg_bd;
};

/* Mstorm Queue Zone */
struct mstorm_eth_queue_zone {
	struct eth_rx_prod_data rx_producers;
	__le32			reserved[2];
};

/* Ustorm Queue Zone */
struct ustorm_eth_queue_zone {
	struct coalescing_timeset	int_coalescing_timeset;
	__le16				reserved[3];
};

/* Ystorm Queue Zone */
struct ystorm_eth_queue_zone {
	struct coalescing_timeset	int_coalescing_timeset;
	__le16				reserved[3];
};

/* ETH doorbell data */
struct eth_db_data {
	u8 params;
#define ETH_DB_DATA_DEST_MASK         0x3
#define ETH_DB_DATA_DEST_SHIFT        0
#define ETH_DB_DATA_AGG_CMD_MASK      0x3
#define ETH_DB_DATA_AGG_CMD_SHIFT     2
#define ETH_DB_DATA_BYPASS_EN_MASK    0x1
#define ETH_DB_DATA_BYPASS_EN_SHIFT   4
#define ETH_DB_DATA_RESERVED_MASK     0x1
#define ETH_DB_DATA_RESERVED_SHIFT    5
#define ETH_DB_DATA_AGG_VAL_SEL_MASK  0x3
#define ETH_DB_DATA_AGG_VAL_SEL_SHIFT 6
	u8	agg_flags;
	__le16	bd_prod;
};

#endif /* __ETH_COMMON__ */
