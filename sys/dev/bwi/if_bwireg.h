/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/dev/netif/bwi/if_bwireg.h,v 1.4 2007/10/19 14:27:04 sephe Exp $
 * $FreeBSD$
 */

#ifndef _IF_BWIREG_H
#define _IF_BWIREG_H

/*
 * Registers for all of the register windows
 */
#define BWI_FLAGS			0xf18
#define BWI_FLAGS_INTR_MASK		__BITS(5, 0)

#define BWI_IMSTATE			0xf90
#define BWI_IMSTATE_INBAND_ERR		__BIT(17)
#define BWI_IMSTATE_TIMEOUT		__BIT(18)

#define BWI_INTRVEC			0xf94

#define BWI_STATE_LO			0xf98
#define BWI_STATE_LO_RESET		__BIT(0)
#define BWI_STATE_LO_DISABLE1		__BIT(1)
#define BWI_STATE_LO_DISABLE2		__BIT(2)
#define BWI_STATE_LO_CLOCK		__BIT(16)
#define BWI_STATE_LO_GATED_CLOCK	__BIT(17)
#define BWI_STATE_LO_FLAG_PHYCLKEN	__BIT(0)
#define BWI_STATE_LO_FLAG_PHYRST	__BIT(1)
#define BWI_STATE_LO_FLAG_PHYLNK	__BIT(11)
#define BWI_STATE_LO_FLAGS_MASK		__BITS(29, 18)

#define BWI_STATE_HI			0xf9c
#define BWI_STATE_HI_SERROR		__BIT(0)
#define BWI_STATE_HI_BUSY		__BIT(2)
#define BWI_STATE_HI_FLAG_MAGIC1	0x1
#define BWI_STATE_HI_FLAG_MAGIC2	0x2
#define BWI_STATE_HI_FLAG_64BIT		0x1000
#define BWI_STATE_HI_FLAGS_MASK		__BITS(28, 16)

#define BWI_CONF_LO			0xfa8
#define BWI_CONF_LO_SERVTO_MASK		__BITS(2, 0)	/* service timeout */
#define BWI_CONF_LO_SERVTO		2
#define BWI_CONF_LO_REQTO_MASK		__BITS(6, 4)	/* request timeout */
#define BWI_CONF_LO_REQTO		3


#define BWI_ID_LO			0xff8
#define BWI_ID_LO_BUSREV_MASK		__BITS(31, 28)
/* Bus revision */
#define BWI_BUSREV_0			0
#define BWI_BUSREV_1			1

#define BWI_ID_HI			0xffc
#define BWI_ID_HI_REGWIN_REV(v)		(((v) & 0xf) | (((v) & 0x7000) >> 8))
#define BWI_ID_HI_REGWIN_TYPE(v)	(((v) & 0x8ff0) >> 4)
#define BWI_ID_HI_REGWIN_VENDOR_MASK	__BITS(31, 16)

/*
 * Registers for common register window
 */
#define BWI_INFO			0x0
#define BWI_INFO_BBPID_MASK		__BITS(15, 0)
#define BWI_INFO_BBPREV_MASK		__BITS(19, 16)
#define BWI_INFO_BBPPKG_MASK		__BITS(23, 20)
#define BWI_INFO_NREGWIN_MASK		__BITS(27, 24)

#define BWI_CAPABILITY			0x4
#define BWI_CAP_CLKMODE			__BIT(18)

#define BWI_CONTROL			0x28
#define BWI_CONTROL_MAGIC0		0x3a4
#define BWI_CONTROL_MAGIC1		0xa4
#define BWI_PLL_ON_DELAY		0xb0
#define BWI_FREQ_SEL_DELAY		0xb4

#define BWI_CLOCK_CTRL			0xb8
#define BWI_CLOCK_CTRL_CLKSRC		__BITS(2, 0)
#define BWI_CLOCK_CTRL_SLOW		__BIT(11)
#define BWI_CLOCK_CTRL_IGNPLL		__BIT(12)
#define BWI_CLOCK_CTRL_NODYN		__BIT(13)
#define BWI_CLOCK_CTRL_FDIV		__BITS(31, 16)	/* freq divisor */

