/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef __FSL_FMAN_MEMAC_H
#define __FSL_FMAN_MEMAC_H

#include "common/general.h"
#include "fsl_enet.h"


#define MEMAC_NUM_OF_PADDRS 7 /* Num of additional exact match MAC adr regs */

/* Control and Configuration Register (COMMAND_CONFIG) */
#define CMD_CFG_MG		0x80000000 /* 00 Magic Packet detection */
#define CMD_CFG_REG_LOWP_RXETY	0x01000000 /* 07 Rx low power indication */
#define CMD_CFG_TX_LOWP_ENA	0x00800000 /* 08 Tx Low Power Idle Enable */
#define CMD_CFG_SFD_ANY		0x00200000 /* 10 Disable SFD check */
#define CMD_CFG_PFC_MODE	0x00080000 /* 12 Enable PFC */
#define CMD_CFG_NO_LEN_CHK	0x00020000 /* 14 Payload length check disable */
#define CMD_CFG_SEND_IDLE	0x00010000 /* 15 Force idle generation */
#define CMD_CFG_CNT_FRM_EN	0x00002000 /* 18 Control frame rx enable */
#define CMD_CFG_SW_RESET	0x00001000 /* 19 S/W Reset, self clearing bit */
#define CMD_CFG_TX_PAD_EN	0x00000800 /* 20 Enable Tx padding of frames */
#define CMD_CFG_LOOPBACK_EN	0x00000400 /* 21 XGMII/GMII loopback enable */
#define CMD_CFG_TX_ADDR_INS	0x00000200 /* 22 Tx source MAC addr insertion */
#define CMD_CFG_PAUSE_IGNORE	0x00000100 /* 23 Ignore Pause frame quanta */
#define CMD_CFG_PAUSE_FWD	0x00000080 /* 24 Terminate/frwd Pause frames */
#define CMD_CFG_CRC_FWD		0x00000040 /* 25 Terminate/frwd CRC of frames */
#define CMD_CFG_PAD_EN		0x00000020 /* 26 Frame padding removal */
#define CMD_CFG_PROMIS_EN	0x00000010 /* 27 Promiscuous operation enable */
#define CMD_CFG_WAN_MODE	0x00000008 /* 28 WAN mode enable */
#define CMD_CFG_RX_EN		0x00000002 /* 30 MAC receive path enable */
#define CMD_CFG_TX_EN		0x00000001 /* 31 MAC transmit path enable */

/* Transmit FIFO Sections Register (TX_FIFO_SECTIONS) */
#define TX_FIFO_SECTIONS_TX_EMPTY_MASK			0xFFFF0000
#define TX_FIFO_SECTIONS_TX_AVAIL_MASK			0x0000FFFF
#define TX_FIFO_SECTIONS_TX_EMPTY_DEFAULT_10G	0x00400000
#define TX_FIFO_SECTIONS_TX_EMPTY_DEFAULT_1G	0x00100000
#define TX_FIFO_SECTIONS_TX_EMPTY_PFC_10G		0x00360000
#define TX_FIFO_SECTIONS_TX_EMPTY_PFC_1G		0x00040000
#define TX_FIFO_SECTIONS_TX_AVAIL_10G			0x00000019
#define TX_FIFO_SECTIONS_TX_AVAIL_1G			0x00000020
#define TX_FIFO_SECTIONS_TX_AVAIL_SLOW_10G		0x00000060

#define GET_TX_EMPTY_DEFAULT_VALUE(_val)					\
_val &= ~TX_FIFO_SECTIONS_TX_EMPTY_MASK;					\
((_val == TX_FIFO_SECTIONS_TX_AVAIL_10G) ?					\
		(_val |= TX_FIFO_SECTIONS_TX_EMPTY_DEFAULT_10G) :	\
		(_val |= TX_FIFO_SECTIONS_TX_EMPTY_DEFAULT_1G));

