/*	$OpenBSD: if_zydreg.h,v 1.19 2006/11/30 19:28:07 damien Exp $	*/
/*	$NetBSD: if_zydreg.h,v 1.2 2007/06/16 11:18:45 kiyohara Exp $	*/
/*	$FreeBSD$	*/

/*-
 * Copyright (c) 2006 by Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2006 by Florian Stoehr <ich@florian-stoehr.de>
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

/*
 * ZyDAS ZD1211/ZD1211B USB WLAN driver.
 */

#define ZYD_CR_GPI_EN		0x9418
#define ZYD_CR_RADIO_PD		0x942c
#define ZYD_CR_RF2948_PD	0x942c
#define ZYD_CR_EN_PS_MANUAL_AGC	0x943c
#define ZYD_CR_CONFIG_PHILIPS	0x9440
#define ZYD_CR_I2C_WRITE	0x9444
#define ZYD_CR_SA2400_SER_RP	0x9448
#define ZYD_CR_RADIO_PE		0x9458
#define ZYD_CR_RST_BUS_MASTER	0x945c
#define ZYD_CR_RFCFG		0x9464
#define ZYD_CR_HSTSCHG		0x946c
#define ZYD_CR_PHY_ON		0x9474
#define ZYD_CR_RX_DELAY		0x9478
#define ZYD_CR_RX_PE_DELAY	0x947c
#define ZYD_CR_GPIO_1		0x9490
#define ZYD_CR_GPIO_2		0x9494
#define ZYD_CR_EnZYD_CRyBufMux	0x94a8
#define ZYD_CR_PS_CTRL		0x9500
#define ZYD_CR_ADDA_PWR_DWN	0x9504
#define ZYD_CR_ADDA_MBIAS_WT	0x9508
#define ZYD_CR_INTERRUPT	0x9510
#define ZYD_CR_MAC_PS_STATE	0x950c
#define ZYD_CR_ATIM_WND_PERIOD	0x951c
#define ZYD_CR_BCN_INTERVAL	0x9520
#define ZYD_CR_PRE_TBTT		0x9524

/*
 * MAC registers.
 */
#define ZYD_MAC_MACADRL		0x9610 /* MAC address (low) */
#define ZYD_MAC_MACADRH		0x9614 /* MAC address (high) */
#define ZYD_MAC_BSSADRL		0x9618 /* BSS address (low) */
#define ZYD_MAC_BSSADRH		0x961c /* BSS address (high) */
#define ZYD_MAC_BCNCFG		0x9620 /* BCN configuration */
#define ZYD_MAC_GHTBL		0x9624 /* Group hash table (low) */
#define ZYD_MAC_GHTBH		0x9628 /* Group hash table (high) */
#define ZYD_MAC_RX_TIMEOUT	0x962c /* Rx timeout value */
#define ZYD_MAC_BAS_RATE	0x9630 /* Basic rate setting */
#define ZYD_MAC_MAN_RATE	0x9634 /* Mandatory rate setting */
#define ZYD_MAC_RTSCTSRATE	0x9638 /* RTS CTS rate */
#define ZYD_MAC_BACKOFF_PROTECT	0x963c /* Backoff protection */
#define ZYD_MAC_RX_THRESHOLD	0x9640 /* Rx threshold */
#define ZYD_MAC_TX_PE_CONTROL	0x9644 /* Tx_PE control */
#define ZYD_MAC_AFTER_PNP	0x9648 /* After PnP */
#define ZYD_MAC_RX_PE_DELAY	0x964c /* Rx_pe delay */
#define ZYD_MAC_RX_ADDR2_L	0x9650 /* RX address2 (low)    */
#define ZYD_MAC_RX_ADDR2_H	0x9654 /* RX address2 (high) */
#define ZYD_MAC_SIFS_ACK_TIME	0x9658 /* Dynamic SIFS ack time */
#define ZYD_MAC_PHY_DELAY	0x9660 /* PHY delay */
#define ZYD_MAC_PHY_DELAY2	0x966c /* PHY delay */
#define ZYD_MAC_BCNFIFO		0x9670 /* Beacon FIFO I/O port */
#define ZYD_MAC_SNIFFER		0x9674 /* Sniffer on/off */
#define ZYD_MAC_ENCRYPTION_TYPE 0x9678 /* Encryption type */
#define ZYD_MAC_RETRY		0x967c /* Retry time */
#define ZYD_MAC_MISC		0x9680 /* Misc */
#define ZYD_MAC_STMACHINESTAT	0x9684 /* State machine status */
#define ZYD_MAC_TX_UNDERRUN_CNT	0x9688 /* TX underrun counter */
#define ZYD_MAC_RXFILTER	0x968c /* Send to host settings */
#define ZYD_MAC_ACK_EXT		0x9690 /* Acknowledge extension */
#define ZYD_MAC_BCNFIFOST	0x9694 /* BCN FIFO set and status */
#define ZYD_MAC_DIFS_EIFS_SIFS	0x9698 /* DIFS, EIFS & SIFS settings */
#define ZYD_MAC_RX_TIMEOUT_CNT	0x969c /* RX timeout count */
#define ZYD_MAC_RX_TOTAL_FRAME	0x96a0 /* RX total frame count */
#define ZYD_MAC_RX_CRC32_CNT	0x96a4 /* RX CRC32 frame count */
#define ZYD_MAC_RX_CRC16_CNT	0x96a8 /* RX CRC16 frame count */
#define ZYD_MAC_RX_UDEC		0x96ac /* RX unicast decr. error count */
#define ZYD_MAC_RX_OVERRUN_CNT	0x96b0 /* RX FIFO overrun count */
#define ZYD_MAC_RX_MDEC		0x96bc /* RX multicast decr. err. cnt. */
#define ZYD_MAC_NAV_TCR		0x96c4 /* NAV timer count read */
#define ZYD_MAC_BACKOFF_ST_RD	0x96c8 /* Backoff status read */
#define ZYD_MAC_DM_RETRY_CNT_RD	0x96cc /* DM retry count read */
#define ZYD_MAC_RX_ACR		0x96d0 /* RX arbitration count read    */
#define ZYD_MAC_TX_CCR		0x96d4 /* Tx complete count read */
#define ZYD_MAC_TCB_ADDR	0x96e8 /* Current PCI process TCP addr */
#define ZYD_MAC_RCB_ADDR	0x96ec /* Next RCB address */
#define ZYD_MAC_CONT_WIN_LIMIT	0x96f0 /* Contention window limit */
#define ZYD_MAC_TX_PKT		0x96f4 /* Tx total packet count read */
#define ZYD_MAC_DL_CTRL		0x96f8 /* Download control */
#define ZYD_MAC_CAM_MODE	0x9700 /* CAM: Continuous Access Mode */
#define ZYD_MACB_TXPWR_CTL1	0x9b00
#define ZYD_MACB_TXPWR_CTL2	0x9b04
#define ZYD_MACB_TXPWR_CTL3	0x9b08
#define ZYD_MACB_TXPWR_CTL4	0x9b0c
#define ZYD_MACB_AIFS_CTL1	0x9b10
#define ZYD_MACB_AIFS_CTL2	0x9b14
#define ZYD_MACB_TXOP		0x9b20
#define ZYD_MACB_MAX_RETRY	0x9b28

/*
 * Miscellaneous registers.
 */
#define ZYD_FIRMWARE_START_ADDR	0xee00
#define ZYD_FIRMWARE_BASE_ADDR	0xee1d /* Firmware base address */

/*
 * EEPROM registers.
 */
#define ZYD_EEPROM_START_HEAD	0xf800 /* EEPROM start */
#define ZYD_EEPROM_SUBID	0xf817
#define ZYD_EEPROM_POD		0xf819
#define ZYD_EEPROM_MAC_ADDR_P1	0xf81b /* Part 1 of the MAC address */
#define ZYD_EEPROM_MAC_ADDR_P2	0xf81d /* Part 2 of the MAC address */
#define ZYD_EEPROM_PWR_CAL	0xf81f /* Calibration */
#define ZYD_EEPROM_PWR_INT	0xf827 /* Calibration */
#define ZYD_EEPROM_ALLOWEDCHAN	0xf82f /* Allowed CH mask, 1 bit each */
#define ZYD_EEPROM_DEVICE_VER	0xf837 /* Device version */
#define ZYD_EEPROM_PHY_REG	0xf83c /* PHY registers */
#define ZYD_EEPROM_36M_CAL	0xf83f /* Calibration */
#define ZYD_EEPROM_11A_INT	0xf847 /* Interpolation */
#define ZYD_EEPROM_48M_CAL	0xf84f /* Calibration */
#define ZYD_EEPROM_48M_INT	0xf857 /* Interpolation */
#define ZYD_EEPROM_54M_CAL	0xf85f /* Calibration */
#define ZYD_EEPROM_54M_INT	0xf867 /* Interpolation */

/*
 * Firmware registers offsets (relative to fwbase).
 */
#define ZYD_FW_FIRMWARE_REV	0x0000 /* Firmware version */
#define ZYD_FW_USB_SPEED	0x0001 /* USB speed (!=0 if highspeed) */
#define ZYD_FW_FIX_TX_RATE	0x0002 /* Fixed TX rate */
#define ZYD_FW_LINK_STATUS	0x0003
#define ZYD_FW_SOFT_RESET	0x0004
#define ZYD_FW_FLASH_CHK	0x0005

/* possible flags for register ZYD_FW_LINK_STATUS */
#define ZYD_LED1		(1 << 8)
#define ZYD_LED2		(1 << 9)

/*
 * RF IDs.
 */
#define ZYD_RF_UW2451		0x2	/* not supported yet */
#define ZYD_RF_UCHIP		0x3	/* not supported yet */
#define ZYD_RF_AL2230		0x4
#define ZYD_RF_AL7230B		0x5
#define ZYD_RF_THETA		0x6	/* not supported yet */
#define ZYD_RF_AL2210		0x7
#define ZYD_RF_MAXIM_NEW	0x8
#define ZYD_RF_GCT		0x9
#define ZYD_RF_AL2230S		0xa	/* not supported yet */
#define ZYD_RF_RALINK		0xb	/* not supported yet */
#define ZYD_RF_INTERSIL		0xc	/* not supported yet */
#define ZYD_RF_RFMD		0xd
#define ZYD_RF_MAXIM_NEW2	0xe
#define ZYD_RF_PHILIPS		0xf	/* not supported yet */

