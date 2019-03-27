/*
 * Copyright (c) 2017 Stormshield.
 * Copyright (c) 2017 Semihalf.
 * Copyright (c) 2015 Internet Initiative Japan Inc.
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
 *
 * $FreeBSD$
 *
 */

#ifndef _IF_MVNETAREG_H_
#define	_IF_MVNETAREG_H_

#if BYTE_ORDER == BIG_ENDIAN
#error "BIG ENDIAN not supported"
#endif

#define	MVNETA_SIZE		0x4000

#define	MVNETA_NWINDOW		6
#define	MVNETA_NREMAP		4

#define	MVNETA_MAX_QUEUE_SIZE	8
#define	MVNETA_RX_QNUM_MAX	1
/* XXX: Currently multi-queue can be used on the Tx side only */
#ifdef MVNETA_MULTIQUEUE
#define	MVNETA_TX_QNUM_MAX	2
#else
#define	MVNETA_TX_QNUM_MAX	1
#endif

#if MVNETA_TX_QNUM_MAX & (MVNETA_TX_QNUM_MAX - 1) != 0
#error "MVNETA_TX_QNUM_MAX Should be a power of 2"
#endif
#if MVNETA_RX_QNUM_MAX & (MVNETA_RX_QNUM_MAX - 1) != 0
#error "MVNETA_RX_QNUM_MAX Should be a power of 2"
#endif

#define	MVNETA_QUEUE(n)		(1 << (n))
#define	MVNETA_QUEUE_ALL	0xff
#define	MVNETA_TX_QUEUE_ALL	((1<<MVNETA_TX_QNUM_MAX)-1)
#define	MVNETA_RX_QUEUE_ALL	((1<<MVNETA_RX_QNUM_MAX)-1)

/*
 * Ethernet Unit Registers
 *  GbE0 BASE 0x00007.0000 SIZE 0x4000
 *  GbE1 BASE 0x00007.4000 SIZE 0x4000
 *
 * TBD: reasonable bus space submapping....
 */
/* Address Decoder Registers */
#define	MVNETA_BASEADDR(n)	(0x2200 + ((n) << 3))	/* Base Address */
#define	MVNETA_S(n)		(0x2204 + ((n) << 3))	/* Size */
#define	MVNETA_HA(n)		(0x2280 + ((n) << 2))	/* High Address Remap */
#define	MVNETA_BARE		0x2290	/* Base Address Enable */
#define	MVNETA_EPAP		0x2294	/* Ethernet Port Access Protect */

/* Global Miscellaneous Registers */
#define	MVNETA_PHYADDR		0x2000
#define	MVNETA_SMI		0x2004
#define	MVNETA_EUDA		0x2008	/* Ethernet Unit Default Address */
#define	MVNETA_EUDID		0x200c	/* Ethernet Unit Default ID */
#define	MVNETA_MBUS_CONF	0x2010	/* MBUS configuration */
#define	MVNETA_MBUS_RETRY_EN	0x20	/* MBUS transactions retry enable */
#define	MVNETA_EUIC		0x2080	/* Ethernet Unit Interrupt Cause */
#define	MVNETA_EUIM		0x2084	/* Ethernet Unit Interrupt Mask */
#define	MVNETA_EUEA		0x2094	/* Ethernet Unit Error Address */
#define	MVNETA_EUIAE		0x2098	/* Ethernet Unit Internal Addr Error */
#define	MVNETA_EUC		0x20b0	/* Ethernet Unit Control */

/* Miscellaneous Registers */
#define	MVNETA_SDC		0x241c	/* SDMA Configuration */

/* Networking Controller Miscellaneous Registers */
#define	MVNETA_PACC		0x2500	/* Port Acceleration Mode */
#define	MVNETA_PV		0x25bc	/* Port Version */

/* Rx DMA Hardware Parser Registers */
#define	MVNETA_EVLANE		0x2410	/* VLAN EtherType */
#define	MVNETA_MACAL		0x2414	/* MAC Address Low */
#define	MVNETA_MACAH		0x2418	/* MAC Address High */
#define	MVNETA_NDSCP		7
#define	MVNETA_DSCP(n)		(0x2420 + ((n) << 2))
#define	MVNETA_VPT2P		0x2440	/* VLAN Priority Tag to Priority */
#define	MVNETA_ETP		0x24bc	/* Ethernet Type Priority */
#define	MVNETA_NDFSMT		64
#define	MVNETA_DFSMT(n)		(0x3400 + ((n) << 2))
			/* Destination Address Filter Special Multicast Table */
#define	MVNETA_NDFOMT		64
#define	MVNETA_DFOMT(n)		(0x3500 + ((n) << 2))
			/* Destination Address Filter Other Multicast Table */
#define	MVNETA_NDFUT		4
#define	MVNETA_DFUT(n)		(0x3600 + ((n) << 2))
			/* Destination Address Filter Unicast Table */

/* Rx DMA Miscellaneous Registers */
#define	MVNETA_PMFS		0x247c	/* Port Rx Minimal Frame Size */
#define	MVNETA_PDFC		0x2484	/* Port Rx Discard Frame Counter */
#define	MVNETA_POFC		0x2488	/* Port Overrun Frame Counter */
#define	MVNETA_RQC		0x2680	/* Receive Queue Command */

/* Rx DMA Networking Controller Miscellaneous Registers */
#define	MVNETA_PRXC(q)		(0x1400 + ((q) << 2)) /*Port RX queues Config*/
#define	MVNETA_PRXSNP(q)	(0x1420 + ((q) << 2)) /* Port RX queues Snoop */
#define	MVNETA_PRXDQA(q)	(0x1480 + ((q) << 2)) /*P RXqueues desc Q Addr*/
#define	MVNETA_PRXDQS(q)	(0x14a0 + ((q) << 2)) /*P RXqueues desc Q Size*/
#define	MVNETA_PRXDQTH(q)	(0x14c0 + ((q) << 2)) /*P RXqueues desc Q Thrs*/
#define	MVNETA_PRXS(q)		(0x14e0 + ((q) << 2)) /*Port RX queues Status */
#define	MVNETA_PRXSU(q)		(0x1500 + ((q) << 2)) /*P RXqueues Stat Update*/
#define	MVNETA_PRXDI(q)		(0x1520 + ((q) << 2)) /*P RXqueues Stat Update*/
#define	MVNETA_PRXINIT		0x1cc0	/* Port RX Initialization */

/* Rx DMA Wake on LAN Registers	0x3690 - 0x36b8 */

/* Tx DMA Miscellaneous Registers */
#define	MVNETA_TQC		0x2448	/* Transmit Queue Command */
#define	MVNETA_TQC_1		0x24e4
#define	MVNETA_PXTFTT		0x2478	/* Port Tx FIFO Threshold */
#define	MVNETA_TXBADFCS		0x3cc0	/*Tx Bad FCS Transmitted Pckts Counter*/
#define	MVNETA_TXDROPPED		0x3cc4	/* Tx Dropped Packets Counter */

