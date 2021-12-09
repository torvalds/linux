/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/* Copyright (c) 2017 Microsemi Corporation
 */

#ifndef _SOC_MSCC_OCELOT_H
#define _SOC_MSCC_OCELOT_H

#include <linux/ptp_clock_kernel.h>
#include <linux/net_tstamp.h>
#include <linux/if_vlan.h>
#include <linux/regmap.h>
#include <net/dsa.h>

/* Port Group IDs (PGID) are masks of destination ports.
 *
 * For L2 forwarding, the switch performs 3 lookups in the PGID table for each
 * frame, and forwards the frame to the ports that are present in the logical
 * AND of all 3 PGIDs.
 *
 * These PGID lookups are:
 * - In one of PGID[0-63]: for the destination masks. There are 2 paths by
 *   which the switch selects a destination PGID:
 *     - The {DMAC, VID} is present in the MAC table. In that case, the
 *       destination PGID is given by the DEST_IDX field of the MAC table entry
 *       that matched.
 *     - The {DMAC, VID} is not present in the MAC table (it is unknown). The
 *       frame is disseminated as being either unicast, multicast or broadcast,
 *       and according to that, the destination PGID is chosen as being the
 *       value contained by ANA_FLOODING_FLD_UNICAST,
 *       ANA_FLOODING_FLD_MULTICAST or ANA_FLOODING_FLD_BROADCAST.
 *   The destination PGID can be an unicast set: the first PGIDs, 0 to
 *   ocelot->num_phys_ports - 1, or a multicast set: the PGIDs from
 *   ocelot->num_phys_ports to 63. By convention, a unicast PGID corresponds to
 *   a physical port and has a single bit set in the destination ports mask:
 *   that corresponding to the port number itself. In contrast, a multicast
 *   PGID will have potentially more than one single bit set in the destination
 *   ports mask.
 * - In one of PGID[64-79]: for the aggregation mask. The switch classifier
 *   dissects each frame and generates a 4-bit Link Aggregation Code which is
 *   used for this second PGID table lookup. The goal of link aggregation is to
 *   hash multiple flows within the same LAG on to different destination ports.
 *   The first lookup will result in a PGID with all the LAG members present in
 *   the destination ports mask, and the second lookup, by Link Aggregation
 *   Code, will ensure that each flow gets forwarded only to a single port out
 *   of that mask (there are no duplicates).
 * - In one of PGID[80-90]: for the source mask. The third time, the PGID table
 *   is indexed with the ingress port (plus 80). These PGIDs answer the
 *   question "is port i allowed to forward traffic to port j?" If yes, then
 *   BIT(j) of PGID 80+i will be found set. The third PGID lookup can be used
 *   to enforce the L2 forwarding matrix imposed by e.g. a Linux bridge.
 */

/* Reserve some destination PGIDs at the end of the range:
 * PGID_BLACKHOLE: used for not forwarding the frames
 * PGID_CPU: used for whitelisting certain MAC addresses, such as the addresses
 *           of the switch port net devices, towards the CPU port module.
 * PGID_UC: the flooding destinations for unknown unicast traffic.
 * PGID_MC: the flooding destinations for non-IP multicast traffic.
 * PGID_MCIPV4: the flooding destinations for IPv4 multicast traffic.
 * PGID_MCIPV6: the flooding destinations for IPv6 multicast traffic.
 * PGID_BC: the flooding destinations for broadcast traffic.
 */
#define PGID_BLACKHOLE			57
#define PGID_CPU			58
#define PGID_UC				59
#define PGID_MC				60
#define PGID_MCIPV4			61
#define PGID_MCIPV6			62
#define PGID_BC				63

#define for_each_unicast_dest_pgid(ocelot, pgid)		\
	for ((pgid) = 0;					\
	     (pgid) < (ocelot)->num_phys_ports;			\
	     (pgid)++)

#define for_each_nonreserved_multicast_dest_pgid(ocelot, pgid)	\
	for ((pgid) = (ocelot)->num_phys_ports + 1;		\
	     (pgid) < PGID_BLACKHOLE;				\
	     (pgid)++)

#define for_each_aggr_pgid(ocelot, pgid)			\
	for ((pgid) = PGID_AGGR;				\
	     (pgid) < PGID_SRC;					\
	     (pgid)++)

/* Aggregation PGIDs, one per Link Aggregation Code */
#define PGID_AGGR			64

/* Source PGIDs, one per physical port */
#define PGID_SRC			80

#define OCELOT_NUM_TC			8

#define OCELOT_SPEED_2500		0
#define OCELOT_SPEED_1000		1
#define OCELOT_SPEED_100		2
#define OCELOT_SPEED_10			3

#define OCELOT_PTP_PINS_NUM		4

#define TARGET_OFFSET			24
#define REG_MASK			GENMASK(TARGET_OFFSET - 1, 0)
#define REG(reg, offset)		[reg & REG_MASK] = offset

#define REG_RESERVED_ADDR		0xffffffff
#define REG_RESERVED(reg)		REG(reg, REG_RESERVED_ADDR)

#define OCELOT_MRP_CPUQ			7

enum ocelot_target {
	ANA = 1,
	QS,
	QSYS,
	REW,
	SYS,
	S0,
	S1,
	S2,
	HSIO,
	PTP,
	GCB,
	DEV_GMII,
	TARGET_MAX,
};

