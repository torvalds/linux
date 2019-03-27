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

#define NGE_CSR			0x00
#define NGE_CFG			0x04
#define NGE_MEAR		0x08
#define NGE_PCITST		0x0C
#define NGE_ISR			0x10
#define NGE_IMR			0x14
#define NGE_IER			0x18
#define NGE_IHR			0x1C
#define NGE_TX_LISTPTR_LO	0x20
#define NGE_TX_LISTPTR_HI	0x24
#define NGE_TX_LISTPTR		NGE_TX_LISTPTR_LO
#define NGE_TX_CFG		0x28
#define NGE_GPIO		0x2C
#define NGE_RX_LISTPTR_LO	0x30
#define NGE_RX_LISTPTR_HI	0x34
#define NGE_RX_LISTPTR		NGE_RX_LISTPTR_LO
#define NGE_RX_CFG		0x38
#define NGE_PRIOQCTL		0x3C
#define NGE_WOLCSR		0x40
#define NGE_PAUSECSR		0x44
#define NGE_RXFILT_CTL		0x48
#define NGE_RXFILT_DATA		0x4C
#define NGE_BOOTROM_ADDR	0x50
#define NGE_BOOTROM_DATA	0x54
#define NGE_SILICONREV		0x58
#define NGE_MIBCTL		0x5C
#define NGE_MIB_RXERRPKT	0x60
#define NGE_MIB_RXERRFCS	0x64
#define NGE_MIB_RXERRMISSEDPKT	0x68
#define NGE_MIB_RXERRALIGN	0x6C
#define NGE_MIB_RXERRSYM	0x70
#define NGE_MIB_RXERRGIANT	0x74
#define NGE_MIB_RXERRRANGLEN	0x78
#define NGE_MIB_RXBADOPCODE	0x7C
#define NGE_MIB_RXPAUSEPKTS	0x80
#define NGE_MIB_TXPAUSEPKTS	0x84
#define NGE_MIB_TXERRSQE	0x88
#define NGE_TXPRIOQ_PTR1	0xA0
#define NGE_TXPRIOQ_PTR2	0xA4
#define NGE_TXPRIOQ_PTR3	0xA8
#define NGE_RXPRIOQ_PTR1	0xB0
#define NGE_RXPRIOQ_PTR2	0xB4
#define NGE_RXPRIOQ_PTR3	0xB8
#define NGE_VLAN_IP_RXCTL	0xBC
#define NGE_VLAN_IP_TXCTL	0xC0
#define NGE_VLAN_DATA		0xC4
#define NGE_CLKRUN		0xCC
#define NGE_TBI_BMCR		0xE0
#define NGE_TBI_BMSR		0xE4
#define NGE_TBI_ANAR		0xE8
#define NGE_TBI_ANLPAR		0xEC
#define NGE_TBI_ANER		0xF0
#define NGE_TBI_ESR		0xF4

/* Control/status register */
#define NGE_CSR_TX_ENABLE	0x00000001
#define NGE_CSR_TX_DISABLE	0x00000002
#define NGE_CSR_RX_ENABLE	0x00000004
#define NGE_CSR_RX_DISABLE	0x00000008
#define NGE_CSR_TX_RESET	0x00000010
#define NGE_CSR_RX_RESET	0x00000020
#define NGE_CSR_SOFTINTR	0x00000080
#define NGE_CSR_RESET		0x00000100
#define NGE_CSR_TX_PRIOQ_ENB0	0x00000200
#define NGE_CSR_TX_PRIOQ_ENB1	0x00000400
#define NGE_CSR_TX_PRIOQ_ENB2	0x00000800
#define NGE_CSR_TX_PRIOQ_ENB3	0x00001000
#define NGE_CSR_RX_PRIOQ_ENB0	0x00002000
#define NGE_CSR_RX_PRIOQ_ENB1	0x00004000
#define NGE_CSR_RX_PRIOQ_ENB2	0x00008000
#define NGE_CSR_RX_PRIOQ_ENB3	0x00010000