/* Tx DMA Networking Controller Miscellaneous Registers */
#define	MVNETA_PTXDQA(q)	(0x3c00 + ((q) << 2)) /*P TXqueues desc Q Addr*/
#define	MVNETA_PTXDQS(q)	(0x3c20 + ((q) << 2)) /*P TXqueues desc Q Size*/
#define	MVNETA_PTXS(q)		(0x3c40 + ((q) << 2)) /* Port TX queues Status*/
#define	MVNETA_PTXSU(q)		(0x3c60 + ((q) << 2)) /*P TXqueues Stat Update*/
#define	MVNETA_PTXDI(q)		(0x3c80 + ((q) << 2)) /* P TXqueues Desc Index*/
#define	MVNETA_TXTBC(q)		(0x3ca0 + ((q) << 2)) /* TX Trans-ed Buf Count*/
#define	MVNETA_PTXINIT		0x3cf0	/* Port TX Initialization */

/* Tx DMA Packet Modification Registers */
#define	MVNETA_NMH		15
#define	MVNETA_TXMH(n)		(0x3d44 + ((n) << 2))
#define	MVNETA_TXMTU		0x3d88

/* Tx DMA Queue Arbiter Registers (Version 1) */
#define	MVNETA_TQFPC_V1		0x24dc	/* Transmit Queue Fixed Priority Cfg */
#define	MVNETA_TQTBC_V1		0x24e0	/* Transmit Queue Token-Bucket Cfg */
#define	MVNETA_MTU_V1		0x24e8	/* MTU */
#define	MVNETA_PMTBS_V1		0x24ec	/* Port Max Token-Bucket Size */
#define	MVNETA_TQTBCOUNT_V1(q)	(0x2700 + ((q) << 4))
				/* Transmit Queue Token-Bucket Counter */
#define	MVNETA_TQTBCONFIG_V1(q)	(0x2704 + ((q) << 4))
				/* Transmit Queue Token-Bucket Configuration */
#define	MVNETA_PTTBC_V1		0x2740	/* Port Transmit Backet Counter */

/* Tx DMA Queue Arbiter Registers (Version 3) */
#define	MVNETA_TQC1_V3		0x3e00	/* Transmit Queue Command1 */
#define	MVNETA_TQFPC_V3		0x3e04	/* Transmit Queue Fixed Priority Cfg */
#define	MVNETA_BRC_V3		0x3e08	/* Basic Refill No of Clocks */
#define	MVNETA_MTU_V3		0x3e0c	/* MTU */
#define	MVNETA_PREFILL_V3	0x3e10	/* Port Backet Refill */
#define	MVNETA_PMTBS_V3		0x3e14	/* Port Max Token-Bucket Size */
#define	MVNETA_QREFILL_V3(q)	(0x3e20 + ((q) << 2))
				/* Transmit Queue Refill */
#define	MVNETA_QMTBS_V3(q)	(0x3e40 + ((q) << 2))
				/* Transmit Queue Max Token-Bucket Size */
#define	MVNETA_QTTBC_V3(q)	(0x3e60 + ((q) << 2))
				/* Transmit Queue Token-Bucket Counter */
#define	MVNETA_TQAC_V3(q)	(0x3e80 + ((q) << 2))
				/* Transmit Queue Arbiter Cfg */
#define	MVNETA_TQIPG_V3(q)	(0x3ea0 + ((q) << 2))
				/* Transmit Queue IPG(valid q=2..3) */
#define	MVNETA_HITKNINLOPKT_V3	0x3eb0	/* High Token in Low Packet */
#define	MVNETA_HITKNINASYNCPKT_V3	0x3eb4	/* High Token in Async Packet */
#define	MVNETA_LOTKNINASYNCPKT_V3	0x3eb8	/* Low Token in Async Packet */
#define	MVNETA_TS_V3		0x3ebc	/* Token Speed */

/* RX_TX DMA Registers */
#define	MVNETA_PXC		0x2400	/* Port Configuration */
#define	MVNETA_PXCX		0x2404	/* Port Configuration Extend */
#define	MVNETA_MH		0x2454	/* Marvell Header */

/* Serial(SMI/MII) Registers */
#define	MVNETA_PSC0		0x243c	/* Port Serial Control0 */
#define	MVNETA_PS0		0x2444	/* Ethernet Port Status */
#define	MVNETA_PSERDESCFG	0x24a0	/* Serdes Configuration */
#define	MVNETA_PSERDESSTS	0x24a4	/* Serdes Status */
#define	MVNETA_PSOMSCD		0x24f4	/* One mS Clock Divider */
#define	MVNETA_PSPFCCD		0x24f8	/* Periodic Flow Control Clock Divider*/

/* Gigabit Ethernet MAC Serial Parameters Configuration Registers */
#define	MVNETA_PSPC		0x2c14	/* Port Serial Parameters Config */
#define	MVNETA_PSP1C		0x2c94	/* Port Serial Parameters 1 Config */

/* Gigabit Ethernet Auto-Negotiation Configuration Registers */
#define	MVNETA_PANC		0x2c0c	/* Port Auto-Negotiation Configuration*/

/* Gigabit Ethernet MAC Control Registers */
#define	MVNETA_PMACC0		0x2c00	/* Port MAC Control 0 */
#define	MVNETA_PMACC1		0x2c04	/* Port MAC Control 1 */
#define	MVNETA_PMACC2		0x2c08	/* Port MAC Control 2 */
#define	MVNETA_PMACC3		0x2c48	/* Port MAC Control 3 */
#define	MVNETA_CCFCPST(p)	(0x2c58 + ((p) << 2)) /*CCFC Port Speed Timerp*/
#define	MVNETA_PMACC4		0x2c90	/* Port MAC Control 4 */

/* Gigabit Ethernet MAC Interrupt Registers */
#define	MVNETA_PIC		0x2c20
#define	MVNETA_PIM		0x2c24

/* Gigabit Ethernet Low Power Idle  Registers */
#define	MVNETA_LPIC0		0x2cc0	/* LowPowerIdle control 0 */
#define	MVNETA_LPIC1		0x2cc4	/* LPI control 1 */
#define	MVNETA_LPIC2		0x2cc8	/* LPI control 2 */
#define	MVNETA_LPIS		0x2ccc	/* LPI status */
#define	MVNETA_LPIC		0x2cd0	/* LPI counter */

/* Gigabit Ethernet MAC PRBS Check Status Registers */
#define	MVNETA_PPRBSS		0x2c38	/* Port PRBS Status */
#define	MVNETA_PPRBSEC		0x2c3c	/* Port PRBS Error Counter */

/* Gigabit Ethernet MAC Status Registers */
#define	MVNETA_PSR		0x2c10	/* Port Status Register0 */

/* Networking Controller Interrupt Registers */
#define	MVNETA_PCP2Q(cpu)	(0x2540 + ((cpu) << 2))
#define	MVNETA_PRXITTH(q)	(0x2580 + ((q) << 2))
				/* Port Rx Interrupt Threshold */
#define	MVNETA_PRXTXTIC		0x25a0	/*Port RX_TX Threshold Interrupt Cause*/
#define	MVNETA_PRXTXTIM		0x25a4	/*Port RX_TX Threshold Interrupt Mask */
#define	MVNETA_PRXTXIC		0x25a8	/* Port RX_TX Interrupt Cause */
#define	MVNETA_PRXTXIM		0x25ac	/* Port RX_TX Interrupt Mask */
#define	MVNETA_PMIC		0x25b0	/* Port Misc Interrupt Cause */
#define	MVNETA_PMIM		0x25b4	/* Port Misc Interrupt Mask */
#define	MVNETA_PIE		0x25b8	/* Port Interrupt Enable */
#define	MVNETA_PSNPCFG		0x25e4	/* Port Snoop Config */
#define	MVNETA_PSNPCFG_DESCSNP_MASK	(0x3 << 4)
#define	MVNETA_PSNPCFG_BUFSNP_MASK	(0x3 << 8)

