/* $OpenBSD: dwiic_pci.c,v 1.32 2025/08/21 03:03:52 jsg Exp $ */
/*
 * Synopsys DesignWare I2C controller
 * PCI attachment
 *
 * Copyright (c) 2015-2017 joshua stein <jcs@openbsd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include <sys/param.h>
#include <sys/systm.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/ic/dwiicvar.h>

/* 13.3: I2C Additional Registers Summary */
#define LPSS_RESETS		0x204
#define  LPSS_RESETS_I2C	(1 << 0) | (1 << 1)
#define  LPSS_RESETS_IDMA	(1 << 2)
#define LPSS_ACTIVELTR		0x210
#define LPSS_IDLELTR		0x214
#define LPSS_CAPS		0x2fc
#define  LPSS_CAPS_NO_IDMA	(1 << 8)
#define  LPSS_CAPS_TYPE_SHIFT	4
#define  LPSS_CAPS_TYPE_MASK	(0xf << LPSS_CAPS_TYPE_SHIFT)

int		dwiic_pci_match(struct device *, void *, void *);
void		dwiic_pci_attach(struct device *, struct device *, void *);
int		dwiic_pci_activate(struct device *, int);
void		dwiic_pci_bus_scan(struct device *,
		    struct i2cbus_attach_args *, void *);

#include "acpi.h"
#if NACPI > 0
struct aml_node *acpi_pci_match(struct device *dev, struct pci_attach_args *pa);
#endif

const struct cfattach dwiic_pci_ca = {
	sizeof(struct dwiic_softc),
	dwiic_pci_match,
	dwiic_pci_attach,
	NULL,
	dwiic_pci_activate,
};

const struct pci_matchid dwiic_pci_ids[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_100SERIES_I2C0 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_100SERIES_I2C1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_100SERIES_I2C2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_100SERIES_I2C3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_100SERIES_LP_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_100SERIES_LP_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_100SERIES_LP_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_100SERIES_LP_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_100SERIES_LP_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_100SERIES_LP_I2C_6 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_200SERIES_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_200SERIES_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_200SERIES_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_200SERIES_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_U_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_U_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_U_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_U_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_U_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_U_I2C_6 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_LP_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_LP_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_LP_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_LP_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_LP_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_LP_I2C_6 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_V_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_V_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_V_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_V_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_495SERIES_LP_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_495SERIES_LP_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_495SERIES_LP_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_495SERIES_LP_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_495SERIES_LP_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_495SERIES_LP_I2C_6 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_I2C_0 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_I2C_6 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_LP_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_LP_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_LP_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_LP_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_LP_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_LP_I2C_6 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_I2C_0 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_LP_I2C_0 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_LP_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_LP_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_LP_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_LP_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_LP_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_LP_I2C_6 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_LP_I2C_7 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_700SERIES_I2C_0 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_700SERIES_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_700SERIES_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_700SERIES_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_700SERIES_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_700SERIES_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_6 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_7 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_8 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_GLK_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_GLK_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_GLK_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_GLK_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_GLK_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_GLK_I2C_6 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_GLK_I2C_7 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_GLK_I2C_8 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_JSL_I2C_0 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_JSL_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_JSL_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_JSL_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_JSL_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_JSL_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_SIO_I2C_0 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_SIO_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_SIO_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_SIO_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_SIO_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_SIO_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_SIO_I2C_6 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_SIO_I2C_7 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ADL_N_I2C_0 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ADL_N_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ADL_N_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ADL_N_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ADL_N_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ADL_N_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ADL_N_I2C_6 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ADL_N_I2C_7 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_MTL_I2C_0 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_MTL_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_MTL_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_MTL_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_MTL_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_MTL_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_LNL_I2C_0 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_LNL_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_LNL_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_LNL_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_LNL_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_LNL_I2C_5 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ARL_U_I2C_0 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ARL_U_I2C_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ARL_U_I2C_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ARL_U_I2C_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ARL_U_I2C_4 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ARL_U_I2C_5 },
};

int
dwiic_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, dwiic_pci_ids, nitems(dwiic_pci_ids)));
}

void
dwiic_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct dwiic_softc *sc = (struct dwiic_softc *)self;
	struct pci_attach_args *pa = aux;
#if NACPI > 0
	struct aml_node *node;
