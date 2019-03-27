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
 * Rhine register definitions.
 */

#define VR_PAR0			0x00	/* node address 0 to 4 */
#define VR_PAR1			0x04	/* node address 2 to 6 */
#define VR_RXCFG		0x06	/* receiver config register */
#define VR_TXCFG		0x07	/* transmit config register */
#define VR_CR0			0x08	/* command register 0 */
#define VR_CR1			0x09	/* command register 1 */
#define	VR_TQW			0x0A	/* tx queue wake 6105M, 8bits */
#define VR_ISR			0x0C	/* interrupt/status register */
#define VR_IMR			0x0E	/* interrupt mask register */
#define VR_MAR0			0x10	/* multicast hash 0 */
#define VR_MAR1			0x14	/* multicast hash 1 */
#define VR_MCAM0		0x10
#define VR_MCAM1		0x11
#define VR_MCAM2		0x12
#define VR_MCAM3		0x13
#define VR_MCAM4		0x14
#define VR_MCAM5		0x15
#define VR_VCAM0		0x16
#define VR_VCAM1		0x17
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
#define	VR_CFGA			0x78
#define	VR_CFGB			0x79
#define	VR_CFGC			0x7A
#define	VR_CFGD			0x7B
#define VR_MPA_CNT		0x7C
#define VR_CRC_CNT		0x7E
#define VR_MISC_CR0		0x80	/* VT6102, 8bits */
#define VR_MISC_CR1		0x81
#define VR_STICKHW		0x83
#define	VR_MII_ISR		0x84
#define	VR_MII_IMR		0x86
#define	VR_CAMMASK		0x88	/* VT6105M, 32bits */
#define	VR_CAMCTL		0x92	/* VT6105M, 8bits */
#define	VR_CAMADDR		0x93	/* VT6105M, 8bits */
#define	VR_FLOWCR0		0x98
#define	VR_FLOWCR1		0x99
#define	VR_PAUSETIMER		0x9A	/* 16bit */
#define	VR_WOLCR_SET		0xA0
#define	VR_PWRCFG_SET		0xA1
#define	VR_TESTREG_SET		0xA2
#define	VR_WOLCFG_SET		0xA3
#define	VR_WOLCR_CLR		0xA4
#define	VR_PWRCFG_CLR		0xA5
#define	VR_TESTREG_CLR		0xA6
#define	VR_WOLCFG_CLR		0xA7
#define	VR_PWRCSR_SET		0xA8
#define	VR_PWRCSR1_SET		0xA9
#define	VR_PWRCSR_CLR		0xAC
#define	VR_PWRCSR1_CLR		0xAD

/* Misc Registers */
#define	VR_MISCCR0_RXPAUSE	0x08
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
#define VR_TXCFG_RXTAGCTL	0x10	/* 6105M */
#define VR_TXCFG_TX_THRESH	0xE0

#define VR_TXTHRESH_32BYTES	0x00
#define VR_TXTHRESH_64BYTES	0x20
#define VR_TXTHRESH_128BYTES	0x40
#define VR_TXTHRESH_256BYTES	0x60
#define VR_TXTHRESH_512BYTES	0x80
#define VR_TXTHRESH_768BYTES	0xA0
#define VR_TXTHRESH_1024BYTES	0xC0
#define VR_TXTHRESH_STORENFWD	0xE0
#define VR_TXTHRESH_MIN		1	/* 64 bytes */
#define VR_TXTHRESH_MAX		5	/* store and forward */

/*
 * Command register bits.
 */
#define VR_CR0_INIT		0x01
#define VR_CR0_START		0x02
#define VR_CR0_STOP		0x04
#define VR_CR0_RX_ON		0x08
#define VR_CR0_TX_ON		0x10
#define	VR_CR0_TX_GO		0x20
#define VR_CR0_RX_GO		0x40
#define VR_CR0_RSVD		0x80
#define VR_CR1_RX_EARLY		0x01
#define VR_CR1_TX_EARLY		0x02
#define VR_CR1_FULLDUPLEX	0x04
#define VR_CR1_TX_NOPOLL	0x08

#define VR_CR1_RESET		0x80

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
#define VR_ISR_UDFI		0x0200	/* Tx FIFO underflow (6102) */
#define VR_ISR_RX_OFLOW		0x0400	/* rx FIFO overflow */
#define VR_ISR_RX_DROPPED	0x0800
#define VR_ISR_RX_NOBUF2	0x1000	/* Rx descriptor running up */
#define VR_ISR_TX_ABRT2		0x2000
#define VR_ISR_LINKSTAT2	0x4000
#define VR_ISR_MAGICPACKET	0x8000

