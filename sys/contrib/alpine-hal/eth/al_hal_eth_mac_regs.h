/*-
*******************************************************************************
Copyright (C) 2015 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 as published by the Free Software Foundation and can be
found at http://www.gnu.org/licenses/gpl-2.0.html

Alternatively, redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following conditions are
met:

    *     Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

    *     Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

/**
 *  @{
 * @file   al_hal_eth_mac_regs.h
 *
 * @brief Ethernet MAC registers
 *
 */

#ifndef __AL_HAL_ETH_MAC_REGS_H__
#define __AL_HAL_ETH_MAC_REGS_H__

#include "al_hal_plat_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/*
* Unit Registers
*/

struct al_eth_mac_1g_stats {
	uint32_t reserved1[2];
	uint32_t aFramesTransmittedOK;			/* 0x68 */
	uint32_t aFramesReceivedOK;			/* 0x6c */
	uint32_t aFrameCheckSequenceErrors;		/* 0x70 */
	uint32_t aAlignmentErrors;			/* 0x74 */
	uint32_t aOctetsTransmittedOK;			/* 0x78 */
	uint32_t aOctetsReceivedOK;			/* 0x7c */
	uint32_t aPAUSEMACCtrlFramesTransmitted;	/* 0x80 */
	uint32_t aPAUSEMACCtrlFramesReceived;		/* 0x84 */
	uint32_t ifInErrors	;			/* 0x88 */
	uint32_t ifOutErrors;				/* 0x8c */
	uint32_t ifInUcastPkts;				/* 0x90 */
	uint32_t ifInMulticastPkts;			/* 0x94 */
	uint32_t ifInBroadcastPkts;			/* 0x98 */
	uint32_t reserved2;
	uint32_t ifOutUcastPkts;			/* 0xa0 */
	uint32_t ifOutMulticastPkts;			/* 0xa4 */
	uint32_t ifOutBroadcastPkts;			/* 0xa8 */
	uint32_t etherStatsDropEvents;			/* 0xac */
	uint32_t etherStatsOctets;			/* 0xb0 */
	uint32_t etherStatsPkts;			/* 0xb4 */
	uint32_t etherStatsUndersizePkts;		/* 0xb8 */
	uint32_t etherStatsOversizePkts;		/* 0xbc */
	uint32_t etherStatsPkts64Octets;		/* 0xc0 */
	uint32_t etherStatsPkts65to127Octets;		/* 0xc4 */
	uint32_t etherStatsPkts128to255Octets;		/* 0xc8 */
	uint32_t etherStatsPkts256to511Octets;		/* 0xcc */
	uint32_t etherStatsPkts512to1023Octets;		/* 0xd0 */
	uint32_t etherStatsPkts1024to1518Octets;	/* 0xd4 */
	uint32_t etherStatsPkts1519toX;			/* 0xd8 */
	uint32_t etherStatsJabbers;			/* 0xdc */
	uint32_t etherStatsFragments;			/* 0xe0 */
	uint32_t reserved3[71];
};

struct al_eth_mac_1g {
	/* [0x0] */
	uint32_t rev;
	uint32_t scratch;
	uint32_t cmd_cfg;
	uint32_t mac_0;
	/* [0x10] */
	uint32_t mac_1;
	uint32_t frm_len;
	uint32_t pause_quant;
	uint32_t rx_section_empty;
	/* [0x20] */
	uint32_t rx_section_full;
	uint32_t tx_section_empty;
	uint32_t tx_section_full;
	uint32_t rx_almost_empty;
	/* [0x30] */
	uint32_t rx_almost_full;
	uint32_t tx_almost_empty;
	uint32_t tx_almost_full;
	uint32_t mdio_addr0;
	/* [0x40] */
	uint32_t mdio_addr1;
	uint32_t Reserved[5];
	/* [0x58] */
	uint32_t reg_stat;
	uint32_t tx_ipg_len;
	/* [0x60] */
	struct al_eth_mac_1g_stats stats;
	/* [0x200] */
	uint32_t phy_regs_base;
	uint32_t Reserved2[127];
};

struct al_eth_mac_10g_stats_v2 {
	uint32_t aFramesTransmittedOK;			/* 0x80 */
	uint32_t reserved1;
	uint32_t aFramesReceivedOK;			/* 0x88 */
	uint32_t reserved2;
	uint32_t aFrameCheckSequenceErrors;		/* 0x90 */
	uint32_t reserved3;
	uint32_t aAlignmentErrors;			/* 0x98 */
	uint32_t reserved4;
	uint32_t aPAUSEMACCtrlFramesTransmitted;	/* 0xa0 */
	uint32_t reserved5;
	uint32_t aPAUSEMACCtrlFramesReceived;		/* 0xa8 */
	uint32_t reserved6;
	uint32_t aFrameTooLongErrors;			/* 0xb0 */
	uint32_t reserved7;
	uint32_t aInRangeLengthErrors;			/* 0xb8 */
	uint32_t reserved8;
	uint32_t VLANTransmittedOK;			/* 0xc0 */
	uint32_t reserved9;
	uint32_t VLANReceivedOK;			/* 0xc8 */
	uint32_t reserved10;
	uint32_t ifOutOctetsL;				/* 0xd0 */
	uint32_t ifOutOctetsH;				/* 0xd4 */
	uint32_t ifInOctetsL;				/* 0xd8 */
	uint32_t ifInOctetsH;				/* 0xdc */
	uint32_t ifInUcastPkts;				/* 0xe0 */
	uint32_t reserved11;
	uint32_t ifInMulticastPkts;			/* 0xe8 */
	uint32_t reserved12;
	uint32_t ifInBroadcastPkts;			/* 0xf0 */
	uint32_t reserved13;
	uint32_t ifOutErrors;				/* 0xf8 */
	uint32_t reserved14[3];
	uint32_t ifOutUcastPkts;			/* 0x108 */
	uint32_t reserved15;
	uint32_t ifOutMulticastPkts;			/* 0x110 */
	uint32_t reserved16;
	uint32_t ifOutBroadcastPkts;			/* 0x118 */
	uint32_t reserved17;
	uint32_t etherStatsDropEvents;			/* 0x120 */
	uint32_t reserved18;
	uint32_t etherStatsOctets;			/* 0x128 */
	uint32_t reserved19;
	uint32_t etherStatsPkts;			/* 0x130 */
	uint32_t reserved20;
	uint32_t etherStatsUndersizePkts;		/* 0x138 */
	uint32_t reserved21;
	uint32_t etherStatsPkts64Octets;		/* 0x140 */
	uint32_t reserved22;
	uint32_t etherStatsPkts65to127Octets;		/* 0x148 */
	uint32_t reserved23;
	uint32_t etherStatsPkts128to255Octets;		/* 0x150 */
	uint32_t reserved24;
	uint32_t etherStatsPkts256to511Octets;		/* 0x158 */
	uint32_t reserved25;
	uint32_t etherStatsPkts512to1023Octets;		/* 0x160 */
	uint32_t reserved26;
	uint32_t etherStatsPkts1024to1518Octets;	/* 0x168 */
	uint32_t reserved27;
	uint32_t etherStatsPkts1519toX;			/* 0x170 */
	uint32_t reserved28;
	uint32_t etherStatsOversizePkts;		/* 0x178 */
	uint32_t reserved29;
	uint32_t etherStatsJabbers;			/* 0x180 */
	uint32_t reserved30;
	uint32_t etherStatsFragments;			/* 0x188 */
	uint32_t reserved31;
	uint32_t ifInErrors;				/* 0x190 */
	uint32_t reserved32[91];
};

struct al_eth_mac_10g_stats_v3_rx {
	uint32_t etherStatsOctets;			/* 0x00 */
	uint32_t reserved2;
	uint32_t ifOctetsL;				/* 0x08 */
	uint32_t ifOctetsH;				/* 0x0c */
	uint32_t aAlignmentErrors;			/* 0x10 */
	uint32_t reserved4;
	uint32_t aPAUSEMACCtrlFrames;			/* 0x18 */
	uint32_t reserved5;
	uint32_t FramesOK;				/* 0x20 */
	uint32_t reserved6;
	uint32_t CRCErrors;				/* 0x28 */
	uint32_t reserved7;
	uint32_t VLANOK;				/* 0x30 */
	uint32_t reserved8;
	uint32_t ifInErrors;				/* 0x38 */
	uint32_t reserved9;
	uint32_t ifInUcastPkts;				/* 0x40 */
	uint32_t reserved10;
	uint32_t ifInMulticastPkts;			/* 0x48 */
	uint32_t reserved11;
	uint32_t ifInBroadcastPkts;			/* 0x50 */
	uint32_t reserved12;
	uint32_t etherStatsDropEvents;			/* 0x58 */
	uint32_t reserved13;
	uint32_t etherStatsPkts;			/* 0x60 */
	uint32_t reserved14;
	uint32_t etherStatsUndersizePkts;		/* 0x68 */
	uint32_t reserved15;
	uint32_t etherStatsPkts64Octets;		/* 0x70 */
	uint32_t reserved16;
	uint32_t etherStatsPkts65to127Octets;		/* 0x78 */
	uint32_t reserved17;
	uint32_t etherStatsPkts128to255Octets;		/* 0x80 */
	uint32_t reserved18;
	uint32_t etherStatsPkts256to511Octets;		/* 0x88 */
	uint32_t reserved19;
	uint32_t etherStatsPkts512to1023Octets;		/* 0x90 */
	uint32_t reserved20;
	uint32_t etherStatsPkts1024to1518Octets;	/* 0x98 */
	uint32_t reserved21;
	uint32_t etherStatsPkts1519toMax;		/* 0xa0 */
	uint32_t reserved22;
	uint32_t etherStatsOversizePkts;		/* 0xa8 */
	uint32_t reserved23;
	uint32_t etherStatsJabbers;			/* 0xb0 */
	uint32_t reserved24;
	uint32_t etherStatsFragments;			/* 0xb8 */
	uint32_t reserved25;
	uint32_t aMACControlFramesReceived;		/* 0xc0 */
	uint32_t reserved26;
	uint32_t aFrameTooLong;				/* 0xc8 */
	uint32_t reserved27;
	uint32_t aInRangeLengthErrors;			/* 0xd0 */
	uint32_t reserved28;
	uint32_t reserved29[10];
};

struct al_eth_mac_10g_stats_v3_tx {
	uint32_t etherStatsOctets;			/* 0x00 */
	uint32_t reserved30;
	uint32_t ifOctetsL;				/* 0x08 */
	uint32_t ifOctetsH;				/* 0x0c */
	uint32_t aAlignmentErrors;			/* 0x10 */
	uint32_t reserved32;
	uint32_t aPAUSEMACCtrlFrames;			/* 0x18 */
	uint32_t reserved33;
	uint32_t FramesOK;				/* 0x20 */
	uint32_t reserved34;
	uint32_t CRCErrors;				/* 0x28 */
	uint32_t reserved35;
	uint32_t VLANOK;				/* 0x30 */
	uint32_t reserved36;
	uint32_t ifOutErrors;				/* 0x38 */
	uint32_t reserved37;
	uint32_t ifUcastPkts;				/* 0x40 */
	uint32_t reserved38;
	uint32_t ifMulticastPkts;			/* 0x48 */
	uint32_t reserved39;
	uint32_t ifBroadcastPkts;			/* 0x50 */
	uint32_t reserved40;
	uint32_t etherStatsDropEvents;			/* 0x58 */
	uint32_t reserved41;
	uint32_t etherStatsPkts;			/* 0x60 */
	uint32_t reserved42;
	uint32_t etherStatsUndersizePkts;		/* 0x68 */
	uint32_t reserved43;
	uint32_t etherStatsPkts64Octets;		/* 0x70 */
	uint32_t reserved44;
	uint32_t etherStatsPkts65to127Octets;		/* 0x78 */
	uint32_t reserved45;
	uint32_t etherStatsPkts128to255Octets;		/* 0x80 */
	uint32_t reserved46;
	uint32_t etherStatsPkts256to511Octets;		/* 0x88 */
	uint32_t reserved47;
	uint32_t etherStatsPkts512to1023Octets;		/* 0x90 */
	uint32_t reserved48;
	uint32_t etherStatsPkts1024to1518Octets;	/* 0x98 */
	uint32_t reserved49;
	uint32_t etherStatsPkts1519toTX_MTU;		/* 0xa0 */
	uint32_t reserved50;
	uint32_t reserved51[4];
	uint32_t aMACControlFrames;			/* 0xc0 */
	uint32_t reserved52[15];
};

struct al_eth_mac_10g_stats_v3 {
	uint32_t reserved1[32];
	/* 0x100 */
	struct al_eth_mac_10g_stats_v3_rx	rx;
	/* 0x200 */
	struct al_eth_mac_10g_stats_v3_tx	tx;
};

