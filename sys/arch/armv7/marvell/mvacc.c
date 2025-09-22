/* $OpenBSD: mvacc.c,v 1.6 2024/02/13 02:14:25 jsg Exp $ */
/*
 * Copyright (c) 2016 Patrick Wildt <patrick@blueri.se>
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

#define SAR				0x00
#define  SAR_CPU_DDR_FREQ_OPT		10
#define  SAR_CPU_DDR_FREQ_OPT_MASK	0x1f
#define  SAR_TCLK_FREQ_OPT		15
#define  SAR_TCLK_FREQ_OPT_MASK		0x1

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

static const uint32_t mvacc_cpu_freqs[] = {
	0, 0, 0, 0,
	1066000000, 0, 0, 0,
	1322000000, 0, 0, 0,
	1600000000,
};

static const int mvacc_l2clk_ratios[32][2] = {
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
};

static const int mvacc_ddrclk_ratios[32][2] = {
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
};

struct mvacc_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	int			 sc_node;
	struct clock_device	 sc_cd;
};

int	 mvacc_match(struct device *, void *, void *);
void	 mvacc_attach(struct device *, struct device *, void *);

uint32_t mvacc_get_frequency(void *, uint32_t *);

const struct cfattach	mvacc_ca = {
	sizeof (struct mvacc_softc), mvacc_match, mvacc_attach
};

struct cfdriver mvacc_cd = {
	NULL, "mvacc", DV_DULL
};

int
mvacc_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-380-core-clock");
}

void amptimer_set_clockrate(int32_t new_frequency); /* XXX */

void
mvacc_attach(struct device *parent, struct device *self, void *args)
{
	struct mvacc_softc *sc = (struct mvacc_softc *)self;
	struct fdt_attach_args *faa = args;
	int idx = 2;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	printf("\n");

	amptimer_set_clockrate(mvacc_get_frequency(sc, &idx));

	sc->sc_cd.cd_node = sc->sc_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_get_frequency = mvacc_get_frequency;
	clock_register(&sc->sc_cd);
}

uint32_t
mvacc_get_frequency(void *cookie, uint32_t *cells)
{
	struct mvacc_softc *sc = cookie;
	uint32_t sar, cpu, tclk;

	sar = HREAD4(sc, SAR);
	cpu = (sar >> SAR_CPU_DDR_FREQ_OPT) & SAR_CPU_DDR_FREQ_OPT_MASK;
	tclk = (sar >> SAR_TCLK_FREQ_OPT) & SAR_TCLK_FREQ_OPT_MASK;

	if (cpu >= nitems(mvacc_cpu_freqs)) {
		printf("%s: invalid cpu frequency", sc->sc_dev.dv_xname);
		return 0;
	}

	switch (cells[0])
	{
	case 0: /* TCLK */
		return tclk ? 200000000 : 250000000;
	case 1: /* CPUCLK */
		return mvacc_cpu_freqs[cpu];
	case 2: /* L2CLK */
		return (mvacc_cpu_freqs[cpu] * mvacc_l2clk_ratios[cpu][0])
		    / mvacc_l2clk_ratios[cpu][1];
	case 3: /* DDRCLK */
		return (mvacc_cpu_freqs[cpu] * mvacc_ddrclk_ratios[cpu][0])
		    / mvacc_ddrclk_ratios[cpu][1];
	default:
		return 0;
	}
}
