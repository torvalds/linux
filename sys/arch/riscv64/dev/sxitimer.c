/*	$OpenBSD: sxitimer.c,v 1.2 2024/10/17 01:57:18 jsg Exp $	*/
/*
 * Copyright (c) 2024 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/clockintr.h>
#include <sys/device.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

/* Registers */
#define TMR_IRQ_EN		0x0000
#define  TMR1_IRQ_EN		(1 << 1)
#define  TMR0_IRQ_EN		(1 << 0)
#define TMR_IRQ_STA		0x0004
#define  TMR1_IRQ_PEND		(1 << 1)
#define  TMR0_IRQ_PEND		(1 << 0)
#define TMR0_CTRL		0x0010
#define  TMR0_MODE_SINGLE	(1 << 7)
#define  TMR0_CLK_PRES_1	(0 << 4)
#define  TMR0_CLK_SRC_OSC24M	(1 << 2)
#define  TMR0_RELOAD		(1 << 1)
#define  TMR0_EN		(1 << 0)
#define TMR0_INTV_VALUE		0x0014

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct sxitimer_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint32_t		sc_ticks_per_second;
	uint64_t		sc_nsec_cycle_ratio;
	uint64_t		sc_nsec_max;
	void			*sc_ih;
};

int	sxitimer_match(struct device *, void *, void *);
void	sxitimer_attach(struct device *, struct device *, void *);

const struct cfattach sxitimer_ca = {
	sizeof (struct sxitimer_softc), sxitimer_match, sxitimer_attach
};

struct cfdriver sxitimer_cd = {
	NULL, "sxitimer", DV_DULL
};

void	sxitimer_startclock(void);
int	sxitimer_intr(void *);
void	sxitimer_rearm(void *, uint64_t);
void	sxitimer_trigger(void *);

struct intrclock sxitimer_intrclock = {
	.ic_rearm = sxitimer_rearm,
	.ic_trigger = sxitimer_trigger
};

int
sxitimer_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "allwinner,sun20i-d1-timer");
}

void
sxitimer_attach(struct device *parent, struct device *self, void *aux)
{
	struct sxitimer_softc *sc = (struct sxitimer_softc *)self;
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

	HWRITE4(sc, TMR_IRQ_EN, 0);

	sc->sc_ticks_per_second = clock_get_frequency(faa->fa_node, NULL);
	sc->sc_nsec_cycle_ratio =
	    sc->sc_ticks_per_second * (1ULL << 32) / 1000000000;
	sc->sc_nsec_max = UINT64_MAX / sc->sc_nsec_cycle_ratio;

	sxitimer_intrclock.ic_cookie = sc;
	cpu_startclock_fcn = sxitimer_startclock;

	sc->sc_ih = fdt_intr_establish_idx(faa->fa_node, 0, IPL_CLOCK,
	    sxitimer_intr, NULL, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf("can't establish interrupt\n");
		return;
	}

	HWRITE4(sc, TMR0_INTV_VALUE, 0);
	HWRITE4(sc, TMR0_CTRL, TMR0_MODE_SINGLE | TMR0_CLK_PRES_1 |
	    TMR0_CLK_SRC_OSC24M | TMR0_RELOAD | TMR0_EN);
	HWRITE4(sc, TMR_IRQ_STA, TMR0_IRQ_PEND);
	HWRITE4(sc, TMR_IRQ_EN, TMR0_IRQ_EN);

	printf(": %u kHz\n", sc->sc_ticks_per_second / 1000);
}

void
sxitimer_startclock(void)
{
	clockintr_cpu_init(&sxitimer_intrclock);
	clockintr_trigger();
}

int
sxitimer_intr(void *frame)
{
	struct sxitimer_softc *sc = sxitimer_intrclock.ic_cookie;

	HWRITE4(sc, TMR_IRQ_STA, TMR0_IRQ_PEND);
	return clockintr_dispatch(frame);
}

void
sxitimer_rearm(void *cookie, uint64_t nsecs)
{
	struct sxitimer_softc *sc = cookie;
	uint32_t cycles;

	if (nsecs > sc->sc_nsec_max)
		nsecs = sc->sc_nsec_max;
	cycles = (nsecs * sc->sc_nsec_cycle_ratio) >> 32;
	if (cycles > UINT32_MAX)
		cycles = UINT32_MAX;
	if (cycles < 1)
		cycles = 1;
	HWRITE4(sc, TMR0_INTV_VALUE, cycles);
	HWRITE4(sc, TMR0_CTRL, TMR0_MODE_SINGLE | TMR0_CLK_PRES_1 |
	    TMR0_CLK_SRC_OSC24M | TMR0_RELOAD | TMR0_EN);
}

void
sxitimer_trigger(void *cookie)
{
	struct sxitimer_softc *sc = cookie;

	HWRITE4(sc, TMR0_INTV_VALUE, 1);
	HWRITE4(sc, TMR0_CTRL, TMR0_MODE_SINGLE | TMR0_CLK_PRES_1 |
	    TMR0_CLK_SRC_OSC24M | TMR0_RELOAD | TMR0_EN);
}
