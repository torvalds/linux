/*	$OpenBSD: if_sisreg.h,v 1.35 2016/01/08 11:23:30 mpi Exp $ */
/*
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
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
 * $FreeBSD: src/sys/pci/if_sisreg.h,v 1.3 2000/08/22 23:26:51 wpaul Exp $
 */

/*
 * Register definitions for the SiS 900 and SiS 7016 chipsets. The
 * 7016 is actually an older chip and some of its registers differ
 * from the 900, however the core operational registers are the same:
 * the differences lie in the OnNow/Wake on LAN stuff which we don't
 * use anyway. The 7016 needs an external MII compliant PHY while the
 * SiS 900 has one built in. All registers are 32-bits wide.
 */

/* Registers common to SiS 900 and SiS 7016 */
#define SIS_CSR			0x00
#define SIS_CFG			0x04
#define SIS_EECTL		0x08
#define SIS_PCICTL		0x0C
#define SIS_ISR			0x10
#define SIS_IMR			0x14
#define SIS_IER			0x18
#define SIS_PHYCTL		0x1C
#define SIS_TX_LISTPTR		0x20
#define SIS_TX_CFG		0x24
#define SIS_RX_LISTPTR		0x30
#define SIS_RX_CFG		0x34
#define SIS_FLOWCTL		0x38
#define SIS_RXFILT_CTL		0x48
#define SIS_RXFILT_DATA		0x4C
#define SIS_PWRMAN_CTL		0xB0
#define SIS_PWERMAN_WKUP_EVENT	0xB4
#define SIS_WKUP_FRAME_CRC	0xBC
#define SIS_WKUP_FRAME_MASK0	0xC0
#define SIS_WKUP_FRAME_MASKXX	0xEC

/* SiS 7016 specific registers */
#define SIS_SILICON_REV		0x5C
#define SIS_MIB_CTL0		0x60
#define SIS_MIB_CTL1		0x64
#define SIS_MIB_CTL2		0x68
#define SIS_MIB_CTL3		0x6C
#define SIS_MIB			0x80
#define SIS_LINKSTS		0xA0
#define SIS_TIMEUNIT		0xA4
#define SIS_GPIO		0xB8

/* NS DP83815/6 registers */
#define NS_IHR			0x1C
#define NS_CLKRUN		0x3C
#define NS_SRR			0x58
#define NS_BMCR			0x80
#define NS_BMSR			0x84
#define NS_PHYIDR1		0x88
#define NS_PHYIDR2		0x8C
#define NS_ANAR			0x90
#define NS_ANLPAR		0x94
#define NS_ANER			0x98
#define NS_ANNPTR		0x9C

#define NS_PHY_CR		0xE4
#define NS_PHY_10BTSCR		0xE8
#define NS_PHY_PAGE		0xCC
#define NS_PHY_EXTCFG		0xF0
#define NS_PHY_DSPCFG		0xF4
#define NS_PHY_SDCFG		0xF8
#define NS_PHY_TDATA		0xFC

#define NS_CLKRUN_PMESTS	0x00008000
#define NS_CLKRUN_PMEENB	0x00000100
#define NS_CLNRUN_CLKRUN_ENB	0x00000001

/* NS silicon revisions */
#define NS_SRR_15C		0x302
#define NS_SRR_15D		0x403
#define NS_SRR_16A		0x505

#define SIS_CSR_TX_ENABLE	0x00000001
#define SIS_CSR_TX_DISABLE	0x00000002
#define SIS_CSR_RX_ENABLE	0x00000004
#define SIS_CSR_RX_DISABLE	0x00000008
#define SIS_CSR_TX_RESET	0x00000010
#define SIS_CSR_RX_RESET	0x00000020
#define SIS_CSR_SOFTINTR	0x00000080
#define SIS_CSR_RESET		0x00000100
#define SIS_CSR_RELOAD		0x00000400

#define SIS_CFG_BIGENDIAN	0x00000001
#define SIS_CFG_PERR_DETECT	0x00000008
#define SIS_CFG_DEFER_DISABLE	0x00000010
#define SIS_CFG_OUTOFWIN_TIMER	0x00000020
#define SIS_CFG_SINGLE_BACKOFF	0x00000040
#define SIS_CFG_PCIREQ_ALG	0x00000080
#define SIS_CFG_FAIR_BACKOFF	0x00000200 /* 635 & 900B Specific */
#define SIS_CFG_RND_CNT		0x00000400 /* 635 & 900B Specific */
#define SIS_CFG_EDB_MASTER_EN	0x00002000

