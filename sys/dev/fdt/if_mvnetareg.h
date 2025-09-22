/*	$OpenBSD: if_mvnetareg.h,v 1.7 2022/06/03 03:17:36 dlg Exp $	*/
/*	$NetBSD: mvnetareg.h,v 1.8 2013/12/23 02:23:25 kiyohara Exp $	*/
/*
 * Copyright (c) 2007, 2013 KIYOHARA Takashi
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _MVNETAREG_H_
#define _MVNETAREG_H_

#define MVNETA_NWINDOW		6
#define MVNETA_NREMAP		4

#define MVNETA_PHY_TIMEOUT	10000	/* msec */

/*
 * Ethernet Unit Registers
 */

#define MVNETA_PRXC(q)		(0x1400 + ((q) << 2)) /*Port RX queues Config*/
#define MVNETA_PRXSNP(q)	(0x1420 + ((q) << 2)) /* Port RX queues Snoop */
#define MVNETA_PRXF01(q)	(0x1440 + ((q) << 2)) /* Port RX Prefetch 0_1 */
#define MVNETA_PRXF23(q)	(0x1460 + ((q) << 2)) /* Port RX Prefetch 2_3 */
#define MVNETA_PRXDQA(q)	(0x1480 + ((q) << 2)) /*P RXqueues desc Q Addr*/
#define MVNETA_PRXDQS(q)	(0x14a0 + ((q) << 2)) /*P RXqueues desc Q Size*/
#define MVNETA_PRXDQTH(q)	(0x14c0 + ((q) << 2)) /*P RXqueues desc Q Thrs*/
#define MVNETA_PRXS(q)		(0x14e0 + ((q) << 2)) /*Port RX queues Status */
#define MVNETA_PRXSU(q)		(0x1500 + ((q) << 2)) /*P RXqueues Stat Update*/
#define MVNETA_PPLBSZ(q)	(0x1700 + ((q) << 2)) /* P Pool n Buffer Size */
#define MVNETA_PRXFC		0x1710	/* Port RX Flow Control */
#define MVNETA_PRXTXP		0x1714	/* Port RX_TX Pause */
#define MVNETA_PRXFCG		0x1718	/* Port RX Flow Control Generation */
#define MVNETA_PRXINIT		0x1cc0	/* Port RX Initialization */
#define MVNETA_RXCTRL		0x1d00	/* RX Control */
#define MVNETA_RXHWFWD(n)	(0x1d10 + (((n) & ~0x1) << 1))
				/* RX Hardware Forwarding (0_1, 2_3,..., 8_9) */
#define MVNETA_RXHWFWDPTR	0x1d30	/* RX Hardware Forwarding Pointer */
#define MVNETA_RXHWFWDTH	0x1d40	/* RX Hardware Forwarding Threshold */
#define MVNETA_RXHWFWDDQA	0x1d44	/* RX Hw Fwd Descriptors Queue Address*/
#define MVNETA_RXHWFWDQS	0x1d48	/* RX Hw Fwd Descriptors Queue Size */
#define MVNETA_RXHWFWDQENB	0x1d4c	/* RX Hw Fwd Queue Enable */
#define MVNETA_RXHWFWDACPT	0x1d50	/* RX Hw Forwarding Accepted Counter */
#define MVNETA_RXHWFWDYDSCRD	0x1d54	/* RX Hw Fwd Yellow Discarded Counter */
#define MVNETA_RXHWFWDGDSCRD	0x1d58	/* RX Hw Fwd Green Discarded Counter */
#define MVNETA_RXHWFWDTHDSCRD	0x1d5c	/*RX HwFwd Threshold Discarded Counter*/
#define MVNETA_RXHWFWDTXGAP	0x1d6c	/*RX Hardware Forwarding TX Access Gap*/

/* Ethernet Unit Global Registers */
#define MVNETA_PHYADDR		0x2000
#define MVNETA_SMI		0x2004
#define MVNETA_EUDA		0x2008	/* Ethernet Unit Default Address */
#define MVNETA_EUDID		0x200c	/* Ethernet Unit Default ID */
#define MVNETA_ERETRY		0x2010	/* Ethernet Unit MBUS Retry */
#define MVNETA_EU 		0x2014	/* Ethernet Unit Reserved */
#define MVNETA_EUIC 		0x2080	/* Ethernet Unit Interrupt Cause */
#define MVNETA_EUIM 		0x2084	/* Ethernet Unit Interrupt Mask */
#define MVNETA_EUEA 		0x2094	/* Ethernet Unit Error Address */
#define MVNETA_EUIAE 		0x2098	/* Ethernet Unit Internal Addr Error */
#define MVNETA_EUPCR 		0x20a0	/* EthernetUnit Port Pads Calibration */
#define MVNETA_EUC 		0x20b0	/* Ethernet Unit Control */

#define MVNETA_BASEADDR(n)	(0x2200 + ((n) << 3))	/* Base Address */
#define MVNETA_S(n)		(0x2204 + ((n) << 3))	/* Size */
#define MVNETA_HA(n)		(0x2280 + ((n) << 2))	/* High Address Remap */
#define MVNETA_BARE 		0x2290	/* Base Address Enable */
#define MVNETA_EPAP 		0x2294	/* Ethernet Port Access Protect */

/* Ethernet Unit Port Registers */
#define MVNETA_PXC		0x2400	/* Port Configuration */
#define MVNETA_PXCX		0x2404	/* Port Configuration Extend */
#define MVNETA_MIISP		0x2408	/* MII Serial Parameters */
#define MVNETA_GMIISP		0x240c	/* GMII Serial Params */
#define MVNETA_EVLANE		0x2410	/* VLAN EtherType */
#define MVNETA_MACAL		0x2414	/* MAC Address Low */
#define MVNETA_MACAH		0x2418	/* MAC Address High */
#define MVNETA_SDC		0x241c	/* SDMA Configuration */
#define MVNETA_DSCP(n)		(0x2420 + ((n) << 2))
#define MVNETA_PSC		0x243c	/* Port Serial Control0 */
#define MVNETA_VPT2P		0x2440	/* VLAN Priority Tag to Priority */
#define MVNETA_PS		0x2444	/* Ethernet Port Status */
#define MVNETA_TQC		0x2448	/* Transmit Queue Command */
#define MVNETA_PSC1		0x244c	/* Port Serial Control1 */
#define MVNETA_MH		0x2454	/* Marvell Header */
#define MVNETA_MTU		0x2458	/* Max Transmit Unit */
#define MVNETA_IC		0x2460	/* Port Interrupt Cause */
#define MVNETA_ICE		0x2464	/* Port Interrupt Cause Extend */
#define MVNETA_PIM		0x2468	/* Port Interrupt Mask */
#define MVNETA_PEIM		0x246c	/* Port Extend Interrupt Mask */
#define MVNETA_PRFUT		0x2470	/* Port Rx FIFO Urgent Threshold */
#define MVNETA_PTFUT		0x2474	/* Port Tx FIFO Urgent Threshold */
#define MVNETA_PXTFTT		0x2478	/* Port Tx FIFO Threshold */
#define MVNETA_PMFS		0x247c	/* Port Rx Minimal Frame Size */
#define MVNETA_PXDFC		0x2484	/* Port Rx Discard Frame Counter */
#define MVNETA_POFC		0x2488	/* Port Overrun Frame Counter */
#define MVNETA_PIAE		0x2494	/* Port Internal Address Error */
#define MVNETA_AIP0ADR		0x2498	/* Arp IP0 Address */
#define MVNETA_AIP1ADR		0x249c	/* Arp IP1 Address */
#define MVNETA_SERDESCFG	0x24a0	/* Serdes Configuration */
#define MVNETA_SERDESSTS	0x24a4	/* Serdes Status */
#define MVNETA_ETP		0x24bc	/* Ethernet Type Priority */
#define MVNETA_TQFPC		0x24dc	/* Transmit Queue Fixed Priority Cfg */
#define MVNETA_TQC_1		0x24e4
#define MVNETA_OMSCD		0x24f4	/* One mS Clock Divider */
#define MVNETA_PFCCD		0x24f8	/* Periodic Flow Control Clock Divider*/
#define MVNETA_PACC		0x2500	/* Port Acceleration Mode */
#define MVNETA_PBMADDR		0x2504	/* Port BM Address */
#define MVNETA_PV		0x25bc	/* Port Version */
#define MVNETA_CRDP(n)		(0x260c + ((n) << 4))
			/* Ethernet Current Receive Descriptor Pointers */
