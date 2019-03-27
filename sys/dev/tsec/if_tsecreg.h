/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2008-2009 Semihalf, Piotr Ziecik
 * Copyright (C) 2006-2007 Semihalf, Piotr Kruszynski
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#define	TSEC_REG_ID		0x000	/* Controller ID register #1. */
#define	TSEC_REG_ID2		0x004	/* Controller ID register #2. */

/* TSEC General Control and Status Registers */
#define TSEC_REG_IEVENT		0x010 /* Interrupt event register */
#define TSEC_REG_IMASK		0x014 /* Interrupt mask register */
#define TSEC_REG_EDIS		0x018 /* Error disabled register */
#define TSEC_REG_ECNTRL		0x020 /* Ethernet control register */
#define TSEC_REG_MINFLR		0x024 /* Minimum frame length register */
#define TSEC_REG_PTV		0x028 /* Pause time value register */
#define TSEC_REG_DMACTRL	0x02c /* DMA control register */
#define TSEC_REG_TBIPA		0x030 /* TBI PHY address register */

/* TSEC FIFO Control and Status Registers */
#define TSEC_REG_FIFO_PAUSE_CTRL 0x04c /* FIFO pause control register */
#define TSEC_REG_FIFO_TX_THR	0x08c /* FIFO transmit threshold register */
#define TSEC_REG_FIFO_TX_STARVE	0x098 /* FIFO transmit starve register */
#define TSEC_REG_FIFO_TX_STARVE_SHUTOFF	0x09c /* FIFO transmit starve shutoff
					       * register */

/* TSEC Transmit Control and Status Registers */
#define TSEC_REG_TCTRL		0x100 /* Transmit control register */
#define TSEC_REG_TSTAT		0x104 /* Transmit Status Register */
#define TSEC_REG_TBDLEN		0x10c /* TxBD data length register */
#define TSEC_REG_TXIC		0x110 /* Transmit interrupt coalescing
				       * configuration register */
#define TSEC_REG_CTBPTR		0x124 /* Current TxBD pointer register */
#define TSEC_REG_TBPTR		0x184 /* TxBD pointer register */
#define TSEC_REG_TBASE		0x204 /* TxBD base address register */
#define TSEC_REG_OSTBD		0x2b0 /* Out-of-sequence TxBD register */
#define TSEC_REG_OSTBDP		0x2b4 /* Out-of-sequence Tx data buffer pointer
				       * register */

/* TSEC Receive Control and Status Registers */
#define TSEC_REG_RCTRL		0x300 /* Receive control register */
#define TSEC_REG_RSTAT		0x304 /* Receive status register */
#define TSEC_REG_RBDLEN		0x30c /* RxBD data length register */
#define TSEC_REG_RXIC		0x310 /* Receive interrupt coalescing
				       * configuration register */
#define TSEC_REG_CRBPTR		0x324 /* Current RxBD pointer register */
#define TSEC_REG_MRBLR		0x340 /* Maximum receive buffer length register */
#define TSEC_REG_RBPTR		0x384 /* RxBD pointer register */
#define TSEC_REG_RBASE		0x404 /* RxBD base address register */

/* TSEC MAC Registers */
#define TSEC_REG_MACCFG1	0x500 /* MAC configuration 1 register */
#define TSEC_REG_MACCFG2	0x504 /* MAC configuration 2 register */
#define TSEC_REG_IPGIFG		0x508 /* Inter-packet gap/inter-frame gap
				       * register */
#define TSEC_REG_HAFDUP		0x50c /* Half-duplex register */
#define TSEC_REG_MAXFRM		0x510 /* Maximum frame length register */
#define TSEC_REG_MIIBASE	0x520 /* MII Management base, rest offsets */
#define TSEC_REG_MIIMCFG	0x0   /* MII Management configuration register */
#define TSEC_REG_MIIMCOM	0x4   /* MII Management command register */
#define TSEC_REG_MIIMADD	0x8   /* MII Management address register */
#define TSEC_REG_MIIMCON	0xc   /* MII Management control register */
#define TSEC_REG_MIIMSTAT	0x10  /* MII Management status register */
#define TSEC_REG_MIIMIND	0x14  /* MII Management indicator register */
#define TSEC_REG_IFSTAT		0x53c /* Interface status register */
#define TSEC_REG_MACSTNADDR1	0x540 /* Station address register, part 1 */
#define TSEC_REG_MACSTNADDR2	0x544 /* Station address register, part 2 */

