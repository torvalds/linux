/*	$OpenBSD: bwireg.h,v 1.11 2022/01/09 05:42:38 jsg Exp $	*/

/*
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
 * $DragonFly: src/sys/dev/netif/bwi/if_bwireg.h,v 1.1 2007/09/08 06:15:54 sephe Exp $
 */

#ifndef _IF_BWIREG_H
#define _IF_BWIREG_H

/*
 * Registers for all of the register windows
 */
#define BWI_FLAGS			0x00000f18
#define BWI_FLAGS_INTR_MASK		0x0000003f

#define BWI_IMSTATE			0x00000f90
#define BWI_IMSTATE_INBAND_ERR		(1 << 17)
#define BWI_IMSTATE_TIMEOUT		(1 << 18)

#define BWI_INTRVEC			0x00000f94

#define BWI_STATE_LO			0x00000f98
#define BWI_STATE_LO_RESET		(1 << 0)
#define BWI_STATE_LO_DISABLE1		(1 << 1)
#define BWI_STATE_LO_DISABLE2		(1 << 2)
#define BWI_STATE_LO_CLOCK		(1 << 16)
#define BWI_STATE_LO_GATED_CLOCK	(1 << 17)
#define BWI_STATE_LO_FLAG_PHYCLKEN	(1 << 0)
#define BWI_STATE_LO_FLAG_PHYRST	(1 << 1)
#define BWI_STATE_LO_FLAG_PHYLNK	(1 << 11)
#define BWI_STATE_LO_FLAGS_MASK		0x3ffc0000

#define BWI_STATE_HI			0x00000f9c
#define BWI_STATE_HI_SERROR		(1 << 0)
#define BWI_STATE_HI_BUSY		(1 << 2)
#define BWI_STATE_HI_FLAG_MAGIC1	0x1
#define BWI_STATE_HI_FLAG_MAGIC2	0x2
#define BWI_STATE_HI_FLAG_64BIT		0x1000
#define BWI_STATE_HI_FLAGS_MASK		0x1fff0000

#define BWI_CONF_LO			0x00000fa8
#define BWI_CONF_LO_SERVTO_MASK		0x00000007	/* service timeout */
#define BWI_CONF_LO_SERVTO		2
#define BWI_CONF_LO_REQTO_MASK		0x00000070	/* request timeout */
#define BWI_CONF_LO_REQTO		3

#define BWI_ID_LO			0x00000ff8
#define BWI_ID_LO_BUSREV_MASK		0xf0000000
/* Bus revision */
#define BWI_BUSREV_0			0
#define BWI_BUSREV_1			1

#define BWI_ID_HI			0x00000ffc
#define BWI_ID_HI_REGWIN_REV(v)		(((v) & 0xf) | (((v) & 0x7000) >> 8))
#define BWI_ID_HI_REGWIN_TYPE(v)	(((v) & 0x8ff0) >> 4)
#define BWI_ID_HI_REGWIN_VENDOR_MASK	0xffff0000

/*
 * Registers for common register window
 */
#define BWI_INFO			0x00000000
#define BWI_INFO_BBPID_MASK		0x0000ffff
#define BWI_INFO_BBPREV_MASK		0x000f0000
#define BWI_INFO_BBPPKG_MASK		0x00f00000
#define BWI_INFO_NREGWIN_MASK		0x0f000000

#define BWI_CAPABILITY			0x00000004
#define BWI_CAP_CLKMODE			(1 << 18)

#define BWI_CONTROL			0x00000028
#define BWI_CONTROL_MAGIC0		0x3a4
#define BWI_CONTROL_MAGIC1		0xa4
#define BWI_PLL_ON_DELAY		0xb0
#define BWI_FREQ_SEL_DELAY		0xb4

#define BWI_CLOCK_CTRL			0x000000b8
#define BWI_CLOCK_CTRL_CLKSRC		(7 << 0)
#define BWI_CLOCK_CTRL_SLOW		(1 << 11)
#define BWI_CLOCK_CTRL_IGNPLL		(1 << 12)
#define BWI_CLOCK_CTRL_NODYN		(1 << 13)
#define BWI_CLOCK_CTRL_FDIV		(0xffff << 16)	/* freq divisor */

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

#define BWI_CLOCK_INFO			0x000000c0
#define BWI_CLOCK_INFO_FDIV		(0xffff << 16)	/* freq divisor */

