/*	$OpenBSD: yukonreg.h,v 1.2 2003/08/12 05:23:06 nate Exp $ */
/*-
 * Copyright (c) 2003 Nathan L. Binkert <binkertn@umich.edu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */

/* General Purpose Status Register (GPSR) */
#define YUKON_GPSR		0x0000

#define YU_GPSR_SPEED		0x8000	/* speed 0 - 10Mbps, 1 - 100Mbps */
#define YU_GPSR_DUPLEX		0x4000	/* 0 - half duplex, 1 - full duplex */
#define YU_GPSR_FCTL_TX		0x2000	/* Tx flow control, 1 - disabled */
#define YU_GPSR_LINK		0x1000	/* link status (down/up) */
#define YU_GPSR_PAUSE		0x0800	/* flow control enable/disable */
#define YU_GPSR_TX_IN_PROG	0x0400	/* transmit in progress */
#define YU_GPSR_EXCESS_COL	0x0200	/* excessive collisions occurred */
#define YU_GPSR_LATE_COL	0x0100	/* late collision occurred */
#define YU_GPSR_MII_PHY_STC	0x0020	/* MII PHY status change */
#define YU_GPSR_GIG_SPEED	0x0010	/* Gigabit Speed (0 - use speed bit) */
#define YU_GPSR_PARTITION	0x0008	/* partition mode */
#define YU_GPSR_FCTL_RX		0x0004	/* Rx flow control, 1 - disabled */
#define YU_GPSR_PROMS_EN	0x0002	/* promiscuous mode, 1 - enabled */

/* General Purpose Control Register (GPCR) */
#define YUKON_GPCR		0x0004

#define YU_GPCR_FCTL_TX_DIS	0x2000	/* Disable Tx flow control 802.3x */
#define YU_GPCR_TXEN		0x1000	/* Transmit Enable */
#define YU_GPCR_RXEN		0x0800	/* Receive Enable */
#define YU_GPCR_BURSTEN		0x0400	/* Burst Mode Enable */
#define YU_GPCR_LPBK		0x0200	/* MAC Loopback Enable */
#define YU_GPCR_PAR		0x0100	/* Partition Enable */
#define YU_GPCR_GIG		0x0080	/* Gigabit Speed 1000Mbps */
#define YU_GPCR_FLP		0x0040	/* Force Link Pass */
#define YU_GPCR_DUPLEX		0x0020	/* Duplex Enable */
#define YU_GPCR_FCTL_RX_DIS	0x0010	/* Disable Rx flow control 802.3x */
#define YU_GPCR_SPEED		0x0008	/* Port Speed 100Mbps */
#define YU_GPCR_DPLX_DIS	0x0004	/* Disable Auto-Update for duplex */
#define YU_GPCR_FCTL_DIS	0x0002	/* Disable Auto-Update for 802.3x */
#define YU_GPCR_SPEED_DIS	0x0001	/* Disable Auto-Update for speed */

/* Transmit Control Register (TCR) */
#define YUKON_TCR		0x0008

#define YU_TCR_FJ		0x8000	/* force jam / flow control */
#define YU_TCR_CRCD		0x4000	/* insert CRC (0 - enable) */
#define YU_TCR_PADD		0x2000	/* pad packets to 64b (0 - enable) */
#define YU_TCR_COLTH		0x1c00	/* collision threshold */

/* Receive Control Register (RCR) */
#define YUKON_RCR		0x000c

#define YU_RCR_UFLEN		0x8000	/* unicast filter enable */
#define YU_RCR_MUFLEN		0x4000	/* multicast filter enable */
#define YU_RCR_CRCR		0x2000	/* remove CRC */
#define YU_RCR_PASSFC		0x1000	/* pass flow control packets */

/* Transmit Flow Control Register (TFCR) */
#define YUKON_TFCR		0x0010	/* Pause Time */

/* Transmit Parameter Register (TPR) */
#define YUKON_TPR		0x0014

#define YU_TPR_JAM_LEN(x)	(((x) & 0x3) << 14)
#define YU_TPR_JAM_IPG(x)	(((x) & 0x1f) << 9)
#define YU_TPR_JAM2DATA_IPG(x)	(((x) & 0x1f) << 4)

/* Serial Mode Register (SMR) */
#define YUKON_SMR		0x0018

#define YU_SMR_DATA_BLIND(x)	(((x) & 0x1f) << 11)
#define YU_SMR_LIMIT4		0x0400	/* reset after 16 / 4 collisions */
#define YU_SMR_MFL_JUMBO	0x0100	/* max frame length for jumbo frames */
#define YU_SMR_MFL_VLAN		0x0200	/* max frame length + vlan tag */
#define YU_SMR_IPG_DATA(x)	((x) & 0x1f)

/* Source Address Low #1 (SAL1) */
#define YUKON_SAL1		0x001c	/* SA1[15:0] */

/* Source Address Middle #1 (SAM1) */
#define YUKON_SAM1		0x0020	/* SA1[31:16] */

/* Source Address High #1 (SAH1) */
#define YUKON_SAH1		0x0024	/* SA1[47:32] */

/* Source Address Low #2 (SAL2) */
#define YUKON_SAL2		0x0028	/* SA2[15:0] */

