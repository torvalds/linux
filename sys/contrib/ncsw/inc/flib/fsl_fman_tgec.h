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

#ifndef __FSL_FMAN_TGEC_H
#define __FSL_FMAN_TGEC_H

#include "common/general.h"
#include "fsl_enet.h"


/* Transmit Inter-Packet Gap Length Register (TX_IPG_LENGTH) */
#define TGEC_TX_IPG_LENGTH_MASK	0x000003ff

enum tgec_counters {
	E_TGEC_COUNTER_R64,
	E_TGEC_COUNTER_R127,
	E_TGEC_COUNTER_R255,
	E_TGEC_COUNTER_R511,
	E_TGEC_COUNTER_R1023,
	E_TGEC_COUNTER_R1518,
	E_TGEC_COUNTER_R1519X,
	E_TGEC_COUNTER_TRFRG,
	E_TGEC_COUNTER_TRJBR,
	E_TGEC_COUNTER_RDRP,
	E_TGEC_COUNTER_RALN,
	E_TGEC_COUNTER_TRUND,
	E_TGEC_COUNTER_TROVR,
	E_TGEC_COUNTER_RXPF,
	E_TGEC_COUNTER_TXPF,
	E_TGEC_COUNTER_ROCT,
	E_TGEC_COUNTER_RMCA,
	E_TGEC_COUNTER_RBCA,
	E_TGEC_COUNTER_RPKT,
	E_TGEC_COUNTER_RUCA,
	E_TGEC_COUNTER_RERR,
	E_TGEC_COUNTER_TOCT,
	E_TGEC_COUNTER_TMCA,
	E_TGEC_COUNTER_TBCA,
	E_TGEC_COUNTER_TUCA,
	E_TGEC_COUNTER_TERR
};

/* Command and Configuration Register (COMMAND_CONFIG) */
#define CMD_CFG_EN_TIMESTAMP	0x00100000
#define CMD_CFG_TX_ADDR_INS_SEL	0x00080000
#define CMD_CFG_NO_LEN_CHK	0x00020000
#define CMD_CFG_SEND_IDLE	0x00010000
#define CMD_CFG_RX_ER_DISC	0x00004000
#define CMD_CFG_CMD_FRM_EN	0x00002000
#define CMD_CFG_STAT_CLR	0x00001000
#define CMD_CFG_LOOPBACK_EN	0x00000400
#define CMD_CFG_TX_ADDR_INS	0x00000200
#define CMD_CFG_PAUSE_IGNORE	0x00000100
#define CMD_CFG_PAUSE_FWD	0x00000080
#define CMD_CFG_PROMIS_EN	0x00000010
#define CMD_CFG_WAN_MODE	0x00000008
#define CMD_CFG_RX_EN		0x00000002
#define CMD_CFG_TX_EN		0x00000001

/* Interrupt Mask Register (IMASK) */
#define TGEC_IMASK_MDIO_SCAN_EVENT	0x00010000
#define TGEC_IMASK_MDIO_CMD_CMPL	0x00008000
#define TGEC_IMASK_REM_FAULT		0x00004000
#define TGEC_IMASK_LOC_FAULT		0x00002000
#define TGEC_IMASK_TX_ECC_ER		0x00001000
#define TGEC_IMASK_TX_FIFO_UNFL		0x00000800
#define TGEC_IMASK_TX_FIFO_OVFL		0x00000400
#define TGEC_IMASK_TX_ER			0x00000200
#define TGEC_IMASK_RX_FIFO_OVFL		0x00000100
#define TGEC_IMASK_RX_ECC_ER		0x00000080
#define TGEC_IMASK_RX_JAB_FRM		0x00000040
#define TGEC_IMASK_RX_OVRSZ_FRM		0x00000020
#define TGEC_IMASK_RX_RUNT_FRM		0x00000010
#define TGEC_IMASK_RX_FRAG_FRM		0x00000008
#define TGEC_IMASK_RX_LEN_ER		0x00000004
#define TGEC_IMASK_RX_CRC_ER		0x00000002
#define TGEC_IMASK_RX_ALIGN_ER		0x00000001

