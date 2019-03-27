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

#define RT2560_DEFAULT_RSSI_CORR	0x79
#define RT2560_NOISE_FLOOR		-95

#define RT2560_TX_RING_COUNT		48
#define RT2560_ATIM_RING_COUNT		4
#define RT2560_PRIO_RING_COUNT		16
#define RT2560_BEACON_RING_COUNT	1
#define RT2560_RX_RING_COUNT		32

#define RT2560_TX_DESC_SIZE	(sizeof (struct rt2560_tx_desc))
#define RT2560_RX_DESC_SIZE	(sizeof (struct rt2560_rx_desc))

#define RT2560_MAX_SCATTER	1

/*
 * Control and status registers.
 */
#define RT2560_CSR0		0x0000	/* ASIC version number */
#define RT2560_CSR1		0x0004	/* System control */
#define RT2560_CSR3		0x000c	/* STA MAC address 0 */
#define RT2560_CSR4		0x0010	/* STA MAC address 1 */
#define RT2560_CSR5		0x0014	/* BSSID 0 */
#define RT2560_CSR6		0x0018	/* BSSID 1 */
#define RT2560_CSR7		0x001c	/* Interrupt source */
#define RT2560_CSR8		0x0020	/* Interrupt mask */
#define RT2560_CSR9		0x0024	/* Maximum frame length */
#define RT2560_SECCSR0		0x0028	/* WEP control */
#define RT2560_CSR11		0x002c	/* Back-off control */
#define RT2560_CSR12		0x0030	/* Synchronization configuration 0 */
#define RT2560_CSR13		0x0034	/* Synchronization configuration 1 */
#define RT2560_CSR14		0x0038	/* Synchronization control */
#define RT2560_CSR15		0x003c	/* Synchronization status */
#define RT2560_CSR16		0x0040	/* TSF timer 0 */
#define RT2560_CSR17		0x0044	/* TSF timer 1 */
#define RT2560_CSR18		0x0048	/* IFS timer 0 */
#define RT2560_CSR19		0x004c	/* IFS timer 1 */
#define RT2560_CSR20		0x0050	/* WAKEUP timer */
#define RT2560_CSR21		0x0054	/* EEPROM control */
#define RT2560_CSR22		0x0058	/* CFP control */
#define RT2560_TXCSR0		0x0060	/* TX control */
#define RT2560_TXCSR1		0x0064	/* TX configuration */
#define RT2560_TXCSR2		0x0068	/* TX descriptor configuration */
#define RT2560_TXCSR3		0x006c	/* TX ring base address */
#define RT2560_TXCSR4		0x0070	/* TX ATIM ring base address */
#define RT2560_TXCSR5		0x0074	/* TX PRIO ring base address */
#define RT2560_TXCSR6		0x0078	/* Beacon base address */
#define RT2560_TXCSR7		0x007c	/* AutoResponder control */
#define RT2560_RXCSR0		0x0080	/* RX control */
#define RT2560_RXCSR1		0x0084	/* RX descriptor configuration */
#define RT2560_RXCSR2		0x0088	/* RX ring base address */
#define RT2560_PCICSR		0x008c	/* PCI control */
#define RT2560_RXCSR3		0x0090	/* BBP ID 0 */
#define RT2560_TXCSR9		0x0094	/* OFDM TX BBP */
#define RT2560_ARSP_PLCP_0	0x0098	/* Auto Responder PLCP address */
#define RT2560_ARSP_PLCP_1	0x009c	/* Auto Responder Basic Rate mask */
#define RT2560_CNT0		0x00a0	/* FCS error counter */
#define RT2560_CNT1		0x00ac	/* PLCP error counter */
#define RT2560_CNT2		0x00b0	/* Long error counter */
#define RT2560_CNT3		0x00b8	/* CCA false alarm counter */
#define RT2560_CNT4		0x00bc	/* RX FIFO Overflow counter */
#define RT2560_CNT5		0x00c0	/* Tx FIFO Underrun counter */
#define RT2560_PWRCSR0		0x00c4	/* Power mode configuration */
#define RT2560_PSCSR0		0x00c8	/* Power state transition time */
#define RT2560_PSCSR1		0x00cc	/* Power state transition time */
#define RT2560_PSCSR2		0x00d0	/* Power state transition time */
#define RT2560_PSCSR3		0x00d4	/* Power state transition time */
#define RT2560_PWRCSR1		0x00d8	/* Manual power control/status */
#define RT2560_TIMECSR		0x00dc	/* Timer control */
#define RT2560_MACCSR0		0x00e0	/* MAC configuration */
#define RT2560_MACCSR1		0x00e4	/* MAC configuration */
#define RT2560_RALINKCSR	0x00e8	/* Ralink RX auto-reset BBCR */
#define RT2560_BCNCSR		0x00ec	/* Beacon interval control */
#define RT2560_BBPCSR		0x00f0	/* BBP serial control */
#define RT2560_RFCSR		0x00f4	/* RF serial control */
#define RT2560_LEDCSR		0x00f8	/* LED control */
#define RT2560_SECCSR3		0x00fc	/* XXX not documented */
#define RT2560_DMACSR0		0x0100	/* Current RX ring address */
#define RT2560_DMACSR1		0x0104	/* Current Tx ring address */
#define RT2560_DMACSR2		0x0104	/* Current Priority ring address */
#define RT2560_DMACSR3		0x0104	/* Current ATIM ring address */
#define RT2560_TXACKCSR0	0x0110	/* XXX not documented */
#define RT2560_GPIOCSR		0x0120	/* */
#define RT2560_BBBPPCSR		0x0124	/* BBP Pin Control */
#define RT2560_FIFOCSR0		0x0128	/* TX FIFO pointer */
#define RT2560_FIFOCSR1		0x012c	/* RX FIFO pointer */
#define RT2560_BCNOCSR		0x0130	/* Beacon time offset */
#define RT2560_RLPWCSR		0x0134	/* RX_PE Low Width */
#define RT2560_TESTCSR		0x0138	/* Test Mode Select */
#define RT2560_PLCP1MCSR	0x013c	/* Signal/Service/Length of ACK @1M */
#define RT2560_PLCP2MCSR	0x0140	/* Signal/Service/Length of ACK @2M */
#define RT2560_PLCP5p5MCSR	0x0144	/* Signal/Service/Length of ACK @5.5M */
#define RT2560_PLCP11MCSR	0x0148	/* Signal/Service/Length of ACK @11M */
#define RT2560_ACKPCTCSR	0x014c	/* ACK/CTS padload consume time */
#define RT2560_ARTCSR1		0x0150	/* ACK/CTS padload consume time */
#define RT2560_ARTCSR2		0x0154	/* ACK/CTS padload consume time */
#define RT2560_SECCSR1		0x0158	/* WEP control */
#define RT2560_BBPCSR1		0x015c	/* BBP TX Configuration */


