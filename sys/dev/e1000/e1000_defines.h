/******************************************************************************
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2001-2015, Intel Corporation 
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/

#ifndef _E1000_DEFINES_H_
#define _E1000_DEFINES_H_

/* Number of Transmit and Receive Descriptors must be a multiple of 8 */
#define REQ_TX_DESCRIPTOR_MULTIPLE  8
#define REQ_RX_DESCRIPTOR_MULTIPLE  8

/* Definitions for power management and wakeup registers */
/* Wake Up Control */
#define E1000_WUC_APME		0x00000001 /* APM Enable */
#define E1000_WUC_PME_EN	0x00000002 /* PME Enable */
#define E1000_WUC_PME_STATUS	0x00000004 /* PME Status */
#define E1000_WUC_APMPME	0x00000008 /* Assert PME on APM Wakeup */
#define E1000_WUC_PHY_WAKE	0x00000100 /* if PHY supports wakeup */

/* Wake Up Filter Control */
#define E1000_WUFC_LNKC	0x00000001 /* Link Status Change Wakeup Enable */
#define E1000_WUFC_MAG	0x00000002 /* Magic Packet Wakeup Enable */
#define E1000_WUFC_EX	0x00000004 /* Directed Exact Wakeup Enable */
#define E1000_WUFC_MC	0x00000008 /* Directed Multicast Wakeup Enable */
#define E1000_WUFC_BC	0x00000010 /* Broadcast Wakeup Enable */
#define E1000_WUFC_ARP	0x00000020 /* ARP Request Packet Wakeup Enable */
#define E1000_WUFC_IPV4	0x00000040 /* Directed IPv4 Packet Wakeup Enable */
#define E1000_WUFC_FLX0		0x00010000 /* Flexible Filter 0 Enable */

/* Wake Up Status */
#define E1000_WUS_LNKC		E1000_WUFC_LNKC
#define E1000_WUS_MAG		E1000_WUFC_MAG
#define E1000_WUS_EX		E1000_WUFC_EX
#define E1000_WUS_MC		E1000_WUFC_MC
#define E1000_WUS_BC		E1000_WUFC_BC

/* Extended Device Control */
#define E1000_CTRL_EXT_LPCD		0x00000004 /* LCD Power Cycle Done */
#define E1000_CTRL_EXT_SDP4_DATA	0x00000010 /* SW Definable Pin 4 data */
#define E1000_CTRL_EXT_SDP6_DATA	0x00000040 /* SW Definable Pin 6 data */
#define E1000_CTRL_EXT_SDP3_DATA	0x00000080 /* SW Definable Pin 3 data */
/* SDP 4/5 (bits 8,9) are reserved in >= 82575 */
#define E1000_CTRL_EXT_SDP4_DIR	0x00000100 /* Direction of SDP4 0=in 1=out */
#define E1000_CTRL_EXT_SDP6_DIR	0x00000400 /* Direction of SDP6 0=in 1=out */
#define E1000_CTRL_EXT_SDP3_DIR	0x00000800 /* Direction of SDP3 0=in 1=out */
#define E1000_CTRL_EXT_FORCE_SMBUS	0x00000800 /* Force SMBus mode */
#define E1000_CTRL_EXT_EE_RST	0x00002000 /* Reinitialize from EEPROM */
/* Physical Func Reset Done Indication */
#define E1000_CTRL_EXT_PFRSTD	0x00004000
#define E1000_CTRL_EXT_SDLPE	0X00040000  /* SerDes Low Power Enable */
#define E1000_CTRL_EXT_SPD_BYPS	0x00008000 /* Speed Select Bypass */
#define E1000_CTRL_EXT_RO_DIS	0x00020000 /* Relaxed Ordering disable */
#define E1000_CTRL_EXT_DMA_DYN_CLK_EN	0x00080000 /* DMA Dynamic Clk Gating */
#define E1000_CTRL_EXT_LINK_MODE_MASK	0x00C00000
/* Offset of the link mode field in Ctrl Ext register */
#define E1000_CTRL_EXT_LINK_MODE_OFFSET	22
#define E1000_CTRL_EXT_LINK_MODE_1000BASE_KX	0x00400000
#define E1000_CTRL_EXT_LINK_MODE_GMII	0x00000000
#define E1000_CTRL_EXT_LINK_MODE_PCIE_SERDES	0x00C00000
#define E1000_CTRL_EXT_LINK_MODE_SGMII	0x00800000
#define E1000_CTRL_EXT_EIAME		0x01000000
#define E1000_CTRL_EXT_IRCA		0x00000001
#define E1000_CTRL_EXT_DRV_LOAD		0x10000000 /* Drv loaded bit for FW */
#define E1000_CTRL_EXT_IAME		0x08000000 /* Int ACK Auto-mask */
#define E1000_CTRL_EXT_PBA_CLR		0x80000000 /* PBA Clear */
#define E1000_CTRL_EXT_LSECCK		0x00001000
#define E1000_CTRL_EXT_PHYPDEN		0x00100000
#define E1000_I2CCMD_REG_ADDR_SHIFT	16
#define E1000_I2CCMD_PHY_ADDR_SHIFT	24
#define E1000_I2CCMD_OPCODE_READ	0x08000000
#define E1000_I2CCMD_OPCODE_WRITE	0x00000000
#define E1000_I2CCMD_READY		0x20000000
#define E1000_I2CCMD_ERROR		0x80000000
#define E1000_I2CCMD_SFP_DATA_ADDR(a)	(0x0000 + (a))
#define E1000_I2CCMD_SFP_DIAG_ADDR(a)	(0x0100 + (a))
#define E1000_MAX_SGMII_PHY_REG_ADDR	255
#define E1000_I2CCMD_PHY_TIMEOUT	200
#define E1000_IVAR_VALID	0x80
#define E1000_GPIE_NSICR	0x00000001
#define E1000_GPIE_MSIX_MODE	0x00000010
#define E1000_GPIE_EIAME	0x40000000
#define E1000_GPIE_PBA		0x80000000

/* Receive Descriptor bit definitions */
#define E1000_RXD_STAT_DD	0x01    /* Descriptor Done */
#define E1000_RXD_STAT_EOP	0x02    /* End of Packet */
#define E1000_RXD_STAT_IXSM	0x04    /* Ignore checksum */
#define E1000_RXD_STAT_VP	0x08    /* IEEE VLAN Packet */
#define E1000_RXD_STAT_UDPCS	0x10    /* UDP xsum calculated */
#define E1000_RXD_STAT_TCPCS	0x20    /* TCP xsum calculated */
#define E1000_RXD_STAT_IPCS	0x40    /* IP xsum calculated */
#define E1000_RXD_STAT_PIF	0x80    /* passed in-exact filter */
#define E1000_RXD_STAT_IPIDV	0x200   /* IP identification valid */
#define E1000_RXD_STAT_UDPV	0x400   /* Valid UDP checksum */
#define E1000_RXD_STAT_DYNINT	0x800   /* Pkt caused INT via DYNINT */
#define E1000_RXD_ERR_CE	0x01    /* CRC Error */
#define E1000_RXD_ERR_SE	0x02    /* Symbol Error */
#define E1000_RXD_ERR_SEQ	0x04    /* Sequence Error */
#define E1000_RXD_ERR_CXE	0x10    /* Carrier Extension Error */
#define E1000_RXD_ERR_TCPE	0x20    /* TCP/UDP Checksum Error */
#define E1000_RXD_ERR_IPE	0x40    /* IP Checksum Error */
#define E1000_RXD_ERR_RXE	0x80    /* Rx Data Error */
#define E1000_RXD_SPC_VLAN_MASK	0x0FFF  /* VLAN ID is in lower 12 bits */

#define E1000_RXDEXT_STATERR_TST	0x00000100 /* Time Stamp taken */
#define E1000_RXDEXT_STATERR_LB		0x00040000
#define E1000_RXDEXT_STATERR_CE		0x01000000
#define E1000_RXDEXT_STATERR_SE		0x02000000
#define E1000_RXDEXT_STATERR_SEQ	0x04000000
#define E1000_RXDEXT_STATERR_CXE	0x10000000
#define E1000_RXDEXT_STATERR_TCPE	0x20000000
#define E1000_RXDEXT_STATERR_IPE	0x40000000
#define E1000_RXDEXT_STATERR_RXE	0x80000000

/* mask to determine if packets should be dropped due to frame errors */
#define E1000_RXD_ERR_FRAME_ERR_MASK ( \
	E1000_RXD_ERR_CE  |		\
	E1000_RXD_ERR_SE  |		\
	E1000_RXD_ERR_SEQ |		\
	E1000_RXD_ERR_CXE |		\
	E1000_RXD_ERR_RXE)

/* Same mask, but for extended and packet split descriptors */
#define E1000_RXDEXT_ERR_FRAME_ERR_MASK ( \
	E1000_RXDEXT_STATERR_CE  |	\
	E1000_RXDEXT_STATERR_SE  |	\
	E1000_RXDEXT_STATERR_SEQ |	\
	E1000_RXDEXT_STATERR_CXE |	\
	E1000_RXDEXT_STATERR_RXE)

#define E1000_MRQC_RSS_ENABLE_2Q		0x00000001
#define E1000_MRQC_RSS_FIELD_MASK		0xFFFF0000
#define E1000_MRQC_RSS_FIELD_IPV4_TCP		0x00010000
#define E1000_MRQC_RSS_FIELD_IPV4		0x00020000
#define E1000_MRQC_RSS_FIELD_IPV6_TCP_EX	0x00040000
#define E1000_MRQC_RSS_FIELD_IPV6_EX		0x00080000
#define E1000_MRQC_RSS_FIELD_IPV6		0x00100000
#define E1000_MRQC_RSS_FIELD_IPV6_TCP		0x00200000

#define E1000_RXDPS_HDRSTAT_HDRSP		0x00008000

/* Management Control */
#define E1000_MANC_SMBUS_EN	0x00000001 /* SMBus Enabled - RO */
#define E1000_MANC_ASF_EN	0x00000002 /* ASF Enabled - RO */
#define E1000_MANC_ARP_EN	0x00002000 /* Enable ARP Request Filtering */
#define E1000_MANC_RCV_TCO_EN	0x00020000 /* Receive TCO Packets Enabled */
#define E1000_MANC_BLK_PHY_RST_ON_IDE	0x00040000 /* Block phy resets */
/* Enable MAC address filtering */
#define E1000_MANC_EN_MAC_ADDR_FILTER	0x00100000
/* Enable MNG packets to host memory */
#define E1000_MANC_EN_MNG2HOST		0x00200000

#define E1000_MANC2H_PORT_623		0x00000020 /* Port 0x26f */
#define E1000_MANC2H_PORT_664		0x00000040 /* Port 0x298 */
#define E1000_MDEF_PORT_623		0x00000800 /* Port 0x26f */
#define E1000_MDEF_PORT_664		0x00000400 /* Port 0x298 */