#define TGEC_EVENTS_MASK					\
	((uint32_t)(TGEC_IMASK_MDIO_SCAN_EVENT			| \
				TGEC_IMASK_MDIO_CMD_CMPL	| \
				TGEC_IMASK_REM_FAULT		| \
				TGEC_IMASK_LOC_FAULT		| \
				TGEC_IMASK_TX_ECC_ER		| \
				TGEC_IMASK_TX_FIFO_UNFL		| \
				TGEC_IMASK_TX_FIFO_OVFL		| \
				TGEC_IMASK_TX_ER		| \
				TGEC_IMASK_RX_FIFO_OVFL		| \
				TGEC_IMASK_RX_ECC_ER		| \
				TGEC_IMASK_RX_JAB_FRM		| \
				TGEC_IMASK_RX_OVRSZ_FRM		| \
				TGEC_IMASK_RX_RUNT_FRM		| \
				TGEC_IMASK_RX_FRAG_FRM		| \
				TGEC_IMASK_RX_LEN_ER		| \
				TGEC_IMASK_RX_CRC_ER		| \
				TGEC_IMASK_RX_ALIGN_ER))

/* Hashtable Control Register (HASHTABLE_CTRL) */
#define TGEC_HASH_MCAST_SHIFT	23
#define TGEC_HASH_MCAST_EN	0x00000200
#define TGEC_HASH_ADR_MSK	0x000001ff

#define DEFAULT_WAN_MODE_ENABLE		FALSE
#define DEFAULT_PROMISCUOUS_MODE_ENABLE	FALSE
#define DEFAULT_PAUSE_FORWARD_ENABLE	FALSE
#define DEFAULT_PAUSE_IGNORE		FALSE
#define DEFAULT_TX_ADDR_INS_ENABLE	FALSE
#define DEFAULT_LOOPBACK_ENABLE		FALSE
#define DEFAULT_CMD_FRAME_ENABLE	FALSE
#define DEFAULT_RX_ERROR_DISCARD	FALSE
#define DEFAULT_SEND_IDLE_ENABLE	FALSE
#define DEFAULT_NO_LENGTH_CHECK_ENABLE	TRUE
#define DEFAULT_LGTH_CHECK_NOSTDR	FALSE
#define DEFAULT_TIME_STAMP_ENABLE	FALSE
#define DEFAULT_TX_IPG_LENGTH		12
#define DEFAULT_MAX_FRAME_LENGTH	0x600
#define DEFAULT_PAUSE_QUANT		0xf000

/*
 * 10G memory map
 */
