/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2008 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_ATH_DESC_H
#define _DEV_ATH_DESC_H

/*
 * Transmit descriptor status.  This structure is filled
 * in only after the tx descriptor process method finds a
 * ``done'' descriptor; at which point it returns something
 * other than HAL_EINPROGRESS.
 *
 * Note that ts_antenna may not be valid for all h/w.  It
 * should be used only if non-zero.
 */
struct ath_tx_status {
	uint16_t	ts_seqnum;	/* h/w assigned sequence number */
	uint16_t	ts_pad1[1];
	uint32_t	ts_tstamp;	/* h/w assigned timestamp */
	uint8_t		ts_status;	/* frame status, 0 => xmit ok */
	uint8_t		ts_rate;	/* h/w transmit rate index */
	int8_t		ts_rssi;	/* tx ack RSSI */
	uint8_t		ts_shortretry;	/* # short retries */
	uint8_t		ts_longretry;	/* # long retries */
	uint8_t		ts_virtcol;	/* virtual collision count */
	uint8_t		ts_antenna;	/* antenna information */
	uint8_t		ts_finaltsi;	/* final transmit series index */
					/* 802.11n status */
	uint8_t		ts_flags;	/* misc flags */
	uint8_t		ts_queue_id;	/* AR9300: TX queue id */
	uint8_t		ts_desc_id;	/* AR9300: TX descriptor id */
	uint8_t		ts_tid;		/* TID */
/* #define ts_rssi ts_rssi_combined */
	uint32_t	ts_ba_low;	/* blockack bitmap low */
	uint32_t	ts_ba_high;	/* blockack bitmap high */
	uint32_t	ts_evm0;	/* evm bytes */
	uint32_t	ts_evm1;
	uint32_t	ts_evm2;
	int8_t		ts_rssi_ctl[3];	/* tx ack RSSI [ctl, chain 0-2] */
	int8_t		ts_rssi_ext[3];	/* tx ack RSSI [ext, chain 0-2] */
	uint8_t		ts_pad[2];
};

/* bits found in ts_status */
#define	HAL_TXERR_XRETRY	0x01	/* excessive retries */
#define	HAL_TXERR_FILT		0x02	/* blocked by tx filtering */
#define	HAL_TXERR_FIFO		0x04	/* fifo underrun */
#define	HAL_TXERR_XTXOP		0x08	/* txop exceeded */
#define	HAL_TXERR_TIMER_EXPIRED	0x10	/* Tx timer expired */

/* bits found in ts_flags */
#define	HAL_TX_BA		0x01	/* Block Ack seen */
#define	HAL_TX_AGGR		0x02	/* Aggregate */ 
#define	HAL_TX_DESC_CFG_ERR	0x10	/* Error in 20/40 desc config */
#define	HAL_TX_DATA_UNDERRUN	0x20	/* Tx buffer underrun */
#define	HAL_TX_DELIM_UNDERRUN	0x40	/* Tx delimiter underrun */
#define	HAL_TX_FAST_TS		0x80	/* Tx locationing timestamp */

/*
 * Receive descriptor status.  This structure is filled
 * in only after the rx descriptor process method finds a
 * ``done'' descriptor; at which point it returns something
 * other than HAL_EINPROGRESS.
 *
 * If rx_status is zero, then the frame was received ok;
 * otherwise the error information is indicated and rs_phyerr
 * contains a phy error code if HAL_RXERR_PHY is set.  In general
 * the frame contents is undefined when an error occurred thought
 * for some errors (e.g. a decryption error), it may be meaningful.
 *
 * Note that the receive timestamp is expanded using the TSF to
 * at least 15 bits (regardless of what the h/w provides directly).
 * Newer hardware supports a full 32-bits; use HAL_CAP_32TSTAMP to
 * find out if the hardware is capable.
 *
 * rx_rssi is in units of dbm above the noise floor.  This value
 * is measured during the preamble and PLCP; i.e. with the initial
 * 4us of detection.  The noise floor is typically a consistent
 * -96dBm absolute power in a 20MHz channel.
 */
struct ath_rx_status {
	uint16_t	rs_datalen;	/* rx frame length */
	uint8_t		rs_status;	/* rx status, 0 => recv ok */
	uint8_t		rs_phyerr;	/* phy error code */
	int8_t		rs_rssi;	/* rx frame RSSI (combined for 11n) */
	uint8_t		rs_keyix;	/* key cache index */
	uint8_t		rs_rate;	/* h/w receive rate index */
	uint8_t		rs_more;	/* more descriptors follow */
	uint32_t	rs_tstamp;	/* h/w assigned timestamp */
	uint32_t	rs_antenna;	/* antenna information */
					/* 802.11n status */
	int8_t		rs_rssi_ctl[3];	/* rx frame RSSI [ctl, chain 0-2] */
	int8_t		rs_rssi_ext[3];	/* rx frame RSSI [ext, chain 0-2] */
	uint8_t		rs_isaggr;	/* is part of the aggregate */
	uint8_t		rs_moreaggr;	/* more frames in aggr to follow */
	uint16_t	rs_flags;	/* misc flags */
	uint8_t		rs_num_delims;	/* number of delims in aggr */
	uint8_t		rs_spare0;	/* padding */
	uint8_t		rs_ness;	/* number of extension spatial streams */
	uint8_t		rs_hw_upload_data_type;	/* hw upload format */
	uint16_t	rs_spare1;
	uint32_t	rs_evm0;	/* evm bytes */
	uint32_t	rs_evm1;
	uint32_t	rs_evm2;
	uint32_t	rs_evm3;	/* needed for ar9300 and later */
	uint32_t	rs_evm4;	/* needed for ar9300 and later */
};

