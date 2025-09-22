/*	$OpenBSD: ne2000.c,v 1.28 2022/01/09 05:42:38 jsg Exp $	*/
/*	$NetBSD: ne2000.c,v 1.12 1998/06/10 01:15:50 thorpej Exp $	*/

/*-
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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

/*
 * Device driver for National Semiconductor DS8390/WD83C690 based ethernet
 * adapters.
 *
 * Copyright (c) 1994, 1995 Charles M. Hannum.  All rights reserved.
 *
 * Copyright (C) 1993, David Greenman.  This software may be used, modified,
 * copied, distributed, and sold, in both source and binary form provided that
 * the above copyright and these terms are retained.  Under no circumstances is
 * the author responsible for the proper functioning of this software, nor does
 * the author assume any responsibility for damages incurred with its use.
 */

/*
 * Common code shared by all NE2000-compatible Ethernet interfaces.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/socket.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bus.h>

#include <dev/ic/dp8390reg.h>
#include <dev/ic/dp8390var.h>

#include <dev/ic/ne2000reg.h>
#include <dev/ic/ne2000var.h>

#include <dev/ic/ax88190reg.h>

int	ne2000_write_mbuf(struct dp8390_softc *, struct mbuf *, int);
int	ne2000_ring_copy(struct dp8390_softc *, int, caddr_t, u_short);
void	ne2000_read_hdr(struct dp8390_softc *, int, struct dp8390_ring *);
int	ne2000_test_mem(struct dp8390_softc *);

void	ne2000_writemem(bus_space_tag_t, bus_space_handle_t,
	    bus_space_tag_t, bus_space_handle_t, u_int8_t *, int, size_t, int);
void	ne2000_readmem(bus_space_tag_t, bus_space_handle_t,
	    bus_space_tag_t, bus_space_handle_t, int, u_int8_t *, size_t, int);

#define ASIC_BARRIER(asict, asich) \
	bus_space_barrier((asict), (asich), 0, 0x10, \
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE)

struct cfdriver ne_cd = {
	NULL, "ne", DV_IFNET
};

int
ne2000_attach(struct ne2000_softc *nsc, u_int8_t *myea)
{
	struct dp8390_softc *dsc = &nsc->sc_dp8390;
	bus_space_tag_t nict = dsc->sc_regt;
	bus_space_handle_t nich = dsc->sc_regh;
	bus_space_tag_t asict = nsc->sc_asict;
	bus_space_handle_t asich = nsc->sc_asich;
	u_int8_t romdata[16];
	int memsize, i, useword;

	/*
	 * Detect it again unless caller specified it; this gives us
	 * the memory size.
	 */
	if (nsc->sc_type == NE2000_TYPE_UNKNOWN)
		nsc->sc_type = ne2000_detect(nsc);

	/*
	 * 8k of memory for NE1000, 16k otherwise.
	 */
	switch (nsc->sc_type) {
	case NE2000_TYPE_UNKNOWN:
	default:
		printf(": where did the card go?\n");
		return (1);
	case NE2000_TYPE_NE1000:
		memsize = 8192;
		useword = 0;
		break;
	case NE2000_TYPE_NE2000:
	case NE2000_TYPE_AX88190:		/* XXX really? */
	case NE2000_TYPE_AX88790:
	case NE2000_TYPE_DL10019:
	case NE2000_TYPE_DL10022:
		memsize = 8192 * 2;
		useword = 1;
		break;
 	}

	nsc->sc_useword = useword;

	dsc->cr_proto = ED_CR_RD2;
	if (nsc->sc_type == NE2000_TYPE_AX88190 ||
	    nsc->sc_type == NE2000_TYPE_AX88790) {
		dsc->rcr_proto = ED_RCR_INTT;
		dsc->sc_flags |= DP8390_DO_AX88190_WORKAROUND;
	} else
		dsc->rcr_proto = 0;

	/*
	 * DCR gets:
	 *
	 *	FIFO threshold to 8, No auto-init Remote DMA,
	 *	byte order=80x86.
	 *
	 * NE1000 gets byte-wide DMA, NE2000 gets word-wide DMA.
	 */
	dsc->dcr_reg = ED_DCR_FT1 | ED_DCR_LS | (useword ? ED_DCR_WTS : 0);

	dsc->test_mem = ne2000_test_mem;
	dsc->ring_copy = ne2000_ring_copy;
	dsc->write_mbuf = ne2000_write_mbuf;
	dsc->read_hdr = ne2000_read_hdr;

	/* Registers are linear. */
	for (i = 0; i < 16; i++)
		dsc->sc_reg_map[i] = i;

	/*
	 * NIC memory doesn't start at zero on an NE board.
	 * The start address is tied to the bus width.
	 * (It happens to be computed the same way as mem size.)
	 */
	dsc->mem_start = memsize;

