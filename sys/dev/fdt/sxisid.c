/*	$OpenBSD: sxisid.c,v 1.4 2021/10/24 17:52:27 mpi Exp $	*/
/*
 * Copyright (c) 2019 Krystian Lewandowski
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <machine/fdt.h>
#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

/* Registers */
#define SID_PRCTL		0x40
#define  SID_PRCTL_OFFSET(n)	(((n) & 0x1ff) << 16)
#define  SID_PRCTL_OP_LOCK	(0xac << 8)
#define  SID_PRCTL_READ		(1 << 1)
#define SID_RDKEY		0x60

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct sxisid_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t 	sc_ioh;

	bus_size_t		sc_size;
	struct nvmem_device	sc_nd;
};

int sxisid_match(struct device *, void *, void *);
void sxisid_attach(struct device *, struct device *, void *);

const struct cfattach sxisid_ca = {
	sizeof(struct sxisid_softc), sxisid_match, sxisid_attach
};

struct cfdriver sxisid_cd = {
	NULL, "sxisid", DV_DULL
};

int	sxisid_read(void *, bus_addr_t, void *, bus_size_t);

int
sxisid_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "allwinner,sun4i-a10-sid") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun7i-a20-sid") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun8i-a83t-sid") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun8i-h3-sid") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-a64-sid") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-h5-sid") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-h6-sid"));
}

void
sxisid_attach(struct device *parent, struct device *self, void *aux)
{
	struct sxisid_softc *sc = (struct sxisid_softc *) self;
	struct fdt_attach_args *faa = aux;
	uint32_t sid[4];
	int i;

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

	printf("\n");

	if (OF_is_compatible(faa->fa_node, "allwinner,sun4i-a10-sid"))
		sc->sc_size = 0x10;
	else if (OF_is_compatible(faa->fa_node, "allwinner,sun8i-a83t-sid") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun8i-h3-sid") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-a64-sid") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-h5-sid"))
		sc->sc_size = 0x100;
	else
		sc->sc_size = 0x200;

	if (sxisid_read(sc, 0, &sid, sizeof(sid)))
		return;

	for (i = 0; i < nitems(sid); i++)
		enqueue_randomness(sid[i]);

	sc->sc_nd.nd_node = faa->fa_node;
	sc->sc_nd.nd_cookie = sc;
	sc->sc_nd.nd_read = sxisid_read;
	nvmem_register(&sc->sc_nd);
}

int
sxisid_read(void *cookie, bus_addr_t addr, void *data, bus_size_t size)
{
	struct sxisid_softc *sc = cookie;
	uint8_t *buf = data;
	uint32_t val;
	int pos, timo, i;

	if (addr >= sc->sc_size || addr + size > sc->sc_size)
		return EINVAL;

	pos = 0;
	while (pos < size) {
		HWRITE4(sc, SID_PRCTL, SID_PRCTL_OFFSET(addr) |
		    SID_PRCTL_OP_LOCK | SID_PRCTL_READ);

		for (timo = 2500; timo > 0; timo--) {
			if ((HREAD4(sc, SID_PRCTL) & SID_PRCTL_READ) == 0)
				break;
			delay(100);
		}
		if (timo == 0)
			return EIO;

		val = HREAD4(sc, SID_RDKEY);
		for (i = 0; i < 4 && pos < size; i++) {
			buf[pos++] = val & 0xff;
			val >>= 8;
			addr++;
		}
	}

	return 0;
}