union al_eth_mac_10g_stats {
	struct al_eth_mac_10g_stats_v2	v2;
	struct al_eth_mac_10g_stats_v3	v3;
};

struct al_eth_mac_10g {
	/* [0x0] */
	uint32_t rev;
	uint32_t scratch;
	uint32_t cmd_cfg;
	uint32_t mac_0;
	/* [0x10] */
	uint32_t mac_1;
	uint32_t frm_len;
	uint32_t Reserved;
	uint32_t rx_fifo_sections;
	/* [0x20] */
	uint32_t tx_fifo_sections;
	uint32_t rx_fifo_almost_f_e;
	uint32_t tx_fifo_almost_f_e;
	uint32_t hashtable_load;
	/* [0x30] */
	uint32_t mdio_cfg_status;
	uint16_t mdio_cmd;
	uint16_t reserved1;
	uint16_t mdio_data;
	uint16_t reserved2;
	uint16_t mdio_regaddr;
	uint16_t reserved3;
	/* [0x40] */
	uint32_t status;
	uint32_t tx_ipg_len;
	uint32_t Reserved1[3];
	/* [0x54] */
	uint32_t cl01_pause_quanta;
	uint32_t cl23_pause_quanta;
	uint32_t cl45_pause_quanta;
	/* [0x60] */
	uint32_t cl67_pause_quanta;
	uint32_t cl01_quanta_thresh;
	uint32_t cl23_quanta_thresh;
	uint32_t cl45_quanta_thresh;
	/* [0x70] */
	uint32_t cl67_quanta_thresh;
	uint32_t rx_pause_status;
	uint32_t Reserved2;
	uint32_t ts_timestamp;
	/* [0x80] */
	union al_eth_mac_10g_stats stats;

	/* [0x300] */
	uint32_t control;
	uint32_t status_reg;
	uint32_t phy_id[2];
	/* [0x310] */
	uint32_t dev_ability;
	uint32_t partner_ability;
	uint32_t an_expansion;
	uint32_t device_np;
	/* [0x320] */
	uint32_t partner_np;
	uint32_t Reserved4[9];

	/* [0x348] */
	uint32_t link_timer_lo;
	uint32_t link_timer_hi;
	/* [0x350] */
	uint32_t if_mode;

	uint32_t Reserved5[43];
};

struct al_eth_mac_gen {
	/* [0x0]  Ethernet Controller Version */
	uint32_t version;
	uint32_t rsrvd_0[2];
	/* [0xc] MAC selection configuration */
	uint32_t cfg;
	/* [0x10] 10/100/1000 MAC external configuration */
	uint32_t mac_1g_cfg;
	/* [0x14] 10/100/1000 MAC status */
	uint32_t mac_1g_stat;
	/* [0x18] RGMII external configuration */
	uint32_t rgmii_cfg;
	/* [0x1c] RGMII status */
	uint32_t rgmii_stat;
	/* [0x20] 1/2.5/10G MAC external configuration */
	uint32_t mac_10g_cfg;
	/* [0x24] 1/2.5/10G MAC status */
	uint32_t mac_10g_stat;
	/* [0x28] XAUI PCS configuration */
	uint32_t xaui_cfg;
	/* [0x2c] XAUI PCS status */
	uint32_t xaui_stat;
	/* [0x30] RXAUI PCS configuration */
	uint32_t rxaui_cfg;
	/* [0x34] RXAUI PCS status */
	uint32_t rxaui_stat;
	/* [0x38] Signal detect configuration */
	uint32_t sd_cfg;
	/* [0x3c] MDIO control register for MDIO interface 1 */
	uint32_t mdio_ctrl_1;
	/* [0x40] MDIO information register for MDIO interface 1 */
	uint32_t mdio_1;
	/* [0x44] MDIO control register for MDIO interface 2 */
	uint32_t mdio_ctrl_2;
	/* [0x48] MDIO information register for MDIO interface 2 */
	uint32_t mdio_2;
	/* [0x4c] XGMII 32 to 64 data FIFO control */
	uint32_t xgmii_dfifo_32_64;
	/* [0x50] Reserved 1 out */
	uint32_t mac_res_1_out;
	/* [0x54] XGMII 64 to 32 data FIFO control */
	uint32_t xgmii_dfifo_64_32;
	/* [0x58] Reserved 1 in */
	uint32_t mac_res_1_in;
	/* [0x5c] SerDes TX FIFO control */
	uint32_t sd_fifo_ctrl;
	/* [0x60] SerDes TX FIFO status */
	uint32_t sd_fifo_stat;
	/* [0x64] SerDes in/out selection */
	uint32_t mux_sel;
	/* [0x68] Clock configuration */
	uint32_t clk_cfg;
	uint32_t rsrvd_1;
	/* [0x70] LOS and SD selection */
	uint32_t los_sel;
	/* [0x74] RGMII selection configuration */
	uint32_t rgmii_sel;
	/* [0x78] Ethernet LED configuration */
	uint32_t led_cfg;
	uint32_t rsrvd[33];
};
struct al_eth_mac_kr {
	/* [0x0] PCS register file address */
	uint32_t pcs_addr;
	/* [0x4] PCS register file data */
	uint32_t pcs_data;
	/* [0x8] AN register file address */
	uint32_t an_addr;
	/* [0xc] AN register file data */
	uint32_t an_data;
	/* [0x10] PMA register file address */
	uint32_t pma_addr;
	/* [0x14] PMA register file data */
	uint32_t pma_data;
	/* [0x18] MTIP register file address */
	uint32_t mtip_addr;
	/* [0x1c] MTIP register file data */
	uint32_t mtip_data;
	/* [0x20] KR PCS config  */
	uint32_t pcs_cfg;
	/* [0x24] KR PCS status  */
	uint32_t pcs_stat;
	uint32_t rsrvd[54];
};
struct al_eth_mac_sgmii {
	/* [0x0] PCS register file address */
	uint32_t reg_addr;
	/* [0x4] PCS register file data */
	uint32_t reg_data;
	/* [0x8] PCS clock divider configuration */
	uint32_t clk_div;
	/* [0xc] PCS Status */
	uint32_t link_stat;
	uint32_t rsrvd[60];
};
struct al_eth_mac_stat {
	/* [0x0] Receive rate matching error */
	uint32_t match_fault;
	/* [0x4] EEE, number of times the MAC went into low power mode */
	uint32_t eee_in;
	/* [0x8] EEE, number of times the MAC went out of low power mode */
	uint32_t eee_out;
	/*
	 * [0xc] 40G PCS,
	 * FEC corrected error indication
	 */
	uint32_t v3_pcs_40g_ll_cerr_0;
	/*
	 * [0x10] 40G PCS,
	 * FEC corrected error indication
	 */
	uint32_t v3_pcs_40g_ll_cerr_1;
	/*
	 * [0x14] 40G PCS,
	 * FEC corrected error indication
	 */
	uint32_t v3_pcs_40g_ll_cerr_2;
	/*
	 * [0x18] 40G PCS,
	 * FEC corrected error indication
	 */
	uint32_t v3_pcs_40g_ll_cerr_3;
	/*
	 * [0x1c] 40G PCS,
	 * FEC uncorrectable error indication
	 */
	uint32_t v3_pcs_40g_ll_ncerr_0;
	/*
	 * [0x20] 40G PCS,
	 * FEC uncorrectable error indication
	 */
	uint32_t v3_pcs_40g_ll_ncerr_1;
	/*
	 * [0x24] 40G PCS,
	 * FEC uncorrectable error indication
	 */
	uint32_t v3_pcs_40g_ll_ncerr_2;
	/*
	 * [0x28] 40G PCS,
	 * FEC uncorrectable error indication
	 */
	uint32_t v3_pcs_40g_ll_ncerr_3;
	/*
	 * [0x2c] 10G_LL PCS,
	 * FEC corrected error indication
	 */
	uint32_t v3_pcs_10g_ll_cerr;
	/*
	 * [0x30] 10G_LL PCS,
	 * FEC uncorrectable error indication
	 */
	uint32_t v3_pcs_10g_ll_ncerr;
	uint32_t rsrvd[51];
};
struct al_eth_mac_stat_lane {
	/* [0x0] Character error */
	uint32_t char_err;
	/* [0x4] Disparity error */
	uint32_t disp_err;
	/* [0x8] Comma detection */
	uint32_t pat;
	uint32_t rsrvd[13];
};
struct al_eth_mac_gen_v3 {
	/* [0x0] ASYNC FIFOs control */
	uint32_t afifo_ctrl;
	/* [0x4] TX ASYNC FIFO configuration */
	uint32_t tx_afifo_cfg_1;
	/* [0x8] TX ASYNC FIFO configuration */
	uint32_t tx_afifo_cfg_2;
	/* [0xc] TX ASYNC FIFO configuration */
	uint32_t tx_afifo_cfg_3;
	/* [0x10] TX ASYNC FIFO configuration */
	uint32_t tx_afifo_cfg_4;
	/* [0x14] TX ASYNC FIFO configuration */
	uint32_t tx_afifo_cfg_5;
	/* [0x18] RX ASYNC FIFO configuration */
	uint32_t rx_afifo_cfg_1;
	/* [0x1c] RX ASYNC FIFO configuration */
	uint32_t rx_afifo_cfg_2;
	/* [0x20] RX ASYNC FIFO configuration */
	uint32_t rx_afifo_cfg_3;
	/* [0x24] RX ASYNC FIFO configuration */
	uint32_t rx_afifo_cfg_4;
	/* [0x28] RX ASYNC FIFO configuration */
	uint32_t rx_afifo_cfg_5;
	/* [0x2c] MAC selection configuration */
	uint32_t mac_sel;
	/* [0x30] 10G LL MAC configuration */
	uint32_t mac_10g_ll_cfg;
	/* [0x34] 10G LL MAC control */
	uint32_t mac_10g_ll_ctrl;
	/* [0x38] 10G LL PCS configuration */
	uint32_t pcs_10g_ll_cfg;
	/* [0x3c] 10G LL PCS status */
	uint32_t pcs_10g_ll_status;
	/* [0x40] 40G LL PCS configuration */
	uint32_t pcs_40g_ll_cfg;
	/* [0x44] 40G LL PCS status */
	uint32_t pcs_40g_ll_status;
	/* [0x48] PCS 40G  register file address */
	uint32_t pcs_40g_ll_addr;
	/* [0x4c] PCS 40G register file data */
	uint32_t pcs_40g_ll_data;
	/* [0x50] 40G LL MAC configuration */
	uint32_t mac_40g_ll_cfg;
	/* [0x54] 40G LL MAC status */
	uint32_t mac_40g_ll_status;
	/* [0x58] Preamble configuration (high [55:32]) */
	uint32_t preamble_cfg_high;
	/* [0x5c] Preamble configuration (low [31:0]) */
	uint32_t preamble_cfg_low;
	/* [0x60] MAC 40G register file address */
	uint32_t mac_40g_ll_addr;
	/* [0x64] MAC 40G register file data */
	uint32_t mac_40g_ll_data;
	/* [0x68] 40G LL MAC control */
	uint32_t mac_40g_ll_ctrl;
	/* [0x6c] PCS 40G  register file address */
	uint32_t pcs_40g_fec_91_ll_addr;
	/* [0x70] PCS 40G register file data */
	uint32_t pcs_40g_fec_91_ll_data;
	/* [0x74] 40G LL PCS EEE configuration */
	uint32_t pcs_40g_ll_eee_cfg;
	/* [0x78] 40G LL PCS EEE status */
	uint32_t pcs_40g_ll_eee_status;
	/*
	 * [0x7c] SERDES 32-bit interface shift configuration (when swap is
	 * enabled)
	 */
	uint32_t serdes_32_tx_shift;
	/*
	 * [0x80] SERDES 32-bit interface shift configuration (when swap is
	 * enabled)
	 */
	uint32_t serdes_32_rx_shift;
	/*
	 * [0x84] SERDES 32-bit interface bit selection
	 */
	uint32_t serdes_32_tx_sel;
	/*
	 * [0x88] SERDES 32-bit interface bit selection
	 */
	uint32_t serdes_32_rx_sel;
	/* [0x8c] AN/LT wrapper  control */
	uint32_t an_lt_ctrl;
	/* [0x90] AN/LT wrapper  register file address */
	uint32_t an_lt_0_addr;
	/* [0x94] AN/LT wrapper register file data */
	uint32_t an_lt_0_data;
	/* [0x98] AN/LT wrapper  register file address */
	uint32_t an_lt_1_addr;
	/* [0x9c] AN/LT wrapper register file data */
	uint32_t an_lt_1_data;
	/* [0xa0] AN/LT wrapper  register file address */
	uint32_t an_lt_2_addr;
	/* [0xa4] AN/LT wrapper register file data */
	uint32_t an_lt_2_data;
	/* [0xa8] AN/LT wrapper  register file address */
	uint32_t an_lt_3_addr;
	/* [0xac] AN/LT wrapper register file data */
	uint32_t an_lt_3_data;
	/* [0xb0] External SERDES control */
	uint32_t ext_serdes_ctrl;
	/* [0xb4] spare bits */
	uint32_t spare;
	uint32_t rsrvd[18];
};