/* TSEC Transmit and Receive Counters */
#define TSEC_REG_MON_TR64	0x680 /* Transmit and receive 64-byte
				       * frame counter register */
#define TSEC_REG_MON_TR127	0x684 /* Transmit and receive 65-127 byte
				       * frame counter register */
#define TSEC_REG_MON_TR255	0x688 /* Transmit and receive 128-255 byte
				       * frame counter register */
#define TSEC_REG_MON_TR511	0x68c /* Transmit and receive 256-511 byte
				       * frame counter register */
#define TSEC_REG_MON_TR1K 	0x690 /* Transmit and receive 512-1023 byte
				       * frame counter register */
#define TSEC_REG_MON_TRMAX	0x694 /* Transmit and receive 1024-1518 byte
				       * frame counter register */
#define TSEC_REG_MON_TRMGV	0x698 /* Transmit and receive 1519-1522 byte
				       * good VLAN frame counter register */

/* TSEC Receive Counters */
#define TSEC_REG_MON_RBYT	0x69c /* Receive byte counter register */
#define TSEC_REG_MON_RPKT	0x6a0 /* Receive packet counter register */
#define TSEC_REG_MON_RFCS	0x6a4 /* Receive FCS error counter register */
#define TSEC_REG_MON_RMCA	0x6a8 /* Receive multicast packet counter
				       * register */
#define TSEC_REG_MON_RBCA	0x6ac /* Receive broadcast packet counter
				       * register */
#define TSEC_REG_MON_RXCF	0x6b0 /* Receive control frame packet counter
				       * register */
#define TSEC_REG_MON_RXPF	0x6b4 /* Receive pause frame packet counter
				       * register */
#define TSEC_REG_MON_RXUO	0x6b8 /* Receive unknown OP code counter
				       * register */
#define TSEC_REG_MON_RALN	0x6bc /* Receive alignment error counter
				       * register */
#define TSEC_REG_MON_RFLR	0x6c0 /* Receive frame length error counter
				       * register */
#define TSEC_REG_MON_RCDE	0x6c4 /* Receive code error counter register */
#define TSEC_REG_MON_RCSE	0x6c8 /* Receive carrier sense error counter
				       * register */
#define TSEC_REG_MON_RUND	0x6cc /* Receive undersize packet counter
				       * register */
#define TSEC_REG_MON_ROVR	0x6d0 /* Receive oversize packet counter
				       * register */
#define TSEC_REG_MON_RFRG	0x6d4 /* Receive fragments counter register */
#define TSEC_REG_MON_RJBR	0x6d8 /* Receive jabber counter register */
#define TSEC_REG_MON_RDRP	0x6dc /* Receive drop counter register */

/* TSEC Transmit Counters */
#define TSEC_REG_MON_TBYT	0x6e0 /* Transmit byte counter register */
#define TSEC_REG_MON_TPKT	0x6e4 /* Transmit packet counter register */
#define TSEC_REG_MON_TMCA	0x6e8 /* Transmit multicast packet counter
				       * register */
#define TSEC_REG_MON_TBCA	0x6ec /* Transmit broadcast packet counter
				       * register */
#define TSEC_REG_MON_TXPF	0x6f0 /* Transmit PAUSE control frame counter
				       * register */
#define TSEC_REG_MON_TDFR	0x6f4 /* Transmit deferral packet counter
				       * register */
#define TSEC_REG_MON_TEDF	0x6f8 /* Transmit excessive deferral packet
				       * counter register */
#define TSEC_REG_MON_TSCL	0x6fc /* Transmit single collision packet counter
				       * register */
#define TSEC_REG_MON_TMCL	0x700 /* Transmit multiple collision packet counter
				       * register */
#define TSEC_REG_MON_TLCL	0x704 /* Transmit late collision packet counter
				       * register */
#define TSEC_REG_MON_TXCL	0x708 /* Transmit excessive collision packet
				       * counter register */
#define TSEC_REG_MON_TNCL	0x70c /* Transmit total collision counter
				       * register */
#define TSEC_REG_MON_TDRP	0x714 /* Transmit drop frame counter register */
#define TSEC_REG_MON_TJBR	0x718 /* Transmit jabber frame counter register */
#define TSEC_REG_MON_TFCS	0x71c /* Transmit FCS error counter register */
#define TSEC_REG_MON_TXCF	0x720 /* Transmit control frame counter register */
#define TSEC_REG_MON_TOVR	0x724 /* Transmit oversize frame counter
				       * register */