struct tgec_regs {
	uint32_t tgec_id;		/* 0x000 Controller ID */
	uint32_t reserved001[1];	/* 0x004 */
	uint32_t command_config;	/* 0x008 Control and configuration */
	uint32_t mac_addr_0;		/* 0x00c Lower 32 bits of the MAC adr */
	uint32_t mac_addr_1;		/* 0x010 Upper 16 bits of the MAC adr */
	uint32_t maxfrm;		/* 0x014 Maximum frame length */
	uint32_t pause_quant;		/* 0x018 Pause quanta */
	uint32_t rx_fifo_sections;	/* 0x01c  */
	uint32_t tx_fifo_sections;	/* 0x020  */
	uint32_t rx_fifo_almost_f_e;	/* 0x024  */
	uint32_t tx_fifo_almost_f_e;	/* 0x028  */
	uint32_t hashtable_ctrl;	/* 0x02c Hash table control*/
	uint32_t mdio_cfg_status;	/* 0x030  */
	uint32_t mdio_command;		/* 0x034  */
	uint32_t mdio_data;		/* 0x038  */
	uint32_t mdio_regaddr;		/* 0x03c  */
	uint32_t status;		/* 0x040  */
	uint32_t tx_ipg_len;		/* 0x044 Transmitter inter-packet-gap */
	uint32_t mac_addr_2;		/* 0x048 Lower 32 bits of 2nd MAC adr */
	uint32_t mac_addr_3;		/* 0x04c Upper 16 bits of 2nd MAC adr */
	uint32_t rx_fifo_ptr_rd;	/* 0x050  */
	uint32_t rx_fifo_ptr_wr;	/* 0x054  */
	uint32_t tx_fifo_ptr_rd;	/* 0x058  */
	uint32_t tx_fifo_ptr_wr;	/* 0x05c  */
	uint32_t imask;			/* 0x060 Interrupt mask */
	uint32_t ievent;		/* 0x064 Interrupt event */
	uint32_t udp_port;		/* 0x068 Defines a UDP Port number */
	uint32_t type_1588v2;		/* 0x06c Type field for 1588v2 */
	uint32_t reserved070[4];	/* 0x070 */
	/*10Ge Statistics Counter */
	uint32_t tfrm_u;		/* 80 aFramesTransmittedOK */
	uint32_t tfrm_l;		/* 84 aFramesTransmittedOK */
	uint32_t rfrm_u;		/* 88 aFramesReceivedOK */
	uint32_t rfrm_l;		/* 8c aFramesReceivedOK */
	uint32_t rfcs_u;		/* 90 aFrameCheckSequenceErrors */
	uint32_t rfcs_l;		/* 94 aFrameCheckSequenceErrors */
	uint32_t raln_u;		/* 98 aAlignmentErrors */
	uint32_t raln_l;		/* 9c aAlignmentErrors */
	uint32_t txpf_u;		/* A0 aPAUSEMACCtrlFramesTransmitted */
	uint32_t txpf_l;		/* A4 aPAUSEMACCtrlFramesTransmitted */
	uint32_t rxpf_u;		/* A8 aPAUSEMACCtrlFramesReceived */
	uint32_t rxpf_l;		/* Ac aPAUSEMACCtrlFramesReceived */
	uint32_t rlong_u;		/* B0 aFrameTooLongErrors */
	uint32_t rlong_l;		/* B4 aFrameTooLongErrors */
	uint32_t rflr_u;		/* B8 aInRangeLengthErrors */
	uint32_t rflr_l;		/* Bc aInRangeLengthErrors */
	uint32_t tvlan_u;		/* C0 VLANTransmittedOK */
	uint32_t tvlan_l;		/* C4 VLANTransmittedOK */
	uint32_t rvlan_u;		/* C8 VLANReceivedOK */
	uint32_t rvlan_l;		/* Cc VLANReceivedOK */
	uint32_t toct_u;		/* D0 ifOutOctets */
	uint32_t toct_l;		/* D4 ifOutOctets */
	uint32_t roct_u;		/* D8 ifInOctets */
	uint32_t roct_l;		/* Dc ifInOctets */
	uint32_t ruca_u;		/* E0 ifInUcastPkts */
	uint32_t ruca_l;		/* E4 ifInUcastPkts */
	uint32_t rmca_u;		/* E8 ifInMulticastPkts */
	uint32_t rmca_l;		/* Ec ifInMulticastPkts */
	uint32_t rbca_u;		/* F0 ifInBroadcastPkts */
	uint32_t rbca_l;		/* F4 ifInBroadcastPkts */
	uint32_t terr_u;		/* F8 ifOutErrors */
	uint32_t terr_l;		/* Fc ifOutErrors */
	uint32_t reserved100[2];	/* 100-108*/
	uint32_t tuca_u;		/* 108 ifOutUcastPkts */
	uint32_t tuca_l;		/* 10c ifOutUcastPkts */
	uint32_t tmca_u;		/* 110 ifOutMulticastPkts */
	uint32_t tmca_l;		/* 114 ifOutMulticastPkts */
	uint32_t tbca_u;		/* 118 ifOutBroadcastPkts */
	uint32_t tbca_l;		/* 11c ifOutBroadcastPkts */
	uint32_t rdrp_u;		/* 120 etherStatsDropEvents */
	uint32_t rdrp_l;		/* 124 etherStatsDropEvents */
	uint32_t reoct_u;		/* 128 etherStatsOctets */
	uint32_t reoct_l;		/* 12c etherStatsOctets */
	uint32_t rpkt_u;		/* 130 etherStatsPkts */
	uint32_t rpkt_l;		/* 134 etherStatsPkts */
	uint32_t trund_u;		/* 138 etherStatsUndersizePkts */
	uint32_t trund_l;		/* 13c etherStatsUndersizePkts */
	uint32_t r64_u;			/* 140 etherStatsPkts64Octets */
	uint32_t r64_l;			/* 144 etherStatsPkts64Octets */
	uint32_t r127_u;		/* 148 etherStatsPkts65to127Octets */
	uint32_t r127_l;		/* 14c etherStatsPkts65to127Octets */
	uint32_t r255_u;		/* 150 etherStatsPkts128to255Octets */
	uint32_t r255_l;		/* 154 etherStatsPkts128to255Octets */
	uint32_t r511_u;		/* 158 etherStatsPkts256to511Octets */
	uint32_t r511_l;		/* 15c etherStatsPkts256to511Octets */
	uint32_t r1023_u;		/* 160 etherStatsPkts512to1023Octets */
	uint32_t r1023_l;		/* 164 etherStatsPkts512to1023Octets */
	uint32_t r1518_u;		/* 168 etherStatsPkts1024to1518Octets */
	uint32_t r1518_l;		/* 16c etherStatsPkts1024to1518Octets */
	uint32_t r1519x_u;		/* 170 etherStatsPkts1519toX */
	uint32_t r1519x_l;		/* 174 etherStatsPkts1519toX */
	uint32_t trovr_u;		/* 178 etherStatsOversizePkts */
	uint32_t trovr_l;		/* 17c etherStatsOversizePkts */
	uint32_t trjbr_u;		/* 180 etherStatsJabbers */
	uint32_t trjbr_l;		/* 184 etherStatsJabbers */
	uint32_t trfrg_u;		/* 188 etherStatsFragments */
	uint32_t trfrg_l;		/* 18C etherStatsFragments */
	uint32_t rerr_u;		/* 190 ifInErrors */
	uint32_t rerr_l;		/* 194 ifInErrors */
};