struct al_eth_mac_regs {
	struct al_eth_mac_1g mac_1g;				/* [0x000] */
	struct al_eth_mac_10g mac_10g;				/* [0x400] */
	uint32_t rsrvd_0[64];					/* [0x800] */
	struct al_eth_mac_gen gen;                              /* [0x900] */
	struct al_eth_mac_kr kr;                                /* [0xa00] */
	struct al_eth_mac_sgmii sgmii;                          /* [0xb00] */
	struct al_eth_mac_stat stat;                            /* [0xc00] */
	struct al_eth_mac_stat_lane stat_lane[4];               /* [0xd00] */
	struct al_eth_mac_gen_v3 gen_v3;                        /* [0xe00] */
};


/*
* Registers Fields
*/

/**** 1G MAC registers ****/
/* cmd_cfg */
#define ETH_1G_MAC_CMD_CFG_TX_ENA	(1 << 0)
#define ETH_1G_MAC_CMD_CFG_RX_ENA	(1 << 1)
/* enable Half Duplex */
#define ETH_1G_MAC_CMD_CFG_HD_EN	(1 << 10)
/* enable 1G speed */
#define ETH_1G_MAC_CMD_CFG_1G_SPD	(1 << 3)
/* enable 10M speed */
#define ETH_1G_MAC_CMD_CFG_10M_SPD	(1 << 25)

/**** 10G MAC registers ****/
/* cmd_cfg */
#define ETH_10G_MAC_CMD_CFG_TX_ENA				(1 << 0)
#define ETH_10G_MAC_CMD_CFG_RX_ENA				(1 << 1)
#define ETH_10G_MAC_CMD_CFG_WAN_MODE			(1 << 3)
#define ETH_10G_MAC_CMD_CFG_PROMIS_EN			(1 << 4)
#define ETH_10G_MAC_CMD_CFG_PAD_EN				(1 << 5)
#define ETH_10G_MAC_CMD_CFG_CRC_FWD				(1 << 6)
#define ETH_10G_MAC_CMD_CFG_PAUSE_FWD			(1 << 7)
#define ETH_10G_MAC_CMD_CFG_PAUSE_IGNORE		(1 << 8)
#define ETH_10G_MAC_CMD_CFG_TX_ADDR_INS			(1 << 9)
#define ETH_10G_MAC_CMD_CFG_LOOP_ENA			(1 << 10)
#define ETH_10G_MAC_CMD_CFG_TX_PAD_EN			(1 << 11)
#define ETH_10G_MAC_CMD_CFG_SW_RESET			(1 << 12)
#define ETH_10G_MAC_CMD_CFG_CNTL_FRM_ENA		(1 << 13)
#define ETH_10G_MAC_CMD_CFG_RX_ERR_DISC			(1 << 14)
#define ETH_10G_MAC_CMD_CFG_PHY_TXENA			(1 << 15)
#define ETH_10G_MAC_CMD_CFG_FORCE_SEND_IDLE		(1 << 16)
#define ETH_10G_MAC_CMD_CFG_NO_LGTH_CHECK		(1 << 17)
#define ETH_10G_MAC_CMD_CFG_COL_CNT_EXT			(1 << 18)
#define ETH_10G_MAC_CMD_CFG_PFC_MODE			(1 << 19)
#define ETH_10G_MAC_CMD_CFG_PAUSE_PFC_COMP		(1 << 20)
#define ETH_10G_MAC_CMD_CFG_SFD_ANY				(1 << 21)
#define ETH_10G_MAC_CMD_CFG_TX_FLUSH			(1 << 22)
#define ETH_10G_MAC_CMD_CFG_TX_LOWP_ENA			(1 << 23)
#define ETH_10G_MAC_CMD_CFG_REG_LOWP_RXEMPTY	(1 << 24)
#define ETH_10G_MAC_CMD_CFG_SHORT_DISCARD		(1 << 25)

/* mdio_cfg_status */
#define ETH_10G_MAC_MDIO_CFG_HOLD_TIME_MASK	0x0000001c
#define ETH_10G_MAC_MDIO_CFG_HOLD_TIME_SHIFT	2

#define ETH_10G_MAC_MDIO_CFG_HOLD_TIME_1_CLK	0
#define ETH_10G_MAC_MDIO_CFG_HOLD_TIME_3_CLK	1
#define ETH_10G_MAC_MDIO_CFG_HOLD_TIME_5_CLK	2
#define ETH_10G_MAC_MDIO_CFG_HOLD_TIME_7_CLK	3
#define ETH_10G_MAC_MDIO_CFG_HOLD_TIME_9_CLK	4
#define ETH_10G_MAC_MDIO_CFG_HOLD_TIME_11_CLK	5
#define ETH_10G_MAC_MDIO_CFG_HOLD_TIME_13_CLK	6
#define ETH_10G_MAC_MDIO_CFG_HOLD_TIME_15_CLK	7

/* control */
#define ETH_10G_MAC_CONTROL_AN_EN_MASK	0x00001000
#define ETH_10G_MAC_CONTROL_AN_EN_SHIFT	12

/* if_mode */
#define ETH_10G_MAC_IF_MODE_SGMII_EN_MASK	0x00000001
#define ETH_10G_MAC_IF_MODE_SGMII_EN_SHIFT	0
#define ETH_10G_MAC_IF_MODE_SGMII_AN_MASK	0x00000002
#define ETH_10G_MAC_IF_MODE_SGMII_AN_SHIFT	1
#define ETH_10G_MAC_IF_MODE_SGMII_SPEED_MASK	0x0000000c
#define ETH_10G_MAC_IF_MODE_SGMII_SPEED_SHIFT	2
#define ETH_10G_MAC_IF_MODE_SGMII_DUPLEX_MASK	0x00000010
#define ETH_10G_MAC_IF_MODE_SGMII_DUPLEX_SHIFT	4

#define ETH_10G_MAC_IF_MODE_SGMII_SPEED_10M	0
#define ETH_10G_MAC_IF_MODE_SGMII_SPEED_100M	1
#define ETH_10G_MAC_IF_MODE_SGMII_SPEED_1G	2

#define ETH_10G_MAC_IF_MODE_SGMII_DUPLEX_FULL	0
#define ETH_10G_MAC_IF_MODE_SGMII_DUPLEX_HALF	1

/**** version register ****/
/*  Revision number (Minor) */
#define ETH_MAC_GEN_VERSION_RELEASE_NUM_MINOR_MASK 0x000000FF
#define ETH_MAC_GEN_VERSION_RELEASE_NUM_MINOR_SHIFT 0
/*  Revision number (Major) */
#define ETH_MAC_GEN_VERSION_RELEASE_NUM_MAJOR_MASK 0x0000FF00
#define ETH_MAC_GEN_VERSION_RELEASE_NUM_MAJOR_SHIFT 8
/*  Date of release */
#define ETH_MAC_GEN_VERSION_DATE_DAY_MASK 0x001F0000
#define ETH_MAC_GEN_VERSION_DATE_DAY_SHIFT 16
/*  Month of release */
#define ETH_MAC_GEN_VERSION_DATA_MONTH_MASK 0x01E00000
#define ETH_MAC_GEN_VERSION_DATA_MONTH_SHIFT 21
/*  Year of release (starting from 2000) */
#define ETH_MAC_GEN_VERSION_DATE_YEAR_MASK 0x3E000000
#define ETH_MAC_GEN_VERSION_DATE_YEAR_SHIFT 25
/*  Reserved */
#define ETH_MAC_GEN_VERSION_RESERVED_MASK 0xC0000000
#define ETH_MAC_GEN_VERSION_RESERVED_SHIFT 30

/**** cfg register ****/
/*
 * Selects between the 10/100/1000 MAC and the 1/2.5/10G MAC:
 * 0 - 10/100/1000
 * 1 - 1/2.5/10G
 */
#define ETH_MAC_GEN_CFG_MAC_1_10         (1 << 0)
/*
 * Selects the operation mode of the 1/2.5/10G MAC:
 * 00 - 1/2.5G SGMII
 * 01 - 10G XAUI/RXAUI
 * 10 – 10G KR
 * 11 – Reserved
 */
#define ETH_MAC_GEN_CFG_XGMII_SGMII_MASK 0x00000006
#define ETH_MAC_GEN_CFG_XGMII_SGMII_SHIFT 1
/*
 * Selects the operation mode of the PCS:
 * 0 - XAUI
 * 1 - RXAUI
 */
#define ETH_MAC_GEN_CFG_XAUI_RXAUI       (1 << 3)
/* Swap bits of TBI (SGMII mode) interface */
#define ETH_MAC_GEN_CFG_SWAP_TBI_RX      (1 << 4)
/*
 * Determines the offset of the TBI bus on the SerDes interface:
 * 0 - LSB
 * 1 - MSB
 */
#define ETH_MAC_GEN_CFG_TBI_MSB_RX       (1 << 5)
/*
 * Selects the SGMII PCS/MAC:
 * 0 – 10G MAC with SGMII
 * 1 – 1G MAC with SGMII
 */
#define ETH_MAC_GEN_CFG_SGMII_SEL        (1 << 6)
/*
 * Selects between RGMII and SGMII for the 1G MAC:
 * 0 – RGMII
 * 1 – SGMII
 */
#define ETH_MAC_GEN_CFG_RGMII_SGMII_SEL  (1 << 7)
/* Swap bits of TBI (SGMII mode) interface */
#define ETH_MAC_GEN_CFG_SWAP_TBI_TX      (1 << 8)
/*
 * Determines the offset of the TBI bus on the SerDes interface:
 *  0 - LSB
 * 1 - MSB
 */
#define ETH_MAC_GEN_CFG_TBI_MSB_TX       (1 << 9)
/*
 * Selection between the MDIO from 10/100/1000 MAC or the 1/2.5/10G MAC
 * 0 - 10/100/1000
 * 1 - 1/2.5/10G
 */
#define ETH_MAC_GEN_CFG_MDIO_1_10        (1 << 10)
/*
 * Swap MDC output
 * 0 – Normal
 * 1 – Flipped
 */
#define ETH_MAC_GEN_CFG_MDIO_POL         (1 << 11)
/* Swap bits on SerDes interface */
#define ETH_MAC_GEN_CFG_SWAP_SERDES_RX_MASK 0x000F0000
#define ETH_MAC_GEN_CFG_SWAP_SERDES_RX_SHIFT 16
/* Swap bits on SerDes interface */
#define ETH_MAC_GEN_CFG_SWAP_SERDES_TX_MASK 0x0F000000
#define ETH_MAC_GEN_CFG_SWAP_SERDES_TX_SHIFT 24

/**** mac_1g_cfg register ****/
/*
 * Selection of the input for the "set_1000" input of the Ethernet 10/100/1000
 * MAC:
 * 0 - From RGMII converter (automatic speed selection)
 * 1 - From register set_1000_def
 */
#define ETH_MAC_GEN_MAC_1G_CFG_SET_1000_SEL (1 << 0)
/* Default value for the 10/100/1000 MAC "set_1000" input */
#define ETH_MAC_GEN_MAC_1G_CFG_SET_1000_DEF (1 << 1)
/*
 * Selection of the input for the "set_10" input of the Ethernet 10/100/1000
 * MAC:
 * 0 - From RGMII converter (automatic speed selection)
 * 1 - From register set_10_def
 */
#define ETH_MAC_GEN_MAC_1G_CFG_SET_10_SEL (1 << 4)
/* Default value for the 10/100/1000 MAC "set_10" input */
#define ETH_MAC_GEN_MAC_1G_CFG_SET_10_DEF (1 << 5)
/* Transmit low power enable */
#define ETH_MAC_GEN_MAC_1G_CFG_LOWP_ENA  (1 << 8)
/*
 * Enable magic packet mode:
 * 0 - Sleep mode
 * 1 - Normal operation
 */
#define ETH_MAC_GEN_MAC_1G_CFG_SLEEPN    (1 << 9)
/* Swap ff_tx_crc input */
#define ETH_MAC_GEN_MAC_1G_CFG_SWAP_FF_TX_CRC (1 << 12)

/**** mac_1g_stat register ****/
/* Status of the en_10 output form the 10/100/1000 MAC */
#define ETH_MAC_GEN_MAC_1G_STAT_EN_10    (1 << 0)
/* Status of the eth_mode output from th 10/100/1000 MAC */
#define ETH_MAC_GEN_MAC_1G_STAT_ETH_MODE (1 << 1)
/* Status of the lowp output from the 10/100/1000 MAC */
#define ETH_MAC_GEN_MAC_1G_STAT_LOWP     (1 << 4)
/* Status of the wakeup output from the 10/100/1000 MAC */
#define ETH_MAC_GEN_MAC_1G_STAT_WAKEUP   (1 << 5)

