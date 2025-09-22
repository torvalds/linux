/*	$OpenBSD: omclock.c,v 1.2 2022/04/06 18:59:26 naddy Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

#define IDLEST_MASK		(0x3 << 16)
#define IDLEST_FUNC		(0x0 << 16)
#define IDLEST_TRANS		(0x1 << 16)
#define IDLEST_IDLE		(0x2 << 16)
#define IDLEST_DISABLED		(0x3 << 16)
#define MODULEMODE_MASK		(0x3 << 0)
#define MODULEMODE_DISABLED	(0x0 << 0)
#define MODULEMODE_ENABLED	(0x2 << 0)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct omclock_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_node;

	struct clock_device	sc_cd;
};

int	omclock_match(struct device *, void *, void *);
void	omclock_attach(struct device *, struct device *, void *);

const struct cfattach omclock_ca = {
	sizeof (struct omclock_softc), omclock_match, omclock_attach
};

struct cfdriver omclock_cd = {
	NULL, "omclock", DV_DULL
};

uint32_t omclock_get_frequency(void *, uint32_t *);
int	omclock_set_frequency(void *, uint32_t *, uint32_t);
void	omclock_enable(void *, uint32_t *, int);

int
omclock_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "ti,clkctrl");
}

void
omclock_attach(struct device *parent, struct device *self, void *aux)
{
	struct omclock_softc *sc = (struct omclock_softc *)self;
	struct fdt_attach_args *faa = aux;
	char name[32];

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	if (OF_getprop(sc->sc_node, "name", name, sizeof(name)) > 0) {
		name[sizeof(name) - 1] = 0;
		printf(": \"%s\"", name);
	}

	printf("\n");

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_get_frequency = omclock_get_frequency;
	sc->sc_cd.cd_set_frequency = omclock_set_frequency;
	sc->sc_cd.cd_enable = omclock_enable;
	clock_register(&sc->sc_cd);
}

uint32_t
omclock_get_frequency(void *cookie, uint32_t *cells)
{
	printf("%s: 0x%08x 0x%08x\n", __func__, cells[0], cells[1]);
	return 0;
}

int
omclock_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	printf("%s: 0x%08x 0x%08x\n", __func__, cells[0], cells[1]);
	return -1;
}

void
omclock_enable(void *cookie, uint32_t *cells, int on)
{
	struct omclock_softc *sc = cookie;
	uint32_t base = cells[0];
	uint32_t idx = cells[1];
	uint32_t reg;
	int retry;

	reg = HREAD4(sc, base);
	if (idx == 0) {
		reg &= ~MODULEMODE_MASK;
		if (on)
			reg |= MODULEMODE_ENABLED;
		else
			reg |= MODULEMODE_DISABLED;
	} else {
		if (on)
			reg |= (1U << idx);
		else
			reg &= ~(1U << idx);
	}
	HWRITE4(sc, base, reg);

	if (idx == 0) {
		retry = 100;
		while (--retry > 0) {
			if ((HREAD4(sc, base) & IDLEST_MASK) == IDLEST_FUNC)
				break;
			delay(10);
		}
		/* XXX Hope for the best if this loop times out. */
	}
}