/* Possible values for BWI_CLOCK_CTRL_CLKSRC */
#define BWI_CLKSRC_LP_OSC		0	/* Low power oscillator */
#define BWI_CLKSRC_CS_OSC		1	/* Crystal oscillator */
#define BWI_CLKSRC_PCI			2
#define BWI_CLKSRC_MAX			3	/* Maximum of clock source */
/* Min/Max frequency for given clock source */
#define BWI_CLKSRC_LP_OSC_FMIN		25000
#define BWI_CLKSRC_LP_OSC_FMAX		43000
#define BWI_CLKSRC_CS_OSC_FMIN		19800000
#define BWI_CLKSRC_CS_OSC_FMAX		20200000
#define BWI_CLKSRC_PCI_FMIN		25000000
#define BWI_CLKSRC_PCI_FMAX		34000000

#define BWI_CLOCK_INFO			0xc0
#define BWI_CLOCK_INFO_FDIV		__BITS(31, 16)	/* freq divisor */

/*
 * Registers for bus register window
 */
#define BWI_BUS_ADDR			0x50
#define BWI_BUS_ADDR_MAGIC		0xfd8

#define BWI_BUS_DATA			0x54

#define BWI_BUS_CONFIG			0x108
#define BWI_BUS_CONFIG_PREFETCH		__BIT(2)
#define BWI_BUS_CONFIG_BURST		__BIT(3)
#define BWI_BUS_CONFIG_MRM		__BIT(5)

/*
 * Register for MAC
 */
#define BWI_TXRX_INTR_STATUS_BASE	0x20
#define BWI_TXRX_INTR_MASK_BASE		0x24
#define BWI_TXRX_INTR_STATUS(i)		(BWI_TXRX_INTR_STATUS_BASE + ((i) * 8))
#define BWI_TXRX_INTR_MASK(i)		(BWI_TXRX_INTR_MASK_BASE + ((i) * 8))

#define BWI_MAC_STATUS			0x120
#define BWI_MAC_STATUS_ENABLE		__BIT(0)
#define BWI_MAC_STATUS_UCODE_START	__BIT(1)
#define BWI_MAC_STATUS_UCODE_JUMP0	__BIT(2)
#define BWI_MAC_STATUS_IHREN		__BIT(10)
#define BWI_MAC_STATUS_GPOSEL_MASK	__BITS(15, 14)
#define BWI_MAC_STATUS_BSWAP		__BIT(16)
#define BWI_MAC_STATUS_INFRA		__BIT(17)
#define BWI_MAC_STATUS_OPMODE_HOSTAP	__BIT(18)
#define BWI_MAC_STATUS_RFLOCK		__BIT(19)
#define BWI_MAC_STATUS_PASS_BCN		__BIT(20)
#define BWI_MAC_STATUS_PASS_BADPLCP	__BIT(21)
#define BWI_MAC_STATUS_PASS_CTL		__BIT(22)
#define BWI_MAC_STATUS_PASS_BADFCS	__BIT(23)
#define BWI_MAC_STATUS_PROMISC		__BIT(24)
#define BWI_MAC_STATUS_HW_PS		__BIT(25)
#define BWI_MAC_STATUS_WAKEUP		__BIT(26)
#define BWI_MAC_STATUS_PHYLNK		__BIT(31)

#define BWI_MAC_INTR_STATUS		0x128
#define BWI_MAC_INTR_MASK		0x12c

#define BWI_MAC_TMPLT_CTRL		0x130
#define BWI_MAC_TMPLT_DATA		0x134

#define BWI_MAC_PS_STATUS		0x140

#define BWI_MOBJ_CTRL			0x160
#define BWI_MOBJ_CTRL_VAL(objid, ofs)	((objid) << 16 | (ofs))
#define BWI_MOBJ_DATA			0x164
#define BWI_MOBJ_DATA_UNALIGN		0x166
/*
 * Memory object IDs
 */