/**** rgmii_cfg register ****/
/*
 * Selection of the input for the "set_1000" input of the RGMII converter
 * 0 - From MAC
 * 1 - From register set_1000_def (automatic speed selection)
 */
#define ETH_MAC_GEN_RGMII_CFG_SET_1000_SEL (1 << 0)
/* Default value for the RGMII converter "set_1000" input */
#define ETH_MAC_GEN_RGMII_CFG_SET_1000_DEF (1 << 1)
/*
 * Selection of the input for the "set_10" input of the RGMII converter:
 * 0 - From MAC
 * 1 - From register set_10_def (automatic speed selection)
 */
#define ETH_MAC_GEN_RGMII_CFG_SET_10_SEL (1 << 4)
/* Default value for the 10/100/1000 MAC "set_10" input  */
#define ETH_MAC_GEN_RGMII_CFG_SET_10_DEF (1 << 5)
/* Enable automatic speed selection (based on PHY in-band status information) */
#define ETH_MAC_GEN_RGMII_CFG_ENA_AUTO   (1 << 8)
/* Force full duplex, only valid when ena_auto is '1'. */
#define ETH_MAC_GEN_RGMII_CFG_SET_FD     (1 << 9)

/**** rgmii_stat register ****/
/*
 * Status of the speed output form the RGMII converter
 * 00 - 10 Mbps
 * 01 - 100 Mbps
 * 10 - 1000 Mbps
 * 11 - Reserved
 */
#define ETH_MAC_GEN_RGMII_STAT_SPEED_MASK 0x00000003
#define ETH_MAC_GEN_RGMII_STAT_SPEED_SHIFT 0
/*
 * Link indication from the RGMII converter (valid only if the external PHY
 * supports in-band status signaling)
 */
#define ETH_MAC_GEN_RGMII_STAT_LINK      (1 << 4)
/*
 * Full duplex indication from the RGMII converter (valid only if the external
 * PHY supports in-band status signaling)
 */
#define ETH_MAC_GEN_RGMII_STAT_DUP       (1 << 5)

/**** mac_10g_cfg register ****/
/* Instruct the XGMII to transmit local fault. */
#define ETH_MAC_GEN_MAC_10G_CFG_TX_LOC_FAULT (1 << 0)
/* Instruct the XGMII to transmit remote fault. */
#define ETH_MAC_GEN_MAC_10G_CFG_TX_REM_FAULT (1 << 1)
/* Instruct the XGMII to transmit link fault. */
#define ETH_MAC_GEN_MAC_10G_CFG_TX_LI_FAULT (1 << 2)
/*
 * Synchronous reset for the PCS layer. Can be used after SerDes lock assertion
 * to reset the PCS state machine.
 */
#define ETH_MAC_GEN_MAC_10G_CFG_SG_SRESET (1 << 3)
/*
 * PHY LOS indication selection
 * 00 - Select register value from phy_los_def
 * 01 - Select input from the SerDes
 * 10 - Select input from GPIO
 * 11 - Select inverted input from GPIO
 */
#define ETH_MAC_GEN_MAC_10G_CFG_PHY_LOS_SEL_MASK 0x00000030
#define ETH_MAC_GEN_MAC_10G_CFG_PHY_LOS_SEL_SHIFT 4
/*
 * Default value for PHY LOS indication. Reflects the LOS indication from the
 * SerDes. ('0' if not used)
 */
#define ETH_MAC_GEN_MAC_10G_CFG_PHY_LOS_DEF (1 << 6)
/* Reverse polarity of the LOS signal from the SerDes */
#define ETH_MAC_GEN_MAC_10G_CFG_PHY_LOS_POL (1 << 7)
/* Transmit low power enable */
#define ETH_MAC_GEN_MAC_10G_CFG_LOWP_ENA (1 << 8)
/* Swap ff_tx_crc input */
#define ETH_MAC_GEN_MAC_10G_CFG_SWAP_FF_TX_CRC (1 << 12)

/**** mac_10g_stat register ****/
/* XGMII RS detects local fault */
#define ETH_MAC_GEN_MAC_10G_STAT_LOC_FAULT (1 << 0)
/* XGMII RS detects remote fault */
#define ETH_MAC_GEN_MAC_10G_STAT_REM_FAULT (1 << 1)
/* XGMII RS detects link fault */
#define ETH_MAC_GEN_MAC_10G_STAT_LI_FAULT (1 << 2)
/* PFC mode */
#define ETH_MAC_GEN_MAC_10G_STAT_PFC_MODE (1 << 3)

#define ETH_MAC_GEN_MAC_10G_STAT_SG_ENA  (1 << 4)

#define ETH_MAC_GEN_MAC_10G_STAT_SG_ANDONE (1 << 5)

#define ETH_MAC_GEN_MAC_10G_STAT_SG_SYNC (1 << 6)

#define ETH_MAC_GEN_MAC_10G_STAT_SG_SPEED_MASK 0x00000180
#define ETH_MAC_GEN_MAC_10G_STAT_SG_SPEED_SHIFT 7
/* Status of the lowp output form the 1/2.5/10G MAC */
#define ETH_MAC_GEN_MAC_10G_STAT_LOWP    (1 << 9)
/* Status of the ts_avail output from th 1/2.5/10G MAC */
#define ETH_MAC_GEN_MAC_10G_STAT_TS_AVAIL (1 << 10)
/* Transmit pause indication */
#define ETH_MAC_GEN_MAC_10G_STAT_PAUSE_ON_MASK 0xFF000000
#define ETH_MAC_GEN_MAC_10G_STAT_PAUSE_ON_SHIFT 24

/**** xaui_cfg register ****/
/* Increase rate matching FIFO threshold */
#define ETH_MAC_GEN_XAUI_CFG_JUMBO_EN    (1 << 0)

/**** xaui_stat register ****/
/* Lane alignment status */
#define ETH_MAC_GEN_XAUI_STAT_ALIGN_DONE (1 << 0)
/* Lane synchronization */
#define ETH_MAC_GEN_XAUI_STAT_SYNC_MASK  0x000000F0
#define ETH_MAC_GEN_XAUI_STAT_SYNC_SHIFT 4
/* Code group alignment indication */
#define ETH_MAC_GEN_XAUI_STAT_CG_ALIGN_MASK 0x00000F00
#define ETH_MAC_GEN_XAUI_STAT_CG_ALIGN_SHIFT 8

/**** rxaui_cfg register ****/
/* Increase rate matching FIFO threshold */
#define ETH_MAC_GEN_RXAUI_CFG_JUMBO_EN   (1 << 0)
/* Scrambler enable */
#define ETH_MAC_GEN_RXAUI_CFG_SRBL_EN    (1 << 1)
/* Disparity calculation across lanes enabled */
#define ETH_MAC_GEN_RXAUI_CFG_DISP_ACROSS_LANE (1 << 2)

/**** rxaui_stat register ****/
/* Lane alignment status */
#define ETH_MAC_GEN_RXAUI_STAT_ALIGN_DONE (1 << 0)
/* Lane synchronization */
#define ETH_MAC_GEN_RXAUI_STAT_SYNC_MASK 0x000000F0
#define ETH_MAC_GEN_RXAUI_STAT_SYNC_SHIFT 4
/* Code group alignment indication */
#define ETH_MAC_GEN_RXAUI_STAT_CG_ALIGN_MASK 0x00000F00
#define ETH_MAC_GEN_RXAUI_STAT_CG_ALIGN_SHIFT 8

/**** sd_cfg register ****/
/*
 * Signal detect selection
 * 0 - from register
 * 1 - from SerDes
 */
#define ETH_MAC_GEN_SD_CFG_SEL_MASK      0x0000000F
#define ETH_MAC_GEN_SD_CFG_SEL_SHIFT     0
/* Signal detect value */
#define ETH_MAC_GEN_SD_CFG_VAL_MASK      0x000000F0
#define ETH_MAC_GEN_SD_CFG_VAL_SHIFT     4
/* Signal detect revers polarity (reverse polarity of signal from the SerDes */
#define ETH_MAC_GEN_SD_CFG_POL_MASK      0x00000F00
#define ETH_MAC_GEN_SD_CFG_POL_SHIFT     8

/**** mdio_ctrl_1 register ****/
/*
 * Available indication
 * 0 - The port was available and it is captured by this Ethernet controller.
 * 1 - The port is used by another Ethernet controller.
 */
#define ETH_MAC_GEN_MDIO_CTRL_1_AVAIL    (1 << 0)

/**** mdio_1 register ****/
/* Current Ethernet interface number that controls the MDIO port */
#define ETH_MAC_GEN_MDIO_1_INFO_MASK     0x000000FF
#define ETH_MAC_GEN_MDIO_1_INFO_SHIFT    0

/**** mdio_ctrl_2 register ****/
/*
 * Available indication
 * 0 - The port was available and it is captured by this Ethernet controller.
 * 1 - The port is used by another Ethernet controller.
 */
#define ETH_MAC_GEN_MDIO_CTRL_2_AVAIL    (1 << 0)

/**** mdio_2 register ****/
/* Current Ethernet interface number that controls the MDIO port */
#define ETH_MAC_GEN_MDIO_2_INFO_MASK     0x000000FF
#define ETH_MAC_GEN_MDIO_2_INFO_SHIFT    0

/**** xgmii_dfifo_32_64 register ****/
/* FIFO enable */
#define ETH_MAC_GEN_XGMII_DFIFO_32_64_ENABLE (1 << 0)
/* Read Write command every 2 cycles */
#define ETH_MAC_GEN_XGMII_DFIFO_32_64_RW_2_CYCLES (1 << 1)
/* Swap LSB MSB when creating wider bus */
#define ETH_MAC_GEN_XGMII_DFIFO_32_64_SWAP_LSB_MSB (1 << 2)
/* Software reset */
#define ETH_MAC_GEN_XGMII_DFIFO_32_64_SW_RESET (1 << 4)
/* Read threshold */
#define ETH_MAC_GEN_XGMII_DFIFO_32_64_READ_TH_MASK 0x0000FF00
#define ETH_MAC_GEN_XGMII_DFIFO_32_64_READ_TH_SHIFT 8
/* FIFO used */
#define ETH_MAC_GEN_XGMII_DFIFO_32_64_USED_MASK 0x00FF0000
#define ETH_MAC_GEN_XGMII_DFIFO_32_64_USED_SHIFT 16

/**** xgmii_dfifo_64_32 register ****/
/* FIFO enable */
#define ETH_MAC_GEN_XGMII_DFIFO_64_32_ENABLE (1 << 0)
/* Read Write command every 2 cycles */
#define ETH_MAC_GEN_XGMII_DFIFO_64_32_RW_2_CYCLES (1 << 1)
/* Swap LSB MSB when creating wider bus */
#define ETH_MAC_GEN_XGMII_DFIFO_64_32_SWAP_LSB_MSB (1 << 2)
/* Software reset */
#define ETH_MAC_GEN_XGMII_DFIFO_64_32_SW_RESET (1 << 4)
/* Read threshold */
#define ETH_MAC_GEN_XGMII_DFIFO_64_32_READ_TH_MASK 0x0000FF00
#define ETH_MAC_GEN_XGMII_DFIFO_64_32_READ_TH_SHIFT 8
/* FIFO used */
#define ETH_MAC_GEN_XGMII_DFIFO_64_32_USED_MASK 0x00FF0000
#define ETH_MAC_GEN_XGMII_DFIFO_64_32_USED_SHIFT 16

/**** sd_fifo_ctrl register ****/
/* FIFO enable */
#define ETH_MAC_GEN_SD_FIFO_CTRL_ENABLE_MASK 0x0000000F
#define ETH_MAC_GEN_SD_FIFO_CTRL_ENABLE_SHIFT 0
/* Software reset */
#define ETH_MAC_GEN_SD_FIFO_CTRL_SW_RESET_MASK 0x000000F0
#define ETH_MAC_GEN_SD_FIFO_CTRL_SW_RESET_SHIFT 4
/* Read threshold */
#define ETH_MAC_GEN_SD_FIFO_CTRL_READ_TH_MASK 0x0000FF00
#define ETH_MAC_GEN_SD_FIFO_CTRL_READ_TH_SHIFT 8

