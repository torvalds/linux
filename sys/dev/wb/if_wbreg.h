/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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

/*
 * Winbond register definitions.
 */

#define WB_BUSCTL		0x00	/* bus control */
#define WB_TXSTART		0x04	/* tx start demand */
#define WB_RXSTART		0x08	/* rx start demand */
#define WB_RXADDR		0x0C	/* rx descriptor list start addr */
#define WB_TXADDR		0x10	/* tx descriptor list start addr */
#define WB_ISR			0x14	/* interrupt status register */
#define WB_NETCFG		0x18	/* network config register */
#define WB_IMR			0x1C	/* interrupt mask */
#define WB_FRAMESDISCARDED	0x20	/* # of discarded frames */
#define WB_SIO			0x24	/* MII and ROM/EEPROM access */
#define WB_BOOTROMADDR		0x28
#define WB_TIMER		0x2C	/* general timer */
#define WB_CURRXCTL		0x30	/* current RX descriptor */
#define WB_CURRXBUF		0x34	/* current RX buffer */
#define WB_MAR0			0x38	/* multicast filter 0 */
#define WB_MAR1			0x3C	/* multicast filter 1 */
#define WB_NODE0		0x40	/* station address 0 */
#define WB_NODE1		0x44	/* station address 1 */
#define WB_BOOTROMSIZE		0x48	/* boot ROM size */
#define WB_CURTXCTL		0x4C	/* current TX descriptor */
#define WB_CURTXBUF		0x50	/* current TX buffer */

/*
 * Bus control bits.
 */
#define WB_BUSCTL_RESET		0x00000001
#define WB_BUSCTL_ARBITRATION	0x00000002
#define WB_BUSCTL_SKIPLEN	0x0000007C
#define WB_BUSCTL_BUF_BIGENDIAN	0x00000080
#define WB_BUSCTL_BURSTLEN	0x00003F00
#define WB_BUSCTL_CACHEALIGN	0x0000C000
#define WB_BUSCTL_DES_BIGENDIAN	0x00100000
#define WB_BUSCTL_WAIT		0x00200000
#define WB_BUSCTL_MUSTBEONE	0x00400000

#define WB_SKIPLEN_1LONG	0x00000004
#define WB_SKIPLEN_2LONG	0x00000008
#define WB_SKIPLEN_3LONG	0x00000010
#define WB_SKIPLEN_4LONG	0x00000020
#define WB_SKIPLEN_5LONG	0x00000040

#define WB_CACHEALIGN_NONE	0x00000000
#define WB_CACHEALIGN_8LONG	0x00004000
#define WB_CACHEALIGN_16LONG	0x00008000
#define WB_CACHEALIGN_32LONG	0x0000C000

#define WB_BURSTLEN_USECA	0x00000000
#define WB_BURSTLEN_1LONG	0x00000100
#define WB_BURSTLEN_2LONG	0x00000200
#define WB_BURSTLEN_4LONG	0x00000400
#define WB_BURSTLEN_8LONG	0x00000800
#define WB_BURSTLEN_16LONG	0x00001000
#define WB_BURSTLEN_32LONG	0x00002000

#define WB_BUSCTL_CONFIG	(WB_CACHEALIGN_8LONG|WB_SKIPLEN_3LONG| \
					WB_BURSTLEN_8LONG)

/*
 * Interrupt status bits.
 */
#define WB_ISR_TX_OK		0x00000001
#define WB_ISR_TX_IDLE		0x00000002
#define WB_ISR_TX_NOBUF		0x00000004
#define WB_ISR_RX_EARLY		0x00000008
#define WB_ISR_RX_ERR		0x00000010
#define WB_ISR_TX_UNDERRUN	0x00000020
#define WB_ISR_RX_OK		0x00000040
#define WB_ISR_RX_NOBUF		0x00000080
#define WB_ISR_RX_IDLE		0x00000100
#define WB_ISR_TX_EARLY		0x00000400
#define WB_ISR_TIMER_EXPIRED	0x00000800
#define WB_ISR_BUS_ERR		0x00002000
#define WB_ISR_ABNORMAL		0x00008000
#define WB_ISR_NORMAL		0x00010000
#define WB_ISR_RX_STATE		0x000E0000
#define WB_ISR_TX_STATE		0x00700000
#define WB_ISR_BUSERRTYPE	0x03800000

