/*
 * TC956X ethernet driver.
 *
 * tc956xmac_ioctl.h
 *
 * Copyright (C) 2018 Synopsys, Inc. and/or its affiliates.
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro and Synopsys Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *  14 Sep 2021 : 1. Synchronization between ethtool vlan features
 *  		  "rx-vlan-offload", "rx-vlan-filter", "tx-vlan-offload" output and register settings.
 * 		  2. Added ethtool support to update "rx-vlan-offload", "rx-vlan-filter",
 *  		  and "tx-vlan-offload".
 * 		  3. Removed IOCTL TC956XMAC_VLAN_STRIP_CONFIG.
 * 		  4. Removed "Disable VLAN Filter" option in IOCTL TC956XMAC_VLAN_FILTERING.
 *  VERSION     : 01-00-13
 */

#ifndef _IOCTL_H__
#define _IOCTL_H__

/* Note: Multiple macro definitions for TC956X_PCIE_LOGSTAT.
 * Please also define/undefine same macro in common.h, if changing in this file
 */
/* #undef TC956X_PCIE_LOGSTAT */
#define TC956X_PCIE_LOGSTAT

enum ioctl_commands {
	TC956XMAC_GET_CBS = 0x1,
	TC956XMAC_SET_CBS = 0x2,
	TC956XMAC_GET_EST = 0x3,
	TC956XMAC_SET_EST = 0x4,
	TC956XMAC_GET_FPE = 0x5,
	TC956XMAC_SET_FPE = 0x6,
	TC956XMAC_GET_RXP = 0x7,
	TC956XMAC_SET_RXP = 0x8,
	TC956XMAC_GET_SPEED = 0x9,
	TC956XMAC_GET_TX_FREE_DESC = 0xa,
	TC956XMAC_REG_RD = 0xb,
	TC956XMAC_REG_WR = 0xc,
	TC956XMAC_SET_MAC_LOOPBACK = 0xd,
	TC956XMAC_SET_PHY_LOOPBACK = 0xe,
	TC956XMAC_L2_DA_FILTERING_CMD = 0xf,
	TC956XMAC_SET_PPS_OUT = 0x10,
	TC956XMAC_PTPCLK_CONFIG = 0x11,
	TC956XMAC_SA0_VLAN_INS_REP_REG = 0x12,
	TC956XMAC_SA1_VLAN_INS_REP_REG = 0x13,
	TC956XMAC_GET_TX_QCNT = 0x14,
	TC956XMAC_GET_RX_QCNT = 0x15,
	TC956XMAC_PCIE_CONFIG_REG_RD = 0x16,
	TC956XMAC_PCIE_CONFIG_REG_WR = 0x17,
	TC956XMAC_VLAN_FILTERING = 0x18,
	TC956XMAC_PTPOFFLOADING = 0x19,
	TC956X_GET_FW_STATUS = 0x1a,
	TC956XMAC_ENABLE_AUX_TIMESTAMP = 0x1b,
	TC956XMAC_ENABLE_ONESTEP_TIMESTAMP = 0x1c,
#ifdef TC956X_PCIE_LOGSTAT
	TC956X_PCIE_SET_LOGSTAT_CONF = 0x1d, /* LOGSTAT : Sets and Prints LTSSM and AER Configuration */
	TC956X_PCIE_GET_LOGSTAT_CONF = 0x1e, /* LOGSTAT : Read, Print and return LTSSM and AER Configuration */
	TC956X_PCIE_GET_LTSSM_LOG    = 0x1f, /* LOGSTAT : Read, Print and return LTSSM Looging Data */
#endif /* #ifdef TC956X_PCIE_LOGSTAT */
#ifndef TC956X
	TC956XMAC_VLAN_STRIP_CONFIG   = 0x22,
#endif
	TC956XMAC_PCIE_LANE_CHANGE	= 0x23,
	TC956XMAC_PCIE_SET_TX_MARGIN	= 0x24,
	TC956XMAC_PCIE_SET_TX_DEEMPHASIS	= 0x25, /*Enable or disable Tx de-emphasis*/
	TC956XMAC_PCIE_SET_DFE	= 0x26,
	TC956XMAC_PCIE_SET_CTLE	= 0x27,
	TC956XMAC_PCIE_SPEED_CHANGE	= 0x28,
};
#define SIOCSTIOCTL	SIOCDEVPRIVATE