#define BWI_WR_MOBJ_AUTOINC		0x100	/* Auto-increment wr */
#define BWI_RD_MOBJ_AUTOINC		0x200	/* Auto-increment rd */
/* Firmware ucode object */
#define BWI_FW_UCODE_MOBJ		0x0
/* Common object */
#define BWI_COMM_MOBJ			0x1
#define BWI_COMM_MOBJ_FWREV		0x0
#define BWI_COMM_MOBJ_FWPATCHLV		0x2
#define BWI_COMM_MOBJ_SLOTTIME		0x10
#define BWI_COMM_MOBJ_MACREV		0x16
#define BWI_COMM_MOBJ_TX_ACK		0x22
#define BWI_COMM_MOBJ_UCODE_STATE	0x40
#define BWI_COMM_MOBJ_SHRETRY_FB	0x44
#define BWI_COMM_MOBJ_LGRETEY_FB	0x46
#define BWI_COMM_MOBJ_TX_BEACON		0x54
#define BWI_COMM_MOBJ_KEYTABLE_OFS	0x56
#define BWI_COMM_MOBJ_TSSI_DS		0x58
#define BWI_COMM_MOBJ_HFLAGS_LO		0x5e
#define BWI_COMM_MOBJ_HFLAGS_MI		0x60
#define BWI_COMM_MOBJ_HFLAGS_HI		0x62
#define BWI_COMM_MOBJ_RF_ATTEN		0x64
#define BWI_COMM_MOBJ_RF_NOISE		0x6e
#define BWI_COMM_MOBJ_TSSI_OFDM		0x70
#define BWI_COMM_MOBJ_PROBE_RESP_TO	0x74
#define BWI_COMM_MOBJ_CHAN		0xa0
#define BWI_COMM_MOBJ_KEY_ALGO		0x100
#define BWI_COMM_MOBJ_TX_PROBE_RESP	0x188
#define BWI_HFLAG_AUTO_ANTDIV		0x1ULL
#define BWI_HFLAG_SYM_WA		0x2ULL	/* ??? SYM work around */
#define BWI_HFLAG_PWR_BOOST_DS		0x8ULL
#define BWI_HFLAG_GDC_WA		0x20ULL	/* ??? GDC work around */
#define BWI_HFLAG_OFDM_PA		0x40ULL
#define BWI_HFLAG_NOT_JAPAN		0x80ULL
#define BWI_HFLAG_MAGIC1		0x200ULL
#define BWI_UCODE_STATE_PS		4
#define BWI_LO_TSSI_MASK		__BITS(7, 0)
#define BWI_HI_TSSI_MASK		__BITS(15, 8)
#define BWI_INVALID_TSSI		0x7f
/* 802.11 object */
#define BWI_80211_MOBJ			0x2
#define BWI_80211_MOBJ_CWMIN		0xc
#define BWI_80211_MOBJ_CWMAX		0x10
#define BWI_80211_MOBJ_SHRETRY		0x18
#define BWI_80211_MOBJ_LGRETRY		0x1c
/* Firmware PCM object */
#define BWI_FW_PCM_MOBJ			0x3
/* MAC address of pairwise keys */
#define BWI_PKEY_ADDR_MOBJ		0x4

#define BWI_TXSTATUS0			0x170
#define BWI_TXSTATUS0_VALID		__BIT(0)
#define BWI_TXSTATUS0_ACKED		__BIT(1)
#define BWI_TXSTATUS0_FREASON_MASK	__BITS(4, 2)	/* Failure reason */
#define BWI_TXSTATUS0_AMPDU		__BIT(5)
#define BWI_TXSTATUS0_PENDING		__BIT(6)
#define BWI_TXSTATUS0_PM		__BIT(7)
#define BWI_TXSTATUS0_RTS_TXCNT_MASK	__BITS(11, 8)
#define BWI_TXSTATUS0_DATA_TXCNT_MASK	__BITS(15, 12)
#define BWI_TXSTATUS0_TXID_MASK		__BITS(31, 16)
#define BWI_TXSTATUS1			0x174