/*
 * Registers for bus register window
 */
#define BWI_BUS_ADDR			0x00000050
#define BWI_BUS_ADDR_MAGIC		0xfd8

#define BWI_BUS_DATA			0x00000054

#define BWI_BUS_CONFIG			0x00000108
#define BWI_BUS_CONFIG_PREFETCH		(1 << 2)
#define BWI_BUS_CONFIG_BURST		(1 << 3)
#define BWI_BUS_CONFIG_MRM		(1 << 5)

/*
 * Register for MAC
 */
#define BWI_TXRX_INTR_STATUS_BASE	0x20
#define BWI_TXRX_INTR_MASK_BASE		0x24
#define BWI_TXRX_INTR_STATUS(i)		(BWI_TXRX_INTR_STATUS_BASE + ((i) * 8))
#define BWI_TXRX_INTR_MASK(i)		(BWI_TXRX_INTR_MASK_BASE + ((i) * 8))

#define BWI_MAC_STATUS			0x00000120
#define BWI_MAC_STATUS_ENABLE		(1U << 0)
#define BWI_MAC_STATUS_UCODE_START	(1U << 1)
#define BWI_MAC_STATUS_UCODE_JUMP0	(1U << 2)
#define BWI_MAC_STATUS_IHREN		(1U << 10)
#define BWI_MAC_STATUS_GPOSEL_MASK	(3U << 14)
#define BWI_MAC_STATUS_BSWAP		(1U << 16)
#define BWI_MAC_STATUS_INFRA		(1U << 17)
#define BWI_MAC_STATUS_OPMODE_HOSTAP	(1U << 18)
#define BWI_MAC_STATUS_RFLOCK		(1U << 19)
#define BWI_MAC_STATUS_PASS_BCN		(1U << 20)
#define BWI_MAC_STATUS_PASS_BADPLCP	(1U << 21)
#define BWI_MAC_STATUS_PASS_CTL		(1U << 22)
#define BWI_MAC_STATUS_PASS_BADFCS	(1U << 23)
#define BWI_MAC_STATUS_PROMISC		(1U << 24)
#define BWI_MAC_STATUS_HW_PS		(1U << 25)
#define BWI_MAC_STATUS_WAKEUP		(1U << 26)
#define BWI_MAC_STATUS_PHYLNK		(1U << 31)

#define BWI_MAC_INTR_STATUS		0x00000128
#define BWI_MAC_INTR_MASK		0x0000012c

#define BWI_MAC_TMPLT_CTRL		0x00000130
#define BWI_MAC_TMPLT_DATA		0x00000134

#define BWI_MAC_PS_STATUS		0x00000140

#define BWI_MOBJ_CTRL			0x00000160
#define BWI_MOBJ_CTRL_VAL(objid, ofs)	((objid) << 16 | (ofs))
#define BWI_MOBJ_DATA			0x00000164
#define BWI_MOBJ_DATA_UNALIGN		0x0166

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
#define BWI_LO_TSSI_MASK		0x00ff
#define BWI_HI_TSSI_MASK		0xff00
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

#define BWI_TXSTATUS_0			0x00000170
#define BWI_TXSTATUS_0_MORE		(1 << 0)
#define BWI_TXSTATUS_0_TXID_MASK	0xffff0000
#define BWI_TXSTATUS_0_INFO(st)		(((st) & 0xfff0) | (((st) & 0xf) >> 1))
#define BWI_TXSTATUS_1			0x00000174

#define BWI_TXRX_CTRL_BASE		0x200
#define BWI_TX32_CTRL			0x0
#define BWI_TX32_RINGINFO		0x4
#define BWI_TX32_INDEX			0x8
#define BWI_TX32_STATUS			0xc
#define BWI_TX32_STATUS_STATE_MASK	0xf000
#define BWI_TX32_STATUS_STATE_DISABLED	0
#define BWI_TX32_STATUS_STATE_IDLE	2
#define BWI_TX32_STATUS_STATE_STOPPED	3
#define BWI_RX32_CTRL			0x10
#define BWI_RX32_CTRL_HDRSZ_MASK	0x00fe
#define BWI_RX32_RINGINFO		0x14
#define BWI_RX32_INDEX			0x18
#define BWI_RX32_STATUS			0x1c
#define BWI_RX32_STATUS_INDEX_MASK	0x0fff
#define BWI_RX32_STATUS_STATE_MASK	0xf000
#define BWI_RX32_STATUS_STATE_DISABLED	0
/* Shared by 32bit TX/RX CTRL */
#define BWI_TXRX32_CTRL_ENABLE		(1 << 0)
#define BWI_TXRX32_CTRL_ADDRHI_MASK	0x00030000
/* Shared by 32bit TX/RX RINGINFO */
#define BWI_TXRX32_RINGINFO_FUNC_TXRX	0x1
#define BWI_TXRX32_RINGINFO_FUNC_MASK	0xc0000000
#define BWI_TXRX32_RINGINFO_ADDR_MASK	0x3fffffff

