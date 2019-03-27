/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2001 Wind River Systems
 * Copyright (c) 1997, 1998, 1999, 2000, 2001
 *	Bill Paul <wpaul@bsdi.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */


#define LGE_MODE1		0x00	/* CSR00 */
#define LGE_MODE2		0x04	/* CSR01 */
#define LGE_PPTXBUF_IDX		0x08	/* CSR02 */
#define LGE_PRODID		0x0C	/* CSR03 */
#define LGE_PPTXBUF_ADDR_LO	0x10	/* CSR04 */
#define LGE_PPTXBUF_ADDR_HI	0x14	/* CSR05 */
#define LGE_RSVD0		0x18	/* CSR06 */
#define LGE_PPRXBUF_IDX		0x1C	/* CSR07 */
#define LGE_PPRXBUF_ADDR_LO	0x20	/* CSR08 */
#define LGE_PPRXBUF_ADDR_HI	0x24	/* CSR09 */
#define LGE_EECTL		0x28	/* CSR10 */
#define LGE_CHIPSTS		0x2C	/* CSR11 */
#define LGE_TXDESC_ADDR_LO	0x30	/* CSR12 */
#define LGE_TXDESC_ADDR_HI	0x34	/* CSR13 */
#define LGE_RXDESC_ADDR_LO	0x38	/* CSR14 */
#define LGE_RXDESC_ADDR_HI	0x3C	/* CSR15 */
#define LGE_PPTXCTL		0x40	/* CSR16 */
#define LGE_PPRXCTL		0x44	/* CSR17 */
#define LGE_INTR_PERIOD		0x48	/* CSR18 */
#define LGE_TXFIFO_PKTCNT	0x4C	/* CSR19 */
#define LGE_TXFIFO_LOWAT	0x50	/* CSR20 */
#define LGE_TXFIFO_FREEDWORDS	0x54	/* CSR21 */
#define LGE_TXFIFO_WRITE	0x58	/* CSR22 */
#define LGE_RSVD1		0x5C	/* CSR23 */
#define LGE_RXFIFO_READ		0x60	/* CSR24 */
#define LGE_RSVD2		0x64	/* CSR25 */
#define LGE_RXFIFO_DWORDCNT	0x68	/* CSR26 */
#define LGE_RXFIFO_HIWAT	0x6C	/* CSR27 */
#define LGE_RXFIFO_PKTCNT	0x70	/* CSR28 */
#define LGE_CMD			0x74	/* CSR29 */
#define LGE_IMR			0x78	/* CSR30 */
#define LGE_RSVD3		0x7C	/* CSR31 */
#define LGE_ISR			0x80	/* CSR32 */
#define LGE_RSVD4		0x84	/* CSR33 */
#define LGE_MAR0		0x88	/* CSR34 */
#define LGE_MAR1		0x8C	/* CSR35 */
#define LGE_LEDCFG0		0x90	/* CSR36 */
#define LGE_LEDCFG1		0x84	/* CSR37 */
#define LGE_LEDCFG2		0x98	/* CSR38 */
#define LGE_LEDCFG3		0x9C	/* CSR39 */
#define LGE_RSVD5		0xA0	/* CSR40 */
#define LGE_EEDATA		0xA4	/* CSR41 */
#define LGE_PAR0		0xA8	/* CSR42 */
#define LGE_PAR1		0xAC	/* CSR43 */
#define LGE_GMIICTL		0xB0	/* CSR44 */
#define LGE_GMIIMODE		0xB4	/* CSR45 */
#define LGE_STATSIDX		0xB8	/* CSR46 */
#define LGE_STATSVAL		0xBC	/* CSR47 */
#define LGE_VLANCTL		0xC0	/* CSR48 */
#define LGE_RSVD6		0xC4	/* CSR49 */
#define LGE_RSVD7		0xC8	/* CSR50 */
#define LGE_CMDSTS		0xCC	/* CSR51 */
#define LGE_FLOWCTL_WAT		0xD0	/* CSR52 */
#define LGE_RSVD8		0xD4	/* CSR53 */
#define LGE_RSVD9		0xD8	/* CSR54 */
#define LGE_RSVD10		0xDC	/* CSR55 */
#define LGE_RSVD11		0xE0	/* CSR56 */
#define LGE_RSVD12		0xE4	/* CSR57 */
#define LGE_TIMER0_CNT		0xE8	/* CSR58 */
#define LGE_TIMER0_INT		0xEC	/* CSR59 */
#define LGE_TIMER1_CNT		0xF0	/* CSR60 */
#define LGE_TIMER1_INT		0xF4	/* CSR61 */
#define LGE_DBG_CMD		0xF8	/* CSR62 */
#define LGE_DBG_DATA		0xFC	/* CSR63 */