/*
 * PHY registers (8 bits, not documented).
 */
#define ZYD_CR0			0x9000
#define ZYD_CR1			0x9004
#define ZYD_CR2			0x9008
#define ZYD_CR3			0x900c
#define ZYD_CR5			0x9010
#define ZYD_CR6			0x9014
#define ZYD_CR7			0x9018
#define ZYD_CR8			0x901c
#define ZYD_CR4			0x9020
#define ZYD_CR9			0x9024
#define ZYD_CR10		0x9028
#define ZYD_CR11		0x902c
#define ZYD_CR12		0x9030
#define ZYD_CR13		0x9034
#define ZYD_CR14		0x9038
#define ZYD_CR15		0x903c
#define ZYD_CR16		0x9040
#define ZYD_CR17		0x9044
#define ZYD_CR18		0x9048
#define ZYD_CR19		0x904c
#define ZYD_CR20		0x9050
#define ZYD_CR21		0x9054
#define ZYD_CR22		0x9058
#define ZYD_CR23		0x905c
#define ZYD_CR24		0x9060
#define ZYD_CR25		0x9064
#define ZYD_CR26		0x9068
#define ZYD_CR27		0x906c
#define ZYD_CR28		0x9070
#define ZYD_CR29		0x9074
#define ZYD_CR30		0x9078
#define ZYD_CR31		0x907c
#define ZYD_CR32		0x9080
#define ZYD_CR33		0x9084
#define ZYD_CR34		0x9088
#define ZYD_CR35		0x908c
#define ZYD_CR36		0x9090
#define ZYD_CR37		0x9094
#define ZYD_CR38		0x9098
#define ZYD_CR39		0x909c
#define ZYD_CR40		0x90a0
#define ZYD_CR41		0x90a4
#define ZYD_CR42		0x90a8
#define ZYD_CR43		0x90ac
#define ZYD_CR44		0x90b0
#define ZYD_CR45		0x90b4
#define ZYD_CR46		0x90b8
#define ZYD_CR47		0x90bc
#define ZYD_CR48		0x90c0
#define ZYD_CR49		0x90c4
#define ZYD_CR50		0x90c8
#define ZYD_CR51		0x90cc
#define ZYD_CR52		0x90d0
#define ZYD_CR53		0x90d4
#define ZYD_CR54		0x90d8
#define ZYD_CR55		0x90dc
#define ZYD_CR56		0x90e0
#define ZYD_CR57		0x90e4
#define ZYD_CR58		0x90e8
#define ZYD_CR59		0x90ec
#define ZYD_CR60		0x90f0
#define ZYD_CR61		0x90f4
#define ZYD_CR62		0x90f8
#define ZYD_CR63		0x90fc
#define ZYD_CR64		0x9100
#define ZYD_CR65		0x9104
#define ZYD_CR66		0x9108
#define ZYD_CR67		0x910c
#define ZYD_CR68		0x9110
#define ZYD_CR69		0x9114
#define ZYD_CR70		0x9118
#define ZYD_CR71		0x911c
#define ZYD_CR72		0x9120
#define ZYD_CR73		0x9124
#define ZYD_CR74		0x9128
#define ZYD_CR75		0x912c
#define ZYD_CR76		0x9130
#define ZYD_CR77		0x9134
#define ZYD_CR78		0x9138
#define ZYD_CR79		0x913c
#define ZYD_CR80		0x9140
#define ZYD_CR81		0x9144
#define ZYD_CR82		0x9148
#define ZYD_CR83		0x914c
#define ZYD_CR84		0x9150
#define ZYD_CR85		0x9154
#define ZYD_CR86		0x9158
#define ZYD_CR87		0x915c
#define ZYD_CR88		0x9160
#define ZYD_CR89		0x9164
#define ZYD_CR90		0x9168
#define ZYD_CR91		0x916c
#define ZYD_CR92		0x9170
#define ZYD_CR93		0x9174
#define ZYD_CR94		0x9178
#define ZYD_CR95		0x917c
#define ZYD_CR96		0x9180
#define ZYD_CR97		0x9184
#define ZYD_CR98		0x9188
#define ZYD_CR99		0x918c
#define ZYD_CR100		0x9190
#define ZYD_CR101		0x9194
#define ZYD_CR102		0x9198
#define ZYD_CR103		0x919c
#define ZYD_CR104		0x91a0
#define ZYD_CR105		0x91a4
#define ZYD_CR106		0x91a8
#define ZYD_CR107		0x91ac
#define ZYD_CR108		0x91b0
#define ZYD_CR109		0x91b4
#define ZYD_CR110		0x91b8
#define ZYD_CR111		0x91bc
#define ZYD_CR112		0x91c0
#define ZYD_CR113		0x91c4
#define ZYD_CR114		0x91c8
#define ZYD_CR115		0x91cc
#define ZYD_CR116		0x91d0
#define ZYD_CR117		0x91d4
#define ZYD_CR118		0x91d8
#define ZYD_CR119		0x91dc
#define ZYD_CR120		0x91e0
#define ZYD_CR121		0x91e4
#define ZYD_CR122		0x91e8
#define ZYD_CR123		0x91ec
#define ZYD_CR124		0x91f0
#define ZYD_CR125		0x91f4
#define ZYD_CR126		0x91f8
#define ZYD_CR127		0x91fc
#define ZYD_CR128		0x9200
#define ZYD_CR129		0x9204
#define ZYD_CR130		0x9208
#define ZYD_CR131		0x920c
#define ZYD_CR132		0x9210
#define ZYD_CR133		0x9214
#define ZYD_CR134		0x9218
#define ZYD_CR135		0x921c
#define ZYD_CR136		0x9220
#define ZYD_CR137		0x9224
#define ZYD_CR138		0x9228
#define ZYD_CR139		0x922c
#define ZYD_CR140		0x9230
#define ZYD_CR141		0x9234
#define ZYD_CR142		0x9238
#define ZYD_CR143		0x923c
#define ZYD_CR144		0x9240
#define ZYD_CR145		0x9244
#define ZYD_CR146		0x9248
#define ZYD_CR147		0x924c
#define ZYD_CR148		0x9250
#define ZYD_CR149		0x9254
#define ZYD_CR150		0x9258
#define ZYD_CR151		0x925c
#define ZYD_CR152		0x9260
#define ZYD_CR153		0x9264
#define ZYD_CR154		0x9268
#define ZYD_CR155		0x926c
#define ZYD_CR156		0x9270
#define ZYD_CR157		0x9274
#define ZYD_CR158		0x9278
#define ZYD_CR159		0x927c
#define ZYD_CR160		0x9280
#define ZYD_CR161		0x9284
#define ZYD_CR162		0x9288
#define ZYD_CR163		0x928c
#define ZYD_CR164		0x9290
#define ZYD_CR165		0x9294
#define ZYD_CR166		0x9298
#define ZYD_CR167		0x929c
#define ZYD_CR168		0x92a0
#define ZYD_CR169		0x92a4
#define ZYD_CR170		0x92a8
#define ZYD_CR171		0x92ac
#define ZYD_CR172		0x92b0
#define ZYD_CR173		0x92b4
#define ZYD_CR174		0x92b8
#define ZYD_CR175		0x92bc
#define ZYD_CR176		0x92c0
#define ZYD_CR177		0x92c4
#define ZYD_CR178		0x92c8
#define ZYD_CR179		0x92cc
#define ZYD_CR180		0x92d0
#define ZYD_CR181		0x92d4
#define ZYD_CR182		0x92d8
#define ZYD_CR183		0x92dc
#define ZYD_CR184		0x92e0
#define ZYD_CR185		0x92e4
#define ZYD_CR186		0x92e8
#define ZYD_CR187		0x92ec
#define ZYD_CR188		0x92f0
#define ZYD_CR189		0x92f4
#define ZYD_CR190		0x92f8
#define ZYD_CR191		0x92fc
#define ZYD_CR192		0x9300
#define ZYD_CR193		0x9304
#define ZYD_CR194		0x9308
#define ZYD_CR195		0x930c
#define ZYD_CR196		0x9310
#define ZYD_CR197		0x9314
#define ZYD_CR198		0x9318
#define ZYD_CR199		0x931c
#define ZYD_CR200		0x9320
#define ZYD_CR201		0x9324
#define ZYD_CR202		0x9328
#define ZYD_CR203		0x932c
#define ZYD_CR204		0x9330
#define ZYD_CR205		0x9334
#define ZYD_CR206		0x9338
#define ZYD_CR207		0x933c
#define ZYD_CR208		0x9340
#define ZYD_CR209		0x9344
#define ZYD_CR210		0x9348
#define ZYD_CR211		0x934c
#define ZYD_CR212		0x9350
#define ZYD_CR213		0x9354
#define ZYD_CR214		0x9358
#define ZYD_CR215		0x935c
#define ZYD_CR216		0x9360
#define ZYD_CR217		0x9364
#define ZYD_CR218		0x9368
#define ZYD_CR219		0x936c
#define ZYD_CR220		0x9370
#define ZYD_CR221		0x9374
#define ZYD_CR222		0x9378
#define ZYD_CR223		0x937c
#define ZYD_CR224		0x9380
#define ZYD_CR225		0x9384
#define ZYD_CR226		0x9388
#define ZYD_CR227		0x938c
#define ZYD_CR228		0x9390
#define ZYD_CR229		0x9394
#define ZYD_CR230		0x9398
#define ZYD_CR231		0x939c
#define ZYD_CR232		0x93a0
#define ZYD_CR233		0x93a4
#define ZYD_CR234		0x93a8
#define ZYD_CR235		0x93ac
#define ZYD_CR236		0x93b0
#define ZYD_CR240		0x93c0
#define ZYD_CR241		0x93c4
#define ZYD_CR242		0x93c8
#define ZYD_CR243		0x93cc
#define ZYD_CR244		0x93d0
#define ZYD_CR245		0x93d4
#define ZYD_CR251		0x93ec
#define ZYD_CR252		0x93f0
#define ZYD_CR253		0x93f4
#define ZYD_CR254		0x93f8
#define ZYD_CR255		0x93fc

