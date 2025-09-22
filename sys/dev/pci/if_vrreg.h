/*	$OpenBSD: if_vrreg.h,v 1.38 2025/07/14 23:49:08 jsg Exp $	*/

/*
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
 * $FreeBSD: src/sys/pci/if_vrreg.h,v 1.17 2003/02/01 01:27:05 silby Exp $
 */

/*
 * Rhine register definitions.
 */

#define VR_PAR0			0x00	/* node address 0 to 4 */
#define VR_PAR1			0x04	/* node address 2 to 6 */
#define VR_RXCFG		0x06	/* receiver config register */
#define VR_TXCFG		0x07	/* transmit config register */
#define VR_COMMAND		0x08	/* command register */
#define VR_TQWK			0x0A	/* transmit queue wake */
#define VR_ISR			0x0C	/* interrupt/status register */
#define VR_IMR			0x0E	/* interrupt mask register */
#define VR_MAR0			0x10	/* multicast hash 0 */
#define VR_MAR1			0x14	/* multicast hash 1 */
#define VR_RXADDR		0x18	/* rx descriptor list start addr */
#define VR_TXADDR		0x1C	/* tx descriptor list start addr */
#define VR_CURRXDESC0		0x20
#define VR_CURRXDESC1		0x24
#define VR_CURRXDESC2		0x28
#define VR_CURRXDESC3		0x2C
#define VR_NEXTRXDESC0		0x30
#define VR_NEXTRXDESC1		0x34
#define VR_NEXTRXDESC2		0x38
#define VR_NEXTRXDESC3		0x3C
#define VR_CURTXDESC0		0x40
#define VR_CURTXDESC1		0x44
#define VR_CURTXDESC2		0x48
#define VR_CURTXDESC3		0x4C
#define VR_NEXTTXDESC0		0x50
#define VR_NEXTTXDESC1		0x54
#define VR_NEXTTXDESC2		0x58
#define VR_NEXTTXDESC3		0x5C
#define VR_CURRXDMA		0x60	/* current RX DMA address */
#define VR_CURTXDMA		0x64	/* current TX DMA address */
#define VR_TALLYCNT		0x68	/* tally counter test register */
#define VR_PHYADDR		0x6C
#define VR_MIISTAT		0x6D
#define VR_BCR0			0x6E
#define VR_BCR1			0x6F
#define VR_MIICMD		0x70
#define VR_MIIADDR		0x71
#define VR_MIIDATA		0x72
#define VR_EECSR		0x74
#define VR_TEST			0x75
#define VR_GPIO			0x76
#define VR_CONFIG		0x78
#define VR_MPA_CNT		0x7C
#define VR_CRC_CNT		0x7E
#define VR_STICKHW		0x83
#define VR_CAMMASK		0x88 /* length 4 bytes */
#define VR_CAMCTRL		0x92
#define VR_CAMADDR		0x93

/* Misc Registers */
#define VR_MISC_CR1		0x81
#define VR_MISCCR1_FORSRST	0x40

/*
 * RX config bits.
 */
#define VR_RXCFG_RX_ERRPKTS	0x01
#define VR_RXCFG_RX_RUNT	0x02
#define VR_RXCFG_RX_MULTI	0x04
#define VR_RXCFG_RX_BROAD	0x08
#define VR_RXCFG_RX_PROMISC	0x10
#define VR_RXCFG_RX_THRESH	0xE0

#define VR_RXTHRESH_32BYTES	0x00
#define VR_RXTHRESH_64BYTES	0x20
#define VR_RXTHRESH_128BYTES	0x40
#define VR_RXTHRESH_256BYTES	0x60
#define VR_RXTHRESH_512BYTES	0x80
#define VR_RXTHRESH_768BYTES	0xA0
#define VR_RXTHRESH_1024BYTES	0xC0
#define VR_RXTHRESH_STORENFWD	0xE0

/*
 * TX config bits.
 */
#define VR_TXCFG_TXTAGEN	0x01	/* 6105M */
#define VR_TXCFG_LOOPBKMODE	0x06
#define VR_TXCFG_BACKOFF	0x08
#define VR_TXCFG_TX_THRESH	0xE0

#define VR_TXTHRESH_32BYTES	0x00
#define VR_TXTHRESH_64BYTES	0x20
#define VR_TXTHRESH_128BYTES	0x40
#define VR_TXTHRESH_256BYTES	0x60
#define VR_TXTHRESH_512BYTES	0x80
#define VR_TXTHRESH_768BYTES	0xA0
#define VR_TXTHRESH_1024BYTES	0xC0
#define VR_TXTHRESH_STORENFWD	0xE0