/* Mode register 1 */
#define LGE_MODE1_SETRST_CTL0	0x00000001
#define LGE_MODE1_SOFTRST	0x00000002
#define LGE_MODE1_DEBTOD	0x00000004	/* Not documented? */
#define LGE_MODE1_TX_FLOWCTL	0x00000008	/* Not documented? */
#define LGE_MODE1_RXTXRIO	0x00000010
#define LGE_MODE1_GMIIPOLL	0x00000020
#define LGE_MODE1_TXPAD		0x00000040
#define LGE_MODE1_RMVPAD	0x00000080	/* Not documented? */
#define LGE_MODE1_SETRST_CTL1	0x00000100
#define LGE_MODE1_TX_ENB	0x00000200
#define LGE_MODE1_RX_ENB	0x00000400
#define LGE_MODE1_RX_MCAST	0x00000800
#define LGE_MODE1_RX_BCAST	0x00001000
#define LGE_MODE1_RX_PROMISC	0x00002000
#define LGE_MODE1_RX_UCAST	0x00004000
#define LGE_MODE1_RX_GIANTS	0x00008000
#define LGE_MODE1_SETRST_CTL2	0x00010000
#define LGE_MODE1_RX_CRC	0x00020000
#define LGE_MODE1_RX_ERRPKTS	0x00040000
#define LGE_MODE1_TX_CRC	0x00080000
#define LGE_MODE1_DEMDEN	0x00100000	/* Not documented? */
#define LGE_MODE1_MPACK_ENB	0x00200000
#define LGE_MODE1_MPACK_BCAST	0x00400000
#define LGE_MODE1_RX_FLOWCTL	0x00800000
#define LGE_MODE1_SETRST_CTL3	0x01000000
#define LGE_MODE1_VLAN_RX	0x02000000
#define LGE_MODE1_VLAN_TX	0x04000000
#define LGE_MODE1_VLAN_STRIP	0x08000000
#define LGE_MODE1_VLAN_INSERT	0x10000000
#define LGE_MODE1_GPIO_CTL0	0x20000000
#define LGE_MODE1_GPIO_CTL1	0x40000000
#define LGE_MODE1_RX_LENCHK	0x80000000


/* Mode register 2 */
#define LGE_MODE2_LOOPBACK	0x000000E0
#define LGE_MODE2_RX_IPCSUM	0x00001000
#define LGE_MODE2_RX_TCPCSUM	0x00002000
#define LGE_MODE2_RX_UDPCSUM	0x00004000
#define LGE_MODE2_RX_ERRCSUM	0x00008000


/* EEPROM register */
#define LGE_EECTL_HAVE_EEPROM	0x00000001
#define LGE_EECTL_CMD_READ	0x00000002
#define LGE_EECTL_CMD_WRITE	0x00000004
#define LGE_EECTL_CSUMERR	0x00000010
#define LGE_EECTL_MULTIACCESS	0x00000020
#define LGE_EECTL_SINGLEACCESS	0x00000040
#define LGE_EECTL_ADDR		0x00001F00
#define LGE_EECTL_ROM_TIMING	0x000F0000
#define LGE_EECTL_HAVE_FLASH	0x00100000
#define LGE_EECTL_WRITEFLASH	0x00200000

#define LGE_EE_NODEADDR_0	0x12
#define LGE_EE_NODEADDR_1	0x13
#define LGE_EE_NODEADDR_2	0x10