#ifdef GWETHER
	{
		int x, mstart = 0;
		int8_t pbuf0[ED_PAGE_SIZE], pbuf[ED_PAGE_SIZE],
		    tbuf[ED_PAGE_SIZE];

		for (i = 0; i < ED_PAGE_SIZE; i++)
			pbuf0[i] = 0;

		/* Search for the start of RAM. */
		for (x = 1; x < 256; x++) {
			ne2000_writemem(nict, nich, asict, asich, pbuf0,
			    x << ED_PAGE_SHIFT, ED_PAGE_SIZE, useword);
			ne2000_readmem(nict, nich, asict, asich,
			    x << ED_PAGE_SHIFT, tbuf, ED_PAGE_SIZE, useword);
			if (bcmp(pbuf0, tbuf, ED_PAGE_SIZE) == 0) {
				for (i = 0; i < ED_PAGE_SIZE; i++)
					pbuf[i] = 255 - x;
				ne2000_writemem(nict, nich, asict, asich,
				    pbuf, x << ED_PAGE_SHIFT, ED_PAGE_SIZE,
				    useword);
				ne2000_readmem(nict, nich, asict, asich,
				    x << ED_PAGE_SHIFT, tbuf, ED_PAGE_SIZE,
				    useword);
				if (bcmp(pbuf, tbuf, ED_PAGE_SIZE) == 0) {
					mstart = x << ED_PAGE_SHIFT;
					memsize = ED_PAGE_SIZE;
					break;
				}
			}
		}

		if (mstart == 0) {
			printf(": cannot find start of RAM\n");
			return;
		}

		/* Search for the end of RAM. */
		for (++x; x < 256; x++) {
			ne2000_writemem(nict, nich, asict, asich, pbuf0,
			    x << ED_PAGE_SHIFT, ED_PAGE_SIZE, useword);
			ne2000_readmem(nict, nich, asict, asich,
			    x << ED_PAGE_SHIFT, tbuf, ED_PAGE_SIZE, useword);
			if (bcmp(pbuf0, tbuf, ED_PAGE_SIZE) == 0) {
				for (i = 0; i < ED_PAGE_SIZE; i++)
					pbuf[i] = 255 - x;
				ne2000_writemem(nict, nich, asict, asich,
				    pbuf, x << ED_PAGE_SHIFT, ED_PAGE_SIZE,
				    useword);
				ne2000_readmem(nict, nich, asict, asich,
				    x << ED_PAGE_SHIFT, tbuf, ED_PAGE_SIZE,
				    useword);
				if (bcmp(pbuf, tbuf, ED_PAGE_SIZE) == 0)
					memsize += ED_PAGE_SIZE;
				else
					break;
			} else
				break;
		}

		printf(": RAM start 0x%x, size %d\n",
		    mstart, memsize);

		dsc->mem_start = mstart;
	}
