/*	$FreeBSD$	*/

/*-
 * Copyright (c) 2006
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

#define RT2661_NOISE_FLOOR	-95

#define RT2661_TX_RING_COUNT	32
#define RT2661_MGT_RING_COUNT	32
#define RT2661_RX_RING_COUNT	64

#define RT2661_TX_DESC_SIZE	(sizeof (struct rt2661_tx_desc))
#define RT2661_TX_DESC_WSIZE	(RT2661_TX_DESC_SIZE / 4)
#define RT2661_RX_DESC_SIZE	(sizeof (struct rt2661_rx_desc))
#define RT2661_RX_DESC_WSIZE	(RT2661_RX_DESC_SIZE / 4)

#define RT2661_MAX_SCATTER	5

/*
 * Control and status registers.
 */
#define RT2661_HOST_CMD_CSR		0x0008
#define RT2661_MCU_CNTL_CSR		0x000c
#define RT2661_SOFT_RESET_CSR		0x0010
#define RT2661_MCU_INT_SOURCE_CSR	0x0014
#define RT2661_MCU_INT_MASK_CSR		0x0018
#define RT2661_PCI_USEC_CSR		0x001c
#define RT2661_H2M_MAILBOX_CSR		0x2100
#define RT2661_M2H_CMD_DONE_CSR		0x2104
#define RT2661_HW_BEACON_BASE0		0x2c00
#define RT2661_MAC_CSR0			0x3000
#define RT2661_MAC_CSR1			0x3004
#define RT2661_MAC_CSR2			0x3008
#define RT2661_MAC_CSR3			0x300c
#define RT2661_MAC_CSR4			0x3010
#define RT2661_MAC_CSR5			0x3014
#define RT2661_MAC_CSR6			0x3018
#define RT2661_MAC_CSR7			0x301c
#define RT2661_MAC_CSR8			0x3020
#define RT2661_MAC_CSR9			0x3024
#define RT2661_MAC_CSR10		0x3028
#define RT2661_MAC_CSR11		0x302c
#define RT2661_MAC_CSR12		0x3030
#define RT2661_MAC_CSR13		0x3034
#define RT2661_MAC_CSR14		0x3038
#define RT2661_MAC_CSR15		0x303c
#define RT2661_TXRX_CSR0		0x3040
#define RT2661_TXRX_CSR1		0x3044
#define RT2661_TXRX_CSR2		0x3048
#define RT2661_TXRX_CSR3		0x304c
#define RT2661_TXRX_CSR4		0x3050
#define RT2661_TXRX_CSR5		0x3054
#define RT2661_TXRX_CSR6		0x3058
#define RT2661_TXRX_CSR7		0x305c
#define RT2661_TXRX_CSR8		0x3060
#define RT2661_TXRX_CSR9		0x3064
#define RT2661_TXRX_CSR10		0x3068
#define RT2661_TXRX_CSR11		0x306c
#define RT2661_TXRX_CSR12		0x3070
#define RT2661_TXRX_CSR13		0x3074
#define RT2661_TXRX_CSR14		0x3078
#define RT2661_TXRX_CSR15		0x307c
#define RT2661_PHY_CSR0			0x3080
#define RT2661_PHY_CSR1			0x3084
#define RT2661_PHY_CSR2			0x3088
#define RT2661_PHY_CSR3			0x308c
#define RT2661_PHY_CSR4			0x3090
#define RT2661_PHY_CSR5			0x3094
#define RT2661_PHY_CSR6			0x3098
#define RT2661_PHY_CSR7			0x309c
#define RT2661_SEC_CSR0			0x30a0
#define RT2661_SEC_CSR1			0x30a4
#define RT2661_SEC_CSR2			0x30a8
#define RT2661_SEC_CSR3			0x30ac
#define RT2661_SEC_CSR4			0x30b0
#define RT2661_SEC_CSR5			0x30b4
#define RT2661_STA_CSR0			0x30c0
#define RT2661_STA_CSR1			0x30c4
#define RT2661_STA_CSR2			0x30c8
#define RT2661_STA_CSR3			0x30cc
#define RT2661_STA_CSR4			0x30d0
#define RT2661_AC0_BASE_CSR		0x3400
#define RT2661_AC1_BASE_CSR		0x3404
#define RT2661_AC2_BASE_CSR		0x3408
#define RT2661_AC3_BASE_CSR		0x340c
#define RT2661_MGT_BASE_CSR		0x3410
#define RT2661_TX_RING_CSR0		0x3418
#define RT2661_TX_RING_CSR1		0x341c
#define RT2661_AIFSN_CSR		0x3420
#define RT2661_CWMIN_CSR		0x3424
#define RT2661_CWMAX_CSR		0x3428
#define RT2661_TX_DMA_DST_CSR		0x342c
#define RT2661_TX_CNTL_CSR		0x3430
#define RT2661_LOAD_TX_RING_CSR		0x3434
#define RT2661_RX_BASE_CSR		0x3450
#define RT2661_RX_RING_CSR		0x3454
#define RT2661_RX_CNTL_CSR		0x3458
#define RT2661_PCI_CFG_CSR		0x3460
#define RT2661_INT_SOURCE_CSR		0x3468
#define RT2661_INT_MASK_CSR		0x346c
#define RT2661_E2PROM_CSR		0x3470
#define RT2661_AC_TXOP_CSR0		0x3474
#define RT2661_AC_TXOP_CSR1		0x3478
#define RT2661_TEST_MODE_CSR		0x3484
#define RT2661_IO_CNTL_CSR		0x3498
#define RT2661_MCU_CODE_BASE		0x4000


