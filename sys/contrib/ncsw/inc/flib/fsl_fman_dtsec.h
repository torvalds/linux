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

#ifndef __FSL_FMAN_DTSEC_H
#define __FSL_FMAN_DTSEC_H

#include "common/general.h"
#include "fsl_enet.h"

/**
 * DOC: dTSEC Init sequence
 *
 * To prepare dTSEC block for transfer use the following call sequence:
 *
 * - fman_dtsec_defconfig() - This step is optional and yet recommended. Its
 * use is to obtain the default dTSEC configuration parameters.
 *
 * - Change dtsec configuration in &dtsec_cfg. This structure will be used
 * to customize the dTSEC behavior.
 *
 * - fman_dtsec_init() - Applies the configuration on dTSEC hardware.  Note that
 * dTSEC is initialized while both Tx and Rx are disabled.
 *
 * - fman_dtsec_set_mac_address() - Set the station address (mac address).
 * This is used by dTSEC to match against received packets.
 *
 * - fman_dtsec_adjust_link() - Set the link speed and duplex parameters
 * after the PHY establishes the link.
 *
 * - dtsec_enable_tx() and dtsec_enable_rx() to enable transmission and
 * reception.
 */

/**
 * DOC: dTSEC Graceful stop
 *
 * To temporary stop dTSEC activity use fman_dtsec_stop_tx() and
 * fman_dtsec_stop_rx(). Note that these functions request dTSEC graceful stop
 * but return before this stop is complete.  To query for graceful stop
 * completion use fman_dtsec_get_event() and check DTSEC_IEVENT_GTSC and
 * DTSEC_IEVENT_GRSC bits. Alternatively the dTSEC interrupt mask can be set to
 * enable graceful stop interrupts.
 *
 * To resume operation after graceful stop use fman_dtsec_start_tx() and
 * fman_dtsec_start_rx().
 */

/**
 * DOC: dTSEC interrupt handling
 *
 * This code does not provide an interrupt handler for dTSEC.  Instead this
 * handler should be implemented and registered to the operating system by the
 * caller.  Some primitives for accessing the event status and mask registers
 * are provided.
 *
 * See "dTSEC Events" section for a list of events that dTSEC can generate.
 */

/**
 * DOC: dTSEC Events
 *
 * Interrupt events cause dTSEC event bits to be set.  Software may poll the
 * event register at any time to check for pending interrupts.  If an event
 * occurs and its corresponding enable bit is set in the interrupt mask
 * register, the event also causes a hardware interrupt at the PIC.
 *
 * To poll for event status use the fman_dtsec_get_event() function.
 * To configure the interrupt mask use fman_dtsec_enable_interrupt() and
 * fman_dtsec_disable_interrupt() functions.
 * After servicing a dTSEC interrupt use fman_dtsec_ack_event to reset the
 * serviced event bit.
 *
 * The following events may be signaled by dTSEC hardware:
 *
 * %DTSEC_IEVENT_BABR - Babbling receive error.  This bit indicates that
 * a frame was received with length in excess of the MAC's maximum frame length
 * register.
 *
 * %DTSEC_IEVENT_RXC - Receive control (pause frame) interrupt.  A pause
 * control frame was received while Rx pause frame handling is enabled.
 * Also see fman_dtsec_handle_rx_pause().
 *
 * %DTSEC_IEVENT_MSRO - MIB counter overflow.  The count for one of the MIB
 * counters has exceeded the size of its register.
 *
 * %DTSEC_IEVENT_GTSC - Graceful transmit stop complete.  Graceful stop is now
 * complete. The transmitter is in a stopped state, in which only pause frames
 * can be transmitted.
 * Also see fman_dtsec_stop_tx().
 *
 * %DTSEC_IEVENT_BABT - Babbling transmit error.  The transmitted frame length
 * has exceeded the value in the MAC's Maximum Frame Length register.
 *
 * %DTSEC_IEVENT_TXC - Transmit control (pause frame) interrupt.  his bit
 * indicates that a control frame was transmitted.
 *
 * %DTSEC_IEVENT_TXE - Transmit error.  This bit indicates that an error
 * occurred on the transmitted channel.  This bit is set whenever any transmit
 * error occurs which causes the dTSEC to discard all or part of a frame
 * (LC, CRL, XFUN).
 *
 * %DTSEC_IEVENT_LC - Late collision.  This bit indicates that a collision
 * occurred beyond the collision window (slot time) in half-duplex mode.
 * The frame is truncated with a bad CRC and the remainder of the frame
 * is discarded.
 *
 * %DTSEC_IEVENT_CRL - Collision retry limit.  is bit indicates that the number
 * of successive transmission collisions has exceeded the MAC's half-duplex
 * register's retransmission maximum count.  The frame is discarded without
 * being transmitted and transmission of the next frame commences.  This only
 * occurs while in half-duplex mode.
 * The number of retransmit attempts can be set in
 * &dtsec_halfdup_cfg.@retransmit before calling fman_dtsec_init().
 *
 * %DTSEC_IEVENT_XFUN - Transmit FIFO underrun.  This bit indicates that the
 * transmit FIFO became empty before the complete frame was transmitted.
 * The frame is truncated with a bad CRC and the remainder of the frame is
 * discarded.
 *
 * %DTSEC_IEVENT_MAG - TBD
 *
 * %DTSEC_IEVENT_MMRD - MII management read completion.
 *
 * %DTSEC_IEVENT_MMWR - MII management write completion.
 *
 * %DTSEC_IEVENT_GRSC - Graceful receive stop complete.  It allows the user to
 * know if the system has completed the stop and it is safe to write to receive
 * registers (status, control or configuration registers) that are used by the
 * system during normal operation.
 *
 * %DTSEC_IEVENT_TDPE - Internal data error on transmit.  This bit indicates
 * that the dTSEC has detected a parity error on its stored transmit data, which
 * is likely to compromise the validity of recently transferred frames.
 *
 * %DTSEC_IEVENT_RDPE - Internal data error on receive.  This bit indicates that
 * the dTSEC has detected a parity error on its stored receive data, which is
 * likely to compromise the validity of recently transferred frames.
 */
