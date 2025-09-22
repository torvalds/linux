/*	$OpenBSD: pckbc_isa.c,v 1.19 2015/08/18 06:54:00 stsp Exp $	*/
/*	$NetBSD: pckbc_isa.c,v 1.2 2000/03/23 07:01:35 thorpej Exp $	*/

/*
 * Copyright (c) 1998
 *	Matthias Drochner.  All rights reserved.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include <dev/ic/i8042reg.h>
#include <dev/ic/pckbcvar.h>

int	pckbc_isa_match(struct device *, void *, void *);
void	pckbc_isa_attach(struct device *, struct device *, void *);
int	pckbc_isa_activate(struct device *, int);

const struct cfattach pckbc_isa_ca = {
	sizeof(struct pckbc_softc), pckbc_isa_match, pckbc_isa_attach,
	NULL, pckbc_isa_activate
};

int
pckbc_isa_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh_d, ioh_c;
	int res;

	/* If values are hardwired to something that they can't be, punt. */
	if ((ia->ia_iobase != IOBASEUNK && ia->ia_iobase != IO_KBD) ||
	    ia->ia_maddr != MADDRUNK ||
	    (ia->ia_irq != IRQUNK && ia->ia_irq != 1 /* XXX */) ||
	    ia->ia_drq != DRQUNK)
		return (0);

	if (pckbc_is_console(iot, IO_KBD) == 0) {
		if (bus_space_map(iot, IO_KBD + KBDATAP, 1, 0, &ioh_d))
			return (0);
		if (bus_space_map(iot, IO_KBD + KBCMDP, 1, 0, &ioh_c))
			goto fail;

		/* flush KBC */
		(void) pckbc_poll_data1(iot, ioh_d, ioh_c, PCKBC_KBD_SLOT, 0);

		/* KBC selftest */
		if (pckbc_send_cmd(iot, ioh_c, KBC_SELFTEST) == 0)
			goto fail2;
		res = pckbc_poll_data1(iot, ioh_d, ioh_c, PCKBC_KBD_SLOT, 0);
		if (res != 0x55) {
			printf("kbc selftest: %x\n", res);
			goto fail2;
		}
		bus_space_unmap(iot, ioh_c, 1);
		bus_space_unmap(iot, ioh_d, 1);
	}

	ia->ia_iobase = IO_KBD;
	ia->ia_iosize = 5;
	ia->ia_msize = 0x0;
	ia->ipa_nirq = PCKBC_NSLOTS;
	ia->ipa_irq[PCKBC_KBD_SLOT].num = 1;
	ia->ipa_irq[PCKBC_AUX_SLOT].num = 12;

	return (1);

fail2:
	bus_space_unmap(iot, ioh_c, 1);
fail:
	bus_space_unmap(iot, ioh_d, 1);
	return (0);
}

int
pckbc_isa_activate(struct device *self, int act)
{
	struct pckbc_softc *sc = (struct pckbc_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);
		pckbc_stop(sc);
		break;
	case DVACT_RESUME:
		pckbc_reset(sc);
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

void
pckbc_isa_attach(struct device *parent, struct device *self, void *aux)
{
	struct pckbc_softc *sc = (struct pckbc_softc *)self;
	struct cfdata *cf = self->dv_cfdata;
	struct isa_attach_args *ia = aux;
	struct pckbc_internal *t;
	bus_space_tag_t iot;
	bus_space_handle_t ioh_d, ioh_c;
	void *rv;
	int slot;

	iot = ia->ia_iot;

	printf("\n");

	for (slot = 0; slot < PCKBC_NSLOTS; slot++) {
		rv = isa_intr_establish(ia->ia_ic, ia->ipa_irq[slot].num,
		    IST_EDGE, IPL_TTY, pckbcintr, sc, sc->sc_dv.dv_xname);
		if (rv == NULL) {
			printf("%s: unable to establish interrupt for irq %d\n",
			    sc->sc_dv.dv_xname, ia->ipa_irq[slot].num);
			/* XXX fail attach? */
		}
	}

	if (pckbc_is_console(iot, IO_KBD)) {
		t = &pckbc_consdata;
		pckbc_console_attached = 1;
		/* t->t_cmdbyte was initialized by cnattach */
	} else {
		if (bus_space_map(iot, IO_KBD + KBDATAP, 1, 0, &ioh_d) ||
		    bus_space_map(iot, IO_KBD + KBCMDP, 1, 0, &ioh_c))
			panic("pckbc_attach: couldn't map");

		t = malloc(sizeof(*t), M_DEVBUF, M_WAITOK | M_ZERO);
		t->t_iot = iot;
		t->t_ioh_d = ioh_d;
		t->t_ioh_c = ioh_c;
		t->t_addr = IO_KBD;
		t->t_cmdbyte = KC8_CPU; /* Enable ports */
	}

	t->t_sc = sc;
	sc->id = t;

	/* Finish off the attach. */
	pckbc_attach(sc, cf->cf_flags);
}