/**** sd_fifo_stat register ****/
/* FIFO 0 used */
#define ETH_MAC_GEN_SD_FIFO_STAT_USED_0_MASK 0x000000FF
#define ETH_MAC_GEN_SD_FIFO_STAT_USED_0_SHIFT 0
/* FIFO 1 used */
#define ETH_MAC_GEN_SD_FIFO_STAT_USED_1_MASK 0x0000FF00
#define ETH_MAC_GEN_SD_FIFO_STAT_USED_1_SHIFT 8
/* FIFO 2 used */
#define ETH_MAC_GEN_SD_FIFO_STAT_USED_2_MASK 0x00FF0000
#define ETH_MAC_GEN_SD_FIFO_STAT_USED_2_SHIFT 16
/* FIFO 3 used */
#define ETH_MAC_GEN_SD_FIFO_STAT_USED_3_MASK 0xFF000000
#define ETH_MAC_GEN_SD_FIFO_STAT_USED_3_SHIFT 24

/**** mux_sel register ****/
/*
 * SGMII input selection selector
 * 00 – SerDes 0
 * 01 – SerDes 1
 * 10 – SerDes 2
 * 11 – SerDes 3
 */
#define ETH_MAC_GEN_MUX_SEL_SGMII_IN_MASK 0x00000003
#define ETH_MAC_GEN_MUX_SEL_SGMII_IN_SHIFT 0
/*
 * RXAUI Lane 0 Input
 * 00 – SerDes 0
 * 01 – SerDes 1
 * 10 – SerDes 2
 * 11 – SerDes 3
 */
#define ETH_MAC_GEN_MUX_SEL_RXAUI_0_IN_MASK 0x0000000C
#define ETH_MAC_GEN_MUX_SEL_RXAUI_0_IN_SHIFT 2
/*
 * RXAUI Lane 1 Input
 * 00 – SERDES 0
 * 01 – SERDES 1
 * 10 – SERDES 2
 * 11 – SERDES 3
 */
#define ETH_MAC_GEN_MUX_SEL_RXAUI_1_IN_MASK 0x00000030
#define ETH_MAC_GEN_MUX_SEL_RXAUI_1_IN_SHIFT 4
/*
 * XAUI Lane 0 Input
 * 00 – SERDES 0
 * 01 – SERDES 1
 * 10 – SERDES 2
 * 11 – SERDES 3
 */
#define ETH_MAC_GEN_MUX_SEL_XAUI_0_IN_MASK 0x000000C0
#define ETH_MAC_GEN_MUX_SEL_XAUI_0_IN_SHIFT 6
/*
 * XAUI Lane 1 Input
 * 00 – SERDES 0
 * 01 – SERDES 1
 * 10 – SERDES 2
 * 11 – SERDES 3
 */
#define ETH_MAC_GEN_MUX_SEL_XAUI_1_IN_MASK 0x00000300
#define ETH_MAC_GEN_MUX_SEL_XAUI_1_IN_SHIFT 8
/*
 * XAUI Lane 2 Input
 * 00 – SERDES 0
 * 01 – SERDES 1
 * 10 – SERDES 2
 * 11 – SERDES 3
 */
#define ETH_MAC_GEN_MUX_SEL_XAUI_2_IN_MASK 0x00000C00
#define ETH_MAC_GEN_MUX_SEL_XAUI_2_IN_SHIFT 10
/*
 * XAUI Lane 3 Input
 * 00 – SERDES 0
 * 01 – SERDES 1
 * 10 – SERDES 2
 * 11 – SERDES 3
 */
#define ETH_MAC_GEN_MUX_SEL_XAUI_3_IN_MASK 0x00003000
#define ETH_MAC_GEN_MUX_SEL_XAUI_3_IN_SHIFT 12
/*
 * KR PCS Input
 * 00 - SERDES 0
 * 01 - SERDES 1
 * 10 - SERDES 2
 * 11 - SERDES 3
 */
#define ETH_MAC_GEN_MUX_SEL_KR_IN_MASK   0x0000C000
#define ETH_MAC_GEN_MUX_SEL_KR_IN_SHIFT  14
/*
 * SerDes 0 input selection (TX)
 * 000 – XAUI lane 0
 * 001 – XAUI lane 1
 * 010 – XAUI lane 2
 * 011 – XAUI lane 3
 * 100 – RXAUI lane 0
 * 101 – RXAUI lane 1
 * 110 – SGMII
 * 111 – KR
 */
#define ETH_MAC_GEN_MUX_SEL_SERDES_0_TX_MASK 0x00070000
#define ETH_MAC_GEN_MUX_SEL_SERDES_0_TX_SHIFT 16
/*
 * SERDES 1 input selection (Tx)
 * 000 – XAUI lane 0
 * 001 – XAUI lane 1
 * 010 – XAUI lane 2
 * 011 – XAUI lane 3
 * 100 – RXAUI lane 0
 * 101 – RXAUI lane 1
 * 110 – SGMII
 * 111 – KR
 */
#define ETH_MAC_GEN_MUX_SEL_SERDES_1_TX_MASK 0x00380000
#define ETH_MAC_GEN_MUX_SEL_SERDES_1_TX_SHIFT 19
/*
 * SerDes 2 input selection (Tx)
 * 000 – XAUI lane 0
 * 001 – XAUI lane 1
 * 010 – XAUI lane 2
 * 011 – XAUI lane 3
 * 100 – RXAUI lane 0
 * 101 – RXAUI lane 1
 * 110 – SGMII
 * 111 – KR
 */
#define ETH_MAC_GEN_MUX_SEL_SERDES_2_TX_MASK 0x01C00000
#define ETH_MAC_GEN_MUX_SEL_SERDES_2_TX_SHIFT 22
/*
 * SerDes 3 input selection (Tx)
 * 000 – XAUI lane 0
 * 001 – XAUI lane 1
 * 010 – XAUI lane 2
 * 011 – XAUI lane 3
 * 100 – RXAUI lane 0
 * 101 – RXAUI lane 1
 * 110 – SGMII
 * 111 – KR
 */
#define ETH_MAC_GEN_MUX_SEL_SERDES_3_TX_MASK 0x0E000000
#define ETH_MAC_GEN_MUX_SEL_SERDES_3_TX_SHIFT 25

/**** clk_cfg register ****/
/*
 * Rx/Tx lane 0 clock MUX select
 * must be aligned with data selector MUXs)
 * 0 – SerDes 0 clock
 * 0 – SerDes 1 clock
 * 2 – SerDes 2 clock
 * 3 – SerDes 3 clock
 */
#define ETH_MAC_GEN_CLK_CFG_LANE_0_CLK_SEL_MASK 0x00000003
#define ETH_MAC_GEN_CLK_CFG_LANE_0_CLK_SEL_SHIFT 0
/*
 * Rx/Tx lane 0 clock MUX select must be aligned with data selector MUXs)
 * 0 - SerDes 0 clock
 * 1 - SerDes 1 clock
 * 2 - SerDes 2 clock
 * 3 - SerDes 3 clock
 */
#define ETH_MAC_GEN_CLK_CFG_LANE_1_CLK_SEL_MASK 0x00000030
#define ETH_MAC_GEN_CLK_CFG_LANE_1_CLK_SEL_SHIFT 4
/*
 * RX/TX lane 0 clock MUX select (should be aligned with data selector MUXs)
 * 0 - SERDES 0 clock
 * 1 - SERDES 1 clock
 * 2 - SERDES 2 clock
 * 3 - SERDES 3 clock
 */
#define ETH_MAC_GEN_CLK_CFG_LANE_2_CLK_SEL_MASK 0x00000300
#define ETH_MAC_GEN_CLK_CFG_LANE_2_CLK_SEL_SHIFT 8
/*
 * Rx/Tx lane 0 clock MUX select must be aligned with data selector MUXs)
 * 0 - SerDes 0 clock
 * 1 - SerDes 1 clock
 * 2 - SerDes 2 clock
 * 3 - SerDes 3 clock
 */
#define ETH_MAC_GEN_CLK_CFG_LANE_3_CLK_SEL_MASK 0x00003000
#define ETH_MAC_GEN_CLK_CFG_LANE_3_CLK_SEL_SHIFT 12
/*
 * MAC GMII Rx clock MUX select must be aligned with data selector MUXs)
 * 0 – RGMII
 * 1 – SGMII
 */
#define ETH_MAC_GEN_CLK_CFG_GMII_RX_CLK_SEL (1 << 16)
/*
 * MAC GMII Tx clock MUX select (should be aligned with data selector MUXs)
 * 0 - RGMII
 * 1 - SGMII
 */
#define ETH_MAC_GEN_CLK_CFG_GMII_TX_CLK_SEL (1 << 18)
/*
 * Tx clock MUX select,
 * Selects the internal clock for the Tx data path
 * 0 – SerDes[0] TX DWORD CLK REF (for RXAUI and SGMII)
 * 1 – SerDes[0] TX WORD CLK REF (for XAUI and KR)
 */
#define ETH_MAC_GEN_CLK_CFG_TX_CLK_SEL   (1 << 28)
/*
 * Rxclock MUX select
 * Selects the internal clock for the Rx data path
 * 0 – XGMII TX CLK 32 LOCAL (for XAUI and RXAUI and KR)
 * 1 – SerDes[0] RX DWORD CLK GENERATED (125M)
 * (for SGMII)
 */
#define ETH_MAC_GEN_CLK_CFG_RX_CLK_SEL   (1 << 30)

/**** los_sel register ****/
/*
 * Selected LOS/SD select
 * 00 – SerDes 0
 * 01 – SerDes 1
 * 10 – SerDes 2
 * 11 – SerDes 3
 */
#define ETH_MAC_GEN_LOS_SEL_LANE_0_SEL_MASK 0x00000003
#define ETH_MAC_GEN_LOS_SEL_LANE_0_SEL_SHIFT 0
/*
 * Selected LOS/SD select
 * 00 - SerDes 0
 * 01 - SerDes 1
 * 10 - SerDes 2
 * 11 - SerDes 3
 */
#define ETH_MAC_GEN_LOS_SEL_LANE_1_SEL_MASK 0x00000030
#define ETH_MAC_GEN_LOS_SEL_LANE_1_SEL_SHIFT 4
/*
 * Selected LOS/SD select
 * 00 - SerDes 0
 * 01 - SerDes 1
 * 10 - SerDes 2
 * 11 - SerDes 3
 */
#define ETH_MAC_GEN_LOS_SEL_LANE_2_SEL_MASK 0x00000300
#define ETH_MAC_GEN_LOS_SEL_LANE_2_SEL_SHIFT 8
/*
 * Selected LOS/SD select
 * 00 - SerDes 0
 * 01 - SerDes 1
 * 10 - SerDes 2
 * 11 - SerDes 3
 */
#define ETH_MAC_GEN_LOS_SEL_LANE_3_SEL_MASK 0x00003000
#define ETH_MAC_GEN_LOS_SEL_LANE_3_SEL_SHIFT 12

/**** rgmii_sel register ****/
/* Swap [3:0] input with [7:4] */
#define ETH_MAC_GEN_RGMII_SEL_RX_SWAP_3_0 (1 << 0)
/* Swap [4] input with [9] */
#define ETH_MAC_GEN_RGMII_SEL_RX_SWAP_4  (1 << 1)
/* Swap [7:4] input with [3:0] */
#define ETH_MAC_GEN_RGMII_SEL_RX_SWAP_7_3 (1 << 2)
/* Swap [9] input with [4] */
#define ETH_MAC_GEN_RGMII_SEL_RX_SWAP_9  (1 << 3)
/* Swap [3:0] input with [7:4] */
#define ETH_MAC_GEN_RGMII_SEL_TX_SWAP_3_0 (1 << 4)
/* Swap [4] input with [9] */
#define ETH_MAC_GEN_RGMII_SEL_TX_SWAP_4  (1 << 5)
/* Swap [7:4] input with [3:0] */
#define ETH_MAC_GEN_RGMII_SEL_TX_SWAP_7_3 (1 << 6)
/* Swap [9] input with [4] */
#define ETH_MAC_GEN_RGMII_SEL_TX_SWAP_9  (1 << 7)

/**** led_cfg register ****/
/*
 * LED source selection:
 * 0 – Default reg
 * 1 – Rx activity
 * 2 – Tx activity
 * 3 – Rx | Tx activity
 * 4-9 – SGMII LEDs
 */
#define ETH_MAC_GEN_LED_CFG_SEL_MASK     0x0000000F
#define ETH_MAC_GEN_LED_CFG_SEL_SHIFT    0

/* turn the led on/off based on default value field (ETH_MAC_GEN_LED_CFG_DEF) */
#define ETH_MAC_GEN_LED_CFG_SEL_DEFAULT_REG	0
#define ETH_MAC_GEN_LED_CFG_SEL_RX_ACTIVITY_DEPRECIATED	1
#define ETH_MAC_GEN_LED_CFG_SEL_TX_ACTIVITY_DEPRECIATED	2
#define ETH_MAC_GEN_LED_CFG_SEL_RX_TX_ACTIVITY_DEPRECIATED 3
#define ETH_MAC_GEN_LED_CFG_SEL_LINK_ACTIVITY 10