#define GET_TX_EMPTY_PFC_VALUE(_val)						\
_val &= ~TX_FIFO_SECTIONS_TX_EMPTY_MASK;					\
((_val == TX_FIFO_SECTIONS_TX_AVAIL_10G) ?					\
		(_val |= TX_FIFO_SECTIONS_TX_EMPTY_PFC_10G) :		\
		(_val |= TX_FIFO_SECTIONS_TX_EMPTY_PFC_1G));

/* Interface Mode Register (IF_MODE) */
#define IF_MODE_MASK		0x00000003 /* 30-31 Mask on i/f mode bits */
#define IF_MODE_XGMII		0x00000000 /* 30-31 XGMII (10G) interface */
#define IF_MODE_GMII		0x00000002 /* 30-31 GMII (1G) interface */
#define IF_MODE_RGMII		0x00000004
#define IF_MODE_RGMII_AUTO	0x00008000
#define IF_MODE_RGMII_1000  0x00004000 /* 10 - 1000Mbps RGMII */
#define IF_MODE_RGMII_100   0x00000000 /* 00 - 100Mbps RGMII */
#define IF_MODE_RGMII_10    0x00002000 /* 01 - 10Mbps RGMII */
#define IF_MODE_RGMII_SP_MASK 0x00006000 /* Setsp mask bits */
#define IF_MODE_RGMII_FD    0x00001000 /* Full duplex RGMII */
#define IF_MODE_HD          0x00000040 /* Half duplex operation */

/* Hash table Control Register (HASHTABLE_CTRL) */
#define HASH_CTRL_MCAST_SHIFT	26
#define HASH_CTRL_MCAST_EN	0x00000100 /* 23 Mcast frame rx for hash */
#define HASH_CTRL_ADDR_MASK	0x0000003F /* 26-31 Hash table address code */

#define GROUP_ADDRESS		0x0000010000000000LL /* MAC mcast indication */
#define HASH_TABLE_SIZE		64 /* Hash tbl size */

/* Transmit Inter-Packet Gap Length Register (TX_IPG_LENGTH) */
#define MEMAC_TX_IPG_LENGTH_MASK	0x0000003F

/* Statistics Configuration Register (STATN_CONFIG) */
#define STATS_CFG_CLR		0x00000004 /* 29 Reset all counters */
#define STATS_CFG_CLR_ON_RD	0x00000002 /* 30 Clear on read */
#define STATS_CFG_SATURATE	0x00000001 /* 31 Saturate at the maximum val */

/* Interrupt Mask Register (IMASK) */
#define MEMAC_IMASK_MGI		0x40000000 /* 1 Magic pkt detect indication */
#define MEMAC_IMASK_TSECC_ER 0x20000000 /* 2 Timestamp FIFO ECC error evnt */
#define MEMAC_IMASK_TECC_ER	0x02000000 /* 6 Transmit frame ECC error evnt */
#define MEMAC_IMASK_RECC_ER	0x01000000 /* 7 Receive frame ECC error evnt */

#define MEMAC_ALL_ERRS_IMASK			\
		((uint32_t)(MEMAC_IMASK_TSECC_ER	| \
			MEMAC_IMASK_TECC_ER	| \
			MEMAC_IMASK_RECC_ER	| \
			MEMAC_IMASK_MGI))