/*
 * The RX_STATE and TX_STATE fields are not described anywhere in the
 * Winbond datasheet, however it appears that the Winbond chip is an
 * attempt at a DEC 'tulip' clone, hence the ISR register is identical
 * to that of the tulip chip and we can steal the bit definitions from
 * the tulip documentation.
 */
#define WB_RXSTATE_STOPPED	0x00000000	/* 000 - Stopped */
#define WB_RXSTATE_FETCH	0x00020000	/* 001 - Fetching descriptor */
#define WB_RXSTATE_ENDCHECK	0x00040000	/* 010 - check for rx end */
#define WB_RXSTATE_WAIT		0x00060000	/* 011 - waiting for packet */
#define WB_RXSTATE_SUSPEND	0x00080000	/* 100 - suspend rx */
#define WB_RXSTATE_CLOSE	0x000A0000	/* 101 - close tx desc */
#define WB_RXSTATE_FLUSH	0x000C0000	/* 110 - flush from FIFO */
#define WB_RXSTATE_DEQUEUE	0x000E0000	/* 111 - dequeue from FIFO */

#define WB_TXSTATE_RESET	0x00000000	/* 000 - reset */
#define WB_TXSTATE_FETCH	0x00100000	/* 001 - fetching descriptor */
#define WB_TXSTATE_WAITEND	0x00200000	/* 010 - wait for tx end */
#define WB_TXSTATE_READING	0x00300000	/* 011 - read and enqueue */
#define WB_TXSTATE_RSVD		0x00400000	/* 100 - reserved */
#define WB_TXSTATE_SETUP	0x00500000	/* 101 - setup packet */
#define WB_TXSTATE_SUSPEND	0x00600000	/* 110 - suspend tx */
#define WB_TXSTATE_CLOSE	0x00700000	/* 111 - close tx desc */

/*
 * Network config bits.
 */
#define WB_NETCFG_RX_ON		0x00000002
#define WB_NETCFG_RX_ALLPHYS	0x00000008
#define WB_NETCFG_RX_MULTI	0x00000010
#define WB_NETCFG_RX_BROAD	0x00000020
#define WB_NETCFG_RX_RUNT	0x00000040
#define WB_NETCFG_RX_ERR	0x00000080
#define WB_NETCFG_FULLDUPLEX	0x00000200
#define WB_NETCFG_LOOPBACK	0x00000C00
#define WB_NETCFG_TX_ON		0x00002000
#define WB_NETCFG_TX_THRESH	0x001FC000
#define WB_NETCFG_RX_EARLYTHRSH	0x1FE00000
#define WB_NETCFG_100MBPS	0x20000000
#define WB_NETCFG_TX_EARLY_ON	0x40000000
#define WB_NETCFG_RX_EARLY_ON	0x80000000

/*
 * The tx threshold can be adjusted in increments of 32 bytes.
 */
#define WB_TXTHRESH(x)		((x >> 5) << 14)
#define WB_TXTHRESH_CHUNK	32
#define WB_TXTHRESH_INIT	0 /*72*/
 
/*
 * Interrupt mask bits.
 */
#define WB_IMR_TX_OK		0x00000001
#define WB_IMR_TX_IDLE		0x00000002
#define WB_IMR_TX_NOBUF		0x00000004
#define WB_IMR_RX_EARLY		0x00000008
#define WB_IMR_RX_ERR		0x00000010
#define WB_IMR_TX_UNDERRUN	0x00000020
#define WB_IMR_RX_OK		0x00000040
#define WB_IMR_RX_NOBUF		0x00000080
#define WB_IMR_RX_IDLE		0x00000100
#define WB_IMR_TX_EARLY		0x00000400
#define WB_IMR_TIMER_EXPIRED	0x00000800
#define WB_IMR_BUS_ERR		0x00002000
#define WB_IMR_ABNORMAL		0x00008000
#define WB_IMR_NORMAL		0x00010000