#define SIS_EECTL_DIN		0x00000001
#define SIS_EECTL_DOUT		0x00000002
#define SIS_EECTL_CLK		0x00000004
#define SIS_EECTL_CSEL		0x00000008

#define SIS96x_EECTL_GNT	0x00000100
#define SIS96x_EECTL_DONE	0x00000200
#define SIS96x_EECTL_REQ	0x00000400

#define	SIS_MII_CLK		0x00000040
#define	SIS_MII_DIR		0x00000020
#define	SIS_MII_DATA		0x00000010

#define SIS_EECMD_WRITE		0x140
#define SIS_EECMD_READ		0x180
#define SIS_EECMD_ERASE		0x1c0

#define SIS_EE_NODEADDR		0x8
#define NS_EE_NODEADDR		0x6

#define SIS_PCICTL_SRAMADDR	0x0000001F
#define SIS_PCICTL_RAMTSTENB	0x00000020
#define SIS_PCICTL_TXTSTENB	0x00000040
#define SIS_PCICTL_RXTSTENB	0x00000080
#define SIS_PCICTL_BMTSTENB	0x00000200
#define SIS_PCICTL_RAMADDR	0x001F0000
#define SIS_PCICTL_ROMTIME	0x0F000000
#define SIS_PCICTL_DISCTEST	0x40000000

#define SIS_ISR_RX_OK		0x00000001
#define SIS_ISR_RX_DESC_OK	0x00000002
#define SIS_ISR_RX_ERR		0x00000004
#define SIS_ISR_RX_EARLY	0x00000008
#define SIS_ISR_RX_IDLE		0x00000010
#define SIS_ISR_RX_OFLOW	0x00000020
#define SIS_ISR_TX_OK		0x00000040
#define SIS_ISR_TX_DESC_OK	0x00000080
#define SIS_ISR_TX_ERR		0x00000100
#define SIS_ISR_TX_IDLE		0x00000200
#define SIS_ISR_TX_UFLOW	0x00000400
#define SIS_ISR_SOFTINTR	0x00000800
#define SIS_ISR_HIBITS		0x00008000
#define SIS_ISR_RX_FIFO_OFLOW	0x00010000
#define SIS_ISR_TGT_ABRT	0x00100000
#define SIS_ISR_BM_ABRT		0x00200000
#define SIS_ISR_SYSERR		0x00400000
#define SIS_ISR_PARITY_ERR	0x00800000
#define SIS_ISR_RX_RESET_DONE	0x01000000
#define SIS_ISR_TX_RESET_DONE	0x02000000
#define SIS_ISR_TX_PAUSE_START	0x04000000
#define SIS_ISR_TX_PAUSE_DONE	0x08000000
#define SIS_ISR_WAKE_EVENT	0x10000000

#define SIS_IMR_RX_OK		0x00000001
#define SIS_IMR_RX_DESC_OK	0x00000002
#define SIS_IMR_RX_ERR		0x00000004
#define SIS_IMR_RX_EARLY	0x00000008
#define SIS_IMR_RX_IDLE		0x00000010
#define SIS_IMR_RX_OFLOW	0x00000020
#define SIS_IMR_TX_OK		0x00000040
#define SIS_IMR_TX_DESC_OK	0x00000080
#define SIS_IMR_TX_ERR		0x00000100
#define SIS_IMR_TX_IDLE		0x00000200
#define SIS_IMR_TX_UFLOW	0x00000400
#define SIS_IMR_SOFTINTR	0x00000800
#define SIS_IMR_HIBITS		0x00008000
#define SIS_IMR_RX_FIFO_OFLOW	0x00010000
#define SIS_IMR_TGT_ABRT	0x00100000
#define SIS_IMR_BM_ABRT		0x00200000
#define SIS_IMR_SYSERR		0x00400000
#define SIS_IMR_PARITY_ERR	0x00800000
#define SIS_IMR_RX_RESET_DONE	0x01000000
#define SIS_IMR_TX_RESET_DONE	0x02000000
#define SIS_IMR_TX_PAUSE_START	0x04000000
#define SIS_IMR_TX_PAUSE_DONE	0x08000000
#define SIS_IMR_WAKE_EVENT	0x10000000

#define SIS_INTRS	\
	(SIS_IMR_RX_OFLOW|SIS_IMR_TX_UFLOW|SIS_IMR_TX_OK|\
	 SIS_IMR_TX_IDLE|SIS_IMR_RX_OK|SIS_IMR_RX_ERR|\
	 SIS_IMR_RX_IDLE|\
	 SIS_IMR_SYSERR)

/* Interrupt Holdoff Register */
#define NS_IHR_HOLDCTL 0x00000100