/* possible flags for register HOST_CMD_CSR */
#define RT2661_KICK_CMD		(1 << 7)
/* Host to MCU (8051) command identifiers */
#define RT2661_MCU_CMD_SLEEP	0x30
#define RT2661_MCU_CMD_WAKEUP	0x31
#define RT2661_MCU_SET_LED	0x50
#define RT2661_MCU_SET_RSSI_LED	0x52

/* possible flags for register MCU_CNTL_CSR */
#define RT2661_MCU_SEL		(1 << 0)
#define RT2661_MCU_RESET	(1 << 1)
#define RT2661_MCU_READY	(1 << 2)

/* possible flags for register MCU_INT_SOURCE_CSR */
#define RT2661_MCU_CMD_DONE		0xff
#define RT2661_MCU_WAKEUP		(1 << 8)
#define RT2661_MCU_BEACON_EXPIRE	(1 << 9)

/* possible flags for register H2M_MAILBOX_CSR */
#define RT2661_H2M_BUSY		(1 << 24)
#define RT2661_TOKEN_NO_INTR	0xff

/* possible flags for register MAC_CSR5 */
#define RT2661_ONE_BSSID	3

/* possible flags for register TXRX_CSR0 */
/* Tx filter flags are in the low 16 bits */
#define RT2661_AUTO_TX_SEQ	(1 << 15)
/* Rx filter flags are in the high 16 bits */
#define RT2661_DISABLE_RX	(1 << 16)
#define RT2661_DROP_CRC_ERROR	(1 << 17)
#define RT2661_DROP_PHY_ERROR	(1 << 18)
#define RT2661_DROP_CTL		(1 << 19)
#define RT2661_DROP_NOT_TO_ME	(1 << 20)
#define RT2661_DROP_TODS	(1 << 21)
#define RT2661_DROP_VER_ERROR	(1 << 22)
#define RT2661_DROP_MULTICAST	(1 << 23)
#define RT2661_DROP_BROADCAST	(1 << 24)
#define RT2661_DROP_ACKCTS	(1 << 25)

/* possible flags for register TXRX_CSR4 */
#define RT2661_SHORT_PREAMBLE	(1 << 19)
#define RT2661_MRR_ENABLED	(1 << 20)
#define RT2661_MRR_CCK_FALLBACK	(1 << 23)

/* possible flags for register TXRX_CSR9 */
#define RT2661_TSF_TICKING	(1 << 16)
#define RT2661_TSF_MODE(x)	(((x) & 0x3) << 17)
/* TBTT stands for Target Beacon Transmission Time */
#define RT2661_ENABLE_TBTT	(1 << 19)
#define RT2661_GENERATE_BEACON	(1 << 20)

