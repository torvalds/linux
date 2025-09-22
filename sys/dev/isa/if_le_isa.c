/*	$OpenBSD: if_le_isa.c,v 1.23 2022/04/06 18:59:28 naddy Exp $	*/
/*	$NetBSD: if_le_isa.c,v 1.2 1996/05/12 23:52:56 mycroft Exp $	*/

/*-
 * Copyright (c) 1995 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell and Rick Macklem.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)if_le.c	8.2 (Berkeley) 11/16/93
 */

#include "bpfilter.h"
#include "isadma.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>

#include <dev/ic/lancereg.h>
#include <dev/ic/lancevar.h>
#include <dev/ic/am7990reg.h>
#include <dev/ic/am7990var.h>

#include <dev/isa/if_levar.h>

static char *card_type[] =
    { "unknown", "BICC Isolan", "NE2100", "DEPCA", "PCnet-ISA" };

int	le_isa_probe(struct device *, void *, void *);
void	le_isa_attach(struct device *, struct device *, void *);

const struct cfattach le_isa_ca = {
	sizeof(struct le_softc), le_isa_probe, le_isa_attach
};

int	depca_isa_probe(struct le_softc *, struct isa_attach_args *);
int	ne2100_isa_probe(struct le_softc *, struct isa_attach_args *);
int	bicc_isa_probe(struct le_softc *, struct isa_attach_args *);
int	lance_isa_probe(struct lance_softc *);

int
le_isa_probe(struct device *parent, void *match, void *aux)
{
	struct le_softc *lesc = match;
	struct isa_attach_args *ia = aux;
	u_int8_t bogusether[ETHER_ADDR_LEN] = { 255, 255, 255, 255, 255, 255 };

#if NISADMA == 0
	if (ia->ia_drq != DRQUNK) {
		printf("cannot support dma lance devices\n");
		return 0;
	}
#endif

	if (bicc_isa_probe(lesc, ia) == 0 && ne2100_isa_probe(lesc, ia) == 0 &&
	    depca_isa_probe(lesc, ia) == 0)
		return (0);

	if (bcmp(lesc->sc_am7990.lsc.sc_arpcom.ac_enaddr, bogusether,
	    sizeof(bogusether)) == 0)
		return (0);

	return (1);
}