/* Configuration register */
#define NGE_CFG_BIGENDIAN	0x00000001
#define NGE_CFG_EXT_125MHZ	0x00000002
#define NGE_CFG_BOOTROM_DIS	0x00000004
#define NGE_CFG_PERR_DETECT	0x00000008
#define NGE_CFG_DEFER_DISABLE	0x00000010
#define NGE_CFG_OUTOFWIN_TIMER	0x00000020
#define NGE_CFG_SINGLE_BACKOFF	0x00000040
#define NGE_CFG_PCIREQ_ALG	0x00000080
#define NGE_CFG_EXTSTS_ENB	0x00000100
#define NGE_CFG_PHY_DIS		0x00000200
#define NGE_CFG_PHY_RST		0x00000400
#define NGE_CFG_64BIT_ADDR_ENB	0x00000800
#define NGE_CFG_64BIT_DATA_ENB	0x00001000
#define NGE_CFG_64BIT_PCI_DET	0x00002000
#define NGE_CFG_64BIT_TARG	0x00004000
#define NGE_CFG_MWI_DIS		0x00008000
#define NGE_CFG_MRM_DIS		0x00010000
#define NGE_CFG_TMRTST		0x00020000
#define NGE_CFG_PHYINTR_SPD	0x00040000
#define NGE_CFG_PHYINTR_LNK	0x00080000
#define NGE_CFG_PHYINTR_DUP	0x00100000
#define NGE_CFG_MODE_1000	0x00400000
#define NGE_CFG_TBI_EN		0x01000000
#define NGE_CFG_DUPLEX_STS	0x10000000
#define NGE_CFG_SPEED_STS	0x60000000
#define NGE_CFG_LINK_STS	0x80000000

/* MII/EEPROM control register */
#define NGE_MEAR_EE_DIN		0x00000001
#define NGE_MEAR_EE_DOUT	0x00000002
#define NGE_MEAR_EE_CLK		0x00000004
#define NGE_MEAR_EE_CSEL	0x00000008
#define NGE_MEAR_MII_DATA	0x00000010
#define NGE_MEAR_MII_DIR	0x00000020
#define NGE_MEAR_MII_CLK	0x00000040

#define NGE_EECMD_WRITE		0x140
#define NGE_EECMD_READ		0x180
#define NGE_EECMD_ERASE		0x1c0

#define NGE_EE_NODEADDR		0xA

/* PCI control register */
#define NGE_PCICTL_SRAMADDR	0x0000001F
#define NGE_PCICTL_RAMTSTENB	0x00000020
#define NGE_PCICTL_TXTSTENB	0x00000040
#define NGE_PCICTL_RXTSTENB	0x00000080
#define NGE_PCICTL_BMTSTENB	0x00000200
#define NGE_PCICTL_RAMADDR	0x001F0000
#define NGE_PCICTL_ROMTIME	0x0F000000
#define NGE_PCICTL_DISCTEST	0x40000000

/* Interrupt/status register */
#define NGE_ISR_RX_OK		0x00000001
#define NGE_ISR_RX_DESC_OK	0x00000002
#define NGE_ISR_RX_ERR		0x00000004
#define NGE_ISR_RX_EARLY	0x00000008
#define NGE_ISR_RX_IDLE		0x00000010
#define NGE_ISR_RX_OFLOW	0x00000020
#define NGE_ISR_TX_OK		0x00000040
#define NGE_ISR_TX_DESC_OK	0x00000080
#define NGE_ISR_TX_ERR		0x00000100
#define NGE_ISR_TX_IDLE		0x00000200
#define NGE_ISR_TX_UFLOW	0x00000400
#define NGE_ISR_MIB_SERVICE	0x00000800
#define NGE_ISR_SOFTINTR	0x00001000
#define NGE_ISR_PME_EVENT	0x00002000
#define NGE_ISR_PHY_INTR	0x00004000
#define NGE_ISR_HIBITS		0x00008000
#define NGE_ISR_RX_FIFO_OFLOW	0x00010000
#define NGE_ISR_TGT_ABRT	0x00020000
#define NGE_ISR_BM_ABRT		0x00040000
#define NGE_ISR_SYSERR		0x00080000
#define NGE_ISR_PARITY_ERR	0x00100000
#define NGE_ISR_RX_RESET_DONE	0x00200000
#define NGE_ISR_TX_RESET_DONE	0x00400000
#define NGE_ISR_RX_PRIOQ_DESC0	0x00800000
#define NGE_ISR_RX_PRIOQ_DESC1	0x01000000
#define NGE_ISR_RX_PRIOQ_DESC2	0x02000000
#define NGE_ISR_RX_PRIOQ_DESC3	0x04000000
#define NGE_ISR_TX_PRIOQ_DESC0	0x08000000
#define NGE_ISR_TX_PRIOQ_DESC1	0x10000000
#define NGE_ISR_TX_PRIOQ_DESC2	0x20000000
#define NGE_ISR_TX_PRIOQ_DESC3	0x40000000