/* Interrupt Mask Register (IMASK) */
#define DTSEC_IMASK_BREN	0x80000000
#define DTSEC_IMASK_RXCEN	0x40000000
#define DTSEC_IMASK_MSROEN	0x04000000
#define DTSEC_IMASK_GTSCEN	0x02000000
#define DTSEC_IMASK_BTEN	0x01000000
#define DTSEC_IMASK_TXCEN	0x00800000
#define DTSEC_IMASK_TXEEN	0x00400000
#define DTSEC_IMASK_LCEN	0x00040000
#define DTSEC_IMASK_CRLEN	0x00020000
#define DTSEC_IMASK_XFUNEN	0x00010000
#define DTSEC_IMASK_ABRTEN	0x00008000
#define DTSEC_IMASK_IFERREN	0x00004000
#define DTSEC_IMASK_MAGEN	0x00000800
#define DTSEC_IMASK_MMRDEN	0x00000400
#define DTSEC_IMASK_MMWREN	0x00000200
#define DTSEC_IMASK_GRSCEN	0x00000100
#define DTSEC_IMASK_TDPEEN	0x00000002
#define DTSEC_IMASK_RDPEEN	0x00000001

#define DTSEC_EVENTS_MASK					\
	((uint32_t)(DTSEC_IMASK_BREN    | \
				DTSEC_IMASK_RXCEN   | \
				DTSEC_IMASK_BTEN    | \
				DTSEC_IMASK_TXCEN   | \
				DTSEC_IMASK_TXEEN   | \
				DTSEC_IMASK_ABRTEN  | \
				DTSEC_IMASK_LCEN    | \
				DTSEC_IMASK_CRLEN   | \
				DTSEC_IMASK_XFUNEN  | \
				DTSEC_IMASK_IFERREN | \
				DTSEC_IMASK_MAGEN   | \
				DTSEC_IMASK_TDPEEN  | \
				DTSEC_IMASK_RDPEEN))

/* dtsec timestamp event bits */
#define TMR_PEMASK_TSREEN	0x00010000
#define TMR_PEVENT_TSRE		0x00010000

/* Group address bit indication */
#define MAC_GROUP_ADDRESS	0x0000010000000000ULL
/* size in bytes of L2 address */
#define MAC_ADDRLEN		6

#define DEFAULT_HALFDUP_ON		FALSE
#define DEFAULT_HALFDUP_RETRANSMIT	0xf
#define DEFAULT_HALFDUP_COLL_WINDOW	0x37
#define DEFAULT_HALFDUP_EXCESS_DEFER	TRUE
#define DEFAULT_HALFDUP_NO_BACKOFF	FALSE
#define DEFAULT_HALFDUP_BP_NO_BACKOFF	FALSE
#define DEFAULT_HALFDUP_ALT_BACKOFF_VAL	0x0A
#define DEFAULT_HALFDUP_ALT_BACKOFF_EN	FALSE
#define DEFAULT_RX_DROP_BCAST		FALSE
#define DEFAULT_RX_SHORT_FRM		TRUE
#define DEFAULT_RX_LEN_CHECK		FALSE
#define DEFAULT_TX_PAD_CRC		TRUE
#define DEFAULT_TX_CRC			FALSE
#define DEFAULT_RX_CTRL_ACC		FALSE
#define DEFAULT_TX_PAUSE_TIME		0xf000
#define DEFAULT_TBIPA			5
#define DEFAULT_RX_PREPEND		0
#define DEFAULT_PTP_TSU_EN		TRUE
#define DEFAULT_PTP_EXCEPTION_EN	TRUE
#define DEFAULT_PREAMBLE_LEN		7
#define DEFAULT_RX_PREAMBLE		FALSE
#define DEFAULT_TX_PREAMBLE		FALSE
#define DEFAULT_LOOPBACK		FALSE
#define DEFAULT_RX_TIME_STAMP_EN	FALSE
#define DEFAULT_TX_TIME_STAMP_EN	FALSE
#define DEFAULT_RX_FLOW			TRUE
#define DEFAULT_TX_FLOW			TRUE
#define DEFAULT_RX_GROUP_HASH_EXD	FALSE
#define DEFAULT_TX_PAUSE_TIME_EXTD	0
#define DEFAULT_RX_PROMISC		FALSE
#define DEFAULT_NON_BACK_TO_BACK_IPG1	0x40
#define DEFAULT_NON_BACK_TO_BACK_IPG2	0x60
#define DEFAULT_MIN_IFG_ENFORCEMENT	0x50
#define DEFAULT_BACK_TO_BACK_IPG	0x60
#define DEFAULT_MAXIMUM_FRAME		0x600
#define DEFAULT_TBI_PHY_ADDR		5
#define DEFAULT_WAKE_ON_LAN			FALSE

/* register related defines (bits, field offsets..) */
#define DTSEC_ID1_ID			0xffff0000
#define DTSEC_ID1_REV_MJ		0x0000FF00
#define DTSEC_ID1_REV_MN		0x000000ff

#define DTSEC_ID2_INT_REDUCED_OFF	0x00010000
#define DTSEC_ID2_INT_NORMAL_OFF	0x00020000

#define DTSEC_ECNTRL_CLRCNT		0x00004000
#define DTSEC_ECNTRL_AUTOZ		0x00002000
#define DTSEC_ECNTRL_STEN		0x00001000
#define DTSEC_ECNTRL_CFG_RO		0x80000000
#define DTSEC_ECNTRL_GMIIM		0x00000040
#define DTSEC_ECNTRL_TBIM		0x00000020
#define DTSEC_ECNTRL_SGMIIM		0x00000002
#define DTSEC_ECNTRL_RPM		0x00000010
#define DTSEC_ECNTRL_R100M		0x00000008
#define DTSEC_ECNTRL_RMM		0x00000004
#define DTSEC_ECNTRL_QSGMIIM		0x00000001

#define DTSEC_TCTRL_THDF		0x00000800
#define DTSEC_TCTRL_TTSE		0x00000040
#define DTSEC_TCTRL_GTS			0x00000020
#define DTSEC_TCTRL_TFC_PAUSE		0x00000010

/* PTV offsets */
#define PTV_PTE_OFST		16

#define RCTRL_CFA		0x00008000
#define RCTRL_GHTX		0x00000400
#define RCTRL_RTSE		0x00000040
#define RCTRL_GRS		0x00000020
#define RCTRL_BC_REJ		0x00000010
#define RCTRL_MPROM		0x00000008
#define RCTRL_RSF		0x00000004
#define RCTRL_UPROM		0x00000001
#define RCTRL_PROM		(RCTRL_UPROM | RCTRL_MPROM)

#define TMR_CTL_ESFDP		0x00000800
#define TMR_CTL_ESFDE		0x00000400

