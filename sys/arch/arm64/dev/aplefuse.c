/*	$OpenBSD: aplefuse.c,v 1.2 2024/05/13 01:15:50 jsg Exp $	*/
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

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

struct aplefuse_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_ios;

	struct nvmem_device	sc_nd;
};

int	aplefuse_match(struct device *, void *, void *);
void	aplefuse_attach(struct device *, struct device *, void *);

const struct cfattach aplefuse_ca = {
	sizeof (struct aplefuse_softc), aplefuse_match, aplefuse_attach
};

struct cfdriver aplefuse_cd = {
	NULL, "aplefuse", DV_DULL
};

int	aplefuse_nvmem_read(void *, bus_addr_t, void *, bus_size_t);

int
aplefuse_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,efuses");
}

void
aplefuse_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplefuse_softc *sc = (struct aplefuse_softc *)self;
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
	sc->sc_ios = faa->fa_reg[0].size;

	printf("\n");

	sc->sc_nd.nd_node = faa->fa_node;
	sc->sc_nd.nd_cookie = sc;
	sc->sc_nd.nd_read = aplefuse_nvmem_read;
	nvmem_register(&sc->sc_nd);
}

int
aplefuse_nvmem_read(void *cookie, bus_addr_t addr, void *data, bus_size_t size)
{
	struct aplefuse_softc *sc = cookie;
	uint8_t *p = data;
	uint32_t value;
	uint8_t buf[4];
	int offset;

	if (addr > sc->sc_ios || size > sc->sc_ios - addr)
		return EINVAL;

	offset = addr & 0x3;
	addr &= ~0x3;
	while (size > 0) {
		value = bus_space_read_4(sc->sc_iot, sc->sc_ioh, addr);
		htolem32(buf, value);
		memcpy(p, &buf[offset], MIN(size, 4 - offset));
		size -= MIN(size, 4 - offset);
		p += MIN(size, 4 - offset);
		addr += 4;
		offset = 0;
	}

	return 0;
}
