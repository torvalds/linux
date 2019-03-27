/*	$FreeBSD$	*/

/*-
 * Copyright (c) 2005, 2006
 *	Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
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
 */

#define RAL_NOISE_FLOOR		-95
#define RAL_RSSI_CORR		120

#define RAL_RX_DESC_SIZE	(sizeof (struct ural_rx_desc))
#define RAL_TX_DESC_SIZE	(sizeof (struct ural_tx_desc))
#define RAL_FRAME_SIZE		0x780	/* NOTE: using 0x980 does not work */

#define RAL_CONFIG_NO	1
#define RAL_IFACE_INDEX 0

#define RAL_VENDOR_REQUEST	0x01
#define RAL_WRITE_MAC		0x02
#define RAL_READ_MAC		0x03
#define RAL_WRITE_MULTI_MAC	0x06
#define RAL_READ_MULTI_MAC	0x07
#define RAL_READ_EEPROM		0x09

/*
 * MAC registers.
 */
#define RAL_MAC_CSR0	0x0400	/* ASIC Version */
#define RAL_MAC_CSR1	0x0402	/* System control */
#define RAL_MAC_CSR2	0x0404	/* MAC addr0 */
#define RAL_MAC_CSR3	0x0406	/* MAC addr1 */
#define RAL_MAC_CSR4	0x0408	/* MAC addr2 */
#define RAL_MAC_CSR5	0x040a	/* BSSID0 */
#define RAL_MAC_CSR6	0x040c	/* BSSID1 */
#define RAL_MAC_CSR7	0x040e	/* BSSID2 */
#define RAL_MAC_CSR8	0x0410	/* Max frame length */
#define RAL_MAC_CSR9	0x0412	/* Timer control */
#define RAL_MAC_CSR10	0x0414	/* Slot time */
#define RAL_MAC_CSR11	0x0416	/* IFS */
#define RAL_MAC_CSR12	0x0418	/* EIFS */
#define RAL_MAC_CSR13	0x041a	/* Power mode0 */
#define RAL_MAC_CSR14	0x041c	/* Power mode1 */
#define RAL_MAC_CSR15	0x041e	/* Power saving transition0 */
#define RAL_MAC_CSR16	0x0420	/* Power saving transition1 */
#define RAL_MAC_CSR17	0x0422	/* Power state control */
#define RAL_MAC_CSR18	0x0424	/* Auto wake-up control */
#define RAL_MAC_CSR19	0x0426	/* GPIO control */
#define RAL_MAC_CSR20	0x0428	/* LED control0 */
#define RAL_MAC_CSR22	0x042c	/* XXX not documented */

/*
 * Tx/Rx Registers.
 */
#define RAL_TXRX_CSR0	0x0440	/* Security control */
#define RAL_TXRX_CSR2	0x0444	/* Rx control */
#define RAL_TXRX_CSR5	0x044a	/* CCK Tx BBP ID0 */
#define RAL_TXRX_CSR6	0x044c	/* CCK Tx BBP ID1 */
#define RAL_TXRX_CSR7	0x044e	/* OFDM Tx BBP ID0 */
#define RAL_TXRX_CSR8	0x0450	/* OFDM Tx BBP ID1 */
#define RAL_TXRX_CSR10	0x0454	/* Auto responder control */
#define RAL_TXRX_CSR11	0x0456	/* Auto responder basic rate */
#define RAL_TXRX_CSR18	0x0464	/* Beacon interval */
#define RAL_TXRX_CSR19	0x0466	/* Beacon/sync control */
#define RAL_TXRX_CSR20	0x0468	/* Beacon alignment */
#define RAL_TXRX_CSR21	0x046a	/* XXX not documented */

/*
 * Security registers.
 */
#define RAL_SEC_CSR0	0x0480	/* Shared key 0, word 0 */

/*
 * PHY registers.
 */
#define RAL_PHY_CSR2	0x04c4	/* Tx MAC configuration */
#define RAL_PHY_CSR4	0x04c8	/* Interface configuration */
#define RAL_PHY_CSR5	0x04ca	/* BBP Pre-Tx CCK */
#define RAL_PHY_CSR6	0x04cc	/* BBP Pre-Tx OFDM */
#define RAL_PHY_CSR7	0x04ce	/* BBP serial control */
#define RAL_PHY_CSR8	0x04d0	/* BBP serial status */
#define RAL_PHY_CSR9	0x04d2	/* RF serial control0 */
#define RAL_PHY_CSR10	0x04d4	/* RF serial control1 */

/*
 * Statistics registers.
 */