#define TC956XMAC_IOCTL_QMODE_DCB		0x0
#define TC956XMAC_IOCTL_QMODE_AVB		0x1

#define TC956XMAC0_REG 0
#define TC956XMAC1_REG 1


/* Do not include the SA */
#define TC956XMAC_SA0_NONE			((TC956XMAC0_REG << 2) | 0)
/* Include/Insert the SA with value given in MAC Addr 0 Reg */
#define TC956XMAC_SA0_DESC_INSERT	((TC956XMAC0_REG << 2) | 1)
/* Replace the SA with the value given in MAC Addr 0 Reg */
#define TC956XMAC_SA0_DESC_REPLACE	((TC956XMAC0_REG << 2) | 2)
/* Include/Insert the SA with value given in MAC Addr 0 Reg */
#define TC956XMAC_SA0_REG_INSERT	((TC956XMAC0_REG << 2) | 2)
/* Replace the SA with the value given in MAC Addr 0 Reg */
#define TC956XMAC_SA0_REG_REPLACE	((TC956XMAC0_REG << 2) | 3)

/* Do not include the SA */
#define TC956XMAC_SA1_NONE			((TC956XMAC1_REG << 2) | 0)
/* Include/Insert the SA with value given in MAC Addr 1 Reg */
#define TC956XMAC_SA1_DESC_INSERT	((TC956XMAC1_REG << 2) | 1)
/* Replace the SA with the value given in MAC Addr 1 Reg */
#define TC956XMAC_SA1_DESC_REPLACE	((TC956XMAC1_REG << 2) | 2)
/* Include/Insert the SA with value given in MAC Addr 1 Reg */
#define TC956XMAC_SA1_REG_INSERT	((TC956XMAC1_REG << 2) | 2)
/* Replace the SA with the value given in MAC Addr 1 Reg */
#define TC956XMAC_SA1_REG_REPLACE	((TC956XMAC1_REG << 2) | 3)


struct tc956xmac_ioctl_cbs_params {
	__u32 send_slope; /* Send Slope value supported 15 bits */
	__u32 idle_slope; /* Idle Slope value supported 20 bits */
	__u32 high_credit; /* High Credit value supported 28 bits */
	__u32 low_credit; /* Low Credit value supported 28 bits */
	__u32 percentage; /* Dummy */
};

struct tc956xmac_ioctl_cbs_cfg {
	__u32 cmd;
	__u32 queue_idx;
	struct tc956xmac_ioctl_cbs_params speed100cfg;
	struct tc956xmac_ioctl_cbs_params speed1000cfg;
	struct tc956xmac_ioctl_cbs_params speed10000cfg;
	struct tc956xmac_ioctl_cbs_params speed5000cfg;
	struct tc956xmac_ioctl_cbs_params speed2500cfg;

};

struct tc956xmac_ioctl_speed {
	__u32 cmd;
	__u32 queue_idx;
	__u32 connected_speed;
};
struct tc956xmac_ioctl_l2_da_filter {
	unsigned int cmd;
	unsigned int chInx;
	int command_error;
	/* 0 - perfect and 1 - hash filtering */
	int perfect_hash;
	/* 0 - perfect and 1 - inverse matching */
	int perfect_inverse_match;
};

struct tc956xmac_ioctl_vlan_filter {
	__u32 cmd;
	/* 0 - disable and 1 - enable */
	/* Please note 0 - disable is not supported */
	int filter_enb_dis;
	/* 0 - perfect and 1 - hash filtering */
	int perfect_hash;
	/* 0 - perfect and 1 - inverse matching */
	int perfect_inverse_match;
};

struct tc956xmac_ioctl_free_desc {
	__u32 cmd;
	__u32 queue_idx;
	void *ptr;
};