/* copied nearly verbatim from the Linux driver rewrite */
#define	ZYD_DEF_PHY							\
{									\
	{ ZYD_CR0,   0x0a }, { ZYD_CR1,   0x06 }, { ZYD_CR2,   0x26 },	\
	{ ZYD_CR3,   0x38 }, { ZYD_CR4,   0x80 }, { ZYD_CR9,   0xa0 },	\
	{ ZYD_CR10,  0x81 }, { ZYD_CR11,  0x00 }, { ZYD_CR12,  0x7f },	\
	{ ZYD_CR13,  0x8c }, { ZYD_CR14,  0x80 }, { ZYD_CR15,  0x3d },	\
	{ ZYD_CR16,  0x20 }, { ZYD_CR17,  0x1e }, { ZYD_CR18,  0x0a },	\
	{ ZYD_CR19,  0x48 }, { ZYD_CR20,  0x0c }, { ZYD_CR21,  0x0c },	\
	{ ZYD_CR22,  0x23 }, { ZYD_CR23,  0x90 }, { ZYD_CR24,  0x14 },	\
	{ ZYD_CR25,  0x40 }, { ZYD_CR26,  0x10 }, { ZYD_CR27,  0x19 },	\
	{ ZYD_CR28,  0x7f }, { ZYD_CR29,  0x80 }, { ZYD_CR30,  0x4b },	\
	{ ZYD_CR31,  0x60 }, { ZYD_CR32,  0x43 }, { ZYD_CR33,  0x08 },	\
	{ ZYD_CR34,  0x06 }, { ZYD_CR35,  0x0a }, { ZYD_CR36,  0x00 },	\
	{ ZYD_CR37,  0x00 }, { ZYD_CR38,  0x38 }, { ZYD_CR39,  0x0c },	\
	{ ZYD_CR40,  0x84 }, { ZYD_CR41,  0x2a }, { ZYD_CR42,  0x80 },	\
	{ ZYD_CR43,  0x10 }, { ZYD_CR44,  0x12 }, { ZYD_CR46,  0xff },	\
	{ ZYD_CR47,  0x1e }, { ZYD_CR48,  0x26 }, { ZYD_CR49,  0x5b },	\
	{ ZYD_CR64,  0xd0 }, { ZYD_CR65,  0x04 }, { ZYD_CR66,  0x58 },	\
	{ ZYD_CR67,  0xc9 }, { ZYD_CR68,  0x88 }, { ZYD_CR69,  0x41 },	\
	{ ZYD_CR70,  0x23 }, { ZYD_CR71,  0x10 }, { ZYD_CR72,  0xff },	\
	{ ZYD_CR73,  0x32 }, { ZYD_CR74,  0x30 }, { ZYD_CR75,  0x65 },	\
	{ ZYD_CR76,  0x41 }, { ZYD_CR77,  0x1b }, { ZYD_CR78,  0x30 },	\
	{ ZYD_CR79,  0x68 }, { ZYD_CR80,  0x64 }, { ZYD_CR81,  0x64 },	\
	{ ZYD_CR82,  0x00 }, { ZYD_CR83,  0x00 }, { ZYD_CR84,  0x00 },	\
	{ ZYD_CR85,  0x02 }, { ZYD_CR86,  0x00 }, { ZYD_CR87,  0x00 },	\
	{ ZYD_CR88,  0xff }, { ZYD_CR89,  0xfc }, { ZYD_CR90,  0x00 },	\
	{ ZYD_CR91,  0x00 }, { ZYD_CR92,  0x00 }, { ZYD_CR93,  0x08 },	\
	{ ZYD_CR94,  0x00 }, { ZYD_CR95,  0x00 }, { ZYD_CR96,  0xff },	\
	{ ZYD_CR97,  0xe7 }, { ZYD_CR98,  0x00 }, { ZYD_CR99,  0x00 },	\
	{ ZYD_CR100, 0x00 }, { ZYD_CR101, 0xae }, { ZYD_CR102, 0x02 },	\
	{ ZYD_CR103, 0x00 }, { ZYD_CR104, 0x03 }, { ZYD_CR105, 0x65 },	\
	{ ZYD_CR106, 0x04 }, { ZYD_CR107, 0x00 }, { ZYD_CR108, 0x0a },	\
	{ ZYD_CR109, 0xaa }, { ZYD_CR110, 0xaa }, { ZYD_CR111, 0x25 },	\
	{ ZYD_CR112, 0x25 }, { ZYD_CR113, 0x00 }, { ZYD_CR119, 0x1e },	\
	{ ZYD_CR125, 0x90 }, { ZYD_CR126, 0x00 }, { ZYD_CR127, 0x00 },	\
	{ ZYD_CR5,   0x00 }, { ZYD_CR6,   0x00 }, { ZYD_CR7,   0x00 },	\
	{ ZYD_CR8,   0x00 }, { ZYD_CR9,   0x20 }, { ZYD_CR12,  0xf0 },	\
	{ ZYD_CR20,  0x0e }, { ZYD_CR21,  0x0e }, { ZYD_CR27,  0x10 },	\
	{ ZYD_CR44,  0x33 }, { ZYD_CR47,  0x1E }, { ZYD_CR83,  0x24 },	\
	{ ZYD_CR84,  0x04 }, { ZYD_CR85,  0x00 }, { ZYD_CR86,  0x0C },	\
	{ ZYD_CR87,  0x12 }, { ZYD_CR88,  0x0C }, { ZYD_CR89,  0x00 },	\
	{ ZYD_CR90,  0x10 }, { ZYD_CR91,  0x08 }, { ZYD_CR93,  0x00 },	\
	{ ZYD_CR94,  0x01 }, { ZYD_CR95,  0x00 }, { ZYD_CR96,  0x50 },	\
	{ ZYD_CR97,  0x37 }, { ZYD_CR98,  0x35 }, { ZYD_CR101, 0x13 },	\
	{ ZYD_CR102, 0x27 }, { ZYD_CR103, 0x27 }, { ZYD_CR104, 0x18 },	\
	{ ZYD_CR105, 0x12 }, { ZYD_CR109, 0x27 }, { ZYD_CR110, 0x27 },	\
	{ ZYD_CR111, 0x27 }, { ZYD_CR112, 0x27 }, { ZYD_CR113, 0x27 },	\
	{ ZYD_CR114, 0x27 }, { ZYD_CR115, 0x26 }, { ZYD_CR116, 0x24 },	\
	{ ZYD_CR117, 0xfc }, { ZYD_CR118, 0xfa }, { ZYD_CR120, 0x4f },	\
	{ ZYD_CR125, 0xaa }, { ZYD_CR127, 0x03 }, { ZYD_CR128, 0x14 },	\
	{ ZYD_CR129, 0x12 }, { ZYD_CR130, 0x10 }, { ZYD_CR131, 0x0C },	\
	{ ZYD_CR136, 0xdf }, { ZYD_CR137, 0x40 }, { ZYD_CR138, 0xa0 },	\
	{ ZYD_CR139, 0xb0 }, { ZYD_CR140, 0x99 }, { ZYD_CR141, 0x82 },	\
	{ ZYD_CR142, 0x54 }, { ZYD_CR143, 0x1c }, { ZYD_CR144, 0x6c },	\
	{ ZYD_CR147, 0x07 }, { ZYD_CR148, 0x4c }, { ZYD_CR149, 0x50 },	\
	{ ZYD_CR150, 0x0e }, { ZYD_CR151, 0x18 }, { ZYD_CR160, 0xfe },	\
	{ ZYD_CR161, 0xee }, { ZYD_CR162, 0xaa }, { ZYD_CR163, 0xfa },	\
	{ ZYD_CR164, 0xfa }, { ZYD_CR165, 0xea }, { ZYD_CR166, 0xbe },	\
	{ ZYD_CR167, 0xbe }, { ZYD_CR168, 0x6a }, { ZYD_CR169, 0xba },	\
	{ ZYD_CR170, 0xba }, { ZYD_CR171, 0xba }, { ZYD_CR204, 0x7d },	\
	{ ZYD_CR203, 0x30 }, { 0, 0}					\
}