/*
 * Interrupt holdoff value for NS DP8316. We can have the chip
 * delay interrupt delivery for a certain period. Units are in
 * 100us - this sets the delay to 1ms holdoff.
 */
#define NS_IHR_DELAY 10

#define NS_IHR_VALUE (NS_IHR_HOLDCTL|NS_IHR_DELAY)

#define SIS_IER_INTRENB		0x00000001

#define SIS_PHYCTL_ACCESS	0x00000010
#define SIS_PHYCTL_OP		0x00000020
#define SIS_PHYCTL_REGADDR	0x000007C0
#define SIS_PHYCTL_PHYADDR	0x0000F800
#define SIS_PHYCTL_PHYDATA	0xFFFF0000

#define SIS_PHYOP_READ		0x00000020
#define SIS_PHYOP_WRITE		0x00000000

#define SIS_TXCFG_DRAIN_THRESH	0x0000003F /* 32-byte units */
#define SIS_TXCFG_FILL_THRESH	0x00003F00 /* 32-byte units */
#define SIS_TXCFG_MPII03D	0x00040000 /* "Must be 1" */
#define SIS_TXCFG_DMABURST	0x00700000
#define SIS_TXCFG_AUTOPAD	0x10000000
#define SIS_TXCFG_LOOPBK	0x20000000
#define SIS_TXCFG_IGN_HBEAT	0x40000000
#define SIS_TXCFG_IGN_CARR	0x80000000

#define SIS_TXCFG_DRAIN(x)	(((x) >> 5) & SIS_TXCFG_DRAIN_THRESH)
#define SIS_TXCFG_FILL(x)	((((x) >> 5) << 8) & SIS_TXCFG_FILL_THRESH)

#define SIS_TXDMA_512BYTES	0x00000000
#define SIS_TXDMA_4BYTES	0x00100000
#define SIS_TXDMA_8BYTES	0x00200000
#define SIS_TXDMA_16BYTES	0x00300000
#define SIS_TXDMA_32BYTES	0x00400000
#define SIS_TXDMA_64BYTES	0x00500000
#define SIS_TXDMA_128BYTES	0x00600000
#define SIS_TXDMA_256BYTES	0x00700000

#define SIS_TXCFG_100	\
	(SIS_TXDMA_64BYTES|SIS_TXCFG_AUTOPAD|\
	 SIS_TXCFG_FILL(ETHER_MIN_LEN)|SIS_TXCFG_DRAIN(ETHER_MAX_DIX_LEN))

#define SIS_TXCFG_10	\
	(SIS_TXDMA_32BYTES|SIS_TXCFG_AUTOPAD|\
	 SIS_TXCFG_FILL(ETHER_MIN_LEN)|SIS_TXCFG_DRAIN(ETHER_MAX_DIX_LEN))

#define SIS_RXCFG_DRAIN_THRESH	0x0000003E /* 8-byte units */
#define SIS_RXCFG_DMABURST	0x00700000
#define SIS_RXCFG_RX_JABBER	0x08000000
#define SIS_RXCFG_RX_TXPKTS	0x10000000
#define SIS_RXCFG_RX_RUNTS	0x40000000
#define SIS_RXCFG_RX_GIANTS	0x80000000

#define SIS_RXCFG_DRAIN(x)	((((x) >> 3) << 1) & SIS_RXCFG_DRAIN_THRESH)

#define SIS_RXDMA_512BYTES	0x00000000
#define SIS_RXDMA_4BYTES	0x00100000
#define SIS_RXDMA_8BYTES	0x00200000
#define SIS_RXDMA_16BYTES	0x00300000
#define SIS_RXDMA_32BYTES	0x00400000
#define SIS_RXDMA_64BYTES	0x00500000
#define SIS_RXDMA_128BYTES	0x00600000
#define SIS_RXDMA_256BYTES	0x00700000

#define SIS_RXCFG256 \
	(SIS_RXCFG_DRAIN(64)|SIS_RXDMA_256BYTES)
#define SIS_RXCFG64 \
	(SIS_RXCFG_DRAIN(64)|SIS_RXDMA_64BYTES)

#define SIS_RXFILTCTL_ADDR	0x000F0000
#define NS_RXFILTCTL_MCHASH	0x00200000
#define NS_RXFILTCTL_ARP	0x00400000
#define NS_RXFILTCTL_PERFECT	0x08000000
#define SIS_RXFILTCTL_ALLPHYS	0x10000000
#define SIS_RXFILTCTL_ALLMULTI	0x20000000
#define SIS_RXFILTCTL_BROAD	0x40000000
#define SIS_RXFILTCTL_ENABLE	0x80000000

