/*
 * Copyright (C) 2006 Freescale Semicondutor, Inc. All rights reserved.
 *
 * Authors: 	Shlomi Gridish <gridish@freescale.com>
 * 		Li Yang <leoli@freescale.com>
 *
 * Description:
 * Internal header file for UCC SLOW unit routines.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef __UCC_SLOW_H__
#define __UCC_SLOW_H__

#include <linux/kernel.h>

#include <asm/immap_qe.h>
#include <asm/qe.h>

#include "ucc.h"

/* transmit BD's status */
#define T_R	0x80000000	/* ready bit */
#define T_PAD	0x40000000	/* add pads to short frames */
#define T_W	0x20000000	/* wrap bit */
#define T_I	0x10000000	/* interrupt on completion */
#define T_L	0x08000000	/* last */

#define T_A	0x04000000	/* Address - the data transmitted as address
				   chars */
#define T_TC	0x04000000	/* transmit CRC */
#define T_CM	0x02000000	/* continuous mode */
#define T_DEF	0x02000000	/* collision on previous attempt to transmit */
#define T_P	0x01000000	/* Preamble - send Preamble sequence before
				   data */
#define T_HB	0x01000000	/* heartbeat */
#define T_NS	0x00800000	/* No Stop */
#define T_LC	0x00800000	/* late collision */
#define T_RL	0x00400000	/* retransmission limit */
#define T_UN	0x00020000	/* underrun */
#define T_CT	0x00010000	/* CTS lost */
#define T_CSL	0x00010000	/* carrier sense lost */
#define T_RC	0x003c0000	/* retry count */

/* Receive BD's status */
#define R_E	0x80000000	/* buffer empty */
#define R_W	0x20000000	/* wrap bit */
#define R_I	0x10000000	/* interrupt on reception */
#define R_L	0x08000000	/* last */
#define R_C	0x08000000	/* the last byte in this buffer is a cntl
				   char */
#define R_F	0x04000000	/* first */
#define R_A	0x04000000	/* the first byte in this buffer is address
				   byte */
#define R_CM	0x02000000	/* continuous mode */
#define R_ID	0x01000000	/* buffer close on reception of idles */
#define R_M	0x01000000	/* Frame received because of promiscuous
				   mode */
#define R_AM	0x00800000	/* Address match */
#define R_DE	0x00800000	/* Address match */
#define R_LG	0x00200000	/* Break received */
#define R_BR	0x00200000	/* Frame length violation */
#define R_NO	0x00100000	/* Rx Non Octet Aligned Packet */
#define R_FR	0x00100000	/* Framing Error (no stop bit) character
				   received */
#define R_PR	0x00080000	/* Parity Error character received */
#define R_AB	0x00080000	/* Frame Aborted */
#define R_SH	0x00080000	/* frame is too short */
#define R_CR	0x00040000	/* CRC Error */
#define R_OV	0x00020000	/* Overrun */
#define R_CD	0x00010000	/* CD lost */
#define R_CL	0x00010000	/* this frame is closed because of a
				   collision */

/* Rx Data buffer must be 4 bytes aligned in most cases.*/
#define UCC_SLOW_RX_ALIGN		4
#define UCC_SLOW_MRBLR_ALIGNMENT	4
#define UCC_SLOW_PRAM_SIZE		0x100
#define ALIGNMENT_OF_UCC_SLOW_PRAM	64

/* UCC Slow Channel Protocol Mode */
enum ucc_slow_channel_protocol_mode {
	UCC_SLOW_CHANNEL_PROTOCOL_MODE_QMC = 0x00000002,
	UCC_SLOW_CHANNEL_PROTOCOL_MODE_UART = 0x00000004,
	UCC_SLOW_CHANNEL_PROTOCOL_MODE_BISYNC = 0x00000008,
};

/* UCC Slow Transparent Transmit CRC (TCRC) */
enum ucc_slow_transparent_tcrc {
	/* 16-bit CCITT CRC (HDLC).  (X16 + X12 + X5 + 1) */
	UCC_SLOW_TRANSPARENT_TCRC_CCITT_CRC16 = 0x00000000,
	/* CRC16 (BISYNC).  (X16 + X15 + X2 + 1) */
	UCC_SLOW_TRANSPARENT_TCRC_CRC16 = 0x00004000,
	/* 32-bit CCITT CRC (Ethernet and HDLC) */
	UCC_SLOW_TRANSPARENT_TCRC_CCITT_CRC32 = 0x00008000,
};