#define MVNETA_RQC		0x2680	/* Receive Queue Command */
#define MVNETA_TCSDP		0x2684	/* Tx Current Served Desc Pointer */
#define MVNETA_TCQDP		0x26c0	/* Tx Current Queue Desc Pointer */
#define MVNETA_TQTBCOUNT(q)	(0x2700 + ((q) << 4))
				/* Transmit Queue Token-Bucket Counter */
#define MVNETA_TQTBCONFIG(q)	(0x2704 + ((q) << 4))
				/* Transmit Queue Token-Bucket Configuration */
#define MVNETA_TQAC(q)		(0x2708 + ((q) << 4))
				/* Transmit Queue Arbiter Configuration */

#define MVNETA_PCP2Q(cpu)	(0x2540 + ((cpu) << 2))	/* Port CPUn to Queue */
#define MVNETA_PRXITTH(q)	(0x2580 + ((q) << 2)) /* Port RX Intr Threshold*/
#define MVNETA_PRXTXTIC		0x25a0	/*Port RX_TX Threshold Interrupt Cause*/
#define MVNETA_PRXTXTIM		0x25a4	/*Port RX_TX Threshold Interrupt Mask */
#define MVNETA_PRXTXIC		0x25a8	/* Port RX_TX Interrupt Cause */
#define MVNETA_PRXTXIM		0x25ac	/* Port RX_TX Interrupt Mask */
#define MVNETA_PMIC		0x25b0	/* Port Misc Interrupt Cause */
#define MVNETA_PMIM		0x25b4	/* Port Misc Interrupt Mask */
#define MVNETA_PIE		0x25b8	/* Port Interrupt Enable */

#define MVNETA_PMACC0		0x2c00	/* Port MAC Control 0 */
#define MVNETA_PMACC1		0x2c04	/* Port MAC Control 1 */
#define MVNETA_PMACC2		0x2c08	/* Port MAC Control 2 */
#define MVNETA_PANC		0x2c0c	/* Port Auto-Negotiation Configuration*/
#define MVNETA_PS0		0x2c10	/* Port Status 0 */
#define MVNETA_PSPC		0x2c14	/* Port Serial Parameters Config */
#define MVNETA_PIC_2		0x2c20	/* Port Interrupt Cause */
#define MVNETA_PIM_2		0x2c24	/* Port Interrupt Mask */
#define MVNETA_PPRBSS		0x2c38	/* Port PRBS Status */
#define MVNETA_PPRBSEC		0x2c3c	/* Port PRBS Error Counter */
#define MVNETA_PMACC3		0x2c48	/* Port MAC Control 3 */
#define MVNETA_CCFCPST(p)	(0x2c58 + ((p) << 2)) /*CCFC Port Speed Timerp*/
#define MVNETA_PMACC4		0x2c90	/* Port MAC Control 4 */
#define MVNETA_PSP1C		0x2c94	/* Port Serial Parameters 1 Config */
#define MVNETA_LPIC0		0x2cc0	/* LowPowerIdle control 0 */
#define MVNETA_LPIC1		0x2cc4	/* LPI control 1 */
#define MVNETA_LPIC2		0x2cc8	/* LPI control 2 */
#define MVNETA_LPIS		0x2ccc	/* LPI status */
#define MVNETA_LPIC		0x2cd0	/* LPI counter */

#define MVNETA_PPLLC		0x2e04	/* Power and PLL Control */
#define MVNETA_DLE		0x2e8c	/* Digital Loopback Enable */
#define MVNETA_RCS		0x2f18	/* Reference Clock Select */

/* MAC MIB Counters 		0x3000 - 0x307c */

/* Rx DMA Wake on LAN Registers	0x3690 - 0x36b8 */

#define MVNETA_NDFSMT		 0x40
#define MVNETA_DFSMT		0x3400
			/* Destination Address Filter Special Multicast Table */
#define MVNETA_NDFOMT		 0x40
#define MVNETA_DFOMT		0x3500
			/* Destination Address Filter Other Multicast Table */
#define MVNETA_NDFUT		  0x4
#define MVNETA_DFUT		0x3600
			/* Destination Address Filter Unicast Table */

#define MVNETA_PTXDQA(q)	(0x3c00 + ((q) << 2)) /*P TXqueues desc Q Addr*/
#define MVNETA_PTXDQS(q)	(0x3c20 + ((q) << 2)) /*P TXqueues desc Q Size*/
#define MVNETA_PTXS(q)		(0x3c40 + ((q) << 2)) /* Port TX queues Status*/
#define MVNETA_PTXSU(q)		(0x3c60 + ((q) << 2)) /*P TXqueues Stat Update*/
#define MVNETA_PTXDI(q)		(0x3c80 + ((q) << 2)) /* P TXqueues Desc Index*/
#define MVNETA_TXTBC(q)		(0x3ca0 + ((q) << 2)) /* TX Trans-ed Buf Count*/
#define MVNETA_PTXINIT		0x3cf0	/* Port TX Initialization */
#define MVNETA_PTXDOSD		0x3cf4	/* Port TX Disable Outstanding Reads */

#define MVNETA_TXBADFCS		0x3cc0	/*Tx Bad FCS Transmitted Pckts Counter*/
#define MVNETA_TXDROPPED	0x3cc4	/* Tx Dropped Packets Counter */
#define MVNETA_TXNB		0x3cfc	/* Tx Number of New Bytes */
#define MVNETA_TXGB		0x3d00	/* Tx Green Number of Bytes */
#define MVNETA_TXYB		0x3d04	/* Tx Yellow Number of Bytes */

/* Tx DMA Packet Modification Registers	0x3d00 - 0x3dff */

/* Tx DMA Queue Arbiter Registers	0x3e00 - 0x3eff */
#define MVNETA_TXMTU		0x3e0c	/* Tx Max MTU */
#define MVNETA_TXMTU_MAX	0x3ffff
#define MVNETA_TXTKSIZE		0x3e14	/* Tx Token Size */
#define MVNETA_TXQTKSIZE(q)	(0x3e40 + ((q) << 2)) /* Tx Token Size */


/* PHY Address (MVNETA_PHYADDR) */
#define MVNETA_PHYADDR_PHYAD_MASK	0x1f
#define MVNETA_PHYADDR_PHYAD(port, phy)	((phy) << ((port) * 5))