/* Receive Control */
#define E1000_RCTL_RST		0x00000001 /* Software reset */
#define E1000_RCTL_EN		0x00000002 /* enable */
#define E1000_RCTL_SBP		0x00000004 /* store bad packet */
#define E1000_RCTL_UPE		0x00000008 /* unicast promisc enable */
#define E1000_RCTL_MPE		0x00000010 /* multicast promisc enable */
#define E1000_RCTL_LPE		0x00000020 /* long packet enable */
#define E1000_RCTL_LBM_NO	0x00000000 /* no loopback mode */
#define E1000_RCTL_LBM_MAC	0x00000040 /* MAC loopback mode */
#define E1000_RCTL_LBM_TCVR	0x000000C0 /* tcvr loopback mode */
#define E1000_RCTL_DTYP_PS	0x00000400 /* Packet Split descriptor */
#define E1000_RCTL_RDMTS_HALF	0x00000000 /* Rx desc min thresh size */
#define E1000_RCTL_RDMTS_HEX	0x00010000
#define E1000_RCTL_RDMTS1_HEX	E1000_RCTL_RDMTS_HEX
#define E1000_RCTL_MO_SHIFT	12 /* multicast offset shift */
#define E1000_RCTL_MO_3		0x00003000 /* multicast offset 15:4 */
#define E1000_RCTL_BAM		0x00008000 /* broadcast enable */
/* these buffer sizes are valid if E1000_RCTL_BSEX is 0 */
#define E1000_RCTL_SZ_2048	0x00000000 /* Rx buffer size 2048 */
#define E1000_RCTL_SZ_1024	0x00010000 /* Rx buffer size 1024 */
#define E1000_RCTL_SZ_512	0x00020000 /* Rx buffer size 512 */
#define E1000_RCTL_SZ_256	0x00030000 /* Rx buffer size 256 */
/* these buffer sizes are valid if E1000_RCTL_BSEX is 1 */
#define E1000_RCTL_SZ_16384	0x00010000 /* Rx buffer size 16384 */
#define E1000_RCTL_SZ_8192	0x00020000 /* Rx buffer size 8192 */
#define E1000_RCTL_SZ_4096	0x00030000 /* Rx buffer size 4096 */
#define E1000_RCTL_VFE		0x00040000 /* vlan filter enable */
#define E1000_RCTL_CFIEN	0x00080000 /* canonical form enable */
#define E1000_RCTL_CFI		0x00100000 /* canonical form indicator */
#define E1000_RCTL_DPF		0x00400000 /* discard pause frames */
#define E1000_RCTL_PMCF		0x00800000 /* pass MAC control frames */
#define E1000_RCTL_BSEX		0x02000000 /* Buffer size extension */
#define E1000_RCTL_SECRC	0x04000000 /* Strip Ethernet CRC */

/* Use byte values for the following shift parameters
 * Usage:
 *     psrctl |= (((ROUNDUP(value0, 128) >> E1000_PSRCTL_BSIZE0_SHIFT) &
 *		  E1000_PSRCTL_BSIZE0_MASK) |
 *		((ROUNDUP(value1, 1024) >> E1000_PSRCTL_BSIZE1_SHIFT) &
 *		  E1000_PSRCTL_BSIZE1_MASK) |
 *		((ROUNDUP(value2, 1024) << E1000_PSRCTL_BSIZE2_SHIFT) &
 *		  E1000_PSRCTL_BSIZE2_MASK) |
 *		((ROUNDUP(value3, 1024) << E1000_PSRCTL_BSIZE3_SHIFT) |;
 *		  E1000_PSRCTL_BSIZE3_MASK))
 * where value0 = [128..16256],  default=256
 *       value1 = [1024..64512], default=4096
 *       value2 = [0..64512],    default=4096
 *       value3 = [0..64512],    default=0
 */

#define E1000_PSRCTL_BSIZE0_MASK	0x0000007F
#define E1000_PSRCTL_BSIZE1_MASK	0x00003F00
#define E1000_PSRCTL_BSIZE2_MASK	0x003F0000
#define E1000_PSRCTL_BSIZE3_MASK	0x3F000000

#define E1000_PSRCTL_BSIZE0_SHIFT	7    /* Shift _right_ 7 */
#define E1000_PSRCTL_BSIZE1_SHIFT	2    /* Shift _right_ 2 */
#define E1000_PSRCTL_BSIZE2_SHIFT	6    /* Shift _left_ 6 */
#define E1000_PSRCTL_BSIZE3_SHIFT	14   /* Shift _left_ 14 */

/* SWFW_SYNC Definitions */
#define E1000_SWFW_EEP_SM	0x01
#define E1000_SWFW_PHY0_SM	0x02
#define E1000_SWFW_PHY1_SM	0x04
#define E1000_SWFW_CSR_SM	0x08
#define E1000_SWFW_PHY2_SM	0x20
#define E1000_SWFW_PHY3_SM	0x40
#define E1000_SWFW_SW_MNG_SM	0x400

/* Device Control */
#define E1000_CTRL_FD		0x00000001  /* Full duplex.0=half; 1=full */
#define E1000_CTRL_PRIOR	0x00000004  /* Priority on PCI. 0=rx,1=fair */
#define E1000_CTRL_GIO_MASTER_DISABLE 0x00000004 /*Blocks new Master reqs */
#define E1000_CTRL_LRST		0x00000008  /* Link reset. 0=normal,1=reset */
#define E1000_CTRL_ASDE		0x00000020  /* Auto-speed detect enable */
#define E1000_CTRL_SLU		0x00000040  /* Set link up (Force Link) */
#define E1000_CTRL_ILOS		0x00000080  /* Invert Loss-Of Signal */
#define E1000_CTRL_SPD_SEL	0x00000300  /* Speed Select Mask */
#define E1000_CTRL_SPD_10	0x00000000  /* Force 10Mb */
#define E1000_CTRL_SPD_100	0x00000100  /* Force 100Mb */
#define E1000_CTRL_SPD_1000	0x00000200  /* Force 1Gb */
#define E1000_CTRL_FRCSPD	0x00000800  /* Force Speed */
#define E1000_CTRL_FRCDPX	0x00001000  /* Force Duplex */
#define E1000_CTRL_LANPHYPC_OVERRIDE	0x00010000 /* SW control of LANPHYPC */
#define E1000_CTRL_LANPHYPC_VALUE	0x00020000 /* SW value of LANPHYPC */
#define E1000_CTRL_MEHE		0x00080000 /* Memory Error Handling Enable */
#define E1000_CTRL_SWDPIN0	0x00040000 /* SWDPIN 0 value */
#define E1000_CTRL_SWDPIN1	0x00080000 /* SWDPIN 1 value */
#define E1000_CTRL_SWDPIN2	0x00100000 /* SWDPIN 2 value */
#define E1000_CTRL_ADVD3WUC	0x00100000 /* D3 WUC */
#define E1000_CTRL_EN_PHY_PWR_MGMT	0x00200000 /* PHY PM enable */
#define E1000_CTRL_SWDPIN3	0x00200000 /* SWDPIN 3 value */
#define E1000_CTRL_SWDPIO0	0x00400000 /* SWDPIN 0 Input or output */
#define E1000_CTRL_SWDPIO2	0x01000000 /* SWDPIN 2 input or output */
#define E1000_CTRL_SWDPIO3	0x02000000 /* SWDPIN 3 input or output */
#define E1000_CTRL_RST		0x04000000 /* Global reset */
#define E1000_CTRL_RFCE		0x08000000 /* Receive Flow Control enable */
#define E1000_CTRL_TFCE		0x10000000 /* Transmit flow control enable */
#define E1000_CTRL_VME		0x40000000 /* IEEE VLAN mode enable */
#define E1000_CTRL_PHY_RST	0x80000000 /* PHY Reset */
#define E1000_CTRL_I2C_ENA	0x02000000 /* I2C enable */

#define E1000_CTRL_MDIO_DIR		E1000_CTRL_SWDPIO2
#define E1000_CTRL_MDIO			E1000_CTRL_SWDPIN2
#define E1000_CTRL_MDC_DIR		E1000_CTRL_SWDPIO3
#define E1000_CTRL_MDC			E1000_CTRL_SWDPIN3

#define E1000_CONNSW_ENRGSRC		0x4
#define E1000_CONNSW_PHYSD		0x400
#define E1000_CONNSW_PHY_PDN		0x800
#define E1000_CONNSW_SERDESD		0x200
#define E1000_CONNSW_AUTOSENSE_CONF	0x2
#define E1000_CONNSW_AUTOSENSE_EN	0x1
#define E1000_PCS_CFG_PCS_EN		8
#define E1000_PCS_LCTL_FLV_LINK_UP	1
#define E1000_PCS_LCTL_FSV_10		0
#define E1000_PCS_LCTL_FSV_100		2
#define E1000_PCS_LCTL_FSV_1000		4
#define E1000_PCS_LCTL_FDV_FULL		8
#define E1000_PCS_LCTL_FSD		0x10
#define E1000_PCS_LCTL_FORCE_LINK	0x20
#define E1000_PCS_LCTL_FORCE_FCTRL	0x80
#define E1000_PCS_LCTL_AN_ENABLE	0x10000
#define E1000_PCS_LCTL_AN_RESTART	0x20000
#define E1000_PCS_LCTL_AN_TIMEOUT	0x40000
#define E1000_ENABLE_SERDES_LOOPBACK	0x0410

#define E1000_PCS_LSTS_LINK_OK		1
#define E1000_PCS_LSTS_SPEED_100	2
#define E1000_PCS_LSTS_SPEED_1000	4
#define E1000_PCS_LSTS_DUPLEX_FULL	8
#define E1000_PCS_LSTS_SYNK_OK		0x10
#define E1000_PCS_LSTS_AN_COMPLETE	0x10000

/* Device Status */
#define E1000_STATUS_FD			0x00000001 /* Duplex 0=half 1=full */
#define E1000_STATUS_LU			0x00000002 /* Link up.0=no,1=link */
#define E1000_STATUS_FUNC_MASK		0x0000000C /* PCI Function Mask */
#define E1000_STATUS_FUNC_SHIFT		2
#define E1000_STATUS_FUNC_1		0x00000004 /* Function 1 */
#define E1000_STATUS_TXOFF		0x00000010 /* transmission paused */
#define E1000_STATUS_SPEED_MASK	0x000000C0
#define E1000_STATUS_SPEED_10		0x00000000 /* Speed 10Mb/s */
#define E1000_STATUS_SPEED_100		0x00000040 /* Speed 100Mb/s */
#define E1000_STATUS_SPEED_1000		0x00000080 /* Speed 1000Mb/s */
#define E1000_STATUS_LAN_INIT_DONE	0x00000200 /* Lan Init Compltn by NVM */
#define E1000_STATUS_PHYRA		0x00000400 /* PHY Reset Asserted */
#define E1000_STATUS_GIO_MASTER_ENABLE	0x00080000 /* Master request status */
#define E1000_STATUS_PCI66		0x00000800 /* In 66Mhz slot */
#define E1000_STATUS_BUS64		0x00001000 /* In 64 bit slot */
#define E1000_STATUS_2P5_SKU		0x00001000 /* Val of 2.5GBE SKU strap */
#define E1000_STATUS_2P5_SKU_OVER	0x00002000 /* Val of 2.5GBE SKU Over */
#define E1000_STATUS_PCIX_MODE		0x00002000 /* PCI-X mode */
#define E1000_STATUS_PCIX_SPEED		0x0000C000 /* PCI-X bus speed */

/* Constants used to interpret the masked PCI-X bus speed. */
#define E1000_STATUS_PCIX_SPEED_66	0x00000000 /* PCI-X bus spd 50-66MHz */
#define E1000_STATUS_PCIX_SPEED_100	0x00004000 /* PCI-X bus spd 66-100MHz */
#define E1000_STATUS_PCIX_SPEED_133	0x00008000 /* PCI-X bus spd 100-133MHz*/

#define SPEED_10	10
#define SPEED_100	100
#define SPEED_1000	1000
#define SPEED_2500	2500
#define HALF_DUPLEX	1
#define FULL_DUPLEX	2

#define PHY_FORCE_TIME	20

#define ADVERTISE_10_HALF		0x0001
#define ADVERTISE_10_FULL		0x0002
#define ADVERTISE_100_HALF		0x0004
#define ADVERTISE_100_FULL		0x0008
#define ADVERTISE_1000_HALF		0x0010 /* Not used, just FYI */
#define ADVERTISE_1000_FULL		0x0020

/* 1000/H is not supported, nor spec-compliant. */
#define E1000_ALL_SPEED_DUPLEX	( \
	ADVERTISE_10_HALF | ADVERTISE_10_FULL | ADVERTISE_100_HALF | \
	ADVERTISE_100_FULL | ADVERTISE_1000_FULL)
#define E1000_ALL_NOT_GIG	( \
	ADVERTISE_10_HALF | ADVERTISE_10_FULL | ADVERTISE_100_HALF | \
	ADVERTISE_100_FULL)
#define E1000_ALL_100_SPEED	(ADVERTISE_100_HALF | ADVERTISE_100_FULL)
#define E1000_ALL_10_SPEED	(ADVERTISE_10_HALF | ADVERTISE_10_FULL)
#define E1000_ALL_HALF_DUPLEX	(ADVERTISE_10_HALF | ADVERTISE_100_HALF)

#define AUTONEG_ADVERTISE_SPEED_DEFAULT		E1000_ALL_SPEED_DUPLEX