/* Miscellaneous Interrupt Registers */
#define	MVNETA_PEUIAE		0x2494	/* Port Internal Address Error */

/* SGMII PHY Registers */
#define	MVNETA_PPLLC		0x2e04	/* Power and PLL Control */
#define	MVNETA_TESTC0		0x2e54	/* PHY Test Control 0 */
#define	MVNETA_TESTPRBSEC0	0x2e7c	/* PHY Test PRBS Error Counter 0 */
#define	MVNETA_TESTPRBSEC1	0x2e80	/* PHY Test PRBS Error Counter 1 */
#define	MVNETA_TESTOOB0		0x2e84	/* PHY Test OOB 0 */
#define	MVNETA_DLE		0x2e8c	/* Digital Loopback Enable */
#define	MVNETA_RCS		0x2f18	/* Reference Clock Select */
#define	MVNETA_COMPHYC		0x2f18	/* COMPHY Control */

/*
 * Ethernet MAC MIB Registers
 *  GbE0 BASE 0x00007.3000
 *  GbE1 BASE 0x00007.7000
 */
/* MAC MIB Counters			0x3000 - 0x307c */
#define	MVNETA_PORTMIB_BASE		0x3000
#define	MVNETA_PORTMIB_SIZE		0x0080
#define	MVNETA_PORTMIB_NOCOUNTER	30

/* Rx */
#define	MVNETA_MIB_RX_GOOD_OCT		0x00 /* 64bit */
#define	MVNETA_MIB_RX_BAD_OCT		0x08
#define	MVNETA_MIB_RX_GOOD_FRAME	0x10
#define	MVNETA_MIB_RX_BAD_FRAME		0x14
#define	MVNETA_MIB_RX_BCAST_FRAME	0x18
#define	MVNETA_MIB_RX_MCAST_FRAME	0x1c
#define	MVNETA_MIB_RX_FRAME64_OCT	0x20
#define	MVNETA_MIB_RX_FRAME127_OCT	0x24
#define	MVNETA_MIB_RX_FRAME255_OCT	0x28
#define	MVNETA_MIB_RX_FRAME511_OCT	0x2c
#define	MVNETA_MIB_RX_FRAME1023_OCT	0x30
#define	MVNETA_MIB_RX_FRAMEMAX_OCT	0x34

/* Tx */
#define	MVNETA_MIB_TX_MAC_TRNS_ERR	0x0c
#define	MVNETA_MIB_TX_GOOD_OCT		0x38 /* 64bit */
#define	MVNETA_MIB_TX_GOOD_FRAME	0x40
#define	MVNETA_MIB_TX_EXCES_COL		0x44
#define	MVNETA_MIB_TX_MCAST_FRAME	0x48
#define	MVNETA_MIB_TX_BCAST_FRAME	0x4c
#define	MVNETA_MIB_TX_MAC_CTL_ERR	0x50

/* Flow Control */
#define	MVNETA_MIB_FC_SENT		0x54
#define	MVNETA_MIB_FC_GOOD		0x58
#define	MVNETA_MIB_FC_BAD		0x5c

/* Packet Processing */
#define	MVNETA_MIB_PKT_UNDERSIZE	0x60
#define	MVNETA_MIB_PKT_FRAGMENT		0x64
#define	MVNETA_MIB_PKT_OVERSIZE		0x68
#define	MVNETA_MIB_PKT_JABBER		0x6c

/* MAC Layer Errors */
#define	MVNETA_MIB_MAC_RX_ERR		0x70
#define	MVNETA_MIB_MAC_CRC_ERR		0x74
#define	MVNETA_MIB_MAC_COL		0x78
#define	MVNETA_MIB_MAC_LATE_COL		0x7c

/* END OF REGISTER NUMBERS */

/*
 *
 * Register Formats
 *
 */
/*
 * Address Decoder Registers
 */
/* Base Address (MVNETA_BASEADDR) */
#define	MVNETA_BASEADDR_TARGET(target)	((target) & 0xf)
#define	MVNETA_BASEADDR_ATTR(attr)	(((attr) & 0xff) << 8)
#define	MVNETA_BASEADDR_BASE(base)	((base) & 0xffff0000)

/* Size (MVNETA_S) */
#define	MVNETA_S_SIZE(size)		(((size) - 1) & 0xffff0000)

/* Base Address Enable (MVNETA_BARE) */
#define	MVNETA_BARE_EN_MASK		((1 << MVNETA_NWINDOW) - 1)
#define	MVNETA_BARE_EN(win)		((1 << (win)) & MVNETA_BARE_EN_MASK)

/* Ethernet Port Access Protect (MVNETA_EPAP) */
#define	MVNETA_EPAP_AC_NAC		0x0	/* No access allowed */
#define	MVNETA_EPAP_AC_RO		0x1	/* Read Only */
#define	MVNETA_EPAP_AC_FA		0x3	/* Full access (r/w) */
#define	MVNETA_EPAP_EPAR(win, ac)	((ac) << ((win) * 2))

/*
 * Global Miscellaneous Registers
 */
/* PHY Address (MVNETA_PHYADDR) */
#define	MVNETA_PHYADDR_PHYAD(phy)	((phy) & 0x1f)
#define	MVNETA_PHYADDR_GET_PHYAD(reg)	((reg) & 0x1f)

/* SMI register fields (MVNETA_SMI) */
#define	MVNETA_SMI_DATA_MASK		0x0000ffff
#define	MVNETA_SMI_PHYAD(phy)		(((phy) & 0x1f) << 16)
#define	MVNETA_SMI_REGAD(reg)		(((reg) & 0x1f) << 21)
#define	MVNETA_SMI_OPCODE_WRITE		(0 << 26)
#define	MVNETA_SMI_OPCODE_READ		(1 << 26)
#define	MVNETA_SMI_READVALID		(1 << 27)
#define	MVNETA_SMI_BUSY			(1 << 28)

/* Ethernet Unit Default ID (MVNETA_EUDID) */
#define	MVNETA_EUDID_DIDR_MASK		0x0000000f
#define	MVNETA_EUDID_DIDR(id)		((id) & 0x0f)
#define	MVNETA_EUDID_DATTR_MASK		0x00000ff0
#define	MVNETA_EUDID_DATTR(attr)	(((attr) & 0xff) << 4)

/* Ethernet Unit Interrupt Cause (MVNETA_EUIC) */
#define	MVNETA_EUIC_ETHERINTSUM		(1 << 0)
#define	MVNETA_EUIC_PARITY		(1 << 1)
#define	MVNETA_EUIC_ADDRVIOL		(1 << 2)
#define	MVNETA_EUIC_ADDRVNOMATCH	(1 << 3)
#define	MVNETA_EUIC_SMIDONE		(1 << 4)
#define	MVNETA_EUIC_COUNTWA		(1 << 5)
#define	MVNETA_EUIC_INTADDRERR		(1 << 7)
#define	MVNETA_EUIC_PORT0DPERR		(1 << 9)
#define	MVNETA_EUIC_TOPDPERR		(1 << 12)