#define ZYD_DEF_PHYB							\
{									\
	{ ZYD_CR0,   0x14 }, { ZYD_CR1,   0x06 }, { ZYD_CR2,   0x26 },	\
	{ ZYD_CR3,   0x38 }, { ZYD_CR4,   0x80 }, { ZYD_CR9,   0xe0 },	\
	{ ZYD_CR10,  0x81 }, { ZYD_CR11,  0x00 }, { ZYD_CR12,  0xf0 },	\
	{ ZYD_CR13,  0x8c }, { ZYD_CR14,  0x80 }, { ZYD_CR15,  0x3d },	\
	{ ZYD_CR16,  0x20 }, { ZYD_CR17,  0x1e }, { ZYD_CR18,  0x0a },	\
	{ ZYD_CR19,  0x48 }, { ZYD_CR20,  0x10 }, { ZYD_CR21,  0x0e },	\
	{ ZYD_CR22,  0x23 }, { ZYD_CR23,  0x90 }, { ZYD_CR24,  0x14 },	\
	{ ZYD_CR25,  0x40 }, { ZYD_CR26,  0x10 }, { ZYD_CR27,  0x10 },	\
	{ ZYD_CR28,  0x7f }, { ZYD_CR29,  0x80 }, { ZYD_CR30,  0x4b },	\
	{ ZYD_CR31,  0x60 }, { ZYD_CR32,  0x43 }, { ZYD_CR33,  0x08 },	\
	{ ZYD_CR34,  0x06 }, { ZYD_CR35,  0x0a }, { ZYD_CR36,  0x00 },	\
	{ ZYD_CR37,  0x00 }, { ZYD_CR38,  0x38 }, { ZYD_CR39,  0x0c },	\
	{ ZYD_CR40,  0x84 }, { ZYD_CR41,  0x2a }, { ZYD_CR42,  0x80 },	\
	{ ZYD_CR43,  0x10 }, { ZYD_CR44,  0x33 }, { ZYD_CR46,  0xff },	\
	{ ZYD_CR47,  0x1E }, { ZYD_CR48,  0x26 }, { ZYD_CR49,  0x5b },	\
	{ ZYD_CR64,  0xd0 }, { ZYD_CR65,  0x04 }, { ZYD_CR66,  0x58 },	\
	{ ZYD_CR67,  0xc9 }, { ZYD_CR68,  0x88 }, { ZYD_CR69,  0x41 },	\
	{ ZYD_CR70,  0x23 }, { ZYD_CR71,  0x10 }, { ZYD_CR72,  0xff },	\
	{ ZYD_CR73,  0x32 }, { ZYD_CR74,  0x30 }, { ZYD_CR75,  0x65 },	\
	{ ZYD_CR76,  0x41 }, { ZYD_CR77,  0x1b }, { ZYD_CR78,  0x30 },	\
	{ ZYD_CR79,  0xf0 }, { ZYD_CR80,  0x64 }, { ZYD_CR81,  0x64 },	\
	{ ZYD_CR82,  0x00 }, { ZYD_CR83,  0x24 }, { ZYD_CR84,  0x04 },	\
	{ ZYD_CR85,  0x00 }, { ZYD_CR86,  0x0c }, { ZYD_CR87,  0x12 },	\
	{ ZYD_CR88,  0x0c }, { ZYD_CR89,  0x00 }, { ZYD_CR90,  0x58 },	\
	{ ZYD_CR91,  0x04 }, { ZYD_CR92,  0x00 }, { ZYD_CR93,  0x00 },	\
	{ ZYD_CR94,  0x01 }, { ZYD_CR95,  0x20 }, { ZYD_CR96,  0x50 },	\
	{ ZYD_CR97,  0x37 }, { ZYD_CR98,  0x35 }, { ZYD_CR99,  0x00 },	\
	{ ZYD_CR100, 0x01 }, { ZYD_CR101, 0x13 }, { ZYD_CR102, 0x27 },	\
	{ ZYD_CR103, 0x27 }, { ZYD_CR104, 0x18 }, { ZYD_CR105, 0x12 },	\
	{ ZYD_CR106, 0x04 }, { ZYD_CR107, 0x00 }, { ZYD_CR108, 0x0a },	\
	{ ZYD_CR109, 0x27 }, { ZYD_CR110, 0x27 }, { ZYD_CR111, 0x27 },	\
	{ ZYD_CR112, 0x27 }, { ZYD_CR113, 0x27 }, { ZYD_CR114, 0x27 },	\
	{ ZYD_CR115, 0x26 }, { ZYD_CR116, 0x24 }, { ZYD_CR117, 0xfc },	\
	{ ZYD_CR118, 0xfa }, { ZYD_CR119, 0x1e }, { ZYD_CR125, 0x90 },	\
	{ ZYD_CR126, 0x00 }, { ZYD_CR127, 0x00 }, { ZYD_CR128, 0x14 },	\
	{ ZYD_CR129, 0x12 }, { ZYD_CR130, 0x10 }, { ZYD_CR131, 0x0c },	\
	{ ZYD_CR136, 0xdf }, { ZYD_CR137, 0xa0 }, { ZYD_CR138, 0xa8 },	\
	{ ZYD_CR139, 0xb4 }, { ZYD_CR140, 0x98 }, { ZYD_CR141, 0x82 },	\
	{ ZYD_CR142, 0x53 }, { ZYD_CR143, 0x1c }, { ZYD_CR144, 0x6c },	\
	{ ZYD_CR147, 0x07 }, { ZYD_CR148, 0x40 }, { ZYD_CR149, 0x40 },	\
	{ ZYD_CR150, 0x14 }, { ZYD_CR151, 0x18 }, { ZYD_CR159, 0x70 },	\
	{ ZYD_CR160, 0xfe }, { ZYD_CR161, 0xee }, { ZYD_CR162, 0xaa },	\
	{ ZYD_CR163, 0xfa }, { ZYD_CR164, 0xfa }, { ZYD_CR165, 0xea },	\
	{ ZYD_CR166, 0xbe }, { ZYD_CR167, 0xbe }, { ZYD_CR168, 0x6a },	\
	{ ZYD_CR169, 0xba }, { ZYD_CR170, 0xba }, { ZYD_CR171, 0xba },	\
	{ ZYD_CR204, 0x7d }, { ZYD_CR203, 0x30 },			\
	{ 0, 0 }							\
}

#define ZYD_RFMD_PHY							\
{									\
	{ ZYD_CR2,   0x1e }, { ZYD_CR9,   0x20 }, { ZYD_CR10,  0x89 },	\
	{ ZYD_CR11,  0x00 }, { ZYD_CR15,  0xd0 }, { ZYD_CR17,  0x68 },	\
	{ ZYD_CR19,  0x4a }, { ZYD_CR20,  0x0c }, { ZYD_CR21,  0x0e },	\
	{ ZYD_CR23,  0x48 }, { ZYD_CR24,  0x14 }, { ZYD_CR26,  0x90 },	\
	{ ZYD_CR27,  0x30 }, { ZYD_CR29,  0x20 }, { ZYD_CR31,  0xb2 },	\
	{ ZYD_CR32,  0x43 }, { ZYD_CR33,  0x28 }, { ZYD_CR38,  0x30 },	\
	{ ZYD_CR34,  0x0f }, { ZYD_CR35,  0xf0 }, { ZYD_CR41,  0x2a },	\
	{ ZYD_CR46,  0x7f }, { ZYD_CR47,  0x1e }, { ZYD_CR51,  0xc5 },	\
	{ ZYD_CR52,  0xc5 }, { ZYD_CR53,  0xc5 }, { ZYD_CR79,  0x58 },	\
	{ ZYD_CR80,  0x30 }, { ZYD_CR81,  0x30 }, { ZYD_CR82,  0x00 },	\
	{ ZYD_CR83,  0x24 }, { ZYD_CR84,  0x04 }, { ZYD_CR85,  0x00 },	\
	{ ZYD_CR86,  0x10 }, { ZYD_CR87,  0x2a }, { ZYD_CR88,  0x10 },	\
	{ ZYD_CR89,  0x24 }, { ZYD_CR90,  0x18 }, { ZYD_CR91,  0x00 },	\
	{ ZYD_CR92,  0x0a }, { ZYD_CR93,  0x00 }, { ZYD_CR94,  0x01 },	\
	{ ZYD_CR95,  0x00 }, { ZYD_CR96,  0x40 }, { ZYD_CR97,  0x37 },	\
	{ ZYD_CR98,  0x05 }, { ZYD_CR99,  0x28 }, { ZYD_CR100, 0x00 },	\
	{ ZYD_CR101, 0x13 }, { ZYD_CR102, 0x27 }, { ZYD_CR103, 0x27 },	\
	{ ZYD_CR104, 0x18 }, { ZYD_CR105, 0x12 }, { ZYD_CR106, 0x1a },	\
	{ ZYD_CR107, 0x24 }, { ZYD_CR108, 0x0a }, { ZYD_CR109, 0x13 },	\
	{ ZYD_CR110, 0x2f }, { ZYD_CR111, 0x27 }, { ZYD_CR112, 0x27 },	\
	{ ZYD_CR113, 0x27 }, { ZYD_CR114, 0x27 }, { ZYD_CR115, 0x40 },	\
	{ ZYD_CR116, 0x40 }, { ZYD_CR117, 0xf0 }, { ZYD_CR118, 0xf0 },	\
	{ ZYD_CR119, 0x16 }, { ZYD_CR122, 0x00 }, { ZYD_CR127, 0x03 },	\
	{ ZYD_CR131, 0x08 }, { ZYD_CR138, 0x28 }, { ZYD_CR148, 0x44 },	\
	{ ZYD_CR150, 0x10 }, { ZYD_CR169, 0xbb }, { ZYD_CR170, 0xbb }	\
}

#define ZYD_RFMD_RF							\
{									\
	0x000007, 0x07dd43, 0x080959, 0x0e6666, 0x116a57, 0x17dd43,	\
	0x1819f9, 0x1e6666, 0x214554, 0x25e7fa, 0x27fffa, 0x294128,	\
	0x2c0000, 0x300000, 0x340000, 0x381e0f, 0x6c180f		\
}

#define ZYD_RFMD_CHANTABLE	\
{				\
	{ 0x181979, 0x1e6666 },	\
	{ 0x181989, 0x1e6666 },	\
	{ 0x181999, 0x1e6666 },	\
	{ 0x1819a9, 0x1e6666 },	\
	{ 0x1819b9, 0x1e6666 },	\
	{ 0x1819c9, 0x1e6666 },	\
	{ 0x1819d9, 0x1e6666 },	\
	{ 0x1819e9, 0x1e6666 },	\
	{ 0x1819f9, 0x1e6666 },	\
	{ 0x181a09, 0x1e6666 },	\
	{ 0x181a19, 0x1e6666 },	\
	{ 0x181a29, 0x1e6666 },	\
	{ 0x181a39, 0x1e6666 },	\
	{ 0x181a60, 0x1c0000 }	\
}

