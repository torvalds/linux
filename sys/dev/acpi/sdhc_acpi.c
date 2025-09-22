/*	$OpenBSD: sdhc_acpi.c,v 1.24 2025/06/11 09:57:01 kettenis Exp $	*/
/*
 * Copyright (c) 2016 Mark Kettenis
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

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>
#undef DEVNAME
#include <dev/sdmmc/sdhcreg.h>
#include <dev/sdmmc/sdhcvar.h>
#include <dev/sdmmc/sdmmcvar.h>

struct sdhc_acpi_softc {
	struct sdhc_softc sc;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;

	bus_space_tag_t sc_memt;
	bus_space_handle_t sc_memh;
	void *sc_ih;

	struct aml_node *sc_gpio_int_node;
	struct aml_node *sc_gpio_io_node;
	uint16_t sc_gpio_int_pin;
	uint16_t sc_gpio_int_flags;
	uint16_t sc_gpio_io_pin;

	struct sdhc_host *sc_host;
};

int	sdhc_acpi_match(struct device *, void *, void *);
void	sdhc_acpi_attach(struct device *, struct device *, void *);

const struct cfattach sdhc_acpi_ca = {
	sizeof(struct sdhc_acpi_softc), sdhc_acpi_match, sdhc_acpi_attach,
	NULL, sdhc_activate
};

const char *sdhc_hids[] = {
	"PNP0D40",
	"80860F14",
	"BCM2847",	/* Raspberry Pi3/4 "arasan" controller */
	"BRCME88C",	/* Raspberry Pi4 "emmc2" controller */
	"INT33BB",
	"PNP0FFF",
	"QCOM24BF",
	NULL
};

int	sdhc_acpi_parse_resources(int, union acpi_resource *, void *);
int	sdhc_acpi_card_detect_nonremovable(struct sdhc_softc *);
int	sdhc_acpi_card_detect_gpio(struct sdhc_softc *);
int	sdhc_acpi_card_detect_intr(void *);
void	sdhc_acpi_power_on(struct sdhc_acpi_softc *, struct aml_node *);
void	sdhc_acpi_explore(struct sdhc_acpi_softc *);

int
sdhc_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1 || aaa->aaa_nirq < 1)
		return 0;
	return acpi_matchhids(aaa, sdhc_hids, cf->cf_driver->cd_name);
}