enum ocelot_reg {
	ANA_ADVLEARN = ANA << TARGET_OFFSET,
	ANA_VLANMASK,
	ANA_PORT_B_DOMAIN,
	ANA_ANAGEFIL,
	ANA_ANEVENTS,
	ANA_STORMLIMIT_BURST,
	ANA_STORMLIMIT_CFG,
	ANA_ISOLATED_PORTS,
	ANA_COMMUNITY_PORTS,
	ANA_AUTOAGE,
	ANA_MACTOPTIONS,
	ANA_LEARNDISC,
	ANA_AGENCTRL,
	ANA_MIRRORPORTS,
	ANA_EMIRRORPORTS,
	ANA_FLOODING,
	ANA_FLOODING_IPMC,
	ANA_SFLOW_CFG,
	ANA_PORT_MODE,
	ANA_CUT_THRU_CFG,
	ANA_PGID_PGID,
	ANA_TABLES_ANMOVED,
	ANA_TABLES_MACHDATA,
	ANA_TABLES_MACLDATA,
	ANA_TABLES_STREAMDATA,
	ANA_TABLES_MACACCESS,
	ANA_TABLES_MACTINDX,
	ANA_TABLES_VLANACCESS,
	ANA_TABLES_VLANTIDX,
	ANA_TABLES_ISDXACCESS,
	ANA_TABLES_ISDXTIDX,
	ANA_TABLES_ENTRYLIM,
	ANA_TABLES_PTP_ID_HIGH,
	ANA_TABLES_PTP_ID_LOW,
	ANA_TABLES_STREAMACCESS,
	ANA_TABLES_STREAMTIDX,
	ANA_TABLES_SEQ_HISTORY,
	ANA_TABLES_SEQ_MASK,
	ANA_TABLES_SFID_MASK,
	ANA_TABLES_SFIDACCESS,
	ANA_TABLES_SFIDTIDX,
	ANA_MSTI_STATE,
	ANA_OAM_UPM_LM_CNT,
	ANA_SG_ACCESS_CTRL,
	ANA_SG_CONFIG_REG_1,
	ANA_SG_CONFIG_REG_2,
	ANA_SG_CONFIG_REG_3,
	ANA_SG_CONFIG_REG_4,
	ANA_SG_CONFIG_REG_5,
	ANA_SG_GCL_GS_CONFIG,
	ANA_SG_GCL_TI_CONFIG,
	ANA_SG_STATUS_REG_1,
	ANA_SG_STATUS_REG_2,
	ANA_SG_STATUS_REG_3,
	ANA_PORT_VLAN_CFG,
	ANA_PORT_DROP_CFG,
	ANA_PORT_QOS_CFG,
	ANA_PORT_VCAP_CFG,
	ANA_PORT_VCAP_S1_KEY_CFG,
	ANA_PORT_VCAP_S2_CFG,
	ANA_PORT_PCP_DEI_MAP,
	ANA_PORT_CPU_FWD_CFG,
	ANA_PORT_CPU_FWD_BPDU_CFG,
	ANA_PORT_CPU_FWD_GARP_CFG,
	ANA_PORT_CPU_FWD_CCM_CFG,
	ANA_PORT_PORT_CFG,
	ANA_PORT_POL_CFG,
	ANA_PORT_PTP_CFG,
	ANA_PORT_PTP_DLY1_CFG,
	ANA_PORT_PTP_DLY2_CFG,
	ANA_PORT_SFID_CFG,
	ANA_PFC_PFC_CFG,
	ANA_PFC_PFC_TIMER,
	ANA_IPT_OAM_MEP_CFG,
	ANA_IPT_IPT,
	ANA_PPT_PPT,
	ANA_FID_MAP_FID_MAP,
	ANA_AGGR_CFG,
	ANA_CPUQ_CFG,
	ANA_CPUQ_CFG2,
	ANA_CPUQ_8021_CFG,
	ANA_DSCP_CFG,
	ANA_DSCP_REWR_CFG,
	ANA_VCAP_RNG_TYPE_CFG,
	ANA_VCAP_RNG_VAL_CFG,
	ANA_VRAP_CFG,
	ANA_VRAP_HDR_DATA,
	ANA_VRAP_HDR_MASK,
	ANA_DISCARD_CFG,
	ANA_FID_CFG,
	ANA_POL_PIR_CFG,
	ANA_POL_CIR_CFG,
	ANA_POL_MODE_CFG,
	ANA_POL_PIR_STATE,
	ANA_POL_CIR_STATE,
	ANA_POL_STATE,
	ANA_POL_FLOWC,
	ANA_POL_HYST,
	ANA_POL_MISC_CFG,
	QS_XTR_GRP_CFG = QS << TARGET_OFFSET,
	QS_XTR_RD,
	QS_XTR_FRM_PRUNING,
	QS_XTR_FLUSH,
	QS_XTR_DATA_PRESENT,
	QS_XTR_CFG,
	QS_INJ_GRP_CFG,
	QS_INJ_WR,
	QS_INJ_CTRL,
	QS_INJ_STATUS,
	QS_INJ_ERR,
	QS_INH_DBG,
	QSYS_PORT_MODE = QSYS << TARGET_OFFSET,
	QSYS_SWITCH_PORT_MODE,
	QSYS_STAT_CNT_CFG,
	QSYS_EEE_CFG,
	QSYS_EEE_THRES,
	QSYS_IGR_NO_SHARING,
	QSYS_EGR_NO_SHARING,
	QSYS_SW_STATUS,
	QSYS_EXT_CPU_CFG,
	QSYS_PAD_CFG,
	QSYS_CPU_GROUP_MAP,
	QSYS_QMAP,
	QSYS_ISDX_SGRP,
	QSYS_TIMED_FRAME_ENTRY,
	QSYS_TFRM_MISC,
	QSYS_TFRM_PORT_DLY,
	QSYS_TFRM_TIMER_CFG_1,
	QSYS_TFRM_TIMER_CFG_2,
	QSYS_TFRM_TIMER_CFG_3,
	QSYS_TFRM_TIMER_CFG_4,
	QSYS_TFRM_TIMER_CFG_5,
	QSYS_TFRM_TIMER_CFG_6,
	QSYS_TFRM_TIMER_CFG_7,
	QSYS_TFRM_TIMER_CFG_8,
	QSYS_RED_PROFILE,
	QSYS_RES_QOS_MODE,
	QSYS_RES_CFG,
	QSYS_RES_STAT,
	QSYS_EGR_DROP_MODE,
	QSYS_EQ_CTRL,
	QSYS_EVENTS_CORE,
	QSYS_QMAXSDU_CFG_0,
	QSYS_QMAXSDU_CFG_1,
	QSYS_QMAXSDU_CFG_2,
	QSYS_QMAXSDU_CFG_3,
	QSYS_QMAXSDU_CFG_4,
	QSYS_QMAXSDU_CFG_5,
	QSYS_QMAXSDU_CFG_6,
	QSYS_QMAXSDU_CFG_7,
	QSYS_PREEMPTION_CFG,
	QSYS_CIR_CFG,
	QSYS_EIR_CFG,
	QSYS_SE_CFG,
	QSYS_SE_DWRR_CFG,
	QSYS_SE_CONNECT,
	QSYS_SE_DLB_SENSE,
	QSYS_CIR_STATE,
	QSYS_EIR_STATE,
	QSYS_SE_STATE,
	QSYS_HSCH_MISC_CFG,
	QSYS_TAG_CONFIG,
	QSYS_TAS_PARAM_CFG_CTRL,
	QSYS_PORT_MAX_SDU,
	QSYS_PARAM_CFG_REG_1,
	QSYS_PARAM_CFG_REG_2,
	QSYS_PARAM_CFG_REG_3,
	QSYS_PARAM_CFG_REG_4,
	QSYS_PARAM_CFG_REG_5,
	QSYS_GCL_CFG_REG_1,
	QSYS_GCL_CFG_REG_2,
	QSYS_PARAM_STATUS_REG_1,
	QSYS_PARAM_STATUS_REG_2,
	QSYS_PARAM_STATUS_REG_3,
	QSYS_PARAM_STATUS_REG_4,
	QSYS_PARAM_STATUS_REG_5,
	QSYS_PARAM_STATUS_REG_6,
	QSYS_PARAM_STATUS_REG_7,
	QSYS_PARAM_STATUS_REG_8,
	QSYS_PARAM_STATUS_REG_9,
	QSYS_GCL_STATUS_REG_1,
	QSYS_GCL_STATUS_REG_2,
	REW_PORT_VLAN_CFG = REW << TARGET_OFFSET,
	REW_TAG_CFG,
	REW_PORT_CFG,
	REW_DSCP_CFG,
	REW_PCP_DEI_QOS_MAP_CFG,
	REW_PTP_CFG,
	REW_PTP_DLY1_CFG,
	REW_RED_TAG_CFG,
	REW_DSCP_REMAP_DP1_CFG,
	REW_DSCP_REMAP_CFG,
	REW_STAT_CFG,
	REW_REW_STICKY,
	REW_PPT,
	SYS_COUNT_RX_OCTETS = SYS << TARGET_OFFSET,
	SYS_COUNT_RX_UNICAST,
	SYS_COUNT_RX_MULTICAST,
	SYS_COUNT_RX_BROADCAST,
	SYS_COUNT_RX_SHORTS,
	SYS_COUNT_RX_FRAGMENTS,
	SYS_COUNT_RX_JABBERS,
	SYS_COUNT_RX_CRC_ALIGN_ERRS,
	SYS_COUNT_RX_SYM_ERRS,
	SYS_COUNT_RX_64,
	SYS_COUNT_RX_65_127,
	SYS_COUNT_RX_128_255,
	SYS_COUNT_RX_256_1023,
	SYS_COUNT_RX_1024_1526,
	SYS_COUNT_RX_1527_MAX,
	SYS_COUNT_RX_PAUSE,
	SYS_COUNT_RX_CONTROL,
	SYS_COUNT_RX_LONGS,
	SYS_COUNT_RX_CLASSIFIED_DROPS,
	SYS_COUNT_TX_OCTETS,
	SYS_COUNT_TX_UNICAST,
	SYS_COUNT_TX_MULTICAST,
	SYS_COUNT_TX_BROADCAST,
	SYS_COUNT_TX_COLLISION,
	SYS_COUNT_TX_DROPS,
	SYS_COUNT_TX_PAUSE,
	SYS_COUNT_TX_64,
	SYS_COUNT_TX_65_127,
	SYS_COUNT_TX_128_511,
	SYS_COUNT_TX_512_1023,
	SYS_COUNT_TX_1024_1526,
	SYS_COUNT_TX_1527_MAX,
	SYS_COUNT_TX_AGING,
	SYS_RESET_CFG,
	SYS_SR_ETYPE_CFG,
	SYS_VLAN_ETYPE_CFG,
	SYS_PORT_MODE,
	SYS_FRONT_PORT_MODE,
	SYS_FRM_AGING,
	SYS_STAT_CFG,
	SYS_SW_STATUS,
	SYS_MISC_CFG,
	SYS_REW_MAC_HIGH_CFG,
	SYS_REW_MAC_LOW_CFG,
	SYS_TIMESTAMP_OFFSET,
	SYS_CMID,
	SYS_PAUSE_CFG,
	SYS_PAUSE_TOT_CFG,
	SYS_ATOP,
	SYS_ATOP_TOT_CFG,
	SYS_MAC_FC_CFG,
	SYS_MMGT,
	SYS_MMGT_FAST,
	SYS_EVENTS_DIF,
	SYS_EVENTS_CORE,
	SYS_CNT,
	SYS_PTP_STATUS,
	SYS_PTP_TXSTAMP,
	SYS_PTP_NXT,
	SYS_PTP_CFG,
	SYS_RAM_INIT,
	SYS_CM_ADDR,
	SYS_CM_DATA_WR,
	SYS_CM_DATA_RD,
	SYS_CM_OP,
	SYS_CM_DATA,
	PTP_PIN_CFG = PTP << TARGET_OFFSET,
	PTP_PIN_TOD_SEC_MSB,
	PTP_PIN_TOD_SEC_LSB,
	PTP_PIN_TOD_NSEC,
	PTP_PIN_WF_HIGH_PERIOD,
	PTP_PIN_WF_LOW_PERIOD,
	PTP_CFG_MISC,
	PTP_CLK_CFG_ADJ_CFG,
	PTP_CLK_CFG_ADJ_FREQ,
	GCB_SOFT_RST = GCB << TARGET_OFFSET,
	GCB_MIIM_MII_STATUS,
	GCB_MIIM_MII_CMD,
	GCB_MIIM_MII_DATA,
	DEV_CLOCK_CFG = DEV_GMII << TARGET_OFFSET,
	DEV_PORT_MISC,
	DEV_EVENTS,
	DEV_EEE_CFG,
	DEV_RX_PATH_DELAY,
	DEV_TX_PATH_DELAY,
	DEV_PTP_PREDICT_CFG,
	DEV_MAC_ENA_CFG,
	DEV_MAC_MODE_CFG,
	DEV_MAC_MAXLEN_CFG,
	DEV_MAC_TAGS_CFG,
	DEV_MAC_ADV_CHK_CFG,
	DEV_MAC_IFG_CFG,
	DEV_MAC_HDX_CFG,
	DEV_MAC_DBG_CFG,
	DEV_MAC_FC_MAC_LOW_CFG,
	DEV_MAC_FC_MAC_HIGH_CFG,
	DEV_MAC_STICKY,
	PCS1G_CFG,
	PCS1G_MODE_CFG,
	PCS1G_SD_CFG,
	PCS1G_ANEG_CFG,
	PCS1G_ANEG_NP_CFG,
	PCS1G_LB_CFG,
	PCS1G_DBG_CFG,
	PCS1G_CDET_CFG,
	PCS1G_ANEG_STATUS,
	PCS1G_ANEG_NP_STATUS,
	PCS1G_LINK_STATUS,
	PCS1G_LINK_DOWN_CNT,
	PCS1G_STICKY,
	PCS1G_DEBUG_STATUS,
	PCS1G_LPI_CFG,
	PCS1G_LPI_WAKE_ERROR_CNT,
	PCS1G_LPI_STATUS,
	PCS1G_TSTPAT_MODE_CFG,
	PCS1G_TSTPAT_STATUS,
	DEV_PCS_FX100_CFG,
	DEV_PCS_FX100_STATUS,
};