#define BWI_PHYINFO			0x03e0
#define BWI_PHYINFO_REV_MASK		0x000f
#define BWI_PHYINFO_TYPE_MASK		0x0f00
#define BWI_PHYINFO_TYPE_11A		0
#define BWI_PHYINFO_TYPE_11B		1
#define BWI_PHYINFO_TYPE_11G		2
#define BWI_PHYINFO_TYPE_11N		5
#define BWI_PHYINFO_VER_MASK		0xf000

#define BWI_RF_ANTDIV			0x03e2	/* Antenna Diversity ?? */

#define BWI_PHY_MAGIC_REG1		0x03e4
#define BWI_PHY_MAGIC_REG1_VAL1		0x3000
#define BWI_PHY_MAGIC_REG1_VAL2		0x0009

#define BWI_BBP_ATTEN			0x03e6
#define BWI_BBP_ATTEN_MAGIC		0x00f4
#define BWI_BBP_ATTEN_MAGIC2		0x8140

#define BWI_BPHY_CTRL			0x03ec
#define BWI_BPHY_CTRL_INIT		0x3f22

#define BWI_RF_CHAN			0x03f0
#define BWI_RF_CHAN_EX			0x03f4

#define BWI_RF_CTRL			0x03f6
/* Register values for BWI_RF_CTRL */
#define BWI_RF_CTRL_RFINFO		0x1
/* XXX extra bits for reading from radio */
#define BWI_RF_CTRL_RD_11A		0x40
#define BWI_RF_CTRL_RD_11BG		0x80
#define BWI_RF_DATA_HI			0x3f8
#define BWI_RF_DATA_LO			0x3fa
/* Values read from BWI_RF_DATA_{HI,LO} after BWI_RF_CTRL_RFINFO */
#define BWI_RFINFO_MANUFACT_MASK	0x0fff
#define BWI_RF_MANUFACT_BCM		0x17f		/* XXX */
#define BWI_RFINFO_TYPE_MASK		0x0ffff000
#define BWI_RF_T_BCM2050		0x2050
#define BWI_RF_T_BCM2053		0x2053
#define BWI_RF_T_BCM2060		0x2060
#define BWI_RFINFO_REV_MASK		0xf0000000

#define BWI_PHY_CTRL			0x03fc
#define BWI_PHY_DATA			0x03fe

#define BWI_ADDR_FILTER_CTRL		0x0420
#define BWI_ADDR_FILTER_CTRL_SET	0x0020
#define BWI_ADDR_FILTER_MYADDR		0
#define BWI_ADDR_FILTER_BSSID		3
#define BWI_ADDR_FILTER_DATA		0x422

#define BWI_MAC_GPIO_CTRL		0x049c
#define BWI_MAC_GPIO_MASK		0x049e
#define BWI_MAC_PRE_TBTT		0x0612
#define BWI_MAC_SLOTTIME		0x0684
#define BWI_MAC_SLOTTIME_ADJUST		510
#define BWI_MAC_POWERUP_DELAY		0x06a8

/*
 * Special registers
 */
/*
 * GPIO control
 * If common regwin exists, then it is within common regwin,
 * else it is in bus regwin.
 */
#define BWI_GPIO_CTRL			0x0000006c

/*
 * Core reset
 */
#define BWI_RESET_CTRL          0x1800
#define BWI_RESET_STATUS        0x1804
#define BWI_RESET_CTRL_RESET    (1 << 0)

/*
 * Extended PCI registers
 */
