/* $OpenBSD: rpipwm.c,v 1.1 2025/09/17 09:32:55 kettenis Exp $ */

/*
 * Copyright (c) 2025 Marcus Glocker <mglocker@openbsd.org>
 * Copyright (c) 2025 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.  *
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
#include <sys/malloc.h>

#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

/* Bus helper macros. */
#define HREAD4(sc, reg)							\
	    (bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	    bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	    HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	    HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

/* Registers. */
#define GLOBAL_CTRL			0x00
#define  GLOBAL_CTRL_SET_UPDATE		(1U << 31)
#define  GLOBAL_CTRL_CHAN_EN(_chan)	(1U << (_chan))
#define CHAN_CTRL(_chan)		(0x14 + ((_chan) * 16))
#define  CHAN_CTRL_FIFO_POP_MASK	(1U << 8)
#define  CHAN_CTRL_INVERT		(1U << 3)
#define  CHAN_CTRL_MODE_TRAILING_EDGE	(0x1 << 0)
#define CHAN_RANGE(_chan)		(0x18 + ((_chan) * 16))
#define CHAN_DUTY(_chan)		(0x20 + ((_chan) * 16))

struct rpipwm_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint32_t		sc_clkin;
	uint32_t		sc_initialized;
	struct pwm_device	sc_pd;
};

int	rpipwm_match(struct device *, void *, void *);
void	rpipwm_attach(struct device *, struct device *, void *);

const struct cfattach rpipwm_ca = {
	sizeof(struct rpipwm_softc), rpipwm_match, rpipwm_attach
};

struct cfdriver rpipwm_cd = {
	NULL, "rpipwm", DV_DULL
};

int	rpipwm_get_state(void *, uint32_t *, struct pwm_state *);
int	rpipwm_set_state(void *, uint32_t *, struct pwm_state *);

int
rpipwm_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "raspberrypi,rp1-pwm");
}

void
rpipwm_attach(struct device *parent, struct device *self, void *aux)
{
	struct rpipwm_softc *sc = (struct rpipwm_softc *)self;
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

	printf("\n");

	pinctrl_byname(faa->fa_node, "default");
	clock_set_assigned(faa->fa_node);
	clock_enable_all(faa->fa_node);

	sc->sc_clkin = clock_get_frequency(faa->fa_node, NULL);

	sc->sc_pd.pd_node = faa->fa_node;
	sc->sc_pd.pd_cookie = sc;
	sc->sc_pd.pd_get_state = rpipwm_get_state;
	sc->sc_pd.pd_set_state = rpipwm_set_state;
	pwm_register(&sc->sc_pd);
}

static inline uint32_t
cycles_to_ns(uint64_t clk_freq, uint32_t cycles)
{
	return cycles * 1000000000ULL / clk_freq;
}

static inline uint32_t
ns_to_cycles(uint64_t clk_freq, uint32_t ns)
{
	return ns * clk_freq / 1000000000ULL;
}

int
rpipwm_get_state(void *cookie, uint32_t *cells, struct pwm_state *ps)
{
	struct rpipwm_softc *sc = cookie;
	uint32_t chan = cells[0];
	uint32_t ctrl, range, duty;

	if (chan >= 4 || sc->sc_clkin == 0)
		return EINVAL;

	/* Initialize a channel to a know state when we first touch it. */
	if ((sc->sc_initialized & GLOBAL_CTRL_CHAN_EN(chan)) == 0) {
		HWRITE4(sc, CHAN_CTRL(chan),
		    CHAN_CTRL_FIFO_POP_MASK | CHAN_CTRL_MODE_TRAILING_EDGE);
		sc->sc_initialized |= GLOBAL_CTRL_CHAN_EN(chan);
	}

	ctrl = HREAD4(sc, GLOBAL_CTRL);
	ps->ps_enabled = !!(ctrl & GLOBAL_CTRL_CHAN_EN(chan));

	range = HREAD4(sc, CHAN_RANGE(chan));
	duty = HREAD4(sc, CHAN_DUTY(chan));
	ps->ps_period = cycles_to_ns(sc->sc_clkin, range);
	ps->ps_pulse_width = cycles_to_ns(sc->sc_clkin, duty);

	return 0;
}

int
rpipwm_set_state(void *cookie, uint32_t *cells, struct pwm_state *ps)
{
	struct rpipwm_softc *sc = cookie;
	uint32_t chan = cells[0];
	uint32_t range, duty;

	if (chan >= 4 || sc->sc_clkin == 0)
		return EINVAL;

	range = ns_to_cycles(sc->sc_clkin, ps->ps_period);
	HWRITE4(sc, CHAN_RANGE(chan), range);

	duty = ns_to_cycles(sc->sc_clkin, ps->ps_pulse_width);
	HWRITE4(sc, CHAN_DUTY(chan), duty);

	if (ps->ps_flags & PWM_POLARITY_INVERTED)
		HSET4(sc, CHAN_CTRL(chan), CHAN_CTRL_INVERT);
	else
		HCLR4(sc, CHAN_CTRL(chan), CHAN_CTRL_INVERT);

	if (ps->ps_enabled)
		HSET4(sc, GLOBAL_CTRL, GLOBAL_CTRL_CHAN_EN(chan));
	else
		HCLR4(sc, GLOBAL_CTRL, GLOBAL_CTRL_CHAN_EN(chan));

	/* activate configuration */
	HSET4(sc, GLOBAL_CTRL, GLOBAL_CTRL_SET_UPDATE);

	return 0;
}
