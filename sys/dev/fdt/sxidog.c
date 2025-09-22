/* $OpenBSD: sxidog.c,v 1.4 2024/01/26 17:03:45 kettenis Exp $ */
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
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
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/fdt/sunxireg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

extern void (*cpuresetfn)(void);

/* Allwinner A10 registers */
#define WDOG_CTRL_REG		0x00
#define  WDOG_KEY		(0x0a57 << 1)
#define  WDOG_RSTART		0x01
#define WDOG_MODE_REG		0x04
#define  WDOG_EN		(1 << 0)
#define  WDOG_RST_EN		(1 << 1) /* system reset */
#define  WDOG_INTV_VALUE(x)	((x) << 3)

/* Allwinner A31 registers */
#define WDOG0_CTRL_REG		0x10
#define  WDOG0_KEY		(0x0a57 << 1)
#define  WDOG0_RSTART		(1 << 0)
#define WDOG0_CFG_REG		0x14
#define  WDOG0_RST_EN		(1 << 0)
#define WDOG0_MODE_REG		0x18
#define  WDOG0_EN		(1 << 0)
#define  WDOG0_INTV_VALUE(x)	((x) << 4)

struct sxidog_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_type;
#define SXIDOG_A10		0
#define SXIDOG_A31		1
	uint32_t		sc_key;
};

struct sxidog_softc *sxidog_sc = NULL;	/* for sxidog_reset() */

int sxidog_match(struct device *, void *, void *);
void sxidog_attach(struct device *, struct device *, void *);
int sxidog_activate(struct device *, int);
int sxidog_callback(void *, int);
void sxidog_reset(void);

const struct cfattach	sxidog_ca = {
	sizeof (struct sxidog_softc), sxidog_match, sxidog_attach,
	NULL, sxidog_activate
};

struct cfdriver sxidog_cd = {
	NULL, "sxidog", DV_DULL
};

int
sxidog_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "allwinner,sun4i-a10-wdt") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun6i-a31-wdt") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun20i-d1-wdt"));
}

void
sxidog_attach(struct device *parent, struct device *self, void *aux)
{
	struct sxidog_softc *sc = (struct sxidog_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("sxidog_attach: bus_space_map failed!");

	if (OF_is_compatible(faa->fa_node, "allwinner,sun20i-d1-wdt"))
		sc->sc_key = 0x16aa0000;

	if (OF_is_compatible(faa->fa_node, "allwinner,sun6i-a31-wdt") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun20i-d1-wdt")) {
		SXIWRITE4(sc, WDOG0_MODE_REG, sc->sc_key);
		SXIWRITE4(sc, WDOG0_CFG_REG, WDOG0_RST_EN | sc->sc_key);
		sc->sc_type = SXIDOG_A31;
	} else {
		SXIWRITE4(sc, WDOG_MODE_REG, 0);
		sc->sc_type = SXIDOG_A10;
	}

	sxidog_sc = sc;
	cpuresetfn = sxidog_reset;

#ifndef SMALL_KERNEL
	wdog_register(sxidog_callback, sc);
#endif

	printf("\n");
}

int
sxidog_activate(struct device *self, int act)
{
	switch (act) {
	case DVACT_POWERDOWN:
#ifndef SMALL_KERNEL
		wdog_shutdown(self);
#endif
		break;
	}

	return (0);
}

int
sxidog_callback(void *arg, int period)
{
	struct sxidog_softc *sc = (struct sxidog_softc *)arg;
	int enable;

	if (period > 16)
		period = 16;
	else if (period < 0)
		period = 0;

	/* Convert to register encoding. */
	if (period > 6)
		period = 6 + (period - 5) / 2;

	switch (sc->sc_type) {
	case SXIDOG_A10:
		enable = (period > 0) ? WDOG_RST_EN : 0;
		SXIWRITE4(sc, WDOG_MODE_REG,
		    enable | WDOG_EN | WDOG_INTV_VALUE(period));
		SXIWRITE4(sc, WDOG_CTRL_REG, WDOG_KEY | WDOG_RSTART);
		break;
	case SXIDOG_A31:
		enable = (period > 0) ? WDOG0_EN : 0;
		SXIWRITE4(sc, WDOG0_MODE_REG,
		    enable | WDOG0_INTV_VALUE(period) | sc->sc_key);
		SXIWRITE4(sc, WDOG0_CTRL_REG, WDOG0_KEY | WDOG0_RSTART);
		break;
	}

	/* Convert back to seconds. */
	if (period > 6)
		period = 6 + (period - 6) * 2;

	return period;
}

void
sxidog_reset(void)
{
	if (sxidog_sc == NULL)
		return;

	sxidog_callback(sxidog_sc, 1);
	delay(1500000);
}