#define MACCFG1_SOFT_RESET	0x80000000
#define MACCFG1_LOOPBACK	0x00000100
#define MACCFG1_RX_FLOW		0x00000020
#define MACCFG1_TX_FLOW		0x00000010
#define MACCFG1_TX_EN		0x00000001
#define MACCFG1_RX_EN		0x00000004
#define MACCFG1_RESET_RxMC	0x00080000
#define MACCFG1_RESET_TxMC	0x00040000
#define MACCFG1_RESET_RxFUN	0x00020000
#define MACCFG1_RESET_TxFUN	0x00010000

#define MACCFG2_NIBBLE_MODE	0x00000100
#define MACCFG2_BYTE_MODE	0x00000200
#define MACCFG2_PRE_AM_Rx_EN	0x00000080
#define MACCFG2_PRE_AM_Tx_EN	0x00000040
#define MACCFG2_LENGTH_CHECK	0x00000010
#define MACCFG2_MAGIC_PACKET_EN	0x00000008
#define MACCFG2_PAD_CRC_EN	0x00000004
#define MACCFG2_CRC_EN		0x00000002
#define MACCFG2_FULL_DUPLEX	0x00000001

#define PREAMBLE_LENGTH_SHIFT	12

#define IPGIFG_NON_BACK_TO_BACK_IPG_1_SHIFT	24
#define IPGIFG_NON_BACK_TO_BACK_IPG_2_SHIFT	16
#define IPGIFG_MIN_IFG_ENFORCEMENT_SHIFT	8

#define IPGIFG_NON_BACK_TO_BACK_IPG_1	0x7F000000
#define IPGIFG_NON_BACK_TO_BACK_IPG_2	0x007F0000
#define IPGIFG_MIN_IFG_ENFORCEMENT	0x0000FF00
#define IPGIFG_BACK_TO_BACK_IPG		0x0000007F

#define HAFDUP_ALT_BEB			0x00080000
#define HAFDUP_BP_NO_BACKOFF		0x00040000
#define HAFDUP_NO_BACKOFF		0x00020000
#define HAFDUP_EXCESS_DEFER		0x00010000
#define HAFDUP_COLLISION_WINDOW		0x000003ff

#define HAFDUP_ALTERNATE_BEB_TRUNCATION_SHIFT	20
#define HAFDUP_RETRANSMISSION_MAX_SHIFT		12
#define HAFDUP_RETRANSMISSION_MAX		0x0000f000

#define NUM_OF_HASH_REGS	8 /* Number of hash table registers */

/* CAR1/2 bits */
#define DTSEC_CAR1_TR64		0x80000000
#define DTSEC_CAR1_TR127	0x40000000
#define DTSEC_CAR1_TR255	0x20000000
#define DTSEC_CAR1_TR511	0x10000000
#define DTSEC_CAR1_TRK1		0x08000000
#define DTSEC_CAR1_TRMAX	0x04000000
#define DTSEC_CAR1_TRMGV	0x02000000

#define DTSEC_CAR1_RBYT		0x00010000
#define DTSEC_CAR1_RPKT		0x00008000
#define DTSEC_CAR1_RFCS		0x00004000
#define DTSEC_CAR1_RMCA		0x00002000
#define DTSEC_CAR1_RBCA		0x00001000
#define DTSEC_CAR1_RXCF		0x00000800
#define DTSEC_CAR1_RXPF		0x00000400
#define DTSEC_CAR1_RXUO		0x00000200
#define DTSEC_CAR1_RALN		0x00000100
#define DTSEC_CAR1_RFLR		0x00000080
#define DTSEC_CAR1_RCDE		0x00000040
#define DTSEC_CAR1_RCSE		0x00000020
#define DTSEC_CAR1_RUND		0x00000010
#define DTSEC_CAR1_ROVR		0x00000008
#define DTSEC_CAR1_RFRG		0x00000004
#define DTSEC_CAR1_RJBR		0x00000002
#define DTSEC_CAR1_RDRP		0x00000001

#define DTSEC_CAR2_TJBR		0x00080000
#define DTSEC_CAR2_TFCS		0x00040000
#define DTSEC_CAR2_TXCF		0x00020000
#define DTSEC_CAR2_TOVR		0x00010000
#define DTSEC_CAR2_TUND		0x00008000
#define DTSEC_CAR2_TFRG		0x00004000
#define DTSEC_CAR2_TBYT		0x00002000
#define DTSEC_CAR2_TPKT		0x00001000
#define DTSEC_CAR2_TMCA		0x00000800
#define DTSEC_CAR2_TBCA		0x00000400
#define DTSEC_CAR2_TXPF		0x00000200
#define DTSEC_CAR2_TDFR		0x00000100
#define DTSEC_CAR2_TEDF		0x00000080
#define DTSEC_CAR2_TSCL		0x00000040
#define DTSEC_CAR2_TMCL		0x00000020
#define DTSEC_CAR2_TLCL		0x00000010
#define DTSEC_CAR2_TXCL		0x00000008
#define DTSEC_CAR2_TNCL		0x00000004
#define DTSEC_CAR2_TDRP		0x00000001

#define CAM1_ERRORS_ONLY \
	(DTSEC_CAR1_RXPF | DTSEC_CAR1_RALN | DTSEC_CAR1_RFLR \
	| DTSEC_CAR1_RCDE | DTSEC_CAR1_RCSE | DTSEC_CAR1_RUND \
	| DTSEC_CAR1_ROVR | DTSEC_CAR1_RFRG | DTSEC_CAR1_RJBR \
	| DTSEC_CAR1_RDRP)

#define CAM2_ERRORS_ONLY (DTSEC_CAR2_TFCS | DTSEC_CAR2_TXPF | DTSEC_CAR2_TDRP)

/*
 * Group of dTSEC specific counters relating to the standard RMON MIB Group 1
 * (or Ethernet) statistics.
 */
#define CAM1_MIB_GRP_1 \
	(DTSEC_CAR1_RDRP | DTSEC_CAR1_RBYT | DTSEC_CAR1_RPKT | DTSEC_CAR1_RMCA\
	| DTSEC_CAR1_RBCA | DTSEC_CAR1_RALN | DTSEC_CAR1_RUND | DTSEC_CAR1_ROVR\
	| DTSEC_CAR1_RFRG | DTSEC_CAR1_RJBR \
	| DTSEC_CAR1_TR64 | DTSEC_CAR1_TR127 | DTSEC_CAR1_TR255 \
	| DTSEC_CAR1_TR511 | DTSEC_CAR1_TRMAX)