#define ZYD_AL2230_PHY							\
{									\
	{ ZYD_CR15,  0x20 }, { ZYD_CR23,  0x40 }, { ZYD_CR24,  0x20 },	\
	{ ZYD_CR26,  0x11 }, { ZYD_CR28,  0x3e }, { ZYD_CR29,  0x00 },	\
	{ ZYD_CR44,  0x33 }, { ZYD_CR106, 0x2a }, { ZYD_CR107, 0x1a },	\
	{ ZYD_CR109, 0x09 }, { ZYD_CR110, 0x27 }, { ZYD_CR111, 0x2b },	\
	{ ZYD_CR112, 0x2b }, { ZYD_CR119, 0x0a }, { ZYD_CR10,  0x89 },	\
	{ ZYD_CR17,  0x28 }, { ZYD_CR26,  0x93 }, { ZYD_CR34,  0x30 },	\
	{ ZYD_CR35,  0x3e }, { ZYD_CR41,  0x24 }, { ZYD_CR44,  0x32 },	\
	{ ZYD_CR46,  0x96 }, { ZYD_CR47,  0x1e }, { ZYD_CR79,  0x58 },	\
	{ ZYD_CR80,  0x30 }, { ZYD_CR81,  0x30 }, { ZYD_CR87,  0x0a },	\
	{ ZYD_CR89,  0x04 }, { ZYD_CR92,  0x0a }, { ZYD_CR99,  0x28 },	\
	{ ZYD_CR100, 0x00 }, { ZYD_CR101, 0x13 }, { ZYD_CR102, 0x27 },	\
	{ ZYD_CR106, 0x24 }, { ZYD_CR107, 0x2a }, { ZYD_CR109, 0x09 },	\
	{ ZYD_CR110, 0x13 }, { ZYD_CR111, 0x1f }, { ZYD_CR112, 0x1f },	\
	{ ZYD_CR113, 0x27 }, { ZYD_CR114, 0x27 }, { ZYD_CR115, 0x24 },	\
	{ ZYD_CR116, 0x24 }, { ZYD_CR117, 0xf4 }, { ZYD_CR118, 0xfc },	\
	{ ZYD_CR119, 0x10 }, { ZYD_CR120, 0x4f }, { ZYD_CR121, 0x77 },	\
	{ ZYD_CR122, 0xe0 }, { ZYD_CR137, 0x88 }, { ZYD_CR252, 0xff },	\
	{ ZYD_CR253, 0xff }, { ZYD_CR251, 0x2f }, { ZYD_CR251, 0x3f },	\
	{ ZYD_CR138, 0x28 }, { ZYD_CR203, 0x06 } 			\
}

#define ZYD_AL2230_PHY_B						\
{									\
	{ ZYD_CR10,  0x89 }, { ZYD_CR15,  0x20 }, { ZYD_CR17,  0x2B },	\
	{ ZYD_CR23,  0x40 }, { ZYD_CR24,  0x20 }, { ZYD_CR26,  0x93 },	\
	{ ZYD_CR28,  0x3e }, { ZYD_CR29,  0x00 }, { ZYD_CR33,  0x28 },	\
	{ ZYD_CR34,  0x30 }, { ZYD_CR35,  0x3e }, { ZYD_CR41,  0x24 },	\
	{ ZYD_CR44,  0x32 }, { ZYD_CR46,  0x99 }, { ZYD_CR47,  0x1e },	\
	{ ZYD_CR48,  0x06 }, { ZYD_CR49,  0xf9 }, { ZYD_CR51,  0x01 },	\
	{ ZYD_CR52,  0x80 }, { ZYD_CR53,  0x7e }, { ZYD_CR65,  0x00 },	\
	{ ZYD_CR66,  0x00 }, { ZYD_CR67,  0x00 }, { ZYD_CR68,  0x00 },	\
	{ ZYD_CR69,  0x28 }, { ZYD_CR79,  0x58 }, { ZYD_CR80,  0x30 },	\
	{ ZYD_CR81,  0x30 }, { ZYD_CR87,  0x0a }, { ZYD_CR89,  0x04 },	\
	{ ZYD_CR91,  0x00 }, { ZYD_CR92,  0x0a }, { ZYD_CR98,  0x8d },	\
	{ ZYD_CR99,  0x00 }, { ZYD_CR101, 0x13 }, { ZYD_CR102, 0x27 },	\
	{ ZYD_CR106, 0x24 }, { ZYD_CR107, 0x2a }, { ZYD_CR109, 0x13 },	\
	{ ZYD_CR110, 0x1f }, { ZYD_CR111, 0x1f }, { ZYD_CR112, 0x1f },	\
	{ ZYD_CR113, 0x27 }, { ZYD_CR114, 0x27 }, { ZYD_CR115, 0x26 },	\
	{ ZYD_CR116, 0x24 }, { ZYD_CR117, 0xfa }, { ZYD_CR118, 0xfa },	\
	{ ZYD_CR119, 0x10 }, { ZYD_CR120, 0x4f }, { ZYD_CR121, 0x6c },	\
	{ ZYD_CR122, 0xfc }, { ZYD_CR123, 0x57 }, { ZYD_CR125, 0xad },	\
	{ ZYD_CR126, 0x6c }, { ZYD_CR127, 0x03 }, { ZYD_CR137, 0x50 },	\
	{ ZYD_CR138, 0xa8 }, { ZYD_CR144, 0xac }, { ZYD_CR150, 0x0d },	\
	{ ZYD_CR252, 0x34 }, { ZYD_CR253, 0x34 }			\
}

#define ZYD_AL2230_PHY_PART1						\
{									\
	{ ZYD_CR240, 0x57 }, { ZYD_CR9,   0xe0 }			\
}

#define ZYD_AL2230_PHY_PART2						\
{									\
	{ ZYD_CR251, 0x2f }, { ZYD_CR251, 0x7f },			\
}

#define ZYD_AL2230_PHY_PART3						\
{									\
	{ ZYD_CR128, 0x14 }, { ZYD_CR129, 0x12 }, { ZYD_CR130, 0x10 },	\
}

#define	ZYD_AL2230S_PHY_INIT						\
{									\
	{ ZYD_CR47,  0x1e }, { ZYD_CR106, 0x22 }, { ZYD_CR107, 0x2a },	\
	{ ZYD_CR109, 0x13 }, { ZYD_CR118, 0xf8 }, { ZYD_CR119, 0x12 },	\
	{ ZYD_CR122, 0xe0 }, { ZYD_CR128, 0x10 }, { ZYD_CR129, 0x0e },	\
	{ ZYD_CR130, 0x10 }						\
}

#define	ZYD_AL2230_PHY_FINI_PART1					\
{									\
	{ ZYD_CR80,  0x30 }, { ZYD_CR81,  0x30 }, { ZYD_CR79,  0x58 },	\
	{ ZYD_CR12,  0xf0 }, { ZYD_CR77,  0x1b }, { ZYD_CR78,  0x58 },	\
	{ ZYD_CR203, 0x06 }, { ZYD_CR240, 0x80 },			\
}

#define ZYD_AL2230_RF_PART1						\
{									\
	0x03f790, 0x033331, 0x00000d, 0x0b3331, 0x03b812, 0x00fff3	\
}

#define ZYD_AL2230_RF_PART2						\
{									\
	0x000da4, 0x0f4dc5, 0x0805b6, 0x011687, 0x000688, 0x0403b9,	\
	0x00dbba, 0x00099b, 0x0bdffc, 0x00000d, 0x00500f		\
}

#define ZYD_AL2230_RF_PART3						\
{									\
	0x00d00f, 0x004c0f, 0x00540f, 0x00700f, 0x00500f		\
}

#define ZYD_AL2230_RF_B							\
{									\
	0x03f790, 0x033331, 0x00000d, 0x0b3331, 0x03b812, 0x00fff3,	\
	0x0005a4, 0x0f4dc5, 0x0805b6, 0x0146c7, 0x000688, 0x0403b9,	\
	0x00dbba, 0x00099b, 0x0bdffc, 0x00000d, 0x00580f		\
}

#define ZYD_AL2230_RF_B_PART1						\
{									\
	0x8cccd0, 0x481dc0, 0xcfff00, 0x25a000				\
}

#define ZYD_AL2230_RF_B_PART2						\
{									\
	0x25a000, 0xa3b2f0, 0x6da010, 0xe36280, 0x116000, 0x9dc020,	\
	0x5ddb00, 0xd99000, 0x3ffbd0, 0xb00000, 0xf01a00		\
}

#define ZYD_AL2230_RF_B_PART3						\
{									\
	0xf01b00, 0xf01e00, 0xf01a00					\
}

#define ZYD_AL2230_CHANTABLE			\
{						\
	{ 0x03f790, 0x033331, 0x00000d },	\
	{ 0x03f790, 0x0b3331, 0x00000d },	\
	{ 0x03e790, 0x033331, 0x00000d },	\
	{ 0x03e790, 0x0b3331, 0x00000d },	\
	{ 0x03f7a0, 0x033331, 0x00000d },	\
	{ 0x03f7a0, 0x0b3331, 0x00000d },	\
	{ 0x03e7a0, 0x033331, 0x00000d },	\
	{ 0x03e7a0, 0x0b3331, 0x00000d },	\
	{ 0x03f7b0, 0x033331, 0x00000d },	\
	{ 0x03f7b0, 0x0b3331, 0x00000d },	\
	{ 0x03e7b0, 0x033331, 0x00000d },	\
	{ 0x03e7b0, 0x0b3331, 0x00000d },	\
	{ 0x03f7c0, 0x033331, 0x00000d },	\
	{ 0x03e7c0, 0x066661, 0x00000d }	\
}

#define ZYD_AL2230_CHANTABLE_B			\
{						\
	{ 0x09efc0, 0x8cccc0, 0xb00000 },	\
	{ 0x09efc0, 0x8cccd0, 0xb00000 },	\
	{ 0x09e7c0, 0x8cccc0, 0xb00000 },	\
	{ 0x09e7c0, 0x8cccd0, 0xb00000 },	\
	{ 0x05efc0, 0x8cccc0, 0xb00000 },	\
	{ 0x05efc0, 0x8cccd0, 0xb00000 },	\
	{ 0x05e7c0, 0x8cccc0, 0xb00000 },	\
	{ 0x05e7c0, 0x8cccd0, 0xb00000 },	\
	{ 0x0defc0, 0x8cccc0, 0xb00000 },	\
	{ 0x0defc0, 0x8cccd0, 0xb00000 },	\
	{ 0x0de7c0, 0x8cccc0, 0xb00000 },	\
	{ 0x0de7c0, 0x8cccd0, 0xb00000 },	\
	{ 0x03efc0, 0x8cccc0, 0xb00000 },	\
	{ 0x03e7c0, 0x866660, 0xb00000 }	\
}