/* Chip status register */
#define LGE_CHIPSTS_HAVETXSPC	0x00000001 /* have room in TX FIFO for pkt */
#define LGE_CHIPSTS_HAVERXPKT	0x00000002 /* RX FIFO holds complete pkt */
#define LGE_CHIPSTS_FLOWCTL_STS	0x00000004
#define LGE_CHIPSTS_GPIO_STS0	0x00000008
#define LGE_CHIPSTS_GPIO_STS1	0x00000010
#define LGE_CHIPSTS_TXIDLE	0x00000020
#define LGE_CHIPSTS_RXIDLE	0x00000040


/* TX PacketPropulsion control register */
#define LGE_PPTXCTL_BUFLEN	0x0000FFFF
#define LGE_PPTXCTL_BUFID	0x003F0000
#define LGE_PPTXCTL_WANTINTR	0x01000000


/* RX PacketPropulsion control register */
#define LGE_PPRXCTL_BUFLEN	0x0000FFFF
#define LGE_PPRXCTL_BUFID	0x003F0000
#define LGE_PPRXCTL_WANTINTR	0x10000000


/* Command register */
#define LGE_CMD_SETRST_CTL0	0x00000001
#define LGE_CMD_STARTTX		0x00000002
#define LGE_CMD_SKIP_RXPKT	0x00000004
#define LGE_CMD_DEL_INTREQ	0x00000008
#define LGE_CMD_PER_INTREQ	0x00000010
#define LGE_CMD_TIMER0		0x00000020
#define LGE_CMD_TIMER1		0x00000040


/* Interrupt mask register */
#define LGE_IMR_SETRST_CTL0	0x00000001
#define LGE_IMR_TXCMDFIFO_EMPTY	0x00000002
#define LGE_IMR_TXFIFO_WAT	0x00000004
#define LGE_IMR_TXDMA_DONE	0x00000008
#define LGE_IMR_DELAYEDINTR	0x00000040
#define LGE_IMR_INTR_ENB	0x00000080
#define LGE_IMR_SETRST_CTL1	0x00000100
#define LGE_IMR_RXCMDFIFO_EMPTY	0x00000200
#define LGE_IMR_RXFIFO_WAT	0x00000400
#define LGE_IMR_RX_DONE		0x00000800
#define LGE_IMR_RXDMA_DONE	0x00001000
#define LGE_IMR_PHY_INTR	0x00002000
#define LGE_IMR_MAGICPKT	0x00004000
#define LGE_IMR_SETRST_CTL2	0x00010000
#define LGE_IMR_GPIO0		0x00020000
#define LGE_IMR_GPIO1		0x00040000
#define LGE_IMR_TIMER0		0x00080000
#define LGE_IMR_TIMER1		0x00100000


#define LGE_INTRS	\
	(LGE_IMR_TXCMDFIFO_EMPTY|LGE_IMR_TXDMA_DONE|LGE_IMR_RX_DONE| \
	 LGE_IMR_RXCMDFIFO_EMPTY|LGE_IMR_RXDMA_DONE|LGE_IMR_PHY_INTR)


/* Interrupt status register */
#define LGE_ISR_TXCMDFIFO_EMPTY	0x00000002
#define LGE_ISR_TXFIFO_WAT	0x00000004
#define LGE_ISR_TXDMA_DONE	0x00000008
#define LGE_ISR_DELAYEDINTR	0x00000040
#define LGE_ISR_INTR_ENB	0x00000080
#define LGE_ISR_RXCMDFIFO_EMPTY	0x00000200
#define LGE_ISR_RXFIFO_WAT	0x00000400
#define LGE_ISR_RX_DONE		0x00000800
#define LGE_ISR_RXDMA_DONE	0x00001000
#define LGE_ISR_PHY_INTR	0x00002000
#define LGE_ISR_MAGICPKT	0x00004000
#define LGE_ISR_GPIO0		0x00020000
#define LGE_ISR_GPIO1		0x00040000
#define LGE_ISR_TIMER0		0x00080000
#define LGE_ISR_TIMER1		0x00100000
#define LGE_ISR_RXDMADONE_CNT	0xFF000000
#define LGE_RX_DMACNT(x)	((x & LGE_ISR_RXDMADONE_CNT) >> 24)