/* Interrupt mask register */
#define NGE_IMR_RX_OK		0x00000001
#define NGE_IMR_RX_DESC_OK	0x00000002
#define NGE_IMR_RX_ERR		0x00000004
#define NGE_IMR_RX_EARLY	0x00000008
#define NGE_IMR_RX_IDLE		0x00000010
#define NGE_IMR_RX_OFLOW	0x00000020
#define NGE_IMR_TX_OK		0x00000040
#define NGE_IMR_TX_DESC_OK	0x00000080
#define NGE_IMR_TX_ERR		0x00000100
#define NGE_IMR_TX_IDLE		0x00000200
#define NGE_IMR_TX_UFLOW	0x00000400
#define NGE_IMR_MIB_SERVICE	0x00000800
#define NGE_IMR_SOFTINTR	0x00001000
#define NGE_IMR_PME_EVENT	0x00002000
#define NGE_IMR_PHY_INTR	0x00004000
#define NGE_IMR_HIBITS		0x00008000
#define NGE_IMR_RX_FIFO_OFLOW	0x00010000
#define NGE_IMR_TGT_ABRT	0x00020000
#define NGE_IMR_BM_ABRT		0x00040000
#define NGE_IMR_SYSERR		0x00080000
#define NGE_IMR_PARITY_ERR	0x00100000
#define NGE_IMR_RX_RESET_DONE	0x00200000
#define NGE_IMR_TX_RESET_DONE	0x00400000
#define NGE_IMR_RX_PRIOQ_DESC0	0x00800000
#define NGE_IMR_RX_PRIOQ_DESC1	0x01000000
#define NGE_IMR_RX_PRIOQ_DESC2	0x02000000
#define NGE_IMR_RX_PRIOQ_DESC3	0x04000000
#define NGE_IMR_TX_PRIOQ_DESC0	0x08000000
#define NGE_IMR_TX_PRIOQ_DESC1	0x10000000
#define NGE_IMR_TX_PRIOQ_DESC2	0x20000000
#define NGE_IMR_TX_PRIOQ_DESC3	0x40000000

#define NGE_INTRS	\
	(NGE_IMR_RX_OFLOW|NGE_IMR_TX_UFLOW|NGE_IMR_TX_OK|\
	 NGE_IMR_TX_IDLE|NGE_IMR_RX_OK|NGE_IMR_RX_ERR|\
	 NGE_IMR_SYSERR|NGE_IMR_PHY_INTR|\
	 NGE_IMR_RX_IDLE|NGE_IMR_RX_FIFO_OFLOW)

/* Interrupt enable register */
#define NGE_IER_INTRENB		0x00000001

/* Interrupt moderation timer register */
#define NGE_IHR_HOLDOFF		0x000000FF
#define NGE_IHR_HOLDCTL		0x00000100

/* Transmit configuration register */
#define NGE_TXCFG_DRAIN_THRESH	0x000000FF /* 32-byte units */
#define NGE_TXCFG_FILL_THRESH	0x0000FF00 /* 32-byte units */
#define NGE_1000MB_BURST_DIS	0x00080000
#define NGE_TXCFG_DMABURST	0x00700000
#define NGE_TXCFG_ECRETRY	0x00800000
#define NGE_TXCFG_AUTOPAD	0x10000000
#define NGE_TXCFG_LOOPBK	0x20000000
#define NGE_TXCFG_IGN_HBEAT	0x40000000
#define NGE_TXCFG_IGN_CARR	0x80000000

#define NGE_TXCFG_DRAIN(x)	(((x) >> 5) & NGE_TXCFG_DRAIN_THRESH)
#define NGE_TXCFG_FILL(x)	((((x) >> 5) << 8) & NGE_TXCFG_FILL_THRESH)

#define NGE_TXDMA_1024BYTES	0x00000000
#define NGE_TXDMA_8BYTES	0x00100000
#define NGE_TXDMA_16BYTES	0x00200000
#define NGE_TXDMA_32BYTES	0x00300000
#define NGE_TXDMA_64BYTES	0x00400000
#define NGE_TXDMA_128BYTES	0x00500000
#define NGE_TXDMA_256BYTES	0x00600000
#define NGE_TXDMA_512BYTES	0x00700000

#define NGE_TXCFG	\
	(NGE_TXDMA_512BYTES|NGE_TXCFG_AUTOPAD|\
	 NGE_TXCFG_FILL(64)|NGE_TXCFG_DRAIN(6400))

/* GPIO register */
#define NGE_GPIO_GP1_OUT	0x00000001
#define NGE_GPIO_GP2_OUT	0x00000002
#define NGE_GPIO_GP3_OUT	0x00000004
#define NGE_GPIO_GP4_OUT	0x00000008
#define NGE_GPIO_GP5_OUT	0x00000010
#define NGE_GPIO_GP1_OUTENB	0x00000020
#define NGE_GPIO_GP2_OUTENB	0x00000040
#define NGE_GPIO_GP3_OUTENB	0x00000080
#define NGE_GPIO_GP4_OUTENB	0x00000100
#define NGE_GPIO_GP5_OUTENB	0x00000200
#define NGE_GPIO_GP1_IN		0x00000400
#define NGE_GPIO_GP2_IN		0x00000800
#define NGE_GPIO_GP3_IN		0x00001000
#define NGE_GPIO_GP4_IN		0x00002000
#define NGE_GPIO_GP5_IN		0x00004000