#define SIS_FILTADDR_PAR0	0x00000000
#define SIS_FILTADDR_PAR1	0x00010000
#define SIS_FILTADDR_PAR2	0x00020000
#define SIS_FILTADDR_MAR0	0x00040000
#define SIS_FILTADDR_MAR1	0x00050000
#define SIS_FILTADDR_MAR2	0x00060000
#define SIS_FILTADDR_MAR3	0x00070000
#define SIS_FILTADDR_MAR4	0x00080000
#define SIS_FILTADDR_MAR5	0x00090000
#define SIS_FILTADDR_MAR6	0x000A0000
#define SIS_FILTADDR_MAR7	0x000B0000

#define NS_FILTADDR_PAR0	0x00000000
#define NS_FILTADDR_PAR1	0x00000002
#define NS_FILTADDR_PAR2	0x00000004

#define NS_FILTADDR_FMEM_LO	0x00000200
#define NS_FILTADDR_FMEM_HI	0x000003FE

/*
 * DMA descriptor structures. The first part of the descriptor
 * is the hardware descriptor format, which is just three longwords.
 * After this, we include some additional structure members for
 * use by the driver. Note that for this structure will be a different
 * size on the alpha, but that's okay as long as it's a multiple of 4
 * bytes in size.
 */
struct sis_desc {
	/* SiS hardware descriptor section */
	u_int32_t		sis_next;
	u_int32_t		sis_cmdsts;
#define sis_rxstat		sis_cmdsts
#define sis_txstat		sis_cmdsts
#define sis_ctl			sis_cmdsts
	u_int32_t		sis_ptr;
	/* Driver software section */
	struct mbuf		*sis_mbuf;
	struct sis_desc		*sis_nextdesc;
	bus_dmamap_t		map;
};

#define SIS_CMDSTS_BUFLEN	0x00000FFF
#define SIS_CMDSTS_PKT_OK	0x08000000
#define SIS_CMDSTS_CRC		0x10000000
#define SIS_CMDSTS_INTR		0x20000000
#define SIS_CMDSTS_MORE		0x40000000
#define SIS_CMDSTS_OWN		0x80000000

#define SIS_LASTDESC(x)		(!(letoh32((x)->sis_ctl) & SIS_CMDSTS_MORE)))
#define SIS_OWNDESC(x)		(letoh32((x)->sis_ctl) & SIS_CMDSTS_OWN)
#define SIS_INC(x, y)		(x) = ((x) == ((y)-1)) ? 0 : (x)+1
#define SIS_RXBYTES(x)		(letoh32((x)->sis_ctl) & SIS_CMDSTS_BUFLEN)

#define SIS_RXSTAT_COLL		0x00010000
#define SIS_RXSTAT_LOOPBK	0x00020000
#define SIS_RXSTAT_ALIGNERR	0x00040000
#define SIS_RXSTAT_CRCERR	0x00080000
#define SIS_RXSTAT_SYMBOLERR	0x00100000
#define SIS_RXSTAT_RUNT		0x00200000
#define SIS_RXSTAT_GIANT	0x00400000
#define SIS_RXSTAT_DSTCLASS	0x01800000
#define SIS_RXSTAT_OVERRUN	0x02000000
#define SIS_RXSTAT_RX_ABORT	0x04000000

#define SIS_RXSTAT_ERROR(x)						\
	((x) & (SIS_RXSTAT_RX_ABORT | SIS_RXSTAT_OVERRUN |		\
	SIS_RXSTAT_GIANT | SIS_RXSTAT_SYMBOLERR | SIS_RXSTAT_RUNT |	\
	SIS_RXSTAT_CRCERR | SIS_RXSTAT_ALIGNERR))

#define SIS_DSTCLASS_REJECT	0x00000000
#define SIS_DSTCLASS_UNICAST	0x00800000
#define SIS_DSTCLASS_MULTICAST	0x01000000
#define SIS_DSTCLASS_BROADCAST	0x02000000

#define SIS_TXSTAT_COLLCNT	0x000F0000
#define SIS_TXSTAT_EXCESSCOLLS	0x00100000
#define SIS_TXSTAT_OUTOFWINCOLL	0x00200000
#define SIS_TXSTAT_EXCESS_DEFER	0x00400000
#define SIS_TXSTAT_DEFERED	0x00800000
#define SIS_TXSTAT_CARR_LOST	0x01000000
#define SIS_TXSTAT_UNDERRUN	0x02000000
#define SIS_TXSTAT_TX_ABORT	0x04000000

#define SIS_MAXTXSEGS		16
#define SIS_RX_LIST_CNT		64
#define SIS_TX_LIST_CNT		128