#define ZYD_AL7230B_PHY_1							\
{									\
	{ ZYD_CR240, 0x57 }, { ZYD_CR15,  0x20 }, { ZYD_CR23,  0x40 },	\
	{ ZYD_CR24,  0x20 }, { ZYD_CR26,  0x11 }, { ZYD_CR28,  0x3e },	\
	{ ZYD_CR29,  0x00 }, { ZYD_CR44,  0x33 }, { ZYD_CR106, 0x22 },	\
	{ ZYD_CR107, 0x1a }, { ZYD_CR109, 0x09 }, { ZYD_CR110, 0x27 },	\
	{ ZYD_CR111, 0x2b }, { ZYD_CR112, 0x2b }, { ZYD_CR119, 0x0a },	\
	{ ZYD_CR122, 0xfc }, { ZYD_CR10,  0x89 }, { ZYD_CR17,  0x28 },	\
	{ ZYD_CR26,  0x93 }, { ZYD_CR34,  0x30 }, { ZYD_CR35,  0x3e },	\
	{ ZYD_CR41,  0x24 }, { ZYD_CR44,  0x32 }, { ZYD_CR46,  0x96 },	\
	{ ZYD_CR47,  0x1e }, { ZYD_CR79,  0x58 }, { ZYD_CR80,  0x30 },	\
	{ ZYD_CR81,  0x30 }, { ZYD_CR87,  0x0a }, { ZYD_CR89,  0x04 },	\
	{ ZYD_CR92,  0x0a }, { ZYD_CR99,  0x28 }, { ZYD_CR100, 0x02 },	\
	{ ZYD_CR101, 0x13 }, { ZYD_CR102, 0x27 }, { ZYD_CR106, 0x22 },	\
	{ ZYD_CR107, 0x3f }, { ZYD_CR109, 0x09 }, { ZYD_CR110, 0x1f },	\
	{ ZYD_CR111, 0x1f }, { ZYD_CR112, 0x1f }, { ZYD_CR113, 0x27 },	\
	{ ZYD_CR114, 0x27 }, { ZYD_CR115, 0x24 }, { ZYD_CR116, 0x3f },	\
	{ ZYD_CR117, 0xfa }, { ZYD_CR118, 0xfc }, { ZYD_CR119, 0x10 },	\
	{ ZYD_CR120, 0x4f }, { ZYD_CR121, 0x77 }, { ZYD_CR137, 0x88 },	\
	{ ZYD_CR138, 0xa8 }, { ZYD_CR252, 0x34 }, { ZYD_CR253, 0x34 },	\
	{ ZYD_CR251, 0x2f }						\
}

#define ZYD_AL7230B_PHY_2						\
{									\
	{ ZYD_CR251, 0x3f }, { ZYD_CR128, 0x14 }, { ZYD_CR129, 0x12 },	\
	{ ZYD_CR130, 0x10 }, { ZYD_CR38,  0x38 }, { ZYD_CR136, 0xdf }	\
}

#define ZYD_AL7230B_PHY_3						\
{									\
	{ ZYD_CR203, 0x06 }, { ZYD_CR240, 0x80 }			\
}

#define ZYD_AL7230B_RF_1						\
{									\
	0x09ec04, 0x8cccc8, 0x4ff821, 0xc5fbfc, 0x21ebfe, 0xafd401,	\
	0x6cf56a, 0xe04073, 0x193d76, 0x9dd844, 0x500007, 0xd8c010,	\
	0x3c9000, 0xbfffff, 0x700000, 0xf15d58				\
}

#define ZYD_AL7230B_RF_2						\
{									\
	0xf15d59, 0xf15d5c, 0xf15d58					\
}

#define ZYD_AL7230B_RF_SETCHANNEL					\
{									\
	0x4ff821, 0xc5fbfc, 0x21ebfe, 0xafd401, 0x6cf56a, 0xe04073,	\
	0x193d76, 0x9dd844, 0x500007, 0xd8c010, 0x3c9000, 0xf15d58	\
}

#define ZYD_AL7230B_CHANTABLE	\
{				\
	{ 0x09ec00, 0x8cccc8 },	\
	{ 0x09ec00, 0x8cccd8 },	\
	{ 0x09ec00, 0x8cccc0 },	\
	{ 0x09ec00, 0x8cccd0 },	\
	{ 0x05ec00, 0x8cccc8 },	\
	{ 0x05ec00, 0x8cccd8 },	\
	{ 0x05ec00, 0x8cccc0 },	\
	{ 0x05ec00, 0x8cccd0 },	\
	{ 0x0dec00, 0x8cccc8 },	\
	{ 0x0dec00, 0x8cccd8 },	\
	{ 0x0dec00, 0x8cccc0 },	\
	{ 0x0dec00, 0x8cccd0 },	\
	{ 0x03ec00, 0x8cccc8 },	\
	{ 0x03ec00, 0x866660 }	\
}

#define ZYD_AL2210_PHY							\
{									\
	{ ZYD_CR9,   0xe0 }, { ZYD_CR10, 0x91 }, { ZYD_CR12,  0x90 },	\
	{ ZYD_CR15,  0xd0 }, { ZYD_CR16, 0x40 }, { ZYD_CR17,  0x58 },	\
	{ ZYD_CR18,  0x04 }, { ZYD_CR23, 0x66 }, { ZYD_CR24,  0x14 },	\
	{ ZYD_CR26,  0x90 }, { ZYD_CR31, 0x80 }, { ZYD_CR34,  0x06 },	\
	{ ZYD_CR35,  0x3e }, { ZYD_CR38, 0x38 }, { ZYD_CR46,  0x90 },	\
	{ ZYD_CR47,  0x1e }, { ZYD_CR64, 0x64 }, { ZYD_CR79,  0xb5 },	\
	{ ZYD_CR80,  0x38 }, { ZYD_CR81, 0x30 }, { ZYD_CR113, 0xc0 },	\
	{ ZYD_CR127, 0x03 }						\
}

#define ZYD_AL2210_RF							\
{									\
	0x2396c0, 0x00fcb1, 0x358132, 0x0108b3, 0xc77804, 0x456415,	\
	0xff2226, 0x806667, 0x7860f8, 0xbb01c9, 0x00000a, 0x00000b	\
}

#define ZYD_AL2210_CHANTABLE						\
{									\
	0x0196c0, 0x019710, 0x019760, 0x0197b0,	0x019800, 0x019850,	\
	0x0198a0, 0x0198f0, 0x019940, 0x019990, 0x0199e0, 0x019a30,	\
	0x019a80, 0x019b40 						\
}

#define ZYD_GCT_PHY							\
{									\
	{ ZYD_CR10,  0x89 }, { ZYD_CR15,  0x20 }, { ZYD_CR17,  0x28 },	\
	{ ZYD_CR23,  0x38 }, { ZYD_CR24,  0x20 }, { ZYD_CR26,  0x93 },	\
	{ ZYD_CR27,  0x15 }, { ZYD_CR28,  0x3e }, { ZYD_CR29,  0x00 },	\
	{ ZYD_CR33,  0x28 }, { ZYD_CR34,  0x30 }, { ZYD_CR35,  0x43 },	\
	{ ZYD_CR41,  0x24 }, { ZYD_CR44,  0x32 }, { ZYD_CR46,  0x92 },	\
	{ ZYD_CR47,  0x1e }, { ZYD_CR48,  0x04 }, { ZYD_CR49,  0xfa },	\
	{ ZYD_CR79,  0x58 }, { ZYD_CR80,  0x30 }, { ZYD_CR81,  0x30 },	\
	{ ZYD_CR87,  0x0a }, { ZYD_CR89,  0x04 }, { ZYD_CR91,  0x00 },	\
	{ ZYD_CR92,  0x0a }, { ZYD_CR98,  0x8d }, { ZYD_CR99,  0x28 },	\
	{ ZYD_CR100, 0x02 }, { ZYD_CR101, 0x09 }, { ZYD_CR102, 0x27 },	\
	{ ZYD_CR106, 0x1c }, { ZYD_CR107, 0x1c }, { ZYD_CR109, 0x13 },	\
	{ ZYD_CR110, 0x1f }, { ZYD_CR111, 0x13 }, { ZYD_CR112, 0x1f },	\
	{ ZYD_CR113, 0x27 }, { ZYD_CR114, 0x23 }, { ZYD_CR115, 0x24 },	\
	{ ZYD_CR116, 0x24 }, { ZYD_CR117, 0xfa }, { ZYD_CR118, 0xf0 },	\
	{ ZYD_CR119, 0x1a }, { ZYD_CR120, 0x4f }, { ZYD_CR121, 0x1f },	\
	{ ZYD_CR122, 0xf0 }, { ZYD_CR123, 0x57 }, { ZYD_CR125, 0xad },	\
	{ ZYD_CR126, 0x6c }, { ZYD_CR127, 0x03 }, { ZYD_CR128, 0x14 },	\
	{ ZYD_CR129, 0x12 }, { ZYD_CR130, 0x10 }, { ZYD_CR137, 0x50 },	\
	{ ZYD_CR138, 0xa8 }, { ZYD_CR144, 0xac }, { ZYD_CR146, 0x20 },	\
	{ ZYD_CR252, 0xff }, { ZYD_CR253, 0xff }			\
}

#define ZYD_GCT_RF							\
{									\
	0x40002b, 0x519e4f, 0x6f81ad, 0x73fffe, 0x25f9c, 0x100047,	\
	0x200999, 0x307602, 0x346063,					\
}

#define	ZYD_GCT_VCO							\
{									\
	{ 0x664d, 0x604d, 0x6675, 0x6475, 0x6655, 0x6455, 0x6665 },	\
	{ 0x666d, 0x606d, 0x664d, 0x644d, 0x6675, 0x6475, 0x6655 },	\
	{ 0x665d, 0x605d, 0x666d, 0x646d, 0x664d, 0x644d, 0x6675 },	\
	{ 0x667d, 0x607d, 0x665d, 0x645d, 0x666d, 0x646d, 0x664d },	\
	{ 0x6643, 0x6043, 0x667d, 0x647d, 0x665d, 0x645d, 0x666d },	\
	{ 0x6663, 0x6063, 0x6643, 0x6443, 0x667d, 0x647d, 0x665d },	\
	{ 0x6653, 0x6053, 0x6663, 0x6463, 0x6643, 0x6443, 0x667d },	\
	{ 0x6673, 0x6073, 0x6653, 0x6453, 0x6663, 0x6463, 0x6643 },	\
	{ 0x664b, 0x604b, 0x6673, 0x6473, 0x6653, 0x6453, 0x6663 },	\
	{ 0x666b, 0x606b, 0x664b, 0x644b, 0x6673, 0x6473, 0x6653 },	\
	{ 0x665b, 0x605b, 0x666b, 0x646b, 0x664b, 0x644b, 0x6673 }	\
}