/* LED Control */
#define E1000_PHY_LED0_MODE_MASK	0x00000007
#define E1000_PHY_LED0_IVRT		0x00000008
#define E1000_PHY_LED0_MASK		0x0000001F

#define E1000_LEDCTL_LED0_MODE_MASK	0x0000000F
#define E1000_LEDCTL_LED0_MODE_SHIFT	0
#define E1000_LEDCTL_LED0_IVRT		0x00000040
#define E1000_LEDCTL_LED0_BLINK		0x00000080

#define E1000_LEDCTL_MODE_LINK_UP	0x2
#define E1000_LEDCTL_MODE_LED_ON	0xE
#define E1000_LEDCTL_MODE_LED_OFF	0xF

/* Transmit Descriptor bit definitions */
#define E1000_TXD_DTYP_D	0x00100000 /* Data Descriptor */
#define E1000_TXD_DTYP_C	0x00000000 /* Context Descriptor */
#define E1000_TXD_POPTS_IXSM	0x01       /* Insert IP checksum */
#define E1000_TXD_POPTS_TXSM	0x02       /* Insert TCP/UDP checksum */
#define E1000_TXD_CMD_EOP	0x01000000 /* End of Packet */
#define E1000_TXD_CMD_IFCS	0x02000000 /* Insert FCS (Ethernet CRC) */
#define E1000_TXD_CMD_IC	0x04000000 /* Insert Checksum */
#define E1000_TXD_CMD_RS	0x08000000 /* Report Status */
#define E1000_TXD_CMD_RPS	0x10000000 /* Report Packet Sent */
#define E1000_TXD_CMD_DEXT	0x20000000 /* Desc extension (0 = legacy) */
#define E1000_TXD_CMD_VLE	0x40000000 /* Add VLAN tag */
#define E1000_TXD_CMD_IDE	0x80000000 /* Enable Tidv register */
#define E1000_TXD_STAT_DD	0x00000001 /* Descriptor Done */
#define E1000_TXD_STAT_EC	0x00000002 /* Excess Collisions */
#define E1000_TXD_STAT_LC	0x00000004 /* Late Collisions */
#define E1000_TXD_STAT_TU	0x00000008 /* Transmit underrun */
#define E1000_TXD_CMD_TCP	0x01000000 /* TCP packet */
#define E1000_TXD_CMD_IP	0x02000000 /* IP packet */
#define E1000_TXD_CMD_TSE	0x04000000 /* TCP Seg enable */
#define E1000_TXD_STAT_TC	0x00000004 /* Tx Underrun */
#define E1000_TXD_EXTCMD_TSTAMP	0x00000010 /* IEEE1588 Timestamp packet */

/* Transmit Control */
#define E1000_TCTL_EN		0x00000002 /* enable Tx */
#define E1000_TCTL_PSP		0x00000008 /* pad short packets */
#define E1000_TCTL_CT		0x00000ff0 /* collision threshold */
#define E1000_TCTL_COLD		0x003ff000 /* collision distance */
#define E1000_TCTL_RTLC		0x01000000 /* Re-transmit on late collision */
#define E1000_TCTL_MULR		0x10000000 /* Multiple request support */

/* Transmit Arbitration Count */
#define E1000_TARC0_ENABLE	0x00000400 /* Enable Tx Queue 0 */

/* SerDes Control */
#define E1000_SCTL_DISABLE_SERDES_LOOPBACK	0x0400
#define E1000_SCTL_ENABLE_SERDES_LOOPBACK	0x0410

/* Receive Checksum Control */
#define E1000_RXCSUM_IPOFL	0x00000100 /* IPv4 checksum offload */
#define E1000_RXCSUM_TUOFL	0x00000200 /* TCP / UDP checksum offload */
#define E1000_RXCSUM_CRCOFL	0x00000800 /* CRC32 offload enable */
#define E1000_RXCSUM_IPPCSE	0x00001000 /* IP payload checksum enable */
#define E1000_RXCSUM_PCSD	0x00002000 /* packet checksum disabled */

/* Header split receive */
#define E1000_RFCTL_NFSW_DIS		0x00000040
#define E1000_RFCTL_NFSR_DIS		0x00000080
#define E1000_RFCTL_ACK_DIS		0x00001000
#define E1000_RFCTL_EXTEN		0x00008000
#define E1000_RFCTL_IPV6_EX_DIS		0x00010000
#define E1000_RFCTL_NEW_IPV6_EXT_DIS	0x00020000
#define E1000_RFCTL_LEF			0x00040000

/* Collision related configuration parameters */
#define E1000_COLLISION_THRESHOLD	15
#define E1000_CT_SHIFT			4
#define E1000_COLLISION_DISTANCE	63
#define E1000_COLD_SHIFT		12

/* Default values for the transmit IPG register */
#define DEFAULT_82542_TIPG_IPGT		10
#define DEFAULT_82543_TIPG_IPGT_FIBER	9
#define DEFAULT_82543_TIPG_IPGT_COPPER	8

#define E1000_TIPG_IPGT_MASK		0x000003FF

#define DEFAULT_82542_TIPG_IPGR1	2
#define DEFAULT_82543_TIPG_IPGR1	8
#define E1000_TIPG_IPGR1_SHIFT		10

#define DEFAULT_82542_TIPG_IPGR2	10
#define DEFAULT_82543_TIPG_IPGR2	6
#define DEFAULT_80003ES2LAN_TIPG_IPGR2	7
#define E1000_TIPG_IPGR2_SHIFT		20

/* Ethertype field values */
#define ETHERNET_IEEE_VLAN_TYPE		0x8100  /* 802.3ac packet */

#define ETHERNET_FCS_SIZE		4
#define MAX_JUMBO_FRAME_SIZE		0x3F00
/* The datasheet maximum supported RX size is 9.5KB (9728 bytes) */
#define MAX_RX_JUMBO_FRAME_SIZE		0x2600
#define E1000_TX_PTR_GAP		0x1F

/* Extended Configuration Control and Size */
#define E1000_EXTCNF_CTRL_MDIO_SW_OWNERSHIP	0x00000020
#define E1000_EXTCNF_CTRL_LCD_WRITE_ENABLE	0x00000001
#define E1000_EXTCNF_CTRL_OEM_WRITE_ENABLE	0x00000008
#define E1000_EXTCNF_CTRL_SWFLAG		0x00000020
#define E1000_EXTCNF_CTRL_GATE_PHY_CFG		0x00000080
#define E1000_EXTCNF_SIZE_EXT_PCIE_LENGTH_MASK	0x00FF0000
#define E1000_EXTCNF_SIZE_EXT_PCIE_LENGTH_SHIFT	16
#define E1000_EXTCNF_CTRL_EXT_CNF_POINTER_MASK	0x0FFF0000
#define E1000_EXTCNF_CTRL_EXT_CNF_POINTER_SHIFT	16

#define E1000_PHY_CTRL_D0A_LPLU			0x00000002
#define E1000_PHY_CTRL_NOND0A_LPLU		0x00000004
#define E1000_PHY_CTRL_NOND0A_GBE_DISABLE	0x00000008
#define E1000_PHY_CTRL_GBE_DISABLE		0x00000040

#define E1000_KABGTXD_BGSQLBIAS			0x00050000

/* Low Power IDLE Control */
#define E1000_LPIC_LPIET_SHIFT		24	/* Low Power Idle Entry Time */

/* PBA constants */
#define E1000_PBA_8K		0x0008    /* 8KB */
#define E1000_PBA_10K		0x000A    /* 10KB */
#define E1000_PBA_12K		0x000C    /* 12KB */
#define E1000_PBA_14K		0x000E    /* 14KB */
#define E1000_PBA_16K		0x0010    /* 16KB */
#define E1000_PBA_18K		0x0012
#define E1000_PBA_20K		0x0014
#define E1000_PBA_22K		0x0016
#define E1000_PBA_24K		0x0018
#define E1000_PBA_26K		0x001A
#define E1000_PBA_30K		0x001E
#define E1000_PBA_32K		0x0020
#define E1000_PBA_34K		0x0022
#define E1000_PBA_35K		0x0023
#define E1000_PBA_38K		0x0026
#define E1000_PBA_40K		0x0028
#define E1000_PBA_48K		0x0030    /* 48KB */
#define E1000_PBA_64K		0x0040    /* 64KB */

#define E1000_PBA_RXA_MASK	0xFFFF

#define E1000_PBS_16K		E1000_PBA_16K

/* Uncorrectable/correctable ECC Error counts and enable bits */
#define E1000_PBECCSTS_CORR_ERR_CNT_MASK	0x000000FF
#define E1000_PBECCSTS_UNCORR_ERR_CNT_MASK	0x0000FF00
#define E1000_PBECCSTS_UNCORR_ERR_CNT_SHIFT	8
#define E1000_PBECCSTS_ECC_ENABLE		0x00010000

#define IFS_MAX			80
#define IFS_MIN			40
#define IFS_RATIO		4
#define IFS_STEP		10
#define MIN_NUM_XMITS		1000

/* SW Semaphore Register */
#define E1000_SWSM_SMBI		0x00000001 /* Driver Semaphore bit */
#define E1000_SWSM_SWESMBI	0x00000002 /* FW Semaphore bit */
#define E1000_SWSM_DRV_LOAD	0x00000008 /* Driver Loaded Bit */

#define E1000_SWSM2_LOCK	0x00000002 /* Secondary driver semaphore bit */

/* Interrupt Cause Read */
#define E1000_ICR_TXDW		0x00000001 /* Transmit desc written back */
#define E1000_ICR_TXQE		0x00000002 /* Transmit Queue empty */
#define E1000_ICR_LSC		0x00000004 /* Link Status Change */
#define E1000_ICR_RXSEQ		0x00000008 /* Rx sequence error */
#define E1000_ICR_RXDMT0	0x00000010 /* Rx desc min. threshold (0) */
#define E1000_ICR_RXO		0x00000040 /* Rx overrun */
#define E1000_ICR_RXT0		0x00000080 /* Rx timer intr (ring 0) */
#define E1000_ICR_VMMB		0x00000100 /* VM MB event */
#define E1000_ICR_RXCFG		0x00000400 /* Rx /c/ ordered set */
#define E1000_ICR_GPI_EN0	0x00000800 /* GP Int 0 */
#define E1000_ICR_GPI_EN1	0x00001000 /* GP Int 1 */
#define E1000_ICR_GPI_EN2	0x00002000 /* GP Int 2 */
#define E1000_ICR_GPI_EN3	0x00004000 /* GP Int 3 */
#define E1000_ICR_TXD_LOW	0x00008000
#define E1000_ICR_MNG		0x00040000 /* Manageability event */
#define E1000_ICR_ECCER		0x00400000 /* Uncorrectable ECC Error */
#define E1000_ICR_TS		0x00080000 /* Time Sync Interrupt */
#define E1000_ICR_DRSTA		0x40000000 /* Device Reset Asserted */
/* If this bit asserted, the driver should claim the interrupt */
#define E1000_ICR_INT_ASSERTED	0x80000000
#define E1000_ICR_DOUTSYNC	0x10000000 /* NIC DMA out of sync */
#define E1000_ICR_RXQ0		0x00100000 /* Rx Queue 0 Interrupt */
#define E1000_ICR_RXQ1		0x00200000 /* Rx Queue 1 Interrupt */
#define E1000_ICR_TXQ0		0x00400000 /* Tx Queue 0 Interrupt */
#define E1000_ICR_TXQ1		0x00800000 /* Tx Queue 1 Interrupt */
#define E1000_ICR_OTHER		0x01000000 /* Other Interrupts */
#define E1000_ICR_FER		0x00400000 /* Fatal Error */

#define E1000_ICR_THS		0x00800000 /* ICR.THS: Thermal Sensor Event*/
#define E1000_ICR_MDDET		0x10000000 /* Malicious Driver Detect */

#define E1000_ITR_MASK		0x000FFFFF /* ITR value bitfield */
#define E1000_ITR_MULT		256 /* ITR mulitplier in nsec */

/* PBA ECC Register */
#define E1000_PBA_ECC_COUNTER_MASK	0xFFF00000 /* ECC counter mask */
#define E1000_PBA_ECC_COUNTER_SHIFT	20 /* ECC counter shift value */
#define E1000_PBA_ECC_CORR_EN	0x00000001 /* Enable ECC error correction */
#define E1000_PBA_ECC_STAT_CLR	0x00000002 /* Clear ECC error counter */
#define E1000_PBA_ECC_INT_EN	0x00000004 /* Enable ICR bit 5 on ECC error */