int
depca_isa_probe(struct le_softc *lesc, struct isa_attach_args *ia)
{
	struct lance_softc *sc = &lesc->sc_am7990.lsc;
	bus_space_tag_t iot = lesc->sc_iot;
	bus_space_handle_t ioh = lesc->sc_ioh;
	int iosize = 16;
	int port;

#if 0
	u_long sum, rom_sum;
	u_char x;
#endif
	int i;

	if (bus_space_map(iot, ia->ia_iobase, iosize, 0, &ioh))
		return (0);
	lesc->sc_iot = iot;
	lesc->sc_ioh = ioh;
	lesc->sc_rap = DEPCA_RAP;
	lesc->sc_rdp = DEPCA_RDP;
	lesc->sc_card = DEPCA;

	if (lance_isa_probe(sc) == 0) {
		bus_space_unmap(iot, ioh, iosize);
		return 0;
	}

	bus_space_write_1(iot, ioh, DEPCA_CSR, DEPCA_CSR_DUM);

	/*
	 * Extract the physical MAC address from the ROM.
	 *
	 * The address PROM is 32 bytes wide, and we access it through
	 * a single I/O port.  On each read, it rotates to the next
	 * position.  We find the ethernet address by looking for a
	 * particular sequence of bytes (0xff, 0x00, 0x55, 0xaa, 0xff,
	 * 0x00, 0x55, 0xaa), and then reading the next 8 bytes (the
	 * ethernet address and a checksum).
	 *
	 * It appears that the PROM can be at one of two locations, so
	 * we just try both.
	 */
	port = DEPCA_ADP;
	for (i = 0; i < 32; i++)
		if (bus_space_read_1(iot, ioh, port) == 0xff &&
		    bus_space_read_1(iot, ioh, port) == 0x00 &&
		    bus_space_read_1(iot, ioh, port) == 0x55 &&
		    bus_space_read_1(iot, ioh, port) == 0xaa &&
		    bus_space_read_1(iot, ioh, port) == 0xff &&
		    bus_space_read_1(iot, ioh, port) == 0x00 &&
		    bus_space_read_1(iot, ioh, port) == 0x55 &&
		    bus_space_read_1(iot, ioh, port) == 0xaa)
			goto found;
	port = DEPCA_ADP + 1;
	for (i = 0; i < 32; i++)
		if (bus_space_read_1(iot, ioh, port) == 0xff &&
		    bus_space_read_1(iot, ioh, port) == 0x00 &&
		    bus_space_read_1(iot, ioh, port) == 0x55 &&
		    bus_space_read_1(iot, ioh, port) == 0xaa &&
		    bus_space_read_1(iot, ioh, port) == 0xff &&
		    bus_space_read_1(iot, ioh, port) == 0x00 &&
		    bus_space_read_1(iot, ioh, port) == 0x55 &&
		    bus_space_read_1(iot, ioh, port) == 0xaa)
			goto found;
	printf("%s: address not found\n", sc->sc_dev.dv_xname);
	return 0;

found:
	for (i = 0; i < sizeof(sc->sc_arpcom.ac_enaddr); i++)
		sc->sc_arpcom.ac_enaddr[i] = bus_space_read_1(iot, ioh, port);

#if 0
	sum =
	    (sc->sc_arpcom.ac_enaddr[0] <<  2) +
	    (sc->sc_arpcom.ac_enaddr[1] << 10) +
	    (sc->sc_arpcom.ac_enaddr[2] <<  1) +
	    (sc->sc_arpcom.ac_enaddr[3] <<  9) +
	    (sc->sc_arpcom.ac_enaddr[4] <<  0) +
	    (sc->sc_arpcom.ac_enaddr[5] <<  8);
	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);

	rom_sum = bus_space_read_1(iot, ioh, port);
	rom_sum |= bus_space_read_1(iot, ioh, port << 8);

	if (sum != rom_sum) {
		printf("%s: checksum mismatch; calculated %04x != read %04x",
		    sc->sc_dev.dv_xname, sum, rom_sum);
		bus_space_unmap(iot, ioh, iosize);
		return 0;
	}
#endif

	bus_space_write_1(iot, ioh, DEPCA_CSR, DEPCA_CSR_NORMAL);

	ia->ia_iosize = iosize;
	ia->ia_drq = DRQUNK;
	bus_space_unmap(iot, ioh, ia->ia_iosize);
	return 1;
}

int
ne2100_isa_probe(struct le_softc *lesc, struct isa_attach_args *ia)
{
	struct lance_softc *sc = &lesc->sc_am7990.lsc;
	bus_space_tag_t iot = lesc->sc_iot;
	bus_space_handle_t ioh = lesc->sc_ioh;
	int iosize = 24;
	int i;

	if (bus_space_map(iot, ia->ia_iobase, iosize, 0, &ioh))
		return (0);
	lesc->sc_iot = iot;
	lesc->sc_ioh = ioh;
	lesc->sc_rap = NE2100_RAP;
	lesc->sc_rdp = NE2100_RDP;
	lesc->sc_card = NE2100;

	if (lance_isa_probe(sc) == 0) {
		bus_space_unmap(iot, ioh, iosize);
		return 0;
	}

	/*
	 * Extract the physical MAC address from the ROM.
	 */
	for (i = 0; i < sizeof(sc->sc_arpcom.ac_enaddr); i++)
		sc->sc_arpcom.ac_enaddr[i] = bus_space_read_1(iot, ioh, i);

	ia->ia_iosize = iosize;
	bus_space_unmap(iot, ioh, ia->ia_iosize);
	return 1;
}