/* SMI register fields (MVNETA_SMI) */
#define MVNETA_SMI_DATA_MASK		0x0000ffff
#define MVNETA_SMI_PHYAD(phy)		(((phy) & 0x1f) << 16)
#define MVNETA_SMI_REGAD(reg)		(((reg) & 0x1f) << 21)
#define MVNETA_SMI_OPCODE_WRITE		(0 << 26)
#define MVNETA_SMI_OPCODE_READ		(1 << 26)
#define MVNETA_SMI_READVALID		(1 << 27)
#define MVNETA_SMI_BUSY			(1 << 28)

/* Ethernet Unit Default ID (MVNETA_EUDID) */
#define MVNETA_EUDID_DIDR_MASK		0x0000000f
#define MVNETA_EUDID_DATTR_MASK		0x00000ff0

/* Ethernet Unit Reserved (MVNETA_EU) */
#define MVNETA_EU_FASTMDC 		(1 << 0)
#define MVNETA_EU_ACCS 			(1 << 1)

/* Ethernet Unit Interrupt Cause (MVNETA_EUIC) */
#define MVNETA_EUIC_ETHERINTSUM 	(1 << 0)
#define MVNETA_EUIC_PARITY 		(1 << 1)
#define MVNETA_EUIC_ADDRVIOL		(1 << 2)
#define MVNETA_EUIC_ADDRVNOMATCH	(1 << 3)
#define MVNETA_EUIC_SMIDONE		(1 << 4)
#define MVNETA_EUIC_COUNTWA		(1 << 5)
#define MVNETA_EUIC_INTADDRERR		(1 << 7)
#define MVNETA_EUIC_PORT0DPERR		(1 << 9)
#define MVNETA_EUIC_TOPDPERR		(1 << 12)

/* Ethernet Unit Internal Addr Error (MVNETA_EUIAE) */
#define MVNETA_EUIAE_INTADDR_MASK 	0x000001ff

/* Ethernet Unit Port Pads Calibration (MVNETA_EUPCR) */
#define MVNETA_EUPCR_DRVN_MASK		0x0000001f
#define MVNETA_EUPCR_TUNEEN		(1 << 16)
#define MVNETA_EUPCR_LOCKN_MASK		0x003e0000
#define MVNETA_EUPCR_OFFSET_MASK	0x1f000000	/* Reserved */
#define MVNETA_EUPCR_WREN		(1U << 31)

/* Ethernet Unit Control (MVNETA_EUC) */
#define MVNETA_EUC_PORT0DPPAR 		(1 << 0)
#define MVNETA_EUC_POLLING	 	(1 << 1)
#define MVNETA_EUC_TOPDPPAR	 	(1 << 3)
#define MVNETA_EUC_PORT0PW 		(1 << 16)
#define MVNETA_EUC_PORTRESET	 	(1 << 24)
#define MVNETA_EUC_RAMSINITIALIZATIONCOMPLETED (1 << 25)

/* Base Address (MVNETA_BASEADDR) */
#define MVNETA_BASEADDR_TARGET(target)	((target) & 0xf)
#define MVNETA_BASEADDR_ATTR(attr)	(((attr) & 0xff) << 8)
#define MVNETA_BASEADDR_BASE(base)	((base) & 0xffff0000)

/* Size (MVNETA_S) */
#define MVNETA_S_SIZE(size)		(((size) - 1) & 0xffff0000)

/* Base Address Enable (MVNETA_BARE) */
#define MVNETA_BARE_EN_MASK		((1 << MVNETA_NWINDOW) - 1)
#define MVNETA_BARE_EN(win)		((1 << (win)) & MVNETA_BARE_EN_MASK)

/* Ethernet Port Access Protect (MVNETA_EPAP) */
#define MVNETA_EPAP_AC_NAC		0x0	/* No access allowed */
#define MVNETA_EPAP_AC_RO		0x1	/* Read Only */
#define MVNETA_EPAP_AC_FA		0x3	/* Full access (r/w) */
#define MVNETA_EPAP_EPAR(win, ac)	((ac) << ((win) * 2))

/* Port Configuration (MVNETA_PXC) */
#define MVNETA_PXC_UPM			(1 << 0) /* Uni Promisc mode */
#define MVNETA_PXC_RXQ(q)		((q) << 1)
#define MVNETA_PXC_RXQ_MASK		MVNETA_PXC_RXQ(7)
#define MVNETA_PXC_RXQARP(q)		((q) << 4)
#define MVNETA_PXC_RXQARP_MASK		MVNETA_PXC_RXQARP(7)
#define MVNETA_PXC_RB			(1 << 7) /* Rej mode of MAC */
#define MVNETA_PXC_RBIP			(1 << 8)
#define MVNETA_PXC_RBARP		(1 << 9)
#define MVNETA_PXC_AMNOTXES		(1 << 12)
#define MVNETA_PXC_RBARPF		(1 << 13)
#define MVNETA_PXC_TCPCAPEN		(1 << 14)
#define MVNETA_PXC_UDPCAPEN		(1 << 15)
#define MVNETA_PXC_TCPQ(q)		((q) << 16)
#define MVNETA_PXC_TCPQ_MASK		MVNETA_PXC_TCPQ(7)
#define MVNETA_PXC_UDPQ(q)		((q) << 19)
#define MVNETA_PXC_UDPQ_MASK		MVNETA_PXC_UDPQ(7)
#define MVNETA_PXC_BPDUQ(q)		((q) << 22)
#define MVNETA_PXC_BPDUQ_MASK		MVNETA_PXC_BPDUQ(7)
#define MVNETA_PXC_RXCS			(1 << 25)

/* Port Configuration Extend (MVNETA_PXCX) */
#define MVNETA_PXCX_SPAN		(1 << 1)
#define MVNETA_PXCX_TXCRCDIS		(1 << 3)

/* MII Serial Parameters (MVNETA_MIISP) */
#define MVNETA_MIISP_JAMLENGTH_12KBIT	0x00000000
#define MVNETA_MIISP_JAMLENGTH_24KBIT	0x00000001
#define MVNETA_MIISP_JAMLENGTH_32KBIT	0x00000002
#define MVNETA_MIISP_JAMLENGTH_48KBIT	0x00000003
#define MVNETA_MIISP_JAMIPG(x)		(((x) & 0x7c) << 0)
#define MVNETA_MIISP_IPGJAMTODATA(x)	(((x) & 0x7c) << 5)
#define MVNETA_MIISP_IPGDATA(x)		(((x) & 0x7c) << 10)
#define MVNETA_MIISP_DATABLIND(x)	(((x) & 0x1f) << 17)

/* GMII Serial Parameters (MVNETA_GMIISP) */
#define MVNETA_GMIISP_IPGDATA(x)	(((x) >> 4) & 0x7)

