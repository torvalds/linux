/*	$OpenBSD: pckbc_ebus.c,v 1.17 2023/07/25 10:00:44 miod Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Driver for i8042 keyboard controller found on some PCI based
 * UltraSPARCs
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>

#include <dev/ic/i8042reg.h>
#include <dev/ic/pckbcvar.h>

struct pckbc_ebus_softc {
	struct pckbc_softc sc_pckbc;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	bus_space_handle_t sc_ioh_c;
	bus_space_handle_t sc_ioh_d;
	void *sc_irq[2];
	int sc_node;
};

int pckbc_ebus_match(struct device *, void *, void *);
void pckbc_ebus_attach(struct device *, struct device *, void *);

const struct cfattach pckbc_ebus_ca = {
	sizeof(struct pckbc_ebus_softc), pckbc_ebus_match, pckbc_ebus_attach
};

int pckbc_ebus_is_console(struct pckbc_ebus_softc *);

int
pckbc_ebus_match(struct device *parent, void *match, void *aux)
{
	struct ebus_attach_args *ea = aux;

	if (strcmp(ea->ea_name, "8042") == 0)
		return (1);
	return (0);
}

void
pckbc_ebus_attach(struct device *parent, struct device *self, void *aux)
{
	struct pckbc_ebus_softc *sc = (void *)self;
	struct pckbc_softc *psc = &sc->sc_pckbc;
	struct ebus_attach_args *ea = aux;
	struct pckbc_internal *t = NULL;
	int console;
	int flags = 0;

	sc->sc_node = ea->ea_node;
	console = pckbc_ebus_is_console(sc);

	/* Use prom address if available, otherwise map it. */
	if (ea->ea_nvaddrs && bus_space_map(ea->ea_iotag, ea->ea_vaddrs[0], 0,
	    0, &sc->sc_ioh) == 0) {
		sc->sc_iot = ea->ea_iotag;
	} else if (ebus_bus_map(ea->ea_iotag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]), ea->ea_regs[0].size,
	    0, 0, &sc->sc_ioh) == 0) {
		sc->sc_iot = ea->ea_iotag;
	} else if (ebus_bus_map(ea->ea_memtag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]), ea->ea_regs[0].size,
	    0, 0, &sc->sc_ioh) == 0) {
		sc->sc_iot = ea->ea_memtag;
	} else {
		printf(": can't map register space\n");
		return;
	}

	/*
	 * Tadpole/RDI systems use a 8042 controller which does not
	 * implement XT scan code translation.
	 * - on the SPARCLE and the Viper, which sport a PC-style
	 *   keyboard with no L function keys, the keyboard defaults
	 *   to scan code set #2.
	 * - on the UltraBook IIe, which sports a complete Sun-style
	 *   keyboard with L function keys and diamond keys,
	 *   the keyboard defaults to scan code set #3.
	 */
	{
		char buf[128];
		OF_getprop(ea->ea_node, "model", buf, sizeof buf);
		if (strcmp(buf, "INTC,80c42") == 0) {
			/*
			 * This is a Tadpole/RDI system. Tell the RDI design
			 * (UltraBook IIe) from the Tadpole design (SPARCLE)
			 * by looking for a tadpmu child node in the latter.
			 */
			int sparcle = 0;
			int node;
			for (node = OF_child(sc->sc_node); node;
			    node = OF_peer(node)) {
				if (OF_getprop(node, "name", buf,
				    sizeof buf) <= 0)
					continue;
				if (strcmp(buf, "tadpmu") == 0) {
					sparcle = 1;
					break;
				}
			}
			flags = PCKBC_NEED_AUXWRITE;
			if (sparcle)
				flags |= PCKBC_FIXED_SET2;
			else
				flags |= PCKBC_FIXED_SET3;
		}
	}

	if (console) {
		if (pckbc_cnattach(sc->sc_iot,
		    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]), KBCMDP, flags) == 0) {
			t = &pckbc_consdata;
			pckbc_console_attached = 1;
			sc->sc_ioh_c = t->t_ioh_c;
			sc->sc_ioh_d = t->t_ioh_d;
		} else
			console = 0;
	}

	if (console == 0) {
		if (bus_space_subregion(sc->sc_iot, sc->sc_ioh,
		    KBCMDP, sizeof(u_int32_t), &sc->sc_ioh_c) != 0) {
			printf(": couldn't get cmd subregion\n");
			return;
		}
		if (bus_space_subregion(sc->sc_iot, sc->sc_ioh,
		    KBDATAP, sizeof(u_int32_t), &sc->sc_ioh_d) != 0) {
			printf(": couldn't get data subregion\n");
			return;
		}

		t = malloc(sizeof(*t), M_DEVBUF, M_NOWAIT | M_ZERO);
		t->t_flags = flags;
	}

	sc->sc_irq[0] = bus_intr_establish(sc->sc_iot, ea->ea_intrs[0],
	    IPL_TTY, 0, pckbcintr, psc, self->dv_xname);
	if (sc->sc_irq[0] == NULL) {
		printf(": couldn't get intr0\n");
		return;
	}

	sc->sc_irq[1] = bus_intr_establish(sc->sc_iot, ea->ea_intrs[1],
	    IPL_TTY, 0, pckbcintr, psc, self->dv_xname);
	if (sc->sc_irq[1] == NULL) {
		printf(": couldn't get intr1\n");
		return;
	}

	t->t_iot = sc->sc_iot;
	t->t_ioh_c = sc->sc_ioh_c;
	t->t_ioh_d = sc->sc_ioh_d;
	t->t_cmdbyte = KC8_CPU;
	t->t_sc = psc;

	psc->id = t;

	printf("\n");
	pckbc_attach(psc, 0);
}

int
pckbc_ebus_is_console(struct pckbc_ebus_softc *sc)
{
	char *name;
	int node;

	/*
	 * Loop through the children of 8042 and see if the keyboard
	 * exists, and further, whether it is the console input device.
	 * This is almost redundant because 8042 doesn't show up in
	 * device tree unless a keyboard is in fact attached.
	 */
	for (node = OF_child(sc->sc_node); node; node = OF_peer(node)) {
		name = getpropstring(node, "name");
		if (name == NULL)
			continue;
		if (strcmp("kb_ps2", name) == 0 ||
		    strcmp("keyboard", name) == 0) {
			if (node == OF_instance_to_package(OF_stdin()))
				return (1);
		}
	}
	return (0);
}
