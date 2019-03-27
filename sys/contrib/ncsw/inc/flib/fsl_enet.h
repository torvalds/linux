/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __FSL_ENET_H
#define __FSL_ENET_H

/**
 @Description  Ethernet MAC-PHY Interface
*/

enum enet_interface {
	E_ENET_IF_MII		= 0x00010000, /**< MII interface */
	E_ENET_IF_RMII		= 0x00020000, /**< RMII interface */
	E_ENET_IF_SMII		= 0x00030000, /**< SMII interface */
	E_ENET_IF_GMII		= 0x00040000, /**< GMII interface */
	E_ENET_IF_RGMII		= 0x00050000, /**< RGMII interface */
	E_ENET_IF_TBI		= 0x00060000, /**< TBI interface */
	E_ENET_IF_RTBI		= 0x00070000, /**< RTBI interface */
	E_ENET_IF_SGMII		= 0x00080000, /**< SGMII interface */
	E_ENET_IF_XGMII		= 0x00090000, /**< XGMII interface */
	E_ENET_IF_QSGMII	= 0x000a0000, /**< QSGMII interface */
	E_ENET_IF_XFI		= 0x000b0000  /**< XFI interface */
};

/**
 @Description  Ethernet Speed (nominal data rate)
*/
enum enet_speed {
	E_ENET_SPEED_10		= 10,	/**< 10 Mbps */
	E_ENET_SPEED_100	= 100,	/**< 100 Mbps */
	E_ENET_SPEED_1000	= 1000,	/**< 1000 Mbps = 1 Gbps */
	E_ENET_SPEED_2500	= 2500,	/**< 2500 Mbps = 2.5 Gbps */
	E_ENET_SPEED_10000	= 10000	/**< 10000 Mbps = 10 Gbps */
};

enum mac_type {
	E_MAC_DTSEC,
	E_MAC_TGEC,
	E_MAC_MEMAC
};

/**************************************************************************//**
 @Description   Enum for inter-module interrupts registration
*//***************************************************************************/
enum fman_event_modules {
	E_FMAN_MOD_PRS,                   /**< Parser event */
	E_FMAN_MOD_KG,                    /**< Keygen event */
	E_FMAN_MOD_PLCR,                  /**< Policer event */
	E_FMAN_MOD_10G_MAC,               /**< 10G MAC event */
	E_FMAN_MOD_1G_MAC,                /**< 1G MAC event */
	E_FMAN_MOD_TMR,                   /**< Timer event */
	E_FMAN_MOD_FMAN_CTRL,             /**< FMAN Controller  Timer event */
	E_FMAN_MOD_MACSEC,
	E_FMAN_MOD_DUMMY_LAST
};

/**************************************************************************//**
 @Description   Enum for interrupts types
*//***************************************************************************/
enum fman_intr_type {
	E_FMAN_INTR_TYPE_ERR,
	E_FMAN_INTR_TYPE_NORMAL
};

/**************************************************************************//**
 @Description   enum for defining MAC types
*//***************************************************************************/
enum fman_mac_type {
	E_FMAN_MAC_10G = 0,               /**< 10G MAC */
	E_FMAN_MAC_1G                     /**< 1G MAC */
};