#define	ZYD_GCT_TXGAIN							\
{									\
	0x0e313, 0x0fb13, 0x0e093, 0x0f893, 0x0ea93, 0x1f093, 0x1f493,	\
	0x1f693, 0x1f393, 0x1f35b, 0x1e6db, 0x1ff3f, 0x1ffff, 0x361d7,	\
	0x37fbf, 0x3ff8b, 0x3ff33, 0x3fb3f, 0x3ffff			\
}

#define	ZYD_GCT_CHANNEL_ACAL						\
{									\
	0x106847, 0x106847, 0x106867, 0x106867, 0x106867, 0x106867,	\
	0x106857, 0x106857, 0x106857, 0x106857, 0x106877, 0x106877,	\
	0x106877, 0x10684f						\
}

#define	ZYD_GCT_CHANNEL_STD						\
{									\
	0x100047, 0x100047, 0x100067, 0x100067, 0x100067, 0x100067,	\
	0x100057, 0x100057, 0x100057, 0x100057, 0x100077, 0x100077,	\
	0x100077, 0x10004f						\
}

#define	ZYD_GCT_CHANNEL_DIV						\
{									\
	0x200999, 0x20099b, 0x200998, 0x20099a, 0x200999, 0x20099b,	\
	0x200998, 0x20099a, 0x200999, 0x20099b, 0x200998, 0x20099a,	\
	0x200999, 0x200ccc						\
}

#define ZYD_MAXIM2_PHY							\
{									\
	{ ZYD_CR23,  0x40 }, { ZYD_CR15,  0x20 }, { ZYD_CR28,  0x3e },	\
	{ ZYD_CR29,  0x00 }, { ZYD_CR26,  0x11 }, { ZYD_CR44,  0x33 },	\
	{ ZYD_CR106, 0x2a }, { ZYD_CR107, 0x1a }, { ZYD_CR109, 0x2b },	\
	{ ZYD_CR110, 0x2b }, { ZYD_CR111, 0x2b }, { ZYD_CR112, 0x2b },	\
	{ ZYD_CR10,  0x89 }, { ZYD_CR17,  0x20 }, { ZYD_CR26,  0x93 },	\
	{ ZYD_CR34,  0x30 }, { ZYD_CR35,  0x40 }, { ZYD_CR41,  0x24 },	\
	{ ZYD_CR44,  0x32 }, { ZYD_CR46,  0x90 }, { ZYD_CR89,  0x18 },	\
	{ ZYD_CR92,  0x0a }, { ZYD_CR101, 0x13 }, { ZYD_CR102, 0x27 },	\
	{ ZYD_CR106, 0x20 }, { ZYD_CR107, 0x24 }, { ZYD_CR109, 0x09 },	\
	{ ZYD_CR110, 0x13 }, { ZYD_CR111, 0x13 }, { ZYD_CR112, 0x13 },	\
	{ ZYD_CR113, 0x27 }, { ZYD_CR114, 0x27 }, { ZYD_CR115, 0x24 },	\
	{ ZYD_CR116, 0x24 }, { ZYD_CR117, 0xf4 }, { ZYD_CR118, 0xfa },	\
	{ ZYD_CR120, 0x4f }, { ZYD_CR121, 0x77 }, { ZYD_CR122, 0xfe },	\
	{ ZYD_CR10,  0x89 }, { ZYD_CR17,  0x20 }, { ZYD_CR26,  0x93 },	\
	{ ZYD_CR34,  0x30 }, { ZYD_CR35,  0x40 }, { ZYD_CR41,  0x24 },	\
	{ ZYD_CR44,  0x32 }, { ZYD_CR46,  0x90 }, { ZYD_CR79,  0x58 },	\
	{ ZYD_CR80,  0x30 }, { ZYD_CR81,  0x30 }, { ZYD_CR89,  0x18 },	\
	{ ZYD_CR92,  0x0a }, { ZYD_CR101, 0x13 }, { ZYD_CR102, 0x27 },	\
	{ ZYD_CR106, 0x20 }, { ZYD_CR107, 0x24 }, { ZYD_CR109, 0x09 },	\
	{ ZYD_CR110, 0x13 }, { ZYD_CR111, 0x13 }, { ZYD_CR112, 0x13 },	\
	{ ZYD_CR113, 0x27 }, { ZYD_CR114, 0x27 }, { ZYD_CR115, 0x24 },	\
	{ ZYD_CR116, 0x24 }, { ZYD_CR117, 0xf4 }, { ZYD_CR118, 0x00 },	\
	{ ZYD_CR120, 0x4f }, { ZYD_CR121, 0x06 }, { ZYD_CR122, 0xfe }	\
}

#define ZYD_MAXIM2_RF							\
{									\
	0x33334, 0x10a03, 0x00400, 0x00ca1, 0x10072, 0x18645, 0x04006,	\
	0x000a7, 0x08258, 0x03fc9, 0x0040a, 0x0000b, 0x0026c		\
}

#define ZYD_MAXIM2_CHANTABLE_F						\
{									\
	0x33334, 0x08884, 0x1ddd4, 0x33334, 0x08884, 0x1ddd4, 0x33334,	\
	0x08884, 0x1ddd4, 0x33334, 0x08884, 0x1ddd4, 0x33334, 0x26664	\
}

#define ZYD_MAXIM2_CHANTABLE	\
{				\
	{ 0x33334, 0x10a03 },	\
	{ 0x08884, 0x20a13 },	\
	{ 0x1ddd4, 0x30a13 },	\
	{ 0x33334, 0x10a13 },	\
	{ 0x08884, 0x20a23 },	\
	{ 0x1ddd4, 0x30a23 },	\
	{ 0x33334, 0x10a23 },	\
	{ 0x08884, 0x20a33 },	\
	{ 0x1ddd4, 0x30a33 },	\
	{ 0x33334, 0x10a33 },	\
	{ 0x08884, 0x20a43 },	\
	{ 0x1ddd4, 0x30a43 },	\
	{ 0x33334, 0x10a43 },	\
	{ 0x26664, 0x20a53 }	\
}

#define	ZYD_TX_RATEDIV							\
{									\
	0x1, 0x2, 0xb, 0xb, 0x1, 0x1, 0x1, 0x1, 0x30, 0x18, 0xc, 0x6,	\
	0x36, 0x24, 0x12, 0x9						\
}

/*
 * Control pipe requests.
 */
#define ZYD_DOWNLOADREQ		0x30
#define ZYD_DOWNLOADSTS		0x31
#define	ZYD_READFWDATAREQ	0x32

/* possible values for register ZYD_CR_INTERRUPT */
#define ZYD_HWINT_MASK		0x004f0000

/* possible values for register ZYD_MAC_MISC */
#define ZYD_UNLOCK_PHY_REGS	0x80

/* possible values for register ZYD_MAC_ENCRYPTION_TYPE */
#define ZYD_ENC_SNIFFER		8

/* flags for register ZYD_MAC_RXFILTER */
#define ZYD_FILTER_ASS_REQ	(1 << 0)
#define ZYD_FILTER_ASS_RSP	(1 << 1)
#define ZYD_FILTER_REASS_REQ	(1 << 2)
#define ZYD_FILTER_REASS_RSP	(1 << 3)
#define ZYD_FILTER_PRB_REQ	(1 << 4)
#define ZYD_FILTER_PRB_RSP	(1 << 5)
#define ZYD_FILTER_BCN		(1 << 8)
#define ZYD_FILTER_ATIM		(1 << 9)
#define ZYD_FILTER_DEASS	(1 << 10)
#define ZYD_FILTER_AUTH		(1 << 11)
#define ZYD_FILTER_DEAUTH	(1 << 12)
#define ZYD_FILTER_PS_POLL	(1 << 26)
#define ZYD_FILTER_RTS		(1 << 27)
#define ZYD_FILTER_CTS		(1 << 28)
#define ZYD_FILTER_ACK		(1 << 29)
#define ZYD_FILTER_CFE		(1 << 30)
#define ZYD_FILTER_CFE_A	(1U << 31)

/* helpers for register ZYD_MAC_RXFILTER */
#define ZYD_FILTER_MONITOR	0xffffffff
#define ZYD_FILTER_BSS							\
	(ZYD_FILTER_ASS_REQ | ZYD_FILTER_ASS_RSP |			\
	 ZYD_FILTER_REASS_REQ | ZYD_FILTER_REASS_RSP |			\
	 ZYD_FILTER_PRB_REQ | ZYD_FILTER_PRB_RSP |			\
	 (0x3 << 6) |							\
	 ZYD_FILTER_BCN | ZYD_FILTER_ATIM | ZYD_FILTER_DEASS |		\
	 ZYD_FILTER_AUTH | ZYD_FILTER_DEAUTH |				\
	 (0x7 << 13) |							\
	 ZYD_FILTER_PS_POLL | ZYD_FILTER_ACK)
#define ZYD_FILTER_HOSTAP						\
	(ZYD_FILTER_ASS_REQ | ZYD_FILTER_REASS_REQ |			\
	 ZYD_FILTER_PRB_REQ | ZYD_FILTER_DEASS | ZYD_FILTER_AUTH |	\
	 ZYD_FILTER_DEAUTH | ZYD_FILTER_PS_POLL)

struct zyd_tx_desc {
	uint8_t			phy;
#define ZYD_TX_PHY_SIGNAL(x)	((x) & 0xf)
#define ZYD_TX_PHY_OFDM		(1 << 4)
#define ZYD_TX_PHY_SHPREAMBLE	(1 << 5)	/* CCK */
#define ZYD_TX_PHY_5GHZ		(1 << 5)	/* OFDM */
	uint16_t		len;
	uint8_t			flags;
#define ZYD_TX_FLAG_BACKOFF	(1 << 0)
#define ZYD_TX_FLAG_MULTICAST	(1 << 1)
#define ZYD_TX_FLAG_TYPE(x)	(((x) & 0x3) << 2)
#define ZYD_TX_TYPE_DATA	0
#define ZYD_TX_TYPE_PS_POLL	1
#define ZYD_TX_TYPE_MGMT	2
#define ZYD_TX_TYPE_CTL		3
#define ZYD_TX_FLAG_WAKEUP	(1 << 4)
#define ZYD_TX_FLAG_RTS		(1 << 5)
#define ZYD_TX_FLAG_ENCRYPT	(1 << 6)
#define ZYD_TX_FLAG_CTS_TO_SELF	(1 << 7)
	uint16_t		pktlen;
	uint16_t		plcp_length;
	uint8_t			plcp_service;
#define ZYD_PLCP_LENGEXT	0x80
	uint16_t		nextlen;
} __packed;