#define CAM2_MIB_GRP_1 (DTSEC_CAR2_TNCL | DTSEC_CAR2_TDRP)

/* memory map */

struct dtsec_regs {
	/* dTSEC General Control and Status Registers */
	uint32_t tsec_id;	/* 0x000 ETSEC_ID register */
	uint32_t tsec_id2;	/* 0x004 ETSEC_ID2 register */
	uint32_t ievent;	/* 0x008 Interrupt event register */
	uint32_t imask;		/* 0x00C Interrupt mask register */
	uint32_t reserved0010[1];
	uint32_t ecntrl;	/* 0x014 E control register */
	uint32_t ptv;		/* 0x018 Pause time value register */
	uint32_t tbipa;		/* 0x01C TBI PHY address register */
	uint32_t tmr_ctrl;	/* 0x020 Time-stamp Control register */
	uint32_t tmr_pevent;	/* 0x024 Time-stamp event register */
	uint32_t tmr_pemask;	/* 0x028 Timer event mask register */
	uint32_t reserved002c[5];
	uint32_t tctrl;		/* 0x040 Transmit control register */
	uint32_t reserved0044[3];
	uint32_t rctrl;		/* 0x050 Receive control register */
	uint32_t reserved0054[11];
	uint32_t igaddr[8]; 	/* 0x080-0x09C Individual/group address */
	uint32_t gaddr[8];	/* 0x0A0-0x0BC Group address registers 0-7 */
	uint32_t reserved00c0[16];
	uint32_t maccfg1;		/* 0x100 MAC configuration #1 */
	uint32_t maccfg2;		/* 0x104 MAC configuration #2 */
	uint32_t ipgifg;		/* 0x108 IPG/IFG */
	uint32_t hafdup;		/* 0x10C Half-duplex */
	uint32_t maxfrm;		/* 0x110 Maximum frame */
	uint32_t reserved0114[10];
	uint32_t ifstat;		/* 0x13C Interface status */
	uint32_t macstnaddr1;		/* 0x140 Station Address,part 1 */
	uint32_t macstnaddr2;		/* 0x144 Station Address,part 2  */
	struct {
	    uint32_t exact_match1; /* octets 1-4 */
	    uint32_t exact_match2; /* octets 5-6 */
	} macaddr[15];	/* 0x148-0x1BC mac exact match addresses 1-15 */
	uint32_t reserved01c0[16];
	uint32_t tr64;	/* 0x200 transmit and receive 64 byte frame counter */
	uint32_t tr127;	/* 0x204 transmit and receive 65 to 127 byte frame
			 * counter */
	uint32_t tr255;	/* 0x208 transmit and receive 128 to 255 byte frame
			 * counter */
	uint32_t tr511;	/* 0x20C transmit and receive 256 to 511 byte frame
			 * counter */
	uint32_t tr1k;	/* 0x210 transmit and receive 512 to 1023 byte frame
			 * counter */
	uint32_t trmax;	/* 0x214 transmit and receive 1024 to 1518 byte frame
			 * counter */
	uint32_t trmgv;	/* 0x218 transmit and receive 1519 to 1522 byte good
			 * VLAN frame count */
	uint32_t rbyt;	/* 0x21C receive byte counter */
	uint32_t rpkt;	/* 0x220 receive packet counter */
	uint32_t rfcs;	/* 0x224 receive FCS error counter */
	uint32_t rmca;	/* 0x228 RMCA receive multicast packet counter */
	uint32_t rbca;	/* 0x22C receive broadcast packet counter */
	uint32_t rxcf;	/* 0x230 receive control frame packet counter */
	uint32_t rxpf;	/* 0x234 receive pause frame packet counter */
	uint32_t rxuo;	/* 0x238 receive unknown OP code counter */
	uint32_t raln;	/* 0x23C receive alignment error counter */
	uint32_t rflr;	/* 0x240 receive frame length error counter */
	uint32_t rcde;	/* 0x244 receive code error counter */
	uint32_t rcse;	/* 0x248 receive carrier sense error counter */
	uint32_t rund;	/* 0x24C receive undersize packet counter */
	uint32_t rovr;	/* 0x250 receive oversize packet counter */
	uint32_t rfrg;	/* 0x254 receive fragments counter */
	uint32_t rjbr;	/* 0x258 receive jabber counter */
	uint32_t rdrp;	/* 0x25C receive drop */
	uint32_t tbyt;	/* 0x260 transmit byte counter */
	uint32_t tpkt;	/* 0x264 transmit packet counter */
	uint32_t tmca;	/* 0x268 transmit multicast packet counter */
	uint32_t tbca;	/* 0x26C transmit broadcast packet counter */
	uint32_t txpf;	/* 0x270 transmit pause control frame counter */
	uint32_t tdfr;	/* 0x274 transmit deferral packet counter */
	uint32_t tedf;	/* 0x278 transmit excessive deferral packet counter */
	uint32_t tscl;	/* 0x27C transmit single collision packet counter */
	uint32_t tmcl;	/* 0x280 transmit multiple collision packet counter */
	uint32_t tlcl;	/* 0x284 transmit late collision packet counter */
	uint32_t txcl;	/* 0x288 transmit excessive collision packet counter */
	uint32_t tncl;	/* 0x28C transmit total collision counter */
	uint32_t reserved0290[1];
	uint32_t tdrp;	/* 0x294 transmit drop frame counter */
	uint32_t tjbr;	/* 0x298 transmit jabber frame counter */
	uint32_t tfcs;	/* 0x29C transmit FCS error counter */
	uint32_t txcf;	/* 0x2A0 transmit control frame counter */
	uint32_t tovr;	/* 0x2A4 transmit oversize frame counter */
	uint32_t tund;	/* 0x2A8 transmit undersize frame counter */
	uint32_t tfrg;	/* 0x2AC transmit fragments frame counter */
	uint32_t car1;	/* 0x2B0 carry register one register* */
	uint32_t car2;	/* 0x2B4 carry register two register* */
	uint32_t cam1;	/* 0x2B8 carry register one mask register */
	uint32_t cam2;	/* 0x2BC carry register two mask register */
	uint32_t reserved02c0[848];
};