/* Extended Interrupt Cause Read */
#define E1000_EICR_RX_QUEUE0	0x00000001 /* Rx Queue 0 Interrupt */
#define E1000_EICR_RX_QUEUE1	0x00000002 /* Rx Queue 1 Interrupt */
#define E1000_EICR_RX_QUEUE2	0x00000004 /* Rx Queue 2 Interrupt */
#define E1000_EICR_RX_QUEUE3	0x00000008 /* Rx Queue 3 Interrupt */
#define E1000_EICR_TX_QUEUE0	0x00000100 /* Tx Queue 0 Interrupt */
#define E1000_EICR_TX_QUEUE1	0x00000200 /* Tx Queue 1 Interrupt */
#define E1000_EICR_TX_QUEUE2	0x00000400 /* Tx Queue 2 Interrupt */
#define E1000_EICR_TX_QUEUE3	0x00000800 /* Tx Queue 3 Interrupt */
#define E1000_EICR_TCP_TIMER	0x40000000 /* TCP Timer */
#define E1000_EICR_OTHER	0x80000000 /* Interrupt Cause Active */
/* TCP Timer */
#define E1000_TCPTIMER_KS	0x00000100 /* KickStart */
#define E1000_TCPTIMER_COUNT_ENABLE	0x00000200 /* Count Enable */
#define E1000_TCPTIMER_COUNT_FINISH	0x00000400 /* Count finish */
#define E1000_TCPTIMER_LOOP	0x00000800 /* Loop */

/* This defines the bits that are set in the Interrupt Mask
 * Set/Read Register.  Each bit is documented below:
 *   o RXT0   = Receiver Timer Interrupt (ring 0)
 *   o TXDW   = Transmit Descriptor Written Back
 *   o RXDMT0 = Receive Descriptor Minimum Threshold hit (ring 0)
 *   o RXSEQ  = Receive Sequence Error
 *   o LSC    = Link Status Change
 */
#define IMS_ENABLE_MASK ( \
	E1000_IMS_RXT0   |    \
	E1000_IMS_TXDW   |    \
	E1000_IMS_RXDMT0 |    \
	E1000_IMS_RXSEQ  |    \
	E1000_IMS_LSC)

/* Interrupt Mask Set */
#define E1000_IMS_TXDW		E1000_ICR_TXDW    /* Tx desc written back */
#define E1000_IMS_TXQE		E1000_ICR_TXQE    /* Transmit Queue empty */
#define E1000_IMS_LSC		E1000_ICR_LSC     /* Link Status Change */
#define E1000_IMS_VMMB		E1000_ICR_VMMB    /* Mail box activity */
#define E1000_IMS_RXSEQ		E1000_ICR_RXSEQ   /* Rx sequence error */
#define E1000_IMS_RXDMT0	E1000_ICR_RXDMT0  /* Rx desc min. threshold */
#define E1000_IMS_RXO		E1000_ICR_RXO     /* Rx overrun */
#define E1000_IMS_RXT0		E1000_ICR_RXT0    /* Rx timer intr */
#define E1000_IMS_TXD_LOW	E1000_ICR_TXD_LOW
#define E1000_IMS_ECCER		E1000_ICR_ECCER   /* Uncorrectable ECC Error */
#define E1000_IMS_TS		E1000_ICR_TS      /* Time Sync Interrupt */
#define E1000_IMS_DRSTA		E1000_ICR_DRSTA   /* Device Reset Asserted */
#define E1000_IMS_DOUTSYNC	E1000_ICR_DOUTSYNC /* NIC DMA out of sync */
#define E1000_IMS_RXQ0		E1000_ICR_RXQ0 /* Rx Queue 0 Interrupt */
#define E1000_IMS_RXQ1		E1000_ICR_RXQ1 /* Rx Queue 1 Interrupt */
#define E1000_IMS_TXQ0		E1000_ICR_TXQ0 /* Tx Queue 0 Interrupt */
#define E1000_IMS_TXQ1		E1000_ICR_TXQ1 /* Tx Queue 1 Interrupt */
#define E1000_IMS_OTHER		E1000_ICR_OTHER /* Other Interrupts */
#define E1000_IMS_FER		E1000_ICR_FER /* Fatal Error */

#define E1000_IMS_THS		E1000_ICR_THS /* ICR.TS: Thermal Sensor Event*/
#define E1000_IMS_MDDET		E1000_ICR_MDDET /* Malicious Driver Detect */
/* Extended Interrupt Mask Set */
#define E1000_EIMS_RX_QUEUE0	E1000_EICR_RX_QUEUE0 /* Rx Queue 0 Interrupt */
#define E1000_EIMS_RX_QUEUE1	E1000_EICR_RX_QUEUE1 /* Rx Queue 1 Interrupt */
#define E1000_EIMS_RX_QUEUE2	E1000_EICR_RX_QUEUE2 /* Rx Queue 2 Interrupt */
#define E1000_EIMS_RX_QUEUE3	E1000_EICR_RX_QUEUE3 /* Rx Queue 3 Interrupt */
#define E1000_EIMS_TX_QUEUE0	E1000_EICR_TX_QUEUE0 /* Tx Queue 0 Interrupt */
#define E1000_EIMS_TX_QUEUE1	E1000_EICR_TX_QUEUE1 /* Tx Queue 1 Interrupt */
#define E1000_EIMS_TX_QUEUE2	E1000_EICR_TX_QUEUE2 /* Tx Queue 2 Interrupt */
#define E1000_EIMS_TX_QUEUE3	E1000_EICR_TX_QUEUE3 /* Tx Queue 3 Interrupt */
#define E1000_EIMS_TCP_TIMER	E1000_EICR_TCP_TIMER /* TCP Timer */
#define E1000_EIMS_OTHER	E1000_EICR_OTHER   /* Interrupt Cause Active */

/* Interrupt Cause Set */
#define E1000_ICS_LSC		E1000_ICR_LSC       /* Link Status Change */
#define E1000_ICS_RXSEQ		E1000_ICR_RXSEQ     /* Rx sequence error */
#define E1000_ICS_RXDMT0	E1000_ICR_RXDMT0    /* Rx desc min. threshold */

/* Extended Interrupt Cause Set */
#define E1000_EICS_RX_QUEUE0	E1000_EICR_RX_QUEUE0 /* Rx Queue 0 Interrupt */
#define E1000_EICS_RX_QUEUE1	E1000_EICR_RX_QUEUE1 /* Rx Queue 1 Interrupt */
#define E1000_EICS_RX_QUEUE2	E1000_EICR_RX_QUEUE2 /* Rx Queue 2 Interrupt */
#define E1000_EICS_RX_QUEUE3	E1000_EICR_RX_QUEUE3 /* Rx Queue 3 Interrupt */
#define E1000_EICS_TX_QUEUE0	E1000_EICR_TX_QUEUE0 /* Tx Queue 0 Interrupt */
#define E1000_EICS_TX_QUEUE1	E1000_EICR_TX_QUEUE1 /* Tx Queue 1 Interrupt */
#define E1000_EICS_TX_QUEUE2	E1000_EICR_TX_QUEUE2 /* Tx Queue 2 Interrupt */
#define E1000_EICS_TX_QUEUE3	E1000_EICR_TX_QUEUE3 /* Tx Queue 3 Interrupt */
#define E1000_EICS_TCP_TIMER	E1000_EICR_TCP_TIMER /* TCP Timer */
#define E1000_EICS_OTHER	E1000_EICR_OTHER   /* Interrupt Cause Active */

#define E1000_EITR_ITR_INT_MASK	0x0000FFFF
/* E1000_EITR_CNT_IGNR is only for 82576 and newer */
#define E1000_EITR_CNT_IGNR	0x80000000 /* Don't reset counters on write */
#define E1000_EITR_INTERVAL 0x00007FFC

/* Transmit Descriptor Control */
#define E1000_TXDCTL_PTHRESH	0x0000003F /* TXDCTL Prefetch Threshold */
#define E1000_TXDCTL_HTHRESH	0x00003F00 /* TXDCTL Host Threshold */
#define E1000_TXDCTL_WTHRESH	0x003F0000 /* TXDCTL Writeback Threshold */
#define E1000_TXDCTL_GRAN	0x01000000 /* TXDCTL Granularity */
#define E1000_TXDCTL_FULL_TX_DESC_WB	0x01010000 /* GRAN=1, WTHRESH=1 */
#define E1000_TXDCTL_MAX_TX_DESC_PREFETCH 0x0100001F /* GRAN=1, PTHRESH=31 */
/* Enable the counting of descriptors still to be processed. */
#define E1000_TXDCTL_COUNT_DESC	0x00400000

/* Flow Control Constants */
#define FLOW_CONTROL_ADDRESS_LOW	0x00C28001
#define FLOW_CONTROL_ADDRESS_HIGH	0x00000100
#define FLOW_CONTROL_TYPE		0x8808

/* 802.1q VLAN Packet Size */
#define VLAN_TAG_SIZE			4    /* 802.3ac tag (not DMA'd) */
#define E1000_VLAN_FILTER_TBL_SIZE	128  /* VLAN Filter Table (4096 bits) */

/* Receive Address
 * Number of high/low register pairs in the RAR. The RAR (Receive Address
 * Registers) holds the directed and multicast addresses that we monitor.
 * Technically, we have 16 spots.  However, we reserve one of these spots
 * (RAR[15]) for our directed address used by controllers with
 * manageability enabled, allowing us room for 15 multicast addresses.
 */
#define E1000_RAR_ENTRIES	15
#define E1000_RAH_AV		0x80000000 /* Receive descriptor valid */
#define E1000_RAL_MAC_ADDR_LEN	4
#define E1000_RAH_MAC_ADDR_LEN	2
#define E1000_RAH_QUEUE_MASK_82575	0x000C0000
#define E1000_RAH_POOL_1	0x00040000

/* Error Codes */
#define E1000_SUCCESS			0
#define E1000_ERR_NVM			1
#define E1000_ERR_PHY			2
#define E1000_ERR_CONFIG		3
#define E1000_ERR_PARAM			4
#define E1000_ERR_MAC_INIT		5
#define E1000_ERR_PHY_TYPE		6
#define E1000_ERR_RESET			9
#define E1000_ERR_MASTER_REQUESTS_PENDING	10
#define E1000_ERR_HOST_INTERFACE_COMMAND	11
#define E1000_BLK_PHY_RESET		12
#define E1000_ERR_SWFW_SYNC		13
#define E1000_NOT_IMPLEMENTED		14
#define E1000_ERR_MBX			15
#define E1000_ERR_INVALID_ARGUMENT	16
#define E1000_ERR_NO_SPACE		17
#define E1000_ERR_NVM_PBA_SECTION	18
#define E1000_ERR_I2C			19
#define E1000_ERR_INVM_VALUE_NOT_FOUND	20

/* Loop limit on how long we wait for auto-negotiation to complete */
#define FIBER_LINK_UP_LIMIT		50
#define COPPER_LINK_UP_LIMIT		10
#define PHY_AUTO_NEG_LIMIT		45
#define PHY_FORCE_LIMIT			20
/* Number of 100 microseconds we wait for PCI Express master disable */
#define MASTER_DISABLE_TIMEOUT		800
/* Number of milliseconds we wait for PHY configuration done after MAC reset */
#define PHY_CFG_TIMEOUT			100
/* Number of 2 milliseconds we wait for acquiring MDIO ownership. */
#define MDIO_OWNERSHIP_TIMEOUT		10
/* Number of milliseconds for NVM auto read done after MAC reset. */
#define AUTO_READ_DONE_TIMEOUT		10

/* Flow Control */
#define E1000_FCRTH_RTH		0x0000FFF8 /* Mask Bits[15:3] for RTH */
#define E1000_FCRTL_RTL		0x0000FFF8 /* Mask Bits[15:3] for RTL */
#define E1000_FCRTL_XONE	0x80000000 /* Enable XON frame transmission */