enum ocelot_regfield {
	ANA_ADVLEARN_VLAN_CHK,
	ANA_ADVLEARN_LEARN_MIRROR,
	ANA_ANEVENTS_FLOOD_DISCARD,
	ANA_ANEVENTS_MSTI_DROP,
	ANA_ANEVENTS_ACLKILL,
	ANA_ANEVENTS_ACLUSED,
	ANA_ANEVENTS_AUTOAGE,
	ANA_ANEVENTS_VS2TTL1,
	ANA_ANEVENTS_STORM_DROP,
	ANA_ANEVENTS_LEARN_DROP,
	ANA_ANEVENTS_AGED_ENTRY,
	ANA_ANEVENTS_CPU_LEARN_FAILED,
	ANA_ANEVENTS_AUTO_LEARN_FAILED,
	ANA_ANEVENTS_LEARN_REMOVE,
	ANA_ANEVENTS_AUTO_LEARNED,
	ANA_ANEVENTS_AUTO_MOVED,
	ANA_ANEVENTS_DROPPED,
	ANA_ANEVENTS_CLASSIFIED_DROP,
	ANA_ANEVENTS_CLASSIFIED_COPY,
	ANA_ANEVENTS_VLAN_DISCARD,
	ANA_ANEVENTS_FWD_DISCARD,
	ANA_ANEVENTS_MULTICAST_FLOOD,
	ANA_ANEVENTS_UNICAST_FLOOD,
	ANA_ANEVENTS_DEST_KNOWN,
	ANA_ANEVENTS_BUCKET3_MATCH,
	ANA_ANEVENTS_BUCKET2_MATCH,
	ANA_ANEVENTS_BUCKET1_MATCH,
	ANA_ANEVENTS_BUCKET0_MATCH,
	ANA_ANEVENTS_CPU_OPERATION,
	ANA_ANEVENTS_DMAC_LOOKUP,
	ANA_ANEVENTS_SMAC_LOOKUP,
	ANA_ANEVENTS_SEQ_GEN_ERR_0,
	ANA_ANEVENTS_SEQ_GEN_ERR_1,
	ANA_TABLES_MACACCESS_B_DOM,
	ANA_TABLES_MACTINDX_BUCKET,
	ANA_TABLES_MACTINDX_M_INDEX,
	QSYS_SWITCH_PORT_MODE_PORT_ENA,
	QSYS_SWITCH_PORT_MODE_SCH_NEXT_CFG,
	QSYS_SWITCH_PORT_MODE_YEL_RSRVD,
	QSYS_SWITCH_PORT_MODE_INGRESS_DROP_MODE,
	QSYS_SWITCH_PORT_MODE_TX_PFC_ENA,
	QSYS_SWITCH_PORT_MODE_TX_PFC_MODE,
	QSYS_TIMED_FRAME_ENTRY_TFRM_VLD,
	QSYS_TIMED_FRAME_ENTRY_TFRM_FP,
	QSYS_TIMED_FRAME_ENTRY_TFRM_PORTNO,
	QSYS_TIMED_FRAME_ENTRY_TFRM_TM_SEL,
	QSYS_TIMED_FRAME_ENTRY_TFRM_TM_T,
	SYS_PORT_MODE_DATA_WO_TS,
	SYS_PORT_MODE_INCL_INJ_HDR,
	SYS_PORT_MODE_INCL_XTR_HDR,
	SYS_PORT_MODE_INCL_HDR_ERR,
	SYS_RESET_CFG_CORE_ENA,
	SYS_RESET_CFG_MEM_ENA,
	SYS_RESET_CFG_MEM_INIT,
	GCB_SOFT_RST_SWC_RST,
	GCB_MIIM_MII_STATUS_PENDING,
	GCB_MIIM_MII_STATUS_BUSY,
	SYS_PAUSE_CFG_PAUSE_START,
	SYS_PAUSE_CFG_PAUSE_STOP,
	SYS_PAUSE_CFG_PAUSE_ENA,
	REGFIELD_MAX
};