/**
 * struct dtsec_mib_grp_1_counters - MIB counter overflows
 *
 * @tr64:	Transmit and Receive 64 byte frame count.  Increment for each
 *		good or bad frame, of any type, transmitted or received, which
 *		is 64 bytes in length.
 * @tr127:	Transmit and Receive 65 to 127 byte frame count.  Increments for
 *		each good or bad frame of any type, transmitted or received,
 *		which is 65-127 bytes in length.
 * @tr255:	Transmit and Receive 128 to 255 byte frame count.  Increments
 *		for each good or bad frame, of any type, transmitted or
 *		received, which is 128-255 bytes in length.
 * @tr511:	Transmit and Receive 256 to 511 byte frame count.  Increments
 *		for each good or bad frame, of any type, transmitted or
 *		received, which is 256-511 bytes in length.
 * @tr1k:	Transmit and Receive 512 to 1023 byte frame count.  Increments
 *		for each good or bad frame, of any type, transmitted or
 *		received, which is 512-1023 bytes in length.
 * @trmax:	Transmit and Receive 1024 to 1518 byte frame count.  Increments
 *		for each good or bad frame, of any type, transmitted or
 *		received, which is 1024-1518 bytes in length.
 * @rfrg:	Receive fragments count.  Increments for each received frame
 *		which is less than 64 bytes in length and contains an invalid
 *		FCS.  This includes integral and non-integral lengths.
 * @rjbr:	Receive jabber count.  Increments for received frames which
 *		exceed 1518 (non VLAN) or 1522 (VLAN) bytes and contain an
 *		invalid FCS.  This includes alignment errors.
 * @rdrp:	Receive dropped packets count.  Increments for received frames
 *		which are streamed to system but are later dropped due to lack
 *		of system resources.  Does not increment for frames rejected due
 *		to address filtering.
 * @raln:	Receive alignment error count.  Increments for each received
 *		frame from 64 to 1518 (non VLAN) or 1522 (VLAN) which contains
 *		an invalid FCS and is not an integral number of bytes.
 * @rund:	Receive undersize packet count.  Increments each time a frame is
 *		received which is less than 64 bytes in length and contains a
 *		valid FCS and is otherwise well formed.  This count does not
 *		include range length errors.
 * @rovr:	Receive oversize packet count.  Increments each time a frame is
 *		received which exceeded 1518 (non VLAN) or 1522 (VLAN) and
 *		contains a valid FCS and is otherwise well formed.
 * @rbyt:	Receive byte count.  Increments by the byte count of frames
 *		received, including those in bad packets, excluding preamble and
 *		SFD but including FCS bytes.
 * @rpkt:	Receive packet count.  Increments for each received frame
 *		(including bad packets, all unicast, broadcast, and multicast
 *		packets).
 * @rmca:	Receive multicast packet count.  Increments for each multicast
 *		frame with valid CRC and of lengths 64 to 1518 (non VLAN) or
 *		1522 (VLAN), excluding broadcast frames. This count does not
 *		include range/length errors.
 * @rbca:	Receive broadcast packet count.  Increments for each broadcast
 *		frame with valid CRC and of lengths 64 to 1518 (non VLAN) or
 *		1522 (VLAN), excluding multicast frames. Does not include
 *		range/length errors.
 * @tdrp:	Transmit drop frame count.  Increments each time a memory error
 *		or an underrun has occurred.
 * @tncl:	Transmit total collision counter. Increments by the number of
 *		collisions experienced during the transmission of a frame. Does
 *		not increment for aborted frames.
 *
 * The structure contains a group of dTSEC HW specific counters relating to the
 * standard RMON MIB Group 1 (or Ethernet statistics) counters.  This structure
 * is counting only the carry events of the corresponding HW counters.
 *
 * tr64 to trmax notes: Frame sizes specified are considered excluding preamble
 * and SFD but including FCS bytes.
 */
struct dtsec_mib_grp_1_counters {
	uint64_t	rdrp;
	uint64_t	tdrp;
	uint64_t	rbyt;
	uint64_t	rpkt;
	uint64_t	rbca;
	uint64_t	rmca;
	uint64_t	raln;
	uint64_t	rund;
	uint64_t	rovr;
	uint64_t	rfrg;
	uint64_t	rjbr;
	uint64_t	tncl;
	uint64_t	tr64;
	uint64_t	tr127;
	uint64_t	tr255;
	uint64_t	tr511;
	uint64_t	tr1k;
	uint64_t	trmax;
};

enum dtsec_stat_counters {
	E_DTSEC_STAT_TR64,
	E_DTSEC_STAT_TR127,
	E_DTSEC_STAT_TR255,
	E_DTSEC_STAT_TR511,
	E_DTSEC_STAT_TR1K,
	E_DTSEC_STAT_TRMAX,
	E_DTSEC_STAT_TRMGV,
	E_DTSEC_STAT_RBYT,
	E_DTSEC_STAT_RPKT,
	E_DTSEC_STAT_RMCA,
	E_DTSEC_STAT_RBCA,
	E_DTSEC_STAT_RXPF,
	E_DTSEC_STAT_RALN,
	E_DTSEC_STAT_RFLR,
	E_DTSEC_STAT_RCDE,
	E_DTSEC_STAT_RCSE,
	E_DTSEC_STAT_RUND,
	E_DTSEC_STAT_ROVR,
	E_DTSEC_STAT_RFRG,
	E_DTSEC_STAT_RJBR,
	E_DTSEC_STAT_RDRP,
	E_DTSEC_STAT_TFCS,
	E_DTSEC_STAT_TBYT,
	E_DTSEC_STAT_TPKT,
	E_DTSEC_STAT_TMCA,
	E_DTSEC_STAT_TBCA,
	E_DTSEC_STAT_TXPF,
	E_DTSEC_STAT_TNCL,
	E_DTSEC_STAT_TDRP
};

enum dtsec_stat_level {
	/* No statistics */
	E_MAC_STAT_NONE = 0,
	/* Only RMON MIB group 1 (ether stats). Optimized for performance */
	E_MAC_STAT_MIB_GRP1,
	/* Only error counters are available. Optimized for performance */
	E_MAC_STAT_PARTIAL,
	/* All counters available. Not optimized for performance */
	E_MAC_STAT_FULL
};