/* Transmit Configuration Word */
#define E1000_TXCW_FD		0x00000020 /* TXCW full duplex */
#define E1000_TXCW_PAUSE	0x00000080 /* TXCW sym pause request */
#define E1000_TXCW_ASM_DIR	0x00000100 /* TXCW astm pause direction */
#define E1000_TXCW_PAUSE_MASK	0x00000180 /* TXCW pause request mask */
#define E1000_TXCW_ANE		0x80000000 /* Auto-neg enable */

/* Receive Configuration Word */
#define E1000_RXCW_CW		0x0000ffff /* RxConfigWord mask */
#define E1000_RXCW_IV		0x08000000 /* Receive config invalid */
#define E1000_RXCW_C		0x20000000 /* Receive config */
#define E1000_RXCW_SYNCH	0x40000000 /* Receive config synch */

#define E1000_TSYNCTXCTL_VALID		0x00000001 /* Tx timestamp valid */
#define E1000_TSYNCTXCTL_ENABLED	0x00000010 /* enable Tx timestamping */

/* HH Time Sync */
#define E1000_TSYNCTXCTL_MAX_ALLOWED_DLY_MASK	0x0000F000 /* max delay */
#define E1000_TSYNCTXCTL_SYNC_COMP_ERR		0x20000000 /* sync err */
#define E1000_TSYNCTXCTL_SYNC_COMP		0x40000000 /* sync complete */
#define E1000_TSYNCTXCTL_START_SYNC		0x80000000 /* initiate sync */

#define E1000_TSYNCRXCTL_VALID		0x00000001 /* Rx timestamp valid */
#define E1000_TSYNCRXCTL_TYPE_MASK	0x0000000E /* Rx type mask */
#define E1000_TSYNCRXCTL_TYPE_L2_V2	0x00
#define E1000_TSYNCRXCTL_TYPE_L4_V1	0x02
#define E1000_TSYNCRXCTL_TYPE_L2_L4_V2	0x04
#define E1000_TSYNCRXCTL_TYPE_ALL	0x08
#define E1000_TSYNCRXCTL_TYPE_EVENT_V2	0x0A
#define E1000_TSYNCRXCTL_ENABLED	0x00000010 /* enable Rx timestamping */
#define E1000_TSYNCRXCTL_SYSCFI		0x00000020 /* Sys clock frequency */

#define E1000_RXMTRL_PTP_V1_SYNC_MESSAGE	0x00000000
#define E1000_RXMTRL_PTP_V1_DELAY_REQ_MESSAGE	0x00010000

#define E1000_RXMTRL_PTP_V2_SYNC_MESSAGE	0x00000000
#define E1000_RXMTRL_PTP_V2_DELAY_REQ_MESSAGE	0x01000000

#define E1000_TSYNCRXCFG_PTP_V1_CTRLT_MASK		0x000000FF
#define E1000_TSYNCRXCFG_PTP_V1_SYNC_MESSAGE		0x00
#define E1000_TSYNCRXCFG_PTP_V1_DELAY_REQ_MESSAGE	0x01
#define E1000_TSYNCRXCFG_PTP_V1_FOLLOWUP_MESSAGE	0x02
#define E1000_TSYNCRXCFG_PTP_V1_DELAY_RESP_MESSAGE	0x03
#define E1000_TSYNCRXCFG_PTP_V1_MANAGEMENT_MESSAGE	0x04

#define E1000_TSYNCRXCFG_PTP_V2_MSGID_MASK		0x00000F00
#define E1000_TSYNCRXCFG_PTP_V2_SYNC_MESSAGE		0x0000
#define E1000_TSYNCRXCFG_PTP_V2_DELAY_REQ_MESSAGE	0x0100
#define E1000_TSYNCRXCFG_PTP_V2_PATH_DELAY_REQ_MESSAGE	0x0200
#define E1000_TSYNCRXCFG_PTP_V2_PATH_DELAY_RESP_MESSAGE	0x0300
#define E1000_TSYNCRXCFG_PTP_V2_FOLLOWUP_MESSAGE	0x0800
#define E1000_TSYNCRXCFG_PTP_V2_DELAY_RESP_MESSAGE	0x0900
#define E1000_TSYNCRXCFG_PTP_V2_PATH_DELAY_FOLLOWUP_MESSAGE 0x0A00
#define E1000_TSYNCRXCFG_PTP_V2_ANNOUNCE_MESSAGE	0x0B00
#define E1000_TSYNCRXCFG_PTP_V2_SIGNALLING_MESSAGE	0x0C00
#define E1000_TSYNCRXCFG_PTP_V2_MANAGEMENT_MESSAGE	0x0D00

#define E1000_TIMINCA_16NS_SHIFT	24
#define E1000_TIMINCA_INCPERIOD_SHIFT	24
#define E1000_TIMINCA_INCVALUE_MASK	0x00FFFFFF

#define E1000_TSICR_TXTS		0x00000002
#define E1000_TSIM_TXTS			0x00000002
/* TUPLE Filtering Configuration */
#define E1000_TTQF_DISABLE_MASK		0xF0008000 /* TTQF Disable Mask */
#define E1000_TTQF_QUEUE_ENABLE		0x100   /* TTQF Queue Enable Bit */
#define E1000_TTQF_PROTOCOL_MASK	0xFF    /* TTQF Protocol Mask */
/* TTQF TCP Bit, shift with E1000_TTQF_PROTOCOL SHIFT */
#define E1000_TTQF_PROTOCOL_TCP		0x0
/* TTQF UDP Bit, shift with E1000_TTQF_PROTOCOL_SHIFT */
#define E1000_TTQF_PROTOCOL_UDP		0x1
/* TTQF SCTP Bit, shift with E1000_TTQF_PROTOCOL_SHIFT */
#define E1000_TTQF_PROTOCOL_SCTP	0x2
#define E1000_TTQF_PROTOCOL_SHIFT	5       /* TTQF Protocol Shift */
#define E1000_TTQF_QUEUE_SHIFT		16      /* TTQF Queue Shfit */
#define E1000_TTQF_RX_QUEUE_MASK	0x70000 /* TTQF Queue Mask */
#define E1000_TTQF_MASK_ENABLE		0x10000000 /* TTQF Mask Enable Bit */
#define E1000_IMIR_CLEAR_MASK		0xF001FFFF /* IMIR Reg Clear Mask */
#define E1000_IMIR_PORT_BYPASS		0x20000 /* IMIR Port Bypass Bit */
#define E1000_IMIR_PRIORITY_SHIFT	29 /* IMIR Priority Shift */
#define E1000_IMIREXT_CLEAR_MASK	0x7FFFF /* IMIREXT Reg Clear Mask */

#define E1000_MDICNFG_EXT_MDIO		0x80000000 /* MDI ext/int destination */
#define E1000_MDICNFG_COM_MDIO		0x40000000 /* MDI shared w/ lan 0 */
#define E1000_MDICNFG_PHY_MASK		0x03E00000
#define E1000_MDICNFG_PHY_SHIFT		21

#define E1000_MEDIA_PORT_COPPER			1
#define E1000_MEDIA_PORT_OTHER			2
#define E1000_M88E1112_AUTO_COPPER_SGMII	0x2
#define E1000_M88E1112_AUTO_COPPER_BASEX	0x3
#define E1000_M88E1112_STATUS_LINK		0x0004 /* Interface Link Bit */
#define E1000_M88E1112_MAC_CTRL_1		0x10
#define E1000_M88E1112_MAC_CTRL_1_MODE_MASK	0x0380 /* Mode Select */
#define E1000_M88E1112_MAC_CTRL_1_MODE_SHIFT	7
#define E1000_M88E1112_PAGE_ADDR		0x16
#define E1000_M88E1112_STATUS			0x01

#define E1000_THSTAT_LOW_EVENT		0x20000000 /* Low thermal threshold */
#define E1000_THSTAT_MID_EVENT		0x00200000 /* Mid thermal threshold */
#define E1000_THSTAT_HIGH_EVENT		0x00002000 /* High thermal threshold */
#define E1000_THSTAT_PWR_DOWN		0x00000001 /* Power Down Event */
#define E1000_THSTAT_LINK_THROTTLE	0x00000002 /* Link Spd Throttle Event */

/* I350 EEE defines */
#define E1000_IPCNFG_EEE_1G_AN		0x00000008 /* IPCNFG EEE Ena 1G AN */
#define E1000_IPCNFG_EEE_100M_AN	0x00000004 /* IPCNFG EEE Ena 100M AN */
#define E1000_EEER_TX_LPI_EN		0x00010000 /* EEER Tx LPI Enable */
#define E1000_EEER_RX_LPI_EN		0x00020000 /* EEER Rx LPI Enable */
#define E1000_EEER_LPI_FC		0x00040000 /* EEER Ena on Flow Cntrl */
/* EEE status */
#define E1000_EEER_EEE_NEG		0x20000000 /* EEE capability nego */
#define E1000_EEER_RX_LPI_STATUS	0x40000000 /* Rx in LPI state */
#define E1000_EEER_TX_LPI_STATUS	0x80000000 /* Tx in LPI state */
#define E1000_EEE_LP_ADV_ADDR_I350	0x040F     /* EEE LP Advertisement */
#define E1000_M88E1543_PAGE_ADDR	0x16       /* Page Offset Register */
#define E1000_M88E1543_EEE_CTRL_1	0x0
#define E1000_M88E1543_EEE_CTRL_1_MS	0x0001     /* EEE Master/Slave */
#define E1000_M88E1543_FIBER_CTRL	0x0        /* Fiber Control Register */
#define E1000_EEE_ADV_DEV_I354		7
#define E1000_EEE_ADV_ADDR_I354		60
#define E1000_EEE_ADV_100_SUPPORTED	(1 << 1)   /* 100BaseTx EEE Supported */
#define E1000_EEE_ADV_1000_SUPPORTED	(1 << 2)   /* 1000BaseT EEE Supported */
#define E1000_PCS_STATUS_DEV_I354	3
#define E1000_PCS_STATUS_ADDR_I354	1
#define E1000_PCS_STATUS_RX_LPI_RCVD	0x0400
#define E1000_PCS_STATUS_TX_LPI_RCVD	0x0800
#define E1000_M88E1512_CFG_REG_1	0x0010
#define E1000_M88E1512_CFG_REG_2	0x0011
#define E1000_M88E1512_CFG_REG_3	0x0007
#define E1000_M88E1512_MODE		0x0014
#define E1000_EEE_SU_LPI_CLK_STP	0x00800000 /* EEE LPI Clock Stop */
#define E1000_EEE_LP_ADV_DEV_I210	7          /* EEE LP Adv Device */
#define E1000_EEE_LP_ADV_ADDR_I210	61         /* EEE LP Adv Register */
/* PCI Express Control */
#define E1000_GCR_RXD_NO_SNOOP		0x00000001
#define E1000_GCR_RXDSCW_NO_SNOOP	0x00000002
#define E1000_GCR_RXDSCR_NO_SNOOP	0x00000004
#define E1000_GCR_TXD_NO_SNOOP		0x00000008
#define E1000_GCR_TXDSCW_NO_SNOOP	0x00000010
#define E1000_GCR_TXDSCR_NO_SNOOP	0x00000020
#define E1000_GCR_CMPL_TMOUT_MASK	0x0000F000
#define E1000_GCR_CMPL_TMOUT_10ms	0x00001000
#define E1000_GCR_CMPL_TMOUT_RESEND	0x00010000
#define E1000_GCR_CAP_VER2		0x00040000

#define PCIE_NO_SNOOP_ALL	(E1000_GCR_RXD_NO_SNOOP | \
				 E1000_GCR_RXDSCW_NO_SNOOP | \
				 E1000_GCR_RXDSCR_NO_SNOOP | \
				 E1000_GCR_TXD_NO_SNOOP    | \
				 E1000_GCR_TXDSCW_NO_SNOOP | \
				 E1000_GCR_TXDSCR_NO_SNOOP)

#define E1000_MMDAC_FUNC_DATA	0x4000 /* Data, no post increment */

/* mPHY address control and data registers */
#define E1000_MPHY_ADDR_CTL		0x0024 /* Address Control Reg */
#define E1000_MPHY_ADDR_CTL_OFFSET_MASK	0xFFFF0000
#define E1000_MPHY_DATA			0x0E10 /* Data Register */