/*
 * Command register bits.
 */
#define VR_CMD_INIT		0x0001
#define VR_CMD_START		0x0002
#define VR_CMD_STOP		0x0004
#define VR_CMD_RX_ON		0x0008
#define VR_CMD_TX_ON		0x0010
#define	VR_CMD_TX_GO		0x0020
#define VR_CMD_RX_GO		0x0040
#define VR_CMD_RSVD		0x0080
#define VR_CMD_RX_EARLY		0x0100
#define VR_CMD_TX_EARLY		0x0200
#define VR_CMD_FULLDUPLEX	0x0400
#define VR_CMD_TX_NOPOLL	0x0800

#define VR_CMD_RESET		0x8000

/*
 * Interrupt status bits.
 */
#define VR_ISR_RX_OK		0x0001	/* packet rx ok */
#define VR_ISR_TX_OK		0x0002	/* packet tx ok */
#define VR_ISR_RX_ERR		0x0004	/* packet rx with err */
#define VR_ISR_TX_ABRT		0x0008	/* tx aborted due to excess colls */
#define VR_ISR_TX_UNDERRUN	0x0010	/* tx buffer underflow */
#define VR_ISR_RX_NOBUF		0x0020	/* no rx buffer available */
#define VR_ISR_BUSERR		0x0040	/* PCI bus error */
#define VR_ISR_STATSOFLOW	0x0080	/* stats counter oflow */
#define VR_ISR_RX_EARLY		0x0100	/* rx early */
#define VR_ISR_LINKSTAT		0x0200	/* MII status change */
#define VR_ISR_ETI		0x0200	/* Tx early (3043/3071) */
#define VR_ISR_UDFI		0x0200	/* Tx FIFO underflow (3065) */
#define VR_ISR_RX_OFLOW		0x0400	/* rx FIFO overflow */
#define VR_ISR_RX_DROPPED	0x0800
#define VR_ISR_RX_NOBUF2	0x1000
#define VR_ISR_TX_ABRT2		0x2000
#define VR_ISR_LINKSTAT2	0x4000
#define VR_ISR_MAGICPACKET	0x8000

/*
 * Interrupt mask bits.
 */
#define VR_IMR_RX_OK		0x0001	/* packet rx ok */
#define VR_IMR_TX_OK		0x0002	/* packet tx ok */
#define VR_IMR_RX_ERR		0x0004	/* packet rx with err */
#define VR_IMR_TX_ABRT		0x0008	/* tx aborted due to excess colls */
#define VR_IMR_TX_UNDERRUN	0x0010	/* tx buffer underflow */
#define VR_IMR_RX_NOBUF		0x0020	/* no rx buffer available */
#define VR_IMR_BUSERR		0x0040	/* PCI bus error */
#define VR_IMR_STATSOFLOW	0x0080	/* stats counter oflow */
#define VR_IMR_RX_EARLY		0x0100	/* rx early */
#define VR_IMR_LINKSTAT		0x0200	/* MII status change */
#define VR_IMR_RX_OFLOW		0x0400	/* rx FIFO overflow */
#define VR_IMR_RX_DROPPED	0x0800
#define VR_IMR_RX_NOBUF2	0x1000
#define VR_IMR_TX_ABRT2		0x2000
#define VR_IMR_LINKSTAT2	0x4000
#define VR_IMR_MAGICPACKET	0x8000

#define VR_INTRS							\
	(VR_IMR_RX_OK|VR_IMR_TX_OK|VR_IMR_RX_NOBUF|			\
	VR_IMR_TX_ABRT|VR_IMR_TX_UNDERRUN|VR_IMR_BUSERR|		\
	VR_IMR_RX_ERR|VR_ISR_RX_DROPPED)

/*
 * MII status register.
 */

#define VR_MIISTAT_SPEED	0x01
#define VR_MIISTAT_LINKFAULT	0x02
#define VR_MIISTAT_MGTREADERR	0x04
#define VR_MIISTAT_MIIERR	0x08
#define VR_MIISTAT_PHYOPT	0x10
#define VR_MIISTAT_MDC_SPEED	0x20
#define VR_MIISTAT_RSVD		0x40
#define VR_MIISTAT_GPIO1POLL	0x80

/*
 * MII command register bits.
 */