enum {
	/* VCAP_CORE_CFG */
	VCAP_CORE_UPDATE_CTRL,
	VCAP_CORE_MV_CFG,
	/* VCAP_CORE_CACHE */
	VCAP_CACHE_ENTRY_DAT,
	VCAP_CACHE_MASK_DAT,
	VCAP_CACHE_ACTION_DAT,
	VCAP_CACHE_CNT_DAT,
	VCAP_CACHE_TG_DAT,
	/* VCAP_CONST */
	VCAP_CONST_VCAP_VER,
	VCAP_CONST_ENTRY_WIDTH,
	VCAP_CONST_ENTRY_CNT,
	VCAP_CONST_ENTRY_SWCNT,
	VCAP_CONST_ENTRY_TG_WIDTH,
	VCAP_CONST_ACTION_DEF_CNT,
	VCAP_CONST_ACTION_WIDTH,
	VCAP_CONST_CNT_WIDTH,
	VCAP_CONST_CORE_CNT,
	VCAP_CONST_IF_CNT,
};

enum ocelot_ptp_pins {
	PTP_PIN_0,
	PTP_PIN_1,
	PTP_PIN_2,
	PTP_PIN_3,
	TOD_ACC_PIN
};

struct ocelot_stat_layout {
	u32 offset;
	char name[ETH_GSTRING_LEN];
};