#define BWI_PCIR_BAR			PCIR_BAR(0)
#define BWI_PCIR_SEL_REGWIN		0x00000080
/* Register value for BWI_PCIR_SEL_REGWIN */
#define BWI_PCIM_REGWIN(id)		(((id) * 0x1000) + 0x18000000)
#define BWI_PCIR_GPIO_IN		0x000000b0
#define BWI_PCIR_GPIO_OUT		0x000000b4
#define BWI_PCIM_GPIO_OUT_CLKSRC	(1 << 4)
#define BWI_PCIR_GPIO_ENABLE		0x000000b8
/* Register values for BWI_PCIR_GPIO_{IN,OUT,ENABLE} */
#define BWI_PCIM_GPIO_PWR_ON		(1 << 6)
#define BWI_PCIM_GPIO_PLL_PWR_OFF	(1 << 7)
#define BWI_PCIR_INTCTL			0x00000094

/*
 * PCI subdevice IDs
 */
#define BWI_PCI_SUBDEVICE_BU4306	0x416
#define BWI_PCI_SUBDEVICE_BCM4309G	0x421

#define BWI_IS_BRCM_BU4306(sc)					\
	((sc)->sc_pci_subvid == PCI_VENDOR_BROADCOM &&		\
	 (sc)->sc_pci_subdid == BWI_PCI_SUBDEVICE_BU4306)
#define BWI_IS_BRCM_BCM4309G(sc)				\
	((sc)->sc_pci_subvid == PCI_VENDOR_BROADCOM &&		\
	 (sc)->sc_pci_subdid == BWI_PCI_SUBDEVICE_BCM4309G)

/*
 * EEPROM start address
 */
#define BWI_SPROM_START			0x1000
#define BWI_SPROM_11BG_EADDR		0x48
#define BWI_SPROM_11A_EADDR		0x54
#define BWI_SPROM_CARD_INFO		0x5c
#define BWI_SPROM_CARD_INFO_LOCALE	(0x0f << 8)
#define BWI_SPROM_LOCALE_JAPAN		5
#define BWI_SPROM_PA_PARAM_11BG		0x5e
#define BWI_SPROM_GPIO01		0x64
#define BWI_SPROM_GPIO_0		0x00ff
#define BWI_SPROM_GPIO_1		0xff00
#define BWI_SPROM_GPIO23		0x0066
#define BWI_SPROM_GPIO_2		0x00ff
#define BWI_SPROM_GPIO_3		0xff00
#define BWI_SPROM_MAX_TXPWR		0x68
#define BWI_SPROM_MAX_TXPWR_MASK_11BG	0x00ff		/* XXX */
#define BWI_SPROM_MAX_TXPWR_MASK_11A	0xff00		/* XXX */
#define BWI_SPROM_PA_PARAM_11A		0x6a
#define BWI_SPROM_IDLE_TSSI		0x70
#define BWI_SPROM_IDLE_TSSI_MASK_11BG	0x00ff		/* XXX */
#define BWI_SPROM_IDLE_TSSI_MASK_11A	0xff00		/* XXX */
#define BWI_SPROM_CARD_FLAGS		0x72
#define BWI_SPROM_ANT_GAIN		0x74
#define BWI_SPROM_ANT_GAIN_MASK_11A	0x00ff
#define BWI_SPROM_ANT_GAIN_MASK_11BG	0xff00

/*
 * SPROM card flags
 */
#define BWI_CARD_F_PA_GPIO9		(1 << 1)	/* GPIO 9 controls PA */
#define BWI_CARD_F_SW_NRSSI		(1 << 3)
#define BWI_CARD_F_NO_SLOWCLK		(1 << 5)	/* no slow clock */
#define BWI_CARD_F_EXT_LNA		(1 << 12)	/* external LNA */
#define BWI_CARD_F_ALT_IQ		(1 << 15)	/* alternate I/Q */

/*
 * PROM GPIO
 */
#define BWI_LED_ACT_LOW			(1 << 7)
#define BWI_LED_ACT_MASK		0x7f
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
#define BWI_INTR_READY			(1 << 0)
#define BWI_INTR_BEACON			(1 << 1)
#define BWI_INTR_TBTT			(1 << 2)
#define BWI_INTR_EO_ATIM		(1 << 5)	/* End of ATIM */
#define BWI_INTR_PMQ			(1 << 6)	/* XXX?? */
#define BWI_INTR_MAC_TXERR		(1 << 9)
#define BWI_INTR_PHY_TXERR		(1 << 11)
#define BWI_INTR_TIMER1			(1 << 14)
#define BWI_INTR_RX_DONE		(1 << 15)
#define BWI_INTR_TX_FIFO		(1 << 16)	/* XXX?? */
#define BWI_INTR_NOISE			(1 << 18)
#define BWI_INTR_RF_DISABLED		(1 << 28)
#define BWI_INTR_TX_DONE		(1 << 29)