#endif /* GWETHER */

	dsc->mem_size = memsize;

	if (myea == NULL) {
		/* Read the station address. */
		if (nsc->sc_type == NE2000_TYPE_AX88190 ||
		    nsc->sc_type == NE2000_TYPE_AX88790) {
			/* Select page 0 registers. */
			NIC_BARRIER(nict, nich);
			bus_space_write_1(nict, nich, ED_P0_CR,
			    ED_CR_RD2 | ED_CR_PAGE_0 | ED_CR_STA);
			NIC_BARRIER(nict, nich);
			/* Select word transfer. */
			bus_space_write_1(nict, nich, ED_P0_DCR, ED_DCR_WTS);
			NIC_BARRIER(nict, nich);
			ne2000_readmem(nict, nich, asict, asich,
			    AX88190_NODEID_OFFSET, dsc->sc_arpcom.ac_enaddr,
			    ETHER_ADDR_LEN, useword);
		} else {
			ne2000_readmem(nict, nich, asict, asich, 0, romdata,
			    sizeof(romdata), useword);
			for (i = 0; i < ETHER_ADDR_LEN; i++)
				dsc->sc_arpcom.ac_enaddr[i] =
				    romdata[i * (useword ? 2 : 1)];
		}
	} else
		bcopy(myea, dsc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);

	/* Clear any pending interrupts that might have occurred above. */
	NIC_BARRIER(nict, nich);
	bus_space_write_1(nict, nich, ED_P0_ISR, 0xff);
	NIC_BARRIER(nict, nich);

	if (dsc->sc_media_init == NULL)
		dsc->sc_media_init = dp8390_media_init;

	if (dp8390_config(dsc)) {
		printf(": setup failed\n");
		return (1);
	}

	return (0);
}

/*
 * Detect an NE-2000 or compatible.  Returns a model code.
 */