enum ocelot_tag_prefix {
	OCELOT_TAG_PREFIX_DISABLED	= 0,
	OCELOT_TAG_PREFIX_NONE,
	OCELOT_TAG_PREFIX_SHORT,
	OCELOT_TAG_PREFIX_LONG,
};

struct ocelot;

struct ocelot_ops {
	struct net_device *(*port_to_netdev)(struct ocelot *ocelot, int port);
	int (*netdev_to_port)(struct net_device *dev);
	int (*reset)(struct ocelot *ocelot);
	u16 (*wm_enc)(u16 value);
	u16 (*wm_dec)(u16 value);
	void (*wm_stat)(u32 val, u32 *inuse, u32 *maxuse);
};

struct ocelot_vcap_block {
	struct list_head rules;
	int count;
	int pol_lpr;
};

struct ocelot_bridge_vlan {
	u16 vid;
	unsigned long portmask;
	unsigned long untagged;
	struct list_head list;
};

enum ocelot_port_tag_config {
	/* all VLANs are egress-untagged */
	OCELOT_PORT_TAG_DISABLED = 0,
	/* all VLANs except the native VLAN and VID 0 are egress-tagged */
	OCELOT_PORT_TAG_NATIVE = 1,
	/* all VLANs except VID 0 are egress-tagged */
	OCELOT_PORT_TAG_TRUNK_NO_VID0 = 2,
	/* all VLANs are egress-tagged */
	OCELOT_PORT_TAG_TRUNK = 3,
};

enum ocelot_sb {
	OCELOT_SB_BUF,
	OCELOT_SB_REF,
	OCELOT_SB_NUM,
};

enum ocelot_sb_pool {
	OCELOT_SB_POOL_ING,
	OCELOT_SB_POOL_EGR,
	OCELOT_SB_POOL_NUM,
};

#define OCELOT_QUIRK_PCS_PERFORMS_RATE_ADAPTATION	BIT(0)
#define OCELOT_QUIRK_QSGMII_PORTS_MUST_BE_UP		BIT(1)