#define VR_MIICMD_CLK		0x01
#define VR_MIICMD_DATAOUT	0x02
#define VR_MIICMD_DATAIN	0x04
#define VR_MIICMD_DIR		0x08
#define VR_MIICMD_DIRECTPGM	0x10
#define VR_MIICMD_WRITE_ENB	0x20
#define VR_MIICMD_READ_ENB	0x40
#define VR_MIICMD_AUTOPOLL	0x80

/*
 * EEPROM control bits.
 */
#define VR_EECSR_DATAIN		0x01	/* data out */
#define VR_EECSR_DATAOUT	0x02	/* data in */
#define VR_EECSR_CLK		0x04	/* clock */
#define VR_EECSR_CS		0x08	/* chip select */
#define VR_EECSR_DPM		0x10
#define VR_EECSR_LOAD		0x20
#define VR_EECSR_EMBP		0x40
#define VR_EECSR_EEPR		0x80

#define VR_EECMD_WRITE		0x140
#define VR_EECMD_READ		0x180
#define VR_EECMD_ERASE		0x1c0

/*
 * Test register bits.
 */
#define VR_TEST_TEST0		0x01
#define VR_TEST_TEST1		0x02
#define VR_TEST_TEST2		0x04
#define VR_TEST_TSTUD		0x08
#define VR_TEST_TSTOV		0x10
#define VR_TEST_BKOFF		0x20
#define VR_TEST_FCOL		0x40
#define VR_TEST_HBDES		0x80

/*
 * Config register bits.
 */
#define VR_CFG_PREACPIWAKE	0x00000001	/* pre-acpi wake */
#define VR_CFG_ABNORMALWAKE	0x00000002	/* abnormal shut down wake */
#define VR_CFG_RSVD0		0x00000004
#define VR_CFG_LED0		0x00000008
#define VR_CFG_LED1		0x00000010
#define VR_CFG_TAGCRC		0x00000020	/* 6105M tag in CRC */
#define VR_CFG_MIIOPT		0x00000040	/* MII extension clock */
#define VR_CFG_EELOAD		0x00000080	/* enable EEPROM programming */
#define VR_CFG_LATMENB		0x00000100	/* latency timer effect enb. */
#define VR_CFG_MRREADWAIT	0x00000200
#define VR_CFG_MRWRITEWAIT	0x00000400
#define VR_CFG_RX_ARB		0x00000800
#define VR_CFG_TX_ARB		0x00001000
#define VR_CFG_READMULTI	0x00002000
#define VR_CFG_TX_PACE		0x00004000
#define VR_CFG_TX_QDIS		0x00008000
#define VR_CFG_ROMSEL0		0x00010000
#define VR_CFG_ROMSEL1		0x00020000
#define VR_CFG_ROMSEL2		0x00040000
#define VR_CFG_ROMTIMESEL	0x00080000
#define VR_CFG_RSVD1		0x00100000
#define VR_CFG_ROMDLY		0x00200000
#define VR_CFG_ROMOPT		0x00400000
#define VR_CFG_RSVD2		0x00800000
#define VR_CFG_BACKOFFOPT	0x01000000
#define VR_CFG_BACKOFFMOD	0x02000000
#define VR_CFG_CAPEFFECT	0x04000000
#define VR_CFG_BACKOFFRAND	0x08000000
#define VR_CFG_MAGICKPACKET	0x10000000
#define VR_CFG_PCIREADLINE	0x20000000
#define VR_CFG_DIAG		0x40000000
#define VR_CFG_GPIOEN		0x80000000

/* Sticky HW bits */
#define VR_STICKHW_DS0		0x01
#define VR_STICKHW_DS1		0x02
#define VR_STICKHW_WOL_ENB	0x04
#define VR_STICKHW_WOL_STS	0x08
#define VR_STICKHW_LEGWOL_ENB	0x80

/* Wake on Lan */
#define VR_WOLCRSET		0xA0
#define VR_PWRCFGSET		0xA1
#define VR_WOLCFGSET		0xA3
#define VR_WOLCRCLR		0xA4
#define VR_PWRCFGCLR		0xA5
#define VR_WOLCFGCLR		0xA7
#define VR_PWRCSRSET		0xA8
#define VR_PWRCSRCLR		0xAC
#define VR_WOLCR_UCAST		0x10
#define VR_WOLCR_MAGIC		0x20
#define VR_WOLCR_LINKON		0x40
#define VR_WOLCR_LINKOFF	0x80
#define VR_WOLCFG_PMEOVR	0x80