/* Receive configuration register */
#define NGE_RXCFG_DRAIN_THRESH	0x0000003E /* 8-byte units */
#define NGE_RXCFG_DMABURST	0x00700000
#define NGE_RXCFG_RX_RANGEERR	0x04000000 /* accept in-range err frames */
#define NGE_RXCFG_RX_GIANTS	0x08000000 /* accept packets > 1518 bytes */
#define NGE_RXCFG_RX_FDX	0x10000000 /* full duplex receive */
#define NGE_RXCFG_RX_NOCRC	0x20000000 /* strip CRC */
#define NGE_RXCFG_RX_RUNT	0x40000000 /* accept short frames */
#define NGE_RXCFG_RX_BADPKTS	0x80000000 /* accept error frames */

#define NGE_RXCFG_DRAIN(x)	((((x) >> 3) << 1) & NGE_RXCFG_DRAIN_THRESH)

#define NGE_RXDMA_1024BYTES	0x00000000
#define NGE_RXDMA_8BYTES	0x00100000
#define NGE_RXDMA_16BYTES	0x00200000
#define NGE_RXDMA_32YTES	0x00300000
#define NGE_RXDMA_64BYTES	0x00400000
#define NGE_RXDMA_128BYTES	0x00500000
#define NGE_RXDMA_256BYTES	0x00600000
#define NGE_RXDMA_512BYTES	0x00700000

/*
 * DP83820/DP83821 with H/W VLAN stripping does not accept short VLAN
 * tagged packets such as ARP, short icmp echo request, etc. It seems
 * that MAC checks frame length for VLAN tagged packets after stripping
 * the VLAN tag. For short VLAN tagged packets it would would be 56
 * (64 - CRC - VLAN info) bytes in length after stripping VLAN tag.
 * If the VLAN tag stripped frames are less than 60 bytes in length
 * the hardware think it received runt packets!
 * Therefore we should accept runt frames to get VLAN tagged ARP
 * packets. In addition, it is known that some revisions of
 * DP83820/DP83821 have another bug that prevent fragmented IP packets
 * from accepting. So we also should accept errored frames.
 */
#define NGE_RXCFG \
	(NGE_RXCFG_DRAIN(64) | NGE_RXDMA_256BYTES| \
	 NGE_RXCFG_RX_RANGEERR | NGE_RXCFG_RX_BADPKTS | NGE_RXCFG_RX_RUNT | \
	 NGE_RXCFG_RX_GIANTS | NGE_RXCFG_RX_NOCRC)

/* Priority queue control */
#define NGE_PRIOQCTL_TXPRIO_ENB	0x00000001
#define NGE_PRIOQCTL_TXFAIR_ENB	0x00000002
#define NGE_PRIOQCTL_RXPRIO	0x0000000C

#define NGE_RXPRIOQ_DISABLED	0x00000000
#define NGE_RXPRIOQ_TWOQS	0x00000004
#define NGE_RXPRIOQ_THREEQS	0x00000008
#define NGE_RXPRIOQ_FOURQS	0x0000000C

/* Wake On LAN command/status register */
#define NGE_WOLCSR_WAKE_ON_PHYINTR	0x00000001
#define NGE_WOLCSR_WAKE_ON_UNICAST	0x00000002
#define NGE_WOLCSR_WAKE_ON_MULTICAST	0x00000004
#define NGE_WOLCSR_WAKE_ON_BROADCAST	0x00000008
#define NGE_WOLCSR_WAKE_ON_ARP		0x00000010
#define NGE_WOLCSR_WAKE_ON_PAT0_MATCH	0x00000020
#define NGE_WOLCSR_WAKE_ON_PAT1_MATCH	0x00000040
#define NGE_WOLCSR_WAKE_ON_PAT2_MATCH	0x00000080
#define NGE_WOLCSR_WAKE_ON_PAT3_MATCH	0x00000100
#define NGE_WOLCSR_WAKE_ON_MAGICPKT	0x00000200
#define NGE_WOLCSR_SECUREON_ENB		0x00000400
#define NGE_WOLCSR_SECUREON_HACK	0x00200000
#define NGE_WOLCSR_PHYINTR		0x00400000
#define NGE_WOLCSR_UNICAST		0x00800000
#define NGE_WOLCSR_MULTICAST		0x01000000
#define NGE_WOLCSR_BROADCAST		0x02000000
#define NGE_WOLCSR_ARP_RCVD		0x04000000
#define NGE_WOLCSR_PAT0_MATCH		0x08000000
#define NGE_WOLCSR_PAT1_MATCH		0x10000000
#define NGE_WOLCSR_PAT2_MATCH		0x20000000
#define NGE_WOLCSR_PAT3_MATCH		0x40000000
#define NGE_WOLCSR_MAGICPKT		0x80000000