#define BWI_TXRX_CTRL_BASE		0x200
#define BWI_TX32_CTRL			0x0
#define BWI_TX32_RINGINFO		0x4
#define BWI_TX32_INDEX			0x8
#define BWI_TX32_STATUS			0xc
#define BWI_TX32_STATUS_STATE_MASK	__BITS(15, 12)
#define BWI_TX32_STATUS_STATE_DISABLED	0
#define BWI_TX32_STATUS_STATE_IDLE	2
#define BWI_TX32_STATUS_STATE_STOPPED	3
#define BWI_RX32_CTRL			0x10
#define BWI_RX32_CTRL_HDRSZ_MASK	__BITS(7, 1)
#define BWI_RX32_RINGINFO		0x14
#define BWI_RX32_INDEX			0x18
#define BWI_RX32_STATUS			0x1c
#define BWI_RX32_STATUS_INDEX_MASK	__BITS(11, 0)
#define BWI_RX32_STATUS_STATE_MASK	__BITS(15, 12)
#define BWI_RX32_STATUS_STATE_DISABLED	0
/* Shared by 32bit TX/RX CTRL */
#define BWI_TXRX32_CTRL_ENABLE		__BIT(0)
#define BWI_TXRX32_CTRL_ADDRHI_MASK	__BITS(17, 16)
/* Shared by 32bit TX/RX RINGINFO */
#define BWI_TXRX32_RINGINFO_FUNC_TXRX	0x1
#define BWI_TXRX32_RINGINFO_FUNC_MASK	__BITS(31, 30)
#define BWI_TXRX32_RINGINFO_ADDR_MASK	__BITS(29, 0)

#define BWI_PHYINFO			0x3e0
#define BWI_PHYINFO_REV_MASK		__BITS(3, 0)
#define BWI_PHYINFO_TYPE_MASK		__BITS(11, 8)
#define BWI_PHYINFO_TYPE_11A		0
#define BWI_PHYINFO_TYPE_11B		1
#define BWI_PHYINFO_TYPE_11G		2
#define BWI_PHYINFO_TYPE_11N		4
#define BWI_PHYINFO_TYPE_11LP		5
#define BWI_PHYINFO_VER_MASK		__BITS(15, 12)

#define BWI_RF_ANTDIV			0x3e2	/* Antenna Diversity?? */

#define BWI_PHY_MAGIC_REG1		0x3e4
#define BWI_PHY_MAGIC_REG1_VAL1		0x3000
#define BWI_PHY_MAGIC_REG1_VAL2		0x9

#define BWI_BBP_ATTEN			0x3e6
#define BWI_BBP_ATTEN_MAGIC		0xf4
#define BWI_BBP_ATTEN_MAGIC2		0x8140

#define BWI_BPHY_CTRL			0x3ec
#define BWI_BPHY_CTRL_INIT		0x3f22

#define BWI_RF_CHAN			0x3f0
#define BWI_RF_CHAN_EX			0x3f4

#define BWI_RF_CTRL			0x3f6
/* Register values for BWI_RF_CTRL */
#define BWI_RF_CTRL_RFINFO		0x1
/* XXX extra bits for reading from radio */
#define BWI_RF_CTRL_RD_11A		0x40
#define BWI_RF_CTRL_RD_11BG		0x80
#define BWI_RF_DATA_HI			0x3f8
#define BWI_RF_DATA_LO			0x3fa
/* Values read from BWI_RF_DATA_{HI,LO} after BWI_RF_CTRL_RFINFO */
#define BWI_RFINFO_MANUFACT_MASK	__BITS(11, 0)
#define BWI_RF_MANUFACT_BCM		0x17f		/* XXX */
#define BWI_RFINFO_TYPE_MASK		__BITS(27, 12)
#define BWI_RF_T_BCM2050		0x2050
#define BWI_RF_T_BCM2053		0x2053
#define BWI_RF_T_BCM2060		0x2060
#define BWI_RFINFO_REV_MASK		__BITS(31, 28)

#define BWI_PHY_CTRL			0x3fc
#define BWI_PHY_DATA			0x3fe

#define BWI_ADDR_FILTER_CTRL		0x420
#define BWI_ADDR_FILTER_CTRL_SET	0x20
#define BWI_ADDR_FILTER_MYADDR		0
#define BWI_ADDR_FILTER_BSSID		3
#define BWI_ADDR_FILTER_DATA		0x422

#define BWI_MAC_GPIO_CTRL		0x49c
#define BWI_MAC_GPIO_MASK		0x49e
#define BWI_MAC_PRE_TBTT		0x612
#define BWI_MAC_SLOTTIME		0x684
#define BWI_MAC_SLOTTIME_ADJUST		510
#define BWI_MAC_POWERUP_DELAY		0x6a8

/*
 * Special registers
 */
/*
 * GPIO control
 * If common regwin exists, then it is within common regwin,
 * else it is in bus regwin.
 */
#define BWI_GPIO_CTRL			0x6c

