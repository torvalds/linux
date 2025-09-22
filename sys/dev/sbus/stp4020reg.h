/*	$OpenBSD: stp4020reg.h,v 1.7 2008/06/26 05:42:18 ray Exp $	*/
/*	$NetBSD: stp4020reg.h,v 1.1 1998/11/22 22:14:35 pk Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef _STP4020_REG_H
#define	_STP4020_REG_H

/*
 * STP4020: SBus/PCMCIA bridge supporting one Type-3 PCMCIA card, or up to
 * two Type-1 and Type-2 PCMCIA cards..
 * Programming information source:
 *	- http://www.sun.com/microelectronics/datasheets/stp4020/
 *	- SunOS 5.5 header file
 */

/*
 * General chip attributes.
 */
#define	STP4020_NSOCK	2	/* number of PCCARD sockets per STP4020 */
#define	STP4020_NWIN	3	/* number of windows per socket */

/*
 * Socket control registers.
 *
 * Each PCMCIA socket has two interface control registers and two interface
 * status registers associated with it.
 */

/*
 * Socket Interface Control register 0
 */
#define	STP4020_ICR0_rsvd1	0xc000	/* reserved bits */
#define	STP4020_ICR0_PROMEN	0x2000	/* FCode PROM enable */
/* Status change interrupts can be routed to one of two SBus interrupt levels:*/
#define	STP4020_ICR0_SCILVL	0x1000	/* card status change interrupt level */
#define	 STP4020_ICR0_SCILVL_SB0	0x0000	/* interrupt on *SB_INT[0] */
#define	 STP4020_ICR0_SCILVL_SB1	0x1000	/* interrupt on *SB_INT[1] */
/* Interrupt enable bits: */
#define	STP4020_ICR0_CDIE	0x0800	/* card detect interrupt enable */
#define	STP4020_ICR0_BVD2IE	0x0400	/* battery voltage detect 2 int en. */
#define	STP4020_ICR0_BVD1IE	0x0200	/* battery voltage detect 1 int en. */
#define	STP4020_ICR0_RDYIE	0x0100	/* ready/busy interrupt enable */
#define	STP4020_ICR0_WPIE	0x0080	/* write protect interrupt enable */
#define	STP4020_ICR0_CTOIE	0x0040	/* PC card timeout interrupt enable */
#define	STP4020_ICR0_rsvd2	0x0020	/* */
#define	STP4020_ICR0_IOIE	0x0010	/* I/O (*IRQ) interrupt enable */
/* PC card I/O interrupts can also be routed to one of two SBus intr levels: */
#define	STP4020_ICR0_IOILVL	0x0008	/* I/O (*IRQ) interrupt level (SBus) */
#define	 STP4020_ICR0_IOILVL_SB0	0x0000	/* interrupt on *SB_INT[0] */
#define	 STP4020_ICR0_IOILVL_SB1	0x0008	/* interrupt on *SB_INT[1] */

#define	STP4020_ICR0_SPKREN	0x0004	/* *SPKR_OUT enable */
#define	STP4020_ICR0_RESET	0x0002	/* PC card reset */
#define	STP4020_ICR0_IFTYPE	0x0001	/* PC card interface type */
#define	 STP4020_ICR0_IFTYPE_MEM	0x0000	/* MEMORY only */
#define	 STP4020_ICR0_IFTYPE_IO		0x0001	/* MEMORY and I/O */
#define STP4020_ICR0_BITS	"\010\1IFTYPE\2RESET\3SPKREN\4IOILVL\5IOIE" \
    "\7CTOIE\10WPIE\11RDYIE\12BVD1IE\13BVD2IE\14CDIE\15SCILV\16PROMEN"

/* Shorthand for all status change interrupts enables */
#define	STP4020_ICR0_ALL_STATUS_IE (	\
	STP4020_ICR0_CDIE |		\
	STP4020_ICR0_BVD2IE |		\
	STP4020_ICR0_BVD1IE |		\
	STP4020_ICR0_RDYIE |		\
	STP4020_ICR0_WPIE |		\
	STP4020_ICR0_CTOIE		\
)

