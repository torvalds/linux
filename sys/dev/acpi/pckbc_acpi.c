/*	$OpenBSD: pckbc_acpi.c,v 1.6 2025/06/16 15:44:35 kettenis Exp $	*/
/*
 * Copyright (c) 2024, 2025, Miodrag Vallat.
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
 */
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

#include <dev/acpi/acpidev.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/ic/i8042reg.h>
#include <dev/ic/pckbcvar.h>

/*
 * This driver is more complicated than it should have to be, as it needs
 * to gather the needs of pckbc (2 I/O ports, and up to 2 interrupts),
 * which may be scattered across two ACPI nodes.
 * Because of this, it is able to attach to two different ACPI nodes,
 * but the second attachment "hijacks" the first one's softc struct, so
 * that all the required data is gathered in one place.
 */

int	pckbc_acpi_match(struct device *, void *, void *);
void	pckbc_acpi_attach(struct device *, struct device *, void *);
int	pckbc_acpi_activate(struct device *, int);

struct pckbc_acpi_gpio_intr {
	struct aml_node *node;
	uint16_t pin;
	uint16_t flags;
};

struct pckbc_acpi_softc {
	struct pckbc_softc sc;
	/* regular interrupts */
	void *sc_ih[2];
	/* gpio interrupts */
	struct pckbc_acpi_gpio_intr sc_gpioint[2];
	unsigned int sc_nints, sc_ngpioints;
};

const struct cfattach pckbc_acpi_ca = {
	.ca_devsize = sizeof(struct pckbc_acpi_softc),
	.ca_match = pckbc_acpi_match,
	.ca_attach = pckbc_acpi_attach,
	.ca_activate = pckbc_acpi_activate
};

struct pckbc_acpi_crs_data {
	struct aml_node *basenode;
	struct pckbc_acpi_gpio_intr intrs[2];
	unsigned int nints;
};

int	pckbc_acpi_match_kbd(struct device *, void *, void *);
int	pckbc_acpi_match_mouse(struct device *, void *, void *,
	    struct pckbc_acpi_softc *);
void	pckbc_acpi_attach_kbd(struct device *, struct device *, void *);
void	pckbc_acpi_attach_mouse(struct device *, struct device *, void *);
struct pckbc_acpi_softc *pckbc_acpi_find(int);
void	pckbc_acpi_crs_walk(struct device *, struct aml_node *,
	    struct pckbc_acpi_crs_data *,
	    int (*)(int, union acpi_resource *, void *));
int	pckbc_acpi_getgpioirqcount(int, union acpi_resource *, void *);
int	pckbc_acpi_getgpioirqdata(int, union acpi_resource *, void *);
void	pckbc_acpi_register_gpio_intrs(struct device *);

int
pckbc_acpi_match(struct device *parent, void *match, void *aux)
{
	struct pckbc_acpi_softc *sc = pckbc_acpi_find(1);
	if (sc == NULL)	/* no pckbc@acpi attachment yet */
		return pckbc_acpi_match_kbd(parent, match, aux);
	else
		return pckbc_acpi_match_mouse(parent, match, aux, sc);
}

void
pckbc_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct pckbc_acpi_softc *sc = pckbc_acpi_find(2);
	if (sc == NULL)	/* no second pckbc@acpi attachment yet */
		return pckbc_acpi_attach_kbd(parent, self, aux);
	else
		return pckbc_acpi_attach_mouse(parent, self, aux);
}

const char *pckbc_acpi_cids_kbd[] = {
	"PNP0303",	/* IBM Enhanced Keyboard (101/102-key, PS/2 Mouse) */
	"PNP030B",
	"PNP0320",
	NULL
};

/*
 * Matching logic for the keyboard node. We want two I/O ports, at least
 * one interrupt, and either a HID match or a CID match (if explicitly
 * allowed, or if the interrupt is a GPIO interrupt).
 */
int
pckbc_acpi_match_kbd(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;
	struct pckbc_acpi_crs_data crsdata;
	int irq, rv;

	if (aaa->aaa_naddr < 2)
		return 0;
	rv = acpi_matchhids(aaa, pckbc_acpi_cids_kbd, cf->cf_driver->cd_name);
	if (rv == 0)
		return 0;
	/*
	 * If this device uses non-GPIO interrupts in an ISA-compatible
	 * way (i.e. edge interrupt, active high), then do not attach
	 * unless explicitly required by device flags.
	 */
	if (cf->cf_flags & 0x0001)
		return rv;
	if (acpi_legacy_free)
		return rv;
	if (aaa->aaa_nirq != 0) {
		for (irq = 0; irq < aaa->aaa_nirq; irq++) {
			if ((aaa->aaa_irq_flags[irq] &
			    (LR_EXTIRQ_MODE | LR_EXTIRQ_POLARITY)) !=
			    LR_EXTIRQ_MODE)
				break;
		}
		if (irq == aaa->aaa_nirq)
			return 0;	/* all legacy */
	} else {
		pckbc_acpi_crs_walk(parent, aaa->aaa_node, &crsdata,
		    pckbc_acpi_getgpioirqcount);
		if (crsdata.nints == 0)
			return 0;	/* interrupt, where art thou? */
	}

	return rv;
}