/* Pause control/status register */
#define NGE_PAUSECSR_CNT		0x0000FFFF
#define NGE_PAUSECSR_PFRAME_SENT	0x00020000
#define NGE_PAUSECSR_RX_DATAFIFO_THR_LO	0x000C0000
#define NGE_PAUSECSR_RX_DATAFIFO_THR_HI	0x00300000
#define NGE_PAUSECSR_RX_STATFIFO_THR_LO	0x00C00000
#define NGE_PAUSECSR_RX_STATFIFO_THR_HI	0x03000000
#define NGE_PAUSECSR_PFRAME_RCVD	0x08000000
#define NGE_PAUSECSR_PAUSE_ACTIVE	0x10000000
#define NGE_PAUSECSR_PAUSE_ON_DA	0x20000000 /* pause on direct addr */
#define NGE_PAUSECSR_PAUSE_ON_MCAST	0x40000000 /* pause on mcast */
#define NGE_PAUSECSR_PAUSE_ENB		0x80000000

/* Receive filter/match control message */
#define MGE_RXFILTCTL_ADDR	0x000003FF
#define NGE_RXFILTCTL_ULMASK	0x00080000
#define NGE_RXFILTCTL_UCHASH	0x00100000
#define NGE_RXFILTCTL_MCHASH	0x00200000
#define NGE_RXFILTCTL_ARP	0x00400000
#define NGE_RXFILTCTL_PMATCH0	0x00800000
#define NGE_RXFILTCTL_PMATCH1	0x01000000
#define NGE_RXFILTCTL_PMATCH2	0x02000000
#define NGE_RXFILTCTL_PMATCH3	0x04000000
#define NGE_RXFILTCTL_PERFECT	0x08000000
#define NGE_RXFILTCTL_ALLPHYS	0x10000000
#define NGE_RXFILTCTL_ALLMULTI	0x20000000
#define NGE_RXFILTCTL_BROAD	0x40000000
#define NGE_RXFILTCTL_ENABLE	0x80000000


#define NGE_FILTADDR_PAR0	0x00000000
#define NGE_FILTADDR_PAR1	0x00000002
#define NGE_FILTADDR_PAR2	0x00000004
#define NGE_FILTADDR_PMATCH0	0x00000006
#define NGE_FILTADDR_PMATCH1	0x00000008
#define NGE_FILTADDR_SOPASS0	0x0000000A
#define NGE_FILTADDR_SOPASS1	0x0000000C
#define NGE_FILTADDR_SOPASS2	0x0000000E
#define NGE_FILTADDR_FMEM_LO	0x00000100
#define NGE_FILTADDR_FMEM_HI	0x000003FE
#define NGE_FILTADDR_MCAST_LO	0x00000100 /* start of multicast filter */
#define NGE_FILTADDR_MCAST_HI	0x000001FE /* end of multicast filter */
#define NGE_MCAST_FILTER_LEN	256	   /* bytes */
#define NGE_FILTADDR_PBUF0	0x00000200 /* pattern buffer 0 */
#define NGE_FILTADDR_PBUF1	0x00000280 /* pattern buffer 1 */
#define NGE_FILTADDR_PBUF2	0x00000300 /* pattern buffer 2 */
#define NGE_FILTADDR_PBUF3	0x00000380 /* pattern buffer 3 */

/* MIB control register */
#define NGE_MIBCTL_WARNTEST	0x00000001
#define NGE_MIBCTL_FREEZE_CNT	0x00000002
#define NGE_MIBCTL_CLEAR_CNT	0x00000004
#define NGE_MIBCTL_STROBE_CNT	0x00000008

/* VLAN/IP RX control register */
#define NGE_VIPRXCTL_TAG_DETECT_ENB	0x00000001
#define NGE_VIPRXCTL_TAG_STRIP_ENB	0x00000002
#define NGE_VIPRXCTL_DROP_TAGGEDPKTS	0x00000004
#define NGE_VIPRXCTL_DROP_UNTAGGEDPKTS	0x00000008
#define NGE_VIPRXCTL_IPCSUM_ENB		0x00000010
#define NGE_VIPRXCTL_REJECT_BADIPCSUM	0x00000020
#define NGE_VIPRXCTL_REJECT_BADTCPCSUM	0x00000040
#define NGE_VIPRXCTL_REJECT_BADUDPCSUM	0x00000080