#define VR_ISR_ERR_BITS		"\20"					\
				"\3RXERR\4TXABRT\5TXUNDERRUN"		\
				"\6RXNOBUF\7BUSERR\10STATSOFLOW"	\
				"\12TXUDF\13RXOFLOW\14RXDROPPED"	\
				"\15RXNOBUF2\16TXABRT2"
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
 * Config A register bits.
 */
#define VR_CFG_GPIO2OUTENB	0x01
#define VR_CFG_GPIO2OUT		0x02	/* gen. purp. pin */
#define VR_CFG_GPIO2IN		0x04	/* gen. purp. pin */
#define VR_CFG_AUTOOPT		0x08	/* enable rx/tx autopoll */
#define VR_CFG_MIIOPT		0x10
#define VR_CFG_MMIENB		0x20	/* memory mapped mode enb */
#define VR_CFG_JUMPER		0x40	/* PHY and oper. mode select */
#define VR_CFG_EELOAD		0x80	/* enable EEPROM programming */

/*
 * Config B register bits.
 */
#define VR_CFG_LATMENB		0x01	/* larency timer effect enb. */
#define VR_CFG_MRREADWAIT	0x02
#define VR_CFG_MRWRITEWAIT	0x04
#define VR_CFG_RX_ARB		0x08
#define VR_CFG_TX_ARB		0x10
#define VR_CFG_READMULTI	0x20
#define VR_CFG_TX_PACE		0x40
#define VR_CFG_TX_QDIS		0x80

/*
 * Config C register bits.
 */
#define VR_CFG_ROMSEL0		0x01
#define VR_CFG_ROMSEL1		0x02
#define VR_CFG_ROMSEL2		0x04
#define VR_CFG_ROMTIMESEL	0x08
#define VR_CFG_RSVD0		0x10
#define VR_CFG_ROMDLY		0x20
#define VR_CFG_ROMOPT		0x40
#define VR_CFG_RSVD1		0x80

/*
 * Config D register bits.
 */
#define VR_CFG_BACKOFFOPT	0x01
#define VR_CFG_BACKOFFMOD	0x02
#define VR_CFG_CAPEFFECT	0x04
#define VR_CFG_BACKOFFRAND	0x08
#define VR_CFG_MAGICKPACKET	0x10
#define VR_CFG_PCIREADLINE	0x20
#define VR_CFG_DIAG		0x40
#define VR_CFG_GPIOEN		0x80

/* Sticky HW bits */
#define VR_STICKHW_DS0		0x01
#define VR_STICKHW_DS1		0x02
#define VR_STICKHW_WOL_ENB	0x04
#define VR_STICKHW_WOL_STS	0x08
#define VR_STICKHW_LEGWOL_ENB	0x80

/*
 * BCR0 register bits. (At least for the VT6102 chip.)
 */
#define VR_BCR0_DMA_LENGTH	0x07

#define VR_BCR0_DMA_32BYTES	0x00
#define VR_BCR0_DMA_64BYTES	0x01
#define VR_BCR0_DMA_128BYTES	0x02
#define VR_BCR0_DMA_256BYTES	0x03
#define VR_BCR0_DMA_512BYTES	0x04
#define VR_BCR0_DMA_1024BYTES	0x05
#define VR_BCR0_DMA_STORENFWD	0x07

#define VR_BCR0_RX_THRESH	0x38

#define VR_BCR0_RXTHRESHCFG	0x00
#define VR_BCR0_RXTHRESH64BYTES	0x08
#define VR_BCR0_RXTHRESH128BYTES 0x10
#define VR_BCR0_RXTHRESH256BYTES 0x18
#define VR_BCR0_RXTHRESH512BYTES 0x20
#define VR_BCR0_RXTHRESH1024BYTES 0x28
#define VR_BCR0_RXTHRESHSTORENFWD 0x38
#define VR_BCR0_EXTLED		0x40
#define VR_BCR0_MED2		0x80

/*
 * BCR1 register bits. (At least for the VT6102 chip.)
 */
#define VR_BCR1_POT0		0x01
#define VR_BCR1_POT1		0x02
#define VR_BCR1_POT2		0x04
#define VR_BCR1_TX_THRESH	0x38
#define VR_BCR1_TXTHRESHCFG	0x00
#define VR_BCR1_TXTHRESH64BYTES	0x08
#define VR_BCR1_TXTHRESH128BYTES 0x10
#define VR_BCR1_TXTHRESH256BYTES 0x18
#define VR_BCR1_TXTHRESH512BYTES 0x20
#define VR_BCR1_TXTHRESH1024BYTES 0x28
#define VR_BCR1_TXTHRESHSTORENFWD 0x38
#define	VR_BCR1_VLANFILT_ENB	0x80	/* VT6105M */