struct sis_list_data {
	struct sis_desc		sis_rx_list[SIS_RX_LIST_CNT];
	struct sis_desc		sis_tx_list[SIS_TX_LIST_CNT];
};

struct sis_ring_data {
	struct if_rxring	sis_rx_ring;
	int			sis_rx_prod;
	int			sis_rx_cons;
	int			sis_tx_prod;
	int			sis_tx_cons;
	int			sis_tx_cnt;
};


/*
 * SiS PCI vendor ID.
 */
#define SIS_VENDORID		0x1039

/*
 * SiS PCI device IDs
 */
#define SIS_DEVICEID_900	0x0900
#define SIS_DEVICEID_7016	0x7016


/*
 * SiS 900 PCI revision codes.
 */
#define SIS_REV_900B		0x0003
#define SIS_REV_630A		0x0080
#define SIS_REV_630E		0x0081
#define SIS_REV_630S		0x0082
#define SIS_REV_630EA1		0x0083
#define SIS_REV_630ET		0x0084
#define SIS_REV_635		0x0090
#define SIS_REV_96x		0x0091

struct sis_type {
	u_int16_t		sis_vid;
	u_int16_t		sis_did;
	char			*sis_name;
};

struct sis_mii_frame {
	u_int8_t		mii_stdelim;
	u_int8_t		mii_opcode;
	u_int8_t		mii_phyaddr;
	u_int8_t		mii_regaddr;
	u_int8_t		mii_turnaround;
	u_int16_t		mii_data;
};

/*
 * MII constants
 */
#define	SIS_MII_STARTDELIM	0x01
#define	SIS_MII_READOP		0x02
#define	SIS_MII_WRITEOP		0x01
#define	SIS_MII_TURNAROUND	0x02

#define SIS_TYPE_900	1
#define SIS_TYPE_7016	2
#define SIS_TYPE_83815	3

struct sis_softc {
	struct device		sc_dev;		/* generic device structure */
	void			*sc_ih;		/* interrupt handler cookie */
	struct arpcom		arpcom;		/* interface info */
	mii_data_t		sc_mii;
	bus_space_handle_t	sis_bhandle;
	bus_space_tag_t		sis_btag;
	u_int8_t		sis_type;
	u_int8_t		sis_rev;
	u_int8_t		sis_link;
	u_int			sis_srr;
	struct sis_list_data	*sis_ldata;
	struct sis_ring_data	sis_cdata;
	struct timeout		sis_timeout;
	bus_dma_tag_t		sc_dmat;
	bus_dmamap_t		sc_listmap;
	bus_dma_segment_t	sc_listseg[1];
	int			sc_listnseg;
	caddr_t			sc_listkva;
	bus_dmamap_t		sc_tx_sparemap;
	int			sis_stopped;
};

/*
 * register space access macros
 */
#define CSR_WRITE_4(sc, reg, val)	\
	bus_space_write_4(sc->sis_btag, sc->sis_bhandle, reg, val)

#define CSR_READ_4(sc, reg)		\
	bus_space_read_4(sc->sis_btag, sc->sis_bhandle, reg)

#define SIS_TIMEOUT		1000
#define SIS_MIN_FRAMELEN	60

/*
 * PCI low memory base and low I/O base register, and
 * other PCI registers.
 */

#define SIS_PCI_VENDOR_ID	0x00
#define SIS_PCI_DEVICE_ID	0x02
#define SIS_PCI_COMMAND		0x04
#define SIS_PCI_STATUS		0x06
#define SIS_PCI_REVID		0x08
#define SIS_PCI_CLASSCODE	0x09
#define SIS_PCI_CACHELEN	0x0C
#define SIS_PCI_LATENCY_TIMER	0x0D
#define SIS_PCI_HEADER_TYPE	0x0E
#define SIS_PCI_LOIO		0x10
#define SIS_PCI_LOMEM		0x14
#define SIS_PCI_BIOSROM		0x30
#define SIS_PCI_INTLINE		0x3C
#define SIS_PCI_INTPIN		0x3D
#define SIS_PCI_MINGNT		0x3E
#define SIS_PCI_MINLAT		0x0F
#define SIS_PCI_RESETOPT	0x48
#define SIS_PCI_EEPROM_DATA	0x4C

/* power management registers */
#define SIS_PCI_CAPID		0x50 /* 8 bits */
#define SIS_PCI_NEXTPTR		0x51 /* 8 bits */
#define SIS_PCI_PWRMGMTCAP	0x52 /* 16 bits */
#define SIS_PCI_PWRMGMTCTRL	0x54 /* 16 bits */

#define SIS_PME_EN		0x0010
#define SIS_PME_STATUS		0x8000