/* VLAN/IP TX control register */
#define NGE_VIPTXCTL_TAG_ALL		0x00000001
#define NGE_VIPTXCTL_TAG_PER_PKT	0x00000002
#define NGE_VIPTXCTL_CSUM_ALL		0x00000004
#define NGE_VIPTXCTL_CSUM_PER_PKT	0x00000008

/* VLAN data register */
#define NGE_VLANDATA_VTYPE	0x0000FFFF
#define NGE_VLANDATA_VTCI	0xFFFF0000

/* Clockrun register */
#define NGE_CLKRUN_PMESTS	0x00008000
#define NGE_CLKRUN_PMEENB	0x00000100
#define NGE_CLNRUN_CLKRUN_ENB	0x00000001


/* TBI BMCR */
#define NGE_TBIBMCR_RESTART_ANEG	0x00000200
#define NGE_TBIBMCR_ENABLE_ANEG		0x00001000
#define NGE_TBIBMCR_LOOPBACK		0x00004000

/* TBI BMSR */
#define NGE_TBIBMSR_ANEG_DONE	0x00000004
#define NGE_TBIBMSR_LINKSTAT	0x00000020

/* TBI ANAR */
#define NGE_TBIANAR_HDX		0x00000020
#define NGE_TBIANAR_FDX		0x00000040
#define NGE_TBIANAR_PS1		0x00000080
#define NGE_TBIANAR_PS2		0x00000100
#define NGE_TBIANAR_PCAP	0x00000180
#define NGE_TBIANAR_REMFAULT	0x00003000
#define NGE_TBIANAR_NEXTPAGE	0x00008000

/* TBI ANLPAR */
#define NGE_TBIANLPAR_HDX	0x00000020
#define NGE_TBIANLPAR_FDX	0x00000040
#define NGE_TBIANAR_PS1		0x00000080
#define NGE_TBIANAR_PS2		0x00000100
#define NGE_TBIANLPAR_PCAP	0x00000180
#define NGE_TBIANLPAR_REMFAULT	0x00003000
#define NGE_TBIANLPAR_NEXTPAGE	0x00008000

/* TBI ANER */
#define NGE_TBIANER_PAGERCVD	0x00000002
#define NGE_TBIANER_NEXTPGABLE	0x00000004

/* TBI EXTSTS */
#define NGE_TBIEXTSTS_HXD	0x00004000
#define NGE_TBIEXTSTS_FXD	0x00008000

/*
 * DMA descriptor structures. The RX and TX descriptor formats are
 * deliberately designed to be similar to facilitate passing them between
 * RX and TX queues on multiple controllers, in the case where you have
 * multiple MACs in a switching configuration. With the 83820, the pointer
 * values can be either 64 bits or 32 bits depending on how the chip is
 * configured. For the 83821, the fields are always 32-bits. There is
 * also an optional extended status field for VLAN and TCP/IP checksum
 * functions. We use the checksum feature so we enable the use of this
 * field. Descriptors must be 64-bit aligned.
 */
struct nge_desc_64 {
	/* Hardware descriptor section */
	uint32_t	nge_next_lo;
	uint32_t	nge_next_hi;
	uint32_t	nge_ptr_lo;
	uint32_t	nge_ptr_hi;
	uint32_t	nge_cmdsts;
	uint32_t	nge_extsts;
};

struct nge_desc_32 {
	/* Hardware descriptor section */
	uint32_t	nge_next;
	uint32_t	nge_ptr;
	uint32_t	nge_cmdsts;
	uint32_t	nge_extsts;
};

#define nge_desc	nge_desc_32

#define NGE_CMDSTS_BUFLEN	0x0000FFFF
#define NGE_CMDSTS_PKT_OK	0x08000000
#define NGE_CMDSTS_CRC		0x10000000
#define NGE_CMDSTS_INTR		0x20000000
#define NGE_CMDSTS_MORE		0x40000000
#define NGE_CMDSTS_OWN		0x80000000

#define NGE_INC(x, y)		(x) = ((x) + 1) % y

#define NGE_RXSTAT_RANGELENERR	0x00010000
#define NGE_RXSTAT_LOOPBK	0x00020000
#define NGE_RXSTAT_ALIGNERR	0x00040000
#define NGE_RXSTAT_CRCERR	0x00080000
#define NGE_RXSTAT_SYMBOLERR	0x00100000
#define NGE_RXSTAT_RUNT		0x00200000
#define NGE_RXSTAT_GIANT	0x00400000
#define NGE_RXSTAT_DSTCLASS	0x01800000
#define NGE_RXSTAT_OVERRUN	0x02000000
#define NGE_RXSTAT_RX_ABORT	0x04000000