/* UCC Slow oversampling rate for transmitter (TDCR) */
enum ucc_slow_tx_oversampling_rate {
	/* 1x clock mode */
	UCC_SLOW_OVERSAMPLING_RATE_TX_TDCR_1 = 0x00000000,
	/* 8x clock mode */
	UCC_SLOW_OVERSAMPLING_RATE_TX_TDCR_8 = 0x00010000,
	/* 16x clock mode */
	UCC_SLOW_OVERSAMPLING_RATE_TX_TDCR_16 = 0x00020000,
	/* 32x clock mode */
	UCC_SLOW_OVERSAMPLING_RATE_TX_TDCR_32 = 0x00030000,
};

/* UCC Slow Oversampling rate for receiver (RDCR)
*/
enum ucc_slow_rx_oversampling_rate {
	/* 1x clock mode */
	UCC_SLOW_OVERSAMPLING_RATE_RX_RDCR_1 = 0x00000000,
	/* 8x clock mode */
	UCC_SLOW_OVERSAMPLING_RATE_RX_RDCR_8 = 0x00004000,
	/* 16x clock mode */
	UCC_SLOW_OVERSAMPLING_RATE_RX_RDCR_16 = 0x00008000,
	/* 32x clock mode */
	UCC_SLOW_OVERSAMPLING_RATE_RX_RDCR_32 = 0x0000c000,
};

/* UCC Slow Transmitter encoding method (TENC)
*/
enum ucc_slow_tx_encoding_method {
	UCC_SLOW_TRANSMITTER_ENCODING_METHOD_TENC_NRZ = 0x00000000,
	UCC_SLOW_TRANSMITTER_ENCODING_METHOD_TENC_NRZI = 0x00000100
};

/* UCC Slow Receiver decoding method (RENC)
*/
enum ucc_slow_rx_decoding_method {
	UCC_SLOW_RECEIVER_DECODING_METHOD_RENC_NRZ = 0x00000000,
	UCC_SLOW_RECEIVER_DECODING_METHOD_RENC_NRZI = 0x00000800
};

/* UCC Slow Diagnostic mode (DIAG)
*/
enum ucc_slow_diag_mode {
	UCC_SLOW_DIAG_MODE_NORMAL = 0x00000000,
	UCC_SLOW_DIAG_MODE_LOOPBACK = 0x00000040,
	UCC_SLOW_DIAG_MODE_ECHO = 0x00000080,
	UCC_SLOW_DIAG_MODE_LOOPBACK_ECHO = 0x000000c0
};

struct ucc_slow_info {
	int ucc_num;
	enum qe_clock rx_clock;
	enum qe_clock tx_clock;
	u32 regs;
	int irq;
	u16 uccm_mask;
	int data_mem_part;
	int init_tx;
	int init_rx;
	u32 tx_bd_ring_len;
	u32 rx_bd_ring_len;
	int rx_interrupts;
	int brkpt_support;
	int grant_support;
	int tsa;
	int cdp;
	int cds;
	int ctsp;
	int ctss;
	int rinv;
	int tinv;
	int rtsm;
	int rfw;
	int tci;
	int tend;
	int tfl;
	int txsy;
	u16 max_rx_buf_length;
	enum ucc_slow_transparent_tcrc tcrc;
	enum ucc_slow_channel_protocol_mode mode;
	enum ucc_slow_diag_mode diag;
	enum ucc_slow_tx_oversampling_rate tdcr;
	enum ucc_slow_rx_oversampling_rate rdcr;
	enum ucc_slow_tx_encoding_method tenc;
	enum ucc_slow_rx_decoding_method renc;
};