/* LED0 config register */
#define LGE_LED0CFG_ENABLE	0x00000002
#define LGE_LED0CFG_INPUT_POL	0x00000004
#define LGE_LED0CFG_PULSE_EXP	0x00000008
#define LGE_LED0CFG_10MBPS	0x00000010
#define LGE_LED0CFG_100MBPS	0x00000100
#define LGE_LED0CFG_1000MBPS	0x00000200
#define LGE_LED0CFG_FDX		0x00000400
#define LGE_LED0CFG_ANEG	0x00000800
#define LGE_LED0CFG_LINKSTS	0x00001000
#define LGE_LED0CFG_RXMATCH	0x00002000
#define LGE_LED0CFG_TX		0x00004000
#define LGE_LED0CFG_RX		0x00008000
#define LGE_LED0CFG_JABBER	0x00010000
#define LGE_LED0CFG_COLLISION	0x00020000
#define LGE_LED0CFG_CARRIER	0x00040000
#define LGE_LED0CFG_LEDOUT	0x10000000


/* LED1 config register */
#define LGE_LED1CFG_ENABLE	0x00000002
#define LGE_LED1CFG_INPUT_POL	0x00000004
#define LGE_LED1CFG_PULSE_EXP	0x00000008
#define LGE_LED1CFG_10MBPS	0x00000010
#define LGE_LED1CFG_100MBPS	0x00000100
#define LGE_LED1CFG_1000MBPS	0x00000200
#define LGE_LED1CFG_FDX		0x00000400
#define LGE_LED1CFG_ANEG	0x00000800
#define LGE_LED1CFG_LINKSTS	0x00001000
#define LGE_LED1CFG_RXMATCH	0x00002000
#define LGE_LED1CFG_TX		0x00004000
#define LGE_LED1CFG_RX		0x00008000
#define LGE_LED1CFG_JABBER	0x00010000
#define LGE_LED1CFG_COLLISION	0x00020000
#define LGE_LED1CFG_CARRIER	0x00040000
#define LGE_LED1CFG_LEDOUT	0x10000000


/* LED2 config register */
#define LGE_LED2CFG_ENABLE	0x00000002
#define LGE_LED2CFG_INPUT_POL	0x00000004
#define LGE_LED2CFG_PULSE_EXP	0x00000008
#define LGE_LED2CFG_10MBPS	0x00000010
#define LGE_LED2CFG_100MBPS	0x00000100
#define LGE_LED2CFG_1000MBPS	0x00000200
#define LGE_LED2CFG_FDX		0x00000400
#define LGE_LED2CFG_ANEG	0x00000800
#define LGE_LED2CFG_LINKSTS	0x00001000
#define LGE_LED2CFG_RXMATCH	0x00002000
#define LGE_LED2CFG_TX		0x00004000
#define LGE_LED2CFG_RX		0x00008000
#define LGE_LED2CFG_JABBER	0x00010000
#define LGE_LED2CFG_COLLISION	0x00020000
#define LGE_LED2CFG_CARRIER	0x00040000
#define LGE_LED2CFG_LEDOUT	0x10000000


/* GMII PHY access register */
#define LGE_GMIICTL_PHYREG	0x0000001F
#define LGE_GMIICTL_CMD		0x00000080
#define LGE_GMIICTL_PHYADDR	0x00001F00
#define LGE_GMIICTL_CMDBUSY	0x00008000
#define LGE_GMIICTL_DATA	0xFFFF0000

#define LGE_GMIICMD_READ	0x00000000
#define LGE_GMIICMD_WRITE	0x00000080

/* GMII PHY mode register */
#define LGE_GMIIMODE_SPEED	0x00000003
#define LGE_GMIIMODE_FDX	0x00000004
#define LGE_GMIIMODE_PROTSEL	0x00000100 /* 0 == GMII, 1 == TBI */
#define LGE_GMIIMODE_PCSENH	0x00000200

#define LGE_SPEED_10		0x00000000
#define LGE_SPEED_100		0x00000001
#define LGE_SPEED_1000		0x00000002


/* VLAN tag control register */
#define LGE_VLANCTL_VLID	0x00000FFF
#define LGE_VLANCTL_USERPRIO	0x0000E000
#define LGE_VLANCTL_TCI_IDX	0x000D0000
#define LGE_VLANCTL_TBLCMD	0x00200000