int
ne2000_detect(struct ne2000_softc *nsc)
{
	struct dp8390_softc *dsc = &nsc->sc_dp8390;
	bus_space_tag_t nict = dsc->sc_regt;
	bus_space_handle_t nich = dsc->sc_regh;
	bus_space_tag_t asict = nsc->sc_asict;
	bus_space_handle_t asich = nsc->sc_asich;
	static u_int8_t test_pattern[32] = "THIS is A memory TEST pattern";
	u_int8_t test_buffer[32], tmp;
	int state, i, rv = 0;

	state = dsc->sc_enabled;
	dsc->sc_enabled = 0;

	/* Reset the board. */
#ifdef GWETHER
	bus_space_write_1(asict, asich, NE2000_ASIC_RESET, 0);
	ASIC_BARRIER(asict, asich);
	delay(200);
#endif /* GWETHER */
	tmp = bus_space_read_1(asict, asich, NE2000_ASIC_RESET);
	ASIC_BARRIER(asict, asich);
	delay(10000);

	/*
	 * I don't know if this is necessary; probably cruft leftover from
	 * Clarkson packet driver code. Doesn't do a thing on the boards I've
	 * tested. -DG [note that a outb(0x84, 0) seems to work here, and is
	 * non-invasive...but some boards don't seem to reset and I don't have
	 * complete documentation on what the 'right' thing to do is...so we do
	 * the invasive thing for now.  Yuck.]
	 */
	bus_space_write_1(asict, asich, NE2000_ASIC_RESET, tmp);
	ASIC_BARRIER(asict, asich);
	delay(5000);

	/*
	 * This is needed because some NE clones apparently don't reset the
	 * NIC properly (or the NIC chip doesn't reset fully on power-up).
	 * XXX - this makes the probe invasive!  Done against my better
	 * judgement.  -DLG
	 */
	bus_space_write_1(nict, nich, ED_P0_CR,
	    ED_CR_RD2 | ED_CR_PAGE_0 | ED_CR_STP);
	NIC_BARRIER(nict, nich);

	delay(5000);

	/*
	 * Generic probe routine for testing for the existence of a DS8390.
	 * Must be performed  after the NIC has just been reset.  This
	 * works by looking at certain register values that are guaranteed
	 * to be initialized a certain way after power-up or reset.
	 *
	 * Specifically:
	 *
	 *	Register		reset bits	set bits
	 *	--------		----------	--------
	 *	CR			TXP, STA	RD2, STP
	 *	ISR					RST
	 *	IMR			<all>
	 *	DCR					LAS
	 *	TCR			LB1, LB0
	 *
	 * We only look at CR and ISR, however, since looking at the others
	 * would require changing register pages, which would be intrusive
	 * if this isn't an 8390.
	 */

	tmp = bus_space_read_1(nict, nich, ED_P0_CR);
	if ((tmp & (ED_CR_RD2 | ED_CR_TXP | ED_CR_STA | ED_CR_STP)) !=
	    (ED_CR_RD2 | ED_CR_STP))
		goto out;

	tmp = bus_space_read_1(nict, nich, ED_P0_ISR);
	if ((tmp & ED_ISR_RST) != ED_ISR_RST)
		goto out;

	bus_space_write_1(nict, nich,
	    ED_P0_CR, ED_CR_RD2 | ED_CR_PAGE_0 | ED_CR_STA);
	NIC_BARRIER(nict, nich);

	for (i = 0; i < 100; i++) {
		if ((bus_space_read_1(nict, nich, ED_P0_ISR) & ED_ISR_RST) ==
		    ED_ISR_RST) {
			/* Ack the reset bit. */
			bus_space_write_1(nict, nich, ED_P0_ISR, ED_ISR_RST);
			NIC_BARRIER(nict, nich);
			break;
		}
		delay(100);
	}

#if 0
	/* XXX */
	if (i == 100)
		goto out;
#endif

	/*
	 * Test the ability to read and write to the NIC memory.  This has
	 * the side effect of determining if this is an NE1000 or an NE2000.
	 */

	/*
	 * This prevents packets from being stored in the NIC memory when
	 * the readmem routine turns on the start bit in the CR.
	 */
	bus_space_write_1(nict, nich, ED_P0_RCR, ED_RCR_MON);
	NIC_BARRIER(nict, nich);

	/* Temporarily initialize DCR for byte operations. */
	bus_space_write_1(nict, nich, ED_P0_DCR, ED_DCR_FT1 | ED_DCR_LS);

	bus_space_write_1(nict, nich, ED_P0_PSTART, 8192 >> ED_PAGE_SHIFT);
	bus_space_write_1(nict, nich, ED_P0_PSTOP, 16384 >> ED_PAGE_SHIFT);

	/*
	 * Write a test pattern in byte mode.  If this fails, then there
	 * probably isn't any memory at 8k - which likely means that the
	 * board is an NE2000.
	 */
	ne2000_writemem(nict, nich, asict, asich, test_pattern, 8192,
	    sizeof(test_pattern), 0);
	ne2000_readmem(nict, nich, asict, asich, 8192, test_buffer,
	    sizeof(test_buffer), 0);

	if (bcmp(test_pattern, test_buffer, sizeof(test_pattern))) {
		/* not an NE1000 - try NE2000 */
		bus_space_write_1(nict, nich, ED_P0_DCR,
		    ED_DCR_WTS | ED_DCR_FT1 | ED_DCR_LS);
		bus_space_write_1(nict, nich, ED_P0_PSTART,
		    16384 >> ED_PAGE_SHIFT);
		bus_space_write_1(nict, nich, ED_P0_PSTOP,
		    32768 >> ED_PAGE_SHIFT);

		/*
		 * Write the test pattern in word mode.  If this also fails,
		 * then we don't know what this board is.
		 */
		ne2000_writemem(nict, nich, asict, asich, test_pattern, 16384,
		    sizeof(test_pattern), 1);
		ne2000_readmem(nict, nich, asict, asich, 16384, test_buffer,
		    sizeof(test_buffer), 1);

		if (bcmp(test_pattern, test_buffer, sizeof(test_pattern)))
			goto out;	/* not an NE2000 either */

		rv = NE2000_TYPE_NE2000;
	} else {
		/* We're an NE1000. */
		rv = NE2000_TYPE_NE1000;
	}

	/* Clear any pending interrupts that might have occurred above. */
	NIC_BARRIER(nict, nich);
	bus_space_write_1(nict, nich, ED_P0_ISR, 0xff);

 out:
	dsc->sc_enabled = state;

	return (rv);
}