/* bits found in rs_status */
#define	HAL_RXERR_CRC		0x01	/* CRC error on frame */
#define	HAL_RXERR_PHY		0x02	/* PHY error, rs_phyerr is valid */
#define	HAL_RXERR_FIFO		0x04	/* fifo overrun */
#define	HAL_RXERR_DECRYPT	0x08	/* non-Michael decrypt error */
#define	HAL_RXERR_MIC		0x10	/* Michael MIC decrypt error */
#define	HAL_RXERR_INCOMP	0x20	/* Rx Desc processing is incomplete */
#define	HAL_RXERR_KEYMISS	0x40	/* Key not found in keycache */

/* bits found in rs_flags */
#define	HAL_RX_MORE		0x0001	/* more descriptors follow */
#define	HAL_RX_MORE_AGGR	0x0002	/* more frames in aggr */
#define	HAL_RX_GI		0x0004	/* full gi */
#define	HAL_RX_2040		0x0008	/* 40 Mhz */
#define	HAL_RX_DELIM_CRC_PRE	0x0010	/* crc error in delimiter pre */
#define	HAL_RX_DELIM_CRC_POST	0x0020	/* crc error in delim after */
#define	HAL_RX_DECRYPT_BUSY	0x0040	/* decrypt was too slow */
#define	HAL_RX_HI_RX_CHAIN	0x0080	/* SM power save: hi Rx chain control */
#define	HAL_RX_IS_APSD		0x0100	/* Is ASPD trigger frame */
#define	HAL_RX_STBC		0x0200	/* Is an STBC frame */
#define	HAL_RX_LOC_INFO		0x0400	/* RX locationing information */

#define	HAL_RX_HW_UPLOAD_DATA	0x1000	/* This is a hardware data frame */
#define	HAL_RX_HW_SOUNDING	0x2000	/* Rx sounding frame (TxBF, positioning) */
#define	HAL_RX_UPLOAD_VALID	0x4000	/* This hardware data frame is valid */

/*
 * This is the format of RSSI[2] on the AR9285/AR9485.
 * It encodes the LNA configuration information.
 *
 * For boards with an external diversity antenna switch,
 * HAL_RX_LNA_EXTCFG encodes which configuration was
 * used (antenna 1 or antenna 2.)  This feeds into the
 * switch table and ensures that the given antenna was
 * connected to an LNA.
 */
#define	HAL_RX_LNA_LNACFG	0x80	/* 1 = main LNA config used, 0 = ALT */
#define	HAL_RX_LNA_EXTCFG	0x40	/* 0 = external diversity ant1, 1 = ant2 */
#define	HAL_RX_LNA_CFG_USED	0x30	/* 2 bits; LNA config used on RX */
#define	HAL_RX_LNA_CFG_USED_S		4
#define	HAL_RX_LNA_CFG_MAIN	0x0c	/* 2 bits; "Main" LNA config */
#define	HAL_RX_LNA_CFG_ALT	0x02	/* 2 bits; "Alt" LNA config */

/*
 * This is the format of RSSI_EXT[2] on the AR9285/AR9485.
 * It encodes the switch table configuration and fast diversity
 * value.
 */
#define	HAL_RX_LNA_FASTDIV	0x40	/* 1 = fast diversity measurement done */
#define	HAL_RX_LNA_SWITCH_0	0x30	/* 2 bits; sw_0[1:0] */
#define	HAL_RX_LNA_SWITCH_COM	0x0f	/* 4 bits, sw_com[3:0] */

