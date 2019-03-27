/*	$FreeBSD$	*/

/*-
 * Copyright (c) 2005, 2006 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2006 Niall O'Higgins <niallo@openbsd.org>
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

#define RT2573_NOISE_FLOOR	-95

#define RT2573_TX_DESC_SIZE	(sizeof (struct rum_tx_desc))
#define RT2573_RX_DESC_SIZE	(sizeof (struct rum_rx_desc))

#define RT2573_CONFIG_NO	1
#define RT2573_IFACE_INDEX	0

#define RT2573_MCU_CNTL		0x01
#define RT2573_WRITE_MAC	0x02
#define RT2573_READ_MAC		0x03
#define RT2573_WRITE_MULTI_MAC	0x06
#define RT2573_READ_MULTI_MAC	0x07
#define RT2573_READ_EEPROM	0x09
#define RT2573_WRITE_LED	0x0a

/*
 * WME registers.
 */
#define RT2573_AIFSN_CSR	0x0400
#define RT2573_CWMIN_CSR	0x0404
#define RT2573_CWMAX_CSR	0x0408
#define RT2573_TXOP01_CSR	0x040C
#define RT2573_TXOP23_CSR	0x0410
#define RT2573_MCU_CODE_BASE	0x0800

/*
 * H/w encryption/decryption support
 */
#define KEY_SIZE		(IEEE80211_KEYBUF_SIZE + IEEE80211_MICBUF_SIZE)
#define RT2573_ADDR_MAX		64
#define RT2573_SKEY_MAX		4

#define RT2573_SKEY(vap, kidx)	(0x1000 + ((vap) * RT2573_SKEY_MAX + \
	(kidx)) * KEY_SIZE)
#define RT2573_PKEY(id)		(0x1200 + (id) * KEY_SIZE)

#define RT2573_ADDR_ENTRY(id)	(0x1a00 + (id) * 8)

/*
 * Shared memory area
 */
#define RT2573_HW_BCN_BASE(id)	(0x2400 + (id) * 0x100)

/*
 * Control and status registers.
 */
#define RT2573_MAC_CSR0		0x3000
#define RT2573_MAC_CSR1		0x3004
#define RT2573_MAC_CSR2		0x3008
#define RT2573_MAC_CSR3		0x300c
#define RT2573_MAC_CSR4		0x3010
#define RT2573_MAC_CSR5		0x3014
#define RT2573_MAC_CSR6		0x3018
#define RT2573_MAC_CSR7		0x301c
#define RT2573_MAC_CSR8		0x3020
#define RT2573_MAC_CSR9		0x3024
#define RT2573_MAC_CSR10	0x3028
#define RT2573_MAC_CSR11	0x302c
#define RT2573_MAC_CSR12	0x3030
#define RT2573_MAC_CSR13	0x3034
#define RT2573_MAC_CSR14	0x3038
#define RT2573_MAC_CSR15	0x303c
#define RT2573_TXRX_CSR0	0x3040
#define RT2573_TXRX_CSR1	0x3044
#define RT2573_TXRX_CSR2	0x3048
#define RT2573_TXRX_CSR3	0x304c
#define RT2573_TXRX_CSR4	0x3050
#define RT2573_TXRX_CSR5	0x3054
#define RT2573_TXRX_CSR6	0x3058
#define RT2573_TXRX_CSR7	0x305c
#define RT2573_TXRX_CSR8	0x3060
#define RT2573_TXRX_CSR9	0x3064
#define RT2573_TXRX_CSR10	0x3068
#define RT2573_TXRX_CSR11	0x306c
#define RT2573_TXRX_CSR12	0x3070
#define RT2573_TXRX_CSR13	0x3074
#define RT2573_TXRX_CSR14	0x3078
#define RT2573_TXRX_CSR15	0x307c
#define RT2573_PHY_CSR0		0x3080
#define RT2573_PHY_CSR1		0x3084
#define RT2573_PHY_CSR2		0x3088
#define RT2573_PHY_CSR3		0x308c
#define RT2573_PHY_CSR4		0x3090
#define RT2573_PHY_CSR5		0x3094
#define RT2573_PHY_CSR6		0x3098
#define RT2573_PHY_CSR7		0x309c
#define RT2573_SEC_CSR0		0x30a0
#define RT2573_SEC_CSR1		0x30a4
#define RT2573_SEC_CSR2		0x30a8
#define RT2573_SEC_CSR3		0x30ac
#define RT2573_SEC_CSR4		0x30b0
#define RT2573_SEC_CSR5		0x30b4
#define RT2573_STA_CSR0		0x30c0
#define RT2573_STA_CSR1		0x30c4
#define RT2573_STA_CSR2		0x30c8
#define RT2573_STA_CSR3		0x30cc
#define RT2573_STA_CSR4		0x30d0
#define RT2573_STA_CSR5		0x30d4


