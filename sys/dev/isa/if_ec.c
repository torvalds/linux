/*	$OpenBSD: if_ec.c,v 1.19 2022/04/06 18:59:28 naddy Exp $	*/
/*	$NetBSD: if_ec.c,v 1.9 1998/07/05 06:49:12 jonathan Exp $	*/

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
 * Device driver for the 3Com Etherlink II (3c503).
 */

#include "bpfilter.h"

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

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include <dev/ic/dp8390reg.h>
#include <dev/ic/dp8390var.h>

#include <dev/isa/if_ecreg.h>

struct ec_softc {
	struct dp8390_softc sc_dp8390;

	bus_space_tag_t sc_asict;	/* space tag for ASIC */
	bus_space_handle_t sc_asich;	/* space handle for ASIC */

	int sc_16bitp;			/* are we 16 bit? */

	void *sc_ih;			/* interrupt handle */
};

int	ec_probe(struct device *, void *, void *);
void	ec_attach(struct device *, struct device *, void *);

const struct cfattach ec_ca = {
	sizeof(struct ec_softc), ec_probe, ec_attach
};

int	ec_set_media(struct ec_softc *, uint64_t);

void	ec_media_init(struct dp8390_softc *);

int	ec_mediachange(struct dp8390_softc *);
void	ec_mediastatus(struct dp8390_softc *, struct ifmediareq *);

void	ec_init_card(struct dp8390_softc *);
int	ec_write_mbuf(struct dp8390_softc *, struct mbuf *, int);
int	ec_ring_copy(struct dp8390_softc *, int, caddr_t, u_short);
void	ec_read_hdr(struct dp8390_softc *, int, struct dp8390_ring *);
int	ec_fake_test_mem(struct dp8390_softc *);
int	ec_test_mem(struct dp8390_softc *);

static __inline void ec_readmem(struct ec_softc *, int, u_int8_t *, int);

static const int ec_iobase[] = {
	0x2e0, 0x2a0, 0x280, 0x250, 0x350, 0x330, 0x310, 0x300,
};
#define	NEC_IOBASE	(sizeof(ec_iobase) / sizeof(ec_iobase[0]))

static const int ec_membase[] = {
	MADDRUNK, MADDRUNK, MADDRUNK, MADDRUNK, 0xc8000, 0xcc000,
	0xd8000, 0xdc000,
};
#define	NEC_MEMBASE	(sizeof(ec_membase) / sizeof(ec_membase[0]))

struct cfdriver ec_cd = {
	NULL, "ec", DV_IFNET
};