/* SDMA Configuration (MVNETA_SDC) */
#define MVNETA_SDC_RIFB			(1 << 0)
#define MVNETA_SDC_RXBSZ(x)		((x) << 1)
#define MVNETA_SDC_RXBSZ_MASK		MVNETA_SDC_RXBSZ(7)
#define MVNETA_SDC_RXBSZ_1_64BITWORDS	MVNETA_SDC_RXBSZ(0)
#define MVNETA_SDC_RXBSZ_2_64BITWORDS	MVNETA_SDC_RXBSZ(1)
#define MVNETA_SDC_RXBSZ_4_64BITWORDS	MVNETA_SDC_RXBSZ(2)
#define MVNETA_SDC_RXBSZ_8_64BITWORDS	MVNETA_SDC_RXBSZ(3)
#define MVNETA_SDC_RXBSZ_16_64BITWORDS	MVNETA_SDC_RXBSZ(4)
#define MVNETA_SDC_BLMR			(1 << 4)
#define MVNETA_SDC_BLMT			(1 << 5)
#define MVNETA_SDC_SWAPMODE		(1 << 6)
#define MVNETA_SDC_IPGINTRX_V1_MASK	__BITS(21, 8)
#define MVNETA_SDC_IPGINTRX_V2_MASK	(__BIT(25) | __BITS(21, 7))
#define MVNETA_SDC_IPGINTRX_V1(x)	(((x) << 4)			\
						& MVNETA_SDC_IPGINTRX_V1_MASK)
#define MVNETA_SDC_IPGINTRX_V2(x)	((((x) & 0x8000) << 10) 	\
						| (((x) & 0x7fff) << 7))
#define MVNETA_SDC_IPGINTRX_V1_MAX	0x3fff
#define MVNETA_SDC_IPGINTRX_V2_MAX	0xffff
#define MVNETA_SDC_TXBSZ(x)		((x) << 22)
#define MVNETA_SDC_TXBSZ_MASK		MVNETA_SDC_TXBSZ(7)
#define MVNETA_SDC_TXBSZ_1_64BITWORDS	MVNETA_SDC_TXBSZ(0)
#define MVNETA_SDC_TXBSZ_2_64BITWORDS	MVNETA_SDC_TXBSZ(1)
#define MVNETA_SDC_TXBSZ_4_64BITWORDS	MVNETA_SDC_TXBSZ(2)
#define MVNETA_SDC_TXBSZ_8_64BITWORDS	MVNETA_SDC_TXBSZ(3)
#define MVNETA_SDC_TXBSZ_16_64BITWORDS	MVNETA_SDC_TXBSZ(4)

/* Port Serial Control (MVNETA_PSC) */
#define MVNETA_PSC_PORTEN		(1 << 0)
#define MVNETA_PSC_FLP			(1 << 1) /* Force_Link_Pass */
#define MVNETA_PSC_ANDUPLEX		(1 << 2)	/* auto nego */
#define MVNETA_PSC_ANFC			(1 << 3)
#define MVNETA_PSC_PAUSEADV		(1 << 4)
#define MVNETA_PSC_FFCMODE		(1 << 5)	/* Force FC */
#define MVNETA_PSC_FBPMODE		(1 << 7)	/* Back pressure */
#define MVNETA_PSC_RESERVED		(1 << 9)	/* Must be set to 1 */
#define MVNETA_PSC_FLFAIL		(1 << 10)	/* Force Link Fail */
#define MVNETA_PSC_ANSPEED		(1 << 13)
#define MVNETA_PSC_DTEADVERT		(1 << 14)
#define MVNETA_PSC_MRU(x)		((x) << 17)
#define MVNETA_PSC_MRU_MASK		MVNETA_PSC_MRU(7)
#define MVNETA_PSC_MRU_1518		0
#define MVNETA_PSC_MRU_1522		1
#define MVNETA_PSC_MRU_1552		2
#define MVNETA_PSC_MRU_9022		3
#define MVNETA_PSC_MRU_9192		4
#define MVNETA_PSC_MRU_9700		5
#define MVNETA_PSC_SETFULLDX		(1 << 21)
#define MVNETA_PSC_SETFCEN		(1 << 22)
#define MVNETA_PSC_SETGMIISPEED		(1 << 23)
#define MVNETA_PSC_SETMIISPEED		(1 << 24)

/* Ethernet Port Status (MVNETA_PS) */
#define MVNETA_PS_LINKUP		(1 << 1)
#define MVNETA_PS_FULLDX		(1 << 2)
#define MVNETA_PS_ENFC			(1 << 3)
#define MVNETA_PS_GMIISPEED		(1 << 4)
#define MVNETA_PS_MIISPEED		(1 << 5)
#define MVNETA_PS_TXINPROG		(1 << 7)
#define MVNETA_PS_TXFIFOEMP		(1 << 10)	/* FIFO Empty */
#define MVNETA_PS_RXFIFOEMPTY		(1 << 16)
/* Armada XP */
#define MVNETA_PS_TXINPROG_MASK		(0xff << 0)
#define MVNETA_PS_TXINPROG_(q)		(1 << ((q) + 0))
#define MVNETA_PS_TXFIFOEMP_MASK	(0xff << 8)
#define MVNETA_PS_TXFIFOEMP_(q)		(1 << ((q) + 8))

/* Transmit Queue Command (MVNETA_TQC) */
#define MVNETA_TQC_ENQ(q)		(1 << ((q) + 0))/* Enable Q */
#define MVNETA_TQC_DISQ(q)		(1 << ((q) + 8))/* Disable Q */

/* Port Serial Control 1 (MVNETA_PSC1) */
#define MVNETA_PSC1_PCSLB		(1 << 1)
#define MVNETA_PSC1_RGMIIEN		(1 << 3)	/* RGMII */
#define MVNETA_PSC1_PRST		(1 << 4)	/* Port Reset */

/* Port Interrupt Cause (MVNETA_IC) */
#define MVNETA_IC_RXBUF			(1 << 0)
#define MVNETA_IC_EXTEND		(1 << 1)
#define MVNETA_IC_RXBUFQ_MASK		(0xff << 2)
#define MVNETA_IC_RXBUFQ(q)		(1 << ((q) + 2))
#define MVNETA_IC_RXERROR		(1 << 10)
#define MVNETA_IC_RXERRQ_MASK		(0xff << 11)
#define MVNETA_IC_RXERRQ(q)		(1 << ((q) + 11))
#define MVNETA_IC_TXEND(q)		(1 << ((q) + 19))
#define MVNETA_IC_ETHERINTSUM		(1U << 31)

/* Port Interrupt Cause Extend (MVNETA_ICE) */
#define MVNETA_ICE_TXBUF_MASK		(0xff << + 0)
#define MVNETA_ICE_TXBUF(q)		(1 << ((q) + 0))
#define MVNETA_ICE_TXERR_MASK		(0xff << + 8)
#define MVNETA_ICE_TXERR(q)		(1 << ((q) + 8))
#define MVNETA_ICE_PHYSTC		(1 << 16)
#define MVNETA_ICE_PTP			(1 << 17)
#define MVNETA_ICE_RXOVR		(1 << 18)
#define MVNETA_ICE_TXUDR		(1 << 19)
#define MVNETA_ICE_LINKCHG		(1 << 20)
#define MVNETA_ICE_SERDESREALIGN	(1 << 21)
#define MVNETA_ICE_INTADDRERR		(1 << 23)
#define MVNETA_ICE_SYNCCHANGED		(1 << 24)
#define MVNETA_ICE_PRBSERROR		(1 << 25)
#define MVNETA_ICE_ETHERINTSUM		(1U << 31)