struct zyd_plcphdr {
	uint8_t			signal;
	uint8_t			reserved[2];
	uint16_t		service;	/* unaligned! */
} __packed;

struct zyd_rx_stat {
	uint8_t			signal_cck;
	uint8_t			rssi;
	uint8_t			signal_ofdm;
	uint8_t			cipher;
#define ZYD_RX_CIPHER_WEP64	1
#define ZYD_RX_CIPHER_TKIP	2
#define ZYD_RX_CIPHER_AES	4
#define ZYD_RX_CIPHER_WEP128	5
#define ZYD_RX_CIPHER_WEP256	6
#define ZYD_RX_CIPHER_WEP	\
	(ZYD_RX_CIPHER_WEP64 | ZYD_RX_CIPHER_WEP128 | ZYD_RX_CIPHER_WEP256)
	uint8_t			flags;
#define ZYD_RX_OFDM		(1 << 0)
#define ZYD_RX_TIMEOUT		(1 << 1)
#define ZYD_RX_OVERRUN		(1 << 2)
#define ZYD_RX_DECRYPTERR	(1 << 3)
#define ZYD_RX_BADCRC32		(1 << 4)
#define ZYD_RX_NOT2ME		(1 << 5)
#define ZYD_RX_BADCRC16		(1 << 6)
#define ZYD_RX_ERROR		(1 << 7)
} __packed;

/* this structure may be unaligned */
struct zyd_rx_desc {
#define ZYD_MAX_RXFRAMECNT	3
	uWord			len[ZYD_MAX_RXFRAMECNT];
	uWord			tag;
#define ZYD_TAG_MULTIFRAME	0x697e
} __packed;

/* I2C bus alike */
struct zyd_rfwrite_cmd {
	uint16_t		code;
	uint16_t		width;
	uint16_t		bit[32];
#define ZYD_RF_IF_LE		(1 << 1)
#define ZYD_RF_CLK		(1 << 2)
#define ZYD_RF_DATA		(1 << 3)
} __packed;

struct zyd_cmd {
	uint16_t		code;
#define ZYD_CMD_IOWR		0x0021	/* write HMAC or PHY register */
#define ZYD_CMD_IORD		0x0022	/* read HMAC or PHY register */
#define ZYD_CMD_RFCFG		0x0023	/* write RF register */
#define ZYD_NOTIF_IORD		0x9001	/* response for ZYD_CMD_IORD */
#define ZYD_NOTIF_MACINTR	0x9001	/* interrupt notification */
#define ZYD_NOTIF_RETRYSTATUS	0xa001	/* Tx retry notification */
	uint8_t			data[64];
} __packed;

/* structure for command ZYD_CMD_IOWR */
struct zyd_pair {
	uint16_t		reg;
/* helpers macros to read/write 32-bit registers */
#define ZYD_REG32_LO(reg)	(reg)
#define ZYD_REG32_HI(reg)	\
	((reg) + ((((reg) & 0xf000) == 0x9000) ? 2 : 1))
	uint16_t		val;
} __packed;

/* structure for notification ZYD_NOTIF_RETRYSTATUS */
struct zyd_notif_retry {
	uint16_t		rate;
	uint8_t			macaddr[IEEE80211_ADDR_LEN];
	uint16_t		count;
} __packed;

#define ZYD_CONFIG_INDEX	0
#define ZYD_IFACE_INDEX		0

#define ZYD_INTR_TIMEOUT	1000
#define ZYD_TX_TIMEOUT		10000

#define ZYD_MAX_TXBUFSZ	\
	(sizeof(struct zyd_tx_desc) + MCLBYTES)
#define ZYD_MIN_FRAGSZ							\
	(sizeof(struct zyd_plcphdr) + IEEE80211_MIN_LEN + 		\
	 sizeof(struct zyd_rx_stat))
#define ZYD_MIN_RXBUFSZ	ZYD_MIN_FRAGSZ
#define ZYX_MAX_RXBUFSZ							\
	((sizeof (struct zyd_plcphdr) + IEEE80211_MAX_LEN +		\
	  sizeof (struct zyd_rx_stat)) * ZYD_MAX_RXFRAMECNT + 		\
	 sizeof (struct zyd_rx_desc))
#define ZYD_TX_DESC_SIZE	(sizeof (struct zyd_tx_desc))

#define ZYD_RX_LIST_CNT		1
#define ZYD_TX_LIST_CNT		5
#define ZYD_CMD_FLAG_READ	(1 << 0)
#define ZYD_CMD_FLAG_SENT	(1 << 1)

/* quickly determine if a given rate is CCK or OFDM */
#define ZYD_RATE_IS_OFDM(rate)	((rate) >= 12 && (rate) != 22)

struct zyd_phy_pair {
	uint16_t		reg;
	uint8_t			val;
};

struct zyd_mac_pair {
	uint16_t		reg;
	uint32_t		val;
};

struct zyd_tx_data {
	STAILQ_ENTRY(zyd_tx_data)	next;
	struct zyd_softc		*sc;
	struct zyd_tx_desc		desc;
	struct mbuf			*m;
	struct ieee80211_node		*ni;
	int				rate;
};
typedef STAILQ_HEAD(, zyd_tx_data) zyd_txdhead;

struct zyd_rx_data {
	struct mbuf			*m;
	int				rssi;
};

struct zyd_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint8_t			wr_flags;
	uint8_t			wr_rate;
	uint16_t		wr_chan_freq;
	uint16_t		wr_chan_flags;
	int8_t			wr_antsignal;
	int8_t			wr_antnoise;
} __packed __aligned(8);

#define ZYD_RX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL) |			\
	 (1 << IEEE80211_RADIOTAP_DBM_ANTNOISE) |			\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))

struct zyd_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t			wt_flags;
	uint8_t			wt_rate;
	uint16_t		wt_chan_freq;
	uint16_t		wt_chan_flags;
} __packed;

#define ZYD_TX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))

struct zyd_softc;	/* forward declaration */

struct zyd_rf {
	/* RF methods */
	int			(*init)(struct zyd_rf *);
	int			(*switch_radio)(struct zyd_rf *, int);
	int			(*set_channel)(struct zyd_rf *, uint8_t);
	int			(*bandedge6)(struct zyd_rf *,
				    struct ieee80211_channel *);
	/* RF attributes */
	struct zyd_softc	*rf_sc;	/* back-pointer */
	int			width;
	int			idx;	/* for GIT RF */
	int			update_pwr;
};

struct zyd_rq {
	struct zyd_cmd		*cmd;
	const uint16_t		*idata;
	struct zyd_pair		*odata;
	int			ilen;
	int			olen;
	int			flags;
	STAILQ_ENTRY(zyd_rq)	rq;
};

struct zyd_vap {
	struct ieee80211vap	vap;
	int			(*newstate)(struct ieee80211vap *,
				    enum ieee80211_state, int);
};
#define	ZYD_VAP(vap)	((struct zyd_vap *)(vap))

enum {
	ZYD_BULK_WR,
	ZYD_BULK_RD,
	ZYD_INTR_WR,
	ZYD_INTR_RD,
	ZYD_N_TRANSFER = 4,
};

struct zyd_softc {
	struct ieee80211com	sc_ic;
	struct ieee80211_ratectl_tx_status sc_txs;
	struct mbufq		sc_snd;
	device_t		sc_dev;
	struct usb_device	*sc_udev;

	struct usb_xfer	*sc_xfer[ZYD_N_TRANSFER];

	int			sc_flags;
#define	ZYD_FLAG_FWLOADED		(1 << 0)
#define	ZYD_FLAG_INITONCE		(1 << 1)
#define	ZYD_FLAG_INITDONE		(1 << 2)
#define	ZYD_FLAG_DETACHED		(1 << 3)
#define	ZYD_FLAG_RUNNING		(1 << 4)

	struct zyd_rf		sc_rf;

	STAILQ_HEAD(, zyd_rq)	sc_rtx;
	STAILQ_HEAD(, zyd_rq)	sc_rqh;

	uint16_t		sc_fwbase;
	uint8_t			sc_regdomain;
	uint8_t			sc_macrev;
	uint16_t		sc_fwrev;
	uint8_t			sc_rfrev;
	uint8_t			sc_parev;
	uint8_t			sc_al2230s;
	uint8_t			sc_bandedge6;
	uint8_t			sc_newphy;
	uint8_t			sc_cckgain;
	uint8_t			sc_fix_cr157;
	uint8_t			sc_ledtype;
	uint8_t			sc_txled;

	uint32_t		sc_atim_wnd;
	uint32_t		sc_pre_tbtt;
	uint32_t		sc_bcn_int;

	uint8_t			sc_pwrcal[14];
	uint8_t			sc_pwrint[14];
	uint8_t			sc_ofdm36_cal[14];
	uint8_t			sc_ofdm48_cal[14];
	uint8_t			sc_ofdm54_cal[14];
	uint8_t			sc_bssid[IEEE80211_ADDR_LEN];

	struct mtx		sc_mtx;
	struct zyd_tx_data	tx_data[ZYD_TX_LIST_CNT];
	zyd_txdhead		tx_q;
	zyd_txdhead		tx_free;
	int			tx_nfree;
	struct zyd_rx_desc	sc_rx_desc;
	struct zyd_rx_data	sc_rx_data[ZYD_MAX_RXFRAMECNT];
	int			sc_rx_count;

	struct zyd_cmd		sc_ibuf;

	struct zyd_rx_radiotap_header	sc_rxtap;
	struct zyd_tx_radiotap_header	sc_txtap;
};

#define	ZYD_LOCK(sc)		mtx_lock(&(sc)->sc_mtx)
#define	ZYD_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mtx)
#define	ZYD_LOCK_ASSERT(sc, t)	mtx_assert(&(sc)->sc_mtx, t)

