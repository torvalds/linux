/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012
 *	Ben Gray <bgray@freebsd.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _IF_SMSCREG_H_
#define _IF_SMSCREG_H_

/*
 * Definitions for the SMSC LAN9514 and LAN9514 USB to ethernet controllers.
 *
 * This information was gleaned from the SMSC driver in the linux kernel, where
 * it is Copyrighted (C) 2007-2008 SMSC.
 *
 */

/**
 * TRANSMIT FRAMES
 * ---------------
 *   Tx frames are prefixed with an 8-byte header which describes the frame
 *
 *         4 bytes      4 bytes           variable
 *      +------------+------------+--- . . . . . . . . . . . . ---+
 *      | TX_CTRL_0  | TX_CTRL_1  |  Ethernet frame data          |
 *      +------------+------------+--- . . . . . . . . . . . . ---+
 *
 *   Where the headers have the following fields:
 *
 *      TX_CTRL_0 <20:16>  Data offset
 *      TX_CTRL_0 <13>     First segment of frame indicator
 *      TX_CTRL_0 <12>     Last segment of frame indicator
 *      TX_CTRL_0 <10:0>   Buffer size (?)
 *
 *      TX_CTRL_1 <14>     Perform H/W checksuming on IP packets 
 *      TX_CTRL_1 <13>     Disable automatic ethernet CRC generation
 *      TX_CTRL_1 <12>     Disable padding (?)
 *      TX_CTRL_1 <10:0>   Packet byte length
 *
 */
#define SMSC_TX_CTRL_0_OFFSET(x)         (((x) & 0x1FUL) << 16)
#define SMSC_TX_CTRL_0_FIRST_SEG         (0x1UL << 13)
#define SMSC_TX_CTRL_0_LAST_SEG          (0x1UL << 12)
#define SMSC_TX_CTRL_0_BUF_SIZE(x)       ((x) & 0x000007FFUL)

#define SMSC_TX_CTRL_1_CSUM_ENABLE       (0x1UL << 14)
#define SMSC_TX_CTRL_1_CRC_DISABLE       (0x1UL << 13)
#define SMSC_TX_CTRL_1_PADDING_DISABLE   (0x1UL << 12)
#define SMSC_TX_CTRL_1_PKT_LENGTH(x)     ((x) & 0x000007FFUL)

/**
 * RECEIVE FRAMES
 * --------------
 *   Rx frames are prefixed with an 4-byte status header which describes any
 *   errors with the frame as well as things like the length
 *
 *         4 bytes             variable
 *      +------------+--- . . . . . . . . . . . . ---+
 *      |   RX_STAT  |  Ethernet frame data          |
 *      +------------+--- . . . . . . . . . . . . ---+
 *
 *   Where the status header has the following fields:
 *
 *      RX_STAT   <30>     Filter Fail
 *      RX_STAT   <29:16>  Frame Length
 *      RX_STAT   <15>     Error Summary
 *      RX_STAT   <13>     Broadcast Frame
 *      RX_STAT   <12>     Length Error
 *      RX_STAT   <11>     Runt Frame
 *      RX_STAT   <10>     Multicast Frame
 *      RX_STAT   <7>      Frame too long
 *      RX_STAT   <6>      Collision Seen
 *      RX_STAT   <5>      Frame Type
 *      RX_STAT   <4>      Receive Watchdog
 *      RX_STAT   <3>      Mii Error
 *      RX_STAT   <2>      Dribbling
 *      RX_STAT   <1>      CRC Error
 *
 */
#define SMSC_RX_STAT_FILTER_FAIL         (0x1UL << 30)
#define SMSC_RX_STAT_FRM_LENGTH(x)       (((x) >> 16) & 0x3FFFUL)
#define SMSC_RX_STAT_ERROR               (0x1UL << 15)
#define SMSC_RX_STAT_BROADCAST           (0x1UL << 13)
#define SMSC_RX_STAT_LENGTH_ERROR        (0x1UL << 12)
#define SMSC_RX_STAT_RUNT                (0x1UL << 11)
#define SMSC_RX_STAT_MULTICAST           (0x1UL << 10)
#define SMSC_RX_STAT_FRM_TO_LONG         (0x1UL << 7)
#define SMSC_RX_STAT_COLLISION           (0x1UL << 6)
#define SMSC_RX_STAT_FRM_TYPE            (0x1UL << 5)
#define SMSC_RX_STAT_WATCHDOG            (0x1UL << 4)
#define SMSC_RX_STAT_MII_ERROR           (0x1UL << 3)
#define SMSC_RX_STAT_DRIBBLING           (0x1UL << 2)
#define SMSC_RX_STAT_CRC_ERROR           (0x1UL << 1)