/*
 * CAMCTL register bits. (VT6105M only)
 */
#define	VR_CAMCTL_ENA		0x01
#define	VR_CAMCTL_VLAN		0x02
#define	VR_CAMCTL_MCAST		0x00
#define	VR_CAMCTL_WRITE		0x04
#define	VR_CAMCTL_READ		0x08

#define	VR_CAM_MCAST_CNT	32
#define	VR_CAM_VLAN_CNT		32

/*
 * FLOWCR1 register bits. (VT6105LOM, VT6105M only)
 */
#define	VR_FLOWCR1_TXLO4	0x00
#define	VR_FLOWCR1_TXLO8	0x40
#define	VR_FLOWCR1_TXLO16	0x80
#define	VR_FLOWCR1_TXLO24	0xC0
#define	VR_FLOWCR1_TXHI24	0x00
#define	VR_FLOWCR1_TXHI32	0x10
#define	VR_FLOWCR1_TXHI48	0x20
#define	VR_FLOWCR1_TXHI64	0x30
#define	VR_FLOWCR1_XONXOFF	0x08
#define	VR_FLOWCR1_TXPAUSE	0x04
#define	VR_FLOWCR1_RXPAUSE	0x02
#define	VR_FLOWCR1_HDX		0x01

/*
 * WOLCR register bits. (VT6102 or higher only)
 */
#define	VR_WOLCR_PATTERN0	0x01
#define	VR_WOLCR_PATTERN1	0x02
#define	VR_WOLCR_PATTERN2	0x04
#define	VR_WOLCR_PATTERN3	0x08
#define	VR_WOLCR_UCAST		0x10
#define	VR_WOLCR_MAGIC		0x20
#define	VR_WOLCR_LINKON		0x40
#define	VR_WOLCR_LINKOFF	0x80

/*
 * PWRCFG register bits. (VT6102 or higher only)
 */
#define	VR_PWRCFG_WOLEN		0x01
#define	VR_PWRCFG_WOLSR		0x02
#define	VR_PWRCFG_LEGACY_WOL	0x10
#define	VR_PWRCFG_WOLTYPE_PULSE	0x20
#define	VR_PWRCFG_SMIITIME	0x80

/*
 * WOLCFG register bits. (VT6102 or higher only)
 */
#define	VR_WOLCFG_PATTERN_PAGE	0x04	/* VT6505 B0 */
#define	VR_WOLCFG_SMIIOPT	0x04
#define	VR_WOLCFG_SMIIACC	0x08
#define	VR_WOLCFG_SAB		0x10
#define	VR_WOLCFG_SAM		0x20
#define	VR_WOLCFG_SFDX		0x40
#define	VR_WOLCFG_PMEOVR	0x80

/*
 * Rhine TX/RX list structure.
 */

