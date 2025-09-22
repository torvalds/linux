/*	$OpenBSD: i82596var.h,v 1.14 2022/01/09 05:42:38 jsg Exp $	*/
/*	$NetBSD: i82586var.h,v 1.10 1998/08/15 04:42:42 mycroft Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg and Charles M. Hannum.
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

/*-
 * Copyright (c) 1992, 1993, University of Vermont and State
 *  Agricultural College.
 * Copyright (c) 1992, 1993, Garrett A. Wollman.
 *
 * Portions:
 * Copyright (c) 1994, 1995, Rafal K. Boni
 * Copyright (c) 1990, 1991, William F. Jolitz
 * Copyright (c) 1990, The Regents of the University of California
 *
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of Vermont
 *	and State Agricultural College and Garrett A. Wollman, by William F.
 *	Jolitz, and by the University of California, Berkeley, Lawrence
 *	Berkeley Laboratory, and its contributors.
 * 4. Neither the names of the Universities nor the names of the authors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR AUTHORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Intel 82586/82596 Ethernet chip
 * Register, bit, and structure definitions.
 *
 * Original StarLAN driver written by Garrett Wollman with reference to the
 * Clarkson Packet Driver code for this chip written by Russ Nelson and others.
 *
 * BPF support code taken from hpdev/if_le.c, supplied with tcpdump.
 *
 * 3C507 support is loosely based on code donated to NetBSD by Rafal Boni.
 *
 * Majorly cleaned up and 3C507 code merged by Charles Hannum.
 *
 * Converted to SUN ie driver by Charles D. Cranor,
 *		October 1994, January 1995.
 * This sun version based on i386 version 1.30.
 */

/* #define I82596_DEBUG */

/* Debug elements */
#define	IED_RINT	0x01
#define	IED_TINT	0x02
#define	IED_RNR		0x04
#define	IED_CNA		0x08
#define	IED_READFRAME	0x10
#define IED_ENQ		0x20
#define IED_XMIT	0x40
#define IED_CMDS	0x80
#define	IED_ALL		0xff

#define B_PER_F		6		/* recv buffers per frame */
#define	IE_RBUF_SIZE	256		/* size of each receive buffer;
						MUST BE POWER OF TWO */
#define	NTXBUF		4		/* number of transmit commands */
#define	IE_TBUF_SIZE	ETHER_MAX_LEN	/* length of transmit buffer */

#define IE_MAXMCAST	(IE_TBUF_SIZE/6)/* must fit in transmit buffer */


#define	IE_INTR_ENRCV	1		/* receive pkt interrupt */
#define	IE_INTR_ENSND	2		/* send pkt interrupt */
#define	IE_INTR_LOOP	3		/* a loop for next one*/
#define	IE_INTR_EXIT	4		/* done w/ interrupts */

#define	IE_CHIP_PROBE	0		/* reset called from chip probe */
#define	IE_CARD_RESET	1		/* reset called from card reset */

#define	IE_PORT_RESET	0
#define	IE_PORT_TEST	1
#define	IE_PORT_SCP	2
#define	IE_PORT_DUMP	3

/*
 * Ethernet status, per interface.
 *
 * The chip uses two types of pointers: 16 bit and 24 bit
 *   24 bit pointers cover the board's memory.
 *   16 bit pointers are offsets from the ISCP's `ie_base'
 *
 * The board's memory is represented by the bus handle `bh'. The MI
 * i82586/596 driver deals exclusively with offsets relative to the
 * board memory bus handle. The `ie_softc' fields below that are marked
 * `MD' are in the domain of the front-end driver; they opaque to the
 * MI driver part.
 *
 * The front-end is required to manage the SCP and ISCP structures. i.e.
 * allocate room for them on the board's memory, and arrange to point the
 * chip at the SCB structure, the offset of which is passed to the MI
 * driver in `sc_scb'.
 *
 * The following functions provide the glue necessary to deal with
 * host and bus idiosyncrasies:
 *
 *	hwreset		- board reset
 *	hwinit		- board initialization
 *	chan_attn	- get chip to look at prepared commands
 *	intrhook	- board dependent interrupt processing
 *
 *	All of the following shared-memory access function use an offset
 *	relative to the bus handle to indicate the shared memory location.
 *	The bus_{read/write}N function take or return offset into the
 *	shared memory in the host's byte-order.
 *
 *	memcopyin	- copy device memory: board to KVA
 *	memcopyout	- copy device memory: KVA to board
 *	bus_read16	- read a 16-bit i82586 pointer
			  `offset' argument will be 16-bit aligned
 *	bus_write16	- write a 16-bit i82586 pointer
			  `offset' argument will be 16-bit aligned
 *	bus_write24	- write a 24-bit i82586 pointer
			  `offset' argument will be 32-bit aligned
 *
 */

struct ie_softc {
	struct device sc_dev;   /* device structure */
	void *sc_ih;

	bus_space_tag_t	iot;	/* bus-space tag of card registers */
	bus_space_handle_t ioh;	/* bus-space handle of card registers */

	bus_space_tag_t	bt;	/* bus-space tag of card memory */
	bus_space_handle_t bh;	/* bus-space handle of card memory */