int
ec_probe(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t nict, asict, memt;
	bus_space_handle_t nich, asich, memh;
	bus_size_t memsize;
	int nich_valid, asich_valid, memh_valid;
	int i, rv = 0;
	u_int8_t x;

	nict = asict = ia->ia_iot;
	memt = ia->ia_memt;

	nich_valid = asich_valid = memh_valid = 0;

	/*
	 * Hmm, a 16-bit card has 16k of memory, but only an 8k window
	 * to it.
	 */
	memsize = 8192;

	/* Disallow wildcarded i/o addresses. */
	if (ia->ia_iobase == -1 /* ISACF_PORT_DEFAULT */)
		return (0);

	/* Disallow wildcarded mem address. */
	if (ia->ia_maddr == -1 /* ISACF_IOMEM_DEFAULT */)
		return (0);

	/* Validate the i/o base. */
	for (i = 0; i < NEC_IOBASE; i++)
		if (ia->ia_iobase == ec_iobase[i])
			break;
	if (i == NEC_IOBASE)
		return (0);

	/* Validate the mem base. */
	for (i = 0; i < NEC_MEMBASE; i++) {
		if (ec_membase[i] == MADDRUNK)
			continue;
		if (ia->ia_maddr == ec_membase[i])
			break;
	}
	if (i == NEC_MEMBASE)
		return (0);

	/* Attempt to map the NIC space. */
	if (bus_space_map(nict, ia->ia_iobase + ELINK2_NIC_OFFSET,
	    ELINK2_NIC_PORTS, 0, &nich))
		goto out;
	nich_valid = 1;

	/* Attempt to map the ASIC space. */
	if (bus_space_map(asict, ia->ia_iobase + ELINK2_ASIC_OFFSET,
	    ELINK2_ASIC_PORTS, 0, &asich))
		goto out;
	asich_valid = 1;

	/* Attempt to map the memory space. */
	if (bus_space_map(memt, ia->ia_maddr, memsize, 0, &memh))
		goto out;
	memh_valid = 1;

	/*
	 * Verify that the kernel configured I/O address matches the
	 * board configured I/O address.
	 *
	 * This is really only useful to see if something that looks like
	 * the board is there; after all, we're already talking to it at
	 * this point.
	 */
	x = bus_space_read_1(asict, asich, ELINK2_BCFR);
	if (x == 0 || (x & (x - 1)) != 0)
		goto out;
	i = ffs(x) - 1;
	if (ia->ia_iobase != ec_iobase[i])
		goto out;

	/*
	 * ...and for the memory address.  Note we do not support
	 * cards configured with shared memory disabled.
	 */
	x = bus_space_read_1(asict, asich, ELINK2_PCFR);
	if (x == 0 || (x & (x - 1)) != 0)
		goto out;
	i = ffs(x) - 1;
	if (ia->ia_maddr != ec_membase[i])
		goto out;

	/* So, we say we've found it! */
	ia->ia_iosize = ELINK2_NIC_PORTS;
	ia->ia_msize = memsize;
	rv = 1;

 out:
	if (nich_valid)
		bus_space_unmap(nict, nich, ELINK2_NIC_PORTS);
	if (asich_valid)
		bus_space_unmap(asict, asich, ELINK2_ASIC_PORTS);
	if (memh_valid)
		bus_space_unmap(memt, memh, memsize);
	return (rv);
}