/* Ethernet Unit Internal Addr Error (MVNETA_EUIAE) */
#define	MVNETA_EUIAE_INTADDR_MASK	0x000001ff
#define	MVNETA_EUIAE_INTADDR(addr)	((addr) & 0x1ff)
#define	MVNETA_EUIAE_GET_INTADDR(addr)	((addr) & 0x1ff)

/* Ethernet Unit Control (MVNETA_EUC) */
#define	MVNETA_EUC_POLLING		(1 << 1)
#define	MVNETA_EUC_PORTRESET		(1 << 24)
#define	MVNETA_EUC_RAMSINITIALIZATIONCOMPLETED (1 << 25)

/*
 * Miscellaneous Registers
 */
/* SDMA Configuration (MVNETA_SDC) */
#define	MVNETA_SDC_RXBSZ(x)		((x) << 1)
#define	MVNETA_SDC_RXBSZ_MASK		MVNETA_SDC_RXBSZ(7)
#define	MVNETA_SDC_RXBSZ_1_64BITWORDS	MVNETA_SDC_RXBSZ(0)
#define	MVNETA_SDC_RXBSZ_2_64BITWORDS	MVNETA_SDC_RXBSZ(1)
#define	MVNETA_SDC_RXBSZ_4_64BITWORDS	MVNETA_SDC_RXBSZ(2)
#define	MVNETA_SDC_RXBSZ_8_64BITWORDS	MVNETA_SDC_RXBSZ(3)
#define	MVNETA_SDC_RXBSZ_16_64BITWORDS	MVNETA_SDC_RXBSZ(4)
#define	MVNETA_SDC_BLMR			(1 << 4)
#define	MVNETA_SDC_BLMT			(1 << 5)
#define	MVNETA_SDC_SWAPMODE		(1 << 6)
#define	MVNETA_SDC_TXBSZ(x)		((x) << 22)
#define	MVNETA_SDC_TXBSZ_MASK		MVNETA_SDC_TXBSZ(7)
#define	MVNETA_SDC_TXBSZ_1_64BITWORDS	MVNETA_SDC_TXBSZ(0)
#define	MVNETA_SDC_TXBSZ_2_64BITWORDS	MVNETA_SDC_TXBSZ(1)
#define	MVNETA_SDC_TXBSZ_4_64BITWORDS	MVNETA_SDC_TXBSZ(2)
#define	MVNETA_SDC_TXBSZ_8_64BITWORDS	MVNETA_SDC_TXBSZ(3)
#define	MVNETA_SDC_TXBSZ_16_64BITWORDS	MVNETA_SDC_TXBSZ(4)

/*
 * Networking Controller Miscellaneous Registers
 */
/* Port Acceleration Mode (MVNETA_PACC) */
#define	MVNETA_PACC_ACCELERATIONMODE_MASK	0x7
#define	MVNETA_PACC_ACCELERATIONMODE_EDM	0x1	/* Enhanced Desc Mode */

/* Port Version (MVNETA_PV) */
#define	MVNETA_PV_VERSION_MASK			0xff
#define	MVNETA_PV_VERSION(v)			((v) & 0xff)
#define	MVNETA_PV_GET_VERSION(reg)		((reg) & 0xff)

/*
 * Rx DMA Hardware Parser Registers
 */
/* Ether Type Priority (MVNETA_ETP) */
#define	MVNETA_ETP_ETHERTYPEPRIEN	(1 << 0)	/* EtherType Prio Ena */
#define	MVNETA_ETP_ETHERTYPEPRIFRSTEN	(1 << 1)
#define	MVNETA_ETP_ETHERTYPEPRIQ	(0x7 << 2)	/*EtherType Prio Queue*/
#define	MVNETA_ETP_ETHERTYPEPRIVAL	(0xffff << 5)	/*EtherType Prio Value*/
#define	MVNETA_ETP_FORCEUNICSTHIT	(1 << 21)	/* Force Unicast hit */

/* Destination Address Filter Registers (MVNETA_DF{SM,OM,U}T) */
#define	MVNETA_DF(n, x)			((x) << (8 * (n)))
#define	MVNETA_DF_PASS			(1 << 0)
#define	MVNETA_DF_QUEUE(q)		((q) << 1)
#define	MVNETA_DF_QUEUE_ALL		((MVNETA_RX_QNUM_MAX-1) << 1)
#define	MVNETA_DF_QUEUE_MASK		((MVNETA_RX_QNUM_MAX-1) << 1)

/*
 * Rx DMA Miscellaneous Registers
 */
/* Port Rx Minimal Frame Size (MVNETA_PMFS) */
#define	MVNETA_PMFS_RXMFS(rxmfs)	(((rxmfs) - 40) & 0x7c)

/* Receive Queue Command (MVNETA_RQC) */
#define	MVNETA_RQC_EN_MASK		(0xff << 0)	/* Enable Q */
#define	MVNETA_RQC_ENQ(q)		(1 << (0 + (q)))
#define	MVNETA_RQC_EN(n)		((n) << 0)
#define	MVNETA_RQC_DIS_MASK		(0xff << 8)	/* Disable Q */
#define	MVNETA_RQC_DISQ(q)		(1 << (8 + (q)))
#define	MVNETA_RQC_DIS(n)		((n) << 8)

/*
 * Rx DMA Networking Controller Miscellaneous Registers
 */
/* Port RX queues Configuration (MVNETA_PRXC) */
#define	MVNETA_PRXC_PACKETOFFSET(o)	(((o) & 0xf) << 8)

/* Port RX queues Snoop (MVNETA_PRXSNP) */
#define	MVNETA_PRXSNP_SNOOPNOOFBYTES(b)	(((b) & 0x3fff) << 0)
#define	MVNETA_PRXSNP_L2DEPOSITNOOFBYTES(b) (((b) & 0x3fff) << 16)

/* Port RX queues Descriptors Queue Size (MVNETA_PRXDQS) */
#define	MVNETA_PRXDQS_DESCRIPTORSQUEUESIZE(s)	(((s) & 0x3fff) << 0)
#define	MVNETA_PRXDQS_BUFFERSIZE(s)		(((s) & 0x1fff) << 19)

/* Port RX queues Descriptors Queue Threshold (MVNETA_PRXDQTH) */
					/* Occupied Descriptors Threshold */
#define	MVNETA_PRXDQTH_ODT(x)		(((x) & 0x3fff) << 0)
					/* Non Occupied Descriptors Threshold */
#define	MVNETA_PRXDQTH_NODT(x)		(((x) & 0x3fff) << 16)

/* Port RX queues Status (MVNETA_PRXS) */
					/* Occupied Descriptors Counter */
#define	MVNETA_PRXS_ODC(x)		(((x) & 0x3fff) << 0)
					/* Non Occupied Descriptors Counter */
#define	MVNETA_PRXS_NODC(x)		(((x) & 0x3fff) << 16)
#define	MVNETA_PRXS_GET_ODC(reg)	(((reg) >> 0) & 0x3fff)
#define	MVNETA_PRXS_GET_NODC(reg)	(((reg) >> 16) & 0x3fff)

/* Port RX queues Status Update (MVNETA_PRXSU) */
#define	MVNETA_PRXSU_NOOFPROCESSEDDESCRIPTORS(x) (((x) & 0xff) << 0)
#define	MVNETA_PRXSU_NOOFNEWDESCRIPTORS(x) (((x) & 0xff) << 16)