#define BWI_INIT_INTRS							\
	(BWI_INTR_READY | BWI_INTR_BEACON | BWI_INTR_TBTT |		\
	 BWI_INTR_EO_ATIM | BWI_INTR_PMQ | BWI_INTR_MAC_TXERR |		\
	 BWI_INTR_PHY_TXERR | BWI_INTR_RX_DONE | BWI_INTR_TX_FIFO |	\
	 BWI_INTR_NOISE | BWI_INTR_RF_DISABLED | BWI_INTR_TX_DONE)
#define BWI_ALL_INTRS			0xffffffff

/*
 * TX/RX interrupts
 */

/* from brcmsmac */
#define	BWI_I_PC		(1 << 10)	/* pci descriptor error */
#define	BWI_I_PD		(1 << 11)	/* pci data error */
#define	BWI_I_DE		(1 << 12)	/* descriptor protocol error */
#define	BWI_I_RU		(1 << 13)	/* receive descriptor underflow */
#define	BWI_I_RO		(1 << 14)	/* receive fifo overflow */
#define	BWI_I_XU		(1 << 15)	/* transmit fifo underflow */
#define	BWI_I_RI		(1 << 16)	/* receive interrupt */
#define	BWI_I_XI		(1 << 24)	/* transmit interrupt */

#define BWI_TXRX_INTR_ERROR		(BWI_I_XU | BWI_I_RO | BWI_I_DE | \
					 BWI_I_PD | BWI_I_PC)
#define BWI_TXRX_INTR_RX		BWI_I_RI
#define BWI_TXRX_TX_INTRS		BWI_TXRX_INTR_ERROR
#define BWI_TXRX_RX_INTRS		(BWI_TXRX_INTR_ERROR | BWI_TXRX_INTR_RX)
#define BWI_TXRX_IS_RX(i)		((i) % 3 == 0)

/* PHY */

#define BWI_PHYR_NRSSI_THR_11B		0x020
#define BWI_PHYR_BBP_ATTEN		0x060
#define BWI_PHYR_TBL_CTRL_11A		0x072
#define BWI_PHYR_TBL_DATA_LO_11A	0x073
#define BWI_PHYR_TBL_DATA_HI_11A	0x074
#define BWI_PHYR_TBL_CTRL_11G		0x472
#define BWI_PHYR_TBL_DATA_LO_11G	0x473
#define BWI_PHYR_TBL_DATA_HI_11G	0x474
#define BWI_PHYR_NRSSI_THR_11G		0x48a
#define BWI_PHYR_NRSSI_CTRL		0x803
#define BWI_PHYR_NRSSI_DATA		0x804
#define BWI_PHYR_RF_LO			0x810

/*
 * PHY Tables
 */
/*
 * http://bcm-specs.sipsolutions.net/APHYSetup/FineFrequency
 * G PHY
 */
