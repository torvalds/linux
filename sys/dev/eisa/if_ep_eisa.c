/*	$OpenBSD: if_ep_eisa.c,v 1.29 2023/09/11 08:41:26 mvs Exp $	*/
/*	$NetBSD: if_ep_eisa.c,v 1.13 1997/04/18 00:50:33 cgd Exp $	*/

/*
 * Copyright (c) 1997 Jonathan Stone <jonathan@NetBSD.org>
 * Copyright (c) 1994 Herb Peyerl <hpeyerl@beer.org>
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
 *      This product includes software developed by Herb Peyerl.
 * 4. The name of Herb Peyerl may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/syslog.h>
#include <sys/timeout.h>
#include <sys/device.h>

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

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/ic/elink3var.h>
#include <dev/ic/elink3reg.h>

#include <dev/eisa/eisareg.h>
#include <dev/eisa/eisavar.h>
#include <dev/eisa/eisadevs.h>

int ep_eisa_match(struct device *, void *, void *);
void ep_eisa_attach(struct device *, struct device *, void *);

const struct cfattach ep_eisa_ca = {
	sizeof(struct ep_softc), ep_eisa_match, ep_eisa_attach
};

/* XXX move these somewhere else */
#define EISA_CONTROL	0x0c84
#define EISA_RESET	0x04
#define EISA_ERROR	0x02
#define EISA_ENABLE	0x01

int
ep_eisa_match(struct device *parent, void *match, void *aux)
{
	struct eisa_attach_args *ea = aux;

	/* must match one of our known ID strings */
	if (strcmp(ea->ea_idstring, "TCM5090") &&
	    strcmp(ea->ea_idstring, "TCM5091") &&
	    strcmp(ea->ea_idstring, "TCM5092") &&
	    strcmp(ea->ea_idstring, "TCM5093") &&
	    strcmp(ea->ea_idstring, "TCM5094") &&
	    strcmp(ea->ea_idstring, "TCM5095") &&
	    strcmp(ea->ea_idstring, "TCM5098") &&
	    strcmp(ea->ea_idstring, "TCM5920") &&
	    strcmp(ea->ea_idstring, "TCM5970") &&
	    strcmp(ea->ea_idstring, "TCM5971") &&
	    strcmp(ea->ea_idstring, "TCM5972"))
		return (0);

	return (1);
}

void
ep_eisa_attach(struct device *parent, struct device *self, void *aux)
{
	struct ep_softc *sc = (void *)self;
	struct eisa_attach_args *ea = aux;
	bus_space_tag_t iot = ea->ea_iot;
	bus_space_handle_t ioh;
	u_int16_t k;
	eisa_chipset_tag_t ec = ea->ea_ec;
	eisa_intr_handle_t ih;
	const char *model, *intrstr;
	int chipset;
	u_int irq;

	/* Map i/o space. */
	if (bus_space_map(iot, EISA_SLOT_ADDR(ea->ea_slot),
	    EISA_SLOT_SIZE, 0, &ioh))
		panic(": can't map i/o space");

	sc->bustype = EP_BUS_EISA;
	sc->sc_ioh = ioh;
	sc->sc_iot = iot;

	bus_space_write_1(iot, ioh, EISA_CONTROL, EISA_ENABLE);
	delay(4000);

	/* XXX What is this doing?!  Reading the i/o address? */
	k = bus_space_read_2(iot, ioh, EP_W0_ADDRESS_CFG);
	k = (k & 0x1f) * 0x10 + 0x200;

	/* Read the IRQ from the card. */
	irq = bus_space_read_2(iot, ioh, EP_W0_RESOURCE_CFG) >> 12;

	chipset = EP_CHIPSET_3C509;	/* assume dumb chipset */
	if (strcmp(ea->ea_idstring, "TCM5090") == 0)
		model = EISA_PRODUCT_TCM5090;
	else if (strcmp(ea->ea_idstring, "TCM5091") == 0)
		model = EISA_PRODUCT_TCM5091;
	else if (strcmp(ea->ea_idstring, "TCM5092") == 0)
		model = EISA_PRODUCT_TCM5092;
	else if (strcmp(ea->ea_idstring, "TCM5093") == 0)
		model = EISA_PRODUCT_TCM5093;
	else if (strcmp(ea->ea_idstring, "TCM5094") == 0)
		model = EISA_PRODUCT_TCM5094;
	else if (strcmp(ea->ea_idstring, "TCM5095") == 0)
		model = EISA_PRODUCT_TCM5095;
	else if (strcmp(ea->ea_idstring, "TCM5098") == 0)
		model = EISA_PRODUCT_TCM5098;
	else if (strcmp(ea->ea_idstring, "TCM5920") == 0) {
		model = EISA_PRODUCT_TCM5920;
		chipset = EP_CHIPSET_VORTEX;
	} else if (strcmp(ea->ea_idstring, "TCM5970") == 0) {
		model = EISA_PRODUCT_TCM5970;
		chipset = EP_CHIPSET_VORTEX;
	} else if (strcmp(ea->ea_idstring, "TCM5971") == 0) {
		model = EISA_PRODUCT_TCM5971;
		chipset = EP_CHIPSET_VORTEX;
	} else if (strcmp(ea->ea_idstring, "TCM5972") == 0) {
		model = EISA_PRODUCT_TCM5972;
		chipset = EP_CHIPSET_VORTEX;
	} else
		model = "unknown model!";

	if (eisa_intr_map(ec, irq, &ih)) {
		printf(": couldn't map interrupt (%u)\n", irq);
		bus_space_unmap(iot, ioh, EISA_SLOT_SIZE);
		return;
	}
	intrstr = eisa_intr_string(ec, ih);
	sc->sc_ih = eisa_intr_establish(ec, ih, IST_EDGE, IPL_NET,
	    epintr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		bus_space_unmap(iot, ioh, EISA_SLOT_SIZE);
		return;
	}

	printf(": %s,", model);
	if (intrstr != NULL)
		printf(" %s,", intrstr);

	epconfig(sc, chipset, NULL);
	/* XXX because epconfig() will not print a newline for vortex chips */
	if (chipset == EP_CHIPSET_VORTEX)
		printf("\n");
}