/**
 * REGISTERS
 *
 */
#define SMSC_ID_REV                 0x000
#define SMSC_INTR_STATUS            0x008
#define SMSC_RX_CFG                 0x00C
#define SMSC_TX_CFG                 0x010
#define SMSC_HW_CFG                 0x014
#define SMSC_PM_CTRL                0x020
#define SMSC_LED_GPIO_CFG           0x024
#define SMSC_GPIO_CFG               0x028
#define SMSC_AFC_CFG                0x02C
#define SMSC_EEPROM_CMD             0x030
#define SMSC_EEPROM_DATA            0x034
#define SMSC_BURST_CAP              0x038
#define SMSC_GPIO_WAKE              0x064
#define SMSC_INTR_CFG               0x068
#define SMSC_BULK_IN_DLY            0x06C
#define SMSC_MAC_CSR                0x100
#define SMSC_MAC_ADDRH              0x104
#define SMSC_MAC_ADDRL              0x108
#define SMSC_HASHH                  0x10C
#define SMSC_HASHL                  0x110
#define SMSC_MII_ADDR               0x114
#define SMSC_MII_DATA               0x118
#define SMSC_FLOW                   0x11C
#define SMSC_VLAN1                  0x120
#define SMSC_VLAN2                  0x124
#define SMSC_WUFF                   0x128
#define SMSC_WUCSR                  0x12C
#define SMSC_COE_CTRL               0x130

/* ID / Revision register */
#define SMSC_ID_REV_CHIP_ID_MASK    0xFFFF0000UL
#define SMSC_ID_REV_CHIP_REV_MASK   0x0000FFFFUL

#define SMSC_RX_FIFO_FLUSH          (0x1UL << 0)

#define SMSC_TX_CFG_ON              (0x1UL << 2)
#define SMSC_TX_CFG_STOP            (0x1UL << 1)
#define SMSC_TX_CFG_FIFO_FLUSH      (0x1UL << 0)

#define SMSC_HW_CFG_BIR             (0x1UL << 12)
#define SMSC_HW_CFG_LEDB            (0x1UL << 11)
#define SMSC_HW_CFG_RXDOFF          (0x3UL << 9)    /* RX pkt alignment */
#define SMSC_HW_CFG_DRP             (0x1UL << 6)
#define SMSC_HW_CFG_MEF             (0x1UL << 5)
#define SMSC_HW_CFG_LRST            (0x1UL << 3)    /* Lite reset */
#define SMSC_HW_CFG_PSEL            (0x1UL << 2)
#define SMSC_HW_CFG_BCE             (0x1UL << 1)
#define SMSC_HW_CFG_SRST            (0x1UL << 0)

#define SMSC_PM_CTRL_PHY_RST        (0x1UL << 4)    /* PHY reset */

#define SMSC_LED_GPIO_CFG_SPD_LED   (0x1UL << 24)
#define SMSC_LED_GPIO_CFG_LNK_LED   (0x1UL << 20)
#define SMSC_LED_GPIO_CFG_FDX_LED   (0x1UL << 16)

/* Hi watermark = 15.5Kb (~10 mtu pkts) */
/* low watermark = 3k (~2 mtu pkts) */
/* backpressure duration = ~ 350us */
/* Apply FC on any frame. */
#define AFC_CFG_DEFAULT             (0x00F830A1)

#define SMSC_EEPROM_CMD_BUSY        (0x1UL << 31)
#define SMSC_EEPROM_CMD_MASK        (0x7UL << 28)
#define SMSC_EEPROM_CMD_READ        (0x0UL << 28)
#define SMSC_EEPROM_CMD_WRITE       (0x3UL << 28)
#define SMSC_EEPROM_CMD_ERASE       (0x5UL << 28)
#define SMSC_EEPROM_CMD_RELOAD      (0x7UL << 28)
#define SMSC_EEPROM_CMD_TIMEOUT     (0x1UL << 10)
#define SMSC_EEPROM_CMD_ADDR_MASK   0x000001FFUL