#define MEMAC_IEVNT_PCS			0x80000000 /* PCS (XG). Link sync (G) */
#define MEMAC_IEVNT_AN			0x40000000 /* Auto-negotiation */
#define MEMAC_IEVNT_LT			0x20000000 /* Link Training/New page */
#define MEMAC_IEVNT_MGI			0x00004000 /* Magic pkt detection */
#define MEMAC_IEVNT_TS_ECC_ER   0x00002000 /* Timestamp FIFO ECC error */
#define MEMAC_IEVNT_RX_FIFO_OVFL	0x00001000 /* Rx FIFO overflow */
#define MEMAC_IEVNT_TX_FIFO_UNFL	0x00000800 /* Tx FIFO underflow */
#define MEMAC_IEVNT_TX_FIFO_OVFL	0x00000400 /* Tx FIFO overflow */
#define MEMAC_IEVNT_TX_ECC_ER		0x00000200 /* Tx frame ECC error */
#define MEMAC_IEVNT_RX_ECC_ER		0x00000100 /* Rx frame ECC error */
#define MEMAC_IEVNT_LI_FAULT		0x00000080 /* Link Interruption flt */
#define MEMAC_IEVNT_RX_EMPTY		0x00000040 /* Rx FIFO empty */
#define MEMAC_IEVNT_TX_EMPTY		0x00000020 /* Tx FIFO empty */
#define MEMAC_IEVNT_RX_LOWP		0x00000010 /* Low Power Idle */
#define MEMAC_IEVNT_PHY_LOS		0x00000004 /* Phy loss of signal */
#define MEMAC_IEVNT_REM_FAULT		0x00000002 /* Remote fault (XGMII) */
#define MEMAC_IEVNT_LOC_FAULT		0x00000001 /* Local fault (XGMII) */

enum memac_counters {
	E_MEMAC_COUNTER_R64,
	E_MEMAC_COUNTER_R127,
	E_MEMAC_COUNTER_R255,
	E_MEMAC_COUNTER_R511,
	E_MEMAC_COUNTER_R1023,
	E_MEMAC_COUNTER_R1518,
	E_MEMAC_COUNTER_R1519X,
	E_MEMAC_COUNTER_RFRG,
	E_MEMAC_COUNTER_RJBR,
	E_MEMAC_COUNTER_RDRP,
	E_MEMAC_COUNTER_RALN,
	E_MEMAC_COUNTER_TUND,
	E_MEMAC_COUNTER_ROVR,
	E_MEMAC_COUNTER_RXPF,
	E_MEMAC_COUNTER_TXPF,
	E_MEMAC_COUNTER_ROCT,
	E_MEMAC_COUNTER_RMCA,
	E_MEMAC_COUNTER_RBCA,
	E_MEMAC_COUNTER_RPKT,
	E_MEMAC_COUNTER_RUCA,
	E_MEMAC_COUNTER_RERR,
	E_MEMAC_COUNTER_TOCT,
	E_MEMAC_COUNTER_TMCA,
	E_MEMAC_COUNTER_TBCA,
	E_MEMAC_COUNTER_TUCA,
	E_MEMAC_COUNTER_TERR
};

#define DEFAULT_PAUSE_QUANTA	0xf000
#define DEFAULT_FRAME_LENGTH	0x600
#define DEFAULT_TX_IPG_LENGTH	12

/*
 * memory map
 */

struct mac_addr {
	uint32_t   mac_addr_l;	/* Lower 32 bits of 48-bit MAC address */
	uint32_t   mac_addr_u;	/* Upper 16 bits of 48-bit MAC address */
};