#define	PCI_VENDOR_BROADCOM	0x14e4		/* Broadcom */
#define	PCI_PRODUCT_BROADCOM_BCM4309	0x4324

/*
 * Extended PCI registers
 */
#define BWI_PCIR_BAR			PCIR_BAR(0)
#define BWI_PCIR_SEL_REGWIN		0x80
/* Register value for BWI_PCIR_SEL_REGWIN */
#define BWI_PCIM_REGWIN(id)		(((id) * 0x1000) + 0x18000000)
#define BWI_PCIR_GPIO_IN		0xb0
#define BWI_PCIR_GPIO_OUT		0xb4
#define BWI_PCIM_GPIO_OUT_CLKSRC	__BIT(4)
#define BWI_PCIR_GPIO_ENABLE		0xb8
/* Register values for BWI_PCIR_GPIO_{IN,OUT,ENABLE} */
#define BWI_PCIM_GPIO_PWR_ON		__BIT(6)
#define BWI_PCIM_GPIO_PLL_PWR_OFF	__BIT(7)
#define BWI_PCIR_INTCTL			0x94

/*
 * PCI subdevice IDs
 */
#define BWI_PCI_SUBDEVICE_BU4306	0x416
#define BWI_PCI_SUBDEVICE_BCM4309G	0x421

#define BWI_IS_BRCM_BU4306(sc) \
	((sc)->sc_pci_subvid == PCI_VENDOR_BROADCOM && \
	 (sc)->sc_pci_subdid == BWI_PCI_SUBDEVICE_BU4306)
#define BWI_IS_BRCM_BCM4309G(sc) \
	((sc)->sc_pci_subvid == PCI_VENDOR_BROADCOM && \
	 (sc)->sc_pci_subdid == BWI_PCI_SUBDEVICE_BCM4309G)

/*
 * EEPROM start address
 */
#define BWI_SPROM_START			0x1000
#define BWI_SPROM_11BG_EADDR		0x48
#define BWI_SPROM_11A_EADDR		0x54
#define BWI_SPROM_CARD_INFO		0x5c
#define BWI_SPROM_CARD_INFO_LOCALE	__BITS(11, 8)
#define BWI_SPROM_LOCALE_JAPAN		5
#define BWI_SPROM_PA_PARAM_11BG		0x5e
#define BWI_SPROM_GPIO01		0x64
#define BWI_SPROM_GPIO_0		__BITS(7, 0)
#define BWI_SPROM_GPIO_1		__BITS(15, 8)
#define BWI_SPROM_GPIO23		0x66
#define BWI_SPROM_GPIO_2		__BITS(7, 0)
#define BWI_SPROM_GPIO_3		__BITS(15, 8)
#define BWI_SPROM_MAX_TXPWR		0x68
#define BWI_SPROM_MAX_TXPWR_MASK_11BG	__BITS(7, 0)	/* XXX */
#define BWI_SPROM_MAX_TXPWR_MASK_11A	__BITS(15, 8)	/* XXX */
#define BWI_SPROM_PA_PARAM_11A		0x6a
#define BWI_SPROM_IDLE_TSSI		0x70
#define BWI_SPROM_IDLE_TSSI_MASK_11BG	__BITS(7, 0)	/* XXX */
#define BWI_SPROM_IDLE_TSSI_MASK_11A	__BITS(15, 8)	/* XXX */
#define BWI_SPROM_CARD_FLAGS		0x72
#define BWI_SPROM_ANT_GAIN		0x74
#define BWI_SPROM_ANT_GAIN_MASK_11A	__BITS(7, 0)
#define BWI_SPROM_ANT_GAIN_MASK_11BG	__BITS(15, 8)

/*
 * SPROM card flags
 */
#define BWI_CARD_F_BT_COEXIST		__BIT(0)	/* Bluetooth coexist */
#define BWI_CARD_F_PA_GPIO9		__BIT(1)	/* GPIO 9 controls PA */
#define BWI_CARD_F_SW_NRSSI		__BIT(3)
#define BWI_CARD_F_NO_SLOWCLK		__BIT(5)	/* no slow clock */
#define BWI_CARD_F_EXT_LNA		__BIT(12)	/* external LNA */
#define BWI_CARD_F_ALT_IQ		__BIT(15)	/* alternate I/Q */