/* Source Address Middle #2 (SAM2) */
#define YUKON_SAM2		0x002c	/* SA2[31:16] */

/* Source Address High #2 (SAH2) */
#define YUKON_SAH2		0x0030	/* SA2[47:32] */

/* Multicatst Address Hash Register 1 (MCAH1) */
#define YUKON_MCAH1		0x0034

/* Multicatst Address Hash Register 2 (MCAH2) */
#define YUKON_MCAH2		0x0038

/* Multicatst Address Hash Register 3 (MCAH3) */
#define YUKON_MCAH3		0x003c

/* Multicatst Address Hash Register 4 (MCAH4) */
#define YUKON_MCAH4		0x0040

/* Transmit Interrupt Register (TIR) */
#define YUKON_TIR		0x0044

#define YU_TIR_OUT_UNICAST	0x0001	/* Num Unicast Packets Transmitted */
#define YU_TIR_OUT_BROADCAST	0x0002	/* Num Broadcast Packets Transmitted */
#define YU_TIR_OUT_PAUSE	0x0004	/* Num Pause Packets Transmitted */
#define YU_TIR_OUT_MULTICAST	0x0008	/* Num Multicast Packets Transmitted */
#define YU_TIR_OUT_OCTETS	0x0030	/* Num Bytes Transmitted */
#define YU_TIR_OUT_64_OCTETS	0x0000	/* Num Packets Transmitted */
#define YU_TIR_OUT_127_OCTETS	0x0000	/* Num Packets Transmitted */
#define YU_TIR_OUT_255_OCTETS	0x0000	/* Num Packets Transmitted */
#define YU_TIR_OUT_511_OCTETS	0x0000	/* Num Packets Transmitted */
#define YU_TIR_OUT_1023_OCTETS	0x0000	/* Num Packets Transmitted */
#define YU_TIR_OUT_1518_OCTETS	0x0000	/* Num Packets Transmitted */
#define YU_TIR_OUT_MAX_OCTETS	0x0000	/* Num Packets Transmitted */
#define YU_TIR_OUT_SPARE	0x0000	/* Num Packets Transmitted */
#define YU_TIR_OUT_COLLISIONS	0x0000	/* Num Packets Transmitted */
#define YU_TIR_OUT_LATE		0x0000	/* Num Packets Transmitted */

/* Receive Interrupt Register (RIR) */
#define YUKON_RIR		0x0048

/* Transmit and Receive Interrupt Register (TRIR) */
#define YUKON_TRIR		0x004c

/* Transmit Interrupt Mask Register (TIMR) */
#define YUKON_TIMR		0x0050

/* Receive Interrupt Mask Register (RIMR) */
#define YUKON_RIMR		0x0054

/* Transmit and Receive Interrupt Mask Register (TRIMR) */
#define YUKON_TRIMR		0x0058

/* SMI Control Register (SMICR) */
#define YUKON_SMICR		0x0080

#define YU_SMICR_PHYAD(x)	(((x) & 0x1f) << 11)
#define YU_SMICR_REGAD(x)	(((x) & 0x1f) << 6)
#define YU_SMICR_OPCODE		0x0020	/* opcode (0 - write, 1 - read) */
#define YU_SMICR_OP_READ	0x0020	/* opcode read */
#define YU_SMICR_OP_WRITE	0x0000	/* opcode write */
#define YU_SMICR_READ_VALID	0x0010	/* read valid */
#define YU_SMICR_BUSY		0x0008	/* busy (writing) */

/* SMI Data Register (SMIDR) */
#define YUKON_SMIDR		0x0084

/* PHY Address Register (PAR) */
#define YUKON_PAR		0x0088

#define YU_PAR_MIB_CLR		0x0020	/* MIB Counters Clear Mode */
#define YU_PAR_LOAD_TSTCNT	0x0010	/* Load count 0xfffffff0 into cntr */

/* Receive status */
#define	YU_RXSTAT_FOFL		0x00000001	/* Rx FIFO overflow */
#define	YU_RXSTAT_CRCERR	0x00000002	/* CRC error */
#define	YU_RXSTAT_FRAGMENT	0x00000008	/* fragment */
#define	YU_RXSTAT_LONGERR	0x00000010	/* too long packet */
#define YU_RXSTAT_MIIERR	0x00000020	/* MII error */
#define	YU_RXSTAT_BADFC		0x00000040	/* bad flow-control packet */
#define	YU_RXSTAT_GOODFC	0x00000080	/* good flow-control packet */
#define YU_RXSTAT_RXOK		0x00000100	/* receice OK (Good packet) */
#define	YU_RXSTAT_BROADCAST	0x00000200	/* broadcast packet */
#define	YU_RXSTAT_MULTICAST	0x00000400	/* multicast packet */
#define	YU_RXSTAT_RUNT		0x00000800	/* undersize packet */
#define	YU_RXSTAT_JABBER	0x00001000	/* jabber packet */
#define	YU_RXSTAT_VLAN		0x00002000	/* VLAN packet */
#define	YU_RXSTAT_LENSHIFT	16

#define	YU_RXSTAT_BYTES(x)	((x) >> YU_RXSTAT_LENSHIFT)