/* possible flags for register PHY_CSR0 */
#define RT2661_PA_PE_2GHZ	(1 << 16)
#define RT2661_PA_PE_5GHZ	(1 << 17)

/* possible flags for register PHY_CSR3 */
#define RT2661_BBP_READ	(1 << 15)
#define RT2661_BBP_BUSY	(1 << 16)

/* possible flags for register PHY_CSR4 */
#define RT2661_RF_21BIT	(21 << 24)
#define RT2661_RF_BUSY	(1U << 31)

/* possible values for register STA_CSR4 */
#define RT2661_TX_STAT_VALID	(1 << 0)
#define RT2661_TX_RESULT(v)	(((v) >> 1) & 0x7)
#define RT2661_TX_RETRYCNT(v)	(((v) >> 4) & 0xf)
#define RT2661_TX_QID(v)	(((v) >> 8) & 0xf)
#define RT2661_TX_SUCCESS	0
#define RT2661_TX_RETRY_FAIL	6

/* possible flags for register TX_CNTL_CSR */
#define RT2661_KICK_MGT	(1 << 4)

/* possible flags for register INT_SOURCE_CSR */
#define RT2661_TX_DONE		(1 << 0)
#define RT2661_RX_DONE		(1 << 1)
#define RT2661_TX0_DMA_DONE	(1 << 16)
#define RT2661_TX1_DMA_DONE	(1 << 17)
#define RT2661_TX2_DMA_DONE	(1 << 18)
#define RT2661_TX3_DMA_DONE	(1 << 19)
#define RT2661_MGT_DONE		(1 << 20)

/* possible flags for register E2PROM_CSR */
#define RT2661_C	(1 << 1)
#define RT2661_S	(1 << 2)
#define RT2661_D	(1 << 3)
#define RT2661_Q	(1 << 4)
#define RT2661_93C46	(1 << 5)

/* Tx descriptor */
struct rt2661_tx_desc {
	uint32_t	flags;
#define RT2661_TX_BUSY		(1 << 0)
#define RT2661_TX_VALID		(1 << 1)
#define RT2661_TX_MORE_FRAG	(1 << 2)
#define RT2661_TX_NEED_ACK	(1 << 3)
#define RT2661_TX_TIMESTAMP	(1 << 4)
#define RT2661_TX_OFDM		(1 << 5)
#define RT2661_TX_IFS		(1 << 6)
#define RT2661_TX_LONG_RETRY	(1 << 7)
#define RT2661_TX_BURST		(1 << 28)

	uint16_t	wme;
#define RT2661_QID(v)		(v)
#define RT2661_AIFSN(v)		((v) << 4)
#define RT2661_LOGCWMIN(v)	((v) << 8)
#define RT2661_LOGCWMAX(v)	((v) << 12)

	uint16_t	xflags;
#define RT2661_TX_HWSEQ		(1 << 12)

	uint8_t		plcp_signal;
	uint8_t		plcp_service;
#define RT2661_PLCP_LENGEXT	0x80

	uint8_t		plcp_length_lo;
	uint8_t		plcp_length_hi;

	uint32_t	iv;
	uint32_t	eiv;

	uint8_t		offset;
	uint8_t		qid;
#define RT2661_QID_MGT	13

	uint8_t		txpower;
#define RT2661_DEFAULT_TXPOWER	0

	uint8_t		reserved1;

	uint32_t	addr[RT2661_MAX_SCATTER];
	uint16_t	len[RT2661_MAX_SCATTER];

	uint16_t	reserved2;
} __packed;

/* Rx descriptor */
struct rt2661_rx_desc {
	uint32_t	flags;
#define RT2661_RX_BUSY		(1 << 0)
#define RT2661_RX_DROP		(1 << 1)
#define RT2661_RX_CRC_ERROR	(1 << 6)
#define RT2661_RX_OFDM		(1 << 7)
#define RT2661_RX_PHY_ERROR	(1 << 8)
#define RT2661_RX_CIPHER_MASK	0x00000600

	uint8_t		rate;
	uint8_t		rssi;
	uint8_t		reserved1;
	uint8_t		offset;
	uint32_t	iv;
	uint32_t	eiv;
	uint32_t	reserved2;
	uint32_t	physaddr;
	uint32_t	reserved3[10];
} __packed;