/* Command status register */
#define LGE_CMDSTS_TXDMADONE	0x000000FF
#define LGE_CMDSTS_RXDMADONE	0x0000FF00
#define LGE_CMDSTS_TXCMDFREE	0x003F0000
#define LGE_CMDSTS_RXCMDFREE	0x3F000000

#define LGE_TXDMADONE_8BIT	LGE_CMDSTS
#define LGE_RXDMADONE_8BIT	(LGE_CMDSTS + 1)
#define LGE_TXCMDFREE_8BIT	(LGE_CMDSTS + 2)
#define LGE_RXCMDFREE_8BIT	(LGE_CMDSTS + 3)

#define LGE_MAXCMDS		31

/* Index for statistics counters. */
#define LGE_STATS_TX_PKTS_OK		0x00
#define LGE_STATS_SINGLE_COLL_PKTS	0x01
#define LGE_STATS_MULTI_COLL_PKTS	0x02
#define LGE_STATS_RX_PKTS_OK		0x03
#define LGE_STATS_FCS_ERRS		0x04
#define LGE_STATS_ALIGN_ERRS		0x05
#define LGE_STATS_DROPPED_PKTS		0x06
#define LGE_STATS_RX_ERR_PKTS		0x07
#define LGE_STATS_TX_ERR_PKTS		0x08
#define LGE_STATS_LATE_COLLS		0x09
#define LGE_STATS_RX_RUNTS		0x0A
#define LGE_STATS_RX_GIANTS		0x0B
#define LGE_STATS_VLAN_PKTS_ACCEPT	0x0C
#define LGE_STATS_VLAN_PKTS_REJECT	0x0D
#define LGE_STATS_IP_CSUM_ERR		0x0E
#define LGE_STATS_UDP_CSUM_ERR		0x0F
#define LGE_STATS_RANGELEN_ERRS		0x10
#define LGE_STATS_TCP_CSUM_ERR		0x11
#define LGE_STATS_RSVD0			0x12
#define LGE_STATS_TX_EXCESS_COLLS	0x13
#define LGE_STATS_RX_UCASTS		0x14
#define LGE_STATS_RX_MCASTS		0x15
#define LGE_STATS_RX_BCASTS		0x16
#define LGE_STATS_RX_PAUSE_PKTS		0x17
#define LGE_STATS_TX_PAUSE_PKTS		0x18
#define LGE_STATS_TX_PKTS_DEFERRED	0x19
#define LGE_STATS_TX_EXCESS_DEFER	0x1A
#define LGE_STATS_CARRIER_SENSE_ERR	0x1B


/*
 * RX and TX DMA descriptor structures for scatter/gather.
 * Each descriptor can have up to 31 fragments in it, however for
 * RX we only need one fragment, and for transmit we only allocate
 * 10 in order to reduce the amount of space we need for the
 * descriptor lists.
 * Note: descriptor structures must be 64-bit aligned.
 */

struct lge_rx_desc {
	/* Hardware descriptor section */
	u_int32_t		lge_ctl;
	u_int32_t		lge_sts;
	u_int32_t		lge_fragptr_lo;
	u_int32_t		lge_fragptr_hi;
	u_int16_t		lge_fraglen;
	u_int16_t		lge_rsvd0;
	u_int32_t		lge_rsvd1;
	/* Driver software section */
	union {
		struct mbuf		*lge_mbuf;
		u_int64_t		lge_dummy;
	} lge_u;
};

struct lge_frag {
	u_int32_t		lge_rsvd0;
	u_int32_t		lge_fragptr_lo;
	u_int32_t		lge_fragptr_hi;
	u_int16_t		lge_fraglen;
	u_int16_t		lge_rsvd1;
};

struct lge_tx_desc {
	/* Hardware descriptor section */
	u_int32_t		lge_ctl;
	struct lge_frag		lge_frags[10];
	u_int32_t		lge_rsvd0;
	union {
		struct mbuf		*lge_mbuf;
		u_int64_t		lge_dummy;
	} lge_u;
};