/**
 * struct tgec_cfg - TGEC configuration
 *
 * @rx_error_discard:    Receive Erroneous Frame Discard Enable. When set to 1
 *            any frame received with an error is discarded in the
 *            Core and not forwarded to the Client interface.
 *            When set to 0 (Reset value), erroneous Frames are
 *            forwarded to the Client interface with ff_rx_err
 *            asserted.
 * @pause_ignore:    Ignore Pause Frame Quanta. If set to 1 received pause
 *            frames are ignored by the MAC. When set to 0
 *            (Reset value) the transmit process is stopped for the
 *            amount of time specified in the pause quanta received
 *            within a pause frame.
 * @pause_forward_enable:
 *            Terminate / Forward Pause Frames. If set to 1 pause
 *            frames are forwarded to the user application. When set
 *            to 0 (Reset value) pause frames are terminated and
 *            discarded within the MAC.
 * @no_length_check_enable:
 *            Payload Length Check Disable. When set to 0
 *            (Reset value), the Core checks the frame's payload
 *            length with the Frame Length/Type field, when set to 1
 *            the payload length check is disabled.
 * @cmd_frame_enable:    Enables reception of all command frames. When set to 1
 *            all Command Frames are accepted, when set to 0
 *            (Reset Value) only Pause Frames are accepted and all
 *            other Command Frames are rejected.
 * @send_idle_enable:    Force Idle Generation. When set to 1, the MAC
 *            permanently sends XGMII Idle sequences even when faults
 *            are received.
 * @wan_mode_enable:    WAN Mode Enable. Sets WAN mode (1) or LAN mode
 *            (0, default) of operation.
 * @promiscuous_mode_enable:
 *            Enables MAC promiscuous operation. When set to 1, all
 *            frames are received without any MAC address filtering,
 *            when set to 0 (Reset value) Unicast Frames with a
 *            destination address not matching the Core MAC Address
 *            (MAC Address programmed in Registers MAC_ADDR_0 and
 *            MAC_ADDR_1 or the MAC address programmed in Registers
 *            MAC_ADDR_2 and MAC_ADDR_3) are rejected.
 * @tx_addr_ins_enable:    Set Source MAC Address on Transmit. If set to 1 the
 *            MAC overwrites the source MAC address received from the
 *            Client Interface with one of the MAC addresses. If set
 *            to 0 (Reset value), the source MAC address from the
 *            Client Interface is transmitted unmodified to the line.
 * @loopback_enable:    PHY Interface Loopback. When set to 1, the signal
 *            loop_ena is set to '1', when set to 0 (Reset value)
 *            the signal loop_ena is set to 0.
 * @lgth_check_nostdr:    The Core interprets the Length/Type field differently
 *            depending on the value of this Bit
 * @time_stamp_enable:    This bit selects between enabling and disabling the
 *            IEEE 1588 functionality. 1: IEEE 1588 is enabled
 *            0: IEEE 1588 is disabled
 * @max_frame_length:    Maximum supported received frame length.
 *            The 10GEC MAC supports reception of any frame size up
 *            to 16,352 bytes (0x3FE0). Typical settings are
 *            0x05EE (1,518 bytes) for standard frames.
 *            Default setting is 0x0600 (1,536 bytes).
 *            Received frames that exceed this stated maximum
 *            are truncated.
 * @pause_quant:    Pause quanta value used with transmitted pause frames.
 *            Each quanta represents a 512 bit-times.
 * @tx_ipg_length:    Transmit Inter-Packet-Gap (IPG) value. A 6-bit value:
 *            Depending on LAN or WAN mode of operation the value has
 *            the following meaning: - LAN Mode: Number of octets in
 *            steps of 4. Valid values are 8, 12, 16, ... 100. DIC is
 *            fully supported (see 10.6.1 page 49) for any setting. A
 *            default of 12 (reset value) must be set to conform to
 *            IEEE802.3ae. Warning: When set to 8, PCS layers may not
 *            be able to perform clock rate compensation. - WAN Mode:
 *            Stretch factor. Valid values are 4..15. The stretch
 *            factor is calculated as (value+1)*8. A default of 12
 *            (reset value) must be set to conform to IEEE 802.3ae
 *            (i.e. 13*8=104). A larger value shrinks the IPG
 *            (increasing bandwidth).
 *
 * This structure contains basic TGEC configuration and must be passed to
 * fman_tgec_init() function.  A default set of configuration values can be
 * obtained by calling fman_tgec_defconfig().
 */