/* MAC Control and Status Register */
#define SMSC_MAC_CSR_RCVOWN         (0x1UL << 23)  /* Half duplex */
#define SMSC_MAC_CSR_LOOPBK         (0x1UL << 21)  /* Loopback */
#define SMSC_MAC_CSR_FDPX           (0x1UL << 20)  /* Full duplex */
#define SMSC_MAC_CSR_MCPAS          (0x1UL << 19)  /* Multicast mode */
#define SMSC_MAC_CSR_PRMS           (0x1UL << 18)  /* Promiscuous mode */
#define SMSC_MAC_CSR_INVFILT        (0x1UL << 17)  /* Inverse filtering */
#define SMSC_MAC_CSR_PASSBAD        (0x1UL << 16)  /* Pass on bad frames */
#define SMSC_MAC_CSR_HPFILT         (0x1UL << 13)  /* Hash filtering */
#define SMSC_MAC_CSR_BCAST          (0x1UL << 11)  /* Broadcast */
#define SMSC_MAC_CSR_TXEN           (0x1UL << 3)   /* TX enable */
#define SMSC_MAC_CSR_RXEN           (0x1UL << 2)   /* RX enable */

/* Interrupt control register */
#define SMSC_INTR_NTEP              (0x1UL << 31) 
#define SMSC_INTR_MACRTO            (0x1UL << 19)
#define SMSC_INTR_TX_STOP           (0x1UL << 17)
#define SMSC_INTR_RX_STOP           (0x1UL << 16)
#define SMSC_INTR_PHY_INT           (0x1UL << 15)
#define SMSC_INTR_TXE               (0x1UL << 14)
#define SMSC_INTR_TDFU              (0x1UL << 13)
#define SMSC_INTR_TDFO              (0x1UL << 12)
#define SMSC_INTR_RXDF              (0x1UL << 11)
#define SMSC_INTR_GPIOS             0x000007FFUL

/* Phy MII interface register */
#define SMSC_MII_WRITE              (0x1UL << 1)
#define SMSC_MII_READ               (0x0UL << 1)
#define SMSC_MII_BUSY               (0x1UL << 0)

/* H/W checksum register */
#define SMSC_COE_CTRL_TX_EN         (0x1UL << 16)  /* Tx H/W csum enable */
#define SMSC_COE_CTRL_RX_MODE       (0x1UL << 1)
#define SMSC_COE_CTRL_RX_EN         (0x1UL << 0)   /* Rx H/W csum enable */

/* Registers on the phy, accessed via MII/MDIO */
#define SMSC_PHY_INTR_STAT          (29)
#define SMSC_PHY_INTR_MASK          (30)

#define SMSC_PHY_INTR_ENERGY_ON     (0x1U << 7)
#define SMSC_PHY_INTR_ANEG_COMP     (0x1U << 6)
#define SMSC_PHY_INTR_REMOTE_FAULT  (0x1U << 5)
#define SMSC_PHY_INTR_LINK_DOWN     (0x1U << 4)

/* USB Vendor Requests */
#define SMSC_UR_WRITE_REG   0xA0
#define SMSC_UR_READ_REG    0xA1
#define SMSC_UR_GET_STATS   0xA2

#define	SMSC_CONFIG_INDEX	0	/* config number 1 */
#define	SMSC_IFACE_IDX		0

/*
 * USB endpoints.
 */
enum {
	SMSC_BULK_DT_RD,
	SMSC_BULK_DT_WR,
	/* the LAN9514 device does support interrupt endpoints, however I couldn't
	 * get then to work reliably and since they are unneeded (poll the mii
	 * status) they are unused.
	 * SMSC_INTR_DT_WR,
	 * SMSC_INTR_DT_RD,
	 */
	SMSC_N_TRANSFER,
};

struct smsc_softc {
	struct usb_ether  sc_ue;
	struct mtx        sc_mtx;
	struct usb_xfer  *sc_xfer[SMSC_N_TRANSFER];
	int               sc_phyno;

	/* The following stores the settings in the mac control (MAC_CSR) register */
	uint32_t          sc_mac_csr;
	uint32_t          sc_rev_id;

	uint32_t          sc_flags;
#define	SMSC_FLAG_LINK      0x0001
#define	SMSC_FLAG_LAN9514   0x1000	/* LAN9514 */
};

#define	SMSC_LOCK(_sc)             mtx_lock(&(_sc)->sc_mtx)
#define	SMSC_UNLOCK(_sc)           mtx_unlock(&(_sc)->sc_mtx)
#define	SMSC_LOCK_ASSERT(_sc, t)   mtx_assert(&(_sc)->sc_mtx, t)

#endif  /* _IF_SMSCREG_H_ */