struct ocelot_port {
	struct ocelot			*ocelot;

	struct regmap			*target;

	bool				vlan_aware;
	/* VLAN that untagged frames are classified to, on ingress */
	const struct ocelot_bridge_vlan	*pvid_vlan;

	unsigned int			ptp_skbs_in_flight;
	u8				ptp_cmd;
	struct sk_buff_head		tx_skbs;
	u8				ts_id;

	phy_interface_t			phy_mode;

	u8				*xmit_template;
	bool				is_dsa_8021q_cpu;
	bool				learn_ena;

	struct net_device		*bond;
	bool				lag_tx_active;

	u16				mrp_ring_id;

	struct net_device		*bridge;
	u8				stp_state;
};

struct ocelot {
	struct device			*dev;
	struct devlink			*devlink;
	struct devlink_port		*devlink_ports;

	const struct ocelot_ops		*ops;
	struct regmap			*targets[TARGET_MAX];
	struct regmap_field		*regfields[REGFIELD_MAX];
	const u32 *const		*map;
	const struct ocelot_stat_layout	*stats_layout;
	unsigned int			num_stats;

	u32				pool_size[OCELOT_SB_NUM][OCELOT_SB_POOL_NUM];
	int				packet_buffer_size;
	int				num_frame_refs;
	int				num_mact_rows;

	struct ocelot_port		**ports;

	u8				base_mac[ETH_ALEN];

	struct list_head		vlans;

	/* Switches like VSC9959 have flooding per traffic class */
	int				num_flooding_pgids;

	/* In tables like ANA:PORT and the ANA:PGID:PGID mask,
	 * the CPU is located after the physical ports (at the
	 * num_phys_ports index).
	 */
	u8				num_phys_ports;

	int				npi;

	enum ocelot_tag_prefix		npi_inj_prefix;
	enum ocelot_tag_prefix		npi_xtr_prefix;

	struct list_head		multicast;
	struct list_head		pgids;

	struct list_head		dummy_rules;
	struct ocelot_vcap_block	block[3];
	struct vcap_props		*vcap;

	/* Workqueue to check statistics for overflow with its lock */
	struct mutex			stats_lock;
	u64				*stats;
	struct delayed_work		stats_work;
	struct workqueue_struct		*stats_queue;

	/* Lock for serializing access to the MAC table */
	struct mutex			mact_lock;

	struct workqueue_struct		*owq;

	u8				ptp:1;
	struct ptp_clock		*ptp_clock;
	struct ptp_clock_info		ptp_info;
	struct hwtstamp_config		hwtstamp_config;
	unsigned int			ptp_skbs_in_flight;
	/* Protects the 2-step TX timestamp ID logic */
	spinlock_t			ts_id_lock;
	/* Protects the PTP interface state */
	struct mutex			ptp_lock;
	/* Protects the PTP clock */
	spinlock_t			ptp_clock_lock;
	struct ptp_pin_desc		ptp_pins[OCELOT_PTP_PINS_NUM];
};

struct ocelot_policer {
	u32 rate; /* kilobit per second */
	u32 burst; /* bytes */
};

#define ocelot_read_ix(ocelot, reg, gi, ri) __ocelot_read_ix(ocelot, reg, reg##_GSZ * (gi) + reg##_RSZ * (ri))
#define ocelot_read_gix(ocelot, reg, gi) __ocelot_read_ix(ocelot, reg, reg##_GSZ * (gi))
#define ocelot_read_rix(ocelot, reg, ri) __ocelot_read_ix(ocelot, reg, reg##_RSZ * (ri))
#define ocelot_read(ocelot, reg) __ocelot_read_ix(ocelot, reg, 0)

#define ocelot_write_ix(ocelot, val, reg, gi, ri) __ocelot_write_ix(ocelot, val, reg, reg##_GSZ * (gi) + reg##_RSZ * (ri))
#define ocelot_write_gix(ocelot, val, reg, gi) __ocelot_write_ix(ocelot, val, reg, reg##_GSZ * (gi))
#define ocelot_write_rix(ocelot, val, reg, ri) __ocelot_write_ix(ocelot, val, reg, reg##_RSZ * (ri))
#define ocelot_write(ocelot, val, reg) __ocelot_write_ix(ocelot, val, reg, 0)

#define ocelot_rmw_ix(ocelot, val, m, reg, gi, ri) __ocelot_rmw_ix(ocelot, val, m, reg, reg##_GSZ * (gi) + reg##_RSZ * (ri))
#define ocelot_rmw_gix(ocelot, val, m, reg, gi) __ocelot_rmw_ix(ocelot, val, m, reg, reg##_GSZ * (gi))
#define ocelot_rmw_rix(ocelot, val, m, reg, ri) __ocelot_rmw_ix(ocelot, val, m, reg, reg##_RSZ * (ri))
#define ocelot_rmw(ocelot, val, m, reg) __ocelot_rmw_ix(ocelot, val, m, reg, 0)

#define ocelot_field_write(ocelot, reg, val) regmap_field_write((ocelot)->regfields[(reg)], (val))
#define ocelot_field_read(ocelot, reg, val) regmap_field_read((ocelot)->regfields[(reg)], (val))
#define ocelot_fields_write(ocelot, id, reg, val) regmap_fields_write((ocelot)->regfields[(reg)], (id), (val))
#define ocelot_fields_read(ocelot, id, reg, val) regmap_fields_read((ocelot)->regfields[(reg)], (id), (val))