/*
 * Write an mbuf chain to the destination NIC memory address using programmed
 * I/O.
 */
int
ne2000_write_mbuf(struct dp8390_softc *sc, struct mbuf *m, int buf)
{
	struct ne2000_softc *nsc = (struct ne2000_softc *)sc;
	bus_space_tag_t nict = sc->sc_regt;
	bus_space_handle_t nich = sc->sc_regh;
	bus_space_tag_t asict = nsc->sc_asict;
	bus_space_handle_t asich = nsc->sc_asich;
	int savelen;
	int maxwait = 100;	/* about 120us */

	savelen = m->m_pkthdr.len;

	/* Select page 0 registers. */
	NIC_BARRIER(nict, nich);
	bus_space_write_1(nict, nich, ED_P0_CR,
	    ED_CR_RD2 | ED_CR_PAGE_0 | ED_CR_STA);
	NIC_BARRIER(nict, nich);

	/* Reset remote DMA complete flag. */
	bus_space_write_1(nict, nich, ED_P0_ISR, ED_ISR_RDC);
	NIC_BARRIER(nict, nich);

	/* Set up DMA byte count. */
	bus_space_write_1(nict, nich, ED_P0_RBCR0, savelen);
	bus_space_write_1(nict, nich, ED_P0_RBCR1, savelen >> 8);

	/* Set up destination address in NIC mem. */
	bus_space_write_1(nict, nich, ED_P0_RSAR0, buf);
	bus_space_write_1(nict, nich, ED_P0_RSAR1, buf >> 8);

	/* Set remote DMA write. */
	NIC_BARRIER(nict, nich);
	bus_space_write_1(nict, nich,
	    ED_P0_CR, ED_CR_RD1 | ED_CR_PAGE_0 | ED_CR_STA);
	NIC_BARRIER(nict, nich);

	/*
	 * Transfer the mbuf chain to the NIC memory.  NE2000 cards
	 * require that data be transferred as words, and only words,
	 * so that case requires some extra code to patch over odd-length
	 * mbufs.
	 */
	if (nsc->sc_type == NE2000_TYPE_NE1000) {
		/* NE1000s are easy. */
		for (; m != 0; m = m->m_next) {
			if (m->m_len) {
				bus_space_write_multi_1(asict, asich,
				    NE2000_ASIC_DATA, mtod(m, u_int8_t *),
				    m->m_len);
			}
		}
	} else {
		/* NE2000s are a bit trickier. */
		u_int8_t *data, savebyte[2];
		int l, leftover;
#ifdef DIAGNOSTIC
		u_int8_t *lim;
#endif
		/* Start out with no leftover data. */
		leftover = 0;
		savebyte[0] = savebyte[1] = 0;

		for (; m != 0; m = m->m_next) {
			l = m->m_len;
			if (l == 0)
				continue;
			data = mtod(m, u_int8_t *);
#ifdef DIAGNOSTIC
			lim = data + l;
#endif
			while (l > 0) {
				if (leftover) {
					/*
					 * Data left over (from mbuf or
					 * realignment).  Buffer the next
					 * byte, and write it and the
					 * leftover data out.
					 */
					savebyte[1] = *data++;
					l--;
					bus_space_write_raw_multi_2(asict,
					    asich, NE2000_ASIC_DATA,
					    savebyte, 2);
					leftover = 0;
				} else if (ALIGNED_POINTER(data,
					   u_int16_t) == 0) {
					/*
					 * Unaligned data; buffer the next
					 * byte.
					 */
					savebyte[0] = *data++;
					l--;
					leftover = 1;
				} else {
					/*
					 * Aligned data; output contiguous
					 * words as much as we can, then
					 * buffer the remaining byte, if any.
					 */
					leftover = l & 1;
					l &= ~1;
					bus_space_write_raw_multi_2(asict,
					    asich, NE2000_ASIC_DATA, data, l);
					data += l;
					if (leftover)
						savebyte[0] = *data++;
					l = 0;
				}
			}
			if (l < 0)
				panic("ne2000_write_mbuf: negative len");
#ifdef DIAGNOSTIC
			if (data != lim)
				panic("ne2000_write_mbuf: data != lim");
#endif
		}
		if (leftover) {
			savebyte[1] = 0;
			bus_space_write_raw_multi_2(asict, asich,
			    NE2000_ASIC_DATA, savebyte, 2);
		}
	}
	NIC_BARRIER(nict, nich);

	/* AX88796 doesn't seem to have remote DMA complete */
	if (sc->sc_flags & DP8390_NO_REMOTE_DMA_COMPLETE)
		return (savelen);

	/*
	 * Wait for remote DMA to complete.  This is necessary because on the
	 * transmit side, data is handled internally by the NIC in bursts, and
	 * we can't start another remote DMA until this one completes.  Not
	 * waiting causes really bad things to happen - like the NIC wedging
	 * the bus.
	 */
	while (((bus_space_read_1(nict, nich, ED_P0_ISR) & ED_ISR_RDC) !=
	    ED_ISR_RDC) && --maxwait) {
		bus_space_read_1(nict, nich, ED_P0_CRDA1);
		bus_space_read_1(nict, nich, ED_P0_CRDA0);
		NIC_BARRIER(nict, nich);
		DELAY(1);
	}

	if (maxwait == 0) {
		log(LOG_WARNING,
		    "%s: remote transmit DMA failed to complete\n",
		    sc->sc_dev.dv_xname);
		dp8390_reset(sc);
	}

	return (savelen);
}