#endif
	bus_size_t iosize;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	uint8_t type;

	memcpy(&sc->sc_paa, pa, sizeof(sc->sc_paa));

	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	if (pci_mapreg_map(pa, PCI_MAPREG_START, PCI_MAPREG_MEM_TYPE_64BIT, 0,
	    &sc->sc_iot, &sc->sc_ioh, NULL, &iosize, 0)) {
		printf(": can't map mem space\n");
		return;
	}

	sc->sc_caps = bus_space_read_4(sc->sc_iot, sc->sc_ioh, LPSS_CAPS);
	type = sc->sc_caps & LPSS_CAPS_TYPE_MASK;
	type >>= LPSS_CAPS_TYPE_SHIFT;
	if (type != 0) {
		printf(": type %d not supported\n", type);
		return;
	}

	/* un-reset - page 958 */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LPSS_RESETS,
	    (LPSS_RESETS_I2C | LPSS_RESETS_IDMA));

#if NACPI > 0
	/* fetch timing parameters from ACPI, if possible */
	node = acpi_pci_match(self, &sc->sc_paa);
	if (node != NULL) {
		sc->sc_devnode = node;

		dwiic_acpi_get_params(sc, "SSCN", &sc->ss_hcnt, &sc->ss_lcnt,
		    NULL);
		dwiic_acpi_get_params(sc, "FMCN", &sc->fs_hcnt, &sc->fs_lcnt,
		    &sc->sda_hold_time);
	}
#endif

	if (dwiic_init(sc)) {
		printf(": failed initializing\n");
		return;
	}

	/* leave the controller disabled */
	dwiic_write(sc, DW_IC_INTR_MASK, 0);
	dwiic_enable(sc, 0);
	dwiic_read(sc, DW_IC_CLR_INTR);

	/* install interrupt handler */
	sc->sc_poll = 1;
	if (pci_intr_map(&sc->sc_paa, &ih) == 0) {
		intrstr = pci_intr_string(sc->sc_paa.pa_pc, ih);
		sc->sc_ih = pci_intr_establish(sc->sc_paa.pa_pc, ih, IPL_BIO,
		    dwiic_intr, sc, sc->sc_dev.dv_xname);
		if (sc->sc_ih != NULL) {
			printf(": %s", intrstr);
			sc->sc_poll = 0;
		}
	}
	if (sc->sc_poll)
		printf(": polling");

	printf("\n");

	rw_init(&sc->sc_i2c_lock, "iiclk");

	/* setup and attach iic bus */
	sc->sc_i2c_tag.ic_cookie = sc;
	sc->sc_i2c_tag.ic_acquire_bus = dwiic_i2c_acquire_bus;
	sc->sc_i2c_tag.ic_release_bus = dwiic_i2c_release_bus;
	sc->sc_i2c_tag.ic_exec = dwiic_i2c_exec;
	sc->sc_i2c_tag.ic_intr_establish = dwiic_i2c_intr_establish;
	sc->sc_i2c_tag.ic_intr_string = dwiic_i2c_intr_string;

	bzero(&sc->sc_iba, sizeof(sc->sc_iba));
	sc->sc_iba.iba_name = "iic";
	sc->sc_iba.iba_tag = &sc->sc_i2c_tag;
	sc->sc_iba.iba_bus_scan = dwiic_pci_bus_scan;
	sc->sc_iba.iba_bus_scan_arg = sc;

	config_found((struct device *)sc, &sc->sc_iba, iicbus_print);

#if NACPI > 0 && !defined(SMALL_KERNEL)
	if (sc->sc_devnode) {
		sc->sc_devnode->i2c = &sc->sc_i2c_tag;
		acpi_register_gsb(acpi_softc, sc->sc_devnode);
	}
#endif
}

int
dwiic_pci_activate(struct device *self, int act)
{
	struct dwiic_softc *sc = (struct dwiic_softc *)self;

	switch (act) {
	case DVACT_RESUME:
		DELAY(10000);	/* 10 msec */
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, LPSS_RESETS,
		    (LPSS_RESETS_I2C | LPSS_RESETS_IDMA));
		DELAY(10000);	/* 10 msec */
		break;
	}
	return dwiic_activate(self, act);
}

void
dwiic_pci_bus_scan(struct device *iic, struct i2cbus_attach_args *iba,
    void *aux)
{
	struct dwiic_softc *sc = (struct dwiic_softc *)aux;

	sc->sc_iic = iic;

#if NACPI > 0
	if (sc->sc_devnode != NULL)
		aml_find_node(sc->sc_devnode, "_HID", dwiic_acpi_found_hid, sc);
#endif
}
