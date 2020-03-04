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

#define IFH_INJ_BYPASS			BIT(31)
#define IFH_INJ_POP_CNT_DISABLE		(3 << 28)

#define IFH_TAG_TYPE_C			0
#define IFH_TAG_TYPE_S			1

#define IFH_REW_OP_NOOP			0x0
#define IFH_REW_OP_DSCP			0x1
#define IFH_REW_OP_ONE_STEP_PTP		0x2
#define IFH_REW_OP_TWO_STEP_PTP		0x3
#define IFH_REW_OP_ORIGIN_PTP		0x5

#define OCELOT_TAG_LEN			16
#define OCELOT_SHORT_PREFIX_LEN		4
#define OCELOT_LONG_PREFIX_LEN		16

#define OCELOT_SPEED_2500		0
#define OCELOT_SPEED_1000		1
#define OCELOT_SPEED_100		2
#define OCELOT_SPEED_10			3

#define TARGET_OFFSET			24
#define REG_MASK			GENMASK(TARGET_OFFSET - 1, 0)
#define REG(reg, offset)		[reg & REG_MASK] = offset

#define REG_RESERVED_ADDR		0xffffffff
#define REG_RESERVED(reg)		REG(reg, REG_RESERVED_ADDR)

enum ocelot_target {
	ANA = 1,
	QS,
	QSYS,
	REW,
	SYS,
	S2,
	HSIO,
	PTP,
	GCB,
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
	S2_CORE_UPDATE_CTRL = S2 << TARGET_OFFSET,
	S2_CORE_MV_CFG,
	S2_CACHE_ENTRY_DAT,
	S2_CACHE_MASK_DAT,
	S2_CACHE_ACTION_DAT,
	S2_CACHE_CNT_DAT,
	S2_CACHE_TG_DAT,
	PTP_PIN_CFG = PTP << TARGET_OFFSET,
	PTP_PIN_TOD_SEC_MSB,
	PTP_PIN_TOD_SEC_LSB,
	PTP_PIN_TOD_NSEC,
	PTP_CFG_MISC,
	PTP_CLK_CFG_ADJ_CFG,
	PTP_CLK_CFG_ADJ_FREQ,
	GCB_SOFT_RST = GCB << TARGET_OFFSET,
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
	QSYS_TIMED_FRAME_ENTRY_TFRM_VLD,
	QSYS_TIMED_FRAME_ENTRY_TFRM_FP,
	QSYS_TIMED_FRAME_ENTRY_TFRM_PORTNO,
	QSYS_TIMED_FRAME_ENTRY_TFRM_TM_SEL,
	QSYS_TIMED_FRAME_ENTRY_TFRM_TM_T,
	SYS_RESET_CFG_CORE_ENA,
	SYS_RESET_CFG_MEM_ENA,
	SYS_RESET_CFG_MEM_INIT,
	GCB_SOFT_RST_SWC_RST,
	REGFIELD_MAX
};

enum ocelot_clk_pins {
	ALT_PPS_PIN	= 1,
	EXT_CLK_PIN,
	ALT_LDST_PIN,
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
	int (*reset)(struct ocelot *ocelot);
};

struct ocelot_acl_block {
	struct list_head rules;
	int count;
};

struct ocelot_port {
	struct ocelot			*ocelot;

	void __iomem			*regs;

	/* Ingress default VLAN (pvid) */
	u16				pvid;

	/* Egress default VLAN (vid) */
	u16				vid;

	u8				ptp_cmd;
	struct sk_buff_head		tx_skbs;
	u8				ts_id;

	phy_interface_t			phy_mode;
};

struct ocelot {
	struct device			*dev;

	const struct ocelot_ops		*ops;
	struct regmap			*targets[TARGET_MAX];
	struct regmap_field		*regfields[REGFIELD_MAX];
	const u32 *const		*map;
	const struct ocelot_stat_layout	*stats_layout;
	unsigned int			num_stats;

	int				shared_queue_sz;

	struct net_device		*hw_bridge_dev;
	u16				bridge_mask;
	u16				bridge_fwd_mask;

	struct ocelot_port		**ports;

	u8				base_mac[ETH_ALEN];

	/* Keep track of the vlan port masks */
	u32				vlan_mask[VLAN_N_VID];

	u8				num_phys_ports;
	u8				num_cpu_ports;
	u8				cpu;

	u32				*lags;

	struct list_head		multicast;

	struct ocelot_acl_block		acl_block;