/*
 * Socket Interface Control register 1
 */
#define	STP4020_ICR1_LPBKEN	0x8000	/* PC card data loopback enable */
#define	STP4020_ICR1_CD1DB	0x4000	/* card detect 1 diagnostic bit */
#define	STP4020_ICR1_BVD2DB	0x2000	/* battery voltage detect 2 diag bit */
#define	STP4020_ICR1_BVD1DB	0x1000	/* battery voltage detect 1 diag bit */
#define	STP4020_ICR1_RDYDB	0x0800	/* ready/busy diagnostic bit */
#define	STP4020_ICR1_WPDB	0x0400	/* write protect diagnostic bit */
#define	STP4020_ICR1_WAITDB	0x0200	/* *WAIT diagnostic bit */
#define	STP4020_ICR1_DIAGEN	0x0100	/* diagnostic enable bit */
#define	STP4020_ICR1_rsvd1	0x0080	/* reserved */
#define	STP4020_ICR1_APWREN	0x0040	/* PC card auto power switch enable */

/*
 * The Vpp controls are two-bit fields which specify which voltage
 * should be switched onto Vpp for this socket.
 *
 * Both of the "no connect" states are equal.
 */
#define	STP4020_ICR1_VPP2EN	0x0030	/* Vpp2 power enable */
#define	 STP4020_ICR1_VPP2_OFF	0x0000	/* no connect */
#define	 STP4020_ICR1_VPP2_VCC	0x0010	/* Vcc switched onto Vpp2 */
#define	 STP4020_ICR1_VPP2_VPP	0x0020	/* Vpp switched onto Vpp2 */
#define	 STP4020_ICR1_VPP2_ZIP	0x0030	/* no connect */

#define	STP4020_ICR1_VPP1EN	0x000c	/* Vpp1 power enable */
#define	 STP4020_ICR1_VPP1_OFF	0x0000	/* no connect */
#define	 STP4020_ICR1_VPP1_VCC	0x0004	/* Vcc switched onto Vpp1 */
#define	 STP4020_ICR1_VPP1_VPP	0x0008	/* Vpp switched onto Vpp1 */
#define	 STP4020_ICR1_VPP1_ZIP	0x000c	/* no connect */

#define	STP4020_ICR1_MSTPWR	0x0002	/* PC card master power enable */
#define	STP4020_ICR1_PCIFOE	0x0001	/* PC card interface output enable */

#define STP4020_ICR1_BITS	"\010\1PCIFOE\2MSTPWR\7APWREN\11DIAGEN" \
    "\12WAITDB\13WPDB\14RDYDB\15BVD1D\16BVD2D\17CD1DB\18LPBKEN"

/*
 * Socket Interface Status register 0
 *
 * Some signals in this register change meaning depending on whether
 * the socket is configured as MEMORY-ONLY or MEMORY & I/O:
 *	mo: valid only if the socket is in memory-only mode
 *	io: valid only if the socket is in memory and I/O mode.
 *
 * Pending interrupts are cleared by writing the corresponding status
 * bit set in the upper half of this register.
 */
#define	STP4020_ISR0_ZERO	0x8000	/* always reads back as zero (mo) */
#define	STP4020_ISR0_IOINT	0x8000	/* PC card I/O intr (*IRQ) posted (io)*/
#define	STP4020_ISR0_SCINT	0x4000	/* status change interrupt posted */
#define	STP4020_ISR0_CDCHG	0x2000	/* card detect status change */
#define	STP4020_ISR0_BVD2CHG	0x1000	/* battery voltage detect 2 status change */
#define	STP4020_ISR0_BVD1CHG	0x0800	/* battery voltage detect 1 status change */
#define	STP4020_ISR0_RDYCHG	0x0400	/* ready/busy status change */
#define	STP4020_ISR0_WPCHG	0x0200	/* write protect status change */
#define	STP4020_ISR0_PCTO	0x0100	/* PC card access timeout */
#define STP4020_ISR0_ALL_STATUS_IRQ	0x7f00