struct memac_regs {
	/* General Control and Status */
	uint32_t res0000[2];
	uint32_t command_config;	/* 0x008 Ctrl and cfg */
	struct mac_addr mac_addr0;	/* 0x00C-0x010 MAC_ADDR_0...1 */
	uint32_t maxfrm;		/* 0x014 Max frame length */
	uint32_t res0018[1];
	uint32_t rx_fifo_sections;	/* Receive FIFO configuration reg */
	uint32_t tx_fifo_sections;	/* Transmit FIFO configuration reg */
	uint32_t res0024[2];
	uint32_t hashtable_ctrl;	/* 0x02C Hash table control */
	uint32_t res0030[4];
	uint32_t ievent;		/* 0x040 Interrupt event */
	uint32_t tx_ipg_length;		/* 0x044 Transmitter inter-packet-gap */
	uint32_t res0048;
	uint32_t imask;			/* 0x04C Interrupt mask */
	uint32_t res0050;
	uint32_t pause_quanta[4];	/* 0x054 Pause quanta */
	uint32_t pause_thresh[4];	/* 0x064 Pause quanta threshold */
	uint32_t rx_pause_status;	/* 0x074 Receive pause status */
	uint32_t res0078[2];
	struct mac_addr mac_addr[MEMAC_NUM_OF_PADDRS]; /* 0x80-0x0B4 mac padr */
	uint32_t lpwake_timer;		/* 0x0B8 Low Power Wakeup Timer */
	uint32_t sleep_timer;		/* 0x0BC Transmit EEE Low Power Timer */
	uint32_t res00c0[8];
	uint32_t statn_config;		/* 0x0E0 Statistics configuration */
	uint32_t res00e4[7];
	/* Rx Statistics Counter */
	uint32_t reoct_l;
	uint32_t reoct_u;
	uint32_t roct_l;
	uint32_t roct_u;
	uint32_t raln_l;
	uint32_t raln_u;
	uint32_t rxpf_l;
	uint32_t rxpf_u;
	uint32_t rfrm_l;
	uint32_t rfrm_u;
	uint32_t rfcs_l;
	uint32_t rfcs_u;
	uint32_t rvlan_l;
	uint32_t rvlan_u;
	uint32_t rerr_l;
	uint32_t rerr_u;
	uint32_t ruca_l;
	uint32_t ruca_u;
	uint32_t rmca_l;
	uint32_t rmca_u;
	uint32_t rbca_l;
	uint32_t rbca_u;
	uint32_t rdrp_l;
	uint32_t rdrp_u;
	uint32_t rpkt_l;
	uint32_t rpkt_u;
	uint32_t rund_l;
	uint32_t rund_u;
	uint32_t r64_l;
	uint32_t r64_u;
	uint32_t r127_l;
	uint32_t r127_u;
	uint32_t r255_l;
	uint32_t r255_u;
	uint32_t r511_l;
	uint32_t r511_u;
	uint32_t r1023_l;
	uint32_t r1023_u;
	uint32_t r1518_l;
	uint32_t r1518_u;
	uint32_t r1519x_l;
	uint32_t r1519x_u;
	uint32_t rovr_l;
	uint32_t rovr_u;
	uint32_t rjbr_l;
	uint32_t rjbr_u;
	uint32_t rfrg_l;
	uint32_t rfrg_u;
	uint32_t rcnp_l;
	uint32_t rcnp_u;
	uint32_t rdrntp_l;
	uint32_t rdrntp_u;
	uint32_t res01d0[12];
	/* Tx Statistics Counter */
	uint32_t teoct_l;
	uint32_t teoct_u;
	uint32_t toct_l;
	uint32_t toct_u;
	uint32_t res0210[2];
	uint32_t txpf_l;
	uint32_t txpf_u;
	uint32_t tfrm_l;
	uint32_t tfrm_u;
	uint32_t tfcs_l;
	uint32_t tfcs_u;
	uint32_t tvlan_l;
	uint32_t tvlan_u;
	uint32_t terr_l;
	uint32_t terr_u;
	uint32_t tuca_l;
	uint32_t tuca_u;
	uint32_t tmca_l;
	uint32_t tmca_u;
	uint32_t tbca_l;
	uint32_t tbca_u;
	uint32_t res0258[2];
	uint32_t tpkt_l;
	uint32_t tpkt_u;
	uint32_t tund_l;
	uint32_t tund_u;
	uint32_t t64_l;
	uint32_t t64_u;
	uint32_t t127_l;
	uint32_t t127_u;
	uint32_t t255_l;
	uint32_t t255_u;
	uint32_t t511_l;
	uint32_t t511_u;
	uint32_t t1023_l;
	uint32_t t1023_u;
	uint32_t t1518_l;
	uint32_t t1518_u;
	uint32_t t1519x_l;
	uint32_t t1519x_u;
	uint32_t res02a8[6];
	uint32_t tcnp_l;
	uint32_t tcnp_u;
	uint32_t res02c8[14];
	/* Line Interface Control */
	uint32_t if_mode;		/* 0x300 Interface Mode Control */
	uint32_t if_status;		/* 0x304 Interface Status */
	uint32_t res0308[14];
	/* HiGig/2 */
	uint32_t hg_config;		/* 0x340 Control and cfg */
	uint32_t res0344[3];
	uint32_t hg_pause_quanta;	/* 0x350 Pause quanta */
	uint32_t res0354[3];
	uint32_t hg_pause_thresh;	/* 0x360 Pause quanta threshold */
	uint32_t res0364[3];
	uint32_t hgrx_pause_status;	/* 0x370 Receive pause status */
	uint32_t hg_fifos_status;	/* 0x374 fifos status */
	uint32_t rhm;			/* 0x378 rx messages counter */
	uint32_t thm;			/* 0x37C tx messages counter */
};