struct vr_desc {
	uint32_t		vr_status;
	uint32_t		vr_ctl;
	uint32_t		vr_data;
	uint32_t		vr_nextphys;
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
#define VR_RXSTAT_ERR_BITS	"\20"				\
				"\1RXERR\2CRCERR\3FRAMEALIGN"	\
				"\4FIFOOFLOW\5GIANT\6RUNT"	\
				"\10BUFERR"

#define VR_RXCTL_BUFLEN		0x000007FF
#define VR_RXCTL_BUFLEN_EXT	0x00007800
#define VR_RXCTL_CHAIN		0x00008000
#define	VR_RXCTL_TAG		0x00010000
#define	VR_RXCTL_UDP		0x00020000
#define	VR_RXCTL_TCP		0x00040000
#define	VR_RXCTL_IP		0x00080000
#define	VR_RXCTL_TCPUDPOK	0x00100000
#define	VR_RXCTL_IPOK		0x00200000
#define	VR_RXCTL_SNAPTAG	0x00400000
#define	VR_RXCTL_RXLERR		0x00800000	/* 6105M */
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
#define VR_TXSTAT_TBUFF		0x00001000
#define VR_TXSTAT_BUSERR	0x00002000
#define VR_TXSTAT_JABTIMEO	0x00004000
#define VR_TXSTAT_ERRSUM	0x00008000
#define VR_TXSTAT_OWN		0x80000000

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

#define VR_MIN_FRAMELEN		60

#define VR_FLAG_FORCEDELAY	1
#define VR_FLAG_SCHEDDELAY	2
#define VR_FLAG_DELAYTIMEO	3


#define VR_TIMEOUT		1000
#define VR_MII_TIMEOUT		10000

#define	VR_PHYADDR_MASK		0x1f

/*
 * General constants that are fun to know.
 *
 * VIA vendor ID
 */
#define	VIA_VENDORID			0x1106

/*
 * VIA Rhine device IDs.
 */
#define	VIA_DEVICEID_RHINE		0x3043
#define VIA_DEVICEID_RHINE_II		0x6100
#define VIA_DEVICEID_RHINE_II_2		0x3065
#define VIA_DEVICEID_RHINE_III		0x3106
#define VIA_DEVICEID_RHINE_III_M	0x3053

/*
 * Delta Electronics device ID.
 */
#define DELTA_VENDORID			0x1500

/*
 * Delta device IDs.
 */
#define DELTA_DEVICEID_RHINE_II		0x1320

/*
 * Addtron vendor ID.
 */
#define ADDTRON_VENDORID		0x4033

/*
 * Addtron device IDs.
 */
#define ADDTRON_DEVICEID_RHINE_II	0x1320

/*
 * VIA Rhine revision IDs
 */

#define REV_ID_VT3043_E			0x04
#define REV_ID_VT3071_A			0x20
#define REV_ID_VT3071_B			0x21
#define REV_ID_VT6102_A			0x40
#define REV_ID_VT6102_B			0x41
#define REV_ID_VT6102_C			0x42
#define REV_ID_VT6102_APOLLO		0x74
#define REV_ID_VT6105_A0		0x80
#define REV_ID_VT6105_B0		0x83
#define REV_ID_VT6105_LOM		0x8A
#define REV_ID_VT6107_A0		0x8C
#define REV_ID_VT6107_A1		0x8D
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
#define VR_PCI_MODE0		0x50
#define VR_PCI_MODE2		0x52
#define VR_PCI_MODE3		0x53

#define VR_MODE2_PCEROPT	0x80 /* VT6102 only */
#define VR_MODE2_DISABT		0x40
#define VR_MODE2_MRDPL		0x08 /* VT6107A1 and above */
#define VR_MODE2_MODE10T	0x02

#define VR_MODE3_XONOPT		0x80
#define VR_MODE3_TPACEN		0x40
#define VR_MODE3_BACKOPT	0x20
#define VR_MODE3_DLTSEL		0x10
#define VR_MODE3_MIIDMY		0x08
#define VR_MODE3_MIION		0x04

/* power management registers */
#define VR_PCI_CAPID		0xDC /* 8 bits */
#define VR_PCI_NEXTPTR		0xDD /* 8 bits */
#define VR_PCI_PWRMGMTCAP	0xDE /* 16 bits */
#define VR_PCI_PWRMGMTCTRL	0xE0 /* 16 bits */

#define VR_PSTATE_MASK		0x0003
#define VR_PSTATE_D0		0x0000
#define VR_PSTATE_D1		0x0002
#define VR_PSTATE_D2		0x0002
#define VR_PSTATE_D3		0x0003
#define VR_PME_EN		0x0010
#define VR_PME_STATUS		0x8000

#define VR_RX_RING_CNT		128
#define VR_TX_RING_CNT		128
#define	VR_TX_RING_SIZE		sizeof(struct vr_desc) * VR_TX_RING_CNT
#define	VR_RX_RING_SIZE		sizeof(struct vr_desc) * VR_RX_RING_CNT
#define	VR_RING_ALIGN		sizeof(struct vr_desc)
#define	VR_RX_ALIGN		sizeof(uint32_t)
#define VR_MAXFRAGS		8
#define	VR_TX_INTR_THRESH	8

#define	VR_ADDR_LO(x)		((uint64_t)(x) & 0xffffffff)
#define	VR_ADDR_HI(x)		((uint64_t)(x) >> 32)
#define	VR_TX_RING_ADDR(sc, i)	\
    ((sc)->vr_rdata.vr_tx_ring_paddr + sizeof(struct vr_desc) * (i))
#define	VR_RX_RING_ADDR(sc, i)	\
    ((sc)->vr_rdata.vr_rx_ring_paddr + sizeof(struct vr_desc) * (i))
#define	VR_INC(x,y)		(x) = (((x) + 1) % y)

struct vr_txdesc {
	struct mbuf	*tx_m;
	bus_dmamap_t	tx_dmamap;
};

struct vr_rxdesc {
	struct mbuf	*rx_m;
	bus_dmamap_t	rx_dmamap;
	struct vr_desc	*desc;
};

struct vr_chain_data {
	bus_dma_tag_t		vr_parent_tag;
	bus_dma_tag_t		vr_tx_tag;
	struct vr_txdesc	vr_txdesc[VR_TX_RING_CNT];
	bus_dma_tag_t		vr_rx_tag;
	struct vr_rxdesc	vr_rxdesc[VR_RX_RING_CNT];
	bus_dma_tag_t		vr_tx_ring_tag;
	bus_dma_tag_t		vr_rx_ring_tag;
	bus_dmamap_t		vr_tx_ring_map;
	bus_dmamap_t		vr_rx_ring_map;
	bus_dmamap_t		vr_rx_sparemap;
	int			vr_tx_pkts;
	int			vr_tx_prod;
	int			vr_tx_cons;
	int			vr_tx_cnt;
	int			vr_rx_cons;
};

struct vr_ring_data {
	struct vr_desc		*vr_rx_ring;
	struct vr_desc		*vr_tx_ring;
	bus_addr_t		vr_rx_ring_paddr;
	bus_addr_t		vr_tx_ring_paddr;
};

struct vr_statistics {
	uint64_t		tx_ok;
	uint64_t		rx_ok;
	uint32_t		tx_errors;
	uint32_t		rx_errors;
	uint32_t		rx_no_buffers;
	uint32_t		rx_no_mbufs;
	uint32_t		rx_crc_errors;
	uint32_t		rx_alignment;
	uint32_t		rx_fifo_overflows;
	uint32_t		rx_giants;
	uint32_t		rx_runts;
	uint32_t		tx_abort;
	uint32_t		tx_collisions;
	uint32_t		tx_late_collisions;
	uint32_t		tx_underrun;
	uint32_t		bus_errors;
	uint32_t		num_restart;
};

struct vr_softc {
	struct ifnet		*vr_ifp;	/* interface info */
	device_t		vr_dev;
	struct resource		*vr_res;
	int			vr_res_id;
	int			vr_res_type;
	struct resource		*vr_irq;
	void			*vr_intrhand;
	device_t		vr_miibus;
	uint8_t			vr_revid;	/* Rhine chip revision */
	int			vr_flags;	/* See VR_F_* below */
#define	VR_F_RESTART		0x0001		/* Restart unit on next tick */
#define	VR_F_TXPAUSE		0x0010
#define	VR_F_SUSPENDED		0x2000
#define	VR_F_DETACHED		0x4000
#define	VR_F_LINK		0x8000
	int			vr_if_flags;
	struct vr_chain_data	vr_cdata;
	struct vr_ring_data	vr_rdata;
	struct vr_statistics	vr_stat;
	struct callout		vr_stat_callout;
	struct mtx		vr_mtx;
	int			vr_quirks;
	int			vr_watchdog_timer;
	int			vr_txthresh;
#ifdef DEVICE_POLLING
	int			rxcycles;
#endif
	struct task		vr_inttask;
};

#define	VR_LOCK(_sc)		mtx_lock(&(_sc)->vr_mtx)
#define	VR_UNLOCK(_sc)		mtx_unlock(&(_sc)->vr_mtx)
#define	VR_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->vr_mtx, MA_OWNED)

/*
 * register space access macros
 */
#define CSR_WRITE_4(sc, reg, val)	bus_write_4(sc->vr_res, reg, val)
#define CSR_WRITE_2(sc, reg, val)	bus_write_2(sc->vr_res, reg, val)
#define CSR_WRITE_1(sc, reg, val)	bus_write_1(sc->vr_res, reg, val)

#define CSR_READ_2(sc, reg)		bus_read_2(sc->vr_res, reg)
#define CSR_READ_1(sc, reg)		bus_read_1(sc->vr_res, reg)

#define VR_SETBIT(sc, reg, x) CSR_WRITE_1(sc, reg, CSR_READ_1(sc, reg) | (x))
#define VR_CLRBIT(sc, reg, x) CSR_WRITE_1(sc, reg, CSR_READ_1(sc, reg) & ~(x))

#define VR_SETBIT16(sc, reg, x) CSR_WRITE_2(sc, reg, CSR_READ_2(sc, reg) | (x))
#define VR_CLRBIT16(sc, reg, x) CSR_WRITE_2(sc, reg, CSR_READ_2(sc, reg) & ~(x))

#define	VR_MCAST_CAM	0
#define	VR_VLAN_CAM	1
