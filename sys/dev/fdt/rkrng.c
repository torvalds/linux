/*	$OpenBSD: rkrng.c,v 1.6 2024/02/17 13:29:25 kettenis Exp $	*/
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
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

/* Registers */

/* V1 */
#define RNG_CTRL			0x0008
#define  RNG_CTRL_START			(1 << 8)
#define RNG_TRNG_CTRL			0x0200
#define  RNG_TRNG_CTRL_OSC_ENABLE	(1 << 16)
#define  RNG_TRNG_CTRL_SAMPLE_PERIOD(x)	(x)
#define RNG_DATA0			0x0204

/* True Random Number Generator (TRNG) */
#define TRNG_RST_CTL			0x0004
#define  TRNG_RST_CTL_SW_RNG_RESET		(0x1U << 1)
#define TRNG_CTL			0x0400
#define  TRNG_CTL_RNG_START			(0x1U << 0)
#define  TRNG_CTL_RNG_ENABLE			(0x1U << 1)
#define  TRNG_CTL_RING_SEL_MASK			(0x3U << 2)
#define  TRNG_CTL_RING_SEL_SLOWEST		(0x0U << 2)
#define  TRNG_CTL_RING_SEL_SLOW			(0x1U << 2)
#define  TRNG_CTL_RING_SEL_FAST			(0x2U << 2)
#define  TRNG_CTL_RING_SEL_FASTEST		(0x3U << 2)
#define  TRNG_CTL_RNG_LEN_MASK			(0x3U << 4)
#define  TRNG_CTL_RNG_LEN_64BIT			(0x0U << 4)
#define  TRNG_CTL_RNG_LEN_128BIT		(0x1U << 4)
#define  TRNG_CTL_RNG_LEN_192BIT		(0x2U << 4)
#define  TRNG_CTL_RNG_LEN_256BIT		(0x3U << 4)
#define TRNG_SAMPLE_CNT			0x0404
#define TRNG_DOUT_BASE			0x0410

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct rkrng_v;

struct rkrng_softc {
	struct device		sc_dev;
	const struct rkrng_v	*sc_v;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct timeout		sc_to;
	int			sc_started;
};

struct rkrng_v {
	unsigned int		version;
	void (*start)(struct rkrng_softc *sc);
	int (*starting)(struct rkrng_softc *sc);
	void (*stop)(struct rkrng_softc *sc);
	bus_size_t		dout;
};

int	rkrng_match(struct device *, void *, void *);
void	rkrng_attach(struct device *, struct device *, void *);

const struct cfattach rkrng_ca = {
	sizeof (struct rkrng_softc), rkrng_match, rkrng_attach
};

struct cfdriver rkrng_cd = {
	NULL, "rkrng", DV_DULL
};

void	rkrng_rnd(void *);

void	rkrng_v1_start(struct rkrng_softc *);
int	rkrng_v1_starting(struct rkrng_softc *);
void	rkrng_v1_stop(struct rkrng_softc *);

static const struct rkrng_v rkrnv_v1 = {
	.version	= 1,
	.start		= rkrng_v1_start,
	.starting	= rkrng_v1_starting,
	.stop		= rkrng_v1_stop,
	.dout		= RNG_DATA0,
};

void	rkrng_v2_start(struct rkrng_softc *);
int	rkrng_v2_starting(struct rkrng_softc *);
void	rkrng_v2_stop(struct rkrng_softc *);

static const struct rkrng_v rkrnv_v2 = {
	.version	= 2,
	.start		= rkrng_v2_start,
	.starting	= rkrng_v2_starting,
	.stop		= rkrng_v2_stop,
	.dout		= TRNG_DOUT_BASE,
};

int
rkrng_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "rockchip,cryptov1-rng") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3288-crypto") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3328-crypto") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3399-crypto") ||
	    OF_is_compatible(faa->fa_node, "rockchip,cryptov2-rng");
}

void
rkrng_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkrng_softc *sc = (struct rkrng_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (OF_is_compatible(faa->fa_node, "rockchip,cryptov1-rng") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3288-crypto") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3328-crypto") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3399-crypto"))
		sc->sc_v = &rkrnv_v1;
	else if (OF_is_compatible(faa->fa_node, "rockchip,cryptov2-rng"))
		sc->sc_v = &rkrnv_v2;
	else {
		printf(": unhandled version\n");
		return;
	}

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

	printf(": ver %u\n", sc->sc_v->version);

	clock_set_assigned(faa->fa_node);
	clock_enable_all(faa->fa_node);

	timeout_set(&sc->sc_to, rkrng_rnd, sc);
	rkrng_rnd(sc);
}

void
rkrng_v1_start(struct rkrng_softc *sc)
{
	HWRITE4(sc, RNG_TRNG_CTRL, RNG_TRNG_CTRL_OSC_ENABLE |
	    RNG_TRNG_CTRL_SAMPLE_PERIOD(100));
	HWRITE4(sc, RNG_CTRL, (RNG_CTRL_START << 16) | RNG_CTRL_START);
}

int
rkrng_v1_starting(struct rkrng_softc *sc)
{
	return (HREAD4(sc, RNG_CTRL) & RNG_CTRL_START);
}

void
rkrng_v1_stop(struct rkrng_softc *sc)
{
	HWRITE4(sc, RNG_CTRL, (RNG_CTRL_START << 16) | 0);
}

void
rkrng_v2_start(struct rkrng_softc *sc)
{
	uint32_t ctl_m = TRNG_CTL_RNG_START | TRNG_CTL_RNG_ENABLE |
	    TRNG_CTL_RING_SEL_MASK | TRNG_CTL_RNG_LEN_MASK;
	uint32_t ctl_v = TRNG_CTL_RNG_START | TRNG_CTL_RNG_ENABLE |
	    TRNG_CTL_RING_SEL_SLOW | TRNG_CTL_RNG_LEN_256BIT;

	HWRITE4(sc, TRNG_SAMPLE_CNT, 100);
	HWRITE4(sc, TRNG_CTL, (ctl_m << 16) | ctl_v);
}

int
rkrng_v2_starting(struct rkrng_softc *sc)
{
	return (HREAD4(sc, TRNG_CTL) & TRNG_CTL_RNG_START);
}

void
rkrng_v2_stop(struct rkrng_softc *sc)
{
	uint32_t ctl_m = TRNG_CTL_RNG_START | TRNG_CTL_RNG_ENABLE;

	HWRITE4(sc, TRNG_CTL, (ctl_m << 16) | 0);
}

void
rkrng_rnd(void *arg)
{
	struct rkrng_softc *sc = arg;
	bus_size_t off;

	if (!sc->sc_started) {
		sc->sc_v->start(sc);
		sc->sc_started = 1;
		timeout_add_usec(&sc->sc_to, 100);
		return;
	}

	if (sc->sc_v->starting(sc)) {
		timeout_add_usec(&sc->sc_to, 100);
		return;
	}

	for (off = 0; off < 32; off += 4)
		enqueue_randomness(HREAD4(sc, sc->sc_v->dout + off));

	sc->sc_v->stop(sc);
	sc->sc_started = 0;

	timeout_add_sec(&sc->sc_to, 1);
}