/* Port Tx FIFO Urgent Threshold (MVNETA_PTFUT) */
#define MVNETA_PTFUT_IPGINTTX_V1_MASK	__BITS(17, 4)
#define MVNETA_PTFUT_IPGINTTX_V2_MASK	__BITS(19, 4)
#define MVNETA_PTFUT_IPGINTTX_V1(x)   __SHIFTIN(x, MVNETA_PTFUT_IPGINTTX_V1_MASK)
#define MVNETA_PTFUT_IPGINTTX_V2(x)   __SHIFTIN(x, MVNETA_PTFUT_IPGINTTX_V2_MASK)
#define MVNETA_PTFUT_IPGINTTX_V1_MAX	0x3fff
#define MVNETA_PTFUT_IPGINTTX_V2_MAX	0xffff

/* Port Rx Minimal Frame Size (MVNETA_PMFS) */
#define MVNETA_PMFS_RXMFS(rxmfs)		(((rxmfs) - 40) & 0x7c)
					/* RxMFS = 40,44,48,52,56,60,64 bytes */

/* Transmit Queue Fixed Priority Configuration */
#define MVNETA_TQFPC_EN(q)		(1 << (q))

/* Receive Queue Command (MVNETA_RQC) */
#define MVNETA_RQC_ENQ_MASK		(0xff << 0)	/* Enable Q */
#define MVNETA_RQC_ENQ(n)		(1 << (0 + (n)))
#define MVNETA_RQC_DISQ_MASK		(0xff << 8)	/* Disable Q */
#define MVNETA_RQC_DISQ(n)		(1 << (8 + (n)))
#define MVNETA_RQC_DISQ_DISABLE(q)	((q) << 8)

/* Destination Address Filter Registers (MVNETA_DF{SM,OM,U}T) */
#define MVNETA_DF(n, x)			((x) << (8 * (n)))
#define MVNETA_DF_PASS			(1 << 0)
#define MVNETA_DF_QUEUE(q)		((q) << 1)
#define MVNETA_DF_QUEUE_MASK		((7) << 1)

/* One mS Clock Divider (MVNETA_OMSCD) */
#define MVNETA_OMSCD_1MS_CLOCK_ENABLE	(1U << 31)

/* Port Acceleration Mode (MVNETA_PACC) */
#define MVGVE_PACC_ACCELERATIONMODE_MASK	0x7
#define MVGVE_PACC_ACCELERATIONMODE_BM		0x0	/* Basic Mode */
#define MVGVE_PACC_ACCELERATIONMODE_EDM		0x1	/* Enhanced Desc Mode */
#define MVGVE_PACC_ACCELERATIONMODE_EDMBM	0x2	/*   with BM */
#define MVGVE_PACC_ACCELERATIONMODE_EDMPNC	0x3	/*   with PnC */
#define MVGVE_PACC_ACCELERATIONMODE_EDMBPMNC	0x4	/*   with BM & PnC */

/* Port BM Address (MVNETA_PBMADDR) */
#define MVNETA_PBMADDR_BMADDRESS_MASK	0xfffff800

/* Port Serdes Config (MVNETA_SERDESCFG) */
#define MVNETA_SERDESCFG_SGMII_PROTO	0x0cc7
#define MVNETA_SERDESCFG_QSGMII_PROTO	0x0667
#define MVNETA_SERDESCFG_HSGMII_PROTO	0x1107

/* Ether Type Priority (MVNETA_ETP) */
#define MVNETA_ETP_ETHERTYPEPRIEN	(1 << 0)	/* EtherType Prio Ena */
#define MVNETA_ETP_ETHERTYPEPRIFRSTEN	(1 << 1)
#define MVNETA_ETP_ETHERTYPEPRIQ		(0x7 << 2)	/*EtherType Prio Queue*/
#define MVNETA_ETP_ETHERTYPEPRIVAL	(0xffff << 5)	/*EtherType Prio Value*/
#define MVNETA_ETP_FORCEUNICSTHIT	(1 << 21)	/* Force Unicast hit */

/* RX Hardware Forwarding (0_1, 2_3,..., 8_9) (MVNETA_RXHWFWD) */
#define MVNETA_RXHWFWD_PORT_BASEADDRESS(p, x)	xxxxx

/* RX Hardware Forwarding Pointer (MVNETA_RXHWFWDPTR) */
#define MVNETA_RXHWFWDPTR_QUEUENO(q)	((q) << 8)	/* Queue Number */
#define MVNETA_RXHWFWDPTR_PORTNO(p)	((p) << 11)	/* Port Number */

/* RX Hardware Forwarding Threshold (MVNETA_RXHWFWDTH) */
#define MVNETA_RXHWFWDTH_DROPRNDGENBITS(n)	(((n) & 0x3ff) << 0)
#define MVNETA_RXHWFWDTH_DROPTHRESHOLD(n)	(((n) & 0xf) << 16)

/* RX Control (MVNETA_RXCTRL) */
#define MVNETA_RXCTRL_PACKETCOLORSRCSELECT(x)	(1 << 0)
#define MVNETA_RXCTRL_GEMPORTIDSRCSEL(x)	((x) << 4)
#define MVNETA_RXCTRL_TXHWFRWMQSRC(x)		(1 << 8)
#define MVNETA_RXCTRL_RX_MH_SELECT(x)		((x) << 12)
#define MVNETA_RXCTRL_RX_TX_SRC_SELECT		(1 << 16)
#define MVNETA_RXCTRL_HWFRWDENB			(1 << 17)
#define MVNETA_RXCTRL_HWFRWDSHORTPOOLID(id)	(((id) & 0x3) << 20)
#define MVNETA_RXCTRL_HWFRWDLONGPOOLID(id)	(((id) & 0x3) << 22)

/* Port RX queues Configuration (MVNETA_PRXC) */
#define MVNETA_PRXC_POOLIDSHORT(i)	(((i) & 0x3) << 4)
#define MVNETA_PRXC_POOLIDLONG(i)	(((i) & 0x3) << 6)
#define MVNETA_PRXC_PACKETOFFSET(o)	(((o) & 0xf) << 8)
#define MVNETA_PRXC_USERPREFETCHCMND0	(1 << 16)

/* Port RX queues Snoop (MVNETA_PRXSNP) */
#define MVNETA_PRXSNP_SNOOPNOOFBYTES(b)	(((b) & 0x3fff) << 0)
#define MVNETA_PRXSNP_L2DEPOSITNOOFBYTES(b) (((b) & 0x3fff) << 16)

/* Port RX queues Snoop (MVNETA_PRXSNP) */
#define MVNETA_PRXF01_PREFETCHCOMMAND0(c) (((c) & 0xffff) << 0) xxxx
#define MVNETA_PRXF01_PREFETCHCOMMAND1(c) (((c) & 0xffff) << 16) xxxx

/* Port RX queues Descriptors Queue Size (MVNETA_PRXDQS) */
#define MVNETA_PRXDQS_DESCRIPTORSQUEUESIZE(s) (((s) & 0x0003fff) << 0)
#define MVNETA_PRXDQS_BUFFERSIZE(s)	(((s) & 0xfff80000) << 19)

/* Port RX queues Descriptors Queue Threshold (MVNETA_PRXDQTH) */
					/* Occupied Descriptors Threshold */
#define MVNETA_PRXDQTH_ODT(x)		(((x) & 0x3fff) << 0)
					/* Non Occupied Descriptors Threshold */
#define MVNETA_PRXDQTH_NODT(x)		(((x) & 0x3fff) << 16)

/* Port RX queues Status (MVNETA_PRXS) */
					/* Occupied Descriptors Counter */
#define MVNETA_PRXS_ODC(x)		(((x) >> 0) & 0x3fff)
					/* Non Occupied Descriptors Counter */