/* Port RX Initialization (MVNETA_PRXINIT) */
#define	MVNETA_PRXINIT_RXDMAINIT	(1 << 0)

/*
 * Tx DMA Miscellaneous Registers
 */
/* Transmit Queue Command (MVNETA_TQC) */
#define	MVNETA_TQC_EN_MASK		(0xff << 0)
#define	MVNETA_TQC_ENQ(q)		(1 << ((q) + 0))/* Enable Q */
#define	MVNETA_TQC_EN(n)		((n) << 0)
#define	MVNETA_TQC_DIS_MASK		(0xff << 8)
#define	MVNETA_TQC_DISQ(q)		(1 << ((q) + 8))/* Disable Q */
#define	MVNETA_TQC_DIS(n)		((n) << 8)

/*
 * Tx DMA Networking Controller Miscellaneous Registers
 */
/* Port TX queues Descriptors Queue Size (MVNETA_PTXDQS) */
					/* Descriptors Queue Size */
#define	MVNETA_PTXDQS_DQS_MASK		(0x3fff << 0)
#define	MVNETA_PTXDQS_DQS(x)		(((x) & 0x3fff) << 0)
					/* Transmitted Buffer Threshold */
#define	MVNETA_PTXDQS_TBT_MASK		(0x3fff << 16)
#define	MVNETA_PTXDQS_TBT(x)		(((x) & 0x3fff) << 16)

/* Port TX queues Status (MVNETA_PTXS) */
					/* Transmitted Buffer Counter */
#define	MVNETA_PTXS_TBC(x)		(((x) & 0x3fff) << 16)

#define	MVNETA_PTXS_GET_TBC(reg)	(((reg) >> 16) & 0x3fff)
					/* Pending Descriptors Counter */
#define	MVNETA_PTXS_PDC(x)		((x) & 0x3fff)
#define	MVNETA_PTXS_GET_PDC(x)		((x) & 0x3fff)

/* Port TX queues Status Update (MVNETA_PTXSU) */
					/* Number Of Written Descriptors */
#define	MVNETA_PTXSU_NOWD(x)		(((x) & 0xff) << 0)
					/* Number Of Released Buffers */
#define	MVNETA_PTXSU_NORB(x)		(((x) & 0xff) << 16)

/* TX Transmitted Buffers Counter (MVNETA_TXTBC) */
					/* Transmitted Buffers Counter */
#define	MVNETA_TXTBC_TBC(x)		(((x) & 0x3fff) << 16)

/* Port TX Initialization (MVNETA_PTXINIT) */
#define	MVNETA_PTXINIT_TXDMAINIT	(1 << 0)

/*
 * Tx DMA Queue Arbiter Registers (Version 1 )
 */
/* Transmit Queue Fixed Priority Configuration */
#define	MVNETA_TQFPC_EN(q)		(1 << (q))

/*
 * RX_TX DMA Registers
 */
/* Port Configuration (MVNETA_PXC) */
#define	MVNETA_PXC_UPM			(1 << 0) /* Uni Promisc mode */
#define	MVNETA_PXC_RXQ(q)		((q) << 1)
#define	MVNETA_PXC_RXQ_MASK		MVNETA_PXC_RXQ(7)
#define	MVNETA_PXC_RXQARP(q)		((q) << 4)
#define	MVNETA_PXC_RXQARP_MASK		MVNETA_PXC_RXQARP(7)
#define	MVNETA_PXC_RB			(1 << 7) /* Rej mode of MAC */
#define	MVNETA_PXC_RBIP			(1 << 8)
#define	MVNETA_PXC_RBARP			(1 << 9)
#define	MVNETA_PXC_AMNOTXES		(1 << 12)
#define	MVNETA_PXC_RBARPF		(1 << 13)
#define	MVNETA_PXC_TCPCAPEN		(1 << 14)
#define	MVNETA_PXC_UDPCAPEN		(1 << 15)
#define	MVNETA_PXC_TCPQ(q)		((q) << 16)
#define	MVNETA_PXC_TCPQ_MASK		MVNETA_PXC_TCPQ(7)
#define	MVNETA_PXC_UDPQ(q)		((q) << 19)
#define	MVNETA_PXC_UDPQ_MASK		MVNETA_PXC_UDPQ(7)
#define	MVNETA_PXC_BPDUQ(q)		((q) << 22)
#define	MVNETA_PXC_BPDUQ_MASK		MVNETA_PXC_BPDUQ(7)
#define	MVNETA_PXC_RXCS			(1 << 25)

/* Port Configuration Extend (MVNETA_PXCX) */
#define	MVNETA_PXCX_SPAN			(1 << 1)
#define	MVNETA_PXCX_TXCRCDIS		(1 << 3)

/* Marvell Header (MVNETA_MH) */
#define	MVNETA_MH_MHEN			(1 << 0)
#define	MVNETA_MH_DAPREFIX		(0x3 << 1)
#define	MVNETA_MH_SPID			(0xf << 4)
#define	MVNETA_MH_MHMASK		(0x3 << 8)
#define	MVNETA_MH_MHMASK_8QUEUES	(0x0 << 8)
#define	MVNETA_MH_MHMASK_4QUEUES	(0x1 << 8)
#define	MVNETA_MH_MHMASK_2QUEUES	(0x3 << 8)
#define	MVNETA_MH_DSAEN_MASK		(0x3 << 10)
#define	MVNETA_MH_DSAEN_DISABLE		(0x0 << 10)
#define	MVNETA_MH_DSAEN_NONEXTENDED	(0x1 << 10)
#define	MVNETA_MH_DSAEN_EXTENDED	(0x2 << 10)

/*
 * Serial(SMI/MII) Registers
 */
#define	MVNETA_PSOMSCD_ENABLE		(1UL<<31)
#define	MVNETA_PSERDESCFG_QSGMII	(0x0667)
#define	MVNETA_PSERDESCFG_SGMII		(0x0cc7)
/* Port Seiral Control0 (MVNETA_PSC0) */
#define	MVNETA_PSC0_FORCE_FC_MASK	(0x3 << 5)
#define	MVNETA_PSC0_FORCE_FC(fc)	(((fc) & 0x3) << 5)
#define	MVNETA_PSC0_FORCE_FC_PAUSE	MVNETA_PSC0_FORCE_FC(0x1)
#define	MVNETA_PSC0_FORCE_FC_NO_PAUSE	MVNETA_PSC0_FORCE_FC(0x0)
#define	MVNETA_PSC0_FORCE_BP_MASK	(0x3 << 7)
#define	MVNETA_PSC0_FORCE_BP(fc)	(((fc) & 0x3) << 5)
#define	MVNETA_PSC0_FORCE_BP_JAM	MVNETA_PSC0_FORCE_BP(0x1)
#define	MVNETA_PSC0_FORCE_BP_NO_JAM	MVNETA_PSC0_FORCE_BP(0x0)
#define	MVNETA_PSC0_DTE_ADV		(1 << 14)
#define	MVNETA_PSC0_IGN_RXERR		(1 << 28)
#define	MVNETA_PSC0_IGN_COLLISION	(1 << 29)
#define	MVNETA_PSC0_IGN_CARRIER		(1 << 30)

/* Ethernet Port Status0 (MVNETA_PS0) */
#define	MVNETA_PS0_TXINPROG		(1 << 0)
#define	MVNETA_PS0_TXFIFOEMP		(1 << 8)
#define	MVNETA_PS0_RXFIFOEMPTY		(1 << 16)