#define BWI_PHY_FREQ_11G_REV1						\
	0x0089,	0x02e9,	0x0409,	0x04e9,	0x05a9,	0x0669,	0x0709,	0x0789,	\
	0x0829,	0x08a9,	0x0929,	0x0989,	0x0a09,	0x0a69,	0x0ac9,	0x0b29,	\
	0x0ba9,	0x0be9,	0x0c49,	0x0ca9,	0x0d09,	0x0d69,	0x0da9,	0x0e09,	\
	0x0e69,	0x0ea9,	0x0f09,	0x0f49,	0x0fa9,	0x0fe9,	0x1029,	0x1089,	\
	0x10c9,	0x1109,	0x1169,	0x11a9,	0x11e9,	0x1229,	0x1289,	0x12c9,	\
	0x1309,	0x1349,	0x1389,	0x13c9,	0x1409,	0x1449,	0x14a9,	0x14e9,	\
	0x1529,	0x1569,	0x15a9,	0x15e9,	0x1629,	0x1669,	0x16a9,	0x16e8,	\
	0x1728,	0x1768,	0x17a8,	0x17e8,	0x1828,	0x1868,	0x18a8,	0x18e8,	\
	0x1928,	0x1968,	0x19a8,	0x19e8,	0x1a28,	0x1a68,	0x1aa8,	0x1ae8,	\
	0x1b28,	0x1b68,	0x1ba8,	0x1be8,	0x1c28,	0x1c68,	0x1ca8,	0x1ce8,	\
	0x1d28,	0x1d68,	0x1dc8,	0x1e08,	0x1e48,	0x1e88,	0x1ec8,	0x1f08,	\
	0x1f48,	0x1f88,	0x1fe8,	0x2028,	0x2068,	0x20a8,	0x2108,	0x2148,	\
	0x2188,	0x21c8,	0x2228,	0x2268,	0x22c8,	0x2308,	0x2348,	0x23a8,	\
	0x23e8,	0x2448,	0x24a8,	0x24e8,	0x2548,	0x25a8,	0x2608,	0x2668,	\
	0x26c8,	0x2728,	0x2787,	0x27e7,	0x2847,	0x28c7,	0x2947,	0x29a7,	\
	0x2a27,	0x2ac7,	0x2b47,	0x2be7,	0x2ca7,	0x2d67,	0x2e47,	0x2f67,	\
	0x3247,	0x3526,	0x3646,	0x3726,	0x3806,	0x38a6,	0x3946,	0x39e6,	\
	0x3a66,	0x3ae6,	0x3b66,	0x3bc6,	0x3c45,	0x3ca5,	0x3d05,	0x3d85,	\
	0x3de5,	0x3e45,	0x3ea5,	0x3ee5,	0x3f45,	0x3fa5,	0x4005,	0x4045,	\
	0x40a5,	0x40e5,	0x4145,	0x4185,	0x41e5,	0x4225,	0x4265,	0x42c5,	\
	0x4305,	0x4345,	0x43a5,	0x43e5,	0x4424,	0x4464,	0x44c4,	0x4504,	\
	0x4544,	0x4584,	0x45c4,	0x4604,	0x4644,	0x46a4,	0x46e4,	0x4724,	\
	0x4764,	0x47a4,	0x47e4,	0x4824,	0x4864,	0x48a4,	0x48e4,	0x4924,	\
	0x4964,	0x49a4,	0x49e4,	0x4a24,	0x4a64,	0x4aa4,	0x4ae4,	0x4b23,	\
	0x4b63,	0x4ba3,	0x4be3,	0x4c23,	0x4c63,	0x4ca3,	0x4ce3,	0x4d23,	\
	0x4d63,	0x4da3,	0x4de3,	0x4e23,	0x4e63,	0x4ea3,	0x4ee3,	0x4f23,	\
	0x4f63,	0x4fc3,	0x5003,	0x5043,	0x5083,	0x50c3,	0x5103,	0x5143,	\
	0x5183,	0x51e2,	0x5222,	0x5262,	0x52a2,	0x52e2,	0x5342,	0x5382,	\
	0x53c2,	0x5402,	0x5462,	0x54a2,	0x5502,	0x5542,	0x55a2,	0x55e2,	\
	0x5642,	0x5682,	0x56e2,	0x5722,	0x5782,	0x57e1,	0x5841,	0x58a1,	\
	0x5901,	0x5961,	0x59c1,	0x5a21,	0x5aa1,	0x5b01,	0x5b81,	0x5be1,	\
	0x5c61,	0x5d01,	0x5d80,	0x5e20,	0x5ee0,	0x5fa0,	0x6080,	0x61c0

/*
 * http://bcm-specs.sipsolutions.net/APHYSetup/noise_table
 */
/* G PHY Revision 1 */
#define BWI_PHY_NOISE_11G_REV1 \
	0x013c,	0x01f5,	0x031a,	0x0631,	0x0001,	0x0001,	0x0001,	0x0001
/* G PHY generic */
#define BWI_PHY_NOISE_11G \
	0x5484, 0x3c40, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000

/*
 * http://bcm-specs.sipsolutions.net/APHYSetup/rotor_table
 * G PHY Revision 1
 */