struct memac_cfg {
	bool		reset_on_init;
	bool		rx_error_discard;
	bool		pause_ignore;
	bool		pause_forward_enable;
	bool		no_length_check_enable;
	bool		cmd_frame_enable;
	bool		send_idle_enable;
	bool		wan_mode_enable;
	bool		promiscuous_mode_enable;
	bool		tx_addr_ins_enable;
	bool		loopback_enable;
	bool		lgth_check_nostdr;
	bool		time_stamp_enable;
	bool		pad_enable;
	bool		phy_tx_ena_on;
	bool		rx_sfd_any;
	bool		rx_pbl_fwd;
	bool		tx_pbl_fwd;
	bool		debug_mode;
	bool		wake_on_lan;
	uint16_t	max_frame_length;
	uint16_t	pause_quanta;
	uint32_t	tx_ipg_length;
};


/**
 * fman_memac_defconfig() - Get default MEMAC configuration
 * @cfg:    pointer to configuration structure.
 *
 * Call this function to obtain a default set of configuration values for
 * initializing MEMAC. The user can overwrite any of the values before calling
 * fman_memac_init(), if specific configuration needs to be applied.
 */
void fman_memac_defconfig(struct memac_cfg *cfg);

int fman_memac_init(struct memac_regs *regs,
	struct memac_cfg *cfg,
	enum enet_interface enet_interface,
	enum enet_speed enet_speed,
	bool slow_10g_if,
	uint32_t exceptions);

void fman_memac_enable(struct memac_regs *regs, bool apply_rx, bool apply_tx);

void fman_memac_disable(struct memac_regs *regs, bool apply_rx, bool apply_tx);

void fman_memac_set_promiscuous(struct memac_regs *regs, bool val);

void fman_memac_add_addr_in_paddr(struct memac_regs *regs,
	uint8_t *adr,
	uint8_t paddr_num);

void fman_memac_clear_addr_in_paddr(struct memac_regs *regs,
	uint8_t paddr_num);

uint64_t fman_memac_get_counter(struct memac_regs *regs,
	enum memac_counters reg_name);

void fman_memac_set_tx_pause_frames(struct memac_regs *regs,
	uint8_t priority, uint16_t pauseTime, uint16_t threshTime);

uint16_t fman_memac_get_max_frame_len(struct memac_regs *regs);

void fman_memac_set_exception(struct memac_regs *regs, uint32_t val,
	bool enable);

void fman_memac_reset_stat(struct memac_regs *regs);

void fman_memac_reset(struct memac_regs *regs);

void fman_memac_reset_filter_table(struct memac_regs *regs);

void fman_memac_set_hash_table_entry(struct memac_regs *regs, uint32_t crc);

void fman_memac_set_hash_table(struct memac_regs *regs, uint32_t val);

void fman_memac_set_rx_ignore_pause_frames(struct memac_regs *regs,
	bool enable);

void fman_memac_set_wol(struct memac_regs *regs, bool enable);

uint32_t fman_memac_get_event(struct memac_regs *regs, uint32_t ev_mask);

void fman_memac_ack_event(struct memac_regs *regs, uint32_t ev_mask);

uint32_t fman_memac_get_interrupt_mask(struct memac_regs *regs);

void fman_memac_adjust_link(struct memac_regs *regs,
	enum enet_interface iface_mode,
	enum enet_speed speed, bool full_dx);



#endif /*__FSL_FMAN_MEMAC_H*/