/* LED default value */
#define ETH_MAC_GEN_LED_CFG_DEF          (1 << 4)
/* LED signal polarity */
#define ETH_MAC_GEN_LED_CFG_POL          (1 << 5)
/*
 * activity timer (MSB)
 * 32 bit timer @SB clock
 */
#define ETH_MAC_GEN_LED_CFG_ACT_TIMER_MASK 0x00FF0000
#define ETH_MAC_GEN_LED_CFG_ACT_TIMER_SHIFT 16
/*
 * activity timer (MSB)
 * 32 bit timer @SB clock
 */
#define ETH_MAC_GEN_LED_CFG_BLINK_TIMER_MASK 0xFF000000
#define ETH_MAC_GEN_LED_CFG_BLINK_TIMER_SHIFT 24

/**** pcs_addr register ****/
/* Address value */
#define ETH_MAC_KR_PCS_ADDR_VAL_MASK     0x0000FFFF
#define ETH_MAC_KR_PCS_ADDR_VAL_SHIFT    0

/**** pcs_data register ****/
/* Data value */
#define ETH_MAC_KR_PCS_DATA_VAL_MASK     0x0000FFFF
#define ETH_MAC_KR_PCS_DATA_VAL_SHIFT    0

/**** an_addr register ****/
/* Address value */
#define ETH_MAC_KR_AN_ADDR_VAL_MASK      0x0000FFFF
#define ETH_MAC_KR_AN_ADDR_VAL_SHIFT     0

/**** an_data register ****/
/* Data value */
#define ETH_MAC_KR_AN_DATA_VAL_MASK      0x0000FFFF
#define ETH_MAC_KR_AN_DATA_VAL_SHIFT     0

/**** pma_addr register ****/
/* Dddress value */
#define ETH_MAC_KR_PMA_ADDR_VAL_MASK     0x0000FFFF
#define ETH_MAC_KR_PMA_ADDR_VAL_SHIFT    0

/**** pma_data register ****/
/* Data value */
#define ETH_MAC_KR_PMA_DATA_VAL_MASK     0x0000FFFF
#define ETH_MAC_KR_PMA_DATA_VAL_SHIFT    0

/**** mtip_addr register ****/
/* Address value */
#define ETH_MAC_KR_MTIP_ADDR_VAL_MASK    0x0000FFFF
#define ETH_MAC_KR_MTIP_ADDR_VAL_SHIFT   0

/**** mtip_data register ****/
/* Data value */
#define ETH_MAC_KR_MTIP_DATA_VAL_MASK    0x0000FFFF
#define ETH_MAC_KR_MTIP_DATA_VAL_SHIFT   0

/**** pcs_cfg register ****/
/* Enable Auto-Negotiation after Reset */
#define ETH_MAC_KR_PCS_CFG_STRAP_AN_ENA  (1 << 0)
/*
 * Signal detect selector for the EEE
 * 0 – Register default value
 * 1 – SerDes value
 */
#define ETH_MAC_KR_PCS_CFG_EEE_SD_SEL    (1 << 4)
/* Signal detect default value for the EEE */
#define ETH_MAC_KR_PCS_CFG_EEE_DEF_VAL   (1 << 5)
/* Signal detect polarity reversal for the EEE */
#define ETH_MAC_KR_PCS_CFG_EEE_SD_POL    (1 << 6)
/* EEE timer value  */
#define ETH_MAC_KR_PCS_CFG_EEE_TIMER_VAL_MASK 0x0000FF00
#define ETH_MAC_KR_PCS_CFG_EEE_TIMER_VAL_SHIFT 8
/*
 * Selects source for the enable SerDes DME signal
 * 0 – Register value
 * 1 – PCS output
 */
#define ETH_MAC_KR_PCS_CFG_DME_SEL       (1 << 16)
/* DME default value */
#define ETH_MAC_KR_PCS_CFG_DME_VAL       (1 << 17)
/* DME default polarity reversal when selecting PCS output */
#define ETH_MAC_KR_PCS_CFG_DME_POL       (1 << 18)

/**** pcs_stat register ****/
/* Link enable by the Auto-Negotiation */
#define ETH_MAC_KR_PCS_STAT_AN_LINK_CONTROL_MASK 0x0000003F
#define ETH_MAC_KR_PCS_STAT_AN_LINK_CONTROL_SHIFT 0
/* Block lock */
#define ETH_MAC_KR_PCS_STAT_BLOCK_LOCK   (1 << 8)
/* hi BER */
#define ETH_MAC_KR_PCS_STAT_HI_BER       (1 << 9)

#define ETH_MAC_KR_PCS_STAT_RX_WAKE_ERR  (1 << 16)

#define ETH_MAC_KR_PCS_STAT_PMA_TXMODE_ALERT (1 << 17)

#define ETH_MAC_KR_PCS_STAT_PMA_TXMODE_QUIET (1 << 18)

#define ETH_MAC_KR_PCS_STAT_PMA_RXMODE_QUIET (1 << 19)

#define ETH_MAC_KR_PCS_STAT_RX_LPI_ACTIVE (1 << 20)

#define ETH_MAC_KR_PCS_STAT_TX_LPI_ACTIVE (1 << 21)

/**** reg_addr register ****/
/* Address value */
#define ETH_MAC_SGMII_REG_ADDR_VAL_MASK  0x0000001F
#define ETH_MAC_SGMII_REG_ADDR_VAL_SHIFT 0

#define ETH_MAC_SGMII_REG_ADDR_CTRL_REG	0x0
#define ETH_MAC_SGMII_REG_ADDR_IF_MODE_REG 0x14

/**** reg_data register ****/
/* Data value */
#define ETH_MAC_SGMII_REG_DATA_VAL_MASK  0x0000FFFF
#define ETH_MAC_SGMII_REG_DATA_VAL_SHIFT 0

#define ETH_MAC_SGMII_REG_DATA_CTRL_AN_ENABLE			(1 << 12)
#define ETH_MAC_SGMII_REG_DATA_IF_MODE_SGMII_EN			(1 << 0)
#define ETH_MAC_SGMII_REG_DATA_IF_MODE_SGMII_AN			(1 << 1)
#define ETH_MAC_SGMII_REG_DATA_IF_MODE_SGMII_SPEED_MASK		0xC
#define ETH_MAC_SGMII_REG_DATA_IF_MODE_SGMII_SPEED_SHIFT	2
#define ETH_MAC_SGMII_REG_DATA_IF_MODE_SGMII_SPEED_10		0x0
#define ETH_MAC_SGMII_REG_DATA_IF_MODE_SGMII_SPEED_100		0x1
#define ETH_MAC_SGMII_REG_DATA_IF_MODE_SGMII_SPEED_1000		0x2
#define ETH_MAC_SGMII_REG_DATA_IF_MODE_SGMII_DUPLEX		(1 << 4)

/**** clk_div register ****/
/* Value for 1000M selection */
#define ETH_MAC_SGMII_CLK_DIV_VAL_1000_MASK 0x000000FF
#define ETH_MAC_SGMII_CLK_DIV_VAL_1000_SHIFT 0
/* Value for 100M selection */
#define ETH_MAC_SGMII_CLK_DIV_VAL_100_MASK 0x0000FF00
#define ETH_MAC_SGMII_CLK_DIV_VAL_100_SHIFT 8
/* Value for 10M selection */
#define ETH_MAC_SGMII_CLK_DIV_VAL_10_MASK 0x00FF0000
#define ETH_MAC_SGMII_CLK_DIV_VAL_10_SHIFT 16
/* Bypass PCS selection */
#define ETH_MAC_SGMII_CLK_DIV_BYPASS     (1 << 24)
/*
 * Divider selection when bypass field is '1', one hot
 * 001 – 1000M
 * 010 – 100M
 * 100 – 10M
 */
#define ETH_MAC_SGMII_CLK_DIV_SEL_MASK   0x0E000000
#define ETH_MAC_SGMII_CLK_DIV_SEL_SHIFT  25

/**** link_stat register ****/

#define ETH_MAC_SGMII_LINK_STAT_SET_1000 (1 << 0)

#define ETH_MAC_SGMII_LINK_STAT_SET_100  (1 << 1)

#define ETH_MAC_SGMII_LINK_STAT_SET_10   (1 << 2)

#define ETH_MAC_SGMII_LINK_STAT_LED_AN   (1 << 3)

#define ETH_MAC_SGMII_LINK_STAT_HD_ENA   (1 << 4)

#define ETH_MAC_SGMII_LINK_STAT_LED_LINK (1 << 5)

/**** afifo_ctrl register ****/
/* enable tx input operation */
#define ETH_MAC_GEN_V3_AFIFO_CTRL_EN_TX_IN (1 << 0)
/* enable tx output operation */
#define ETH_MAC_GEN_V3_AFIFO_CTRL_EN_TX_OUT (1 << 1)
/* enable rx input operation */
#define ETH_MAC_GEN_V3_AFIFO_CTRL_EN_RX_IN (1 << 4)
/* enable rx output operation */
#define ETH_MAC_GEN_V3_AFIFO_CTRL_EN_RX_OUT (1 << 5)
/* enable tx FIFO input operation */
#define ETH_MAC_GEN_V3_AFIFO_CTRL_EN_TX_FIFO_IN (1 << 8)
/* enable tx FIFO output operation */
#define ETH_MAC_GEN_V3_AFIFO_CTRL_EN_TX_FIFO_OUT (1 << 9)
/* enable rx FIFO input operation */
#define ETH_MAC_GEN_V3_AFIFO_CTRL_EN_RX_FIFO_IN (1 << 12)
/* enable rx FIFO output operation */
#define ETH_MAC_GEN_V3_AFIFO_CTRL_EN_RX_FIFO_OUT (1 << 13)

/**** tx_afifo_cfg_1 register ****/
/* minimum packet size configuration */
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_1_MIN_PKT_SIZE_MASK 0x0000FFFF
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_1_MIN_PKT_SIZE_SHIFT 0

/**** tx_afifo_cfg_2 register ****/
/* maximum packet size configuration */
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_2_MAX_PKT_SIZE_MASK 0x0000FFFF
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_2_MAX_PKT_SIZE_SHIFT 0

/**** tx_afifo_cfg_3 register ****/
/* input bus width */
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_3_INPUT_BUS_W_MASK 0x0000FFFF
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_3_INPUT_BUS_W_SHIFT 0
/* input bus width divide factor */
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_3_INPUT_BUS_W_F_MASK 0xFFFF0000
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_3_INPUT_BUS_W_F_SHIFT 16

/**** tx_afifo_cfg_4 register ****/
/* output bus width */
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_4_OUTPUT_BUS_W_MASK 0x0000FFFF
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_4_OUTPUT_BUS_W_SHIFT 0
/* output bus width divide factor */
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_4_OUTPUT_BUS_W_F_MASK 0xFFFF0000
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_4_OUTPUT_BUS_W_F_SHIFT 16

/**** tx_afifo_cfg_5 register ****/
/*
 * determines if the input bus is valid/read or “write enable”.
 * 0 – write enable
 * 1 – valid/ready
 */
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_5_INPUT_BUS_VALID_RDY (1 << 0)
/*
 * determines if the output bus is valid/read or “write enable”.
 * 0 – write enable
 * 1 – valid/ready
 */
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_5_OUTPUT_BUS_VALID_RDY (1 << 1)
/* Swap input bus bytes */
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_5_INPUT_BUS_SWAP_BYTES (1 << 4)
/* Swap output bus bytes */
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_5_OUTPUT_BUS_SWAP_BYTES (1 << 5)
/*
 * output clock select
 * 0 – mac_ll_tx_clk
 * 1 – clk_mac_sys_clk
 */
#define ETH_MAC_GEN_V3_TX_AFIFO_CFG_5_OUTPUT_CLK_SEL (1 << 8)

/**** rx_afifo_cfg_1 register ****/
/* minimum packet size configuration */
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_1_MIN_PKT_SIZE_MASK 0x0000FFFF
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_1_MIN_PKT_SIZE_SHIFT 0

/**** rx_afifo_cfg_2 register ****/
/* maximum packet size configuration */
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_2_MAX_PKT_SIZE_MASK 0x0000FFFF
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_2_MAX_PKT_SIZE_SHIFT 0

/**** rx_afifo_cfg_3 register ****/
/* input bus width */
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_3_INPUT_BUS_W_MASK 0x0000FFFF
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_3_INPUT_BUS_W_SHIFT 0
/* input bus width divide factor */
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_3_INPUT_BUS_W_F_MASK 0xFFFF0000
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_3_INPUT_BUS_W_F_SHIFT 16