/**
 * struct dtsec_cfg - dTSEC configuration
 *
 * @halfdup_on:		Transmit half-duplex flow control, under software
 *			control for 10/100-Mbps half-duplex media. If set,
 *			back pressure is applied to media by raising carrier.
 * @halfdup_retransmit:	Number of retransmission attempts following a collision.
 *			If this is exceeded dTSEC aborts transmission due to
 *			excessive collisions. The standard specifies the
 *			attempt limit to be 15.
 * @halfdup_coll_window:The number of bytes of the frame during which
 *			collisions may occur. The default value of 55
 *			corresponds to the frame byte at the end of the
 *			standard 512-bit slot time window. If collisions are
 *			detected after this byte, the late collision event is
 *			asserted and transmission of current frame is aborted.
 * @rx_drop_bcast:	Discard broadcast frames.  If set, all broadcast frames
 *			will be discarded by dTSEC.
 * @rx_short_frm:	Accept short frames.  If set, dTSEC will accept frames
 *			of length 14..63 bytes.
 * @rx_len_check:	Length check for received frames.  If set, the MAC
 *			checks the frame's length field on receive to ensure it
 *			matches the actual data field length. This only works
 *			for received frames with length field less than 1500.
 *			No check is performed for larger frames.
 * @tx_pad_crc:		Pad and append CRC.  If set, the MAC pads all
 *			transmitted short frames and appends a CRC to every
 *			frame regardless of padding requirement.
 * @tx_crc:		Transmission CRC enable.  If set, the MAC appends a CRC
 *			to all frames.  If frames presented to the MAC have a
 *			valid length and contain a valid CRC, @tx_crc should be
 *			reset.
 *			This field is ignored if @tx_pad_crc is set.
 * @rx_ctrl_acc:	Control frame accept.  If set, this overrides 802.3
 *			standard control frame behavior, and all Ethernet frames
 *			that have an ethertype of 0x8808 are treated as normal
 *			Ethernet frames and passed up to the packet interface on
 *			a DA match.  Received pause control frames are passed to
 *			the packet interface only if Rx flow control is also
 *			disabled.  See fman_dtsec_handle_rx_pause() function.
 * @tx_pause_time:	Transmit pause time value.  This pause value is used as
 *			part of the pause frame to be sent when a transmit pause
 *			frame is initiated.  If set to 0 this disables
 *			transmission of pause frames.
 * @rx_preamble:	Receive preamble enable.  If set, the MAC recovers the
 *			received Ethernet 7-byte preamble and passes it to the
 *			packet interface at the start of each received frame.
 *			This field should be reset for internal MAC loop-back
 *			mode.
 * @tx_preamble:	User defined preamble enable for transmitted frames.
 *			If set, a user-defined preamble must passed to the MAC
 *			and it is transmitted instead of the standard preamble.
 * @preamble_len:	Length, in bytes, of the preamble field preceding each
 *			Ethernet start-of-frame delimiter byte.  The default
 *			value of 0x7 should be used in order to guarantee
 *			reliable operation with IEEE 802.3 compliant hardware.
 * @rx_prepend:		Packet alignment padding length.  The specified number
 *			of bytes (1-31) of zero padding are inserted before the
 *			start of each received frame.  For Ethernet, where
 *			optional preamble extraction is enabled, the padding
 *			appears before the preamble, otherwise the padding
 *			precedes the layer 2 header.
 *
 * This structure contains basic dTSEC configuration and must be passed to
 * fman_dtsec_init() function.  A default set of configuration values can be
 * obtained by calling fman_dtsec_defconfig().
 */
struct dtsec_cfg {
	bool		halfdup_on;
	bool		halfdup_alt_backoff_en;
	bool		halfdup_excess_defer;
	bool		halfdup_no_backoff;
	bool		halfdup_bp_no_backoff;
	uint8_t		halfdup_alt_backoff_val;
	uint16_t	halfdup_retransmit;
	uint16_t	halfdup_coll_window;
	bool		rx_drop_bcast;
	bool		rx_short_frm;
	bool		rx_len_check;
	bool		tx_pad_crc;
	bool		tx_crc;
	bool		rx_ctrl_acc;
	unsigned short	tx_pause_time;
	unsigned short	tbipa;
	bool		ptp_tsu_en;
	bool		ptp_exception_en;
	bool		rx_preamble;
	bool		tx_preamble;
	unsigned char	preamble_len;
	unsigned char	rx_prepend;
	bool		loopback;
	bool		rx_time_stamp_en;
	bool		tx_time_stamp_en;
	bool		rx_flow;
	bool		tx_flow;
	bool		rx_group_hash_exd;
	bool		rx_promisc;
	uint8_t		tbi_phy_addr;
	uint16_t	tx_pause_time_extd;
	uint16_t	maximum_frame;
	uint32_t	non_back_to_back_ipg1;
	uint32_t	non_back_to_back_ipg2;
	uint32_t	min_ifg_enforcement;
	uint32_t	back_to_back_ipg;
	bool		wake_on_lan;
};


/**
 * fman_dtsec_defconfig() - Get default dTSEC configuration
 * @cfg:	pointer to configuration structure.
 *
 * Call this function to obtain a default set of configuration values for
 * initializing dTSEC.  The user can overwrite any of the values before calling
 * fman_dtsec_init(), if specific configuration needs to be applied.
 */
void fman_dtsec_defconfig(struct dtsec_cfg *cfg);

/**
 * fman_dtsec_init() - Init dTSEC hardware block
 * @regs:		Pointer to dTSEC register block
 * @cfg:		dTSEC configuration data
 * @iface_mode:		dTSEC interface mode, the type of MAC - PHY interface.
 * @iface_speed:	1G or 10G
 * @macaddr:		MAC station address to be assigned to the device
 * @fm_rev_maj:		major rev number
 * @fm_rev_min:		minor rev number
 * @exceptions_mask:	initial exceptions mask
 *
 * This function initializes dTSEC and applies basic configuration.
 *
 * dTSEC initialization sequence:
 * Before enabling Rx/Tx call dtsec_set_address() to set MAC address,
 * fman_dtsec_adjust_link() to configure interface speed and duplex and finally
 * dtsec_enable_tx()/dtsec_enable_rx() to start transmission and reception.
 *
 * Returns: 0 if successful, an error code otherwise.
 */
int fman_dtsec_init(struct dtsec_regs *regs, struct dtsec_cfg *cfg,
	enum enet_interface iface_mode,
	enum enet_speed iface_speed,
	uint8_t *macaddr, uint8_t fm_rev_maj,
	uint8_t fm_rev_min,
	uint32_t exception_mask);

/**
 * fman_dtsec_enable() - Enable dTSEC Tx and Tx
 * @regs:	Pointer to dTSEC register block
 * @apply_rx:	enable rx side
 * @apply_tx:	enable tx side
 *
 * This function resets Tx and Rx graceful stop bit and enables dTSEC Tx and Rx.
 */