#define RAL_RF1	0
#define RAL_RF2	2
#define RAL_RF3	1
#define RAL_RF4	3

/* dual-band RF */
#define RT2661_RF_5225	1
#define RT2661_RF_5325	2
/* single-band RF */
#define RT2661_RF_2527	3
#define RT2661_RF_2529	4

#define RT2661_RX_DESC_BACK	4

#define RT2661_SMART_MODE	(1 << 0)

#define RT2661_BBPR94_DEFAULT	6

#define RT2661_SHIFT_D	3
#define RT2661_SHIFT_Q	4

#define RT2661_EEPROM_MAC01		0x02
#define RT2661_EEPROM_MAC23		0x03
#define RT2661_EEPROM_MAC45		0x04
#define RT2661_EEPROM_ANTENNA		0x10
#define RT2661_EEPROM_CONFIG2		0x11
#define RT2661_EEPROM_BBP_BASE		0x13
#define RT2661_EEPROM_TXPOWER		0x23
#define RT2661_EEPROM_FREQ_OFFSET	0x2f
#define RT2661_EEPROM_RSSI_2GHZ_OFFSET	0x4d
#define RT2661_EEPROM_RSSI_5GHZ_OFFSET	0x4e

#define RT2661_EEPROM_DELAY	1	/* minimum hold time (microsecond) */

/*
 * control and status registers access macros
 */
#define RAL_READ(sc, reg)						\
	bus_space_read_4((sc)->sc_st, (sc)->sc_sh, (reg))

#define RAL_READ_REGION_4(sc, offset, datap, count)			\
	bus_space_read_region_4((sc)->sc_st, (sc)->sc_sh, (offset),	\
	    (datap), (count))

#define RAL_WRITE(sc, reg, val)						\
	bus_space_write_4((sc)->sc_st, (sc)->sc_sh, (reg), (val))

#define RAL_WRITE_REGION_1(sc, offset, datap, count)			\
	bus_space_write_region_1((sc)->sc_st, (sc)->sc_sh, (offset),	\
	    (datap), (count))

/*
 * EEPROM access macro
 */
#define RT2661_EEPROM_CTL(sc, val) do {					\
	RAL_WRITE((sc), RT2661_E2PROM_CSR, (val));			\
	DELAY(RT2661_EEPROM_DELAY);					\
} while (/* CONSTCOND */0)

/*
 * Default values for MAC registers; values taken from the reference driver.
 */
#define RT2661_DEF_MAC					\
	{ RT2661_TXRX_CSR0,        0x0000b032 },	\
	{ RT2661_TXRX_CSR1,        0x9eb39eb3 },	\
	{ RT2661_TXRX_CSR2,        0x8a8b8c8d },	\
	{ RT2661_TXRX_CSR3,        0x00858687 },	\
	{ RT2661_TXRX_CSR7,        0x2e31353b },	\
	{ RT2661_TXRX_CSR8,        0x2a2a2a2c },	\
	{ RT2661_TXRX_CSR15,       0x0000000f },	\
	{ RT2661_MAC_CSR6,         0x00000fff },	\
	{ RT2661_MAC_CSR8,         0x016c030a },	\
	{ RT2661_MAC_CSR10,        0x00000718 },	\
	{ RT2661_MAC_CSR12,        0x00000004 },	\
	{ RT2661_MAC_CSR13,        0x0000e000 },	\
	{ RT2661_SEC_CSR0,         0x00000000 },	\
	{ RT2661_SEC_CSR1,         0x00000000 },	\
	{ RT2661_SEC_CSR5,         0x00000000 },	\
	{ RT2661_PHY_CSR1,         0x000023b0 },	\
	{ RT2661_PHY_CSR5,         0x060a100c },	\
	{ RT2661_PHY_CSR6,         0x00080606 },	\
	{ RT2661_PHY_CSR7,         0x00000a08 },	\
	{ RT2661_PCI_CFG_CSR,      0x3cca4808 },	\
	{ RT2661_AIFSN_CSR,        0x00002273 },	\
	{ RT2661_CWMIN_CSR,        0x00002344 },	\
	{ RT2661_CWMAX_CSR,        0x000034aa },	\
	{ RT2661_TEST_MODE_CSR,    0x00000200 },	\
	{ RT2661_M2H_CMD_DONE_CSR, 0xffffffff }