/*
 * Given a source and destination address, copy 'amount' of a packet from
 * the ring buffer into a linear destination buffer.  Takes into account
 * ring-wrap.
 */
int
ne2000_ring_copy(struct dp8390_softc *sc, int src, caddr_t dst,
    u_short amount)
{
	struct ne2000_softc *nsc = (struct ne2000_softc *)sc;
	bus_space_tag_t nict = sc->sc_regt;
	bus_space_handle_t nich = sc->sc_regh;
	bus_space_tag_t asict = nsc->sc_asict;
	bus_space_handle_t asich = nsc->sc_asich;
	u_short tmp_amount;
	int useword = nsc->sc_useword;

	/* Does copy wrap to lower addr in ring buffer? */
	if (src + amount > sc->mem_end) {
		tmp_amount = sc->mem_end - src;

		/* Copy amount up to end of NIC memory. */
		ne2000_readmem(nict, nich, asict, asich, src,
		    (u_int8_t *)dst, tmp_amount, useword);

		amount -= tmp_amount;
		src = sc->mem_ring;
		dst += tmp_amount;
	}

	ne2000_readmem(nict, nich, asict, asich, src, (u_int8_t *)dst,
	    amount, useword);

	return (src + amount);
}

void
ne2000_read_hdr(struct dp8390_softc *sc, int buf, struct dp8390_ring *hdr)
{
	struct ne2000_softc *nsc = (struct ne2000_softc *)sc;

	ne2000_readmem(sc->sc_regt, sc->sc_regh, nsc->sc_asict, nsc->sc_asich,
	    buf, (u_int8_t *)hdr, sizeof(struct dp8390_ring),
	    nsc->sc_useword);
#if BYTE_ORDER == BIG_ENDIAN
	hdr->count = swap16(hdr->count);
#endif
}

int
ne2000_test_mem(struct dp8390_softc *sc)
{
	/* Noop. */
	return (0);
}

/*
 * Given a NIC memory source address and a host memory destination address,
 * copy 'amount' from NIC to host using programmed i/o.  The 'amount' is
 * rounded up to a word - ok as long as mbufs are word sized.
 */