/**** rx_afifo_cfg_4 register ****/
/* output bus width */
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_4_OUTPUT_BUS_W_MASK 0x0000FFFF
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_4_OUTPUT_BUS_W_SHIFT 0
/* output bus width divide factor */
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_4_OUTPUT_BUS_W_F_MASK 0xFFFF0000
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_4_OUTPUT_BUS_W_F_SHIFT 16

/**** rx_afifo_cfg_5 register ****/
/*
 * determines if the input bus is valid/read or “write enable”.
 * 0 – write enable
 * 1 – valid/ready
 */
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_5_INPUT_BUS_VALID_RDY (1 << 0)
/*
 * determines if the output bus is valid/read or “write enable”.
 * 0 – write enable
 * 1 – valid/ready
 */
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_5_OUTPUT_BUS_VALID_RDY (1 << 1)
/* Swap input bus bytes */
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_5_INPUT_BUS_SWAP_BYTES (1 << 4)
/* Swap output bus bytes */
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_5_OUTPUT_BUS_SWAP_BYTES (1 << 5)
/*
 * input clock select
 * 0 – mac_ll_rx_clk
 * 1 – clk_serdes_int_0_tx_dword_ref
 * 2 – clk_mac_sys_clk
 * 3 – mac_ll_tx_clk
 */
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_5_INPUT_CLK_SEL_MASK 0x00000300
#define ETH_MAC_GEN_V3_RX_AFIFO_CFG_5_INPUT_CLK_SEL_SHIFT 8

/**** mac_sel register ****/
/*
 * Select the MAC that is connected to the SGMII PCS.
 * 0 – 1G MAC
 * 1 – 10G MAC
 */
#define ETH_MAC_GEN_V3_MAC_SEL_MAC_10G_SGMII (1 << 0)
/*
 * Select between the 10G and 40G MAC
 * 0 – 10G MAC
 * 1 – 40G MAC
 */
#define ETH_MAC_GEN_V3_MAC_SEL_MAC_10G_40G (1 << 4)

/**** mac_10g_ll_cfg register ****/
/*
 * select between 10G (KR PCS) and 1G (SGMII) mode.
 * 0 – 10G
 * 1 – 1G
 */
#define ETH_MAC_GEN_V3_MAC_10G_LL_CFG_MODE_1G (1 << 0)
/* enable Magic packet detection in the MAC (all other packets are dropped) */
#define ETH_MAC_GEN_V3_MAC_10G_LL_CFG_MAGIC_ENA (1 << 5)

/**** mac_10g_ll_ctrl register ****/
/* Force the MAC to stop TX transmission after low power mode. */
#define ETH_MAC_GEN_V3_MAC_10G_LL_CTRL_LPI_TXHOLD (1 << 0)

/**** pcs_10g_ll_cfg register ****/
/* RX FEC Enable */
#define ETH_MAC_GEN_V3_PCS_10G_LL_CFG_FEC_EN_RX (1 << 0)
/* TX FEC enable */
#define ETH_MAC_GEN_V3_PCS_10G_LL_CFG_FEC_EN_TX (1 << 1)
/*
 * RX FEC error propagation enable,
 * (debug, always 0)
 */
#define ETH_MAC_GEN_V3_PCS_10G_LL_CFG_FEC_ERR_ENA (1 << 2)
/*
 * Gearbox configuration:
 * 00 -16
 * 01 – 20
 * 10 – 32
 * 11 – reserved
 */
#define ETH_MAC_GEN_V3_PCS_10G_LL_CFG_TX_GB_CFG_MASK 0x00000030
#define ETH_MAC_GEN_V3_PCS_10G_LL_CFG_TX_GB_CFG_SHIFT 4
/*
 * Gearbox configuration:
 * 00 -16
 * 01 – 20
 * 10 – 32
 * 11 – reserved
 */
#define ETH_MAC_GEN_V3_PCS_10G_LL_CFG_RX_GB_CFG_MASK 0x000000C0
#define ETH_MAC_GEN_V3_PCS_10G_LL_CFG_RX_GB_CFG_SHIFT 6

/**** pcs_10g_ll_status register ****/
/* FEC locked indication */
#define ETH_MAC_GEN_V3_PCS_10G_LL_STATUS_FEC_LOCKED (1 << 0)

/**** pcs_40g_ll_cfg register ****/
/* RX FEC Enable */
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_FEC_EN_RX_MASK 0x0000000F
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_FEC_EN_RX_SHIFT 0
/* TX FEC enable */
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_FEC_EN_TX_MASK 0x000000F0
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_FEC_EN_TX_SHIFT 4
/*
 * RX FEC error propagation enable,
 * (debug, always 0)
 */
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_FEC_ERR_EN_MASK 0x00000F00
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_FEC_ERR_EN_SHIFT 8
/*
 * SERDES width, 16 bit enable
 * 1 – 16
 * 2 – 32
 */
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_SD_16B (1 << 12)
/* FEC 91 enable */
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_FEC91_ENA (1 << 13)
/*
 * PHY LOS indication selection
 * 00 - Select register value from phy_los_def
 * 01 - Select input from the SerDes
 * 10 - Select input from GPIO
 * 11 - Select inverted input from GPIO
 */
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_PHY_LOS_SEL_MASK 0x00030000
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_PHY_LOS_SEL_SHIFT 16
/* PHY LOS default value */
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_PHY_LOS_DEF (1 << 18)
/* PHY LOS polarity */
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_PHY_LOS_POL (1 << 19)
/*
 * Energy detect  indication selection
 * 00 - Select register value from phy_los_def
 * 01 - Select input from the SerDes
 * 10 - Select input from GPIO
 * 11 - Select inverted input from GPIO
 */
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_ENERGY_DETECT_SEL_MASK 0x00300000
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_ENERGY_DETECT_SEL_SHIFT 20
/* Energy detect default value */
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_ENERGY_DETECT_DEF (1 << 22)
/* Energy detect polarity */
#define ETH_MAC_GEN_V3_PCS_40G_LL_CFG_ENERGY_DETECT_POL (1 << 23)

/**** pcs_40g_ll_status register ****/
/* Block lock */
#define ETH_MAC_GEN_V3_PCS_40G_LL_STATUS_BLOCK_LOCK_MASK 0x0000000F
#define ETH_MAC_GEN_V3_PCS_40G_LL_STATUS_BLOCK_LOCK_SHIFT 0
/* align done */
#define ETH_MAC_GEN_V3_PCS_40G_LL_STATUS_ALIGN_DONE (1 << 4)
/* high BER */
#define ETH_MAC_GEN_V3_PCS_40G_LL_STATUS_HIGH_BER (1 << 8)
/* FEC locked indication */
#define ETH_MAC_GEN_V3_PCS_40G_LL_STATUS_FEC_LOCKED_MASK 0x0000F000
#define ETH_MAC_GEN_V3_PCS_40G_LL_STATUS_FEC_LOCKED_SHIFT 12

/**** pcs_40g_ll_addr register ****/
/* Address value */
#define ETH_MAC_GEN_V3_PCS_40G_LL_ADDR_VAL_MASK 0x0001FFFF
#define ETH_MAC_GEN_V3_PCS_40G_LL_ADDR_VAL_SHIFT 0

/**** pcs_40g_ll_data register ****/
/* Data value */
#define ETH_MAC_GEN_V3_PCS_40G_LL_DATA_VAL_MASK 0x0000FFFF
#define ETH_MAC_GEN_V3_PCS_40G_LL_DATA_VAL_SHIFT 0

/**** mac_40g_ll_cfg register ****/
/* change TX CRC polarity */
#define ETH_MAC_GEN_V3_MAC_40G_LL_CFG_SWAP_FF_TX_CRC (1 << 0)
/* force TX remote fault */
#define ETH_MAC_GEN_V3_MAC_40G_LL_CFG_TX_REM_FAULT (1 << 4)
/* force TX local fault */
#define ETH_MAC_GEN_V3_MAC_40G_LL_CFG_TX_LOC_FAULT (1 << 5)
/* force TX Link fault */
#define ETH_MAC_GEN_V3_MAC_40G_LL_CFG_TX_LI_FAULT (1 << 6)
/*
 * PHY LOS indication selection
 * 00 - Select register value from phy_los_def
 * 01 - Select input from the SerDes
 * 10 - Select input from GPIO
 * 11 - Select inverted input from GPIO
 */
#define ETH_MAC_GEN_V3_MAC_40G_LL_CFG_PHY_LOS_SEL_MASK 0x00000300
#define ETH_MAC_GEN_V3_MAC_40G_LL_CFG_PHY_LOS_SEL_SHIFT 8
/* PHY LOS default value */
#define ETH_MAC_GEN_V3_MAC_40G_LL_CFG_PHY_LOS_DEF (1 << 10)
/* PHY LOS polarity */
#define ETH_MAC_GEN_V3_MAC_40G_LL_CFG_PHY_LOS_POL (1 << 11)

/**** mac_40g_ll_status register ****/
/* pause on indication */
#define ETH_MAC_GEN_V3_MAC_40G_LL_STATUS_PAUSE_ON_MASK 0x000000FF
#define ETH_MAC_GEN_V3_MAC_40G_LL_STATUS_PAUSE_ON_SHIFT 0
/* local fault indication received */
#define ETH_MAC_GEN_V3_MAC_40G_LL_STATUS_LOC_FAULT (1 << 8)
/* remote fault indication received */
#define ETH_MAC_GEN_V3_MAC_40G_LL_STATUS_REM_FAULT (1 << 9)
/* Link fault indication */
#define ETH_MAC_GEN_V3_MAC_40G_LL_STATUS_LI_FAULT (1 << 10)

/**** preamble_cfg_high register ****/
/* preamble value */
#define ETH_MAC_GEN_V3_PREAMBLE_CFG_HIGH_VAL_MASK 0x00FFFFFF
#define ETH_MAC_GEN_V3_PREAMBLE_CFG_HIGH_VAL_SHIFT 0

/**** mac_40g_ll_addr register ****/
/* Address value */
#define ETH_MAC_GEN_V3_MAC_40G_LL_ADDR_VAL_MASK 0x000003FF
#define ETH_MAC_GEN_V3_MAC_40G_LL_ADDR_VAL_SHIFT 0

/**** mac_40g_ll_ctrl register ****/
/* Force the MAC to stop TX transmission after low power mode. */
#define ETH_MAC_GEN_V3_MAC_40G_LL_CTRL_LPI_TXHOLD (1 << 0)

#define ETH_MAC_GEN_V3_MAC_40G_LL_CTRL_REG_LOWP_ENA (1 << 1)

/**** pcs_40g_fec_91_ll_addr register ****/
/* Address value */
#define ETH_MAC_GEN_V3_PCS_40G_FEC_91_LL_ADDR_VAL_MASK 0x000001FF
#define ETH_MAC_GEN_V3_PCS_40G_FEC_91_LL_ADDR_VAL_SHIFT 0

/**** pcs_40g_fec_91_ll_data register ****/
/* Data value */
#define ETH_MAC_GEN_V3_PCS_40G_FEC_91_LL_DATA_VAL_MASK 0x0000FFFF
#define ETH_MAC_GEN_V3_PCS_40G_FEC_91_LL_DATA_VAL_SHIFT 0

/**** pcs_40g_ll_eee_cfg register ****/
/* Low power timer configuration */
#define ETH_MAC_GEN_V3_PCS_40G_LL_EEE_CFG_TIMER_VAL_MASK 0x000000FF
#define ETH_MAC_GEN_V3_PCS_40G_LL_EEE_CFG_TIMER_VAL_SHIFT 0
/* Low power Fast wake */
#define ETH_MAC_GEN_V3_PCS_40G_LL_EEE_CFG_LPI_FW (1 << 8)

/**** pcs_40g_ll_eee_status register ****/
/* TX LPI mode */
#define ETH_MAC_GEN_V3_PCS_40G_LL_EEE_STATUS_TX_LPI_MODE_MASK 0x00000003
#define ETH_MAC_GEN_V3_PCS_40G_LL_EEE_STATUS_TX_LPI_MODE_SHIFT 0
/* TX LPI state */
#define ETH_MAC_GEN_V3_PCS_40G_LL_EEE_STATUS_TX_LPI_STATE_MASK 0x00000070
#define ETH_MAC_GEN_V3_PCS_40G_LL_EEE_STATUS_TX_LPI_STATE_SHIFT 4
/* TX LPI mode */
#define ETH_MAC_GEN_V3_PCS_40G_LL_EEE_STATUS_RX_LPI_MODE (1 << 8)
/* TX LPI state */
#define ETH_MAC_GEN_V3_PCS_40G_LL_EEE_STATUS_RX_LPI_STATE_MASK 0x00007000
#define ETH_MAC_GEN_V3_PCS_40G_LL_EEE_STATUS_RX_LPI_STATE_SHIFT 12
/* TX LPI active */
#define ETH_MAC_GEN_V3_PCS_40G_LL_EEE_STATUS_RX_LPI_ACTIVE (1 << 15)