/* AFE CSR Offset for PCS CLK */
#define E1000_MPHY_PCS_CLK_REG_OFFSET	0x0004
/* Override for near end digital loopback. */
#define E1000_MPHY_PCS_CLK_REG_DIGINELBEN	0x10

/* PHY Control Register */
#define MII_CR_SPEED_SELECT_MSB	0x0040  /* bits 6,13: 10=1000, 01=100, 00=10 */
#define MII_CR_COLL_TEST_ENABLE	0x0080  /* Collision test enable */
#define MII_CR_FULL_DUPLEX	0x0100  /* FDX =1, half duplex =0 */
#define MII_CR_RESTART_AUTO_NEG	0x0200  /* Restart auto negotiation */
#define MII_CR_ISOLATE		0x0400  /* Isolate PHY from MII */
#define MII_CR_POWER_DOWN	0x0800  /* Power down */
#define MII_CR_AUTO_NEG_EN	0x1000  /* Auto Neg Enable */
#define MII_CR_SPEED_SELECT_LSB	0x2000  /* bits 6,13: 10=1000, 01=100, 00=10 */
#define MII_CR_LOOPBACK		0x4000  /* 0 = normal, 1 = loopback */
#define MII_CR_RESET		0x8000  /* 0 = normal, 1 = PHY reset */
#define MII_CR_SPEED_1000	0x0040
#define MII_CR_SPEED_100	0x2000
#define MII_CR_SPEED_10		0x0000

/* PHY Status Register */
#define MII_SR_EXTENDED_CAPS	0x0001 /* Extended register capabilities */
#define MII_SR_JABBER_DETECT	0x0002 /* Jabber Detected */
#define MII_SR_LINK_STATUS	0x0004 /* Link Status 1 = link */
#define MII_SR_AUTONEG_CAPS	0x0008 /* Auto Neg Capable */
#define MII_SR_REMOTE_FAULT	0x0010 /* Remote Fault Detect */
#define MII_SR_AUTONEG_COMPLETE	0x0020 /* Auto Neg Complete */
#define MII_SR_PREAMBLE_SUPPRESS 0x0040 /* Preamble may be suppressed */
#define MII_SR_EXTENDED_STATUS	0x0100 /* Ext. status info in Reg 0x0F */
#define MII_SR_100T2_HD_CAPS	0x0200 /* 100T2 Half Duplex Capable */
#define MII_SR_100T2_FD_CAPS	0x0400 /* 100T2 Full Duplex Capable */
#define MII_SR_10T_HD_CAPS	0x0800 /* 10T   Half Duplex Capable */
#define MII_SR_10T_FD_CAPS	0x1000 /* 10T   Full Duplex Capable */
#define MII_SR_100X_HD_CAPS	0x2000 /* 100X  Half Duplex Capable */
#define MII_SR_100X_FD_CAPS	0x4000 /* 100X  Full Duplex Capable */
#define MII_SR_100T4_CAPS	0x8000 /* 100T4 Capable */

/* Autoneg Advertisement Register */
#define NWAY_AR_SELECTOR_FIELD	0x0001   /* indicates IEEE 802.3 CSMA/CD */
#define NWAY_AR_10T_HD_CAPS	0x0020   /* 10T   Half Duplex Capable */
#define NWAY_AR_10T_FD_CAPS	0x0040   /* 10T   Full Duplex Capable */
#define NWAY_AR_100TX_HD_CAPS	0x0080   /* 100TX Half Duplex Capable */
#define NWAY_AR_100TX_FD_CAPS	0x0100   /* 100TX Full Duplex Capable */
#define NWAY_AR_100T4_CAPS	0x0200   /* 100T4 Capable */
#define NWAY_AR_PAUSE		0x0400   /* Pause operation desired */
#define NWAY_AR_ASM_DIR		0x0800   /* Asymmetric Pause Direction bit */
#define NWAY_AR_REMOTE_FAULT	0x2000   /* Remote Fault detected */
#define NWAY_AR_NEXT_PAGE	0x8000   /* Next Page ability supported */

/* Link Partner Ability Register (Base Page) */
#define NWAY_LPAR_SELECTOR_FIELD	0x0000 /* LP protocol selector field */
#define NWAY_LPAR_10T_HD_CAPS		0x0020 /* LP 10T Half Dplx Capable */
#define NWAY_LPAR_10T_FD_CAPS		0x0040 /* LP 10T Full Dplx Capable */
#define NWAY_LPAR_100TX_HD_CAPS		0x0080 /* LP 100TX Half Dplx Capable */
#define NWAY_LPAR_100TX_FD_CAPS		0x0100 /* LP 100TX Full Dplx Capable */
#define NWAY_LPAR_100T4_CAPS		0x0200 /* LP is 100T4 Capable */
#define NWAY_LPAR_PAUSE			0x0400 /* LP Pause operation desired */
#define NWAY_LPAR_ASM_DIR		0x0800 /* LP Asym Pause Direction bit */
#define NWAY_LPAR_REMOTE_FAULT		0x2000 /* LP detected Remote Fault */
#define NWAY_LPAR_ACKNOWLEDGE		0x4000 /* LP rx'd link code word */
#define NWAY_LPAR_NEXT_PAGE		0x8000 /* Next Page ability supported */

/* Autoneg Expansion Register */
#define NWAY_ER_LP_NWAY_CAPS		0x0001 /* LP has Auto Neg Capability */
#define NWAY_ER_PAGE_RXD		0x0002 /* LP 10T Half Dplx Capable */
#define NWAY_ER_NEXT_PAGE_CAPS		0x0004 /* LP 10T Full Dplx Capable */
#define NWAY_ER_LP_NEXT_PAGE_CAPS	0x0008 /* LP 100TX Half Dplx Capable */
#define NWAY_ER_PAR_DETECT_FAULT	0x0010 /* LP 100TX Full Dplx Capable */

/* 1000BASE-T Control Register */
#define CR_1000T_ASYM_PAUSE	0x0080 /* Advertise asymmetric pause bit */
#define CR_1000T_HD_CAPS	0x0100 /* Advertise 1000T HD capability */
#define CR_1000T_FD_CAPS	0x0200 /* Advertise 1000T FD capability  */
/* 1=Repeater/switch device port 0=DTE device */
#define CR_1000T_REPEATER_DTE	0x0400
/* 1=Configure PHY as Master 0=Configure PHY as Slave */
#define CR_1000T_MS_VALUE	0x0800
/* 1=Master/Slave manual config value 0=Automatic Master/Slave config */
#define CR_1000T_MS_ENABLE	0x1000
#define CR_1000T_TEST_MODE_NORMAL 0x0000 /* Normal Operation */
#define CR_1000T_TEST_MODE_1	0x2000 /* Transmit Waveform test */
#define CR_1000T_TEST_MODE_2	0x4000 /* Master Transmit Jitter test */
#define CR_1000T_TEST_MODE_3	0x6000 /* Slave Transmit Jitter test */
#define CR_1000T_TEST_MODE_4	0x8000 /* Transmitter Distortion test */

/* 1000BASE-T Status Register */
#define SR_1000T_IDLE_ERROR_CNT		0x00FF /* Num idle err since last rd */
#define SR_1000T_ASYM_PAUSE_DIR		0x0100 /* LP asym pause direction bit */
#define SR_1000T_LP_HD_CAPS		0x0400 /* LP is 1000T HD capable */
#define SR_1000T_LP_FD_CAPS		0x0800 /* LP is 1000T FD capable */
#define SR_1000T_REMOTE_RX_STATUS	0x1000 /* Remote receiver OK */
#define SR_1000T_LOCAL_RX_STATUS	0x2000 /* Local receiver OK */
#define SR_1000T_MS_CONFIG_RES		0x4000 /* 1=Local Tx Master, 0=Slave */
#define SR_1000T_MS_CONFIG_FAULT	0x8000 /* Master/Slave config fault */

#define SR_1000T_PHY_EXCESSIVE_IDLE_ERR_COUNT	5

/* PHY 1000 MII Register/Bit Definitions */
/* PHY Registers defined by IEEE */
#define PHY_CONTROL		0x00 /* Control Register */
#define PHY_STATUS		0x01 /* Status Register */
#define PHY_ID1			0x02 /* Phy Id Reg (word 1) */
#define PHY_ID2			0x03 /* Phy Id Reg (word 2) */
#define PHY_AUTONEG_ADV		0x04 /* Autoneg Advertisement */
#define PHY_LP_ABILITY		0x05 /* Link Partner Ability (Base Page) */
#define PHY_AUTONEG_EXP		0x06 /* Autoneg Expansion Reg */
#define PHY_NEXT_PAGE_TX	0x07 /* Next Page Tx */
#define PHY_LP_NEXT_PAGE	0x08 /* Link Partner Next Page */
#define PHY_1000T_CTRL		0x09 /* 1000Base-T Control Reg */
#define PHY_1000T_STATUS	0x0A /* 1000Base-T Status Reg */
#define PHY_EXT_STATUS		0x0F /* Extended Status Reg */

#define PHY_CONTROL_LB		0x4000 /* PHY Loopback bit */

/* NVM Control */
#define E1000_EECD_SK		0x00000001 /* NVM Clock */
#define E1000_EECD_CS		0x00000002 /* NVM Chip Select */
#define E1000_EECD_DI		0x00000004 /* NVM Data In */
#define E1000_EECD_DO		0x00000008 /* NVM Data Out */
#define E1000_EECD_REQ		0x00000040 /* NVM Access Request */
#define E1000_EECD_GNT		0x00000080 /* NVM Access Grant */
#define E1000_EECD_PRES		0x00000100 /* NVM Present */
#define E1000_EECD_SIZE		0x00000200 /* NVM Size (0=64 word 1=256 word) */
#define E1000_EECD_BLOCKED	0x00008000 /* Bit banging access blocked flag */
#define E1000_EECD_ABORT	0x00010000 /* NVM operation aborted flag */
#define E1000_EECD_TIMEOUT	0x00020000 /* NVM read operation timeout flag */
#define E1000_EECD_ERROR_CLR	0x00040000 /* NVM error status clear bit */
/* NVM Addressing bits based on type 0=small, 1=large */
#define E1000_EECD_ADDR_BITS	0x00000400
#define E1000_EECD_TYPE		0x00002000 /* NVM Type (1-SPI, 0-Microwire) */
#define E1000_NVM_GRANT_ATTEMPTS	1000 /* NVM # attempts to gain grant */
#define E1000_EECD_AUTO_RD		0x00000200  /* NVM Auto Read done */
#define E1000_EECD_SIZE_EX_MASK		0x00007800  /* NVM Size */
#define E1000_EECD_SIZE_EX_SHIFT	11
#define E1000_EECD_FLUPD		0x00080000 /* Update FLASH */
#define E1000_EECD_AUPDEN		0x00100000 /* Ena Auto FLASH update */
#define E1000_EECD_SEC1VAL		0x00400000 /* Sector One Valid */
#define E1000_EECD_SEC1VAL_VALID_MASK	(E1000_EECD_AUTO_RD | E1000_EECD_PRES)
#define E1000_EECD_FLUPD_I210		0x00800000 /* Update FLASH */
#define E1000_EECD_FLUDONE_I210		0x04000000 /* Update FLASH done */
#define E1000_EECD_FLASH_DETECTED_I210	0x00080000 /* FLASH detected */
#define E1000_EECD_SEC1VAL_I210		0x02000000 /* Sector One Valid */
#define E1000_FLUDONE_ATTEMPTS		20000
#define E1000_EERD_EEWR_MAX_COUNT	512 /* buffered EEPROM words rw */
#define E1000_I210_FIFO_SEL_RX		0x00
#define E1000_I210_FIFO_SEL_TX_QAV(_i)	(0x02 + (_i))
#define E1000_I210_FIFO_SEL_TX_LEGACY	E1000_I210_FIFO_SEL_TX_QAV(0)
#define E1000_I210_FIFO_SEL_BMC2OS_TX	0x06
#define E1000_I210_FIFO_SEL_BMC2OS_RX	0x01

#define E1000_I210_FLASH_SECTOR_SIZE	0x1000 /* 4KB FLASH sector unit size */
/* Secure FLASH mode requires removing MSb */
#define E1000_I210_FW_PTR_MASK		0x7FFF
/* Firmware code revision field word offset*/
#define E1000_I210_FW_VER_OFFSET	328