#define TSEC_REG_MON_TUND	0x728 /* Transmit undersize frame counter
				       * register */
#define TSEC_REG_MON_TFRG	0x72c /* Transmit fragments frame counter
				       * register */

/* TSEC General Registers */
#define TSEC_REG_MON_CAR1	0x730 /* Carry register one register */
#define TSEC_REG_MON_CAR2	0x734 /* Carry register two register */
#define TSEC_REG_MON_CAM1	0x738 /* Carry register one mask register */
#define TSEC_REG_MON_CAM2	0x73c /* Carry register two mask register */

/* TSEC Hash Function Registers */
#define TSEC_REG_IADDR0		0x800 /* Indivdual address register 0 */
#define TSEC_REG_IADDR1		0x804 /* Indivdual address register 1 */
#define TSEC_REG_IADDR2		0x808 /* Indivdual address register 2 */
#define TSEC_REG_IADDR3		0x80c /* Indivdual address register 3 */
#define TSEC_REG_IADDR4		0x810 /* Indivdual address register 4 */
#define TSEC_REG_IADDR5		0x814 /* Indivdual address register 5 */
#define TSEC_REG_IADDR6		0x818 /* Indivdual address register 6 */
#define TSEC_REG_IADDR7		0x81c /* Indivdual address register 7 */
#define TSEC_REG_GADDR0		0x880 /* Group address register 0 */
#define TSEC_REG_GADDR1		0x884 /* Group address register 1 */
#define TSEC_REG_GADDR2		0x888 /* Group address register 2 */
#define TSEC_REG_GADDR3		0x88c /* Group address register 3 */
#define TSEC_REG_GADDR4		0x890 /* Group address register 4 */
#define TSEC_REG_GADDR5		0x894 /* Group address register 5 */
#define TSEC_REG_GADDR6		0x898 /* Group address register 6 */
#define TSEC_REG_GADDR7		0x89c /* Group address register 7 */
#define	TSEC_REG_IADDR(n)	(TSEC_REG_IADDR0 + (n << 2))
#define	TSEC_REG_GADDR(n)	(TSEC_REG_GADDR0 + (n << 2))

/* TSEC attribute registers */
#define TSEC_REG_ATTR		0xbf8 /* Attributes Register */
#define TSEC_REG_ATTRELI	0xbfc /* Attributes EL & EI register */

/* Size of TSEC registers area */
#define TSEC_IO_SIZE		0x1000

/* reg bits */
#define TSEC_FIFO_PAUSE_CTRL_EN		0x0002

#define TSEC_DMACTRL_TDSEN		0x00000080 /* Tx Data snoop enable */
#define TSEC_DMACTRL_TBDSEN		0x00000040 /* TxBD snoop enable */
#define TSEC_DMACTRL_GRS		0x00000010 /* Graceful receive stop */
#define TSEC_DMACTRL_GTS		0x00000008 /* Graceful transmit stop */
#define DMACTRL_WWR			0x00000002 /* Write with response */
#define DMACTRL_WOP			0x00000001 /* Wait or poll */

#define	TSEC_RCTRL_VLEX			0x00002000 /* Enable automatic VLAN tag
						    * extraction and deletion
						    * from Ethernet frames */
#define	TSEC_RCTRL_IPCSEN		0x00000200 /* IP Checksum verification enable */
#define	TSEC_RCTRL_TUCSEN		0x00000100 /* TCP or UDP Checksum verification enable */
#define	TSEC_RCTRL_PRSDEP		0x000000C0 /* Parser control */
#define	TSEC_RCRTL_PRSFM		0x00000020 /* FIFO-mode parsing */
#define TSEC_RCTRL_BC_REJ		0x00000010 /* Broadcast frame reject */
#define TSEC_RCTRL_PROM			0x00000008 /* Promiscuous mode */
#define TSEC_RCTRL_RSF			0x00000004 /* Receive short frame mode */

#define	TSEC_RCTRL_PRSDEP_PARSER_OFF	0x00000000 /* Parser Disabled */
#define	TSEC_RCTRL_PRSDEP_PARSE_L2	0x00000040 /* Parse L2 */
#define	TSEC_RCTRL_PRSDEP_PARSE_L23	0x00000080 /* Parse L2 and L3 */
#define	TSEC_RCTRL_PRSDEP_PARSE_L234	0x000000C0 /* Parse L2, L3 and L4 */

#define	TSEC_TCTRL_IPCSEN		0x00004000 /* IP header checksum generation enable */
#define	TSEC_TCTRL_TUCSEN		0x00002000 /* TCP/UDP header checksum generation enable */