/**** serdes_32_tx_shift register ****/
/* bit shift */
#define ETH_MAC_GEN_V3_SERDES_32_TX_SHIFT_SERDES_0_MASK 0x0000001F
#define ETH_MAC_GEN_V3_SERDES_32_TX_SHIFT_SERDES_0_SHIFT 0
/* bit shift */
#define ETH_MAC_GEN_V3_SERDES_32_TX_SHIFT_SERDES_1_MASK 0x000003E0
#define ETH_MAC_GEN_V3_SERDES_32_TX_SHIFT_SERDES_1_SHIFT 5
/* bit shift */
#define ETH_MAC_GEN_V3_SERDES_32_TX_SHIFT_SERDES_2_MASK 0x00007C00
#define ETH_MAC_GEN_V3_SERDES_32_TX_SHIFT_SERDES_2_SHIFT 10
/* bit shift */
#define ETH_MAC_GEN_V3_SERDES_32_TX_SHIFT_SERDES_3_MASK 0x000F8000
#define ETH_MAC_GEN_V3_SERDES_32_TX_SHIFT_SERDES_3_SHIFT 15

/**** serdes_32_rx_shift register ****/
/* bit shift */
#define ETH_MAC_GEN_V3_SERDES_32_RX_SHIFT_SERDES_0_MASK 0x0000001F
#define ETH_MAC_GEN_V3_SERDES_32_RX_SHIFT_SERDES_0_SHIFT 0
/* bit shift */
#define ETH_MAC_GEN_V3_SERDES_32_RX_SHIFT_SERDES_1_MASK 0x000003E0
#define ETH_MAC_GEN_V3_SERDES_32_RX_SHIFT_SERDES_1_SHIFT 5
/* bit shift */
#define ETH_MAC_GEN_V3_SERDES_32_RX_SHIFT_SERDES_2_MASK 0x00007C00
#define ETH_MAC_GEN_V3_SERDES_32_RX_SHIFT_SERDES_2_SHIFT 10
/* bit shift */
#define ETH_MAC_GEN_V3_SERDES_32_RX_SHIFT_SERDES_3_MASK 0x000F8000
#define ETH_MAC_GEN_V3_SERDES_32_RX_SHIFT_SERDES_3_SHIFT 15

/**** serdes_32_tx_sel register ****/
/*
 * 0 – directly from serdes
 * 1 – swapped
 * 2 – swapped with shift
 * 3 - legacy (based on gen cfg register)
 */
#define ETH_MAC_GEN_V3_SERDES_32_TX_SEL_SERDES_0_MASK 0x00000003
#define ETH_MAC_GEN_V3_SERDES_32_TX_SEL_SERDES_0_SHIFT 0
/*
 * 0 – directly from serdes
 * 1 – swapped
 * 2 – swapped with shift
 * 3 - legacy (based on gen cfg register)
 */
#define ETH_MAC_GEN_V3_SERDES_32_TX_SEL_SERDES_1_MASK 0x00000030
#define ETH_MAC_GEN_V3_SERDES_32_TX_SEL_SERDES_1_SHIFT 4
/*
 * 0 – directly from serdes
 * 1 – swapped
 * 2 – swapped with shift
 * 3 - legacy (based on gen cfg register)
 */
#define ETH_MAC_GEN_V3_SERDES_32_TX_SEL_SERDES_2_MASK 0x00000300
#define ETH_MAC_GEN_V3_SERDES_32_TX_SEL_SERDES_2_SHIFT 8
/*
 * 0 – directly from serdes
 * 1 – swapped
 * 2 – swapped with shift
 * 3 - legacy (based on gen cfg register)
 */
#define ETH_MAC_GEN_V3_SERDES_32_TX_SEL_SERDES_3_MASK 0x00003000
#define ETH_MAC_GEN_V3_SERDES_32_TX_SEL_SERDES_3_SHIFT 12

/**** serdes_32_rx_sel register ****/
/*
 * 0 – directly from serdes
 * 1 – swapped
 * 2 – swapped with shift
 * 3 - legacy (based on gen cfg register)
 */
#define ETH_MAC_GEN_V3_SERDES_32_RX_SEL_SERDES_0_MASK 0x00000003
#define ETH_MAC_GEN_V3_SERDES_32_RX_SEL_SERDES_0_SHIFT 0
/*
 * 0 – directly from serdes
 * 1 – swapped
 * 2 – swapped with shift
 * 3 - legacy (based on gen cfg register)
 */
#define ETH_MAC_GEN_V3_SERDES_32_RX_SEL_SERDES_1_MASK 0x00000030
#define ETH_MAC_GEN_V3_SERDES_32_RX_SEL_SERDES_1_SHIFT 4
/*
 * 0 – directly from serdes
 * 1 – swapped
 * 2 – swapped with shift
 * 3 - legacy (based on gen cfg register)
 */
#define ETH_MAC_GEN_V3_SERDES_32_RX_SEL_SERDES_2_MASK 0x00000300
#define ETH_MAC_GEN_V3_SERDES_32_RX_SEL_SERDES_2_SHIFT 8
/*
 * 0 – directly from serdes
 * 1 – swapped
 * 2 – swapped with shift
 * 3 - legacy (based on gen cfg register)
 */
#define ETH_MAC_GEN_V3_SERDES_32_RX_SEL_SERDES_3_MASK 0x00003000
#define ETH_MAC_GEN_V3_SERDES_32_RX_SEL_SERDES_3_SHIFT 12

/**** an_lt_ctrl register ****/
/* reset lane [3:0] */
#define ETH_MAC_GEN_V3_AN_LT_CTRL_SW_RESET_MASK 0x0000000F
#define ETH_MAC_GEN_V3_AN_LT_CTRL_SW_RESET_SHIFT 0

/* PHY LOS indication input selection
 * 0 - from serdes
 * 1 - from an_lt
 */
#define ETH_MAC_GEN_V3_AN_LT_CTRL_PHY_LOS_SEL_LANE_0 (1 << 8)
/* PHY LOS indication input selection
 * 0 - from serdes
 * 1 - from an_lt
 */
#define ETH_MAC_GEN_V3_AN_LT_CTRL_PHY_LOS_SEL_LANE_1 (1 << 9)
/* PHY LOS indication input selection
 * 0 - from serdes
 * 1 - from an_lt
 */
#define ETH_MAC_GEN_V3_AN_LT_CTRL_PHY_LOS_SEL_LANE_2 (1 << 10)
/* PHY LOS indication input selection
 * 0 - from serdes
 * 1 - from an_lt
 */
#define ETH_MAC_GEN_V3_AN_LT_CTRL_PHY_LOS_SEL_LANE_3 (1 << 11)

/**** an_lt_0_addr register ****/
/* Address value */
#define ETH_MAC_GEN_V3_AN_LT_0_ADDR_VAL_MASK 0x0000FFFF
#define ETH_MAC_GEN_V3_AN_LT_0_ADDR_VAL_SHIFT 0

/**** an_lt_1_addr register ****/
/* Address value */
#define ETH_MAC_GEN_V3_AN_LT_1_ADDR_VAL_MASK 0x0000FFFF
#define ETH_MAC_GEN_V3_AN_LT_1_ADDR_VAL_SHIFT 0

/**** an_lt_2_addr register ****/
/* Address value */
#define ETH_MAC_GEN_V3_AN_LT_2_ADDR_VAL_MASK 0x0000FFFF
#define ETH_MAC_GEN_V3_AN_LT_2_ADDR_VAL_SHIFT 0

/**** an_lt_3_addr register ****/
/* Address value */
#define ETH_MAC_GEN_V3_AN_LT_3_ADDR_VAL_MASK 0x0000FFFF
#define ETH_MAC_GEN_V3_AN_LT_3_ADDR_VAL_SHIFT 0

/**** ext_serdes_ctrl register ****/
/*
 * Lane 0, SERDES selection:
 * 0 – 10G SERDES, lane 0
 * 1 – 25G SERDES, lane 0
 */
#define ETH_MAC_GEN_V3_EXT_SERDES_CTRL_LANE_0_SEL_25_10 (1 << 0)
/*
 * Lane 1, SERDES selection:
 * 0 – 10G SERDES, lane 1
 * 1 – 25G SERDES, lane 1
 */
#define ETH_MAC_GEN_V3_EXT_SERDES_CTRL_LANE_1_SEL_25_10 (1 << 1)
/*
 * Lane 2, SERDES selection:
 * 0 – 10G SERDES, lane 2
 * 1 – 25G SERDES, lane 0
 */
#define ETH_MAC_GEN_V3_EXT_SERDES_CTRL_LANE_2_SEL_25_10 (1 << 2)
/*
 * Lane 3, SERDES selection:
 * 0 – 10G SERDES, lane 3
 * 1 – 25G SERDES, lane 1
 */
#define ETH_MAC_GEN_V3_EXT_SERDES_CTRL_LANE_3_SEL_25_10 (1 << 3)

/* Lane 0 Rx, 25G 40bit-32bit gearshitf sw reset */
#define ETH_MAC_GEN_V3_EXT_SERDES_CTRL_LANE_0_RX_25_GS_SW_RESET (1 << 4)
/* Lane 0 Tx, 25G 40bit-32bit gearshitf sw reset */
#define ETH_MAC_GEN_V3_EXT_SERDES_CTRL_LANE_0_TX_25_GS_SW_RESET (1 << 5)
/* Lane 1 Rx, 25G 40bit-32bit gearshitf sw reset */
#define ETH_MAC_GEN_V3_EXT_SERDES_CTRL_LANE_1_RX_25_GS_SW_RESET (1 << 6)
/* Lane 1 Tx, 25G 40bit-32bit gearshitf sw reset */
#define ETH_MAC_GEN_V3_EXT_SERDES_CTRL_LANE_1_TX_25_GS_SW_RESET (1 << 7)
/* SerDes 25G gear shift Tx lane selector */
#define ETH_MAC_GEN_V3_EXT_SERDES_CTRL_SRDS25_GS_TX_LANE_CLK_SEL (1 << 8)

/*** MAC Core registers addresses ***/
/* command config */
#define ETH_MAC_GEN_V3_MAC_40G_COMMAND_CONFIG_ADDR	0x00000008
#define ETH_MAC_GEN_V3_MAC_40G_COMMAND_CONFIG_TX_ENA	(1 << 0)
#define ETH_MAC_GEN_V3_MAC_40G_COMMAND_CONFIG_RX_ENA	(1 << 1)
#define ETH_MAC_GEN_V3_MAC_40G_COMMAND_CONFIG_PFC_MODE	(1 << 19)

/* frame length */
#define ETH_MAC_GEN_V3_MAC_40G_FRM_LENGTH_ADDR		0x00000014

#define ETH_MAC_GEN_V3_MAC_40G_CL01_PAUSE_QUANTA_ADDR	0x00000054
#define ETH_MAC_GEN_V3_MAC_40G_CL23_PAUSE_QUANTA_ADDR	0x00000058
#define ETH_MAC_GEN_V3_MAC_40G_CL45_PAUSE_QUANTA_ADDR	0x0000005C
#define ETH_MAC_GEN_V3_MAC_40G_CL67_PAUSE_QUANTA_ADDR	0x00000060
#define ETH_MAC_GEN_V3_MAC_40G_CL01_QUANTA_THRESH_ADDR	0x00000064
#define ETH_MAC_GEN_V3_MAC_40G_CL23_QUANTA_THRESH_ADDR	0x00000068
#define ETH_MAC_GEN_V3_MAC_40G_CL45_QUANTA_THRESH_ADDR	0x0000006C
#define ETH_MAC_GEN_V3_MAC_40G_CL67_QUANTA_THRESH_ADDR	0x00000070

/* spare */
#define ETH_MAC_GEN_V3_SPARE_CHICKEN_DISABLE_TIMESTAMP_STRETCH (1 << 0)

/*** PCS Core registers addresses ***/
/* 40g control/status */
#define ETH_MAC_GEN_V3_PCS_40G_CONTROL_STATUS_ADDR      0x00000000
/* 40g EEE control and capability */
#define ETH_MAC_GEN_V3_PCS_40G_EEE_CONTROL_ADDR         0x00000028
/* 10g control_1 */
#define ETH_MAC_KR_PCS_CONTROL_1_ADDR                   0x00000000

#define ETH_MAC_KR_PCS_BASE_R_STATUS2			0x00000021

#define ETH_MAC_KR_AN_MILLISECONDS_COUNTER_ADDR         0x00008000
#define ETH_MAC_AN_LT_MILLISECONDS_COUNTER_ADDR         0x00000020

#ifdef __cplusplus
}
#endif

#endif /* __AL_HAL_ETH_MAC_REGS_H__ */

/** @} end of Ethernet group */