struct tgec_cfg {
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
	uint16_t	max_frame_length;
	uint16_t	pause_quant;
	uint32_t	tx_ipg_length;
	bool		skip_fman11_workaround;
};


void fman_tgec_defconfig(struct tgec_cfg *cfg);

/**
 * fman_tgec_init() - Init tgec hardware block
 * @regs:        Pointer to tgec register block
 * @cfg:        tgec configuration data
 * @exceptions_mask:    initial exceptions mask
 *
 * This function initializes the tgec controller and applies its
 * basic configuration.
 *
 * Returns: 0 if successful, an error code otherwise.
 */

int fman_tgec_init(struct tgec_regs *regs, struct tgec_cfg *cfg,
	uint32_t exception_mask);

void fman_tgec_enable(struct tgec_regs *regs, bool apply_rx, bool apply_tx);

void fman_tgec_disable(struct tgec_regs *regs, bool apply_rx, bool apply_tx);

uint32_t fman_tgec_get_revision(struct tgec_regs *regs);

void fman_tgec_set_mac_address(struct tgec_regs *regs, uint8_t *macaddr);

void fman_tgec_set_promiscuous(struct tgec_regs *regs, bool val);

/**
 * fman_tgec_reset_stat() - Completely resets all TGEC HW counters
 * @regs:    Pointer to TGEC register block
 */