enum fman_mac_exceptions {
	E_FMAN_MAC_EX_10G_MDIO_SCAN_EVENTMDIO = 0,
		/**< 10GEC MDIO scan event interrupt */
	E_FMAN_MAC_EX_10G_MDIO_CMD_CMPL,
		/**< 10GEC MDIO command completion interrupt */
	E_FMAN_MAC_EX_10G_REM_FAULT,
		/**< 10GEC, mEMAC Remote fault interrupt */
	E_FMAN_MAC_EX_10G_LOC_FAULT,
		/**< 10GEC, mEMAC Local fault interrupt */
	E_FMAN_MAC_EX_10G_1TX_ECC_ER,
		/**< 10GEC, mEMAC Transmit frame ECC error interrupt */
	E_FMAN_MAC_EX_10G_TX_FIFO_UNFL,
		/**< 10GEC, mEMAC Transmit FIFO underflow interrupt */
	E_FMAN_MAC_EX_10G_TX_FIFO_OVFL,
		/**< 10GEC, mEMAC Transmit FIFO overflow interrupt */
	E_FMAN_MAC_EX_10G_TX_ER,
		/**< 10GEC Transmit frame error interrupt */
	E_FMAN_MAC_EX_10G_RX_FIFO_OVFL,
		/**< 10GEC, mEMAC Receive FIFO overflow interrupt */
	E_FMAN_MAC_EX_10G_RX_ECC_ER,
		/**< 10GEC, mEMAC Receive frame ECC error interrupt */
	E_FMAN_MAC_EX_10G_RX_JAB_FRM,
		/**< 10GEC Receive jabber frame interrupt */
	E_FMAN_MAC_EX_10G_RX_OVRSZ_FRM,
		/**< 10GEC Receive oversized frame interrupt */
	E_FMAN_MAC_EX_10G_RX_RUNT_FRM,
		/**< 10GEC Receive runt frame interrupt */
	E_FMAN_MAC_EX_10G_RX_FRAG_FRM,
		/**< 10GEC Receive fragment frame interrupt */
	E_FMAN_MAC_EX_10G_RX_LEN_ER,
		/**< 10GEC Receive payload length error interrupt */
	E_FMAN_MAC_EX_10G_RX_CRC_ER,
		/**< 10GEC Receive CRC error interrupt */
	E_FMAN_MAC_EX_10G_RX_ALIGN_ER,
		/**< 10GEC Receive alignment error interrupt */
	E_FMAN_MAC_EX_1G_BAB_RX,
		/**< dTSEC Babbling receive error */
	E_FMAN_MAC_EX_1G_RX_CTL,
		/**< dTSEC Receive control (pause frame) interrupt */
	E_FMAN_MAC_EX_1G_GRATEFUL_TX_STP_COMPLET,
		/**< dTSEC Graceful transmit stop complete */
	E_FMAN_MAC_EX_1G_BAB_TX,
		/**< dTSEC Babbling transmit error */
	E_FMAN_MAC_EX_1G_TX_CTL,
		/**< dTSEC Transmit control (pause frame) interrupt */
	E_FMAN_MAC_EX_1G_TX_ERR,
		/**< dTSEC Transmit error */
	E_FMAN_MAC_EX_1G_LATE_COL,
		/**< dTSEC Late collision */
	E_FMAN_MAC_EX_1G_COL_RET_LMT,
		/**< dTSEC Collision retry limit */
	E_FMAN_MAC_EX_1G_TX_FIFO_UNDRN,
		/**< dTSEC Transmit FIFO underrun */
	E_FMAN_MAC_EX_1G_MAG_PCKT,
		/**< dTSEC Magic Packet detection */
	E_FMAN_MAC_EX_1G_MII_MNG_RD_COMPLET,
		/**< dTSEC MII management read completion */
	E_FMAN_MAC_EX_1G_MII_MNG_WR_COMPLET,
		/**< dTSEC MII management write completion */
	E_FMAN_MAC_EX_1G_GRATEFUL_RX_STP_COMPLET,
		/**< dTSEC Graceful receive stop complete */
	E_FMAN_MAC_EX_1G_TX_DATA_ERR,
		/**< dTSEC Internal data error on transmit */
	E_FMAN_MAC_EX_1G_RX_DATA_ERR,
		/**< dTSEC Internal data error on receive */
	E_FMAN_MAC_EX_1G_1588_TS_RX_ERR,
		/**< dTSEC Time-Stamp Receive Error */
	E_FMAN_MAC_EX_1G_RX_MIB_CNT_OVFL,
		/**< dTSEC MIB counter overflow */
	E_FMAN_MAC_EX_TS_FIFO_ECC_ERR,
		/**< mEMAC Time-stamp FIFO ECC error interrupt;
		not supported on T4240/B4860 rev1 chips */
};

#define ENET_IF_SGMII_BASEX 0x80000000
	/**< SGMII/QSGII interface with 1000BaseX auto-negotiation between MAC
	and phy or backplane;
	Note: 1000BaseX auto-negotiation relates only to interface between MAC
	and phy/backplane, SGMII phy can still synchronize with far-end phy at
	10Mbps, 100Mbps or 1000Mbps */