/*
 * Default values for BBP registers; values taken from the reference driver.
 */
#define RT2661_DEF_BBP	\
	{   3, 0x00 },	\
	{  15, 0x30 },	\
	{  17, 0x20 },	\
	{  21, 0xc8 },	\
	{  22, 0x38 },	\
	{  23, 0x06 },	\
	{  24, 0xfe },	\
	{  25, 0x0a },	\
	{  26, 0x0d },	\
	{  34, 0x12 },	\
	{  37, 0x07 },	\
	{  39, 0xf8 },	\
	{  41, 0x60 },	\
	{  53, 0x10 },	\
	{  54, 0x18 },	\
	{  60, 0x10 },	\
	{  61, 0x04 },	\
	{  62, 0x04 },	\
	{  75, 0xfe },	\
	{  86, 0xfe },	\
	{  88, 0xfe },	\
	{  90, 0x0f },	\
	{  99, 0x00 },	\
	{ 102, 0x16 },	\
	{ 107, 0x04 }

/*
 * Default settings for RF registers; values taken from the reference driver.
 */
#define RT2661_RF5225_1					\
	{   1, 0x00b33, 0x011e1, 0x1a014, 0x30282 },	\
	{   2, 0x00b33, 0x011e1, 0x1a014, 0x30287 },	\
	{   3, 0x00b33, 0x011e2, 0x1a014, 0x30282 },	\
	{   4, 0x00b33, 0x011e2, 0x1a014, 0x30287 },	\
	{   5, 0x00b33, 0x011e3, 0x1a014, 0x30282 },	\
	{   6, 0x00b33, 0x011e3, 0x1a014, 0x30287 },	\
	{   7, 0x00b33, 0x011e4, 0x1a014, 0x30282 },	\
	{   8, 0x00b33, 0x011e4, 0x1a014, 0x30287 },	\
	{   9, 0x00b33, 0x011e5, 0x1a014, 0x30282 },	\
	{  10, 0x00b33, 0x011e5, 0x1a014, 0x30287 },	\
	{  11, 0x00b33, 0x011e6, 0x1a014, 0x30282 },	\
	{  12, 0x00b33, 0x011e6, 0x1a014, 0x30287 },	\
	{  13, 0x00b33, 0x011e7, 0x1a014, 0x30282 },	\
	{  14, 0x00b33, 0x011e8, 0x1a014, 0x30284 },	\
							\
	{  36, 0x00b33, 0x01266, 0x26014, 0x30288 },	\
	{  40, 0x00b33, 0x01268, 0x26014, 0x30280 },	\
	{  44, 0x00b33, 0x01269, 0x26014, 0x30282 },	\
	{  48, 0x00b33, 0x0126a, 0x26014, 0x30284 },	\
	{  52, 0x00b33, 0x0126b, 0x26014, 0x30286 },	\
	{  56, 0x00b33, 0x0126c, 0x26014, 0x30288 },	\
	{  60, 0x00b33, 0x0126e, 0x26014, 0x30280 },	\
	{  64, 0x00b33, 0x0126f, 0x26014, 0x30282 },	\
							\
	{ 100, 0x00b33, 0x0128a, 0x2e014, 0x30280 },	\
	{ 104, 0x00b33, 0x0128b, 0x2e014, 0x30282 },	\
	{ 108, 0x00b33, 0x0128c, 0x2e014, 0x30284 },	\
	{ 112, 0x00b33, 0x0128d, 0x2e014, 0x30286 },	\
	{ 116, 0x00b33, 0x0128e, 0x2e014, 0x30288 },	\
	{ 120, 0x00b33, 0x012a0, 0x2e014, 0x30280 },	\
	{ 124, 0x00b33, 0x012a1, 0x2e014, 0x30282 },	\
	{ 128, 0x00b33, 0x012a2, 0x2e014, 0x30284 },	\
	{ 132, 0x00b33, 0x012a3, 0x2e014, 0x30286 },	\
	{ 136, 0x00b33, 0x012a4, 0x2e014, 0x30288 },	\
	{ 140, 0x00b33, 0x012a6, 0x2e014, 0x30280 },	\
							\
	{ 149, 0x00b33, 0x012a8, 0x2e014, 0x30287 },	\
	{ 153, 0x00b33, 0x012a9, 0x2e014, 0x30289 },	\
	{ 157, 0x00b33, 0x012ab, 0x2e014, 0x30281 },	\
	{ 161, 0x00b33, 0x012ac, 0x2e014, 0x30283 },	\
	{ 165, 0x00b33, 0x012ad, 0x2e014, 0x30285 }