/* possible flags for register RXCSR0 */
#define RT2560_DISABLE_RX		(1 << 0)
#define RT2560_DROP_CRC_ERROR		(1 << 1)
#define RT2560_DROP_PHY_ERROR		(1 << 2)
#define RT2560_DROP_CTL			(1 << 3)
#define RT2560_DROP_NOT_TO_ME		(1 << 4)
#define RT2560_DROP_TODS		(1 << 5)
#define RT2560_DROP_VERSION_ERROR	(1 << 6)

/* possible flags for register CSR1 */
#define RT2560_RESET_ASIC	(1 << 0)
#define RT2560_RESET_BBP	(1 << 1)
#define RT2560_HOST_READY	(1 << 2)

/* possible flags for register CSR14 */
#define RT2560_ENABLE_TSF		(1 << 0)
#define RT2560_ENABLE_TSF_SYNC(x)	(((x) & 0x3) << 1)
#define RT2560_ENABLE_TBCN		(1 << 3)
#define RT2560_ENABLE_BEACON_GENERATOR	(1 << 6)

/* possible flags for register CSR21 */
#define RT2560_C	(1 << 1)
#define RT2560_S	(1 << 2)
#define RT2560_D	(1 << 3)
#define RT2560_Q	(1 << 4)
#define RT2560_93C46	(1 << 5)