struct tc956xmac_PPS_Config {
	__u32 cmd;
	unsigned int ptpclk_freq;
	unsigned int ppsout_freq;
	unsigned int ppsout_duty;
	unsigned int ppsout_align_ns;	/* first output align to ppsout_align_ns in ns */
	unsigned short ppsout_ch;
	bool  ppsout_align;		/* first output align */
};

struct tc956xmac_ioctl_reg_rd_wr {
	__u32 cmd;
	__u32 queue_idx;
	__u32 bar_num;
	__u32 addr;
	void *ptr;
};

struct tc956xmac_ioctl_loopback {
	__u32 cmd;
	__u32 flags;
};
struct tc956xmac_ioctl_phy_loopback {
	__u32 cmd;
	__u32 flags;
	__u32 phy_reg;
	__u32 bit;
};

struct tc956xmac_rx_parser_entry {
	__le32 match_data;
	__le32 match_en;
	__u8 af:1;
	__u8 rf:1;
	__u8 im:1;
	__u8 nc:1;
	__u8 res1:4;
	__u8 frame_offset:6;
	__u8 res2:2;
	__u8 ok_index;
	__u8 res3;
	__u16 dma_ch_no;
	__u16 res4;
} __packed;

struct tc956xmac_ioctl_rxp_entry {
	__u32 match_data;
	__u32 match_en;
	__u8 af:1;
	__u8 rf:1;
	__u8 im:1;
	__u8 nc:1;
	__u8 res1:4;
	__u8 frame_offset:6;
	__u8 res2:2;
	__u8 ok_index;
	__u8 res3;
	__u16 dma_ch_no;
	__u16 res4;
} __attribute__((packed));

#define TC956XMAC_RX_PARSER_MAX_ENTRIES		128

typedef struct tc956xmac_rx_parser_cfg {
	bool enable;
	__u32 nve;
	__u32 npe;
	struct tc956xmac_rx_parser_entry entries[TC956XMAC_RX_PARSER_MAX_ENTRIES];
} tc956xmac_rx_parser_cfg;

struct tc956xmac_ioctl_rxp_cfg {
	__u32 cmd;
	__u32 frpes;
	__u32 enabled;
	__u32 nve;
	__u32 npe;
	struct tc956xmac_ioctl_rxp_entry entries[TC956XMAC_RX_PARSER_MAX_ENTRIES];
};

#define TC956XMAC_IOCTL_EST_GCL_MAX_ENTRIES		1024

struct tc956xmac_ioctl_est_cfg {
	__u32 cmd;
	__u32 enabled;
	__u32 estwid;
	__u32 estdep;
	__u32 btr_offset[2];
	__u32 ctr[2];
	__u32 ter;
	__u32 gcl[TC956XMAC_IOCTL_EST_GCL_MAX_ENTRIES];
	__u32 gcl_size;
};

struct tc956xmac_ioctl_fpe_cfg {
	__u32 cmd;
	__u32 enabled;
	__u32 pec;
	__u32 afsz;
	__u32 RA_time;
	__u32 HA_time;
};

struct tc956xmac_ioctl_sa_ins_cfg {
	__u32 cmd;
	unsigned int control_flag;
	unsigned char mac_addr[ETH_ALEN];

};

struct tc956xmac_ioctl_tx_qcnt {
	__u32 cmd;
	__u32 queue_idx;
	void *ptr;
};

struct tc956xmac_ioctl_rx_qcnt {
	__u32 cmd;
	__u32 queue_idx;
	void *ptr;
};

struct tc956xmac_ioctl_pcie_reg_rd_wr {
	__u32 cmd;
	__u32 addr;
	void *ptr;
};

struct tc956x_ioctl_fwstatus {
	__u32 cmd;
	__u32 wdt_count;
	__u32 systick_count;
	__u32 fw_status;
};

#ifndef TC956X
struct tc956xmac_ioctl_vlan_strip_cfg {
	__u32 cmd;
	__u32 enabled; /* 1 to enable stripping, 0 to disable stripping */
};
#endif
enum lane_width {
	LANE_1	= 1,
	LANE_2	= 2,
	LANE_4	= 4,
};

enum pcie_speed {
	GEN_1	= 1, /*2.5 GT/s*/
	GEN_2	= 2, /*5 GT/s*/
	GEN_3	= 3, /*8 GT/s*/
};