/*
 * BCR0 register bits. (At least for the VT6102 chip.)
 */
#define VR_BCR0_DMA_LENGTH      0x07

#define VR_BCR0_DMA_32BYTES     0x00
#define VR_BCR0_DMA_64BYTES     0x01
#define VR_BCR0_DMA_128BYTES    0x02
#define VR_BCR0_DMA_256BYTES    0x03
#define VR_BCR0_DMA_512BYTES    0x04
#define VR_BCR0_DMA_1024BYTES   0x05
#define VR_BCR0_DMA_STORENFWD   0x07
 
#define VR_BCR0_RX_THRESH       0x38

#define VR_BCR0_RXTHRESHCFG     0x00
#define VR_BCR0_RXTHRESH64BYTES 0x08
#define VR_BCR0_RXTHRESH128BYTES 0x10
#define VR_BCR0_RXTHRESH256BYTES 0x18
#define VR_BCR0_RXTHRESH512BYTES 0x20
#define VR_BCR0_RXTHRESH1024BYTES 0x28
#define VR_BCR0_RXTHRESHSTORENFWD 0x38
#define VR_BCR0_EXTLED          0x40
#define VR_BCR0_MED2            0x80

/*
 * BCR1 register bits. (At least for the VT6102 chip.)
 */
#define VR_BCR1_POT0            0x01
#define VR_BCR1_POT1            0x02
#define VR_BCR1_POT2            0x04
#define VR_BCR1_TX_THRESH       0x38
#define VR_BCR1_TXTHRESHCFG     0x00
#define VR_BCR1_TXTHRESH64BYTES 0x08
#define VR_BCR1_TXTHRESH128BYTES 0x10
#define VR_BCR1_TXTHRESH256BYTES 0x18
#define VR_BCR1_TXTHRESH512BYTES 0x20
#define VR_BCR1_TXTHRESH1024BYTES 0x28
#define VR_BCR1_TXTHRESHSTORENFWD 0x38
#define VR_BCR1_VLANFILT_ENB	0x80		/* 6105M */

/*
 * CAM Control registers (VT6105M)
 */
#define VR_CAMCTRL_WREN		0x01
#define VR_CAMCTRL_VCAMSEL	0x02
#define VR_CAMCTRL_WRITE	0x04
#define VR_CAMCTRL_READ		0x08

/*
 * Rhine TX/RX list structure.
 */

struct vr_desc {
	u_int32_t		vr_status;
	u_int32_t		vr_ctl;
	u_int32_t		vr_data;
	u_int32_t		vr_next;
};

#define VR_RXSTAT_RXERR		0x00000001
#define VR_RXSTAT_CRCERR	0x00000002
#define VR_RXSTAT_FRAMEALIGNERR	0x00000004
#define VR_RXSTAT_FIFOOFLOW	0x00000008
#define VR_RXSTAT_GIANT		0x00000010
#define VR_RXSTAT_RUNT		0x00000020
#define VR_RXSTAT_BUSERR	0x00000040
#define VR_RXSTAT_FRAG		0x00000040	/* 6105M */
#define VR_RXSTAT_BUFFERR	0x00000080
#define VR_RXSTAT_LASTFRAG	0x00000100
#define VR_RXSTAT_FIRSTFRAG	0x00000200
#define VR_RXSTAT_RLINK		0x00000400
#define VR_RXSTAT_RX_PHYS	0x00000800
#define VR_RXSTAT_RX_BROAD	0x00001000
#define VR_RXSTAT_RX_MULTI	0x00002000
#define VR_RXSTAT_RX_VIDHIT	0x00004000	/* 6105M */
#define VR_RXSTAT_RX_OK		0x00008000
#define VR_RXSTAT_RXLEN		0x07FF0000
#define VR_RXSTAT_RXLEN_EXT	0x78000000
#define VR_RXSTAT_OWN		0x80000000

#define VR_RXBYTES(x)		((x & VR_RXSTAT_RXLEN) >> 16)
#define VR_RXSTAT (VR_RXSTAT_FIRSTFRAG|VR_RXSTAT_LASTFRAG|VR_RXSTAT_OWN)

#define VR_RXCTL_BUFLEN		0x000007FF
#define VR_RXCTL_BUFLEN_EXT	0x00007800
#define VR_RXCTL_CHAIN		0x00008000
#define VR_RXCTL_TAG		0x00010000
#define VR_RXCTL_UDP		0x00020000
#define VR_RXCTL_TCP		0x00040000
#define VR_RXCTL_IP		0x00080000
#define VR_RXCTL_TCPUDPOK	0x00100000
#define VR_RXCTL_IPOK		0x00200000
#define VR_RXCTL_SNAPTAG	0x00400000
#define VR_RXCTL_RXLERR		0x00800000	/* 6105M */
#define VR_RXCTL_RX_INTR	0x00800000