#define RT2560_SHIFT_D	3
#define RT2560_SHIFT_Q	4

/* possible flags for register TXCSR0 */
#define RT2560_KICK_TX		(1 << 0)
#define RT2560_KICK_ATIM	(1 << 1)
#define RT2560_KICK_PRIO	(1 << 2)
#define RT2560_ABORT_TX		(1 << 3)

/* possible flags for register SECCSR0 */
#define RT2560_KICK_DECRYPT	(1 << 0)

/* possible flags for register SECCSR1 */
#define RT2560_KICK_ENCRYPT	(1 << 0)

/* possible flags for register CSR7 */
#define RT2560_BEACON_EXPIRE	0x00000001
#define RT2560_WAKEUP_EXPIRE	0x00000002
#define RT2560_ATIM_EXPIRE	0x00000004
#define RT2560_TX_DONE		0x00000008
#define RT2560_ATIM_DONE	0x00000010
#define RT2560_PRIO_DONE	0x00000020
#define RT2560_RX_DONE		0x00000040
#define RT2560_DECRYPTION_DONE	0x00000080
#define RT2560_ENCRYPTION_DONE	0x00000100

#define RT2560_INTR_MASK							\
	(~(RT2560_BEACON_EXPIRE | RT2560_WAKEUP_EXPIRE | RT2560_TX_DONE |	\
	   RT2560_PRIO_DONE | RT2560_RX_DONE | RT2560_DECRYPTION_DONE |		\
	   RT2560_ENCRYPTION_DONE))

/* Tx descriptor */
struct rt2560_tx_desc {
	uint32_t	flags;
#define RT2560_TX_BUSY			(1 << 0)
#define RT2560_TX_VALID			(1 << 1)

#define RT2560_TX_RESULT_MASK		0x0000001c
#define RT2560_TX_SUCCESS		(0 << 2)
#define RT2560_TX_SUCCESS_RETRY		(1 << 2)
#define RT2560_TX_FAIL_RETRY		(2 << 2)
#define RT2560_TX_FAIL_INVALID		(3 << 2)
#define RT2560_TX_FAIL_OTHER		(4 << 2)

#define RT2560_TX_MORE_FRAG		(1 << 8)
#define RT2560_TX_ACK			(1 << 9)
#define RT2560_TX_TIMESTAMP		(1 << 10)
#define RT2560_TX_OFDM			(1 << 11)
#define RT2560_TX_CIPHER_BUSY		(1 << 12)

#define RT2560_TX_IFS_MASK		0x00006000
#define RT2560_TX_IFS_BACKOFF		(0 << 13)
#define RT2560_TX_IFS_SIFS		(1 << 13)
#define RT2560_TX_IFS_NEWBACKOFF	(2 << 13)
#define RT2560_TX_IFS_NONE		(3 << 13)

#define RT2560_TX_LONG_RETRY		(1 << 15)

#define RT2560_TX_CIPHER_MASK		0xe0000000
#define RT2560_TX_CIPHER_NONE		(0 << 29)
#define RT2560_TX_CIPHER_WEP40		(1 << 29)
#define RT2560_TX_CIPHER_WEP104		(2 << 29)
#define RT2560_TX_CIPHER_TKIP		(3 << 29)
#define RT2560_TX_CIPHER_AES		(4 << 29)

#define RT2560_TX_RETRYCNT(v)	(((v) >> 5) & 0x7)

	uint32_t	physaddr;
	uint16_t	wme;
#define RT2560_LOGCWMAX(x)	(((x) & 0xf) << 12)
#define RT2560_LOGCWMIN(x)	(((x) & 0xf) << 8)
#define RT2560_AIFSN(x)		(((x) & 0x3) << 6)
#define RT2560_IVOFFSET(x)	(((x) & 0x3f))

	uint16_t	reserved1;
	uint8_t		plcp_signal;
	uint8_t		plcp_service;
#define RT2560_PLCP_LENGEXT	0x80

	uint8_t		plcp_length_lo;
	uint8_t		plcp_length_hi;
	uint32_t	iv;
	uint32_t	eiv;
	uint8_t		key[IEEE80211_KEYBUF_SIZE];
	uint32_t	reserved2[2];
} __packed;