/* possible values for register RT2573_ADDR_MODE */
#define RT2573_MODE_MASK	0x7
#define RT2573_MODE_NOSEC	0
#define RT2573_MODE_WEP40	1
#define RT2573_MODE_WEP104	2
#define RT2573_MODE_TKIP	3
#define RT2573_MODE_AES_CCMP	4
#define RT2573_MODE_CKIP40	5
#define RT2573_MODE_CKIP104	6

/* possible flags for register RT2573_MAC_CSR1 */
#define RT2573_RESET_ASIC	(1 << 0)
#define RT2573_RESET_BBP	(1 << 1)
#define RT2573_HOST_READY	(1 << 2)

/* possible flags for register MAC_CSR5 */
#define RT2573_NUM_BSSID_MSK(n)	(((n * 3) & 3) << 16)

/* possible flags for register MAC_CSR11 */
#define RT2573_AUTO_WAKEUP		(1 << 15)
#define RT2573_TBCN_EXP(n)		((n) << 8)
#define RT2573_TBCN_EXP_MAX		0x7f
#define RT2573_TBCN_DELAY(t)		(t)
#define RT2573_TBCN_DELAY_MAX		0xff

/* possible flags for register TXRX_CSR0 */
/* Tx filter flags are in the low 16 bits */
#define RT2573_AUTO_TX_SEQ		(1 << 15)
/* Rx filter flags are in the high 16 bits */
#define RT2573_DISABLE_RX		(1 << 16)
#define RT2573_DROP_CRC_ERROR		(1 << 17)
#define RT2573_DROP_PHY_ERROR		(1 << 18)
#define RT2573_DROP_CTL			(1 << 19)
#define RT2573_DROP_NOT_TO_ME		(1 << 20)
#define RT2573_DROP_TODS		(1 << 21)
#define RT2573_DROP_VER_ERROR		(1 << 22)
#define RT2573_DROP_MULTICAST		(1 << 23)
#define RT2573_DROP_BROADCAST		(1 << 24)
#define RT2573_DROP_ACKCTS		(1 << 25)

/* possible flags for register TXRX_CSR4 */
#define RT2573_ACKCTS_PWRMGT	(1 << 16)
#define RT2573_SHORT_PREAMBLE	(1 << 18)
#define RT2573_MRR_ENABLED	(1 << 19)
#define RT2573_MRR_CCK_FALLBACK	(1 << 22)
#define RT2573_LONG_RETRY(max)	((max) << 24)
#define RT2573_LONG_RETRY_MASK	(0xf << 24)
#define RT2573_SHORT_RETRY(max)	((max) << 28)
#define RT2573_SHORT_RETRY_MASK	(0xf << 28)

/* possible flags for register TXRX_CSR9 */
#define RT2573_TSF_TIMER_EN		(1 << 16)
#define RT2573_TSF_SYNC_MODE(x)		(((x) & 0x3) << 17)
#define RT2573_TSF_SYNC_MODE_DIS	0
#define RT2573_TSF_SYNC_MODE_STA	1
#define RT2573_TSF_SYNC_MODE_IBSS	2
#define RT2573_TSF_SYNC_MODE_HOSTAP	3
#define RT2573_TBTT_TIMER_EN		(1 << 19)
#define RT2573_BCN_TX_EN		(1 << 20)

/* possible flags for register PHY_CSR0 */
#define RT2573_PA_PE_2GHZ	(1 << 16)
#define RT2573_PA_PE_5GHZ	(1 << 17)

/* possible flags for register PHY_CSR3 */
#define RT2573_BBP_READ	(1 << 15)
#define RT2573_BBP_BUSY	(1 << 16)
/* possible flags for register PHY_CSR4 */
#define RT2573_RF_20BIT	(20 << 24)
#define RT2573_RF_BUSY	(1U << 31)

/* LED values */
#define RT2573_LED_RADIO	(1 << 8)
#define RT2573_LED_G		(1 << 9)
#define RT2573_LED_A		(1 << 10)
#define RT2573_LED_ON		0x1e1e
#define RT2573_LED_OFF		0x0