#define NGE_DSTCLASS_REJECT	0x00000000
#define NGE_DSTCLASS_UNICAST	0x00800000
#define NGE_DSTCLASS_MULTICAST	0x01000000
#define NGE_DSTCLASS_BROADCAST	0x02000000

#define NGE_TXSTAT_COLLCNT	0x000F0000
#define NGE_TXSTAT_EXCESSCOLLS	0x00100000
#define NGE_TXSTAT_OUTOFWINCOLL	0x00200000
#define NGE_TXSTAT_EXCESS_DEFER	0x00400000
#define NGE_TXSTAT_DEFERED	0x00800000
#define NGE_TXSTAT_CARR_LOST	0x01000000
#define NGE_TXSTAT_UNDERRUN	0x02000000
#define NGE_TXSTAT_TX_ABORT	0x04000000

#define NGE_TXEXTSTS_VLAN_TCI	0x0000FFFF
#define NGE_TXEXTSTS_VLANPKT	0x00010000
#define NGE_TXEXTSTS_IPCSUM	0x00020000
#define NGE_TXEXTSTS_TCPCSUM	0x00080000
#define NGE_TXEXTSTS_UDPCSUM	0x00200000

#define NGE_RXEXTSTS_VTCI	0x0000FFFF
#define NGE_RXEXTSTS_VLANPKT	0x00010000
#define NGE_RXEXTSTS_IPPKT	0x00020000
#define NGE_RXEXTSTS_IPCSUMERR	0x00040000
#define NGE_RXEXTSTS_TCPPKT	0x00080000
#define NGE_RXEXTSTS_TCPCSUMERR	0x00100000
#define NGE_RXEXTSTS_UDPPKT	0x00200000
#define NGE_RXEXTSTS_UDPCSUMERR	0x00400000

#define NGE_TX_RING_CNT		256
#define NGE_RX_RING_CNT		256
#define	NGE_TX_RING_SIZE	sizeof(struct nge_desc) * NGE_TX_RING_CNT
#define	NGE_RX_RING_SIZE	sizeof(struct nge_desc) * NGE_RX_RING_CNT
#define	NGE_RING_ALIGN		sizeof(uint64_t)
#define	NGE_RX_ALIGN		sizeof(uint64_t)
#define	NGE_MAXTXSEGS		16

#define	NGE_ADDR_LO(x)		((uint64_t)(x) & 0xffffffff)
#define	NGE_ADDR_HI(x)		((uint64_t)(x) >> 32)
#define	NGE_TX_RING_ADDR(sc, i)	\
    ((sc)->nge_rdata.nge_tx_ring_paddr + sizeof(struct nge_desc) * (i))
#define	NGE_RX_RING_ADDR(sc, i)	\
    ((sc)->nge_rdata.nge_rx_ring_paddr + sizeof(struct nge_desc) * (i))

struct nge_txdesc {
	struct mbuf		*tx_m;
	bus_dmamap_t		tx_dmamap;
};

struct nge_rxdesc {
	struct mbuf		*rx_m;
	bus_dmamap_t		rx_dmamap;
};

struct nge_chain_data {
	bus_dma_tag_t		nge_parent_tag;
	bus_dma_tag_t		nge_tx_tag;
	struct nge_txdesc	nge_txdesc[NGE_TX_RING_CNT];
	bus_dma_tag_t		nge_rx_tag;
	struct nge_rxdesc	nge_rxdesc[NGE_RX_RING_CNT];
	bus_dma_tag_t		nge_tx_ring_tag;
	bus_dma_tag_t		nge_rx_ring_tag;
	bus_dmamap_t		nge_tx_ring_map;
	bus_dmamap_t		nge_rx_ring_map;
	bus_dmamap_t		nge_rx_sparemap;
	int			nge_tx_prod;
	int			nge_tx_cons;
	int			nge_tx_cnt;
	int			nge_rx_cons;
};

struct nge_ring_data {
	struct nge_desc		*nge_tx_ring;
	bus_addr_t		nge_tx_ring_paddr;
	struct nge_desc		*nge_rx_ring;
	bus_addr_t		nge_rx_ring_paddr;
};

/*
 * NatSemi PCI vendor ID.
 */
#define NGE_VENDORID		0x100B

/*
 * 83820/83821 PCI device IDs
 */
#define NGE_DEVICEID		0x0022

struct nge_type {
	uint16_t		nge_vid;
	uint16_t		nge_did;
	const char		*nge_name;
};