	const struct vcap_field		*vcap_is2_keys;
	const struct vcap_field		*vcap_is2_actions;
	const struct vcap_props		*vcap;

	/* Workqueue to check statistics for overflow with its lock */
	struct mutex			stats_lock;
	u64				*stats;
	struct delayed_work		stats_work;
	struct workqueue_struct		*stats_queue;

	u8				ptp:1;
	struct ptp_clock		*ptp_clock;
	struct ptp_clock_info		ptp_info;
	struct hwtstamp_config		hwtstamp_config;
	/* Protects the PTP interface state */
	struct mutex			ptp_lock;
	/* Protects the PTP clock */
	spinlock_t			ptp_clock_lock;
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

/* I/O */
u32 ocelot_port_readl(struct ocelot_port *port, u32 reg);
void ocelot_port_writel(struct ocelot_port *port, u32 val, u32 reg);
u32 __ocelot_read_ix(struct ocelot *ocelot, u32 reg, u32 offset);
void __ocelot_write_ix(struct ocelot *ocelot, u32 val, u32 reg, u32 offset);
void __ocelot_rmw_ix(struct ocelot *ocelot, u32 val, u32 mask, u32 reg,
		     u32 offset);

/* Hardware initialization */
int ocelot_regfields_init(struct ocelot *ocelot,
			  const struct reg_field *const regfields);
struct regmap *ocelot_regmap_init(struct ocelot *ocelot, struct resource *res);
void ocelot_set_cpu_port(struct ocelot *ocelot, int cpu,
			 enum ocelot_tag_prefix injection,
			 enum ocelot_tag_prefix extraction);
int ocelot_init(struct ocelot *ocelot);
void ocelot_deinit(struct ocelot *ocelot);
void ocelot_init_port(struct ocelot *ocelot, int port);

/* DSA callbacks */
void ocelot_port_enable(struct ocelot *ocelot, int port,
			struct phy_device *phy);
void ocelot_port_disable(struct ocelot *ocelot, int port);
void ocelot_get_strings(struct ocelot *ocelot, int port, u32 sset, u8 *data);
void ocelot_get_ethtool_stats(struct ocelot *ocelot, int port, u64 *data);
int ocelot_get_sset_count(struct ocelot *ocelot, int port, int sset);
int ocelot_get_ts_info(struct ocelot *ocelot, int port,
		       struct ethtool_ts_info *info);
void ocelot_set_ageing_time(struct ocelot *ocelot, unsigned int msecs);
void ocelot_adjust_link(struct ocelot *ocelot, int port,
			struct phy_device *phydev);
void ocelot_port_vlan_filtering(struct ocelot *ocelot, int port,
				bool vlan_aware);
void ocelot_bridge_stp_state_set(struct ocelot *ocelot, int port, u8 state);
int ocelot_port_bridge_join(struct ocelot *ocelot, int port,
			    struct net_device *bridge);
int ocelot_port_bridge_leave(struct ocelot *ocelot, int port,
			     struct net_device *bridge);
int ocelot_fdb_dump(struct ocelot *ocelot, int port,
		    dsa_fdb_dump_cb_t *cb, void *data);
int ocelot_fdb_add(struct ocelot *ocelot, int port,
		   const unsigned char *addr, u16 vid, bool vlan_aware);
int ocelot_fdb_del(struct ocelot *ocelot, int port,
		   const unsigned char *addr, u16 vid);
int ocelot_vlan_add(struct ocelot *ocelot, int port, u16 vid, bool pvid,
		    bool untagged);
int ocelot_vlan_del(struct ocelot *ocelot, int port, u16 vid);
int ocelot_hwstamp_get(struct ocelot *ocelot, int port, struct ifreq *ifr);
int ocelot_hwstamp_set(struct ocelot *ocelot, int port, struct ifreq *ifr);
int ocelot_ptp_gettime64(struct ptp_clock_info *ptp, struct timespec64 *ts);
int ocelot_port_add_txtstamp_skb(struct ocelot_port *ocelot_port,
				 struct sk_buff *skb);
void ocelot_get_txtstamp(struct ocelot *ocelot);
int ocelot_cls_flower_replace(struct ocelot *ocelot, int port,
			      struct flow_cls_offload *f, bool ingress);
int ocelot_cls_flower_destroy(struct ocelot *ocelot, int port,
			      struct flow_cls_offload *f, bool ingress);
int ocelot_cls_flower_stats(struct ocelot *ocelot, int port,
			    struct flow_cls_offload *f, bool ingress);

#endif