void
sdhc_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct sdhc_acpi_softc *sc = (struct sdhc_acpi_softc *)self;
	struct acpi_attach_args *aaa = aux;
	struct aml_value res;
	uint64_t capmask, capset;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	if (aml_evalname(sc->sc_acpi, sc->sc_node, "_CRS", 0, NULL, &res)) {
		printf(": can't find registers\n");
		return;
	}

	aml_parse_resource(&res, sdhc_acpi_parse_resources, sc);

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);
	printf(" irq %d", aaa->aaa_irq[0]);

	sc->sc_memt = aaa->aaa_bst[0];
	if (bus_space_map(sc->sc_memt, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc_memh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_ih = acpi_intr_establish(aaa->aaa_irq[0], aaa->aaa_irq_flags[0],
	    IPL_BIO, sdhc_intr, sc, sc->sc.sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	if (sc->sc_gpio_io_node && sc->sc_gpio_io_node->gpio) {
		sc->sc.sc_card_detect = sdhc_acpi_card_detect_gpio;
		printf(", gpio");
	}

	printf("\n");

	if (sc->sc_gpio_int_node && sc->sc_gpio_int_node->gpio) {
		struct acpi_gpio *gpio = sc->sc_gpio_int_node->gpio;

		gpio->intr_establish(gpio->cookie, sc->sc_gpio_int_pin,
		    sc->sc_gpio_int_flags, IPL_BIO,
		    sdhc_acpi_card_detect_intr, sc);
	}

	sdhc_acpi_power_on(sc, sc->sc_node);
	sdhc_acpi_explore(sc);

	capmask = acpi_getpropint(sc->sc_node, "sdhci-caps-mask", 0);
	capset = acpi_getpropint(sc->sc_node, "sdhci-caps", 0);

	/* Raspberry Pi4 "emmc2" controller. */
	if (strcmp(aaa->aaa_dev, "BRCME88C") == 0)
		sc->sc.sc_flags |= SDHC_F_NOPWR0;

	sc->sc.sc_host = &sc->sc_host;
	sc->sc.sc_dmat = aaa->aaa_dmat;
	sc->sc.sc_clkbase = acpi_getpropint(sc->sc_node,
	    "clock-frequency", 0) / 1000;
	sdhc_host_found(&sc->sc, sc->sc_memt, sc->sc_memh,
	    aaa->aaa_size[0], 1, capmask, capset);
}

int
sdhc_acpi_parse_resources(int crsidx, union acpi_resource *crs, void *arg)
{
	struct sdhc_acpi_softc *sc = arg;
	int type = AML_CRSTYPE(crs);
	struct aml_node *node;
	uint16_t pin;

	switch (type) {
	case LR_GPIO:
		node = aml_searchname(sc->sc_node, (char *)&crs->pad[crs->lr_gpio.res_off]);
		pin = *(uint16_t *)&crs->pad[crs->lr_gpio.pin_off];
		if (crs->lr_gpio.type == LR_GPIO_INT) {
			sc->sc_gpio_int_node = node;
			sc->sc_gpio_int_pin = pin;
			sc->sc_gpio_int_flags = crs->lr_gpio.tflags;
		} else if (crs->lr_gpio.type == LR_GPIO_IO) {
			sc->sc_gpio_io_node = node;
			sc->sc_gpio_io_pin = pin;
		}
		break;
	}

	return 0;
}

int
sdhc_acpi_card_detect_nonremovable(struct sdhc_softc *ssc)
{
	return 1;
}

int
sdhc_acpi_card_detect_gpio(struct sdhc_softc *ssc)
{
	struct sdhc_acpi_softc *sc = (struct sdhc_acpi_softc *)ssc;
	struct acpi_gpio *gpio = sc->sc_gpio_io_node->gpio;
	uint16_t pin = sc->sc_gpio_io_pin;

	/* Card detect GPIO signal is active-low. */
	return !gpio->read_pin(gpio->cookie, pin);
}

int
sdhc_acpi_card_detect_intr(void *arg)
{
	struct sdhc_acpi_softc *sc = arg;

	sdhc_needs_discover(&sc->sc);

	return (1);
}

void
sdhc_acpi_power_on(struct sdhc_acpi_softc *sc, struct aml_node *node)
{
	node = aml_searchname(node, "_PS0");
	if (node && aml_evalnode(sc->sc_acpi, node, 0, NULL, NULL))
		printf("%s: _PS0 failed\n", sc->sc.sc_dev.dv_xname);
}

int
sdhc_acpi_do_explore(struct aml_node *node, void *arg)
{
	struct sdhc_acpi_softc *sc = arg;
	int64_t sta, rmv;

	/* We're only interested in our children. */
	if (node == sc->sc_node)
		return 0;

	/* Only consider devices that are actually present. */
	if (node->value == NULL ||
	    node->value->type != AML_OBJTYPE_DEVICE)
		return 1;
	if (aml_evalinteger(sc->sc_acpi, node, "_STA", 0, NULL, &sta))
		sta = STA_PRESENT | STA_ENABLED | STA_DEV_OK | 0x1000;
	if ((sta & STA_PRESENT) == 0)
		return 1;

	acpi_attach_deps(sc->sc_acpi, node);

	/* Override card detect if we have non-removable devices. */
	if (aml_evalinteger(sc->sc_acpi, node, "_RMV", 0, NULL, &rmv))
		rmv = 1;
	if (rmv == 0) {
		sc->sc.sc_flags |= SDHC_F_NONREMOVABLE;
		if (sc->sc.sc_card_detect == NULL) {
			sc->sc.sc_card_detect =
			    sdhc_acpi_card_detect_nonremovable;
		}
	}

	sdhc_acpi_power_on(sc, node);

	return 1;
}

void
sdhc_acpi_explore(struct sdhc_acpi_softc *sc)
{
	aml_walknodes(sc->sc_node, AML_WALK_PRE, sdhc_acpi_do_explore, sc);
}