#define MVNETA_PRXS_NODC(x)		(((x) >> 16) & 0x3fff)

/* Port RX queues Status Update (MVNETA_PRXSU) */
#define MVNETA_PRXSU_MAX		0xff /* works as a mask too */
					/* Number Of Processed Descriptors */
#define MVNETA_PRXSU_NOPD(x)		(((x) & MVNETA_PRXSU_MAX) << 0)
					/* Number Of New Descriptors */
#define MVNETA_PRXSU_NOND(x)		(((x) & MVNETA_PRXSU_MAX) << 16)

/* Port RX Flow Control (MVNETA_PRXFC) */
#define MVNETA_PRXFC_PERPRIOFCGENCONTROL	(1 << 0)
#define MVNETA_PRXFC_TXPAUSECONTROL		(1 << 1)

/* Port RX_TX Pause (MVNETA_PRXTXP) */
#define MVNETA_PRXTXP_TXPAUSE(x)		((x) & 0xff)

/* Port RX Flow Control Generation (MVNETA_PRXFCG) */
#define MVNETA_PRXFCG_PERPRIOFCGENDATA		(1 << 0)
#define MVNETA_PRXFCG_PERPRIOFCGENQNO(x)	(((x) & 0x7) << 4)

/* Port RX Initialization (MVNETA_PRXINIT) */
#define MVNETA_PRXINIT_RXDMAINIT		(1 << 0)

/* TX Number of New Bytes (MVNETA_TXNB) */
#define MVNETA_TXNB_NOOFNEWBYTES(b)	(((b) & 0xffff) << 0)
#define MVNETA_TXNB_PKTQNO(q)		(((q) & 0x7) << 28)
#define MVNETA_TXNB_PKTCOLOR		(1U << 31)

/* Port TX queues Descriptors Queue Size (MVNETA_PTXDQS) */
					/* Descriptors Queue Size */
#define MVNETA_PTXDQS_DQS(x)		(((x) & 0x3fff) << 0)
					/* Transmitted Buffer Threshold */
#define MVNETA_PTXDQS_TBT(x)		(((x) & 0x3fff) << 16)

/* Port TX queues Status (MVNETA_PTXS) */
					/* Pending Descriptors Counter */
#define MVNETA_PTXDQS_PDC(x)		(((x) >> 0) & 0x3fff)
					/* Transmitted Buffer Counter */
#define MVNETA_PTXS_TBC(x)		(((x) >> 16) & 0x3fff)

/* Port TX queues Status Update (MVNETA_PTXSU) */
#define MVNETA_PTXSU_MAX		0xff /* works as a mask too */
					/* Number Of Written Descriptors */
#define MVNETA_PTXSU_NOWD(x)		(((x) & MVNETA_PTXSU_MAX) << 0)
					/* Number Of Released Buffers */
#define MVNETA_PTXSU_NORB(x)		(((x) & MVNETA_PTXSU_MAX) << 16)

/* TX Transmitted Buffers Counter (MVNETA_TXTBC) */
					/* Transmitted Buffers Counter */
#define MVNETA_TXTBC_TBC(x)		(((x) & 0x3fff) << 16)

/* Port TX Initialization (MVNETA_PTXINIT) */
#define MVNETA_PTXINIT_TXDMAINIT	(1 << 0)

/* Marvell Header (MVNETA_MH) */
#define MVNETA_MH_MHEN			(1 << 0)
#define MVNETA_MH_DAPREFIX		(0x3 << 1)
#define MVNETA_MH_SPID			(0xf << 4)
#define MVNETA_MH_MHMASK		(0x3 << 8)
#define MVNETA_MH_MHMASK_8QUEUES	(0x0 << 8)
#define MVNETA_MH_MHMASK_4QUEUES	(0x1 << 8)
#define MVNETA_MH_MHMASK_2QUEUES	(0x3 << 8)
#define MVNETA_MH_DSAEN_MASK		(0x3 << 10)
#define MVNETA_MH_DSAEN_DISABLE		(0x0 << 10)
#define MVNETA_MH_DSAEN_NONEXTENDED	(0x1 << 10)
#define MVNETA_MH_DSAEN_EXTENDED		(0x2 << 10)

/* Port Auto-Negotiation Configuration (MVNETA_PANC) */
#define MVNETA_PANC_FORCELINKFAIL	(1 << 0)
#define MVNETA_PANC_FORCELINKPASS	(1 << 1)
#define MVNETA_PANC_INBANDANEN		(1 << 2)
#define MVNETA_PANC_INBANDANBYPASSEN	(1 << 3)
#define MVNETA_PANC_INBANDRESTARTAN	(1 << 4)
#define MVNETA_PANC_SETMIISPEED		(1 << 5)
#define MVNETA_PANC_SETGMIISPEED	(1 << 6)
#define MVNETA_PANC_ANSPEEDEN		(1 << 7)
#define MVNETA_PANC_SETFCEN		(1 << 8)
#define MVNETA_PANC_PAUSEADV		(1 << 9)
#define MVNETA_PANC_ANFCEN		(1 << 11)
#define MVNETA_PANC_SETFULLDX		(1 << 12)
#define MVNETA_PANC_ANDUPLEXEN		(1 << 13)
#define MVNETA_PANC_RESERVED		(1 << 15)

/* Port MAC Control 0 (MVNETA_PMACC0) */
#define MVNETA_PMACC0_PORTEN		(1 << 0)
#define MVNETA_PMACC0_PORTTYPE		(1 << 1)
#define MVNETA_PMACC0_FRAMESIZELIMIT(x)	((((x) >> 1) & 0x7ffc) << 2)
#define MVNETA_PMACC0_RESERVED		(1 << 15)

/* Port MAC Control 1 (MVNETA_PMACC1) */
#define MVNETA_PMACC1_PCSLB		(1 << 6)

/* Port MAC Control 2 (MVNETA_PMACC2) */
#define MVNETA_PMACC2_INBANDAN		(1 << 0)
#define MVNETA_PMACC2_PCSEN		(1 << 3)
#define MVNETA_PMACC2_RGMIIEN		(1 << 4)
#define MVNETA_PMACC2_PADDINGDIS	(1 << 5)
#define MVNETA_PMACC2_PORTMACRESET	(1 << 6)
#define MVNETA_PMACC2_PRBSCHECKEN	(1 << 10)
#define MVNETA_PMACC2_PRBSGENEN		(1 << 11)
#define MVNETA_PMACC2_SDTT_MASK		(3 << 12)  /* Select Data To Transmit */
#define MVNETA_PMACC2_SDTT_RM		(0 << 12)	/* Regular Mode */
#define MVNETA_PMACC2_SDTT_PRBS		(1 << 12)	/* PRBS Mode */
#define MVNETA_PMACC2_SDTT_ZC		(2 << 12)	/* Zero Constant */
#define MVNETA_PMACC2_SDTT_OC		(3 << 12)	/* One Constant */
#define MVNETA_PMACC2_RESERVED		(3 << 14)

/* Port MAC Control 3 (MVNETA_PMACC3) */
#define MVNETA_PMACC3_IPG_MASK		0x7f80

/* Port MAC Control 4 (MVNETA_PMACC4) */
#define MVNETA_PMACC4_SHORT_PREAMBLE	(1 << 1)