#define E1000_NVM_RW_REG_DATA	16  /* Offset to data in NVM read/write regs */
#define E1000_NVM_RW_REG_DONE	2   /* Offset to READ/WRITE done bit */
#define E1000_NVM_RW_REG_START	1   /* Start operation */
#define E1000_NVM_RW_ADDR_SHIFT	2   /* Shift to the address bits */
#define E1000_NVM_POLL_WRITE	1   /* Flag for polling for write complete */
#define E1000_NVM_POLL_READ	0   /* Flag for polling for read complete */
#define E1000_FLASH_UPDATES	2000

/* NVM Word Offsets */
#define NVM_COMPAT			0x0003
#define NVM_ID_LED_SETTINGS		0x0004
#define NVM_SERDES_AMPLITUDE		0x0006 /* SERDES output amplitude */
#define NVM_PHY_CLASS_WORD		0x0007
#define E1000_I210_NVM_FW_MODULE_PTR	0x0010
#define E1000_I350_NVM_FW_MODULE_PTR	0x0051
#define NVM_FUTURE_INIT_WORD1		0x0019
#define NVM_MAC_ADDR			0x0000
#define NVM_SUB_DEV_ID			0x000B
#define NVM_SUB_VEN_ID			0x000C
#define NVM_DEV_ID			0x000D
#define NVM_VEN_ID			0x000E
#define NVM_INIT_CTRL_2			0x000F
#define NVM_INIT_CTRL_4			0x0013
#define NVM_LED_1_CFG			0x001C
#define NVM_LED_0_2_CFG			0x001F

#define NVM_COMPAT_VALID_CSUM		0x0001
#define NVM_FUTURE_INIT_WORD1_VALID_CSUM	0x0040

#define NVM_INIT_CONTROL2_REG		0x000F
#define NVM_INIT_CONTROL3_PORT_B	0x0014
#define NVM_INIT_3GIO_3			0x001A
#define NVM_SWDEF_PINS_CTRL_PORT_0	0x0020
#define NVM_INIT_CONTROL3_PORT_A	0x0024
#define NVM_CFG				0x0012
#define NVM_ALT_MAC_ADDR_PTR		0x0037
#define NVM_CHECKSUM_REG		0x003F
#define NVM_COMPATIBILITY_REG_3		0x0003
#define NVM_COMPATIBILITY_BIT_MASK	0x8000

#define E1000_NVM_CFG_DONE_PORT_0	0x040000 /* MNG config cycle done */
#define E1000_NVM_CFG_DONE_PORT_1	0x080000 /* ...for second port */
#define E1000_NVM_CFG_DONE_PORT_2	0x100000 /* ...for third port */
#define E1000_NVM_CFG_DONE_PORT_3	0x200000 /* ...for fourth port */

#define NVM_82580_LAN_FUNC_OFFSET(a)	((a) ? (0x40 + (0x40 * (a))) : 0)

/* Mask bits for fields in Word 0x24 of the NVM */
#define NVM_WORD24_COM_MDIO		0x0008 /* MDIO interface shared */
#define NVM_WORD24_EXT_MDIO		0x0004 /* MDIO accesses routed extrnl */
/* Offset of Link Mode bits for 82575/82576 */
#define NVM_WORD24_LNK_MODE_OFFSET	8
/* Offset of Link Mode bits for 82580 up */
#define NVM_WORD24_82580_LNK_MODE_OFFSET	4


/* Mask bits for fields in Word 0x0f of the NVM */
#define NVM_WORD0F_PAUSE_MASK		0x3000
#define NVM_WORD0F_PAUSE		0x1000
#define NVM_WORD0F_ASM_DIR		0x2000
#define NVM_WORD0F_SWPDIO_EXT_MASK	0x00F0

/* Mask bits for fields in Word 0x1a of the NVM */
#define NVM_WORD1A_ASPM_MASK		0x000C

/* Mask bits for fields in Word 0x03 of the EEPROM */
#define NVM_COMPAT_LOM			0x0800

/* length of string needed to store PBA number */
#define E1000_PBANUM_LENGTH		11

/* For checksumming, the sum of all words in the NVM should equal 0xBABA. */
#define NVM_SUM				0xBABA

/* PBA (printed board assembly) number words */
#define NVM_PBA_OFFSET_0		8
#define NVM_PBA_OFFSET_1		9
#define NVM_PBA_PTR_GUARD		0xFAFA
#define NVM_RESERVED_WORD		0xFFFF
#define NVM_PHY_CLASS_A			0x8000
#define NVM_SERDES_AMPLITUDE_MASK	0x000F
#define NVM_SIZE_MASK			0x1C00
#define NVM_SIZE_SHIFT			10
#define NVM_WORD_SIZE_BASE_SHIFT	6
#define NVM_SWDPIO_EXT_SHIFT		4

/* NVM Commands - Microwire */
#define NVM_READ_OPCODE_MICROWIRE	0x6  /* NVM read opcode */
#define NVM_WRITE_OPCODE_MICROWIRE	0x5  /* NVM write opcode */
#define NVM_ERASE_OPCODE_MICROWIRE	0x7  /* NVM erase opcode */
#define NVM_EWEN_OPCODE_MICROWIRE	0x13 /* NVM erase/write enable */
#define NVM_EWDS_OPCODE_MICROWIRE	0x10 /* NVM erase/write disable */

/* NVM Commands - SPI */
#define NVM_MAX_RETRY_SPI	5000 /* Max wait of 5ms, for RDY signal */
#define NVM_READ_OPCODE_SPI	0x03 /* NVM read opcode */
#define NVM_WRITE_OPCODE_SPI	0x02 /* NVM write opcode */
#define NVM_A8_OPCODE_SPI	0x08 /* opcode bit-3 = address bit-8 */
#define NVM_WREN_OPCODE_SPI	0x06 /* NVM set Write Enable latch */
#define NVM_RDSR_OPCODE_SPI	0x05 /* NVM read Status register */

/* SPI NVM Status Register */
#define NVM_STATUS_RDY_SPI	0x01

/* Word definitions for ID LED Settings */
#define ID_LED_RESERVED_0000	0x0000
#define ID_LED_RESERVED_FFFF	0xFFFF
#define ID_LED_DEFAULT		((ID_LED_OFF1_ON2  << 12) | \
				 (ID_LED_OFF1_OFF2 <<  8) | \
				 (ID_LED_DEF1_DEF2 <<  4) | \
				 (ID_LED_DEF1_DEF2))
#define ID_LED_DEF1_DEF2	0x1
#define ID_LED_DEF1_ON2		0x2
#define ID_LED_DEF1_OFF2	0x3
#define ID_LED_ON1_DEF2		0x4
#define ID_LED_ON1_ON2		0x5
#define ID_LED_ON1_OFF2		0x6
#define ID_LED_OFF1_DEF2	0x7
#define ID_LED_OFF1_ON2		0x8
#define ID_LED_OFF1_OFF2	0x9

#define IGP_ACTIVITY_LED_MASK	0xFFFFF0FF
#define IGP_ACTIVITY_LED_ENABLE	0x0300
#define IGP_LED3_MODE		0x07000000

/* PCI/PCI-X/PCI-EX Config space */
#define PCIX_COMMAND_REGISTER		0xE6
#define PCIX_STATUS_REGISTER_LO		0xE8
#define PCIX_STATUS_REGISTER_HI		0xEA
#define PCI_HEADER_TYPE_REGISTER	0x0E
#define PCIE_LINK_STATUS		0x12
#define PCIE_DEVICE_CONTROL2		0x28

#define PCIX_COMMAND_MMRBC_MASK		0x000C
#define PCIX_COMMAND_MMRBC_SHIFT	0x2
#define PCIX_STATUS_HI_MMRBC_MASK	0x0060
#define PCIX_STATUS_HI_MMRBC_SHIFT	0x5
#define PCIX_STATUS_HI_MMRBC_4K		0x3
#define PCIX_STATUS_HI_MMRBC_2K		0x2
#define PCIX_STATUS_LO_FUNC_MASK	0x7
#define PCI_HEADER_TYPE_MULTIFUNC	0x80
#define PCIE_LINK_WIDTH_MASK		0x3F0
#define PCIE_LINK_WIDTH_SHIFT		4
#define PCIE_LINK_SPEED_MASK		0x0F
#define PCIE_LINK_SPEED_2500		0x01
#define PCIE_LINK_SPEED_5000		0x02
#define PCIE_DEVICE_CONTROL2_16ms	0x0005

#ifndef ETH_ADDR_LEN
#define ETH_ADDR_LEN			6
#endif

#define PHY_REVISION_MASK		0xFFFFFFF0
#define MAX_PHY_REG_ADDRESS		0x1F  /* 5 bit address bus (0-0x1F) */
#define MAX_PHY_MULTI_PAGE_REG		0xF

/* Bit definitions for valid PHY IDs.
 * I = Integrated
 * E = External
 */
#define M88E1000_E_PHY_ID	0x01410C50
#define M88E1000_I_PHY_ID	0x01410C30
#define M88E1011_I_PHY_ID	0x01410C20
#define IGP01E1000_I_PHY_ID	0x02A80380
#define M88E1111_I_PHY_ID	0x01410CC0
#define M88E1543_E_PHY_ID	0x01410EA0
#define M88E1512_E_PHY_ID	0x01410DD0
#define M88E1112_E_PHY_ID	0x01410C90
#define I347AT4_E_PHY_ID	0x01410DC0
#define M88E1340M_E_PHY_ID	0x01410DF0
#define GG82563_E_PHY_ID	0x01410CA0
#define IGP03E1000_E_PHY_ID	0x02A80390
#define IFE_E_PHY_ID		0x02A80330
#define IFE_PLUS_E_PHY_ID	0x02A80320
#define IFE_C_E_PHY_ID		0x02A80310
#define BME1000_E_PHY_ID	0x01410CB0
#define BME1000_E_PHY_ID_R2	0x01410CB1
#define I82577_E_PHY_ID		0x01540050
#define I82578_E_PHY_ID		0x004DD040
#define I82579_E_PHY_ID		0x01540090
#define I217_E_PHY_ID		0x015400A0
#define I82580_I_PHY_ID		0x015403A0
#define I350_I_PHY_ID		0x015403B0
#define I210_I_PHY_ID		0x01410C00
#define IGP04E1000_E_PHY_ID	0x02A80391
#define M88_VENDOR		0x0141

/* M88E1000 Specific Registers */
#define M88E1000_PHY_SPEC_CTRL		0x10  /* PHY Specific Control Reg */
#define M88E1000_PHY_SPEC_STATUS	0x11  /* PHY Specific Status Reg */
#define M88E1000_EXT_PHY_SPEC_CTRL	0x14  /* Extended PHY Specific Cntrl */
#define M88E1000_RX_ERR_CNTR		0x15  /* Receive Error Counter */

#define M88E1000_PHY_EXT_CTRL		0x1A  /* PHY extend control register */
#define M88E1000_PHY_PAGE_SELECT	0x1D  /* Reg 29 for pg number setting */
#define M88E1000_PHY_GEN_CONTROL	0x1E  /* meaning depends on reg 29 */
#define M88E1000_PHY_VCO_REG_BIT8	0x100 /* Bits 8 & 11 are adjusted for */
#define M88E1000_PHY_VCO_REG_BIT11	0x800 /* improved BER performance */

/* M88E1000 PHY Specific Control Register */
#define M88E1000_PSCR_POLARITY_REVERSAL	0x0002 /* 1=Polarity Reverse enabled */
/* MDI Crossover Mode bits 6:5 Manual MDI configuration */
#define M88E1000_PSCR_MDI_MANUAL_MODE	0x0000
#define M88E1000_PSCR_MDIX_MANUAL_MODE	0x0020  /* Manual MDIX configuration */
/* 1000BASE-T: Auto crossover, 100BASE-TX/10BASE-T: MDI Mode */
#define M88E1000_PSCR_AUTO_X_1000T	0x0040
/* Auto crossover enabled all speeds */
#define M88E1000_PSCR_AUTO_X_MODE	0x0060
#define M88E1000_PSCR_ASSERT_CRS_ON_TX	0x0800 /* 1=Assert CRS on Tx */