void
ne2000_readmem(bus_space_tag_t nict, bus_space_handle_t nich,
    bus_space_tag_t asict, bus_space_handle_t asich, int src,
    u_int8_t *dst, size_t amount, int useword)
{

	/* Select page 0 registers. */
	NIC_BARRIER(nict, nich);
	bus_space_write_1(nict, nich, ED_P0_CR,
	    ED_CR_RD2 | ED_CR_PAGE_0 | ED_CR_STA);
	NIC_BARRIER(nict, nich);

	/* Round up to a word. */
	if (amount & 1)
		++amount;

	/* Set up DMA byte count. */
	bus_space_write_1(nict, nich, ED_P0_RBCR0, amount);
	bus_space_write_1(nict, nich, ED_P0_RBCR1, amount >> 8);

	/* Set up source address in NIC mem. */
	bus_space_write_1(nict, nich, ED_P0_RSAR0, src);
	bus_space_write_1(nict, nich, ED_P0_RSAR1, src >> 8);

	NIC_BARRIER(nict, nich);
	bus_space_write_1(nict, nich, ED_P0_CR,
	    ED_CR_RD0 | ED_CR_PAGE_0 | ED_CR_STA);

	ASIC_BARRIER(asict, asich);
	if (useword)
		bus_space_read_raw_multi_2(asict, asich, NE2000_ASIC_DATA,
		    dst, amount);
	else
		bus_space_read_multi_1(asict, asich, NE2000_ASIC_DATA,
		    dst, amount);
}

/*
 * Stripped down routine for writing a linear buffer to NIC memory.  Only
 * used in the probe routine to test the memory.  'len' must be even.
 */
void
ne2000_writemem(bus_space_tag_t nict, bus_space_handle_t nich,
    bus_space_tag_t asict, bus_space_handle_t asich, u_int8_t *src,
    int dst, size_t len, int useword)
{
	int maxwait = 100;	/* about 120us */

	/* Select page 0 registers. */
	NIC_BARRIER(nict, nich);
	bus_space_write_1(nict, nich, ED_P0_CR,
	    ED_CR_RD2 | ED_CR_PAGE_0 | ED_CR_STA);
	NIC_BARRIER(nict, nich);

	/* Reset remote DMA complete flag. */
	bus_space_write_1(nict, nich, ED_P0_ISR, ED_ISR_RDC);
	NIC_BARRIER(nict, nich);

	/* Set up DMA byte count. */
	bus_space_write_1(nict, nich, ED_P0_RBCR0, len);
	bus_space_write_1(nict, nich, ED_P0_RBCR1, len >> 8);

	/* Set up destination address in NIC mem. */
	bus_space_write_1(nict, nich, ED_P0_RSAR0, dst);
	bus_space_write_1(nict, nich, ED_P0_RSAR1, dst >> 8);

	/* Set remote DMA write. */
	NIC_BARRIER(nict, nich);
	bus_space_write_1(nict, nich, ED_P0_CR,
	    ED_CR_RD1 | ED_CR_PAGE_0 | ED_CR_STA);
	NIC_BARRIER(nict, nich);

	ASIC_BARRIER(asict, asich);
	if (useword)
		bus_space_write_raw_multi_2(asict, asich, NE2000_ASIC_DATA,
		    src, len);
	else
		bus_space_write_multi_1(asict, asich, NE2000_ASIC_DATA,
		    src, len);
	ASIC_BARRIER(asict, asich);

	/*
	 * Wait for remote DMA to complete.  This is necessary because on the
	 * transmit side, data is handled internally by the NIC in bursts, and
	 * we can't start another remote DMA until this one completes.  Not
	 * waiting causes really bad things to happen - like the NIC wedging
	 * the bus.
	 */
	while (((bus_space_read_1(nict, nich, ED_P0_ISR) & ED_ISR_RDC) !=
	    ED_ISR_RDC) && --maxwait)
		DELAY(1);
}

int
ne2000_detach(struct ne2000_softc *sc, int flags)
{
	return (dp8390_detach(&sc->sc_dp8390, flags));
}