#define	STP4020_ISR0_LIVE	0x00ff	/* live status bit mask */
#define	STP4020_ISR0_CD2ST	0x0080	/* card detect 2 live status */
#define	STP4020_ISR0_CD1ST	0x0040	/* card detect 1 live status */
#define	STP4020_ISR0_BVD2ST	0x0020	/* battery voltage detect 2 live status (mo) */
#define	STP4020_ISR0_SPKR	0x0020	/* SPKR signal live status (io)*/
#define	STP4020_ISR0_BVD1ST	0x0010	/* battery voltage detect 1 live status (mo) */
#define	STP4020_ISR0_STSCHG	0x0010	/* I/O *STSCHG signal live status (io)*/
#define	STP4020_ISR0_RDYST	0x0008	/* ready/busy live status (mo) */
#define	STP4020_ISR0_IOREQ	0x0008	/* I/O *REQ signal live status (io) */
#define	STP4020_ISR0_WPST	0x0004	/* write protect live status (mo) */
#define	STP4020_ISR0_IOIS16	0x0004	/* IOIS16 signal live status (io) */
#define	STP4020_ISR0_WAITST	0x0002	/* wait signal live status */
#define	STP4020_ISR0_PWRON	0x0001	/* PC card power status */

#define STP4020_ISR0_IOBITS	"\010\1PWRON\2WAITST\3IOIS16\4IOREQ" \
    "\5STSCHG\6SPKR\7CD1ST\10CD2ST\11PCTO\12WPCHG\13RDYCHG\14BVD1CHG" \
    "\15BVD2CHG\16CDCHG\17SCINT\20IOINT"
#define STP4020_ISR0_MOBITS	"\010\1PWRON\2WAITST\3WPST\4RDYST" \
    "\5BVD1ST\6BVD2ST\7CD1ST\10CD2ST\11PCTO\12WPCHG\13RDYCHG\14BVD1CHG" \
    "\15BVD2CHG\16CDCHG\17SCINT"

/*
 * Socket Interface Status register 1
 */
#define	STP4020_ISR1_rsvd	0xffc0	/* reserved */
#define	STP4020_ISR1_PCTYPE_M	0x0030	/* PC card type(s) supported bit mask */
#define	STP4020_ISR1_PCTYPE_S	4	/* PC card type(s) supported bit shift */
#define	STP4020_ISR1_REV_M	0x000f	/* ASIC revision level bit mask */
#define	STP4020_ISR1_REV_S	0	/* ASIC revision level bit shift */


/*
 * Socket window control/status register definitions.
 *
 * According to SunOS 5.5:
 *	"Each PCMCIA socket has three windows associated with it; each of
 *	these windows can be programmed to map in either the AM, CM or IO
 *	space on the PC card.  Each window can also be programmed with a
 *	starting or base address relative to the PC card's address zero.
 *	Each window is a fixed 1Mb in size.
 *
 *	Each window has two window control registers associated with it to
 *	control the window's PCMCIA bus timing parameters, PC card address
 *	space that the window maps, and the base address in the
 *	selected PC card's address space."
 */
#define	STP4020_WINDOW_SIZE		(1024*1024) /* 1MB */
#define	STP4020_WINDOW_SHIFT	20	/* for 1MB */

/*
 * PC card Window Control register 0
 */
#define	STP4020_WCR0_rsvd	0x8000	/* reserved */
#define	STP4020_WCR0_CMDLNG_M	0x7c00	/* command strobe length bit mask */
#define	STP4020_WCR0_CMDLNG_S	10	/* command strobe length bit shift */
#define	STP4020_WCR0_CMDDLY_M	0x0300	/* command strobe delay bit mask */
#define	STP4020_WCR0_CMDDLY_S	8	/* command strobe delay bit shift */
#define	STP4020_MEM_SPEED_MIN	100
#define	STP4020_MEM_SPEED_MAX	1370
/*
 * The ASPSEL (Address Space Select) bits control which of the three PC card
 * address spaces this window maps in.
 */