/*
 * Gigabit Ethernet MAC Serial Parameters Configuration Registers
 */
#define	MVNETA_PSPC_MUST_SET		(1 << 3 | 1 << 4 | 1 << 5 | 0x23 << 6)
#define	MVNETA_PSP1C_MUST_SET		(1 << 0 | 1 << 1 | 1 << 2)

/*
 * Gigabit Ethernet Auto-Negotiation Configuration Registers
 */
/* Port Auto-Negotiation Configuration (MVNETA_PANC) */
#define	MVNETA_PANC_FORCELINKFAIL	(1 << 0)
#define	MVNETA_PANC_FORCELINKPASS	(1 << 1)
#define	MVNETA_PANC_INBANDANEN		(1 << 2)
#define	MVNETA_PANC_INBANDANBYPASSEN	(1 << 3)
#define	MVNETA_PANC_INBANDRESTARTAN	(1 << 4)
#define	MVNETA_PANC_SETMIISPEED		(1 << 5)
#define	MVNETA_PANC_SETGMIISPEED	(1 << 6)
#define	MVNETA_PANC_ANSPEEDEN		(1 << 7)
#define	MVNETA_PANC_SETFCEN		(1 << 8)
#define	MVNETA_PANC_PAUSEADV		(1 << 9)
#define	MVNETA_PANC_ANFCEN		(1 << 11)
#define	MVNETA_PANC_SETFULLDX		(1 << 12)
#define	MVNETA_PANC_ANDUPLEXEN		(1 << 13)
#define	MVNETA_PANC_MUSTSET		(1 << 15)

/*
 * Gigabit Ethernet MAC Control Registers
 */
/* Port MAC Control 0 (MVNETA_PMACC0) */
#define	MVNETA_PMACC0_PORTEN		(1 << 0)
#define	MVNETA_PMACC0_PORTTYPE		(1 << 1)
#define	MVNETA_PMACC0_FRAMESIZELIMIT(x)		((((x) >> 1) << 2) & 0x7ffc)
#define	MVNETA_PMACC0_FRAMESIZELIMIT_MASK	(0x7ffc)
#define	MVNETA_PMACC0_MUSTSET		(1 << 15)

/* Port MAC Control 1 (MVNETA_PMACC1) */
#define	MVNETA_PMACC1_PCSLB		(1 << 6)

/* Port MAC Control 2 (MVNETA_PMACC2) */
#define	MVNETA_PMACC2_INBANDANMODE	(1 << 0)
#define	MVNETA_PMACC2_PCSEN		(1 << 3)
#define	MVNETA_PMACC2_PCSEN		(1 << 3)
#define	MVNETA_PMACC2_RGMIIEN		(1 << 4)
#define	MVNETA_PMACC2_PADDINGDIS		(1 << 5)
#define	MVNETA_PMACC2_PORTMACRESET	(1 << 6)
#define	MVNETA_PMACC2_PRBSCHECKEN	(1 << 10)
#define	MVNETA_PMACC2_PRBSGENEN		(1 << 11)
#define	MVNETA_PMACC2_SDTT_MASK		(3 << 12)  /* Select Data To Transmit */
#define	MVNETA_PMACC2_SDTT_RM		(0 << 12)	/* Regular Mode */
#define	MVNETA_PMACC2_SDTT_PRBS		(1 << 12)	/* PRBS Mode */
#define	MVNETA_PMACC2_SDTT_ZC		(2 << 12)	/* Zero Constant */
#define	MVNETA_PMACC2_SDTT_OC		(3 << 12)	/* One Constant */
#define	MVNETA_PMACC2_MUSTSET		(3 << 14)

/* Port MAC Control 3 (MVNETA_PMACC3) */
#define	MVNETA_PMACC3_IPG_MASK		0x7f80

/*
 * Gigabit Ethernet MAC Interrupt Registers
 */
/* Port Interrupt Cause/Mask (MVNETA_PIC/MVNETA_PIM) */
#define	MVNETA_PI_INTSUM			(1 << 0)
#define	MVNETA_PI_LSC			(1 << 1)   /* LinkStatus Change */
#define	MVNETA_PI_ACOP			(1 << 2)   /* AnCompleted OnPort */
#define	MVNETA_PI_AOOR			(1 << 5)   /* AddressOut Of Range */
#define	MVNETA_PI_SSC			(1 << 6)   /* SyncStatus Change */
#define	MVNETA_PI_PRBSEOP		(1 << 7)   /* QSGMII PRBS error */
#define	MVNETA_PI_MIBCWA			(1 << 15)  /* MIB counter wrap around */
#define	MVNETA_PI_QSGMIIPRBSE		(1 << 10)  /* QSGMII PRBS error */
#define	MVNETA_PI_PCSRXPRLPI		(1 << 11)  /* PCS Rx path received LPI*/
#define	MVNETA_PI_PCSTXPRLPI		(1 << 12)  /* PCS Tx path received LPI*/
#define	MVNETA_PI_MACRXPRLPI		(1 << 13)  /* MAC Rx path received LPI*/
#define	MVNETA_PI_MIBCCD			(1 << 14)  /* MIB counters copy done */

/*
 * Gigabit Ethernet MAC Low Power Idle Registers
 */
/* LPI Control 0 (MVNETA_LPIC0) */
#define	MVNETA_LPIC0_LILIMIT(x)		(((x) & 0xff) << 0)
#define	MVNETA_LPIC0_TSLIMIT(x)		(((x) & 0xff) << 8)

/* LPI Control 1 (MVNETA_LPIC1) */
#define	MVNETA_LPIC1_LPIRE		(1 << 0)	/* LPI request enable */
#define	MVNETA_LPIC1_LPIRF		(1 << 1)	/* LPI request force */
#define	MVNETA_LPIC1_LPIMM		(1 << 2)	/* LPI manual mode */
#define	MVNETA_LPIC1_TWLIMIT(x)		(((x) & 0xfff) << 4)

/* LPI Control 2 (MVNETA_LPIC2) */
#define	MVNETA_LPIC2_MUSTSET		0x17d

/* LPI Status (MVNETA_LPIS) */
#define	MVNETA_LPIS_PCSRXPLPIS		(1 << 0) /* PCS Rx path LPI status */
#define	MVNETA_LPIS_PCSTXPLPIS		(1 << 1) /* PCS Tx path LPI status */
#define	MVNETA_LPIS_MACRXPLPIS		(1 << 2)/* MAC Rx path LP idle status */
#define	MVNETA_LPIS_MACTXPLPWS		(1 << 3)/* MAC Tx path LP wait status */
#define	MVNETA_LPIS_MACTXPLPIS		(1 << 4)/* MAC Tx path LP idle status */

/*
 * Gigabit Ethernet MAC PRBS Check Status Registers
 */
/* Port PRBS Status (MVNETA_PPRBSS) */
#define	MVNETA_PPRBSS_PRBSCHECKLOCKED	(1 << 0)
#define	MVNETA_PPRBSS_PRBSCHECKRDY	(1 << 1)

/*
 * Gigabit Ethernet MAC Status Registers
 */
