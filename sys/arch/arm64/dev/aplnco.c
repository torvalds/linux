/*	$OpenBSD: aplnco.c,v 1.2 2022/05/29 16:19:08 kettenis Exp $	*/
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
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

#define NCO_LFSR_POLY		0xa01
#define NCO_LFSR_MASK		0x7ff

#define NCO_STRIDE		0x4000

#define NCO_CTRL(idx)		((idx) * NCO_STRIDE + 0x0000)
#define  NCO_CTRL_ENABLE	(1U << 31)
#define NCO_DIV(idx)		((idx) * NCO_STRIDE + 0x0004)
#define  NCO_DIV_COARSE(div)	((div >> 2) & NCO_LFSR_MASK)
#define  NCO_DIV_FINE(div)	(div & 0x3)
#define NCO_INC1(idx)		((idx) * NCO_STRIDE + 0x0008)
#define NCO_INC2(idx)		((idx) * NCO_STRIDE + 0x000c)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct aplnco_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int			sc_node;
	unsigned int		sc_nclocks;
	struct clock_device	sc_cd;
};

int	aplnco_match(struct device *, void *, void *);
void	aplnco_attach(struct device *, struct device *, void *);

const struct cfattach aplnco_ca = {
	sizeof (struct aplnco_softc), aplnco_match, aplnco_attach
};

struct cfdriver aplnco_cd = {
	NULL, "aplnco", DV_DULL
};

void	aplnco_enable(void *, uint32_t *, int);
uint32_t aplnco_get_frequency(void *, uint32_t *);
int	aplnco_set_frequency(void *, uint32_t *, uint32_t);

int
aplnco_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,nco");
}

void
aplnco_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplnco_softc *sc = (struct aplnco_softc *)self;
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
	sc->sc_nclocks = faa->fa_reg[0].size / NCO_STRIDE;

	printf("\n");

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_enable = aplnco_enable;
	sc->sc_cd.cd_get_frequency = aplnco_get_frequency;
	sc->sc_cd.cd_set_frequency = aplnco_set_frequency;
	clock_register(&sc->sc_cd);
}

uint16_t
aplnco_lfsr(uint16_t fwd)
{
	uint16_t lfsr = NCO_LFSR_MASK;
	uint16_t i;

	for (i = NCO_LFSR_MASK; i > 0; i--) {
		if (lfsr & 1)
			lfsr = (lfsr >> 1) ^ (NCO_LFSR_POLY >> 1);
		else
			lfsr = (lfsr >> 1);
		if (i == fwd)
			return lfsr;
	}

	return 0;
}

uint16_t
aplnco_lfsr_inv(uint16_t inv)
{
	uint16_t lfsr = NCO_LFSR_MASK;
	uint16_t i;

	for (i = NCO_LFSR_MASK; i > 0; i--) {
		if (lfsr & 1)
			lfsr = (lfsr >> 1) ^ (NCO_LFSR_POLY >> 1);
		else
			lfsr = (lfsr >> 1);
		if (lfsr == inv)
			return i;
	}

	return 0;
}

void
aplnco_enable(void *cookie, uint32_t *cells, int on)
{
	struct aplnco_softc *sc = cookie;
	uint32_t idx = cells[0];

	if (idx >= sc->sc_nclocks)
		return;

	if (on)
		HSET4(sc, NCO_CTRL(idx), NCO_CTRL_ENABLE);
	else
		HCLR4(sc, NCO_CTRL(idx), NCO_CTRL_ENABLE);
}

uint32_t
aplnco_get_frequency(void *cookie, uint32_t *cells)
{
	struct aplnco_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t div, parent_freq;
	uint16_t coarse, fine;
	int32_t inc1, inc2;
	int64_t div64;

	if (idx >= sc->sc_nclocks)
		return 0;

	parent_freq = clock_get_frequency(sc->sc_node, NULL);
	div = HREAD4(sc, NCO_DIV(idx));
	coarse = NCO_DIV_COARSE(div);
	fine = NCO_DIV_FINE(div);

	coarse = aplnco_lfsr_inv(coarse) + 2;
	div = (coarse << 2) + fine;

	inc1 = HREAD4(sc, NCO_INC1(idx));
	inc2 = HREAD4(sc, NCO_INC2(idx));
	if (inc1 < 0 || inc2 > 0 || (inc1 == 0 && inc2 == 0))
		return 0;

	div64 = (int64_t)div * (inc1 - inc2) + inc1;
	if (div64 == 0)
		return 0;

	return ((int64_t)parent_freq * 2 * (inc1 - inc2)) / div64;
}

int
aplnco_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct aplnco_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t div, parent_freq;
	uint16_t coarse;
	int32_t inc1, inc2;
	uint32_t ctrl;

	if (idx >= sc->sc_nclocks)
		return ENXIO;

	if (freq == 0)
		return EINVAL;

	parent_freq = clock_get_frequency(sc->sc_node, NULL);
	div = (parent_freq * 2) / freq;
	coarse = (div >> 2) - 2;
	if (coarse > NCO_LFSR_MASK)
		return EINVAL;

	inc1 = 2 * parent_freq - div * freq;
	inc2 = -(freq - inc1);

	coarse = aplnco_lfsr(coarse);
	div = (coarse << 2) + (div & 3);

	ctrl = HREAD4(sc, NCO_CTRL(idx));
	HWRITE4(sc, NCO_CTRL(idx), ctrl & ~NCO_CTRL_ENABLE);
	HWRITE4(sc, NCO_DIV(idx), div);
	HWRITE4(sc, NCO_INC1(idx), inc1);
	HWRITE4(sc, NCO_INC2(idx), inc2);
	HWRITE4(sc, NCO_CTRL(idx), ctrl);

	return 0;
}