#define	STP4020_WCR0_ASPSEL_M	0x00c0	/* address space select bit mask */
#define	 STP4020_WCR0_ASPSEL_AM	0x0000	/* attribute memory */
#define	 STP4020_WCR0_ASPSEL_CM	0x0040	/* common memory */
#define	 STP4020_WCR0_ASPSEL_IO	0x0080	/* I/O */
/*
 * The base address controls which 1MB range in the 64MB card address space
 * this window maps to.
 */
#define	STP4020_WCR0_BASE_M	0x0003f	/* base address bit mask */
#define	STP4020_WCR0_BASE_S	0	/* base address bit shift */

#define	STP4020_ADDR2PAGE(x)	((x) >> 20)

/*
 * PC card Window Control register 1
 */
#define	STP4020_WCR1_rsvd	0xffe0	/* reserved */
#define	STP4020_WCR1_RECDLY_M	0x0018	/* recovery delay bit mask */
#define	STP4020_WCR1_RECDLY_S	3	/* recovery delay bit shift */
#define	STP4020_WCR1_WAITDLY_M	0x0006	/* *WAIT signal delay bit mask */
#define	STP4020_WCR1_WAITDLY_S	1	/* *WAIT signal delay bit shift */
#define	STP4020_WCR1_WAITREQ_M	0x0001	/* *WAIT signal is required bit mask */
#define	STP4020_WCR1_WAITREQ_S	0	/* *WAIT signal is required bit shift */

#if for_reference_only
/*
 * STP4020 CSR structures
 *
 * There is one stp4020_regs_t structure per instance, and it refers to
 *	the complete Stp4020 register set.
 *
 * For each socket, there is one stp4020_socket_csr_t structure, which
 *	refers to all the registers for that socket.  That structure is
 *	made up of the window register structures as well as the registers
 *	that control overall socket operation.
 *
 * For each window, there is one stp4020_window_ctl_t structure, which
 *	refers to all the registers for that window.
 */

/*
 * per-window CSR structure
 */
typedef struct stp4020_window_ctl_t {
    volatile	ushort_t	ctl0;		/* window control register 0 */
    volatile	ushort_t	ctl1;		/* window control register 1 */
} stp4020_window_ctl_t;

/*
 * per-socket CSR structure
 */
typedef struct stp4020_socket_csr_t {
    volatile	struct stp4020_window_ctl_t	window[STP4020_NWIN];
    volatile	ushort_t	ctl0;		/* socket control register 0 */
    volatile	ushort_t	ctl1;		/* socket control register 1 */
    volatile	ushort_t	stat0;		/* socket status register 0 */
    volatile	ushort_t	stat1;		/* socket status register 1 */
    volatile	uchar_t	filler[12];	/* filler space */
} stp4020_socket_csr_t;

/*
 * per-instance CSR structure
 */
typedef struct stp4020_regs_t {
    struct stp4020_socket_csr_t	socket[STP4020_NSOCK];	/* socket CSRs */
} stp4020_regs_t;
#endif /* reference */

/* Size of control and status register banks */
#define STP4020_SOCKREGS_SIZE	32
#define STP4020_WINREGS_SIZE	 4

/* Relative socket control & status register offsets */
#define STP4020_ICR0_IDX	12
#define STP4020_ICR1_IDX	14
#define STP4020_ISR0_IDX	16
#define STP4020_ISR1_IDX	18

/* Relative Window control register offsets */
#define STP4020_WCR0_IDX	 0
#define STP4020_WCR1_IDX	 2

/* Socket control and status register offsets */
#define STP4020_ICR0_REG(s)	((32 * (s)) + STP4020_ICR0_IDX)
#define STP4020_ICR1_REG(s)	((32 * (s)) + STP4020_ICR1_IDX)
#define STP4020_ISR0_REG(s)	((32 * (s)) + STP4020_ISR0_IDX)
#define STP4020_ISR1_REG(s)	((32 * (s)) + STP4020_ISR1_IDX)

/* Window control and status registers; one set per socket */
#define STP4020_WCR0_REG(s,w)	((32 * (s)) + (4 * (w)) + STP4020_WCR0_IDX)
#define STP4020_WCR1_REG(s,w)	((32 * (s)) + (4 * (w)) + STP4020_WCR1_IDX)

#endif	/* _STP4020_REG_H */