const char *pckbc_acpi_cids_mouse[] = {
	"PNP0F03",	/* Microsoft PS/2-style Mouse */
	"PNP0F13",	/* PS/2 Mouse */
	NULL
};

/*
 * Matching logic for the mouse node. We want a previous keyboard to have
 * attached, and want an interrupt if the keyboard node didn't have two.
 */
int
pckbc_acpi_match_mouse(struct device *parent, void *match, void *aux,
    struct pckbc_acpi_softc *pasc)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;
	struct pckbc_acpi_crs_data crsdata;
	int rv;

	/*
	 * We only need to attach the mouse node if the keyboard attachment
	 * succeeded, we need interrupt information for the aux slot, and
	 * this acpi node provides it.
	 */
	if ((pasc->sc_nints == 0 && pasc->sc_ngpioints == 0) ||
	    (pasc->sc_nints + pasc->sc_ngpioints == 2))
		return 0;
	rv = acpi_matchhids(aaa, pckbc_acpi_cids_mouse, cf->cf_driver->cd_name);
	if (rv == 0)
		return 0;
	/* perform the expensive checks last */
	if (aaa->aaa_nirq == 0) {
		pckbc_acpi_crs_walk(parent, aaa->aaa_node, &crsdata,
		    pckbc_acpi_getgpioirqcount);
		if (crsdata.nints == 0)
			return 0;
	}
	return rv;
}

int
pckbc_acpi_activate(struct device *self, int act)
{
	struct pckbc_acpi_softc *pasc = (struct pckbc_acpi_softc *)self;
	struct pckbc_softc *sc = &pasc->sc;
	int rv = 0;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);
		if (pasc->sc_nints + pasc->sc_ngpioints != 0)
			pckbc_stop(sc);
		break;
	case DVACT_RESUME:
		if (pasc->sc_nints + pasc->sc_ngpioints != 0)
			pckbc_reset(sc);
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return rv;
}