#ifdef TC956X_PCIE_LOGSTAT
/**
 * enum port - Enumeration for ports available
 */
enum ports {
	UPSTREAM_PORT     = 0U, /* Used for Calculating port Offset */
	DOWNSTREAM_PORT1  = 1U,
	DOWNSTREAM_PORT2  = 2U,
	INTERNAL_ENDPOINT = 3U,
};


/**
 * enum tx_debug_monitor - Enumeration for Tx Debug Monitor
 */
enum tx_debug_monitor {
	TX_DEBUG_MONITOR0 = 0U,
	TX_DEBUG_MONITOR1 = 1U,
	TX_DEBUG_MONITOR2 = 2U,
	TX_DEBUG_MONITOR3 = 3U,
	TX_DEBUG_MONITOR4 = 4U,
	TX_DEBUG_MONITOR5 = 5U,
	TX_DEBUG_MONITOR6 = 6U,
	TX_DEBUG_MONITOR7 = 7U,
	TX_DEBUG_MONITOR8 = 8U,
	TX_DEBUG_MONITOR9 = 9U,
	TX_DEBUG_MONITOR10 = 10U,
};

/**
 * enum rx_debug_monitor - Enumeration for Rx Debug Monitor
 */
enum rx_debug_monitor {
	RX_DEBUG_MONITOR0 = 0U,
	RX_DEBUG_MONITOR1 = 1U,
};

/**
 * struct tc956x_ltssm_conf - Configuration Parameters
 *
 * State Logging Configuration Parameters.
 */
struct tc956x_ltssm_conf {
	__u8 logging_stop_cnt_val;
	__u8 logging_stop_linkwdth_en;
	__u8 logging_stop_linkspeed_en;
	__u8 logging_stop_timeout_en;
	__u8 logging_accept_txrxL0s_en;
	__u8 logging_post_stop_enable;
	__u8 ltssm_fifo_pointer;
};

/**
 * struct tc956x_ltssm_log - Logging Parameters
 *
 * State Logging Data parameters.
 */
struct tc956x_ltssm_log {
	__u8 ltssm_state;
	__u8 eq_phase;
	__u8 rxL0s;
	__u8 txL0s;
	__u8 substate_L1;
	__u8 active_lane;
	__u8 link_speed;
	__u8 dl_active;
	__u8 ltssm_timeout;
	__u8 ltssm_stop_status;
};

/**
 * struct tc956x_tx_dbg_mon - Tx Debug Monitor Parameters
 *
 * Covers all Tx Debug Monitor Parameters from 0 to 10.
 */
struct tc956x_tx_dbg_mon {
	__u8 gen3_tx_preset_override_en;
	__u8 gen3_pcoeff_tx_post_cur;
	__u8 gen3_pcoeff_tx_main_cur;
	__u8 gen3_pcoeff_tx_pre_cur;
	__u8 gen12_txmargin_override_en;
	__u16 gen12_txswing1_txmargin0;
	__u16 gen12_txswing0_txmargin0;
};

/**
 * struct tc956x_rx_dbg_mon - Rx Debug Monitor Parameters
 *
 * Covers all Rx Debug Monitor Parameters both 0 and 1.
 */
struct tc956x_rx_dbg_mon {
	__u8 gen3_rx_param_override_en;
	__u8 gen3_rx_param00_dfe_dlev;
	__u8 gen3_rx_param00_ctle_c;
	__u8 gen3_rx_param00_ctle_r;
	__u8 gen3_rx_param00_vga;
	__u8 gen3_rx_param00_dfe_h5;
	__u8 gen3_rx_param00_dfe_h4;
	__u8 gen3_rx_param00_dfe_h3;
	__u8 gen3_rx_param00_dfe_h2;
	__u8 gen3_rx_param00_dfe_h1;
};



/**
 * struct tc956x_ioctl_pcie_lane_change - IOCTL arguments for
 * PCIe USP and DSPs lane change for power reduction
 * dst_lane - Lane number which has to be switched from current configuration
 * port - this lane switch to be operated on which port
 */