#define BWI_PHY_ROTOR_11G_REV1				\
	0xfeb93ffd, 0xfec63ffd, 0xfed23ffd, 0xfedf3ffd,	\
	0xfeec3ffe, 0xfef83ffe, 0xff053ffe, 0xff113ffe,	\
	0xff1e3ffe, 0xff2a3fff, 0xff373fff, 0xff443fff,	\
	0xff503fff, 0xff5d3fff, 0xff693fff, 0xff763fff,	\
	0xff824000, 0xff8f4000, 0xff9b4000, 0xffa84000,	\
	0xffb54000, 0xffc14000, 0xffce4000, 0xffda4000,	\
	0xffe74000, 0xfff34000, 0x00004000, 0x000d4000,	\
	0x00194000, 0x00264000, 0x00324000, 0x003f4000,	\
	0x004b4000, 0x00584000, 0x00654000, 0x00714000,	\
	0x007e4000, 0x008a3fff, 0x00973fff, 0x00a33fff,	\
	0x00b03fff, 0x00bc3fff, 0x00c93fff, 0x00d63fff,	\
	0x00e23ffe, 0x00ef3ffe, 0x00fb3ffe, 0x01083ffe,	\
	0x01143ffe, 0x01213ffd, 0x012e3ffd, 0x013a3ffd,	\
	0x01473ffd

/*
 * http://bcm-specs.sipsolutions.net/APHYSetup/noise_scale_table
 */
/* G PHY Revision [0,2] */
#define BWI_PHY_NOISE_SCALE_11G_REV2					\
	0x6c77,	0x5162,	0x3b40,	0x3335,	0x2f2d,	0x2a2a,	0x2527,	0x1f21,	\
	0x1a1d,	0x1719,	0x1616,	0x1414,	0x1414,	0x1400,	0x1414,	0x1614,	\
	0x1716,	0x1a19,	0x1f1d,	0x2521,	0x2a27,	0x2f2a,	0x332d,	0x3b35,	\
	0x5140,	0x6c62,	0x0077
/* G PHY Revision 7 */
#define BWI_PHY_NOISE_SCALE_11G_REV7					\
	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	\
	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa400,	0xa4a4,	0xa4a4,	\
	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	0xa4a4,	\
	0xa4a4,	0xa4a4,	0x00a4
/* G PHY generic */
#define BWI_PHY_NOISE_SCALE_11G						\
	0xd8dd,	0xcbd4,	0xbcc0,	0xb6b7,	0xb2b0,	0xadad,	0xa7a9,	0x9fa1,	\
	0x969b,	0x9195,	0x8f8f,	0x8a8a,	0x8a8a,	0x8a00,	0x8a8a,	0x8f8a,	\
	0x918f,	0x9695,	0x9f9b,	0xa7a1,	0xada9,	0xb2ad,	0xb6b0,	0xbcb7,	\
	0xcbc0,	0xd8d4,	0x00dd

/*
 * http://bcm-specs.sipsolutions.net/APHYSetup/sigma_square_table
 */
/* G PHY Revision 2 */
#define BWI_PHY_SIGMA_SQ_11G_REV2					\
	0x007a,	0x0075,	0x0071,	0x006c,	0x0067,	0x0063,	0x005e,	0x0059,	\
	0x0054,	0x0050,	0x004b,	0x0046,	0x0042,	0x003d,	0x003d,	0x003d,	\
	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	\
	0x003d,	0x003d,	0x0000,	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	\
	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	0x003d,	\
	0x0042,	0x0046,	0x004b,	0x0050,	0x0054,	0x0059,	0x005e,	0x0063,	\
	0x0067,	0x006c,	0x0071,	0x0075,	0x007a
/* G PHY Revision (2,7] */
#define BWI_PHY_SIGMA_SQ_11G_REV7					\
	0x00de,	0x00dc,	0x00da,	0x00d8,	0x00d6,	0x00d4,	0x00d2,	0x00cf,	\
	0x00cd,	0x00ca,	0x00c7,	0x00c4,	0x00c1,	0x00be,	0x00be,	0x00be,	\
	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	\
	0x00be,	0x00be,	0x0000,	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	\
	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	0x00be,	\
	0x00c1,	0x00c4,	0x00c7,	0x00ca,	0x00cd,	0x00cf,	0x00d2,	0x00d4,	\
	0x00d6,	0x00d8,	0x00da,	0x00dc,	0x00de