#define RAL_STA_CSR0	0x04e0	/* FCS error */


#define RAL_DISABLE_RX		(1 << 0)
#define RAL_DROP_CRC		(1 << 1)
#define RAL_DROP_PHY		(1 << 2)
#define RAL_DROP_CTL		(1 << 3)
#define RAL_DROP_NOT_TO_ME	(1 << 4)
#define RAL_DROP_TODS		(1 << 5)
#define RAL_DROP_BAD_VERSION	(1 << 6)
#define RAL_DROP_MULTICAST	(1 << 9)
#define RAL_DROP_BROADCAST	(1 << 10)

#define RAL_SHORT_PREAMBLE	(1 << 2)

#define RAL_RESET_ASIC	(1 << 0)
#define RAL_RESET_BBP	(1 << 1)
#define RAL_HOST_READY	(1 << 2)

#define RAL_ENABLE_TSF			(1 << 0)
#define RAL_ENABLE_TSF_SYNC(x)		(((x) & 0x3) << 1)
#define RAL_ENABLE_TBCN			(1 << 3)
#define RAL_ENABLE_BEACON_GENERATOR	(1 << 4)

#define RAL_RF_AWAKE	(3 << 7)
#define RAL_BBP_AWAKE	(3 << 5)

#define RAL_BBP_WRITE	(1 << 15)
#define RAL_BBP_BUSY	(1 << 0)

#define RAL_RF1_AUTOTUNE	0x08000
#define RAL_RF3_AUTOTUNE	0x00040

#define RAL_RF_2522	0x00
#define RAL_RF_2523	0x01
#define RAL_RF_2524	0x02
#define RAL_RF_2525	0x03
#define RAL_RF_2525E	0x04
#define RAL_RF_2526	0x05
/* dual-band RF */
#define RAL_RF_5222	0x10

#define RAL_BBP_VERSION	0
#define RAL_BBP_TX	2
#define RAL_BBP_RX	14

#define RAL_BBP_ANTA		0x00
#define RAL_BBP_DIVERSITY	0x01
#define RAL_BBP_ANTB		0x02
#define RAL_BBP_ANTMASK		0x03
#define RAL_BBP_FLIPIQ		0x04

#define RAL_JAPAN_FILTER	0x08

struct ural_tx_desc {
	uint32_t	flags;
#define RAL_TX_RETRY(x)		((x) << 4)
#define RAL_TX_MORE_FRAG	(1 << 8)
#define RAL_TX_ACK		(1 << 9)
#define RAL_TX_TIMESTAMP	(1 << 10)
#define RAL_TX_OFDM		(1 << 11)
#define RAL_TX_NEWSEQ		(1 << 12)

#define RAL_TX_IFS_MASK		0x00006000
#define RAL_TX_IFS_BACKOFF	(0 << 13)
#define RAL_TX_IFS_SIFS		(1 << 13)
#define RAL_TX_IFS_NEWBACKOFF	(2 << 13)
#define RAL_TX_IFS_NONE		(3 << 13)

	uint16_t	wme;
#define RAL_LOGCWMAX(x)		(((x) & 0xf) << 12)
#define RAL_LOGCWMIN(x)		(((x) & 0xf) << 8)
#define RAL_AIFSN(x)		(((x) & 0x3) << 6)
#define RAL_IVOFFSET(x)		(((x) & 0x3f))

	uint16_t	reserved1;
	uint8_t		plcp_signal;
	uint8_t		plcp_service;
#define RAL_PLCP_LENGEXT	0x80

	uint8_t		plcp_length_lo;
	uint8_t		plcp_length_hi;
	uint32_t	iv;
	uint32_t	eiv;
} __packed;

struct ural_rx_desc {
	uint32_t	flags;
#define RAL_RX_CRC_ERROR	(1 << 5)
#define RAL_RX_OFDM		(1 << 6)
#define RAL_RX_PHY_ERROR	(1 << 7)

	uint8_t		rssi;
	uint8_t		rate;
	uint16_t	reserved;

	uint32_t	iv;
	uint32_t	eiv;
} __packed;

#define RAL_RF_LOBUSY	(1 << 15)
#define RAL_RF_BUSY	(1U << 31)
#define RAL_RF_20BIT	(20 << 24)

#define RAL_RF1	0
#define RAL_RF2	2
#define RAL_RF3	1
#define RAL_RF4	3

#define RAL_EEPROM_ADDRESS	0x0004
#define RAL_EEPROM_TXPOWER	0x003c
#define RAL_EEPROM_CONFIG0	0x0016
#define RAL_EEPROM_BBP_BASE	0x001c