struct ucc_slow_private {
	struct ucc_slow_info *us_info;
	struct ucc_slow *us_regs;	/* a pointer to memory map of UCC regs */
	struct ucc_slow_pram *us_pram;	/* a pointer to the parameter RAM */
	u32 us_pram_offset;
	int enabled_tx;		/* Whether channel is enabled for Tx (ENT) */
	int enabled_rx;		/* Whether channel is enabled for Rx (ENR) */
	int stopped_tx;		/* Whether channel has been stopped for Tx
				   (STOP_TX, etc.) */
	int stopped_rx;		/* Whether channel has been stopped for Rx */
	struct list_head confQ;	/* frames passed to chip waiting for tx */
	u32 first_tx_bd_mask;	/* mask is used in Tx routine to save status
				   and length for first BD in a frame */
	u32 tx_base_offset;	/* first BD in Tx BD table offset (In MURAM) */
	u32 rx_base_offset;	/* first BD in Rx BD table offset (In MURAM) */
	struct qe_bd *confBd;	/* next BD for confirm after Tx */
	struct qe_bd *tx_bd;	/* next BD for new Tx request */
	struct qe_bd *rx_bd;	/* next BD to collect after Rx */
	void *p_rx_frame;	/* accumulating receive frame */
	u16 *p_ucce;		/* a pointer to the event register in memory.
				 */
	u16 *p_uccm;		/* a pointer to the mask register in memory */
	u16 saved_uccm;		/* a saved mask for the RX Interrupt bits */
#ifdef STATISTICS
	u32 tx_frames;		/* Transmitted frames counters */
	u32 rx_frames;		/* Received frames counters (only frames
				   passed to application) */
	u32 rx_discarded;	/* Discarded frames counters (frames that
				   were discarded by the driver due to
				   errors) */
#endif				/* STATISTICS */
};

/* ucc_slow_init
 * Initializes Slow UCC according to provided parameters.
 *
 * us_info  - (In) pointer to the slow UCC info structure.
 * uccs_ret - (Out) pointer to the slow UCC structure.
 */
int ucc_slow_init(struct ucc_slow_info * us_info, struct ucc_slow_private ** uccs_ret);

/* ucc_slow_free
 * Frees all resources for slow UCC.
 *
 * uccs - (In) pointer to the slow UCC structure.
 */
void ucc_slow_free(struct ucc_slow_private * uccs);

/* ucc_slow_enable
 * Enables a fast UCC port.
 * This routine enables Tx and/or Rx through the General UCC Mode Register.
 *
 * uccs - (In) pointer to the slow UCC structure.
 * mode - (In) TX, RX, or both.
 */
void ucc_slow_enable(struct ucc_slow_private * uccs, enum comm_dir mode);

/* ucc_slow_disable
 * Disables a fast UCC port.
 * This routine disables Tx and/or Rx through the General UCC Mode Register.
 *
 * uccs - (In) pointer to the slow UCC structure.
 * mode - (In) TX, RX, or both.
 */
void ucc_slow_disable(struct ucc_slow_private * uccs, enum comm_dir mode);

/* ucc_slow_poll_transmitter_now
 * Immediately forces a poll of the transmitter for data to be sent.
 * Typically, the hardware performs a periodic poll for data that the
 * transmit routine has set up to be transmitted. In cases where
 * this polling cycle is not soon enough, this optional routine can
 * be invoked to force a poll right away, instead. Proper use for
 * each transmission for which this functionality is desired is to
 * call the transmit routine and then this routine right after.
 *
 * uccs - (In) pointer to the slow UCC structure.
 */
void ucc_slow_poll_transmitter_now(struct ucc_slow_private * uccs);

/* ucc_slow_graceful_stop_tx
 * Smoothly stops transmission on a specified slow UCC.
 *
 * uccs - (In) pointer to the slow UCC structure.
 */
void ucc_slow_graceful_stop_tx(struct ucc_slow_private * uccs);

/* ucc_slow_stop_tx
 * Stops transmission on a specified slow UCC.
 *
 * uccs - (In) pointer to the slow UCC structure.
 */
void ucc_slow_stop_tx(struct ucc_slow_private * uccs);

/* ucc_slow_restart_x
 * Restarts transmitting on a specified slow UCC.
 *
 * uccs - (In) pointer to the slow UCC structure.
 */
void ucc_slow_restart_x(struct ucc_slow_private * uccs);

u32 ucc_slow_get_qe_cr_subblock(int uccs_num);

#endif				/* __UCC_SLOW_H__ */