#define lge_mbuf	lge_u.lge_mbuf

#define LGE_RXCTL_BUFLEN	0x0000FFFF
#define LGE_RXCTL_FRAGCNT	0x001F0000
#define LGE_RXCTL_LENERR	0x00400000
#define LGE_RXCTL_UCAST		0x00800000
#define LGR_RXCTL_BCAST		0x01000000
#define LGE_RXCTL_MCAST		0x02000000
#define LGE_RXCTL_GIANT		0x04000000
#define LGE_RXCTL_OFLOW		0x08000000
#define LGE_RXCTL_CRCERR	0x10000000
#define LGE_RXCTL_RUNT		0x20000000
#define LGE_RXCTL_ALGNERR	0x40000000
#define LGE_RXCTL_WANTINTR	0x80000000

#define LGE_RXCTL_ERRMASK	\
	(LGE_RXCTL_LENERR|LGE_RXCTL_OFLOW|	\
	 LGE_RXCTL_CRCERR|LGE_RXCTL_RUNT|	\
	 LGE_RXCTL_ALGNERR)

#define LGE_RXSTS_VLTBIDX	0x0000000F
#define LGE_RXSTS_VLTBLHIT	0x00000010
#define LGE_RXSTS_IPCSUMERR	0x00000100
#define LGE_RXSTS_TCPCSUMERR	0x00000200
#define LGE_RXSTS_UDPCSUMERR	0x00000400
#define LGE_RXSTS_ISIP		0x00000800
#define LGE_RXSTS_ISTCP		0x00001000
#define LGE_RXSTS_ISUDP		0x00002000

#define LGE_TXCTL_BUFLEN	0x0000FFFF
#define LGE_TXCTL_FRAGCNT	0x001F0000
#define LGE_TXCTL_VLTBIDX	0x0F000000
#define LGE_TXCTL_VLIS		0x10000000
#define LGE_TXCTL_WANTINTR	0x80000000

#define LGE_INC(x, y)		(x) = (x + 1) % y
#define LGE_FRAGCNT_1		(1<<16)
#define LGE_FRAGCNT_10		(10<<16)  
#define LGE_FRAGCNT(x)		(x<<16)
#define LGE_RXBYTES(x)		(x->lge_ctl & 0xFFFF)
#define LGE_RXTAIL(x)		\
	(x->lge_ldata->lge_rx_list[x->lge_cdata.lge_rx_prod])

#define LGE_RX_LIST_CNT		64
#define LGE_TX_LIST_CNT		128

struct lge_list_data {
	struct lge_rx_desc	lge_rx_list[LGE_RX_LIST_CNT];
	struct lge_tx_desc	lge_tx_list[LGE_TX_LIST_CNT];
};


/*
 * Level 1 PCI vendor ID.
 */
#define LGE_VENDORID		0x1394

/*
 * LXT 1001 PCI device IDs
 */
#define LGE_DEVICEID		0x0001

struct lge_type {
	u_int16_t		lge_vid;
	u_int16_t		lge_did;
	const char		*lge_name;
};

#define LGE_JUMBO_FRAMELEN	9018
#define LGE_JUMBO_MTU		(LGE_JUMBO_FRAMELEN-ETHER_HDR_LEN-ETHER_CRC_LEN)
#define LGE_JSLOTS		384

#define LGE_JRAWLEN (LGE_JUMBO_FRAMELEN + ETHER_ALIGN)
#define LGE_JLEN (LGE_JRAWLEN + (sizeof(u_int64_t) - \
	(LGE_JRAWLEN % sizeof(u_int64_t))))
#define LGE_JPAGESZ PAGE_SIZE
#define LGE_RESID (LGE_JPAGESZ - (LGE_JLEN * LGE_JSLOTS) % LGE_JPAGESZ)
#define LGE_JMEM ((LGE_JLEN * LGE_JSLOTS) + LGE_RESID)

struct lge_jpool_entry {
	int				slot;
	SLIST_ENTRY(lge_jpool_entry)	jpool_entries;
};

struct lge_ring_data {
	int			lge_rx_prod;
	int			lge_rx_cons;
	int			lge_tx_prod;
	int			lge_tx_cons;
	/* Stick the jumbo mem management stuff here too. */
	caddr_t			lge_jslots[LGE_JSLOTS];
	void			*lge_jumbo_buf;
};