enum enet_mode {
	E_ENET_MODE_INVALID           = 0,
		/**< Invalid Ethernet mode */
	E_ENET_MODE_MII_10            = (E_ENET_IF_MII   | E_ENET_SPEED_10),
		/**<    10 Mbps MII   */
	E_ENET_MODE_MII_100           = (E_ENET_IF_MII   | E_ENET_SPEED_100),
		/**<   100 Mbps MII   */
	E_ENET_MODE_RMII_10           = (E_ENET_IF_RMII  | E_ENET_SPEED_10),
		/**<    10 Mbps RMII  */
	E_ENET_MODE_RMII_100          = (E_ENET_IF_RMII  | E_ENET_SPEED_100),
		/**<   100 Mbps RMII  */
	E_ENET_MODE_SMII_10           = (E_ENET_IF_SMII  | E_ENET_SPEED_10),
		/**<    10 Mbps SMII  */
	E_ENET_MODE_SMII_100          = (E_ENET_IF_SMII  | E_ENET_SPEED_100),
		/**<   100 Mbps SMII  */
	E_ENET_MODE_GMII_1000         = (E_ENET_IF_GMII  | E_ENET_SPEED_1000),
		/**<  1000 Mbps GMII  */
	E_ENET_MODE_RGMII_10          = (E_ENET_IF_RGMII | E_ENET_SPEED_10),
		/**<    10 Mbps RGMII */
	E_ENET_MODE_RGMII_100         = (E_ENET_IF_RGMII | E_ENET_SPEED_100),
		/**<   100 Mbps RGMII */
	E_ENET_MODE_RGMII_1000        = (E_ENET_IF_RGMII | E_ENET_SPEED_1000),
		/**<  1000 Mbps RGMII */
	E_ENET_MODE_TBI_1000          = (E_ENET_IF_TBI   | E_ENET_SPEED_1000),
		/**<  1000 Mbps TBI   */
	E_ENET_MODE_RTBI_1000         = (E_ENET_IF_RTBI  | E_ENET_SPEED_1000),
		/**<  1000 Mbps RTBI  */
	E_ENET_MODE_SGMII_10          = (E_ENET_IF_SGMII | E_ENET_SPEED_10),
		/**< 10 Mbps SGMII with auto-negotiation between MAC and
		SGMII phy according to Cisco SGMII specification */
	E_ENET_MODE_SGMII_100         = (E_ENET_IF_SGMII | E_ENET_SPEED_100),
		/**< 100 Mbps SGMII with auto-negotiation between MAC and
		SGMII phy according to Cisco SGMII specification */
	E_ENET_MODE_SGMII_1000        = (E_ENET_IF_SGMII | E_ENET_SPEED_1000),
		/**< 1000 Mbps SGMII with auto-negotiation between MAC and
		SGMII phy according to Cisco SGMII specification */
	E_ENET_MODE_SGMII_BASEX_10    = (ENET_IF_SGMII_BASEX | E_ENET_IF_SGMII
		| E_ENET_SPEED_10),
		/**< 10 Mbps SGMII with 1000BaseX auto-negotiation between
		MAC and SGMII phy or backplane */
	E_ENET_MODE_SGMII_BASEX_100   = (ENET_IF_SGMII_BASEX | E_ENET_IF_SGMII
		| E_ENET_SPEED_100),
		/**< 100 Mbps SGMII with 1000BaseX auto-negotiation between
		MAC and SGMII phy or backplane */
	E_ENET_MODE_SGMII_BASEX_1000  = (ENET_IF_SGMII_BASEX | E_ENET_IF_SGMII
		| E_ENET_SPEED_1000),
		/**< 1000 Mbps SGMII with 1000BaseX auto-negotiation between
		MAC and SGMII phy or backplane */
	E_ENET_MODE_QSGMII_1000       = (E_ENET_IF_QSGMII | E_ENET_SPEED_1000),
		/**< 1000 Mbps QSGMII with auto-negotiation between MAC and
		QSGMII phy according to Cisco QSGMII specification */
	E_ENET_MODE_QSGMII_BASEX_1000 = (ENET_IF_SGMII_BASEX | E_ENET_IF_QSGMII
		| E_ENET_SPEED_1000),
		/**< 1000 Mbps QSGMII with 1000BaseX auto-negotiation between
		MAC and QSGMII phy or backplane */
	E_ENET_MODE_XGMII_10000       = (E_ENET_IF_XGMII | E_ENET_SPEED_10000),
		/**< 10000 Mbps XGMII */
	E_ENET_MODE_XFI_10000         = (E_ENET_IF_XFI   | E_ENET_SPEED_10000)
		/**< 10000 Mbps XFI */
};

enum fmam_mac_statistics_level {
	E_FMAN_MAC_NONE_STATISTICS,	/**< No statistics */
	E_FMAN_MAC_PARTIAL_STATISTICS,	/**< Only error counters are available;
					Optimized for performance */
	E_FMAN_MAC_FULL_STATISTICS	/**< All counters available; Not
					optimized for performance */
};

#define _MAKE_ENET_MODE(_interface, _speed) (enum enet_mode)((_interface) \
	| (_speed))

#define _ENET_INTERFACE_FROM_MODE(mode) (enum enet_interface) \
	((mode) & 0x0FFF0000)
#define _ENET_SPEED_FROM_MODE(mode) (enum enet_speed)((mode) & 0x0000FFFF)
#define _ENET_ADDR_TO_UINT64(_enet_addr)		\
	(uint64_t)(((uint64_t)(_enet_addr)[0] << 40) |	\
		((uint64_t)(_enet_addr)[1] << 32) |	\
		((uint64_t)(_enet_addr)[2] << 24) |	\
		((uint64_t)(_enet_addr)[3] << 16) |	\
		((uint64_t)(_enet_addr)[4] << 8) |	\
		((uint64_t)(_enet_addr)[5]))

#define _MAKE_ENET_ADDR_FROM_UINT64(_addr64, _enet_addr)		\
	do {								\
		int i;							\
		for (i = 0; i < ENET_NUM_OCTETS_PER_ADDRESS; i++)	\
			(_enet_addr)[i] = (uint8_t)((_addr64) >> ((5-i)*8));\
	} while (0)

#endif /* __FSL_ENET_H */