#define VR_RXCTL (VR_RXCTL_CHAIN|VR_RXCTL_RX_INTR)

#define VR_TXSTAT_DEFER		0x00000001
#define VR_TXSTAT_UNDERRUN	0x00000002
#define VR_TXSTAT_COLLCNT	0x00000078
#define VR_TXSTAT_SQE		0x00000080
#define VR_TXSTAT_ABRT		0x00000100
#define VR_TXSTAT_LATECOLL	0x00000200
#define VR_TXSTAT_CARRLOST	0x00000400
#define VR_TXSTAT_UDF		0x00000800
#define VR_TXSTAT_BUSERR	0x00002000
#define VR_TXSTAT_JABTIMEO	0x00004000
#define VR_TXSTAT_ERRSUM	0x00008000
/*
 * Note there are only 15 bits total.
 * The drop eligible indicator is left out.
 * VLAN ID at the lower 12 bits 0x0FFF;
 * priority code point at the upper 3 bits 0x7000.
 */
#define VR_TXSTAT_PQMASK	0x7FFF0000
#define VR_TXSTAT_OWN		0x80000000
#define VR_TXSTAT_PQSHIFT	16

#define VR_TXCTL_BUFLEN		0x000007FF
#define VR_TXCTL_BUFLEN_EXT	0x00007800
#define VR_TXCTL_TLINK		0x00008000
#define VR_TXCTL_NOCRC		0x00010000
#define VR_TXCTL_INSERTTAG	0x00020000
#define VR_TXCTL_IPCSUM		0x00040000
#define VR_TXCTL_UDPCSUM	0x00080000
#define VR_TXCTL_TCPCSUM	0x00100000
#define VR_TXCTL_FIRSTFRAG	0x00200000
#define VR_TXCTL_LASTFRAG	0x00400000
#define VR_TXCTL_FINT		0x00800000

/* TDES3 aka vr_next */
#define VR_TXNEXT_INTDISABLE	0x00000001

#define VR_MAXFRAGS		8
#define VR_RX_LIST_CNT		128
#define VR_TX_LIST_CNT		128
#define VR_MIN_FRAMELEN		60
#define VR_RXLEN		1524
/* determined experimentally; seems intermittent with higher values */
#define VR_RXLEN_BABYJUMBO	1758
#define VR_TX_INTR_THRESH	8

struct vr_list_data {
	struct vr_desc		vr_rx_list[VR_RX_LIST_CNT];
	struct vr_desc		vr_tx_list[VR_TX_LIST_CNT];
};

struct vr_chain {
	struct vr_desc		*vr_ptr;
	struct mbuf		*vr_mbuf;
	struct vr_chain		*vr_nextdesc;
	bus_addr_t		vr_paddr;
	bus_dmamap_t		vr_map;
};

struct vr_chain_onefrag {
	struct vr_desc		*vr_ptr;
	struct vr_chain_onefrag	*vr_nextdesc;
	struct mbuf 		*vr_mbuf;
	bus_addr_t		vr_paddr;
	bus_dmamap_t		vr_map;
};

struct vr_chain_data {
	struct vr_chain_onefrag	vr_rx_chain[VR_RX_LIST_CNT];
	struct vr_chain		vr_tx_chain[VR_TX_LIST_CNT];

	struct vr_chain_onefrag	*vr_rx_cons;
	struct vr_chain_onefrag	*vr_rx_prod;

	struct vr_chain		*vr_tx_cons;
	struct vr_chain		*vr_tx_prod;
	int			vr_tx_cnt;
	unsigned int		vr_tx_pkts;
};