#define WB_INTRS	\
	(WB_IMR_RX_OK|WB_IMR_TX_OK|WB_IMR_RX_NOBUF|WB_IMR_RX_ERR|	\
	WB_IMR_TX_NOBUF|WB_IMR_TX_UNDERRUN|WB_IMR_BUS_ERR|		\
	WB_IMR_ABNORMAL|WB_IMR_NORMAL|WB_IMR_TX_EARLY)
/*
 * Serial I/O (EEPROM/ROM) bits.
 */
#define WB_SIO_EE_CS		0x00000001	/* EEPROM chip select */
#define WB_SIO_EE_CLK		0x00000002	/* EEPROM clock */
#define WB_SIO_EE_DATAIN	0x00000004	/* EEPROM data output */
#define WB_SIO_EE_DATAOUT	0x00000008	/* EEPROM data input */
#define WB_SIO_ROMDATA4		0x00000010
#define WB_SIO_ROMDATA5		0x00000020
#define WB_SIO_ROMDATA6		0x00000040
#define WB_SIO_ROMDATA7		0x00000080
#define WB_SIO_ROMCTL_WRITE	0x00000200
#define WB_SIO_ROMCTL_READ	0x00000400
#define WB_SIO_EESEL		0x00000800
#define WB_SIO_MII_CLK		0x00010000	/* MDIO clock */
#define WB_SIO_MII_DATAIN	0x00020000	/* MDIO data out */
#define WB_SIO_MII_DIR		0x00040000	/* MDIO dir */
#define WB_SIO_MII_DATAOUT	0x00080000	/* MDIO data in */

#define WB_EECMD_WRITE		0x140
#define WB_EECMD_READ		0x180
#define WB_EECMD_ERASE		0x1c0

/*
 * Winbond TX/RX descriptor structure.
 */

struct wb_desc {
	u_int32_t		wb_status;
	u_int32_t		wb_ctl;
	u_int32_t		wb_ptr1;
	u_int32_t		wb_ptr2;
};

#define wb_data		wb_ptr1
#define wb_next		wb_ptr2

#define WB_RXSTAT_CRCERR	0x00000002
#define WB_RXSTAT_DRIBBLE	0x00000004
#define WB_RXSTAT_MIIERR	0x00000008
#define WB_RXSTAT_LATEEVENT	0x00000040
#define WB_RXSTAT_GIANT		0x00000080
#define WB_RXSTAT_LASTFRAG	0x00000100
#define WB_RXSTAT_FIRSTFRAG	0x00000200
#define WB_RXSTAT_MULTICAST	0x00000400
#define WB_RXSTAT_RUNT		0x00000800
#define WB_RXSTAT_RXTYPE	0x00003000
#define WB_RXSTAT_RXERR		0x00008000
#define WB_RXSTAT_RXLEN		0x3FFF0000
#define WB_RXSTAT_RXCMP		0x40000000
#define WB_RXSTAT_OWN		0x80000000

#define WB_RXBYTES(x)		((x & WB_RXSTAT_RXLEN) >> 16)
#define WB_RXSTAT (WB_RXSTAT_FIRSTFRAG|WB_RXSTAT_LASTFRAG|WB_RXSTAT_OWN)

#define WB_RXCTL_BUFLEN1	0x00000FFF
#define WB_RXCTL_BUFLEN2	0x00FFF000
#define WB_RXCTL_RLINK		0x01000000
#define WB_RXCTL_RLAST		0x02000000