/* USB vendor requests */
#define RT2573_MCU_SLEEP	7
#define RT2573_MCU_RUN		8
#define RT2573_MCU_WAKEUP	9

#define RT2573_SMART_MODE	(1 << 0)

#define RT2573_BBPR94_DEFAULT	6

#define RT2573_BBP_WRITE	(1 << 15)

/* dual-band RF */
#define RT2573_RF_5226	1
#define RT2573_RF_5225	3
/* single-band RF */
#define RT2573_RF_2528	2
#define RT2573_RF_2527	4

#define RT2573_BBP_VERSION	0

struct rum_tx_desc {
	uint32_t	flags;
#define RT2573_TX_BURST			(1 << 0)
#define RT2573_TX_VALID			(1 << 1)
#define RT2573_TX_MORE_FRAG		(1 << 2)
#define RT2573_TX_NEED_ACK		(1 << 3)
#define RT2573_TX_TIMESTAMP		(1 << 4)
#define RT2573_TX_OFDM			(1 << 5)
#define RT2573_TX_IFS_SIFS		(1 << 6)
#define RT2573_TX_LONG_RETRY		(1 << 7)
#define RT2573_TX_TKIPMIC		(1 << 8)
#define RT2573_TX_KEY_PAIR		(1 << 9)
#define RT2573_TX_KEY_ID(id)		(((id) & 0x3f) << 10)
#define RT2573_TX_CIP_MODE(m)		((m) << 29)

	uint16_t	wme;
#define RT2573_QID(v)		(v)
#define RT2573_AIFSN(v)		((v) << 4)
#define RT2573_LOGCWMIN(v)	((v) << 8)
#define RT2573_LOGCWMAX(v)	((v) << 12)

	uint8_t		hdrlen;
	uint8_t		xflags;
#define RT2573_TX_HWSEQ		(1 << 4)

	uint8_t		plcp_signal;
	uint8_t		plcp_service;
#define RT2573_PLCP_LENGEXT	0x80

	uint8_t		plcp_length_lo;
	uint8_t		plcp_length_hi;

	uint32_t	iv;
	uint32_t	eiv;

	uint8_t		offset;
	uint8_t		qid;
	uint8_t		txpower;
#define RT2573_DEFAULT_TXPOWER	0

	uint8_t		reserved;
} __packed;

struct rum_rx_desc {
	uint32_t	flags;
#define RT2573_RX_BUSY		(1 << 0)
#define RT2573_RX_DROP		(1 << 1)
#define RT2573_RX_UC2ME		(1 << 2)
#define RT2573_RX_MC		(1 << 3)
#define RT2573_RX_BC		(1 << 4)
#define RT2573_RX_MYBSS		(1 << 5)
#define RT2573_RX_CRC_ERROR	(1 << 6)
#define RT2573_RX_OFDM		(1 << 7)

#define RT2573_RX_DEC_MASK	(3 << 8)
#define RT2573_RX_DEC_OK	(0 << 8)

#define RT2573_RX_IV_ERROR	(1 << 8)
#define RT2573_RX_MIC_ERROR	(2 << 8)
#define RT2573_RX_KEY_ERROR	(3 << 8)

#define RT2573_RX_KEY_PAIR	(1 << 28)

#define RT2573_RX_CIP_MASK	(7 << 29)
#define RT2573_RX_CIP_MODE(m)	((m) << 29)

	uint8_t		rate;
	uint8_t		rssi;
	uint8_t		reserved1;
	uint8_t		offset;
	uint32_t	iv;
	uint32_t	eiv;
	uint32_t	reserved2[2];
} __packed;

#define RT2573_RF1	0
#define RT2573_RF2	2
#define RT2573_RF3	1
#define RT2573_RF4	3

#define RT2573_EEPROM_MACBBP		0x0000
#define RT2573_EEPROM_ADDRESS		0x0004
#define RT2573_EEPROM_ANTENNA		0x0020
#define RT2573_EEPROM_CONFIG2		0x0022
#define RT2573_EEPROM_BBP_BASE		0x0026
#define RT2573_EEPROM_TXPOWER		0x0046
#define RT2573_EEPROM_FREQ_OFFSET	0x005e
#define RT2573_EEPROM_RSSI_2GHZ_OFFSET	0x009a
#define RT2573_EEPROM_RSSI_5GHZ_OFFSET	0x009c