#define ocelot_target_read_ix(ocelot, target, reg, gi, ri) \
	__ocelot_target_read_ix(ocelot, target, reg, reg##_GSZ * (gi) + reg##_RSZ * (ri))
#define ocelot_target_read_gix(ocelot, target, reg, gi) \
	__ocelot_target_read_ix(ocelot, target, reg, reg##_GSZ * (gi))
#define ocelot_target_read_rix(ocelot, target, reg, ri) \
	__ocelot_target_read_ix(ocelot, target, reg, reg##_RSZ * (ri))
#define ocelot_target_read(ocelot, target, reg) \
	__ocelot_target_read_ix(ocelot, target, reg, 0)

#define ocelot_target_write_ix(ocelot, target, val, reg, gi, ri) \
	__ocelot_target_write_ix(ocelot, target, val, reg, reg##_GSZ * (gi) + reg##_RSZ * (ri))
#define ocelot_target_write_gix(ocelot, target, val, reg, gi) \
	__ocelot_target_write_ix(ocelot, target, val, reg, reg##_GSZ * (gi))
#define ocelot_target_write_rix(ocelot, target, val, reg, ri) \
	__ocelot_target_write_ix(ocelot, target, val, reg, reg##_RSZ * (ri))
#define ocelot_target_write(ocelot, target, val, reg) \
	__ocelot_target_write_ix(ocelot, target, val, reg, 0)

/* I/O */
u32 ocelot_port_readl(struct ocelot_port *port, u32 reg);
void ocelot_port_writel(struct ocelot_port *port, u32 val, u32 reg);
void ocelot_port_rmwl(struct ocelot_port *port, u32 val, u32 mask, u32 reg);
u32 __ocelot_read_ix(struct ocelot *ocelot, u32 reg, u32 offset);
void __ocelot_write_ix(struct ocelot *ocelot, u32 val, u32 reg, u32 offset);
void __ocelot_rmw_ix(struct ocelot *ocelot, u32 val, u32 mask, u32 reg,
		     u32 offset);
u32 __ocelot_target_read_ix(struct ocelot *ocelot, enum ocelot_target target,
			    u32 reg, u32 offset);
void __ocelot_target_write_ix(struct ocelot *ocelot, enum ocelot_target target,
			      u32 val, u32 reg, u32 offset);

/* Packet I/O */
bool ocelot_can_inject(struct ocelot *ocelot, int grp);
void ocelot_port_inject_frame(struct ocelot *ocelot, int port, int grp,
			      u32 rew_op, struct sk_buff *skb);
int ocelot_xtr_poll_frame(struct ocelot *ocelot, int grp, struct sk_buff **skb);
void ocelot_drain_cpu_queue(struct ocelot *ocelot, int grp);

/* Hardware initialization */
int ocelot_regfields_init(struct ocelot *ocelot,
			  const struct reg_field *const regfields);
struct regmap *ocelot_regmap_init(struct ocelot *ocelot, struct resource *res);
int ocelot_init(struct ocelot *ocelot);
void ocelot_deinit(struct ocelot *ocelot);
void ocelot_init_port(struct ocelot *ocelot, int port);
void ocelot_deinit_port(struct ocelot *ocelot, int port);

/* DSA callbacks */
void ocelot_get_strings(struct ocelot *ocelot, int port, u32 sset, u8 *data);
void ocelot_get_ethtool_stats(struct ocelot *ocelot, int port, u64 *data);
int ocelot_get_sset_count(struct ocelot *ocelot, int port, int sset);
int ocelot_get_ts_info(struct ocelot *ocelot, int port,
		       struct ethtool_ts_info *info);
void ocelot_set_ageing_time(struct ocelot *ocelot, unsigned int msecs);
int ocelot_port_vlan_filtering(struct ocelot *ocelot, int port, bool enabled,
			       struct netlink_ext_ack *extack);
void ocelot_bridge_stp_state_set(struct ocelot *ocelot, int port, u8 state);
void ocelot_apply_bridge_fwd_mask(struct ocelot *ocelot);
int ocelot_port_pre_bridge_flags(struct ocelot *ocelot, int port,
				 struct switchdev_brport_flags val);
void ocelot_port_bridge_flags(struct ocelot *ocelot, int port,
			      struct switchdev_brport_flags val);
void ocelot_port_bridge_join(struct ocelot *ocelot, int port,
			     struct net_device *bridge);
void ocelot_port_bridge_leave(struct ocelot *ocelot, int port,
			      struct net_device *bridge);
int ocelot_fdb_dump(struct ocelot *ocelot, int port,
		    dsa_fdb_dump_cb_t *cb, void *data);
int ocelot_fdb_add(struct ocelot *ocelot, int port,
		   const unsigned char *addr, u16 vid);
int ocelot_fdb_del(struct ocelot *ocelot, int port,
		   const unsigned char *addr, u16 vid);
int ocelot_vlan_prepare(struct ocelot *ocelot, int port, u16 vid, bool pvid,
			bool untagged, struct netlink_ext_ack *extack);
int ocelot_vlan_add(struct ocelot *ocelot, int port, u16 vid, bool pvid,
		    bool untagged);
int ocelot_vlan_del(struct ocelot *ocelot, int port, u16 vid);
int ocelot_hwstamp_get(struct ocelot *ocelot, int port, struct ifreq *ifr);
int ocelot_hwstamp_set(struct ocelot *ocelot, int port, struct ifreq *ifr);
int ocelot_port_txtstamp_request(struct ocelot *ocelot, int port,
				 struct sk_buff *skb,
				 struct sk_buff **clone);
void ocelot_get_txtstamp(struct ocelot *ocelot);
void ocelot_port_set_maxlen(struct ocelot *ocelot, int port, size_t sdu);
int ocelot_get_max_mtu(struct ocelot *ocelot, int port);
int ocelot_port_policer_add(struct ocelot *ocelot, int port,
			    struct ocelot_policer *pol);
int ocelot_port_policer_del(struct ocelot *ocelot, int port);
int ocelot_cls_flower_replace(struct ocelot *ocelot, int port,
			      struct flow_cls_offload *f, bool ingress);
int ocelot_cls_flower_destroy(struct ocelot *ocelot, int port,
			      struct flow_cls_offload *f, bool ingress);
int ocelot_cls_flower_stats(struct ocelot *ocelot, int port,
			    struct flow_cls_offload *f, bool ingress);
int ocelot_port_mdb_add(struct ocelot *ocelot, int port,
			const struct switchdev_obj_port_mdb *mdb);
int ocelot_port_mdb_del(struct ocelot *ocelot, int port,
			const struct switchdev_obj_port_mdb *mdb);
int ocelot_port_lag_join(struct ocelot *ocelot, int port,
			 struct net_device *bond,
			 struct netdev_lag_upper_info *info);
void ocelot_port_lag_leave(struct ocelot *ocelot, int port,
			   struct net_device *bond);
void ocelot_port_lag_change(struct ocelot *ocelot, int port, bool lag_tx_active);

int ocelot_devlink_sb_register(struct ocelot *ocelot);
void ocelot_devlink_sb_unregister(struct ocelot *ocelot);
int ocelot_sb_pool_get(struct ocelot *ocelot, unsigned int sb_index,
		       u16 pool_index,
		       struct devlink_sb_pool_info *pool_info);
int ocelot_sb_pool_set(struct ocelot *ocelot, unsigned int sb_index,
		       u16 pool_index, u32 size,
		       enum devlink_sb_threshold_type threshold_type,
		       struct netlink_ext_ack *extack);
int ocelot_sb_port_pool_get(struct ocelot *ocelot, int port,
			    unsigned int sb_index, u16 pool_index,
			    u32 *p_threshold);
int ocelot_sb_port_pool_set(struct ocelot *ocelot, int port,
			    unsigned int sb_index, u16 pool_index,
			    u32 threshold, struct netlink_ext_ack *extack);
int ocelot_sb_tc_pool_bind_get(struct ocelot *ocelot, int port,
			       unsigned int sb_index, u16 tc_index,
			       enum devlink_sb_pool_type pool_type,
			       u16 *p_pool_index, u32 *p_threshold);
int ocelot_sb_tc_pool_bind_set(struct ocelot *ocelot, int port,
			       unsigned int sb_index, u16 tc_index,
			       enum devlink_sb_pool_type pool_type,
			       u16 pool_index, u32 threshold,
			       struct netlink_ext_ack *extack);
int ocelot_sb_occ_snapshot(struct ocelot *ocelot, unsigned int sb_index);
int ocelot_sb_occ_max_clear(struct ocelot *ocelot, unsigned int sb_index);
int ocelot_sb_occ_port_pool_get(struct ocelot *ocelot, int port,
				unsigned int sb_index, u16 pool_index,
				u32 *p_cur, u32 *p_max);
int ocelot_sb_occ_tc_port_bind_get(struct ocelot *ocelot, int port,
				   unsigned int sb_index, u16 tc_index,
				   enum devlink_sb_pool_type pool_type,
				   u32 *p_cur, u32 *p_max);

void ocelot_phylink_mac_link_down(struct ocelot *ocelot, int port,
				  unsigned int link_an_mode,
				  phy_interface_t interface,
				  unsigned long quirks);
void ocelot_phylink_mac_link_up(struct ocelot *ocelot, int port,
				struct phy_device *phydev,
				unsigned int link_an_mode,
				phy_interface_t interface,
				int speed, int duplex,
				bool tx_pause, bool rx_pause,
				unsigned long quirks);

#if IS_ENABLED(CONFIG_BRIDGE_MRP)
int ocelot_mrp_add(struct ocelot *ocelot, int port,
		   const struct switchdev_obj_mrp *mrp);
int ocelot_mrp_del(struct ocelot *ocelot, int port,
		   const struct switchdev_obj_mrp *mrp);
int ocelot_mrp_add_ring_role(struct ocelot *ocelot, int port,
			     const struct switchdev_obj_ring_role_mrp *mrp);
int ocelot_mrp_del_ring_role(struct ocelot *ocelot, int port,
			     const struct switchdev_obj_ring_role_mrp *mrp);
#else
static inline int ocelot_mrp_add(struct ocelot *ocelot, int port,
				 const struct switchdev_obj_mrp *mrp)
{
	return -EOPNOTSUPP;
}

static inline int ocelot_mrp_del(struct ocelot *ocelot, int port,
				 const struct switchdev_obj_mrp *mrp)
{
	return -EOPNOTSUPP;
}

static inline int
ocelot_mrp_add_ring_role(struct ocelot *ocelot, int port,
			 const struct switchdev_obj_ring_role_mrp *mrp)
{
	return -EOPNOTSUPP;
}

static inline int
ocelot_mrp_del_ring_role(struct ocelot *ocelot, int port,
			 const struct switchdev_obj_ring_role_mrp *mrp)
{
	return -EOPNOTSUPP;
}
#endif

#endif