int
bicc_isa_probe(struct le_softc *lesc, struct isa_attach_args *ia)
{
	struct lance_softc *sc = &lesc->sc_am7990.lsc;
	bus_space_handle_t ioh;
	bus_space_tag_t iot = ia->ia_iot;
	int iosize = 16;
	int i;

	if (bus_space_map(iot, ia->ia_iobase, iosize, 0, &ioh))
		return (0);
	lesc->sc_iot = iot;
	lesc->sc_ioh = ioh;
	lesc->sc_rap = BICC_RAP;
	lesc->sc_rdp = BICC_RDP;
	lesc->sc_card = BICC;

	if (lance_isa_probe(sc) == 0) {
		bus_space_unmap(iot, ioh, iosize);
		return 0;
	}

	/*
	 * Extract the physical MAC address from the ROM.
	 */
	for (i = 0; i < sizeof(sc->sc_arpcom.ac_enaddr); i++)
		sc->sc_arpcom.ac_enaddr[i] = bus_space_read_1(iot, ioh, i * 2);

	ia->ia_iosize = iosize;
	bus_space_unmap(iot, ioh, ia->ia_iosize);
	return 1;
}

/*
 * Determine which chip is present on the card.
 */
int
lance_isa_probe(struct lance_softc *sc)
{

	/* Stop the LANCE chip and put it in a known state. */
	le_isa_wrcsr(sc, LE_CSR0, LE_C0_STOP);
	delay(100);

	if (le_isa_rdcsr(sc, LE_CSR0) != LE_C0_STOP)
		return 0;

	le_isa_wrcsr(sc, LE_CSR3, sc->sc_conf3);
	return 1;
}

void
le_isa_attach(struct device *parent, struct device *self,
    void *aux)
{
	struct le_softc *lesc = (void *)self;
	struct lance_softc *sc = &lesc->sc_am7990.lsc;
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;

	if (bus_space_map(iot, ia->ia_iobase, ia->ia_iosize, 0, &ioh))
		panic("%s: can't map I/O-ports", sc->sc_dev.dv_xname);
	lesc->sc_iot = iot;
	lesc->sc_ioh = ioh;

	printf(": %s Ethernet\n", card_type[lesc->sc_card]);

	if (lesc->sc_card == DEPCA) {
		u_char *mem, val;
		int i;

		mem = sc->sc_mem = ISA_HOLE_VADDR(ia->ia_maddr);

		val = 0xff;
		for (;;) {
			for (i = 0; i < ia->ia_msize; i++)
				mem[i] = val;
			for (i = 0; i < ia->ia_msize; i++)
				if (mem[i] != val) {
					printf("%s: failed to clear memory\n",
					    sc->sc_dev.dv_xname);
					return;
				}
			if (val == 0x00)
				break;
			val -= 0x55;
		}

		sc->sc_conf3 = LE_C3_ACON;
		sc->sc_addr = 0;
		sc->sc_memsize = ia->ia_msize;
	} else {
		sc->sc_mem = malloc(16384, M_DEVBUF, M_NOWAIT);
		if (sc->sc_mem == 0) {
			printf("%s: couldn't allocate memory for card\n",
			    sc->sc_dev.dv_xname);
			return;
		}

		sc->sc_conf3 = 0;
		sc->sc_addr = kvtop(sc->sc_mem);
		sc->sc_memsize = 16384;
	}

	sc->sc_copytodesc = lance_copytobuf_contig;
	sc->sc_copyfromdesc = lance_copyfrombuf_contig;
	sc->sc_copytobuf = lance_copytobuf_contig;
	sc->sc_copyfrombuf = lance_copyfrombuf_contig;
	sc->sc_zerobuf = lance_zerobuf_contig;

	sc->sc_rdcsr = le_isa_rdcsr;
	sc->sc_wrcsr = le_isa_wrcsr;
	sc->sc_hwreset = NULL;
	sc->sc_hwinit = NULL;

	printf("%s", sc->sc_dev.dv_xname);
	am7990_config(&lesc->sc_am7990);

#if NISADMA > 0
	if (ia->ia_drq != DRQUNK)
		isadma_cascade(ia->ia_drq);
#endif

	lesc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq, IST_EDGE,
	    IPL_NET, le_isa_intredge, sc, sc->sc_dev.dv_xname);
}
