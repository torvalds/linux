/*	$OpenBSD: gscpm.c,v 1.14 2023/02/04 19:19:36 cheloha Exp $	*/
/*
 * Copyright (c) 2004 Alexander Yurchenko <grange@openbsd.org>
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
 * National Semiconductor Geode SC1100 SMI/ACPI module.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/timetc.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <i386/pci/gscpmreg.h>

struct gscpm_softc {
	struct device sc_dev;

	pci_chipset_tag_t sc_pc;
	pcitag_t sc_tag;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_acpi_ioh;
};

int	gscpm_match(struct device *, void *, void *);
void	gscpm_attach(struct device *, struct device *, void *);

void	gscpm_setperf(int);

u_int	gscpm_get_timecount(struct timecounter *tc);

struct timecounter gscpm_timecounter = {
	.tc_get_timecount = gscpm_get_timecount,
	.tc_counter_mask = 0xffffff,
	.tc_frequency = 3579545,
	.tc_name = "GSCPM",
	.tc_quality = 1000,
	.tc_priv = NULL,
	.tc_user = 0,
};

const struct cfattach gscpm_ca = {
	sizeof (struct gscpm_softc),
	gscpm_match,
	gscpm_attach
};

struct cfdriver gscpm_cd = {
	NULL, "gscpm", DV_DULL
};

#if 0
static void *gscpm_cookie;	/* XXX */
#endif

int
gscpm_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_NS &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_NS_SC1100_SMI)
		return (1);

	return (0);
}

void
gscpm_attach(struct device *parent, struct device *self, void *aux)
{
	struct gscpm_softc *sc = (struct gscpm_softc *)self;
	struct pci_attach_args *pa = aux;
	pcireg_t csr, acpibase;

	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;
	sc->sc_iot = pa->pa_iot;

	/* Enable I/O space */
	csr = pci_conf_read(sc->sc_pc, sc->sc_tag, PCI_COMMAND_STATUS_REG);
	pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_COMMAND_STATUS_REG,
	    csr | PCI_COMMAND_IO_ENABLE);

	/* Map ACPI registers */
	acpibase = pci_conf_read(sc->sc_pc, sc->sc_tag, GSCPM_ACPIBASE);
	if (PCI_MAPREG_IO_ADDR(acpibase) == 0 ||
	    bus_space_map(sc->sc_iot, PCI_MAPREG_IO_ADDR(acpibase),
	    GSCPM_ACPISIZE, 0, &sc->sc_acpi_ioh)) {
		printf(": failed to map ACPI registers\n");
		return;
	}

	printf("\n");

	/* Hook into the kern_tc */
	gscpm_timecounter.tc_priv = sc;
	tc_init(&gscpm_timecounter);

/* XXX: disabled due to unresolved yet hardware errata */
#if 0
	/* Hook into the hw.setperf sysctl */
	gscpm_cookie = sc;
	cpu_setperf = gscpm_setperf;
#endif

}

u_int
gscpm_get_timecount(struct timecounter *tc)
{
	struct gscpm_softc *sc = tc->tc_priv;

	return (bus_space_read_4(sc->sc_iot, sc->sc_acpi_ioh, GSCPM_PM_TMR));
}

#if 0
void
gscpm_setperf(int level)
{
	struct gscpm_softc *sc = gscpm_cookie;
	int i;
	u_int32_t pctl;

	pctl = bus_space_read_4(sc->sc_iot, sc->sc_acpi_ioh, GSCPM_P_CNT);

	if (level == 100) {
		/* 100 is a maximum performance, disable throttling */
		pctl &= ~GSCPM_P_CNT_THTEN;
	} else {
		for (i = 0; i < GSCPM_THT_LEVELS; i++)
			if (level >= gscpm_tht[i].level)
				break;
		pctl = (0xf0 | GSCPM_P_CNT_THTEN |
		    GSCPM_P_CNT_CLK(gscpm_tht[i].value));
	}

	/* Update processor control register */
	bus_space_write_4(sc->sc_iot, sc->sc_acpi_ioh, GSCPM_P_CNT, pctl);
}
#endif