#define TSEC_TSTAT_THLT			0x80000000 /* Transmit halt */
#define TSEC_RSTAT_QHLT			0x00800000 /* RxBD queue is halted */

#define TSEC_IEVENT_BABR		0x80000000 /* Babbling receive error */
#define TSEC_IEVENT_RXC			0x40000000 /* Receive control interrupt */
#define TSEC_IEVENT_BSY			0x20000000 /* Busy condition interrupt */
#define TSEC_IEVENT_EBERR		0x10000000 /* Ethernet bus error */
#define TSEC_IEVENT_MSRO		0x04000000 /* MSTAT Register Overflow */
#define TSEC_IEVENT_GTSC		0x02000000 /* Graceful transmit stop complete */
#define TSEC_IEVENT_BABT		0x01000000 /* Babbling transmit error */
#define TSEC_IEVENT_TXC			0x00800000 /* Transmit control interrupt */
#define TSEC_IEVENT_TXE			0x00400000 /* Transmit error */
#define TSEC_IEVENT_TXB			0x00200000 /* Transmit buffer */
#define TSEC_IEVENT_TXF			0x00100000 /* Transmit frame interrupt */
#define TSEC_IEVENT_LC			0x00040000 /* Late collision */
#define TSEC_IEVENT_CRL			0x00020000 /* Collision retry limit/excessive
						    * defer abort */
#define TSEC_IEVENT_XFUN		0x00010000 /* Transmit FIFO underrun */
#define TSEC_IEVENT_RXB			0x00008000 /* Receive buffer */
#define TSEC_IEVENT_MMRD		0x00000400 /* MII management read completion */
#define TSEC_IEVENT_MMWR		0x00000200 /* MII management write completion */
#define TSEC_IEVENT_GRSC		0x00000100 /* Graceful receive stop complete */
#define TSEC_IEVENT_RXF			0x00000080 /* Receive frame interrupt */

#define TSEC_IMASK_BREN		0x80000000 /* Babbling receiver interrupt */
#define TSEC_IMASK_RXCEN	0x40000000 /* Receive control interrupt */
#define TSEC_IMASK_BSYEN	0x20000000 /* Busy interrupt */
#define TSEC_IMASK_EBERREN	0x10000000 /* Ethernet controller bus error */
#define TSEC_IMASK_MSROEN	0x04000000 /* MSTAT register overflow interrupt */
#define TSEC_IMASK_GTSCEN	0x02000000 /* Graceful transmit stop complete interrupt */
#define TSEC_IMASK_BTEN		0x01000000 /* Babbling transmitter interrupt */
#define TSEC_IMASK_TXCEN	0x00800000 /* Transmit control interrupt */
#define TSEC_IMASK_TXEEN	0x00400000 /* Transmit error interrupt */
#define TSEC_IMASK_TXBEN	0x00200000 /* Transmit buffer interrupt */
#define TSEC_IMASK_TXFEN	0x00100000 /* Transmit frame interrupt */
#define TSEC_IMASK_LCEN		0x00040000 /* Late collision */
#define TSEC_IMASK_CRLEN	0x00020000 /* Collision retry limit/excessive defer */
#define TSEC_IMASK_XFUNEN	0x00010000 /* Transmit FIFO underrun */
#define TSEC_IMASK_RXBEN	0x00008000 /* Receive buffer interrupt */
#define TSEC_IMASK_MMRD		0x00000400 /* MII management read completion */
#define TSEC_IMASK_MMWR		0x00000200 /* MII management write completion */
#define TSEC_IMASK_GRSCEN	0x00000100 /* Graceful receive stop complete interrupt */
#define TSEC_IMASK_RXFEN	0x00000080 /* Receive frame interrupt */

#define TSEC_ATTR_ELCWT		0x00004000 /* Write extracted data to L2 cache */
#define TSEC_ATTR_BDLWT		0x00000800 /* Write buffer descriptor to L2 cache */
#define TSEC_ATTR_RDSEN		0x00000080 /* Rx data snoop enable */
#define TSEC_ATTR_RBDSEN	0x00000040 /* RxBD snoop enable */