/* Rx descriptor */
struct rt2560_rx_desc {
	uint32_t	flags;
#define RT2560_RX_BUSY		(1 << 0)
#define RT2560_RX_CRC_ERROR	(1 << 5)
#define RT2560_RX_OFDM		(1 << 6)
#define RT2560_RX_PHY_ERROR	(1 << 7)
#define RT2560_RX_CIPHER_BUSY	(1 << 8)
#define RT2560_RX_ICV_ERROR	(1 << 9)

#define RT2560_RX_CIPHER_MASK	0xe0000000
#define RT2560_RX_CIPHER_NONE	(0 << 29)
#define RT2560_RX_CIPHER_WEP40	(1 << 29)
#define RT2560_RX_CIPHER_WEP104	(2 << 29)
#define RT2560_RX_CIPHER_TKIP	(3 << 29)
#define RT2560_RX_CIPHER_AES	(4 << 29)

	uint32_t	physaddr;
	uint8_t		rate;
	uint8_t		rssi;
	uint8_t		ta[IEEE80211_ADDR_LEN];
	uint32_t	iv;
	uint32_t	eiv;
	uint8_t		key[IEEE80211_KEYBUF_SIZE];
	uint32_t	reserved[2];
} __packed;

#define RAL_RF1	0
#define RAL_RF2	2
#define RAL_RF3	1
#define RAL_RF4	3

#define RT2560_RF1_AUTOTUNE	0x08000
#define RT2560_RF3_AUTOTUNE	0x00040

#define RT2560_BBP_BUSY		(1 << 15)
#define RT2560_BBP_WRITE	(1 << 16)
#define RT2560_RF_20BIT		(20 << 24)
#define RT2560_RF_BUSY		(1U << 31)

#define RT2560_RF_2522	0x00
#define RT2560_RF_2523	0x01
#define RT2560_RF_2524	0x02
#define RT2560_RF_2525	0x03
#define RT2560_RF_2525E	0x04
#define RT2560_RF_2526	0x05
/* dual-band RF */
#define RT2560_RF_5222	0x10

#define RT2560_BBP_VERSION	0
#define RT2560_BBP_TX		2
#define RT2560_BBP_RX		14

#define RT2560_BBP_ANTA		0x00
#define RT2560_BBP_DIVERSITY	0x01
#define RT2560_BBP_ANTB		0x02
#define RT2560_BBP_ANTMASK	0x03
#define RT2560_BBP_FLIPIQ	0x04

#define RT2560_LED_MODE_DEFAULT		0
#define RT2560_LED_MODE_TXRX_ACTIVITY	1
#define RT2560_LED_MODE_SINGLE		2
#define RT2560_LED_MODE_ASUS		3

#define RT2560_JAPAN_FILTER	0x8

#define RT2560_EEPROM_DELAY	1	/* minimum hold time (microsecond) */

#define RT2560_EEPROM_CONFIG0	16
#define RT2560_EEPROM_BBP_BASE	19
#define RT2560_EEPROM_TXPOWER	35
#define RT2560_EEPROM_CALIBRATE	62

/*
 * control and status registers access macros
 */
#define RAL_READ(sc, reg)						\
	bus_space_read_4((sc)->sc_st, (sc)->sc_sh, (reg))

#define RAL_WRITE(sc, reg, val)						\
	bus_space_write_4((sc)->sc_st, (sc)->sc_sh, (reg), (val))

/*
 * EEPROM access macro
 */
#define RT2560_EEPROM_CTL(sc, val) do {					\
	RAL_WRITE((sc), RT2560_CSR21, (val));				\
	DELAY(RT2560_EEPROM_DELAY);					\
} while (/* CONSTCOND */0)

/*
 * Default values for MAC registers; values taken from the reference driver.
 */