void
pckbc_acpi_attach_kbd(struct device *parent, struct device *self, void *aux)
{
	struct pckbc_acpi_softc *pasc = (struct pckbc_acpi_softc *)self;
	struct pckbc_softc *sc = &pasc->sc;
	struct acpi_attach_args *aaa = aux;
	struct pckbc_internal *t;
	struct pckbc_acpi_crs_data crsdata;
	bus_space_handle_t ioh_d, ioh_c;
	int irq, rv;

	if (aaa->aaa_nirq == 0)
		pckbc_acpi_crs_walk(parent, aaa->aaa_node, &crsdata,
		    pckbc_acpi_getgpioirqdata);

	printf(" addr 0x%llx/0x%llx 0x%llx/0x%llx", aaa->aaa_addr[0],
	    aaa->aaa_size[0], aaa->aaa_addr[1], aaa->aaa_size[1]);
	if (aaa->aaa_nirq == 0) {
		printf(" gpio irq pin");
		for (irq = 0; irq < crsdata.nints &&
		    irq < nitems(pasc->sc_gpioint); irq++)
			printf(" %d", crsdata.intrs[irq].pin);
	} else {
		printf(" irq");
		for (irq = 0; irq < aaa->aaa_nirq && irq < nitems(pasc->sc_ih);
		    irq++)
			printf(" %d", aaa->aaa_irq[irq]);
	}
	printf(": \"%s\"\n", aaa->aaa_dev);

	if (aaa->aaa_nirq == 0) {
		/* Remember GPIO interrupt details for later setup */
		for (irq = 0; irq < crsdata.nints; irq++) {
			if (pasc->sc_ngpioints == nitems(pasc->sc_gpioint))
				break;
			pasc->sc_gpioint[pasc->sc_ngpioints++] =
			    crsdata.intrs[irq];	/* struct copy */
		}
	} else {
		for (irq = 0; irq < aaa->aaa_nirq; irq++) {
			if (pasc->sc_nints == nitems(pasc->sc_ih))
				break;
			pasc->sc_ih[pasc->sc_nints] = acpi_intr_establish(
			    aaa->aaa_irq[irq], aaa->aaa_irq_flags[irq],
			    IPL_TTY, pckbcintr, pasc, self->dv_xname);
			if (pasc->sc_ih[pasc->sc_nints] == NULL) {
				printf("%s: can't establish interrupt %d\n",
				    self->dv_xname, aaa->aaa_irq[irq]);
				goto fail_intr;
			}
			pasc->sc_nints++;
		}
	}

	if (pckbc_is_console(aaa->aaa_bst[0], aaa->aaa_addr[0])) {
		t = &pckbc_consdata;
		pckbc_console_attached = 1;
		/* t->t_cmdbyte was initialized by cnattach */
	} else {
		if ((rv = bus_space_map(aaa->aaa_bst[0], aaa->aaa_addr[0], 1, 0,
		    &ioh_d)) != 0) {
			printf("%s: couldn't map data port (%d)\n",
			    self->dv_xname, rv);
			goto fail_mapd;
		}
		if ((rv = bus_space_map(aaa->aaa_bst[1], aaa->aaa_addr[1], 1, 0,
		    &ioh_c)) != 0) {
			printf("%s: couldn't map command port (%d)\n",
			    self->dv_xname, rv);
			goto fail_mapc;
		}

		t = malloc(sizeof(*t), M_DEVBUF, M_WAITOK | M_ZERO);
		/*
		 * pckbc should theoretically be updated to use separate
		 * bus_space_tag_t for the data and command ports, since on
		 * this particular attachment they appear as separate I/O
		 * resources. But since these are I/O resources, all
		 * aaa_bst[] are identical, so we can avoid this change
		 * for the time being as long as the logic in
		 * acpi_parse_resources() does not change.
		 */
		t->t_iot = aaa->aaa_bst[0];
		t->t_ioh_d = ioh_d;
		t->t_ioh_c = ioh_c;
		t->t_cmdbyte = KC8_CPU; /* Enable ports */
	}

	t->t_sc = sc;
	sc->id = t;

	pckbc_attach(sc, 0);
	config_defer(self, pckbc_acpi_register_gpio_intrs);
	return;

 fail_mapc:
	bus_space_unmap(aaa->aaa_bst[0], ioh_d, 1);
 fail_mapd:
 fail_intr:
	if (aaa->aaa_nirq != 0) {
		for (irq = pasc->sc_nints - 1; irq >= 0; irq--)
			acpi_intr_disestablish(pasc->sc_ih[irq]);
		pasc->sc_nints = 0;
	}
}

void
pckbc_acpi_attach_mouse(struct device *parent, struct device *self, void *aux)
{
	struct pckbc_acpi_softc *pasc = pckbc_acpi_find(1);
	struct acpi_attach_args *aaa = aux;
	struct pckbc_acpi_crs_data crsdata;
	int irq, base;

	if (aaa->aaa_nirq == 0)
		pckbc_acpi_crs_walk(parent, aaa->aaa_node, &crsdata,
		    pckbc_acpi_getgpioirqdata);

	if (aaa->aaa_nirq == 0) {
		printf(" gpio irq pin");
		for (irq = 0; irq < crsdata.nints &&
		    irq < nitems(pasc->sc_gpioint) - pasc->sc_ngpioints; irq++)
			printf(" %d", crsdata.intrs[irq].pin);
	} else {
		printf(" irq");
		for (irq = 0; irq < aaa->aaa_nirq &&
		    irq < nitems(pasc->sc_ih) - pasc->sc_nints; irq++)
			printf(" %d", aaa->aaa_irq[irq]);
	}
	printf(": \"%s\"\n", aaa->aaa_dev);

	if (aaa->aaa_nirq == 0) {
		/* Remember GPIO interrupt details for later setup */
		for (irq = 0; irq < crsdata.nints; irq++) {
			if (pasc->sc_ngpioints == nitems(pasc->sc_gpioint))
				break;
			pasc->sc_gpioint[pasc->sc_ngpioints++] =
			    crsdata.intrs[irq];	/* struct copy */
		}
	} else {
		base = pasc->sc_nints;
		for (irq = 0; irq < aaa->aaa_nirq; irq++) {
			if (pasc->sc_nints == nitems(pasc->sc_ih))
				break;
			pasc->sc_ih[pasc->sc_nints] = acpi_intr_establish(
			    aaa->aaa_irq[irq], aaa->aaa_irq_flags[irq],
			    IPL_TTY, pckbcintr, pasc, self->dv_xname);
			if (pasc->sc_ih[pasc->sc_nints] == NULL) {
				printf("%s: can't establish interrupt %d\n",
				    self->dv_xname, aaa->aaa_irq[irq]);
				goto fail_intr;
			}
			pasc->sc_nints++;
		}
	}

	return;

 fail_intr:
	if (aaa->aaa_nirq != 0) {
		for (irq = pasc->sc_nints - 1; irq >= base; irq--)
			acpi_intr_disestablish(pasc->sc_ih[irq]);
		pasc->sc_nints = base;
	}
}