void
ec_attach(struct device *parent, struct device *self, void *aux)
{
	struct ec_softc *esc = (struct ec_softc *)self;
	struct dp8390_softc *sc = &esc->sc_dp8390;
	struct isa_attach_args *ia = aux;
	bus_space_tag_t nict, asict, memt;
	bus_space_handle_t nich, asich, memh;
	bus_size_t memsize;
	u_int8_t tmp;
	int i;

	printf("\n");

	nict = asict = ia->ia_iot;
	memt = ia->ia_memt;

	/*
	 * Hmm, a 16-bit card has 16k of memory, but only an 8k window
	 * to it.
	 */
	memsize = 8192;

	/* Map the NIC space. */
	if (bus_space_map(nict, ia->ia_iobase + ELINK2_NIC_OFFSET,
	    ELINK2_NIC_PORTS, 0, &nich)) {
		printf("%s: can't map nic i/o space\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/* Map the ASIC space. */
	if (bus_space_map(asict, ia->ia_iobase + ELINK2_ASIC_OFFSET,
	    ELINK2_ASIC_PORTS, 0, &asich)) {
		printf("%s: can't map asic i/o space\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/* Map the memory space. */
	if (bus_space_map(memt, ia->ia_maddr, memsize, 0, &memh)) {
		printf("%s: can't map shared memory\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	esc->sc_asict = asict;
	esc->sc_asich = asich;

	sc->sc_regt = nict;
	sc->sc_regh = nich;

	sc->sc_buft = memt;
	sc->sc_bufh = memh;

	/* Interface is always enabled. */
	sc->sc_enabled = 1;

	/* Registers are linear. */
	for (i = 0; i < 16; i++)
		sc->sc_reg_map[i] = i;

	/* Now we can use the NIC_{GET,PUT}() macros. */

	/*
	 * Reset NIC and ASIC.  Enable on-board transceiver throughout
	 * reset sequence since it will lock up if the cable isn't
	 * connected if we don't.
	 */
	bus_space_write_1(asict, asich, ELINK2_CR,
	    ELINK2_CR_RST | ELINK2_CR_XSEL);

	/* Wait for a while, then un-reset it. */
	delay(50);

	/*
	 * The 3Com ASIC defaults to rather strange settings for the CR
	 * after a reset.  It's important to set it again after the
	 * following write (this is done when we map the PROM below).
	 */
	bus_space_write_1(asict, asich, ELINK2_CR, ELINK2_CR_XSEL);

	/* Wait a bit for the NIC to recover from the reset. */
	delay(5000);

	/*
	 * Get the station address from on-board ROM.
	 *
	 * First, map Ethernet address PROM over the top of where the NIC
	 * registers normally appear.
	 */
	bus_space_write_1(asict, asich, ELINK2_CR,
	    ELINK2_CR_XSEL | ELINK2_CR_EALO);

	for (i = 0; i < ETHER_ADDR_LEN; i++)
		sc->sc_arpcom.ac_enaddr[i] = NIC_GET(nict, nich, i);

	/*
	 * Unmap PROM - select NIC registers.  The proper setting of the
	 * transceiver is set in later in ec_init_card() via dp8390_init().
	 */
	bus_space_write_1(asict, asich, ELINK2_CR, ELINK2_CR_XSEL);

	/* Determine if this is an 8-bit or 16-bit board. */

	/* Select page 0 registers. */
	NIC_PUT(nict, nich, ED_P0_CR, ED_CR_RD2 | ED_CR_PAGE_0 | ED_CR_STP);

	/*
	 * Attempt to clear WTS.  If it doesn't clear, then this is a
	 * 16-bit board.
	 */
	NIC_PUT(nict, nich, ED_P0_DCR, 0);

	/* Select page 2 registers. */
	NIC_PUT(nict, nich, ED_P0_CR, ED_CR_RD2 | ED_CR_PAGE_2 | ED_CR_STP);

	/* The 3c503 forces the WTS bit to a one if this is a 16-bit board. */
	if (NIC_GET(nict, nich, ED_P2_DCR) & ED_DCR_WTS)
		esc->sc_16bitp = 1;
	else
		esc->sc_16bitp = 0;

	printf("%s: 3Com 3c503 Ethernet (%s-bit)",
	    sc->sc_dev.dv_xname, esc->sc_16bitp ? "16" : "8");

	/* Select page 0 registers. */
	NIC_PUT(nict, nich, ED_P2_CR, ED_CR_RD2 | ED_CR_PAGE_0 | ED_CR_STP);

	sc->cr_proto = ED_CR_RD2;

	/*
	 * DCR gets:
	 *
	 *	FIFO threshold to 8, No auto-init Remote DMA,
	 *	byte order=80x86.
	 *
	 * 16-bit cards also get word-wide DMA transfers.
	 */
	sc->dcr_reg = ED_DCR_FT1 | ED_DCR_LS |
	    (esc->sc_16bitp ? ED_DCR_WTS : 0);

	sc->test_mem = ec_fake_test_mem;
	sc->ring_copy = ec_ring_copy;
	sc->write_mbuf = ec_write_mbuf;
	sc->read_hdr = ec_read_hdr;

	sc->sc_media_init = ec_media_init;

	sc->sc_mediachange = ec_mediachange;
	sc->sc_mediastatus = ec_mediastatus;

	sc->mem_start = 0;
	sc->mem_size = memsize;

	/* Do generic parts of attach. */
	if (dp8390_config(sc)) {
		printf(": configuration failed\n");
		return;
	}

	/*
	 * We need to override the way dp8390_config() set up our
	 * shared memory.
	 *
	 * We have an entire 8k window to put the transmit buffers on the
	 * 16-bit boards.  But since the 16bit 3c503's shared memory is only
	 * fast enough to overlap the loading of one full-size packet, trying
	 * to load more than 2 buffers can actually leave the transmitter idle
	 * during the load.  So 2 seems the best value.  (Although a mix of
	 * variable-sized packets might change this assumption.  Nonetheless,
	 * we optimize for linear transfers of same-size packets.)
	 */
	if (esc->sc_16bitp) {
		if (sc->sc_dev.dv_cfdata->cf_flags & DP8390_NO_MULTI_BUFFERING)
			sc->txb_cnt = 1;
		else
			sc->txb_cnt = 2;

		sc->tx_page_start = ELINK2_TX_PAGE_OFFSET_16BIT;
		sc->rec_page_start = ELINK2_RX_PAGE_OFFSET_16BIT;
		sc->rec_page_stop = (memsize >> ED_PAGE_SHIFT) + 
		    sc->rec_page_start;
		sc->mem_ring = sc->mem_start;
	} else {
		sc->txb_cnt = 1;
		sc->tx_page_start = ELINK2_TX_PAGE_OFFSET_8BIT;
		sc->rec_page_start = sc->tx_page_start + ED_TXBUF_SIZE;
		sc->rec_page_stop = (memsize >> ED_PAGE_SHIFT) +
		    sc->tx_page_start;
		sc->mem_ring = sc->mem_start +
		    (ED_TXBUF_SIZE << ED_PAGE_SHIFT);
	}

	/*
	 * Initialize CA page start/stop registers.  Probably only needed
	 * if doing DMA, but what the Hell.
	 */
	bus_space_write_1(asict, asich, ELINK2_PSTR, sc->rec_page_start);
	bus_space_write_1(asict, asich, ELINK2_PSPR, sc->rec_page_stop);

	/*
	 * Program the IRQ.
	 */
	switch (ia->ia_irq) {
	case 9:	tmp = ELINK2_IDCFR_IRQ2; break;
	case 3:	tmp = ELINK2_IDCFR_IRQ3; break;
	case 4:	tmp = ELINK2_IDCFR_IRQ4; break;
	case 5:	tmp = ELINK2_IDCFR_IRQ5; break;
		break;

	case IRQUNK:
		printf("%s: wildcarded IRQ is not allowed\n",
		    sc->sc_dev.dv_xname);
		return;

	default:
		printf("%s: invalid IRQ %d, must be 3, 4, 5, or 9\n",
		    sc->sc_dev.dv_xname, ia->ia_irq);
		return;
	}

	bus_space_write_1(asict, asich, ELINK2_IDCFR, tmp);

	/*
	 * Initialize the GA configuration register.  Set bank and enable
	 * shared memory.
	 */
	bus_space_write_1(asict, asich, ELINK2_GACFR,
	    ELINK2_GACFR_RSEL | ELINK2_GACFR_MBS0);

	/*
	 * Initialize "Vector Pointer" registers.  These gawd-awful things
	 * are compared to 20 bits of the address on the ISA, and if they
	 * match, the shared memory is disabled.  We se them to 0xffff0...
	 * allegedly the reset vector.
	 */
	bus_space_write_1(asict, asich, ELINK2_VPTR2, 0xff);
	bus_space_write_1(asict, asich, ELINK2_VPTR1, 0xff);
	bus_space_write_1(asict, asich, ELINK2_VPTR0, 0x00);

	/*
	 * Now run the real memory test.
	 */
	if (ec_test_mem(sc)) {
		printf("%s: memory test failed\n", sc->sc_dev.dv_xname);
		return;
	}

	/* Establish interrupt handler. */
	esc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq, IST_EDGE,
	    IPL_NET, dp8390_intr, sc, sc->sc_dev.dv_xname);
	if (esc->sc_ih == NULL)
		printf("%s: can't establish interrupt\n", sc->sc_dev.dv_xname);
}

int
ec_fake_test_mem(struct dp8390_softc *sc)
{
	/*
	 * We have to do this after we initialize the GA, but we
	 * have to do that after calling dp8390_config(), which
	 * wants to test memory.  Put this noop here, and then
	 * actually test memory later.
	 */
	return (0);
}

int
ec_test_mem(struct dp8390_softc *sc)
{
	struct ec_softc *esc = (struct ec_softc *)sc;
	bus_space_tag_t memt = sc->sc_buft;
	bus_space_handle_t memh = sc->sc_bufh;
	bus_size_t memsize = sc->mem_size;
	int i;

	if (esc->sc_16bitp)
		bus_space_set_region_2(memt, memh, 0, 0, memsize >> 1);
	else
		bus_space_set_region_1(memt, memh, 0, 0, memsize);

	if (esc->sc_16bitp) {
		for (i = 0; i < memsize; i += 2) {
			if (bus_space_read_2(memt, memh, i) != 0)
				goto fail;
		}
	} else {
		for (i = 0; i < memsize; i++) {
			if (bus_space_read_1(memt, memh, i) != 0)
				goto fail;
		}
	}

	return (0);

 fail:
	printf("%s: failed to clear shared memory at offset 0x%x\n",
	    sc->sc_dev.dv_xname, i);
	return (1);
}

/*
 * Given a NIC memory source address and a host memory destination address,
 * copy 'len' from NIC to host using shared memory.  The 'len' is rounded
 * up to a word - ok as long as mbufs are word-sized.
 */
static __inline void
ec_readmem(struct ec_softc *esc, int from, u_int8_t *to, int len)
{
	bus_space_tag_t memt = esc->sc_dp8390.sc_buft;
	bus_space_handle_t memh = esc->sc_dp8390.sc_bufh;

	if (len & 1)
		++len;

	if (esc->sc_16bitp)
		bus_space_read_region_2(memt, memh, from, (u_int16_t *)to,
		    len >> 1);
	else
		bus_space_read_region_1(memt, memh, from, to, len);
}

int
ec_write_mbuf(struct dp8390_softc *sc, struct mbuf *m, int buf)
{
	struct ec_softc *esc = (struct ec_softc *)sc;
	bus_space_tag_t asict = esc->sc_asict;
	bus_space_handle_t asich = esc->sc_asich;
	bus_space_tag_t memt = esc->sc_dp8390.sc_buft;
	bus_space_handle_t memh = esc->sc_dp8390.sc_bufh;
	u_int8_t *data, savebyte[2];
	int savelen, len, leftover;
#ifdef DIAGNOSTIC
	u_int8_t *lim;
#endif

	savelen = m->m_pkthdr.len;

	/*
	 * 8-bit boards are simple: we're already in the correct
	 * page, and no alignment tricks are necessary.
	 */
	if (esc->sc_16bitp == 0) {
		for (; m != NULL; buf += m->m_len, m = m->m_next)
			bus_space_write_region_1(memt, memh, buf,
			    mtod(m, u_int8_t *), m->m_len);
		return (savelen);
	}

	/*
	 * If it's a 16-bit board, we have transmit buffers
	 * in a different page; switch to it.
	 */
	if (esc->sc_16bitp)
		bus_space_write_1(asict, asich, ELINK2_GACFR,
		    ELINK2_GACFR_RSEL);

	/* Start out with no leftover data. */
	leftover = 0;
	savebyte[0] = savebyte[1] = 0;

	for (; m != NULL; m = m->m_next) {
		len = m->m_len;
		if (len == 0)
			continue;
		data = mtod(m, u_int8_t *);
#ifdef DIAGNOSTIC
		lim = data + len;
#endif
		while (len > 0) {
			if (leftover) {
				/*
				 * Data left over (from mbuf or realignment).
				 * Buffer the next byte, and write it and
				 * the leftover data out.
				 */
				savebyte[1] = *data++;
				len--;
				bus_space_write_2(memt, memh, buf,
				    *(u_int16_t *)savebyte);
				buf += 2;
				leftover = 0;
			} else if (ALIGNED_POINTER(data, u_int16_t) == 0) {
				/*
				 * Unaligned data; buffer the next byte.
				 */
				savebyte[0] = *data++;
				len--;
				leftover = 1;
			} else {
				/*
				 * Aligned data; output contiguous words as
				 * much as we can, then buffer the remaining
				 * byte, if any.
				 */
				leftover = len & 1;
				len &= ~1;
				bus_space_write_region_2(memt, memh, buf,
				    (u_int16_t *)data, len >> 1);
				data += len;
				buf += len;
				if (leftover)
					savebyte[0] = *data++;
				len = 0;
			}
		}
		if (len < 0)
			panic("ec_write_mbuf: negative len");
#ifdef DIAGNOSTIC
		if (data != lim)
			panic("ec_write_mbuf: data != lim");
#endif
	}
	if (leftover) {
		savebyte[1] = 0;
		bus_space_write_2(memt, memh, buf, *(u_int16_t *)savebyte);
	}

	/*
	 * Switch back to receive page.
	 */
	if (esc->sc_16bitp)
		bus_space_write_1(asict, asich, ELINK2_GACFR,
		    ELINK2_GACFR_RSEL | ELINK2_GACFR_MBS0);
	
	return (savelen);
}

int
ec_ring_copy(struct dp8390_softc *sc, int src, caddr_t dst,
    u_short amount)
{
	struct ec_softc *esc = (struct ec_softc *)sc;
	u_short tmp_amount;

	/* Does copy wrap to lower addr in ring buffer? */
	if (src + amount > sc->mem_end) {
		tmp_amount = sc->mem_end - src;

		/* Copy amount up to end of NIC memory. */
		ec_readmem(esc, src, dst, tmp_amount);

		amount -= tmp_amount;
		src = sc->mem_ring;
		dst += tmp_amount;
	}

	ec_readmem(esc, src, dst, amount);

	return (src + amount);
}

void
ec_read_hdr(struct dp8390_softc *sc, int packet_ptr,
    struct dp8390_ring *packet_hdrp)
{
	struct ec_softc *esc = (struct ec_softc *)sc;

	ec_readmem(esc, packet_ptr, (u_int8_t *)packet_hdrp,
	    sizeof(struct dp8390_ring));
#if BYTE_ORDER == BIG_ENDIAN
	packet_hdrp->count = swap16(packet_hdrp->count);
#endif
}

void
ec_media_init(struct dp8390_softc *sc)
{
	ifmedia_init(&sc->sc_media, 0, dp8390_mediachange, dp8390_mediastatus);
	ifmedia_add(&sc->sc_media, IFM_ETHER|IFM_10_2, 0, NULL);
	ifmedia_add(&sc->sc_media, IFM_ETHER|IFM_10_5, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_10_2);
}

int
ec_mediachange(struct dp8390_softc *sc)
{
	struct ec_softc *esc = (struct ec_softc *)sc;
	struct ifmedia *ifm = &sc->sc_media;

	return (ec_set_media(esc, ifm->ifm_media));
}

void
ec_mediastatus(struct dp8390_softc *sc, struct ifmediareq *ifmr)
{
	struct ifmedia *ifm = &sc->sc_media;

	/*
	 * The currently selected media is always the active media.
	 */
	ifmr->ifm_active = ifm->ifm_cur->ifm_media;
}

void
ec_init_card(struct dp8390_softc *sc)
{
	struct ec_softc *esc = (struct ec_softc *)sc;
	struct ifmedia *ifm = &sc->sc_media;

	(void) ec_set_media(esc, ifm->ifm_cur->ifm_media);
}

int
ec_set_media(struct ec_softc *esc, uint64_t media)
{
	u_int8_t new;

	if (IFM_TYPE(media) != IFM_ETHER)
		return (EINVAL);

	switch (IFM_SUBTYPE(media)) {
	case IFM_10_2:
		new = ELINK2_CR_XSEL;
		break;

	case IFM_10_5:
		new = 0;
		break;

	default:
		return (EINVAL);
	}

	bus_space_write_1(esc->sc_asict, esc->sc_asich, ELINK2_CR, new);
	return (0);
}