void fman_dtsec_enable(struct dtsec_regs *regs, bool apply_rx, bool apply_tx);

/**
 * fman_dtsec_disable() - Disable dTSEC Tx and Rx
 * @regs:	Pointer to dTSEC register block
 * @apply_rx:	disable rx side
 * @apply_tx:	disable tx side
 *
 * This function disables Tx and Rx in dTSEC.
 */
void fman_dtsec_disable(struct dtsec_regs *regs, bool apply_rx, bool apply_tx);

/**
 * fman_dtsec_get_revision() - Get dTSEC hardware revision
 * @regs:   Pointer to dTSEC register block
 *
 * Returns dtsec_id content
 *
 * Call this function to obtain the dTSEC hardware version.
 */
uint32_t fman_dtsec_get_revision(struct dtsec_regs *regs);

/**
 * fman_dtsec_set_mac_address() - Set MAC station address
 * @regs:   Pointer to dTSEC register block
 * @macaddr:    MAC address array
 *
 * This function sets MAC station address.  To enable unicast reception call
 * this after fman_dtsec_init().  While promiscuous mode is disabled dTSEC will
 * match the destination address of received unicast frames against this
 * address.
 */
void fman_dtsec_set_mac_address(struct dtsec_regs *regs, uint8_t *macaddr);

/**
 * fman_dtsec_get_mac_address() - Query MAC station address
 * @regs:   Pointer to dTSEC register block
 * @macaddr:    MAC address array
 */
void fman_dtsec_get_mac_address(struct dtsec_regs *regs, uint8_t *macaddr);

/**
 * fman_dtsec_set_uc_promisc() - Sets unicast promiscuous mode
 * @regs:	Pointer to dTSEC register block
 * @enable:	Enable unicast promiscuous mode
 *
 * Use this function to enable/disable dTSEC L2 address filtering.  If the
 * address filtering is disabled all unicast packets are accepted.
 * To set dTSEC in promiscuous mode call both fman_dtsec_set_uc_promisc() and
 * fman_dtsec_set_mc_promisc() to disable filtering for both unicast and
 * multicast addresses.
 */
void fman_dtsec_set_uc_promisc(struct dtsec_regs *regs, bool enable);

/**
 * fman_dtsec_set_wol() - Enable/Disable wake on lan
 *                        (magic packet support)
 * @regs:   Pointer to dTSEC register block
 * @en:     Enable Wake On Lan support in dTSEC
 *
 */
void fman_dtsec_set_wol(struct dtsec_regs *regs, bool en);

/**
 * fman_dtsec_adjust_link() - Adjust dTSEC speed/duplex settings
 * @regs:	Pointer to dTSEC register block
 * @iface_mode: dTSEC interface mode
 * @speed:	Link speed
 * @full_dx:	True for full-duplex, false for half-duplex.
 *
 * This function configures the MAC to function and the desired rates.  Use it
 * to configure dTSEC after fman_dtsec_init() and whenever the link speed
 * changes (for instance following PHY auto-negociation).
 *
 * Returns: 0 if successful, an error code otherwise.
 */
int fman_dtsec_adjust_link(struct dtsec_regs *regs,
	enum enet_interface iface_mode,
	enum enet_speed speed, bool full_dx);

/**
 * fman_dtsec_set_tbi_phy_addr() - Updates TBI address field
 * @regs:	Pointer to dTSEC register block
 * @address:	Valid PHY address in the range of 1 to 31. 0 is reserved.
 *
 * In SGMII mode, the dTSEC's TBIPA field must contain a valid TBI PHY address
 * so that the associated TBI PHY (i.e. the link) may be initialized.
 *
 * Returns: 0 if successful, an error code otherwise.
 */
int fman_dtsec_set_tbi_phy_addr(struct dtsec_regs *regs,
	uint8_t addr);

/**
 * fman_dtsec_set_max_frame_len() - Set max frame length
 * @regs:	Pointer to dTSEC register block
 * @length:	Max frame length.
 *
 * Sets maximum frame length for received and transmitted frames.  Frames that
 * exceeds this length are truncated.
 */
void fman_dtsec_set_max_frame_len(struct dtsec_regs *regs, uint16_t length);

/**
 * fman_dtsec_get_max_frame_len() - Query max frame length
 * @regs:	Pointer to dTSEC register block
 *
 * Returns: the current value of the maximum frame length.
 */
uint16_t fman_dtsec_get_max_frame_len(struct dtsec_regs *regs);

/**
 * fman_dtsec_handle_rx_pause() - Configure pause frame handling
 * @regs:	Pointer to dTSEC register block
 * @en:		Enable pause frame handling in dTSEC
 *
 * If enabled, dTSEC will handle pause frames internally.  This must be disabled
 * if dTSEC is set in half-duplex mode.
 * If pause frame handling is disabled and &dtsec_cfg.rx_ctrl_acc is set, pause
 * frames will be transferred to the packet interface just like regular Ethernet
 * frames.
 */
void fman_dtsec_handle_rx_pause(struct dtsec_regs *regs, bool en);

/**
 * fman_dtsec_set_tx_pause_frames() - Configure Tx pause time
 * @regs:	Pointer to dTSEC register block
 * @time:	Time value included in pause frames
 *
 * Call this function to set the time value used in transmitted pause frames.
 * If time is 0, transmission of pause frames is disabled
 */
void fman_dtsec_set_tx_pause_frames(struct dtsec_regs *regs, uint16_t time);

/**
 * fman_dtsec_ack_event() - Acknowledge handled events
 * @regs:	Pointer to dTSEC register block
 * @ev_mask:	Events to acknowledge
 *
 * After handling events signaled by dTSEC in either polling or interrupt mode,
 * call this function to reset the associated status bits in dTSEC event
 * register.
 */
void fman_dtsec_ack_event(struct dtsec_regs *regs, uint32_t ev_mask);

/**
 * fman_dtsec_get_event() - Returns currently asserted events
 * @regs:	Pointer to dTSEC register block
 * @ev_mask:	Mask of relevant events
 *
 * Call this function to obtain a bit-mask of events that are currently asserted
 * in dTSEC, taken from IEVENT register.
 *
 * Returns: a bit-mask of events asserted in dTSEC.
 */
uint32_t fman_dtsec_get_event(struct dtsec_regs *regs, uint32_t ev_mask);