	const char *sc_type;	/* (MD) hardware type */
	int	sc_vers;	/* (MD) hardware version */
	int	sc_irq;		/* (MD) irq in md format */
	void	*sc_iobase;	/* (MD) KVA of base of 24 bit addr space */
	u_long	sc_maddr;	/* (MD) base of chip's RAM (16bit addr space) */
	u_int	sc_msize;	/* (MD) how much RAM we have/use */
	u_int	sc_flags;	/* (MI/MD) flags */
#define	IEMD_FLAG0	0x00010000
#define	IEMD_FLAG1	0x00020000
#define	IEMD_FLAG2	0x00040000
#define	IEMD_FLAG3	0x00080000
#define	IEMD_FLAG4	0x00100000
#define	IEMD_FLAG5	0x00200000
#define	IEMD_FLAG6	0x00400000
#define	IEMD_FLAG7	0x00800000
	struct	arpcom sc_arpcom;	/* system ethercom structure */
	struct	ifmedia sc_media;	/* supported media information */

	/* Bus glue */
	void	(*hwreset)(struct ie_softc *, int);
	void	(*hwinit)(struct ie_softc *);
	void	(*chan_attn)(struct ie_softc *);
	void	(*port)(struct ie_softc *, u_int);
	int	(*intrhook)(struct ie_softc *, int where);

	void	(*memcopyin)(struct ie_softc *, void *, int, size_t);
	void	(*memcopyout)(struct ie_softc *, const void *,
				   int, size_t);
	u_int16_t (*ie_bus_read16)(struct ie_softc *, int offset);
	void	(*ie_bus_write16)(struct ie_softc *, int offset,
					u_int16_t value);
	void	(*ie_bus_write24)(struct ie_softc *, int offset,
					int addr);

	/* Media management */
        int  (*sc_mediachange)(struct ie_softc *);
				/* card dependent media change */
        void (*sc_mediastatus)(struct ie_softc *, struct ifmediareq *);
				/* card dependent media status */


	/*
	 * Offsets (relative to bus handle) of the i82586 SYSTEM structures.
	 */
	int	scp;		/* Offset to the SCP (set by front-end) */
	int	iscp;		/* Offset to the ISCP (set by front-end) */
	int	scb;		/* Offset to SCB (set by front-end) */

	/*
	 * Offset and size of a block of board memory where the buffers
	 * are to be allocated from (initialized by front-end).
	 */
	int	buf_area;	/* Start of descriptors and buffers */
	int	buf_area_sz;	/* Size of above */

	/* SYSBUS byte */
	int	sysbus;

	/*
	 * The buffers & descriptors (recv and xmit)
	 */
	int	rframes;	/* Offset to `nrxbuf' frame descriptors */
	int	rbds;		/* Offset to `nrxbuf' buffer descriptors */
	int	rbufs;		/* Offset to `nrxbuf' receive buffers */
#define IE_RBUF_ADDR(sc, i)	(sc->rbufs + ((i) * IE_RBUF_SIZE))
        int	rfhead, rftail;
	int	rbhead, rbtail;
	int	nframes;	/* number of frames in use */
	int	nrxbuf;		/* number of recv buffs in use */
	int	rnr_expect;	/* XXX - expect a RCVR not ready interrupt */

	int	nop_cmds;	/* Offset to NTXBUF no-op commands */
	int	xmit_cmds;	/* Offset to NTXBUF transmit commands */
	int	xbds;		/* Offset to NTXBUF buffer descriptors */
	int	xbufs;		/* Offset to NTXBUF transmit buffers */
#define IE_XBUF_ADDR(sc, i)	(sc->xbufs + ((i) * IE_TBUF_SIZE))

	int	xchead, xctail;
	int	xmit_busy;
	int	do_xmitnopchain;	/* Controls use of xmit NOP chains */

	/* Multicast addresses */
	char	*mcast_addrs;		/* Current MC filter addresses */
	int	mcast_addrs_size;	/* Current size of MC buffer */
	int	mcast_count;		/* Current # of addrs in buffer */
	int	want_mcsetup;		/* run mcsetup at next opportunity */

	int	promisc;		/* are we in promisc mode? */
	int	async_cmd_inprogress;	/* we didn't wait for 586 to accept
					   a command */

#ifdef I82596_DEBUG
	int	sc_debug;
#endif
};

/* Exported functions */
int 	i82596_intr(void *);
int 	i82596_probe(struct ie_softc *);
int 	i82596_proberam(struct ie_softc *);
void 	i82596_attach(struct ie_softc *, const char *, u_int8_t *, 
	    uint64_t *, int, uint64_t);
int 	i82596_start_cmd(struct ie_softc *, int, int, int, int);

/*
 * Interrupt Acknowledge.
 */
static __inline__ void
ie_ack(struct ie_softc *sc, u_int mask) /* in native byte-order */
{
	u_int status;
	int off = IE_SCB_STATUS(sc->scb);

	bus_space_barrier(sc->bt, sc->bh, off, 2, BUS_SPACE_BARRIER_READ);
	status = (sc->ie_bus_read16)(sc, off);
	i82596_start_cmd(sc, status & mask, 0, 0, 0);
}

