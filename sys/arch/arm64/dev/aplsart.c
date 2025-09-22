/*	$OpenBSD: aplsart.c,v 1.4 2022/11/11 11:45:10 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/extent.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>
#include <dev/ofw/ofw_power.h>

#define SART2_CONFIG(idx)	(0x0000 + 4 * (idx))
#define  SART2_CONFIG_FLAGS_MASK	0xff000000
#define  SART2_CONFIG_FLAGS_ALLOW	0xff000000
#define SART2_ADDR(idx)		(0x0040 + 4 * (idx))

#define SART3_CONFIG(idx)	(0x0000 + 4 * (idx))
#define  SART3_CONFIG_FLAGS_MASK	0x000000ff
#define  SART3_CONFIG_FLAGS_ALLOW	0x000000ff
#define SART3_ADDR(idx)		(0x0040 + 4 * (idx))
#define SART3_SIZE(idx)		(0x0080 + 4 * (idx))

#define SART_NUM_ENTRIES	16
#define SART_ADDR_SHIFT		12
#define SART_SIZE_SHIFT		12

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct aplsart_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_node;
	uint32_t		sc_phandle;
	int			sc_version;
};

int	aplsart_match(struct device *, void *, void *);
void	aplsart_attach(struct device *, struct device *, void *);
int	aplsart_activate(struct device *, int);

const struct cfattach aplsart_ca = {
	sizeof (struct aplsart_softc), aplsart_match, aplsart_attach, NULL,
	aplsart_activate
};

struct cfdriver aplsart_cd = {
	NULL, "aplsart", DV_DULL
};

int
aplsart_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,t6000-sart") ||
	    OF_is_compatible(faa->fa_node, "apple,t8103-sart");
}

void
aplsart_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplsart_softc *sc = (struct aplsart_softc *)self;
	struct fdt_attach_args *faa = aux;

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
	sc->sc_phandle = OF_getpropint(faa->fa_node, "phandle", 0);

	if (OF_is_compatible(sc->sc_node, "apple,t8103-sart"))
		sc->sc_version = 2;
	if (OF_is_compatible(sc->sc_node, "apple,t6000-sart"))
		sc->sc_version = 3;

	power_domain_enable_all(sc->sc_node);

	printf("\n");
}

int
aplsart_activate(struct device *self, int act)
{
	struct aplsart_softc *sc = (struct aplsart_softc *)self;

	switch (act) {
	case DVACT_POWERDOWN:
		power_domain_disable_all(sc->sc_node);
		break;
	case DVACT_RESUME:
		power_domain_enable_all(sc->sc_node);
		break;
	}

	return 0;
}

int
aplsart2_map(struct aplsart_softc *sc, bus_addr_t addr, bus_size_t size)
{
	uint32_t conf;
	int i;

	for (i = 0; i < SART_NUM_ENTRIES; i++) {
		conf = HREAD4(sc, SART2_CONFIG(i));
		if (conf & SART2_CONFIG_FLAGS_MASK)
			continue;

		HWRITE4(sc, SART2_ADDR(i), addr >> SART_ADDR_SHIFT);
		HWRITE4(sc, SART2_CONFIG(i),
		    size >> SART_SIZE_SHIFT | SART2_CONFIG_FLAGS_ALLOW);
		return 0;
	}

	return ENOENT;
}

int
aplsart3_map(struct aplsart_softc *sc, bus_addr_t addr, bus_size_t size)
{
	uint32_t conf;
	int i;

	for (i = 0; i < SART_NUM_ENTRIES; i++) {
		conf = HREAD4(sc, SART3_CONFIG(i));
		if (conf & SART3_CONFIG_FLAGS_MASK)
			continue;

		HWRITE4(sc, SART3_ADDR(i), addr >> SART_ADDR_SHIFT);
		HWRITE4(sc, SART3_SIZE(i), size >> SART_SIZE_SHIFT);
		HWRITE4(sc, SART3_CONFIG(i), SART3_CONFIG_FLAGS_ALLOW);
		return 0;
	}

	return ENOENT;
}

int
aplsart_map(uint32_t phandle, bus_addr_t addr, bus_size_t size)
{
	struct aplsart_softc *sc;
	int i;

	for (i = 0; i < aplsart_cd.cd_ndevs; i++) {
		sc = (struct aplsart_softc *)aplsart_cd.cd_devs[i];

		if (sc->sc_phandle == phandle) {
			if (sc->sc_version == 2)
				return aplsart2_map(sc, addr, size);
			else
				return aplsart3_map(sc, addr, size);
		}
	}

	return ENXIO;
}

int
aplsart2_unmap(struct aplsart_softc *sc, bus_addr_t addr, bus_size_t size)
{
	int i;

	for (i = 0; i < SART_NUM_ENTRIES; i++) {
		if (HREAD4(sc, SART2_ADDR(i)) != (addr >> SART_ADDR_SHIFT))
			continue;

		HWRITE4(sc, SART2_ADDR(i), 0);
		HWRITE4(sc, SART2_CONFIG(i), 0);
		return 0;
	}

	return ENOENT;
}

int
aplsart3_unmap(struct aplsart_softc *sc, bus_addr_t addr, bus_size_t size)
{
	int i;

	for (i = 0; i < SART_NUM_ENTRIES; i++) {
		if (HREAD4(sc, SART3_ADDR(i)) != (addr >> SART_ADDR_SHIFT))
			continue;

		HWRITE4(sc, SART3_ADDR(i), 0);
		HWRITE4(sc, SART3_SIZE(i), 0);
		HWRITE4(sc, SART3_CONFIG(i), 0);
		return 0;
	}

	return ENOENT;
}

int
aplsart_unmap(uint32_t phandle, bus_addr_t addr, bus_size_t size)
{
	struct aplsart_softc *sc;
	int i;

	for (i = 0; i < aplsart_cd.cd_ndevs; i++) {
		sc = (struct aplsart_softc *)aplsart_cd.cd_devs[i];

		if (sc->sc_phandle == phandle) {
			if (sc->sc_version == 2)
				return aplsart2_unmap(sc, addr, size);
			else
				return aplsart3_unmap(sc, addr, size);
		}
	}

	return ENXIO;
}