/* M88E1000 PHY Specific Status Register */
#define M88E1000_PSSR_REV_POLARITY	0x0002 /* 1=Polarity reversed */
#define M88E1000_PSSR_DOWNSHIFT		0x0020 /* 1=Downshifted */
#define M88E1000_PSSR_MDIX		0x0040 /* 1=MDIX; 0=MDI */
/* 0 = <50M
 * 1 = 50-80M
 * 2 = 80-110M
 * 3 = 110-140M
 * 4 = >140M
 */
#define M88E1000_PSSR_CABLE_LENGTH	0x0380
#define M88E1000_PSSR_LINK		0x0400 /* 1=Link up, 0=Link down */
#define M88E1000_PSSR_SPD_DPLX_RESOLVED	0x0800 /* 1=Speed & Duplex resolved */
#define M88E1000_PSSR_DPLX		0x2000 /* 1=Duplex 0=Half Duplex */
#define M88E1000_PSSR_SPEED		0xC000 /* Speed, bits 14:15 */
#define M88E1000_PSSR_100MBS		0x4000 /* 01=100Mbs */
#define M88E1000_PSSR_1000MBS		0x8000 /* 10=1000Mbs */

#define M88E1000_PSSR_CABLE_LENGTH_SHIFT	7

/* Number of times we will attempt to autonegotiate before downshifting if we
 * are the master
 */
#define M88E1000_EPSCR_MASTER_DOWNSHIFT_MASK	0x0C00
#define M88E1000_EPSCR_MASTER_DOWNSHIFT_1X	0x0000
/* Number of times we will attempt to autonegotiate before downshifting if we
 * are the slave
 */
#define M88E1000_EPSCR_SLAVE_DOWNSHIFT_MASK	0x0300
#define M88E1000_EPSCR_SLAVE_DOWNSHIFT_1X	0x0100
#define M88E1000_EPSCR_TX_CLK_25	0x0070 /* 25  MHz TX_CLK */

/* Intel I347AT4 Registers */
#define I347AT4_PCDL		0x10 /* PHY Cable Diagnostics Length */
#define I347AT4_PCDC		0x15 /* PHY Cable Diagnostics Control */
#define I347AT4_PAGE_SELECT	0x16

/* I347AT4 Extended PHY Specific Control Register */

/* Number of times we will attempt to autonegotiate before downshifting if we
 * are the master
 */
#define I347AT4_PSCR_DOWNSHIFT_ENABLE	0x0800
#define I347AT4_PSCR_DOWNSHIFT_MASK	0x7000
#define I347AT4_PSCR_DOWNSHIFT_1X	0x0000
#define I347AT4_PSCR_DOWNSHIFT_2X	0x1000
#define I347AT4_PSCR_DOWNSHIFT_3X	0x2000
#define I347AT4_PSCR_DOWNSHIFT_4X	0x3000
#define I347AT4_PSCR_DOWNSHIFT_5X	0x4000
#define I347AT4_PSCR_DOWNSHIFT_6X	0x5000
#define I347AT4_PSCR_DOWNSHIFT_7X	0x6000
#define I347AT4_PSCR_DOWNSHIFT_8X	0x7000

/* I347AT4 PHY Cable Diagnostics Control */
#define I347AT4_PCDC_CABLE_LENGTH_UNIT	0x0400 /* 0=cm 1=meters */

/* M88E1112 only registers */
#define M88E1112_VCT_DSP_DISTANCE	0x001A

/* M88EC018 Rev 2 specific DownShift settings */
#define M88EC018_EPSCR_DOWNSHIFT_COUNTER_MASK	0x0E00
#define M88EC018_EPSCR_DOWNSHIFT_COUNTER_5X	0x0800

#define I82578_EPSCR_DOWNSHIFT_ENABLE		0x0020
#define I82578_EPSCR_DOWNSHIFT_COUNTER_MASK	0x001C

/* BME1000 PHY Specific Control Register */
#define BME1000_PSCR_ENABLE_DOWNSHIFT	0x0800 /* 1 = enable downshift */

/* Bits...
 * 15-5: page
 * 4-0: register offset
 */
#define GG82563_PAGE_SHIFT	5
#define GG82563_REG(page, reg)	\
	(((page) << GG82563_PAGE_SHIFT) | ((reg) & MAX_PHY_REG_ADDRESS))
#define GG82563_MIN_ALT_REG	30

/* GG82563 Specific Registers */
#define GG82563_PHY_SPEC_CTRL		GG82563_REG(0, 16) /* PHY Spec Cntrl */
#define GG82563_PHY_PAGE_SELECT		GG82563_REG(0, 22) /* Page Select */
#define GG82563_PHY_SPEC_CTRL_2		GG82563_REG(0, 26) /* PHY Spec Cntrl2 */
#define GG82563_PHY_PAGE_SELECT_ALT	GG82563_REG(0, 29) /* Alt Page Select */

/* MAC Specific Control Register */
#define GG82563_PHY_MAC_SPEC_CTRL	GG82563_REG(2, 21)

#define GG82563_PHY_DSP_DISTANCE	GG82563_REG(5, 26) /* DSP Distance */

/* Page 193 - Port Control Registers */
/* Kumeran Mode Control */
#define GG82563_PHY_KMRN_MODE_CTRL	GG82563_REG(193, 16)
#define GG82563_PHY_PWR_MGMT_CTRL	GG82563_REG(193, 20) /* Pwr Mgt Ctrl */

/* Page 194 - KMRN Registers */
#define GG82563_PHY_INBAND_CTRL		GG82563_REG(194, 18) /* Inband Ctrl */

/* MDI Control */
#define E1000_MDIC_REG_MASK	0x001F0000
#define E1000_MDIC_REG_SHIFT	16
#define E1000_MDIC_PHY_MASK	0x03E00000
#define E1000_MDIC_PHY_SHIFT	21
#define E1000_MDIC_OP_WRITE	0x04000000
#define E1000_MDIC_OP_READ	0x08000000
#define E1000_MDIC_READY	0x10000000
#define E1000_MDIC_ERROR	0x40000000
#define E1000_MDIC_DEST		0x80000000

/* SerDes Control */
#define E1000_GEN_CTL_READY		0x80000000
#define E1000_GEN_CTL_ADDRESS_SHIFT	8
#define E1000_GEN_POLL_TIMEOUT		640

/* LinkSec register fields */
#define E1000_LSECTXCAP_SUM_MASK	0x00FF0000
#define E1000_LSECTXCAP_SUM_SHIFT	16
#define E1000_LSECRXCAP_SUM_MASK	0x00FF0000
#define E1000_LSECRXCAP_SUM_SHIFT	16

#define E1000_LSECTXCTRL_EN_MASK	0x00000003
#define E1000_LSECTXCTRL_DISABLE	0x0
#define E1000_LSECTXCTRL_AUTH		0x1
#define E1000_LSECTXCTRL_AUTH_ENCRYPT	0x2
#define E1000_LSECTXCTRL_AISCI		0x00000020
#define E1000_LSECTXCTRL_PNTHRSH_MASK	0xFFFFFF00
#define E1000_LSECTXCTRL_RSV_MASK	0x000000D8

#define E1000_LSECRXCTRL_EN_MASK	0x0000000C
#define E1000_LSECRXCTRL_EN_SHIFT	2
#define E1000_LSECRXCTRL_DISABLE	0x0
#define E1000_LSECRXCTRL_CHECK		0x1
#define E1000_LSECRXCTRL_STRICT		0x2
#define E1000_LSECRXCTRL_DROP		0x3
#define E1000_LSECRXCTRL_PLSH		0x00000040
#define E1000_LSECRXCTRL_RP		0x00000080
#define E1000_LSECRXCTRL_RSV_MASK	0xFFFFFF33

/* Tx Rate-Scheduler Config fields */
#define E1000_RTTBCNRC_RS_ENA		0x80000000
#define E1000_RTTBCNRC_RF_DEC_MASK	0x00003FFF
#define E1000_RTTBCNRC_RF_INT_SHIFT	14
#define E1000_RTTBCNRC_RF_INT_MASK	\
	(E1000_RTTBCNRC_RF_DEC_MASK << E1000_RTTBCNRC_RF_INT_SHIFT)

/* DMA Coalescing register fields */
/* DMA Coalescing Watchdog Timer */
#define E1000_DMACR_DMACWT_MASK		0x00003FFF
/* DMA Coalescing Rx Threshold */
#define E1000_DMACR_DMACTHR_MASK	0x00FF0000
#define E1000_DMACR_DMACTHR_SHIFT	16
/* Lx when no PCIe transactions */
#define E1000_DMACR_DMAC_LX_MASK	0x30000000
#define E1000_DMACR_DMAC_LX_SHIFT	28
#define E1000_DMACR_DMAC_EN		0x80000000 /* Enable DMA Coalescing */
/* DMA Coalescing BMC-to-OS Watchdog Enable */
#define E1000_DMACR_DC_BMC2OSW_EN	0x00008000

/* DMA Coalescing Transmit Threshold */
#define E1000_DMCTXTH_DMCTTHR_MASK	0x00000FFF

#define E1000_DMCTLX_TTLX_MASK		0x00000FFF /* Time to LX request */

/* Rx Traffic Rate Threshold */
#define E1000_DMCRTRH_UTRESH_MASK	0x0007FFFF
/* Rx packet rate in current window */
#define E1000_DMCRTRH_LRPRCW		0x80000000

/* DMA Coal Rx Traffic Current Count */
#define E1000_DMCCNT_CCOUNT_MASK	0x01FFFFFF

/* Flow ctrl Rx Threshold High val */
#define E1000_FCRTC_RTH_COAL_MASK	0x0003FFF0
#define E1000_FCRTC_RTH_COAL_SHIFT	4
/* Lx power decision based on DMA coal */
#define E1000_PCIEMISC_LX_DECISION	0x00000080

#define E1000_RXPBS_CFG_TS_EN		0x80000000 /* Timestamp in Rx buffer */
#define E1000_RXPBS_SIZE_I210_MASK	0x0000003F /* Rx packet buffer size */
#define E1000_TXPB0S_SIZE_I210_MASK	0x0000003F /* Tx packet buffer 0 size */
#define I210_RXPBSIZE_DEFAULT		0x000000A2 /* RXPBSIZE default */
#define I210_TXPBSIZE_DEFAULT		0x04000014 /* TXPBSIZE default */

#define E1000_DOBFFCTL_OBFFTHR_MASK	0x000000FF /* OBFF threshold */
#define E1000_DOBFFCTL_EXIT_ACT_MASK	0x01000000 /* Exit active CB */

/* Proxy Filter Control */
#define E1000_PROXYFC_D0		0x00000001 /* Enable offload in D0 */
#define E1000_PROXYFC_EX		0x00000004 /* Directed exact proxy */
#define E1000_PROXYFC_MC		0x00000008 /* Directed MC Proxy */
#define E1000_PROXYFC_BC		0x00000010 /* Broadcast Proxy Enable */
#define E1000_PROXYFC_ARP_DIRECTED	0x00000020 /* Directed ARP Proxy Ena */
#define E1000_PROXYFC_IPV4		0x00000040 /* Directed IPv4 Enable */
#define E1000_PROXYFC_IPV6		0x00000080 /* Directed IPv6 Enable */
#define E1000_PROXYFC_NS		0x00000200 /* IPv6 Neighbor Solicitation */
#define E1000_PROXYFC_ARP		0x00000800 /* ARP Request Proxy Ena */
/* Proxy Status */
#define E1000_PROXYS_CLEAR		0xFFFFFFFF /* Clear */

/* Firmware Status */
#define E1000_FWSTS_FWRI		0x80000000 /* FW Reset Indication */
/* VF Control */
#define E1000_VTCTRL_RST		0x04000000 /* Reset VF */

#define E1000_STATUS_LAN_ID_MASK	0x00000000C /* Mask for Lan ID field */
/* Lan ID bit field offset in status register */
#define E1000_STATUS_LAN_ID_OFFSET	2
#define E1000_VFTA_ENTRIES		128
#define E1000_UNUSEDARG
#ifndef ERROR_REPORT
#define ERROR_REPORT(fmt)	do { } while (0)
#endif /* ERROR_REPORT */
#endif /* _E1000_DEFINES_H_ */