#define WB_TXSTAT_DEFER		0x00000001
#define WB_TXSTAT_UNDERRUN	0x00000002
#define WB_TXSTAT_COLLCNT	0x00000078
#define WB_TXSTAT_SQE		0x00000080
#define WB_TXSTAT_ABORT		0x00000100
#define WB_TXSTAT_LATECOLL	0x00000200
#define WB_TXSTAT_NOCARRIER	0x00000400
#define WB_TXSTAT_CARRLOST	0x00000800
#define WB_TXSTAT_TXERR		0x00001000
#define WB_TXSTAT_OWN		0x80000000

#define WB_TXCTL_BUFLEN1	0x000007FF
#define WB_TXCTL_BUFLEN2	0x003FF800
#define WB_TXCTL_PAD		0x00800000
#define WB_TXCTL_TLINK		0x01000000
#define WB_TXCTL_TLAST		0x02000000
#define WB_TXCTL_NOCRC		0x08000000
#define WB_TXCTL_FIRSTFRAG	0x20000000
#define WB_TXCTL_LASTFRAG	0x40000000
#define WB_TXCTL_FINT		0x80000000

#define WB_MAXFRAGS		16
#define WB_RX_LIST_CNT		64
#define WB_TX_LIST_CNT		128
#define WB_MIN_FRAMELEN		60
#define ETHER_ALIGN		2

/*
 * A transmit 'super descriptor' is actually WB_MAXFRAGS regular
 * descriptors clumped together. The idea here is to emulate the
 * multi-fragment descriptor layout found in devices such as the
 * Texas Instruments ThunderLAN and 3Com boomerang and cylone chips.
 * The advantage to using this scheme is that it avoids buffer copies.
 * The disadvantage is that there's a certain amount of overhead due
 * to the fact that each 'fragment' is 16 bytes long. In my tests,
 * this limits top speed to about 10.5MB/sec. It should be more like
 * 11.5MB/sec. However, the upshot is that you can achieve better
 * results on slower machines: a Pentium 200 can pump out packets at
 * same speed as a PII 400.
 */
struct wb_txdesc {
	struct wb_desc		wb_frag[WB_MAXFRAGS];
};

#define WB_TXNEXT(x)	x->wb_ptr->wb_frag[x->wb_lastdesc].wb_next
#define WB_TXSTATUS(x)	x->wb_ptr->wb_frag[x->wb_lastdesc].wb_status
#define WB_TXCTL(x)	x->wb_ptr->wb_frag[x->wb_lastdesc].wb_ctl
#define WB_TXDATA(x)	x->wb_ptr->wb_frag[x->wb_lastdesc].wb_data

#define WB_TXOWN(x)	x->wb_ptr->wb_frag[0].wb_status

#define WB_UNSENT	0x1234

#define WB_BUFBYTES	(1024 * sizeof(u_int32_t))

struct wb_buf {
	u_int32_t		wb_data[1024];
};

struct wb_list_data {
	struct wb_buf		wb_rxbufs[WB_RX_LIST_CNT];
	struct wb_desc		wb_rx_list[WB_RX_LIST_CNT];
	struct wb_txdesc	wb_tx_list[WB_TX_LIST_CNT];
};

struct wb_chain {
	struct wb_txdesc	*wb_ptr;
	struct mbuf		*wb_mbuf;
	struct wb_chain		*wb_nextdesc;
	u_int8_t		wb_lastdesc;
};

struct wb_chain_onefrag {
	struct wb_desc		*wb_ptr;
	struct mbuf		*wb_mbuf;
	void			*wb_buf;
	struct wb_chain_onefrag	*wb_nextdesc;
	u_int8_t		wb_rlast;
};

struct wb_chain_data {
	u_int8_t		wb_pad[WB_MIN_FRAMELEN];
	struct wb_chain_onefrag	wb_rx_chain[WB_RX_LIST_CNT];
	struct wb_chain		wb_tx_chain[WB_TX_LIST_CNT];

	struct wb_chain_onefrag	*wb_rx_head;

	struct wb_chain		*wb_tx_head;
	struct wb_chain		*wb_tx_tail;
	struct wb_chain		*wb_tx_free;
};