struct vr_mii_frame {
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
#define VR_MII_STARTDELIM	0x01
#define VR_MII_READOP		0x02
#define VR_MII_WRITEOP		0x01
#define VR_MII_TURNAROUND	0x02

#define VR_FLAG_FORCEDELAY	1
#define VR_FLAG_SCHEDDELAY	2
#define VR_FLAG_DELAYTIMEO	3	

struct vr_dmamem {
	bus_dmamap_t		vrm_map;
	bus_dma_segment_t	vrm_seg;
	int			vrm_nsegs;
	size_t			vrm_size;
	caddr_t			vrm_kva;
};

struct vr_softc {
	struct device		sc_dev;		/* generic device structure */
	pci_chipset_tag_t	sc_pc;		/* PCI registers info */
	pcitag_t		sc_tag;
	void *			sc_ih;		/* interrupt handler cookie */
	struct arpcom		arpcom;		/* interface info */
	bus_space_handle_t	vr_bhandle;	/* bus space handle */
	bus_space_tag_t		vr_btag;	/* bus space tag */
	bus_dma_tag_t		sc_dmat;
	struct vr_type		*vr_info;	/* Rhine adapter info */
	u_int8_t		vr_revid;	/* Rhine chip revision */
	u_int8_t		vr_flags;	/* See VR_F_* below */
	struct vr_list_data	*vr_ldata;
	struct vr_chain_data	vr_cdata;
	struct mii_data		sc_mii;
	struct timeout		sc_to;
	struct timeout		sc_rxto;
	struct vr_dmamem	sc_listmap;	/* descriptor list map */
	struct vr_dmamem	sc_zeromap;	/* zero pad map */
	struct if_rxring	sc_rxring;
	int			sc_rxbufs;
	int			vr_link;
	int			vr_quirks;
};

#define VR_F_RESTART		0x01		/* Restart unit on next tick */

/*
 * register space access macros
 */
#define CSR_WRITE_4(sc, reg, val)	\
	bus_space_write_4(sc->vr_btag, sc->vr_bhandle, reg, val)
#define CSR_WRITE_2(sc, reg, val)	\
	bus_space_write_2(sc->vr_btag, sc->vr_bhandle, reg, val)
#define CSR_WRITE_1(sc, reg, val)	\
	bus_space_write_1(sc->vr_btag, sc->vr_bhandle, reg, val)

#define CSR_READ_4(sc, reg)		\
	bus_space_read_4(sc->vr_btag, sc->vr_bhandle, reg)
#define CSR_READ_2(sc, reg)		\
	bus_space_read_2(sc->vr_btag, sc->vr_bhandle, reg)
#define CSR_READ_1(sc, reg)		\
	bus_space_read_1(sc->vr_btag, sc->vr_bhandle, reg)

#define VR_TIMEOUT		1000

/*
 * VIA Rhine revision IDs
 */

#define REV_ID_VT3043_E			0x04
#define REV_ID_VT3071_A			0x20
#define REV_ID_VT3071_B			0x21
#define REV_ID_VT3065_A			0x40
#define REV_ID_VT3065_B			0x41
#define REV_ID_VT3065_C			0x42
#define REV_ID_VT6102_APOLLO		0x74
#define REV_ID_VT6103			0x78
#define REV_ID_VT3106			0x80
#define REV_ID_VT3106_J			0x80	/* 0x80-0x8F */
#define REV_ID_VT3106_S			0x90	/* 0x90-0xA0 */
#define REV_ID_VT6105M_A0		0x90
#define REV_ID_VT6105M_B1		0x94

/*
 * PCI low memory base and low I/O base register, and
 * other PCI registers.
 */

#define VR_PCI_VENDOR_ID	0x00
#define VR_PCI_DEVICE_ID	0x02
#define VR_PCI_COMMAND		0x04
#define VR_PCI_STATUS		0x06
#define VR_PCI_REVID		0x08
#define VR_PCI_CLASSCODE	0x09
#define VR_PCI_LATENCY_TIMER	0x0D
#define VR_PCI_HEADER_TYPE	0x0E
#define VR_PCI_LOIO		0x10
#define VR_PCI_LOMEM		0x14
#define VR_PCI_BIOSROM		0x30
#define VR_PCI_INTLINE		0x3C
#define VR_PCI_INTPIN		0x3D
#define VR_PCI_MINGNT		0x3E
#define VR_PCI_MINLAT		0x0F
#define VR_PCI_RESETOPT		0x48
#define VR_PCI_EEPROM_DATA	0x4C
#define VR_PCI_MODE		0x50

#define VR_MODE3_MIION		0x04

/* power management registers */
#define VR_PCI_CAPID		0xDC /* 8 bits */
#define VR_PCI_NEXTPTR		0xDD /* 8 bits */
#define VR_PCI_PWRMGMTCAP	0xDE /* 16 bits */
#define VR_PCI_PWRMGMTCTRL	0xE0 /* 16 bits */

#define VR_PME_EN		0x0010
#define VR_PME_STATUS		0x8000