#define NGE_JUMBO_FRAMELEN	9022
#define NGE_JUMBO_MTU		\
	(NGE_JUMBO_FRAMELEN - sizeof(struct ether_vlan_header) - ETHER_CRC_LEN)

/* Statistics counters. */
struct nge_stats {
	uint32_t		rx_pkts_errs;
	uint32_t		rx_crc_errs;
	uint32_t		rx_fifo_oflows;
	uint32_t		rx_align_errs;
	uint32_t		rx_sym_errs;
	uint32_t		rx_pkts_jumbos;
	uint32_t		rx_len_errs;
	uint32_t		rx_unctl_frames;
	uint32_t		rx_pause;
	uint32_t		tx_pause;
	uint32_t		tx_seq_errs;
};

struct nge_softc {
	struct ifnet		*nge_ifp;
	device_t		nge_dev;
	struct resource		*nge_res;
	int			nge_res_type;
	int			nge_res_id;
	struct resource		*nge_irq;
	void			*nge_intrhand;
	device_t		nge_miibus;
	int			nge_if_flags;
	uint32_t		nge_flags;
#define	NGE_FLAG_TBI		0x0002
#define	NGE_FLAG_SUSPENDED	0x2000
#define	NGE_FLAG_DETACH		0x4000
#define	NGE_FLAG_LINK		0x8000
	struct nge_chain_data	nge_cdata;
	struct nge_ring_data	nge_rdata;
	struct callout		nge_stat_ch;
	struct nge_stats	nge_stats;
	struct mtx		nge_mtx;
#ifdef DEVICE_POLLING
	int			rxcycles;
#endif
	int			nge_watchdog_timer;
	int			nge_int_holdoff;
	struct mbuf		*nge_head;
	struct mbuf		*nge_tail;
};

#define	NGE_LOCK_INIT(_sc, _name) \
	mtx_init(&(_sc)->nge_mtx, _name, MTX_NETWORK_LOCK, MTX_DEF)
#define	NGE_LOCK(_sc)		mtx_lock(&(_sc)->nge_mtx)
#define	NGE_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->nge_mtx, MA_OWNED)
#define	NGE_UNLOCK(_sc)		mtx_unlock(&(_sc)->nge_mtx)
#define	NGE_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->nge_mtx)

/*
 * register space access macros
 */
#define CSR_WRITE_4(sc, reg, val)	\
	bus_write_4((sc)->nge_res, reg, val)

#define CSR_BARRIER_4(sc, reg, flags)	\
	bus_barrier((sc)->nge_res, reg, 4, flags)

#define CSR_READ_4(sc, reg)		\
	bus_read_4((sc)->nge_res, reg)

#define NGE_TIMEOUT		1000

#define	NGE_INT_HOLDOFF_DEFAULT	1
#define	NGE_INT_HOLDOFF_MIN	0
#define	NGE_INT_HOLDOFF_MAX	255

/*
 * PCI low memory base and low I/O base register, and
 * other PCI registers.
 */

#define NGE_PCI_VENDOR_ID	0x00
#define NGE_PCI_DEVICE_ID	0x02
#define NGE_PCI_COMMAND		0x04
#define NGE_PCI_STATUS		0x06
#define NGE_PCI_REVID		0x08
#define NGE_PCI_CLASSCODE	0x09
#define NGE_PCI_CACHELEN	0x0C
#define NGE_PCI_LATENCY_TIMER	0x0D
#define NGE_PCI_HEADER_TYPE	0x0E
#define NGE_PCI_LOIO		0x10
#define NGE_PCI_LOMEM		0x14
#define NGE_PCI_BIOSROM		0x30
#define NGE_PCI_INTLINE		0x3C
#define NGE_PCI_INTPIN		0x3D
#define NGE_PCI_MINGNT		0x3E
#define NGE_PCI_MINLAT		0x0F
#define NGE_PCI_RESETOPT	0x48
#define NGE_PCI_EEPROM_DATA	0x4C

/* power management registers */
#define NGE_PCI_CAPID		0x50 /* 8 bits */
#define NGE_PCI_NEXTPTR		0x51 /* 8 bits */
#define NGE_PCI_PWRMGMTCAP	0x52 /* 16 bits */
#define NGE_PCI_PWRMGMTCTRL	0x54 /* 16 bits */

#define NGE_PSTATE_MASK		0x0003
#define NGE_PSTATE_D0		0x0000
#define NGE_PSTATE_D1		0x0001
#define NGE_PSTATE_D2		0x0002
#define NGE_PSTATE_D3		0x0003
#define NGE_PME_EN		0x0010
#define NGE_PME_STATUS		0x8000