enum {
	HAL_PHYERR_UNDERRUN		= 0,	/* Transmit underrun */
	HAL_PHYERR_TIMING		= 1,	/* Timing error */
	HAL_PHYERR_PARITY		= 2,	/* Illegal parity */
	HAL_PHYERR_RATE			= 3,	/* Illegal rate */
	HAL_PHYERR_LENGTH		= 4,	/* Illegal length */
	HAL_PHYERR_RADAR		= 5,	/* Radar detect */
	HAL_PHYERR_SERVICE		= 6,	/* Illegal service */
	HAL_PHYERR_TOR			= 7,	/* Transmit override receive */
	/* NB: these are specific to the 5212 and later */
	HAL_PHYERR_OFDM_TIMING		= 17,	/* */
	HAL_PHYERR_OFDM_SIGNAL_PARITY	= 18,	/* */
	HAL_PHYERR_OFDM_RATE_ILLEGAL	= 19,	/* */
	HAL_PHYERR_OFDM_LENGTH_ILLEGAL	= 20,	/* */
	HAL_PHYERR_OFDM_POWER_DROP	= 21,	/* */
	HAL_PHYERR_OFDM_SERVICE		= 22,	/* */
	HAL_PHYERR_OFDM_RESTART		= 23,	/* */
	HAL_PHYERR_FALSE_RADAR_EXT	= 24,	/* */
	HAL_PHYERR_CCK_TIMING		= 25,	/* */
	HAL_PHYERR_CCK_HEADER_CRC	= 26,	/* */
	HAL_PHYERR_CCK_RATE_ILLEGAL	= 27,	/* */
	HAL_PHYERR_CCK_SERVICE		= 30,	/* */
	HAL_PHYERR_CCK_RESTART		= 31,	/* */
	HAL_PHYERR_CCK_LENGTH_ILLEGAL	= 32,	/* */
	HAL_PHYERR_CCK_POWER_DROP	= 33,	/* */
	/* AR5416 and later */
	HAL_PHYERR_HT_CRC_ERROR		= 34,	/* */
	HAL_PHYERR_HT_LENGTH_ILLEGAL	= 35,	/* */
	HAL_PHYERR_HT_RATE_ILLEGAL	= 36,	/* */

	HAL_PHYERR_SPECTRAL		= 38,
};

/* value found in rs_keyix to mark invalid entries */
#define	HAL_RXKEYIX_INVALID	((uint8_t) -1)
/* value used to specify no encryption key for xmit */
#define	HAL_TXKEYIX_INVALID	((u_int) -1)

/* XXX rs_antenna definitions */

/*
 * Definitions for the software frame/packet descriptors used by
 * the Atheros HAL.  This definition obscures hardware-specific
 * details from the driver.  Drivers are expected to fillin the
 * portions of a descriptor that are not opaque then use HAL calls
 * to complete the work.  Status for completed frames is returned
 * in a device-independent format.
 */
#define	HAL_DESC_HW_SIZE	20

struct ath_desc {
	/*
	 * The following definitions are passed directly
	 * the hardware and managed by the HAL.  Drivers
	 * should not touch those elements marked opaque.
	 */
	uint32_t	ds_link;	/* phys address of next descriptor */
	uint32_t	ds_data;	/* phys address of data buffer */
	uint32_t	ds_ctl0;	/* opaque DMA control 0 */
	uint32_t	ds_ctl1;	/* opaque DMA control 1 */
	uint32_t	ds_hw[HAL_DESC_HW_SIZE];	/* opaque h/w region */
};

struct ath_desc_txedma {
	uint32_t	ds_info;
	uint32_t	ds_link;
	uint32_t	ds_hw[21];	/* includes buf/len */
};

struct ath_desc_status {
	union {
		struct ath_tx_status tx;/* xmit status */
		struct ath_rx_status rx;/* recv status */
	} ds_us;
};

#define	ds_txstat	ds_us.tx
#define	ds_rxstat	ds_us.rx

/* flags passed to tx descriptor setup methods */
/* This is a uint16_t field in ath_buf, just be warned! */
#define	HAL_TXDESC_CLRDMASK	0x0001	/* clear destination filter mask */
#define	HAL_TXDESC_NOACK	0x0002	/* don't wait for ACK */
#define	HAL_TXDESC_RTSENA	0x0004	/* enable RTS */
#define	HAL_TXDESC_CTSENA	0x0008	/* enable CTS */
#define	HAL_TXDESC_INTREQ	0x0010	/* enable per-descriptor interrupt */
#define	HAL_TXDESC_VEOL		0x0020	/* mark virtual EOL */
/* NB: this only affects frame, not any RTS/CTS */
#define	HAL_TXDESC_DURENA	0x0040	/* enable h/w write of duration field */
#define	HAL_TXDESC_EXT_ONLY	0x0080	/* send on ext channel only (11n) */
#define	HAL_TXDESC_EXT_AND_CTL	0x0100	/* send on ext + ctl channels (11n) */
#define	HAL_TXDESC_VMF		0x0200	/* virtual more frag */
#define	HAL_TXDESC_LOWRXCHAIN	0x0400	/* switch to low RX chain */
#define	HAL_TXDESC_LDPC		0x1000	/* Set LDPC TX for all rates */
#define	HAL_TXDESC_HWTS		0x2000	/* Request Azimuth Timestamp in TX payload */
#define	HAL_TXDESC_POS		0x4000	/* Request ToD/ToA locationing */

/* flags passed to rx descriptor setup methods */
#define	HAL_RXDESC_INTREQ	0x0020	/* enable per-descriptor interrupt */
#endif /* _DEV_ATH_DESC_H */