#define TSEC_MACCFG1_SOFT_RESET		0x80000000 /* Soft reset */
#define TSEC_MACCFG1_RESET_RX_MC	0x00080000 /* Reset receive MAC control block */
#define TSEC_MACCFG1_RESET_TX_MC	0x00040000 /* Reset transmit MAC control block */
#define TSEC_MACCFG1_RESET_RX_FUN	0x00020000 /* Reset receive function block */
#define TSEC_MACCFG1_RESET_TX_FUN	0x00010000 /* Reset transmit function block */
#define TSEC_MACCFG1_LOOPBACK		0x00000100 /* Loopback */
#define TSEC_MACCFG1_RX_FLOW		0x00000020 /* Receive flow */
#define TSEC_MACCFG1_TX_FLOW		0x00000010 /* Transmit flow */
#define TSEC_MACCFG1_SYNCD_RX_EN	0x00000008 /* Receive enable synchronized
						    * to the receive stream (Read-only) */
#define TSEC_MACCFG1_RX_EN		0x00000004 /* Receive enable */
#define TSEC_MACCFG1_SYNCD_TX_EN	0x00000002 /* Transmit enable synchronized
						    * to the transmit stream (Read-only) */
#define TSEC_MACCFG1_TX_EN		0x00000001 /* Transmit enable */

#define TSEC_MACCFG2_PRECNT		0x00007000 /* Preamble Length (0x7) */
#define TSEC_MACCFG2_IF			0x00000300 /* Determines the type of interface
						    * to which the MAC is connected */
#define TSEC_MACCFG2_MII		0x00000100 /* Nibble mode (MII) */
#define TSEC_MACCFG2_GMII		0x00000200 /* Byte mode (GMII/TBI) */
#define TSEC_MACCFG2_HUGEFRAME		0x00000020 /* Huge frame enable */
#define TSEC_MACCFG2_LENGTHCHECK	0x00000010 /* Length check */
#define TSEC_MACCFG2_PADCRC		0x00000004 /* Pad and append CRC */
#define TSEC_MACCFG2_CRCEN		0x00000002 /* CRC enable */
#define TSEC_MACCFG2_FULLDUPLEX		0x00000001 /* Full duplex configure */

#define	TSEC_ECNTRL_STEN		0x00001000 /* Statistics enabled */
#define	TSEC_ECNTRL_GMIIM		0x00000040 /* GMII I/F mode */
#define	TSEC_ECNTRL_TBIM		0x00000020 /* Ten-bit I/F mode */
#define	TSEC_ECNTRL_R100M		0x00000008 /* RGMII/RMII 100 mode */
#define	TSEC_ECNTRL_RMM			0x00000004 /* Reduced-pin mode */
#define	TSEC_ECNTRL_SGMIIM		0x00000002 /* Serial GMII mode */

#define TSEC_MIIMCFG_RESETMGMT		0x80000000 /* Reset management */
#define TSEC_MIIMCFG_NOPRE		0x00000010 /* Preamble suppress */
#define TSEC_MIIMCFG_CLKDIV28		0x00000007 /* source clock divided by 28 */
#define TSEC_MIIMCFG_CLKDIV20		0x00000006 /* source clock divided by 20 */
#define TSEC_MIIMCFG_CLKDIV14		0x00000005 /* source clock divided by 14 */
#define TSEC_MIIMCFG_CLKDIV10		0x00000004 /* source clock divided by 10 */
#define TSEC_MIIMCFG_CLKDIV8		0x00000003 /* source clock divided by 8 */
#define TSEC_MIIMCFG_CLKDIV6		0x00000002 /* source clock divided by 6 */
#define TSEC_MIIMCFG_CLKDIV4		0x00000001 /* source clock divided by 4 */

#define TSEC_MIIMIND_NOTVALID		0x00000004 /* Not valid */
#define TSEC_MIIMIND_SCAN		0x00000002 /* Scan in progress */
#define TSEC_MIIMIND_BUSY		0x00000001 /* Busy */

#define TSEC_MIIMCOM_SCANCYCLE		0x00000002 /* Scan cycle */
#define TSEC_MIIMCOM_READCYCLE		0x00000001 /* Read cycle */

/* Transmit Data Buffer Descriptor (TxBD) Field Descriptions */
#define TSEC_TXBD_R		0x8000 /* Ready */
#define TSEC_TXBD_PADCRC	0x4000 /* PAD/CRC */
#define TSEC_TXBD_W		0x2000 /* Wrap */
#define TSEC_TXBD_I		0x1000 /* Interrupt */
#define TSEC_TXBD_L		0x0800 /* Last in frame */
#define TSEC_TXBD_TC		0x0400 /* Tx CRC */
#define TSEC_TXBD_DEF		0x0200 /* Defer indication */
#define TSEC_TXBD_TO1		0x0100 /* Transmit software ownership */
#define TSEC_TXBD_HFE		0x0080 /* Huge frame enable (written by user) */
#define TSEC_TXBD_LC		0x0080 /* Late collision (written by TSEC) */
#define TSEC_TXBD_RL		0x0040 /* Retransmission Limit */
#define TSEC_TXBD_TOE		0x0002 /* TCP/IP Offload Enable */
#define TSEC_TXBD_UN		0x0002 /* Underrun */
#define TSEC_TXBD_TXTRUNC	0x0001 /* TX truncation */