struct tc956x_ioctl_pcie_lane_change {
	__u32 cmd;
	enum lane_width target_lane_width; /* 1, 2, 4 */
	enum ports port; /* USP, DSP1, others return error*/
};


/**
 * struct tc956x_ioctl_pcie_set_tx_margin - IOCTL arguments for
 * PCIe USP and DSPs tx margin change for power reduction
 * txmargin - target tx margin value.
 * port - USP/DSP1/DSP2 on which Tx margin setting to be done
 */
struct tc956x_ioctl_pcie_set_tx_margin {
	__u32 cmd;
	__u16 txmargin; /* Set Only LSB 0:8 bits valid*/
	enum ports port; /* USP, DSP1, DSP2*/
};

/**
 * struct tc956x_ioctl_pcie_set_tx_deemphasis - IOCTL arguments for
 * PCIe USP and DSPs tx de-emphasis change for power reduction
 * enable - enable or disable tx demphasis setting value.
 * txpreset - Gen3 Tx preset value as defined by PCIe Base Specifications
 * port - USP/DSP1/DSP2 on which Tx Deemphasis setting to be done
 * Note: Gen1, Gen2 txpreset configuration not supported
 */
struct tc956x_ioctl_pcie_set_tx_deemphasis {
	__u32 cmd;
	__u8 enable; /* 1: enable, 0: disable*/
	__u8 txpreset; /* Gen3 tx preset, valid values are from 0 to 10; dont care in case of 'disable' selection */
	enum ports port; /* USP, DSP1, DSP2*/
};


/**
 * struct tc956x_ioctl_pcie_set_dfe - IOCTL arguments for
 * PCIe USP and DSPs DFE disable/enable for power reduction
 * enable - enable or disable DFE
 * port - USP/DSP1/DSP2 on which DFE should be enabled/disabled
 */
struct tc956x_ioctl_pcie_set_dfe{
	__u32 cmd;
	__u8 enable; /* 1: enable, 0: disable*/
	enum ports port; /* USP, DSP1, DSP2*/
};

/**
 * struct tc956x_ioctl_pcie_set_ctle_fixed_mode - IOCTL arguments for
 * PCIe USP and DSPs CTLE configuration
 * eqc_force - CTLE C value
 * eq_res - CTLE R value
 * vga_ctrl - CTLE VGA value
 * port - USP/DSP1/DSP2 on which CTLE fixed mode setting to be done
 */

struct tc956x_ioctl_pcie_set_ctle_fixed_mode{
	__u32 cmd;
	__u8 eqc_force;
	__u8 eq_res;
	__u8 vga_ctrl;
	enum ports port; /* USP, DSP1, DSP2r*/
};

/**
 * struct tc956x_ioctl_pcie_set_speed - IOCTL arguments for
 * PCIe USP and DSPs speed change
 * speed - target pcie gen speed
 */
struct tc956x_ioctl_pcie_set_speed {
	__u32 cmd;
	enum pcie_speed speed; /*1 or 2 or 3*/
};


/**
 * struct tc956x_ioctl_logstat - IOCTL arguments for Log
 *                               and Statistics configuration
 *
 * If TC956X_PCIE_SET_LOGSTAT_CONF IOCTL used, user will
 * set tc956x_ltssm_conf for the specified port.
 * Otherwise, if TC956X_PCIE_GET_LOGSTAT_CONF IOCTL used,
 * the driver will return tc956x_ltssm_conf
 * for the port passed as IOCTL arguments.
 */
struct tc956x_ioctl_logstatconf {
	__u32 cmd;
	struct tc956x_ltssm_conf *logstat_conf;
	enum ports port;
};

/**
 * struct tc956x_ioctl_ltssm - IOCTL arguments for State log data
 *
 * Driver will return tc956x_ltssm_log
 * for the port passed as IOCTL arguments.
 */
struct tc956x_ioctl_ltssm {
	__u32 cmd;
	struct tc956x_ltssm_log *ltssm_logd;
	enum ports port;
};

#endif /* #ifdef TC956X_PCIE_LOGSTAT */

#endif /*_IOCT_H */