/**
 * fman_dtsec_get_interrupt_mask() - Returns a bit-mask of enabled interrupts
 * @regs:   Pointer to dTSEC register block
 *
 * Call this function to obtain a bit-mask of enabled interrupts
 * in dTSEC, taken from IMASK register.
 *
 * Returns: a bit-mask of enabled interrupts in dTSEC.
 */
uint32_t fman_dtsec_get_interrupt_mask(struct dtsec_regs *regs);

void fman_dtsec_clear_addr_in_paddr(struct dtsec_regs *regs,
	uint8_t paddr_num);

void fman_dtsec_add_addr_in_paddr(struct dtsec_regs *regs,
	uint64_t addr,
	uint8_t paddr_num);

void fman_dtsec_enable_tmr_interrupt (struct dtsec_regs *regs);

void fman_dtsec_disable_tmr_interrupt(struct dtsec_regs *regs);

/**
 * fman_dtsec_disable_interrupt() - Disables interrupts for the specified events
 * @regs:	Pointer to dTSEC register block
 * @ev_mask:	Mask of relevant events
 *
 * Call this function to disable interrupts in dTSEC for the specified events.
 * To enable interrupts use fman_dtsec_enable_interrupt().
 */
void fman_dtsec_disable_interrupt(struct dtsec_regs *regs, uint32_t ev_mask);

/**
 * fman_dtsec_enable_interrupt() - Enable interrupts for the specified events
 * @regs:	Pointer to dTSEC register block
 * @ev_mask:	Mask of relevant events
 *
 * Call this function to enable interrupts in dTSEC for the specified events.
 * To disable interrupts use fman_dtsec_disable_interrupt().
 */
void fman_dtsec_enable_interrupt(struct dtsec_regs *regs, uint32_t ev_mask);

/**
 * fman_dtsec_set_ts() - Enables dTSEC timestamps
 * @regs:	Pointer to dTSEC register block
 * @en:		true to enable timestamps, false to disable them
 *
 * Call this function to enable/disable dTSEC timestamps.  This affects both
 * Tx and Rx.
 */
void fman_dtsec_set_ts(struct dtsec_regs *regs, bool en);

/**
 * fman_dtsec_set_bucket() - Enables/disables a filter bucket
 * @regs:   Pointer to dTSEC register block
 * @bucket: Bucket index
 * @enable: true/false to enable/disable this bucket
 *
 * This function enables or disables the specified bucket.  Enabling a bucket
 * associated with an address configures dTSEC to accept received packets
 * with that destination address.
 * Multiple addresses may be associated with the same bucket.  Disabling a
 * bucket will affect all addresses associated with that bucket. A bucket that
 * is enabled requires further filtering and verification in the upper layers
 *
 */
void fman_dtsec_set_bucket(struct dtsec_regs *regs, int bucket, bool enable);

/**
 * dtsec_set_hash_table() - insert a crc code into thr filter table
 * @regs:	Pointer to dTSEC register block
 * @crc:	crc to insert
 * @mcast:	true is this is a multicast address
 * @ghtx:	true if we are in ghtx mode
 *
 * This function inserts a crc code into the filter table.
 */
void fman_dtsec_set_hash_table(struct dtsec_regs *regs, uint32_t crc,
	bool mcast, bool ghtx);

/**
 * fman_dtsec_reset_filter_table() - Resets the address filtering table
 * @regs:	Pointer to dTSEC register block
 * @mcast:	Reset multicast entries
 * @ucast:	Reset unicast entries
 *
 * Resets all entries in L2 address filter table.  After calling this function
 * all buckets enabled using fman_dtsec_set_bucket() will be disabled.
 * If dtsec_init_filter_table() was called with @unicast_hash set to false,
 * @ucast argument is ignored.
 * This does not affect the primary nor the 15 additional addresses configured
 * using dtsec_set_address() or dtsec_set_match_address().
 */
void fman_dtsec_reset_filter_table(struct dtsec_regs *regs, bool mcast,
	bool ucast);

/**
 * fman_dtsec_set_mc_promisc() - Set multicast promiscuous mode
 * @regs:	Pointer to dTSEC register block
 * @enable:	Enable multicast promiscuous mode
 *
 * Call this to enable/disable L2 address filtering for multicast packets.
 */
void fman_dtsec_set_mc_promisc(struct dtsec_regs *regs, bool enable);

/* statistics APIs */

/**
 * fman_dtsec_set_stat_level() - Enable a group of MIB statistics counters
 * @regs:	Pointer to dTSEC register block
 * @level:	Specifies a certain group of dTSEC MIB HW counters or _all_,
 *		to specify all the existing counters.
 *		If set to _none_, it disables all the counters.
 *
 * Enables the MIB statistics hw counters and sets up the carry interrupt
 * masks for the counters corresponding to the @level input parameter.
 *
 * Returns: error if invalid @level value given.
 */
int fman_dtsec_set_stat_level(struct dtsec_regs *regs,
	enum dtsec_stat_level level);

/**
 * fman_dtsec_reset_stat() - Completely resets all dTSEC HW counters
 * @regs:	Pointer to dTSEC register block
 */
void fman_dtsec_reset_stat(struct dtsec_regs *regs);

/**
 * fman_dtsec_get_clear_carry_regs() - Read and clear carry bits (CAR1-2 registers)
 * @regs:	Pointer to dTSEC register block
 * @car1:	car1 register value
 * @car2:	car2 register value
 *
 * When set, the carry bits signal that an overflow occurred on the
 * corresponding counters.
 * Note that the carry bits (CAR1-2 registers) will assert the
 * %DTSEC_IEVENT_MSRO interrupt if unmasked (via CAM1-2 regs).
 *
 * Returns: true if overflow occurred, otherwise - false
 */
bool fman_dtsec_get_clear_carry_regs(struct dtsec_regs *regs,
	uint32_t *car1, uint32_t *car2);

uint32_t fman_dtsec_check_and_clear_tmr_event(struct dtsec_regs *regs);

uint32_t fman_dtsec_get_stat_counter(struct dtsec_regs *regs,
	enum dtsec_stat_counters reg_name);

void fman_dtsec_start_tx(struct dtsec_regs *regs);
void fman_dtsec_start_rx(struct dtsec_regs *regs);
void fman_dtsec_stop_tx(struct dtsec_regs *regs);
void fman_dtsec_stop_rx(struct dtsec_regs *regs);
uint32_t fman_dtsec_get_rctrl(struct dtsec_regs *regs);


#endif /* __FSL_FMAN_DTSEC_H */