struct wb_type {
	u_int16_t		wb_vid;
	u_int16_t		wb_did;
	const char		*wb_name;
};

struct wb_softc {
	struct ifnet		*wb_ifp;	/* interface info */
	device_t		wb_dev;
	device_t		wb_miibus;
	struct resource		*wb_res;
	struct resource		*wb_irq;
	void			*wb_intrhand;
	struct wb_type		*wb_info;	/* Winbond adapter info */
	u_int8_t		wb_type;
	u_int16_t		wb_txthresh;
	int			wb_cachesize;
	int			wb_timer;
	caddr_t			wb_ldata_ptr;
	struct wb_list_data	*wb_ldata;
	struct wb_chain_data	wb_cdata;
	struct callout		wb_stat_callout;
	struct mtx		wb_mtx;
};

#define	WB_LOCK(_sc)		mtx_lock(&(_sc)->wb_mtx)
#define	WB_UNLOCK(_sc)		mtx_unlock(&(_sc)->wb_mtx)
#define	WB_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->wb_mtx, MA_OWNED)

/*
 * register space access macros
 */
#define CSR_WRITE_4(sc, reg, val)	bus_write_4(sc->wb_res, reg, val)
#define CSR_WRITE_2(sc, reg, val)	bus_write_2(sc->wb_res, reg, val)
#define CSR_WRITE_1(sc, reg, val)	bus_write_1(sc->wb_res, reg, val)

#define CSR_READ_4(sc, reg)		bus_read_4(sc->wb_res, reg)
#define CSR_READ_2(sc, reg)		bus_read_2(sc->wb_res, reg)
#define CSR_READ_1(sc, reg)		bus_read_1(sc->wb_res, reg)

#define	CSR_BARRIER(sc, reg, length, flags)				\
	bus_barrier(sc->wb_res, reg, length, flags)

#define WB_TIMEOUT		1000

/*
 * General constants that are fun to know.
 *
 * Winbond PCI vendor ID
 */
#define	WB_VENDORID		0x1050

/*
 * Winbond device IDs.
 */
#define	WB_DEVICEID_840F	0x0840

/*
 * Compex vendor ID.
 */
#define CP_VENDORID		0x11F6

/*
 * Compex device IDs.
 */
#define CP_DEVICEID_RL100	0x2011

/*
 * PCI low memory base and low I/O base register, and
 * other PCI registers.
 */

#define WB_PCI_VENDOR_ID	0x00
#define WB_PCI_DEVICE_ID	0x02
#define WB_PCI_COMMAND		0x04
#define WB_PCI_STATUS		0x06
#define WB_PCI_CLASSCODE	0x09
#define WB_PCI_CACHELEN		0x0C
#define WB_PCI_LATENCY_TIMER	0x0D
#define WB_PCI_HEADER_TYPE	0x0E
#define WB_PCI_LOIO		0x10
#define WB_PCI_LOMEM		0x14
#define WB_PCI_BIOSROM		0x30
#define WB_PCI_INTLINE		0x3C
#define WB_PCI_INTPIN		0x3D
#define WB_PCI_MINGNT		0x3E
#define WB_PCI_MINLAT		0x0F
#define WB_PCI_RESETOPT		0x48
#define WB_PCI_EEPROM_DATA	0x4C

/* power management registers */
#define WB_PCI_CAPID		0xDC /* 8 bits */
#define WB_PCI_NEXTPTR		0xDD /* 8 bits */
#define WB_PCI_PWRMGMTCAP	0xDE /* 16 bits */
#define WB_PCI_PWRMGMTCTRL	0xE0 /* 16 bits */

#define WB_PSTATE_MASK		0x0003
#define WB_PSTATE_D0		0x0000
#define WB_PSTATE_D1		0x0002
#define WB_PSTATE_D2		0x0002
#define WB_PSTATE_D3		0x0003
#define WB_PME_EN		0x0010
#define WB_PME_STATUS		0x8000