#define RT2560_DEF_MAC				\
	{ RT2560_PSCSR0,      0x00020002 },	\
	{ RT2560_PSCSR1,      0x00000002 },	\
	{ RT2560_PSCSR2,      0x00020002 },	\
	{ RT2560_PSCSR3,      0x00000002 },	\
	{ RT2560_TIMECSR,     0x00003f21 },	\
	{ RT2560_CSR9,        0x00000780 },	\
	{ RT2560_CSR11,       0x07041483 },	\
	{ RT2560_CNT3,        0x00000000 },	\
	{ RT2560_TXCSR1,      0x07614562 },	\
	{ RT2560_ARSP_PLCP_0, 0x8c8d8b8a },	\
	{ RT2560_ACKPCTCSR,   0x7038140a },	\
	{ RT2560_ARTCSR1,     0x21212929 },	\
	{ RT2560_ARTCSR2,     0x1d1d1d1d },	\
	{ RT2560_RXCSR0,      0xffffffff },	\
	{ RT2560_RXCSR3,      0xb3aab3af },	\
	{ RT2560_PCICSR,      0x000003b8 },	\
	{ RT2560_PWRCSR0,     0x3f3b3100 },	\
	{ RT2560_GPIOCSR,     0x0000ff00 },	\
	{ RT2560_TESTCSR,     0x000000f0 },	\
	{ RT2560_PWRCSR1,     0x000001ff },	\
	{ RT2560_MACCSR0,     0x00213223 },	\
	{ RT2560_MACCSR1,     0x00235518 },	\
	{ RT2560_RLPWCSR,     0x00000040 },	\
	{ RT2560_RALINKCSR,   0x9a009a11 },	\
	{ RT2560_CSR7,        0xffffffff },	\
	{ RT2560_BBPCSR1,     0x82188200 },	\
	{ RT2560_TXACKCSR0,   0x00000020 },	\
	{ RT2560_SECCSR3,     0x0000e78f }

/*
 * Default values for BBP registers; values taken from the reference driver.
 */
#define RT2560_DEF_BBP	\
	{  3, 0x02 },	\
	{  4, 0x19 },	\
	{ 14, 0x1c },	\
	{ 15, 0x30 },	\
	{ 16, 0xac },	\
	{ 17, 0x48 },	\
	{ 18, 0x18 },	\
	{ 19, 0xff },	\
	{ 20, 0x1e },	\
	{ 21, 0x08 },	\
	{ 22, 0x08 },	\
	{ 23, 0x08 },	\
	{ 24, 0x80 },	\
	{ 25, 0x50 },	\
	{ 26, 0x08 },	\
	{ 27, 0x23 },	\
	{ 30, 0x10 },	\
	{ 31, 0x2b },	\
	{ 32, 0xb9 },	\
	{ 34, 0x12 },	\
	{ 35, 0x50 },	\
	{ 39, 0xc4 },	\
	{ 40, 0x02 },	\
	{ 41, 0x60 },	\
	{ 53, 0x10 },	\
	{ 54, 0x18 },	\
	{ 56, 0x08 },	\
	{ 57, 0x10 },	\
	{ 58, 0x08 },	\
	{ 61, 0x60 },	\
	{ 62, 0x10 },	\
	{ 75, 0xff }

/*
 * Default values for RF register R2 indexed by channel numbers; values taken
 * from the reference driver.
 */
#define RT2560_RF2522_R2						\
{									\
	0x307f6, 0x307fb, 0x30800, 0x30805, 0x3080a, 0x3080f, 0x30814,	\
	0x30819, 0x3081e, 0x30823, 0x30828, 0x3082d, 0x30832, 0x3083e	\
}

#define RT2560_RF2523_R2						\
{									\
	0x00327, 0x00328, 0x00329, 0x0032a, 0x0032b, 0x0032c, 0x0032d,	\
	0x0032e, 0x0032f, 0x00340, 0x00341, 0x00342, 0x00343, 0x00346	\
}

#define RT2560_RF2524_R2						\
{									\
	0x00327, 0x00328, 0x00329, 0x0032a, 0x0032b, 0x0032c, 0x0032d,	\
	0x0032e, 0x0032f, 0x00340, 0x00341, 0x00342, 0x00343, 0x00346	\
}