/*
 * Register all GPIO interrupts.
 * This is done after all acpi devices have attached, as the GPIO handler
 * may attach after us.
 */
void
pckbc_acpi_register_gpio_intrs(struct device *dev)
{
	struct pckbc_acpi_softc *sc = (struct pckbc_acpi_softc *)dev;
	int irq;

	for (irq = 0; irq < sc->sc_ngpioints; irq++) {
		struct acpi_gpio *gpio = sc->sc_gpioint[irq].node->gpio;
		if (gpio == NULL) {
			printf("%s: unable to setup gpio pin %d interrupt\n",
			    dev->dv_xname, sc->sc_gpioint[irq].pin);
			continue;
		}
		gpio->intr_establish(gpio->cookie, sc->sc_gpioint[irq].pin,
		    sc->sc_gpioint[irq].flags, IPL_TTY, pckbcintr, sc);
	}
}

/*
 * Iterate over all pckbc attachments. The `nth' argument tells us which
 * pckbc@acpi device to return.
 *
 * Note that, at `match' time, the device we may end up attaching is not
 * found, but at `attach' time, it will be found.
 */
struct pckbc_acpi_softc *
pckbc_acpi_find(int nth)
{
	extern struct cfdriver pckbc_cd;
	struct device *sc;
	int devno;

	for (devno = 0; devno < pckbc_cd.cd_ndevs; devno++) {
		if ((sc = pckbc_cd.cd_devs[devno]) == NULL)
			continue;
		if (sc->dv_cfdata->cf_attach != &pckbc_acpi_ca)
			continue;
		if (--nth == 0)
			return (struct pckbc_acpi_softc *)sc;
	}

	return NULL;
}

/*
 * _CRS resource walker.
 */
void
pckbc_acpi_crs_walk(struct device *acpidev, struct aml_node *basenode,
    struct pckbc_acpi_crs_data *crsdata,
    int (*walker)(int, union acpi_resource *, void *))
{
	struct aml_value val;

	memset(crsdata, 0, sizeof *crsdata);
	crsdata->basenode = basenode;
	if (aml_evalname((struct acpi_softc *)acpidev, basenode, "_CRS",
	    0, NULL, &val) != 0)
		return;
	if (val.type == AML_OBJTYPE_BUFFER && val.length >= 5)
		aml_parse_resource(&val, walker, crsdata);
	aml_freevalue(&val);
}

/*
 * _CRS walker callback, which counts GPIO interrupts.
 */
int
pckbc_acpi_getgpioirqcount(int crsidx, union acpi_resource *crs, void *arg)
{
	struct pckbc_acpi_crs_data *crsdata = arg;
	struct aml_node *node;

	if (crsdata->nints == nitems(crsdata->intrs))
		return 0;

	switch (AML_CRSTYPE(crs)) {
	case LR_GPIO:
		if (crs->lr_gpio.type != LR_GPIO_INT)
			break;
		node = aml_searchname(crsdata->basenode,
		    (char *)&crs->pad[crs->lr_gpio.res_off]);
		if (node != NULL)
			crsdata->nints++;
		break;
	}
	return 0;
}

/*
 * _CRS walker callback, which registers GPIO interrupt details.
 */
int
pckbc_acpi_getgpioirqdata(int crsidx, union acpi_resource *crs, void *arg)
{
	struct pckbc_acpi_crs_data *crsdata = arg;
	struct aml_node *node;

	if (crsdata->nints == nitems(crsdata->intrs))
		return 0;

	switch (AML_CRSTYPE(crs)) {
	case LR_GPIO:
		if (crs->lr_gpio.type != LR_GPIO_INT)
			break;
		node = aml_searchname(crsdata->basenode,
		    (char *)&crs->pad[crs->lr_gpio.res_off]);
		if (node != NULL) {
			crsdata->intrs[crsdata->nints].node = node;
			crsdata->intrs[crsdata->nints].pin =
			    *(uint16_t *)&crs->pad[crs->lr_gpio.pin_off];
			crsdata->intrs[crsdata->nints].flags =
			    crs->lr_gpio.tflags;
			crsdata->nints++;
		}
		break;
	}
	return 0;
}