/* Port Interrupt Cause/Mask (MVNETA_PIC_2/MVNETA_PIM_2) */
#define MVNETA_PI_2_INTSUM		(1 << 0)
#define MVNETA_PI_2_LSC			(1 << 1)   /* LinkStatus Change */
#define MVNETA_PI_2_ACOP		(1 << 2)   /* AnCompleted OnPort */
#define MVNETA_PI_2_AOOR		(1 << 5)   /* AddressOut Of Range */
#define MVNETA_PI_2_SSC			(1 << 6)   /* SyncStatus Change */
#define MVNETA_PI_2_PRBSEOP		(1 << 7)   /* QSGMII PRBS error */
#define MVNETA_PI_2_MIBCWA		(1 << 15)  /* MIB counter wrap around */
#define MVNETA_PI_2_QSGMIIPRBSE		(1 << 10)  /* QSGMII PRBS error */
#define MVNETA_PI_2_PCSRXPRLPI		(1 << 11)  /* PCS Rx path received LPI*/
#define MVNETA_PI_2_PCSTXPRLPI		(1 << 12)  /* PCS Tx path received LPI*/
#define MVNETA_PI_2_MACRXPRLPI		(1 << 13)  /* MAC Rx path received LPI*/
#define MVNETA_PI_2_MIBCCD		(1 << 14)  /* MIB counters copy done */

/* LPI Control 0 (MVNETA_LPIC0) */
#define MVNETA_LPIC0_LILIMIT(x)		(((x) & 0xff) << 0)
#define MVNETA_LPIC0_TSLIMIT(x)		(((x) & 0xff) << 8)

/* LPI Control 1 (MVNETA_LPIC1) */
#define MVNETA_LPIC1_LPIRE		(1 << 0)	/* LPI request enable */
#define MVNETA_LPIC1_LPIRF		(1 << 1)	/* LPI request force */
#define MVNETA_LPIC1_LPIMM		(1 << 2)	/* LPI manual mode */
#define MVNETA_LPIC1_TWLIMIT		(((x) & 0xfff) << 4)

/* LPI Status (MVNETA_LPIS) */
#define MVNETA_LPIS_PCSRXPLPIS		(1 << 0) /* PCS Rx path LPI status */
#define MVNETA_LPIS_PCSTXPLPIS		(1 << 1) /* PCS Tx path LPI status */
#define MVNETA_LPIS_MACRXPLPIS		(1 << 2)/* MAC Rx path LP idle status */
#define MVNETA_LPIS_MACTXPLPWS		(1 << 3)/* MAC Tx path LP wait status */
#define MVNETA_LPIS_MACTXPLPIS		(1 << 4)/* MAC Tx path LP idle status */

/* Port PRBS Status (MVNETA_PPRBSS) */
#define MVNETA_PPRBSS_PRBSCHECKLOCKED	(1 << 0)
#define MVNETA_PPRBSS_PRBSCHECKRDY	(1 << 1)

/* Port Status 0 (MVNETA_PS0) */
#define MVNETA_PS0_LINKUP		(1 << 0)
#define MVNETA_PS0_GMIISPEED		(1 << 1)
#define MVNETA_PS0_MIISPEED		(1 << 2)
#define MVNETA_PS0_FULLDX		(1 << 3)
#define MVNETA_PS0_RXFCEN		(1 << 4)
#define MVNETA_PS0_TXFCEN		(1 << 5)
#define MVNETA_PS0_PRP			(1 << 6) /* Port Rx Pause */
#define MVNETA_PS0_PTP			(1 << 7) /* Port Tx Pause */
#define MVNETA_PS0_PDP			(1 << 8) /*Port is Doing Back-Pressure*/
#define MVNETA_PS0_SYNCFAIL10MS		(1 << 10)
#define MVNETA_PS0_ANDONE		(1 << 11)
#define MVNETA_PS0_IBANBA		(1 << 12) /* InBand AutoNeg BypassAct */
#define MVNETA_PS0_SYNCOK		(1 << 14)

/* Port CPUn to Queue (MVNETA_PCP2Q) */
#define MVNETA_PCP2Q_RXQAE_ALL		(0xff << 0)/*QueueAccessEnable*/
#define MVNETA_PCP2Q_TXQAE_ALL		(0xff << 8)/*QueueAccessEnable*/

/* Port RX_TX Threshold Interrupt Cause/Mask (MVNETA_PRXTXTIC/MVNETA_PRXTXTIM) */
#define MVNETA_PRXTXTI_TBTCQ(q)		(1 << ((q) + 0))
#define MVNETA_PRXTXTI_RBICTAPQ(q)	(1 << ((q) + 8))
#define MVNETA_PRXTXTI_RDTAQ(q)		(1 << ((q) + 16))
#define MVNETA_PRXTXTI_PRXTXICSUMMARY	(1 << 29)
#define MVNETA_PRXTXTI_PTXERRORSUMMARY	(1 << 30)
#define MVNETA_PRXTXTI_PMISCICSUMMARY	(1U << 31)

/* Port RX_TX Interrupt Cause/Mask (MVNETA_PRXTXIC/MVNETA_PRXTXIM) */
#define MVNETA_PRXTXI_TBRQ(q)		(1 << ((q) + 0))
#define MVNETA_PRXTXI_RPQ(q)		(1 << ((q) + 8))
#define MVNETA_PRXTXI_RREQ(q)		(1 << ((q) + 16))
#define MVNETA_PRXTXI_PRXTXTHICSUMMARY	(1 << 29)
#define MVNETA_PRXTXI_PTXERRORSUMMARY	(1 << 30)
#define MVNETA_PRXTXI_PMISCICSUMMARY	(1U << 31)

/* Port Misc Interrupt Cause/Mask (MVNETA_PMIC/MVNETA_PMIM) */
#define MVNETA_PMI_PHYSTATUSCHNG	(1 << 0)
#define MVNETA_PMI_LINKCHANGE		(1 << 1)
#define MVNETA_PMI_PTP			(1 << 4)
#define MVNETA_PMI_PME			(1 << 6) /* Packet Modification Error */
#define MVNETA_PMI_IAE			(1 << 7) /* Internal Address Error */
#define MVNETA_PMI_RXOVERRUN		(1 << 8)
#define MVNETA_PMI_RXCRCERROR		(1 << 9)
#define MVNETA_PMI_RXLARGEPACKET		(1 << 10)
#define MVNETA_PMI_TXUNDRN		(1 << 11)
#define MVNETA_PMI_PRBSERROR		(1 << 12)
#define MVNETA_PMI_PSCSYNCCHNG		(1 << 13)
#define MVNETA_PMI_SRSE			(1 << 14) /* SerdesRealignSyncError */
#define MVNETA_PMI_RNBTP(q)		(1 << ((q) + 16)) /* RxNoBuffersToPool*/
#define MVNETA_PMI_TREQ(q)		(1 << ((q) + 24)) /* TxResourceErrorQ */

/* Port Interrupt Enable (MVNETA_PIE) */
#define MVNETA_PIE_RXPKTINTRPTENB_ALL	(0xff << 24)
#define MVNETA_PIE_TXPKTINTRPTENB_ALL	(0xff << 8)