/*
 * SPROM GPIO
 */
#define BWI_LED_ACT_LOW			__BIT(7)
#define BWI_LED_ACT_MASK		__BITS(6, 0)
#define BWI_LED_ACT_OFF			0
#define BWI_LED_ACT_ON			1
#define BWI_LED_ACT_BLINK		2
#define BWI_LED_ACT_RF_ENABLED		3
#define BWI_LED_ACT_5GHZ		4
#define BWI_LED_ACT_2GHZ		5
#define BWI_LED_ACT_11G			6
#define BWI_LED_ACT_BLINK_SLOW		7
#define BWI_LED_ACT_BLINK_POLL		8
#define BWI_LED_ACT_UNKN		9
#define BWI_LED_ACT_ASSOC		10
#define BWI_LED_ACT_NULL		11

#define BWI_VENDOR_LED_ACT_COMPAQ	\
	BWI_LED_ACT_RF_ENABLED,		\
	BWI_LED_ACT_2GHZ,		\
	BWI_LED_ACT_5GHZ,		\
	BWI_LED_ACT_OFF

#define BWI_VENDOR_LED_ACT_LINKSYS	\
	BWI_LED_ACT_ASSOC,		\
	BWI_LED_ACT_2GHZ,		\
	BWI_LED_ACT_5GHZ,		\
	BWI_LED_ACT_OFF

#define BWI_VENDOR_LED_ACT_DEFAULT	\
	BWI_LED_ACT_BLINK,		\
	BWI_LED_ACT_2GHZ,		\
	BWI_LED_ACT_5GHZ,		\
	BWI_LED_ACT_OFF

/*
 * BBP IDs
 */
#define BWI_BBPID_BCM4301		0x4301
#define BWI_BBPID_BCM4306		0x4306
#define BWI_BBPID_BCM4317		0x4317
#define BWI_BBPID_BCM4320		0x4320
#define BWI_BBPID_BCM4321		0x4321

/*
 * Register window types
 */
#define BWI_REGWIN_T_COM		0x800
#define BWI_REGWIN_T_BUSPCI		0x804
#define BWI_REGWIN_T_MAC		0x812
#define BWI_REGWIN_T_BUSPCIE		0x820

/*
 * MAC interrupts
 */
#define BWI_INTR_READY			__BIT(0)
#define BWI_INTR_BEACON			__BIT(1)
#define BWI_INTR_TBTT			__BIT(2)
#define BWI_INTR_EO_ATIM		__BIT(5)	/* End of ATIM */
#define BWI_INTR_PMQ			__BIT(6)	/* XXX?? */
#define BWI_INTR_MAC_TXERR		__BIT(9)
#define BWI_INTR_PHY_TXERR		__BIT(11)
#define BWI_INTR_TIMER1			__BIT(14)
#define BWI_INTR_RX_DONE		__BIT(15)
#define BWI_INTR_TX_FIFO		__BIT(16)	/* XXX?? */
#define BWI_INTR_NOISE			__BIT(18)
#define BWI_INTR_RF_DISABLED		__BIT(28)
#define BWI_INTR_TX_DONE		__BIT(29)

#define BWI_INIT_INTRS \
	(BWI_INTR_READY | BWI_INTR_BEACON | BWI_INTR_TBTT | \
	 BWI_INTR_EO_ATIM | BWI_INTR_PMQ | BWI_INTR_MAC_TXERR | \
	 BWI_INTR_PHY_TXERR | BWI_INTR_RX_DONE | BWI_INTR_TX_FIFO | \
	 BWI_INTR_NOISE | BWI_INTR_RF_DISABLED | BWI_INTR_TX_DONE)
#define BWI_ALL_INTRS			0xffffffff

/*
 * TX/RX interrupts
 */
#define BWI_TXRX_INTR_ERROR		(__BIT(15) | __BIT(14) | __BITS(12, 10))
#define BWI_TXRX_INTR_RX		__BIT(16)
#define BWI_TXRX_TX_INTRS		BWI_TXRX_INTR_ERROR
#define BWI_TXRX_RX_INTRS		(BWI_TXRX_INTR_ERROR | BWI_TXRX_INTR_RX)
#define BWI_TXRX_IS_RX(i)		((i) % 3 == 0)

#endif	/* !_IF_BWIREG_H */