#define RT2560_RF2525_R2						\
{									\
	0x20327, 0x20328, 0x20329, 0x2032a, 0x2032b, 0x2032c, 0x2032d,	\
	0x2032e, 0x2032f, 0x20340, 0x20341, 0x20342, 0x20343, 0x20346	\
}

#define RT2560_RF2525_HI_R2						\
{									\
	0x2032f, 0x20340, 0x20341, 0x20342, 0x20343, 0x20344, 0x20345,	\
	0x20346, 0x20347, 0x20348, 0x20349, 0x2034a, 0x2034b, 0x2034e	\
}

#define RT2560_RF2525E_R2						\
{									\
	0x2044d, 0x2044e, 0x2044f, 0x20460, 0x20461, 0x20462, 0x20463,	\
	0x20464, 0x20465, 0x20466, 0x20467, 0x20468, 0x20469, 0x2046b	\
}

#define RT2560_RF2526_HI_R2						\
{									\
	0x0022a, 0x0022b, 0x0022b, 0x0022c, 0x0022c, 0x0022d, 0x0022d,	\
	0x0022e, 0x0022e, 0x0022f, 0x0022d, 0x00240, 0x00240, 0x00241	\
}

#define RT2560_RF2526_R2						\
{									\
	0x00226, 0x00227, 0x00227, 0x00228, 0x00228, 0x00229, 0x00229,	\
	0x0022a, 0x0022a, 0x0022b, 0x0022b, 0x0022c, 0x0022c, 0x0022d	\
}

/*
 * For dual-band RF, RF registers R1 and R4 also depend on channel number;
 * values taken from the reference driver.
 */
#define RT2560_RF5222				\
	{   1, 0x08808, 0x0044d, 0x00282 },	\
	{   2, 0x08808, 0x0044e, 0x00282 },	\
	{   3, 0x08808, 0x0044f, 0x00282 },	\
	{   4, 0x08808, 0x00460, 0x00282 },	\
	{   5, 0x08808, 0x00461, 0x00282 },	\
	{   6, 0x08808, 0x00462, 0x00282 },	\
	{   7, 0x08808, 0x00463, 0x00282 },	\
	{   8, 0x08808, 0x00464, 0x00282 },	\
	{   9, 0x08808, 0x00465, 0x00282 },	\
	{  10, 0x08808, 0x00466, 0x00282 },	\
	{  11, 0x08808, 0x00467, 0x00282 },	\
	{  12, 0x08808, 0x00468, 0x00282 },	\
	{  13, 0x08808, 0x00469, 0x00282 },	\
	{  14, 0x08808, 0x0046b, 0x00286 },	\
						\
	{  36, 0x08804, 0x06225, 0x00287 },	\
	{  40, 0x08804, 0x06226, 0x00287 },	\
	{  44, 0x08804, 0x06227, 0x00287 },	\
	{  48, 0x08804, 0x06228, 0x00287 },	\
	{  52, 0x08804, 0x06229, 0x00287 },	\
	{  56, 0x08804, 0x0622a, 0x00287 },	\
	{  60, 0x08804, 0x0622b, 0x00287 },	\
	{  64, 0x08804, 0x0622c, 0x00287 },	\
						\
	{ 100, 0x08804, 0x02200, 0x00283 },	\
	{ 104, 0x08804, 0x02201, 0x00283 },	\
	{ 108, 0x08804, 0x02202, 0x00283 },	\
	{ 112, 0x08804, 0x02203, 0x00283 },	\
	{ 116, 0x08804, 0x02204, 0x00283 },	\
	{ 120, 0x08804, 0x02205, 0x00283 },	\
	{ 124, 0x08804, 0x02206, 0x00283 },	\
	{ 128, 0x08804, 0x02207, 0x00283 },	\
	{ 132, 0x08804, 0x02208, 0x00283 },	\
	{ 136, 0x08804, 0x02209, 0x00283 },	\
	{ 140, 0x08804, 0x0220a, 0x00283 },	\
						\
	{ 149, 0x08808, 0x02429, 0x00281 },	\
	{ 153, 0x08808, 0x0242b, 0x00281 },	\
	{ 157, 0x08808, 0x0242d, 0x00281 },	\
	{ 161, 0x08808, 0x0242f, 0x00281 }