/* Power and PLL Control (MVNETA_PPLLC) */
#define MVNETA_PPLLC_REF_FREF_SEL_MASK	(0xf << 0)
#define MVNETA_PPLLC_PHY_MODE_MASK	(7 << 5)
#define MVNETA_PPLLC_PHY_MODE_SATA	(0 << 5)
#define MVNETA_PPLLC_PHY_MODE_SAS	(1 << 5)
#define MVNETA_PPLLC_PLL_LOCK		(1 << 8)
#define MVNETA_PPLLC_PU_DFE		(1 << 10)
#define MVNETA_PPLLC_PU_TX_INTP		(1 << 11)
#define MVNETA_PPLLC_PU_TX		(1 << 12)
#define MVNETA_PPLLC_PU_RX		(1 << 13)
#define MVNETA_PPLLC_PU_PLL		(1 << 14)

/* Digital Loopback Enable (MVNETA_DLE) */
#define MVNETA_DLE_LOCAL_SEL_BITS_MASK	(3 << 10)
#define MVNETA_DLE_LOCAL_SEL_BITS_10BITS	(0 << 10)
#define MVNETA_DLE_LOCAL_SEL_BITS_20BITS	(1 << 10)
#define MVNETA_DLE_LOCAL_SEL_BITS_40BITS	(2 << 10)
#define MVNETA_DLE_LOCAL_RXPHER_TO_TX_EN	(1 << 12)
#define MVNETA_DLE_LOCAL_ANA_TX2RX_LPBK_EN (1 << 13)
#define MVNETA_DLE_LOCAL_DIG_TX2RX_LPBK_EN (1 << 14)
#define MVNETA_DLE_LOCAL_DIG_RX2TX_LPBK_EN (1 << 15)

/* Reference Clock Select (MVNETA_RCS) */
#define MVNETA_RCS_REFCLK_SEL		(1 << 10)


/*
 * Set the chip's packet size limit to 9022.
 * (ETHER_MAX_LEN_JUMBO + ETHER_VLAN_ENCAP_LEN)
 */
#define MVNETA_MRU		9022

#define MVNETA_RXBUF_ALIGN	32	/* Cache line size */
#define MVNETA_RXBUF_MASK	(MVNETA_RXBUF_ALIGN - 1)
#define MVNETA_HWHEADER_SIZE	2


/*
 * DMA descriptors
 *    Despite the documentation saying these descriptors only need to be
 *    aligned to 16-byte boundaries, 32-byte alignment seems to be required
 *    by the hardware.  We'll just pad them out to that to make it easier.
 */
struct mvneta_tx_desc {
#if BYTE_ORDER == BIG_ENDIAN
	uint16_t bytecnt;		/* Descriptor buffer byte count */
	uint16_t l4ichk;		/* CPU provided TCP Checksum */
	uint32_t cmdsts;		/* Descriptor command status */
	uint32_t nextdescptr;		/* Next descriptor pointer */
	uint32_t bufptr;		/* Descriptor buffer pointer */
#else	/* LITTLE_ENDIAN */
	uint32_t cmdsts;		/* Descriptor command status */
	uint16_t l4ichk;		/* CPU provided TCP Checksum */
	uint16_t bytecnt;		/* Descriptor buffer byte count */
	uint32_t bufptr;		/* Descriptor buffer pointer */
	uint32_t nextdescptr;		/* Next descriptor pointer */
#endif
	uint32_t _padding[4];
} __packed __aligned(32);

struct mvneta_rx_desc {
#if BYTE_ORDER == BIG_ENDIAN
	uint16_t bytecnt;		/* Descriptor buffer byte count */
	uint16_t bufsize;		/* Buffer size */
	uint32_t cmdsts;		/* Descriptor command status */
	uint32_t nextdescptr;		/* Next descriptor pointer */
	uint32_t bufptr;		/* Descriptor buffer pointer */
#else	/* LITTLE_ENDIAN */
	uint32_t cmdsts;		/* Descriptor command status */
	uint16_t bufsize;		/* Buffer size */
	uint16_t bytecnt;		/* Descriptor buffer byte count */
	uint32_t bufptr;		/* Descriptor buffer pointer */
	uint32_t nextdescptr;		/* Next descriptor pointer */
#endif
	uint32_t _padding[4];
} __packed __aligned(32);

#define MVNETA_ERROR_SUMMARY		(1 << 0)
#define MVNETA_BUFFER_OWNED_MASK		(1U << 31)
#define MVNETA_BUFFER_OWNED_BY_HOST	(0U << 31)
#define MVNETA_BUFFER_OWNED_BY_DMA	(1U << 31)

#define MVNETA_TX_ERROR_CODE_MASK	(3 << 1)
#define MVNETA_TX_LATE_COLLISION_ERROR	(0 << 1)
#define MVNETA_TX_UNDERRUN_ERROR	(1 << 1)
#define MVNETA_TX_EXCESSIVE_COLLISION_ERRO (2 << 1)
#define MVNETA_TX_LLC_SNAP_FORMAT	(1 << 9)
#define MVNETA_TX_IP_NO_FRAG		(1 << 10)
#define MVNETA_TX_IP_HEADER_LEN(len)	((len) << 11)
#define MVNETA_TX_VLAN_TAGGED_FRAME	(1 << 15)
#define MVNETA_TX_L4_TYPE_TCP		(0 << 16)
#define MVNETA_TX_L4_TYPE_UDP		(1 << 16)
#define MVNETA_TX_GENERATE_L4_CHKSUM	(1 << 17)
#define MVNETA_TX_GENERATE_IP_CHKSUM	(1 << 18)
#define MVNETA_TX_ZERO_PADDING		(1 << 19)
#define MVNETA_TX_LAST_DESC		(1 << 20)
#define MVNETA_TX_FIRST_DESC		(1 << 21)
#define MVNETA_TX_GENERATE_CRC		(1 << 22)
#define MVNETA_TX_ENABLE_INTERRUPT	(1 << 23)
#define MVNETA_TX_L4_CSUM_FULL		(1 << 30)
#define MVNETA_TX_L4_CSUM_NOT		(1U << 31)

#define MVNETA_RX_ERROR_CODE_MASK	(3 << 1)
#define MVNETA_RX_CRC_ERROR		(0 << 1)
#define MVNETA_RX_OVERRUN_ERROR		(1 << 1)
#define MVNETA_RX_MAX_FRAME_LEN_ERROR	(2 << 1)
#define MVNETA_RX_RESOURCE_ERROR	(3 << 1)
#define MVNETA_RX_L4_CHECKSUM_MASK	(0xffff << 3)
#define MVNETA_RX_VLAN_TAGGED_FRAME	(1 << 19)
#define MVNETA_RX_BPDU_FRAME		(1 << 20)
#define MVNETA_RX_L4_TYPE_MASK		(3 << 21)
#define MVNETA_RX_L4_TYPE_TCP		(0 << 21)
#define MVNETA_RX_L4_TYPE_UDP		(1 << 21)
#define MVNETA_RX_L4_TYPE_OTHER		(2 << 21)
#define MVNETA_RX_NOT_LLC_SNAP_FORMAT	(1 << 23)
#define MVNETA_RX_IP_FRAME_TYPE		(1 << 24)
#define MVNETA_RX_IP_HEADER_OK		(1 << 25)
#define MVNETA_RX_LAST_DESC		(1 << 26)
#define MVNETA_RX_FIRST_DESC		(1 << 27)
#define MVNETA_RX_UNKNOWN_DA		(1 << 28)
#define MVNETA_RX_ENABLE_INTERRUPT	(1 << 29)
#define MVNETA_RX_L4_CHECKSUM_OK	(1 << 30)

#define MVNETA_RX_IP_FRAGMENT		(1 << 2)

#endif	/* _MVGEREG_H_ */