/* Port Status Register (MVNETA_PSR) */
#define	MVNETA_PSR_LINKUP		(1 << 0)
#define	MVNETA_PSR_GMIISPEED		(1 << 1)
#define	MVNETA_PSR_MIISPEED		(1 << 2)
#define	MVNETA_PSR_FULLDX		(1 << 3)
#define	MVNETA_PSR_RXFCEN		(1 << 4)
#define	MVNETA_PSR_TXFCEN		(1 << 5)
#define	MVNETA_PSR_PRP			(1 << 6) /* Port Rx Pause */
#define	MVNETA_PSR_PTP			(1 << 7) /* Port Tx Pause */
#define	MVNETA_PSR_PDP			(1 << 8) /*Port is Doing Back-Pressure*/
#define	MVNETA_PSR_SYNCFAIL10MS		(1 << 10)
#define	MVNETA_PSR_ANDONE		(1 << 11)
#define	MVNETA_PSR_IBANBA		(1 << 12) /* InBand AutoNeg BypassAct */
#define	MVNETA_PSR_SYNCOK		(1 << 14)

/*
 * Networking Controller Interrupt Registers
 */
/* Port CPU to Queue */
#define	MVNETA_MAXCPU			2
#define	MVNETA_PCP2Q_TXQEN(q)		(1 << ((q) + 8))
#define	MVNETA_PCP2Q_TXQEN_MASK		(0xff << 8)
#define	MVNETA_PCP2Q_RXQEN(q)		(1 << ((q) + 0))
#define	MVNETA_PCP2Q_RXQEN_MASK		(0xff << 0)

/* Port RX_TX Interrupt Threshold */
#define	MVNETA_PRXITTH_RITT(t)		((t) & 0xffffff)

/* Port RX_TX Threshold Interrupt Cause/Mask (MVNETA_PRXTXTIC/MVNETA_PRXTXTIM) */
#define	MVNETA_PRXTXTI_TBTCQ(q)		(1 << ((q) + 0))
#define	MVNETA_PRXTXTI_TBTCQ_MASK	(0xff << 0)
#define	MVNETA_PRXTXTI_GET_TBTCQ(reg)	(((reg) >> 0) & 0xff)
					/* Tx Buffer Threshold Cross Queue*/
#define	MVNETA_PRXTXTI_RBICTAPQ(q)	(1 << ((q) + 8))
#define	MVNETA_PRXTXTI_RBICTAPQ_MASK	(0xff << 8)
#define	MVNETA_PRXTXTI_GET_RBICTAPQ(reg)	(((reg) >> 8) & 0xff)
				/* Rx Buffer Int. Coaleasing Th. Pri. Alrt Q */
#define	MVNETA_PRXTXTI_RDTAQ(q)		(1 << ((q) + 16))
#define	MVNETA_PRXTXTI_RDTAQ_MASK	(0xff << 16)
#define	MVNETA_PRXTXTI_GET_RDTAQ(reg)	(((reg) >> 16) & 0xff)
					/* Rx Descriptor Threshold Alert Queue*/
#define	MVNETA_PRXTXTI_PRXTXICSUMMARY	(1 << 29)	/* PRXTXI summary */
#define	MVNETA_PRXTXTI_PTXERRORSUMMARY	(1 << 30)	/* PTEXERROR summary */
#define	MVNETA_PRXTXTI_PMISCICSUMMARY	(1UL << 31)	/* PMISCIC summary */

/* Port RX_TX Interrupt Cause/Mask (MVNETA_PRXTXIC/MVNETA_PRXTXIM) */
#define	MVNETA_PRXTXI_TBRQ(q)		(1 << ((q) + 0))
#define	MVNETA_PRXTXI_TBRQ_MASK		(0xff << 0)
#define	MVNETA_PRXTXI_GET_TBRQ(reg)	(((reg) >> 0) & 0xff)
#define	MVNETA_PRXTXI_RPQ(q)		(1 << ((q) + 8))
#define	MVNETA_PRXTXI_RPQ_MASK		(0xff << 8)
#define	MVNETA_PRXTXI_GET_RPQ(reg)	(((reg) >> 8) & 0xff)
#define	MVNETA_PRXTXI_RREQ(q)		(1 << ((q) + 16))
#define	MVNETA_PRXTXI_RREQ_MASK		(0xff << 16)
#define	MVNETA_PRXTXI_GET_RREQ(reg)	(((reg) >> 16) & 0xff)
#define	MVNETA_PRXTXI_PRXTXTHICSUMMARY	(1 << 29)
#define	MVNETA_PRXTXI_PTXERRORSUMMARY	(1 << 30)
#define	MVNETA_PRXTXI_PMISCICSUMMARY	(1UL << 31)

/* Port Misc Interrupt Cause/Mask (MVNETA_PMIC/MVNETA_PMIM) */
#define	MVNETA_PMI_PHYSTATUSCHNG	(1 << 0)
#define	MVNETA_PMI_LINKCHANGE		(1 << 1)
#define	MVNETA_PMI_IAE			(1 << 7) /* Internal Address Error */
#define	MVNETA_PMI_RXOVERRUN		(1 << 8)
#define	MVNETA_PMI_RXCRCERROR		(1 << 9)
#define	MVNETA_PMI_RXLARGEPACKET	(1 << 10)
#define	MVNETA_PMI_TXUNDRN		(1 << 11)
#define	MVNETA_PMI_PRBSERROR		(1 << 12)
#define	MVNETA_PMI_PSCSYNCCHANGE	(1 << 13)
#define	MVNETA_PMI_SRSE			(1 << 14) /* SerdesRealignSyncError */
#define	MVNETA_PMI_TREQ(q)		(1 << ((q) + 24)) /* TxResourceErrorQ */
#define	MVNETA_PMI_TREQ_MASK		(0xff << 24) /* TxResourceErrorQ */

/* Port Interrupt Enable (MVNETA_PIE) */
#define	MVNETA_PIE_RXPKTINTRPTENB(q)	(1 << ((q) + 0))
#define	MVNETA_PIE_TXPKTINTRPTENB(q)	(1 << ((q) + 8))
#define	MVNETA_PIE_RXPKTINTRPTENB_MASK	(0xff << 0)
#define	MVNETA_PIE_TXPKTINTRPTENB_MASK	(0xff << 8)

/*
 * Miscellaneous Interrupt Registers
 */
#define	MVNETA_PEUIAE_ADDR_MASK		(0x3fff)
#define	MVNETA_PEUIAE_ADDR(addr)	((addr) & 0x3fff)
#define	MVNETA_PEUIAE_GET_ADDR(reg)	((reg) & 0x3fff)

/*
 * SGMII PHY Registers
 */
/* Power and PLL Control (MVNETA_PPLLC) */
#define	MVNETA_PPLLC_REF_FREF_SEL_MASK	(0xf << 0)
#define	MVNETA_PPLLC_PHY_MODE_MASK	(7 << 5)
#define	MVNETA_PPLLC_PHY_MODE_SATA	(0 << 5)
#define	MVNETA_PPLLC_PHY_MODE_SAS	(1 << 5)
#define	MVNETA_PPLLC_PLL_LOCK		(1 << 8)
#define	MVNETA_PPLLC_PU_DFE		(1 << 10)
#define	MVNETA_PPLLC_PU_TX_INTP		(1 << 11)
#define	MVNETA_PPLLC_PU_TX		(1 << 12)
#define	MVNETA_PPLLC_PU_RX		(1 << 13)
#define	MVNETA_PPLLC_PU_PLL		(1 << 14)

