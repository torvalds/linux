/* $OpenBSD: imxiic_acpi.c,v 1.4 2022/04/06 18:59:27 naddy Exp $ */
/*
 * Copyright (c) 2015, 2016 joshua stein <jcs@openbsd.org>
 * Copyright (c) 2020 Patrick Wildt <patrick@blueri.se>
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
#include <sys/kernel.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/ic/imxiicvar.h>

struct imxiic_acpi_softc {
	struct imxiic_softc	ac_sc;
	struct acpi_softc	*ac_acpi;
	struct aml_node		*ac_devnode;
	struct device		*ac_iic;
};

struct imxiic_crs {
	uint16_t i2c_addr;
	struct aml_node *devnode;
};

int	imxiic_acpi_match(struct device *, void *, void *);
void	imxiic_acpi_attach(struct device *, struct device *, void *);

int	imxiic_acpi_parse_crs(int, union acpi_resource *, void *);
void	imxiic_acpi_bus_scan(struct device *, struct i2cbus_attach_args *,
	    void *);
int	imxiic_acpi_found_hid(struct aml_node *, void *);

const struct cfattach imxiic_acpi_ca = {
	sizeof(struct imxiic_acpi_softc),
	imxiic_acpi_match,
	imxiic_acpi_attach,
	NULL,
	NULL,
};

const char *imxiic_hids[] = {
	"NXP0001",
	NULL
};

int
imxiic_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1)
		return 0;
	return acpi_matchhids(aaa, imxiic_hids, cf->cf_driver->cd_name);
}

void
imxiic_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxiic_acpi_softc *ac = (struct imxiic_acpi_softc *)self;
	struct imxiic_softc *sc = &ac->ac_sc;
	struct acpi_attach_args *aaa = aux;
	struct i2cbus_attach_args iba;

	ac->ac_acpi = (struct acpi_softc *)parent;
	ac->ac_devnode = aaa->aaa_node;

	printf(" %s", ac->ac_devnode->name);

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);

	sc->sc_iot = aaa->aaa_bst[0];
	if (bus_space_map(sc->sc_iot, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_reg_shift = 0;
	sc->sc_clk_div = imxiic_vf610_clk_div;
	sc->sc_clk_ndiv = nitems(imxiic_vf610_clk_div);
	sc->sc_type = I2C_TYPE_VF610;

	/*
	 * Older versions of the ACPI tables for this device had the naming for
	 * the clkrate and bitrate confused.  For those, keep the old value of
	 * 100 kHz.
	 */
	sc->sc_clkrate = acpi_getpropint(ac->ac_devnode,
	    "uefi-clock-frequency", 0) / 1000;
	sc->sc_bitrate = acpi_getpropint(ac->ac_devnode,
	    "clock-frequency", 0) / 1000;
	if (sc->sc_clkrate == 0) {
		sc->sc_clkrate = acpi_getpropint(ac->ac_devnode,
		    "clock-frequency", 0) / 1000;
		sc->sc_bitrate = 100000 / 1000;
	}
	if (sc->sc_clkrate == 0) {
		printf(": clock frequency unknown\n");
		return;
	}

	printf("\n");

	imxiic_setspeed(sc, sc->sc_bitrate);
	imxiic_enable(sc, 0);

	sc->stopped = 1;
	rw_init(&sc->sc_buslock, sc->sc_dev.dv_xname);

	sc->i2c_tag.ic_cookie = sc;
	sc->i2c_tag.ic_acquire_bus = imxiic_i2c_acquire_bus;
	sc->i2c_tag.ic_release_bus = imxiic_i2c_release_bus;
	sc->i2c_tag.ic_exec = imxiic_i2c_exec;

	bzero(&iba, sizeof iba);
	iba.iba_name = "iic";
	iba.iba_tag = &sc->i2c_tag;
	iba.iba_bus_scan = imxiic_acpi_bus_scan;
	iba.iba_bus_scan_arg = sc;
	config_found(&sc->sc_dev, &iba, iicbus_print);

#ifndef SMALL_KERNEL
	ac->ac_devnode->i2c = &sc->i2c_tag;
	acpi_register_gsb(ac->ac_acpi, ac->ac_devnode);
#endif
}

int
imxiic_acpi_parse_crs(int crsidx, union acpi_resource *crs, void *arg)
{
	struct imxiic_crs *sc_crs = arg;

	switch (AML_CRSTYPE(crs)) {
	case LR_SERBUS:
		if (crs->lr_serbus.type == LR_SERBUS_I2C)
			sc_crs->i2c_addr = crs->lr_i2cbus._adr;
		break;
	case SR_IRQ:
	case LR_EXTIRQ:
	case LR_MEM32:
	case LR_MEM32FIXED:
		break;
	default:
		printf("%s: unknown resource type %d\n", __func__,
		    AML_CRSTYPE(crs));
	}

	return 0;
}

void
imxiic_acpi_bus_scan(struct device *iic, struct i2cbus_attach_args *iba,
    void *aux)
{
	struct imxiic_acpi_softc *ac = (struct imxiic_acpi_softc *)aux;

	ac->ac_iic = iic;
	aml_find_node(ac->ac_devnode, "_HID", imxiic_acpi_found_hid, ac);
}

int
imxiic_acpi_found_hid(struct aml_node *node, void *arg)
{
	struct imxiic_acpi_softc *ac = (struct imxiic_acpi_softc *)arg;
	struct imxiic_softc *sc = &ac->ac_sc;
	struct imxiic_crs crs;
	struct aml_value res;
	int64_t sta;
	char cdev[16], dev[16];
	struct i2c_attach_args ia;

	/* Skip our own _HID. */
	if (node->parent == ac->ac_devnode)
		return 0;

	/* Only direct descendants, because of possible muxes. */
	if (node->parent && node->parent->parent != ac->ac_devnode)
		return 0;

	if (acpi_parsehid(node, arg, cdev, dev, 16) != 0)
		return 0;

	sta = acpi_getsta(acpi_softc, node->parent);
	if ((sta & STA_PRESENT) == 0)
		return 0;

	if (aml_evalname(acpi_softc, node->parent, "_CRS", 0, NULL, &res))
		return 0;

	if (res.type != AML_OBJTYPE_BUFFER || res.length < 5) {
		printf("%s: invalid _CRS object (type %d len %d)\n",
		    sc->sc_dev.dv_xname, res.type, res.length);
		aml_freevalue(&res);
		return (0);
	}
	memset(&crs, 0, sizeof(crs));
	crs.devnode = ac->ac_devnode;
	aml_parse_resource(&res, imxiic_acpi_parse_crs, &crs);
	aml_freevalue(&res);

	acpi_attach_deps(acpi_softc, node->parent);

	memset(&ia, 0, sizeof(ia));
	ia.ia_tag = &sc->i2c_tag;
	ia.ia_name = dev;
	ia.ia_addr = crs.i2c_addr;
	ia.ia_cookie = node->parent;

	config_found(ac->ac_iic, &ia, iicbus_print);
	node->parent->attached = 1;

	return 0;
}
