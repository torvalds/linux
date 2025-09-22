/*	$OpenBSD: if_lc_isa.c,v 1.15 2023/09/11 08:41:26 mvs Exp $ */
/*	$NetBSD: if_lc_isa.c,v 1.10 2001/06/13 10:46:03 wiz Exp $ */

/*-
 * Copyright (c) 1994, 1995, 1997 Matt Thomas <matt@3am-software.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * DEC EtherWORKS 3 Ethernet Controllers
 *
 * Written by Matt Thomas
 *
 *   This driver supports the LEMAC (DE203, DE204, and DE205) cards.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/cpu.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ic/lemacreg.h>
#include <dev/ic/lemacvar.h>

#include <dev/isa/isavar.h>

extern struct cfdriver lc_cd;

int	lemac_isa_find(struct lemac_softc *, struct isa_attach_args *,
    int);
int	lemac_isa_probe(struct device *, void *, void *);
void	lemac_isa_attach(struct device *, struct device *, void *);

const struct cfattach lc_isa_ca = {
	sizeof(struct lemac_softc), lemac_isa_probe, lemac_isa_attach
};

int
lemac_isa_find(struct lemac_softc *sc, struct isa_attach_args *ia, int attach)
{
	bus_addr_t maddr;
	bus_size_t msize;
	int rv = 0, irq;

	/*
	 * Disallow wildcarded i/o addresses.
	 */
	if (ia->ia_iobase == IOBASEUNK)
		return 0;

	/*
	 * Make sure this is a valid LEMAC address.
	 */
	if (ia->ia_iobase & (LEMAC_IOSIZE - 1))
		return 0;

	sc->sc_iot = ia->ia_iot;

	/*
	 * Map the LEMAC's port space for the probe sequence.
	 */
	ia->ia_iosize = LEMAC_IOSIZE;

	if (bus_space_map(sc->sc_iot, ia->ia_iobase, ia->ia_iosize, 0,
	    &sc->sc_ioh)) {
		if (attach)
			printf(": can't map i/o space\n");
		return (0);
	}

	/*
	 * Read the Ethernet address from the EEPROM.
	 * It must start with one of the DEC OUIs and pass the
	 * DEC ethernet checksum test.
	 */
	if (lemac_port_check(sc->sc_iot, sc->sc_ioh) == 0)
		goto outio;

	/*
	 * Get information about memory space and attempt to map it.
	 */
	lemac_info_get(sc->sc_iot, sc->sc_ioh, &maddr, &msize, &irq);

	if (ia->ia_maddr != maddr && ia->ia_maddr != MADDRUNK)
		goto outio;

	if (maddr != 0 && msize != 0) {
		sc->sc_memt = ia->ia_memt;
		if (bus_space_map(ia->ia_memt, maddr, msize, 0,
		    &sc->sc_memh)) {
			if (attach)
				printf(": can't map mem space\n");
			goto outio;
		}
	}

	/*
	 * Double-check IRQ configuration.
	 */
	if (ia->ia_irq != irq && ia->ia_irq != IRQUNK)
		printf("%s: overriding IRQ %d to %d\n", sc->sc_dv.dv_xname,
		    ia->ia_irq, irq);

	if (attach) {
		lemac_ifattach(sc);

		sc->sc_ih = isa_intr_establish(ia->ia_ic, irq, IST_EDGE,
		    IPL_NET, lemac_intr, sc, sc->sc_dv.dv_xname);
	}

	/*
	 * I guess we've found one.
	 */
	rv = 1;

	ia->ia_maddr = maddr;
	ia->ia_msize = msize;
	ia->ia_irq = irq;

	if (maddr != 0 && msize != 0 && (rv == 0 || !attach))
		bus_space_unmap(sc->sc_memt, sc->sc_memh, msize);
outio:
	if (rv == 0 || !attach)
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, LEMAC_IOSIZE);
	return (rv);
}

int
lemac_isa_probe(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	struct cfdata *cf = ((struct device *)match)->dv_cfdata;
	struct lemac_softc sc;

	snprintf(sc.sc_dv.dv_xname, sizeof sc.sc_dv.dv_xname, "%s%d",
	    lc_cd.cd_name, cf->cf_unit);
    
	return (lemac_isa_find(&sc, ia, 0));
}

void
lemac_isa_attach(struct device *parent, struct device *self, void *aux)
{
	struct lemac_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;

	lemac_isa_find(sc, ia, 1);
}