void fman_tgec_reset_stat(struct tgec_regs *regs);

/**
 * fman_tgec_get_counter() - Reads TGEC HW counters
 * @regs:    Pointer to TGEC register block
 * @reg_name:    Counter name according to the appropriate enum
 *
 * Returns:    Required counter value
 */
uint64_t fman_tgec_get_counter(struct tgec_regs *regs,
	enum tgec_counters reg_name);

/**
 * fman_tgec_set_hash_table() - Sets the Hashtable Control Register
 * @regs:    Pointer to TGEC register block
 * @value:    Value to be written in Hashtable Control Register
 */
void fman_tgec_set_hash_table(struct tgec_regs *regs, uint32_t value);

/**
 * fman_tgec_set_tx_pause_frames() - Sets the Pause Quanta Register
 * @regs:    Pointer to TGEC register block
 * @pause_time:    Pause quanta value used with transmitted pause frames.
 *        Each quanta represents a 512 bit-times
 */
void fman_tgec_set_tx_pause_frames(struct tgec_regs *regs, uint16_t pause_time);

/**
 * fman_tgec_set_rx_ignore_pause_frames() - Changes the policy WRT pause frames
 * @regs:    Pointer to TGEC register block
 * @en:        Ignore/Respond to pause frame quanta
 *
 * Sets the value of PAUSE_IGNORE field in the COMMAND_CONFIG Register
 * 0 - MAC stops transmit process for the duration specified
 * in the Pause frame quanta of a received Pause frame.
 * 1 - MAC ignores received Pause frames.
 */
void fman_tgec_set_rx_ignore_pause_frames(struct tgec_regs *regs, bool en);

/**
 * fman_tgec_enable_1588_time_stamp() - change timestamp functionality
 * @regs:    Pointer to TGEC register block
 * @en:        enable/disable timestamp functionality
 *
 * Sets the value of EN_TIMESTAMP field in the COMMAND_CONFIG Register
 * IEEE 1588 timestamp functionality control:
 * 0 disabled, 1 enabled
 */

void fman_tgec_enable_1588_time_stamp(struct tgec_regs *regs, bool en);

uint32_t fman_tgec_get_event(struct tgec_regs *regs, uint32_t ev_mask);

void fman_tgec_ack_event(struct tgec_regs *regs, uint32_t ev_mask);

uint32_t fman_tgec_get_interrupt_mask(struct tgec_regs *regs);

/**
 * fman_tgec_add_addr_in_paddr() - Sets additional exact match MAC address
 * @regs:    Pointer to TGEC register block
 * @addr_ptr:    Pointer to 6-byte array containing the MAC address
 *
 * Sets the additional station MAC address
 */
void fman_tgec_add_addr_in_paddr(struct tgec_regs *regs, uint8_t *addr_ptr);

void fman_tgec_clear_addr_in_paddr(struct tgec_regs *regs);

void fman_tgec_enable_interrupt(struct tgec_regs *regs, uint32_t ev_mask);

void fman_tgec_disable_interrupt(struct tgec_regs *regs, uint32_t ev_mask);

void fman_tgec_reset_filter_table(struct tgec_regs *regs);

void fman_tgec_set_hash_table_entry(struct tgec_regs *regs, uint32_t crc);


/**
 * fman_tgec_get_max_frame_len() - Returns the maximum frame length value
 * @regs:    Pointer to TGEC register block
 */
uint16_t fman_tgec_get_max_frame_len(struct tgec_regs *regs);

/**
 * fman_tgec_set_erratum_tx_fifo_corruption_10gmac_a007() - Initialize the
 * main tgec configuration parameters
 * @regs:    Pointer to TGEC register block
 *
 * TODO
 */
void fman_tgec_set_erratum_tx_fifo_corruption_10gmac_a007(struct tgec_regs
	*regs);


#endif /* __FSL_FMAN_TGEC_H */