/*
 * http://bcm-specs.sipsolutions.net/APHYSetup/retard_table
 * G PHY
 */
#define BWI_PHY_DELAY_11G_REV1				\
	0xdb93cb87, 0xd666cf64, 0xd1fdd358, 0xcda6d826,	\
	0xca38dd9f, 0xc729e2b4, 0xc469e88e, 0xc26aee2b,	\
	0xc0def46c, 0xc073fa62, 0xc01d00d5, 0xc0760743,	\
	0xc1560d1e, 0xc2e51369, 0xc4ed18ff, 0xc7ac1ed7,	\
	0xcb2823b2, 0xcefa28d9, 0xd2f62d3f, 0xd7bb3197,	\
	0xdce53568, 0xe1fe3875, 0xe7d13b35, 0xed663d35,	\
	0xf39b3ec4, 0xf98e3fa7, 0x00004000, 0x06723fa7,	\
	0x0c653ec4, 0x129a3d35, 0x182f3b35, 0x1e023875,	\
	0x231b3568, 0x28453197, 0x2d0a2d3f, 0x310628d9,	\
	0x34d823b2, 0x38541ed7, 0x3b1318ff, 0x3d1b1369,	\
	0x3eaa0d1e, 0x3f8a0743, 0x3fe300d5, 0x3f8dfa62,	\
	0x3f22f46c, 0x3d96ee2b, 0x3b97e88e, 0x38d7e2b4,	\
	0x35c8dd9f, 0x325ad826, 0x2e03d358, 0x299acf64,	\
	0x246dcb87

/* RF */

#define BWI_RFR_ATTEN			0x43

#define BWI_RFR_TXPWR			0x52
#define BWI_RFR_TXPWR1_MASK		0x0070

#define BWI_RFR_BBP_ATTEN		0x60
#define BWI_RFR_BBP_ATTEN_CALIB_BIT	(1 << 0)
#define BWI_RFR_BBP_ATTEN_CALIB_IDX	(0x0f << 1)

/*
 * TSSI -- TX power maps
 */
/*
 * http://bcm-specs.sipsolutions.net/TSSI_to_DBM_Table
 * B PHY
 */
#define BWI_TXPOWER_MAP_11B						\
	0x4d,	0x4c,	0x4b,	0x4a,	0x4a,	0x49,	0x48,	0x47,	\
	0x47,	0x46,	0x45,	0x45,	0x44,	0x43,	0x42,	0x42,	\
	0x41,	0x40,	0x3f,	0x3e,	0x3d,	0x3c,	0x3b,	0x3a,	\
	0x39,	0x38,	0x37,	0x36,	0x35,	0x34,	0x32,	0x31,	\
	0x30,	0x2f,	0x2d,	0x2c,	0x2b,	0x29,	0x28,	0x26,	\
	0x25,	0x23,	0x21,	0x1f,	0x1d,	0x1a,	0x17,	0x14,	\
	0x10,	0x0c,	0x06,	0x00,	-7,	-7,	-7,	-7, 	\
	-7,	-7,	-7,	-7,	-7,	-7,	-7,	-7
/*
 * http://bcm-specs.sipsolutions.net/TSSI_to_DBM_Table
 * G PHY
 */
#define BWI_TXPOWER_MAP_11G						\
	77,	77,	77,	76,	76,	76,	75,	75,	\
	74,	74,	73,	73,	73,	72,	72,	71,	\
	71,	70,	70,	69,	68,	68,	67,	67,	\
	66,	65,	65,	64,	63,	63,	62,	61,	\
	60,	59,	58,	57,	56,	55,	54,	53,	\
	52,	50,	49,	47,	45,	43,	40,	37,	\
	33,	28,	22,	14,	5,	-7,	-20,	-20,	\
	-20,	-20,	-20,	-20,	-20,	-20,	-20,	-20

/* Find least significant bit that is set */
#define	__LOWEST_SET_BIT(__mask) ((((__mask) - 1) & (__mask)) ^ (__mask))

#define	__SHIFTOUT(__x, __mask) (((__x) & (__mask)) / __LOWEST_SET_BIT(__mask))
#define	__SHIFTIN(__x, __mask) ((__x) * __LOWEST_SET_BIT(__mask))
#define	__SHIFTOUT_MASK(__mask) __SHIFTOUT((__mask), (__mask))

#endif	/* !_IF_BWIREG_H */