struct lge_softc {
	struct ifnet		*lge_ifp;
	device_t		lge_dev;
	bus_space_handle_t	lge_bhandle;
	bus_space_tag_t		lge_btag;
	struct resource		*lge_res;
	struct resource		*lge_irq;
	void			*lge_intrhand;
	device_t		lge_miibus;
	u_int8_t		lge_type;
	u_int8_t		lge_link;
	u_int8_t		lge_pcs;
	int			lge_if_flags;
	int			lge_timer;
	struct lge_list_data	*lge_ldata;
	struct lge_ring_data	lge_cdata;
	struct callout		lge_stat_callout;
	struct mtx		lge_mtx;
	SLIST_HEAD(__lge_jfreehead, lge_jpool_entry)	lge_jfree_listhead;
	SLIST_HEAD(__lge_jinusehead, lge_jpool_entry)	lge_jinuse_listhead;
};

/*
 * register space access macros
 */
#define CSR_WRITE_4(sc, reg, val)	\
	bus_space_write_4(sc->lge_btag, sc->lge_bhandle, reg, val)

#define CSR_READ_2(sc, reg)		\
	bus_space_read_2(sc->lge_btag, sc->lge_bhandle, reg)

#define CSR_WRITE_2(sc, reg, val)	\
	bus_space_write_2(sc->lge_btag, sc->lge_bhandle, reg, val)

#define CSR_READ_4(sc, reg)		\
	bus_space_read_4(sc->lge_btag, sc->lge_bhandle, reg)

#define CSR_WRITE_1(sc, reg, val)	\
	bus_space_write_1(sc->lge_btag, sc->lge_bhandle, reg, val)

#define CSR_READ_1(sc, reg)		\
	bus_space_read_1(sc->lge_btag, sc->lge_bhandle, reg)

#define	LGE_LOCK(sc)		mtx_lock(&(sc)->lge_mtx)
#define	LGE_UNLOCK(sc)		mtx_unlock(&(sc)->lge_mtx)
#define	LGE_LOCK_ASSERT(sc)	mtx_assert(&(sc)->lge_mtx, MA_OWNED)

#define LGE_TIMEOUT		1000
#define LGE_RXLEN		1536
#define LGE_MIN_FRAMELEN	60

/*
 * PCI low memory base and low I/O base register, and
 * other PCI registers.
 */

#define LGE_PCI_VENDOR_ID	0x00
#define LGE_PCI_DEVICE_ID	0x02
#define LGE_PCI_COMMAND		0x04
#define LGE_PCI_STATUS		0x06
#define LGE_PCI_REVID		0x08
#define LGE_PCI_CLASSCODE	0x09
#define LGE_PCI_CACHELEN	0x0C
#define LGE_PCI_LATENCY_TIMER	0x0D
#define LGE_PCI_HEADER_TYPE	0x0E
#define LGE_PCI_LOIO		0x10
#define LGE_PCI_LOMEM		0x14
#define LGE_PCI_BIOSROM		0x30
#define LGE_PCI_INTLINE		0x3C
#define LGE_PCI_INTPIN		0x3D
#define LGE_PCI_MINGNT		0x3E
#define LGE_PCI_MINLAT		0x0F
#define LGE_PCI_RESETOPT	0x48
#define LGE_PCI_EEPROM_DATA	0x4C

/* power management registers */
#define LGE_PCI_CAPID		0x50 /* 8 bits */
#define LGE_PCI_NEXTPTR		0x51 /* 8 bits */
#define LGE_PCI_PWRMGMTCAP	0x52 /* 16 bits */
#define LGE_PCI_PWRMGMTCTRL	0x54 /* 16 bits */

#define LGE_PSTATE_MASK		0x0003
#define LGE_PSTATE_D0		0x0000
#define LGE_PSTATE_D1		0x0001
#define LGE_PSTATE_D2		0x0002
#define LGE_PSTATE_D3		0x0003
#define LGE_PME_EN		0x0010
#define LGE_PME_STATUS		0x8000