#define RT2661_RF5225_2					\
	{   1, 0x00b33, 0x011e1, 0x1a014, 0x30282 },	\
	{   2, 0x00b33, 0x011e1, 0x1a014, 0x30287 },	\
	{   3, 0x00b33, 0x011e2, 0x1a014, 0x30282 },	\
	{   4, 0x00b33, 0x011e2, 0x1a014, 0x30287 },	\
	{   5, 0x00b33, 0x011e3, 0x1a014, 0x30282 },	\
	{   6, 0x00b33, 0x011e3, 0x1a014, 0x30287 },	\
	{   7, 0x00b33, 0x011e4, 0x1a014, 0x30282 },	\
	{   8, 0x00b33, 0x011e4, 0x1a014, 0x30287 },	\
	{   9, 0x00b33, 0x011e5, 0x1a014, 0x30282 },	\
	{  10, 0x00b33, 0x011e5, 0x1a014, 0x30287 },	\
	{  11, 0x00b33, 0x011e6, 0x1a014, 0x30282 },	\
	{  12, 0x00b33, 0x011e6, 0x1a014, 0x30287 },	\
	{  13, 0x00b33, 0x011e7, 0x1a014, 0x30282 },	\
	{  14, 0x00b33, 0x011e8, 0x1a014, 0x30284 },	\
							\
	{  36, 0x00b35, 0x11206, 0x26014, 0x30280 },	\
	{  40, 0x00b34, 0x111a0, 0x26014, 0x30280 },	\
	{  44, 0x00b34, 0x111a1, 0x26014, 0x30286 },	\
	{  48, 0x00b34, 0x111a3, 0x26014, 0x30282 },	\
	{  52, 0x00b34, 0x111a4, 0x26014, 0x30288 },	\
	{  56, 0x00b34, 0x111a6, 0x26014, 0x30284 },	\
	{  60, 0x00b34, 0x111a8, 0x26014, 0x30280 },	\
	{  64, 0x00b34, 0x111a9, 0x26014, 0x30286 },	\
							\
	{ 100, 0x00b35, 0x11226, 0x2e014, 0x30280 },	\
	{ 104, 0x00b35, 0x11228, 0x2e014, 0x30280 },	\
	{ 108, 0x00b35, 0x1122a, 0x2e014, 0x30280 },	\
	{ 112, 0x00b35, 0x1122c, 0x2e014, 0x30280 },	\
	{ 116, 0x00b35, 0x1122e, 0x2e014, 0x30280 },	\
	{ 120, 0x00b34, 0x111c0, 0x2e014, 0x30280 },	\
	{ 124, 0x00b34, 0x111c1, 0x2e014, 0x30286 },	\
	{ 128, 0x00b34, 0x111c3, 0x2e014, 0x30282 },	\
	{ 132, 0x00b34, 0x111c4, 0x2e014, 0x30288 },	\
	{ 136, 0x00b34, 0x111c6, 0x2e014, 0x30284 },	\
	{ 140, 0x00b34, 0x111c8, 0x2e014, 0x30280 },	\
							\
	{ 149, 0x00b34, 0x111cb, 0x2e014, 0x30286 },	\
	{ 153, 0x00b34, 0x111cd, 0x2e014, 0x30282 },	\
	{ 157, 0x00b35, 0x11242, 0x2e014, 0x30285 },	\
	{ 161, 0x00b35, 0x11244, 0x2e014, 0x30285 },	\
	{ 165, 0x00b35, 0x11246, 0x2e014, 0x30285 }