/* Receive Data Buffer Descriptor (RxBD) Field Descriptions */
#define TSEC_RXBD_E		0x8000 /* Empty */
#define TSEC_RXBD_RO1		0x4000 /* Receive software ownership bit */
#define TSEC_RXBD_W		0x2000 /* Wrap */
#define TSEC_RXBD_I		0x1000 /* Interrupt */
#define TSEC_RXBD_L		0x0800 /* Last in frame */
#define TSEC_RXBD_F		0x0400 /* First in frame */
#define TSEC_RXBD_M		0x0100 /* Miss - The frame was received because
					* of promiscuous mode. */
#define TSEC_RXBD_B		0x0080 /* Broadcast */
#define TSEC_RXBD_MC		0x0040 /* Multicast */
#define TSEC_RXBD_LG		0x0020 /* Large - Rx frame length violation */
#define TSEC_RXBD_NO		0x0010 /* Rx non-octet aligned frame */
#define TSEC_RXBD_SH		0x0008 /* Short frame */
#define TSEC_RXBD_CR		0x0004 /* Rx CRC error */
#define TSEC_RXBD_OV		0x0002 /* Overrun */
#define TSEC_RXBD_TR		0x0001 /* Truncation */
#define TSEC_RXBD_ZEROONINIT (TSEC_RXBD_TR | TSEC_RXBD_OV | TSEC_RXBD_CR |  \
		TSEC_RXBD_SH | TSEC_RXBD_NO | TSEC_RXBD_LG | TSEC_RXBD_MC | \
		TSEC_RXBD_B | TSEC_RXBD_M)

#define TSEC_TXBUFFER_ALIGNMENT		64
#define TSEC_RXBUFFER_ALIGNMENT		64

/* Transmit Path Off-Load Frame Control Block flags */
#define TSEC_TX_FCB_VLAN		0x8000 /* VLAN control word valid */
#define TSEC_TX_FCB_L3_IS_IP		0x4000 /* Layer 3 header is an IP header */
#define TSEC_TX_FCB_L3_IS_IP6		0x2000 /* IP header is IP version 6 */
#define TSEC_TX_FCB_L4_IS_TCP_UDP	0x1000 /* Layer 4 header is a TCP or UDP header */
#define TSEC_TX_FCB_L4_IS_UDP		0x0800 /* UDP protocol at layer 4 */
#define TSEC_TX_FCB_CSUM_IP		0x0400 /* Checksum IP header enable */
#define TSEC_TX_FCB_CSUM_TCP_UDP	0x0200 /* Checksum TCP or UDP header enable */
#define TSEC_TX_FCB_FLAG_NO_PH_CSUM	0x0100 /* Disable pseudo-header checksum */
#define TSEC_TX_FCB_FLAG_PTP		0x0001 /* This is a PTP packet */

/* Receive Path Off-Load Frame Control Block flags */
#define	TSEC_RX_FCB_VLAN		0x8000 /* VLAN tag recognized */
#define	TSEC_RX_FCB_IP_FOUND		0x4000 /* IP header found at layer 3 */
#define	TSEC_RX_FCB_IP6_FOUND		0x2000 /* IP version 6 header found at layer 3 */
#define	TSEC_RX_FCB_TCP_UDP_FOUND	0x1000 /* TCP or UDP header found at layer 4 */
#define	TSEC_RX_FCB_IP_CSUM		0x0800 /* IPv4 header checksum checked */
#define	TSEC_RX_FCB_TCP_UDP_CSUM	0x0400 /* TCP or UDP header checksum checked */
#define	TSEC_RX_FCB_IP_CSUM_ERROR	0x0200 /* IPv4 header checksum verification error */
#define	TSEC_RX_FCB_TCP_UDP_CSUM_ERROR	0x0100 /* TCP or UDP header checksum verification error */
#define	TSEC_RX_FCB_PARSE_ERROR		0x000C /* Parse error */
