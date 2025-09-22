/* $OpenBSD: moxtet.c,v 1.2 2021/10/24 17:52:26 mpi Exp $ */
/*
 * Copyright (c) 2019 Patrick Wildt <patrick@blueri.se>
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <dev/spi/spivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>

#define MOX_NMODULE		10

#define MOX_CPU_EMMC		0x00
#define MOX_CPU_SD		0x10

#define MOX_MODULE_SFP		0x01
#define MOX_MODULE_PCI		0x02
#define MOX_MODULE_TOPAZ	0x03
#define MOX_MODULE_PERIDOT	0x04
#define MOX_MODULE_USB3		0x05
#define MOX_MODULE_PASSPCI	0x06

struct moxtet_softc {
	struct device		 sc_dev;
	int			 sc_node;

	spi_tag_t		 sc_spi_tag;
	struct spi_config	 sc_spi_conf;

	int			 sc_nmodule;
};

int	 moxtet_match(struct device *, void *, void *);
void	 moxtet_attach(struct device *, struct device *, void *);
int	 moxtet_detach(struct device *, int);

int	 moxtet_read(struct moxtet_softc *, char *, size_t);
int	 moxtet_write(struct moxtet_softc *, char *, size_t);

const struct cfattach moxtet_ca = {
	sizeof(struct moxtet_softc), moxtet_match, moxtet_attach, moxtet_detach
};

struct cfdriver moxtet_cd = {
	NULL, "moxtet", DV_DULL
};

int
moxtet_match(struct device *parent, void *match, void *aux)
{
	struct spi_attach_args *sa = aux;

	if (strcmp(sa->sa_name, "cznic,moxtet") == 0)
		return 1;

	return 0;
}

void
moxtet_attach(struct device *parent, struct device *self, void *aux)
{
	struct moxtet_softc *sc = (struct moxtet_softc *)self;
	struct spi_attach_args *sa = aux;
	uint8_t buf[MOX_NMODULE];
	int i;

	sc->sc_spi_tag = sa->sa_tag;
	sc->sc_node = *(int *)sa->sa_cookie;

	pinctrl_byname(sc->sc_node, "default");

	sc->sc_spi_conf.sc_bpw = 8;
	sc->sc_spi_conf.sc_freq = OF_getpropint(sc->sc_node,
	    "spi-max-frequency", 0);
	sc->sc_spi_conf.sc_cs = OF_getpropint(sc->sc_node, "reg", 0);
	if (OF_getproplen(sc->sc_node, "spi-cpol") == 0)
		sc->sc_spi_conf.sc_flags |= SPI_CONFIG_CPOL;
	if (OF_getproplen(sc->sc_node, "spi-cpha") == 0)
		sc->sc_spi_conf.sc_flags |= SPI_CONFIG_CPHA;
	if (OF_getproplen(sc->sc_node, "spi-cs-high") == 0)
		sc->sc_spi_conf.sc_flags |= SPI_CONFIG_CS_HIGH;

	if (moxtet_read(sc, buf, sizeof(buf))) {
		printf(": can't read moxtet\n");
		return;
	}

	if (buf[0] == MOX_CPU_EMMC)
		printf(": eMMC");
	else if (buf[0] == MOX_CPU_SD)
		printf(": SD");
	else {
		printf(": unknown\n");
		return;
	}

	for (i = 1; i < MOX_NMODULE; i++) {
		if (buf[i] == 0xff)
			break;
		sc->sc_nmodule++;
		switch (buf[i] & 0xf) {
		case MOX_MODULE_SFP:
			printf(", SFP");
			break;
		case MOX_MODULE_PCI:
			printf(", mPCIe");
			break;
		case MOX_MODULE_TOPAZ:
			printf(", 4x GbE");
			break;
		case MOX_MODULE_PERIDOT:
			printf(", 8x GbE");
			break;
		case MOX_MODULE_USB3:
			printf(", 4x USB 3.0");
			break;
		case MOX_MODULE_PASSPCI:
			printf(", mPCIe (passthrough)");
			break;
		default:
			printf(", unknown (0x%02x)", buf[i] & 0xf);
			break;
		}
	}

	printf("\n");
}

int
moxtet_detach(struct device *self, int flags)
{
	return 0;
}

int
moxtet_read(struct moxtet_softc *sc, char *buf, size_t len)
{
	int error;

	spi_acquire_bus(sc->sc_spi_tag, 0);
	spi_config(sc->sc_spi_tag, &sc->sc_spi_conf);
	error = spi_read(sc->sc_spi_tag, buf, len);
	spi_release_bus(sc->sc_spi_tag, 0);
	return error;
}

int
moxtet_write(struct moxtet_softc *sc, char *buf, size_t len)
{
	int error;

	spi_acquire_bus(sc->sc_spi_tag, 0);
	spi_config(sc->sc_spi_tag, &sc->sc_spi_conf);
	error = spi_write(sc->sc_spi_tag, buf, len);
	spi_release_bus(sc->sc_spi_tag, 0);
	return error;
}