/* Digital Loopback Enable (MVNETA_DLE) */
#define	MVNETA_DLE_LOCAL_SEL_BITS_MASK		(3 << 10)
#define	MVNETA_DLE_LOCAL_SEL_BITS_10BITS	(0 << 10)
#define	MVNETA_DLE_LOCAL_SEL_BITS_20BITS	(1 << 10)
#define	MVNETA_DLE_LOCAL_SEL_BITS_40BITS	(2 << 10)
#define	MVNETA_DLE_LOCAL_RXPHER_TO_TX_EN	(1 << 12)
#define	MVNETA_DLE_LOCAL_ANA_TX2RX_LPBK_EN	(1 << 13)
#define	MVNETA_DLE_LOCAL_DIG_TX2RX_LPBK_EN	(1 << 14)
#define	MVNETA_DLE_LOCAL_DIG_RX2TX_LPBK_EN	(1 << 15)

/* Reference Clock Select (MVNETA_RCS) */
#define	MVNETA_RCS_REFCLK_SEL		(1 << 10)

/*
 * DMA descriptors
 */
struct mvneta_tx_desc {
	/* LITTLE_ENDIAN */
	uint32_t command;		/* off 0x00: commands */
	uint16_t l4ichk;		/* initial checksum */
	uint16_t bytecnt;		/* 0ff 0x04: buffer byte count */
	uint32_t bufptr_pa;		/* off 0x08: buffer ptr(PA) */
	uint32_t flags;			/* off 0x0c: flags */
	uint32_t reserved0;		/* off 0x10 */
	uint32_t reserved1;		/* off 0x14 */
	uint32_t reserved2;		/* off 0x18 */
	uint32_t reserved3;		/* off 0x1c */
};

struct mvneta_rx_desc {
	/* LITTLE_ENDIAN */
	uint32_t status;		/* status and flags */
	uint16_t reserved0;
	uint16_t bytecnt;		/* buffer byte count */
	uint32_t bufptr_pa;		/* packet buffer pointer */
	uint32_t reserved1;
	uint32_t bufptr_va;
	uint16_t reserved2;
	uint16_t l4chk;			/* L4 checksum */
	uint32_t reserved3;
	uint32_t reserved4;
};

/*
 * Received packet command header:
 *  network controller => software
 * the controller parse the packet and set some flags.
 */
#define	MVNETA_RX_IPV4_FRAGMENT	(1UL << 31) /* Fragment Indicator */
#define	MVNETA_RX_L4_CHECKSUM_OK	(1 << 30) /* L4 Checksum */
/* bit 29 reserved */
#define	MVNETA_RX_U			(1 << 28) /* Unknown Destination */
#define	MVNETA_RX_F			(1 << 27) /* First buffer */
#define	MVNETA_RX_L			(1 << 26) /* Last buffer */
#define	MVNETA_RX_IP_HEADER_OK		(1 << 25) /* IP Header is OK */
#define	MVNETA_RX_L3_IP			(1 << 24) /* IP Type 0:IP6 1:IP4 */
#define	MVNETA_RX_L2_EV2		(1 << 23) /* Ethernet v2 frame */
#define	MVNETA_RX_L4_MASK		(3 << 21) /* L4 Type */
#define	MVNETA_RX_L4_TCP		(0x00 << 21)
#define	MVNETA_RX_L4_UDP		(0x01 << 21)
#define	MVNETA_RX_L4_OTH		(0x10 << 21)
#define	MVNETA_RX_BPDU			(1 << 20) /* BPDU frame */
#define	MVNETA_RX_VLAN			(1 << 19) /* VLAN tag found */
#define	MVNETA_RX_EC_MASK		(3 << 17) /* Error code */
#define	MVNETA_RX_EC_CE			(0x00 << 17) /* CRC error */
#define	MVNETA_RX_EC_OR			(0x01 << 17) /* FIFO overrun */
#define	MVNETA_RX_EC_MF			(0x10 << 17) /* Max. frame len */
#define	MVNETA_RX_EC_RE			(0x11 << 17) /* Resource error */
#define	MVNETA_RX_ES			(1 << 16) /* Error summary */
/* bit 15:0 reserved */

/*
 * Transmit packet command header:
 *  software => network controller
 */
#define	MVNETA_TX_CMD_L4_CHECKSUM_MASK		(0x3 << 30) /* Do L4 Checksum */
#define	MVNETA_TX_CMD_L4_CHECKSUM_FRAG		(0x0 << 30)
#define	MVNETA_TX_CMD_L4_CHECKSUM_NOFRAG	(0x1 << 30)
#define	MVNETA_TX_CMD_L4_CHECKSUM_NONE		(0x2 << 30)
#define	MVNETA_TX_CMD_PACKET_OFFSET_MASK	(0x7f << 23) /* Payload offset */
#define	MVNETA_TX_CMD_W_PACKET_OFFSET(v)	(((v) & 0x7f) << 23)
/* bit 22 reserved */
#define	MVNETA_TX_CMD_F			(1 << 21) /* First buffer */
#define	MVNETA_TX_CMD_L			(1 << 20) /* Last buffer */
#define	MVNETA_TX_CMD_PADDING		(1 << 19) /* Pad short frame */
#define	MVNETA_TX_CMD_IP4_CHECKSUM	(1 << 18) /* Do IPv4 Checksum */
#define	MVNETA_TX_CMD_L3_IP4		(0 << 17)
#define	MVNETA_TX_CMD_L3_IP6		(1 << 17)
#define	MVNETA_TX_CMD_L4_TCP		(0 << 16)
#define	MVNETA_TX_CMD_L4_UDP		(1 << 16)
/* bit 15:13 reserved */
#define	MVNETA_TX_CMD_IP_HEADER_LEN_MASK	(0x1f << 8) /* IP header len >> 2 */
#define	MVNETA_TX_CMD_IP_HEADER_LEN(v)		(((v) & 0x1f) << 8)
/* bit 7 reserved */
#define	MVNETA_TX_CMD_L3_OFFSET_MASK	(0x7f << 0) /* offset of L3 hdr. */
#define	MVNETA_TX_CMD_L3_OFFSET(v)	(((v) & 0x7f) << 0)

/*
 * Transmit packet extra attributes
 * and error status returned from network controller.
 */
#define	MVNETA_TX_F_DSA_TAG		(3 << 30)	/* DSA Tag */
/* bit 29:8 reserved */
#define	MVNETA_TX_F_MH_SEL		(0xf << 4)	/* Marvell Header */
/* bit 3 reserved */
#define	MVNETA_TX_F_EC_MASK		(3 << 1)	/* Error code */
#define	MVNETA_TX_F_EC_LC		(0x00 << 1)	/* Late Collision */
#define	MVNETA_TX_F_EC_UR		(0x01 << 1)	/* Underrun */
#define	MVNETA_TX_F_EC_RL		(0x10 << 1)	/* Excess. Collision */
#define	MVNETA_TX_F_EC_RESERVED		(0x11 << 1)
#define	MVNETA_TX_F_ES			(1 << 0)	/* Error summary */

#define	MVNETA_ERROR_SUMMARY		(1 << 0)
#define	MVNETA_BUFFER_OWNED_MASK	(1UL << 31)
#define	MVNETA_BUFFER_OWNED_BY_HOST	(0UL << 31)
#define	MVNETA_BUFFER_OWNED_BY_DMA	(1UL << 31)

#endif	/* _IF_MVNETAREG_H_ */
