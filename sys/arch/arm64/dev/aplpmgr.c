/*	$OpenBSD: aplpmgr.c,v 1.5 2023/07/20 20:40:44 kettenis Exp $	*/
/*
 * Copyright (c) 2021 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/fdt.h>

#define PMGR_PS_TARGET_MASK	0x0000000f
#define PMGR_PS_TARGET_SHIFT	0
#define PMGR_PS_ACTUAL_MASK	0x000000f0
#define PMGR_PS_ACTUAL_SHIFT	4
#define  PMGR_PS_ACTIVE		0xf
#define  PMGR_PS_PWRGATE	0x0
#define PMGR_WAS_PWRGATED	0x00000100
#define PMGR_WAS_CLKGATED	0x00000200
#define PMGR_DEV_DISABLE	0x00000400
#define PMGR_RESET		0x80000000

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct aplpmgr_softc;

struct aplpmgr_pwrstate {
	struct aplpmgr_softc	*ps_sc;
	bus_size_t		ps_offset;
	int			ps_enablecount;
	struct power_domain_device ps_pd;
	struct reset_device	ps_rd;
};

struct aplpmgr_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct aplpmgr_pwrstate	*sc_pwrstate;
	int			sc_npwrstate;
};

int	aplpmgr_match(struct device *, void *, void *);
void	aplpmgr_attach(struct device *, struct device *, void *);

const struct cfattach aplpmgr_ca = {
	sizeof (struct aplpmgr_softc), aplpmgr_match, aplpmgr_attach
};

struct cfdriver aplpmgr_cd = {
	NULL, "aplpmgr", DV_DULL
};

void	aplpmgr_enable(void *, uint32_t *, int);
void	aplpmgr_reset(void *, uint32_t *, int);

int
aplpmgr_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	if (OF_is_compatible(faa->fa_node, "apple,pmgr"))
		return 10;	/* Must beat syscon(4). */

	return 0;
}

void
aplpmgr_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplpmgr_softc *sc = (struct aplpmgr_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct aplpmgr_pwrstate *ps;
	uint32_t reg[2];
	int node;

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

	for (node = OF_child(faa->fa_node); node; node = OF_peer(node)) {
		if (OF_is_compatible(node, "apple,pmgr-pwrstate"))
			sc->sc_npwrstate++;
	}

	sc->sc_pwrstate = mallocarray(sc->sc_npwrstate,
	    sizeof(*sc->sc_pwrstate), M_DEVBUF, M_WAITOK | M_ZERO);

	ps = sc->sc_pwrstate;
	for (node = OF_child(faa->fa_node); node; node = OF_peer(node)) {
		if (!OF_is_compatible(node, "apple,pmgr-pwrstate"))
			continue;

		if (OF_getpropintarray(node, "reg", reg,
		    sizeof(reg)) != sizeof(reg)) {
			printf("%s: invalid reg property\n",
			    sc->sc_dev.dv_xname);
			continue;
		}

		ps->ps_sc = sc;
		ps->ps_offset = reg[0];
		if (OF_getpropbool(node, "apple,always-on"))
			ps->ps_enablecount = 1;

		ps->ps_pd.pd_node = node;
		ps->ps_pd.pd_cookie = ps;
		ps->ps_pd.pd_enable = aplpmgr_enable;
		power_domain_register(&ps->ps_pd);

		ps->ps_rd.rd_node = node;
		ps->ps_rd.rd_cookie = ps;
		ps->ps_rd.rd_reset = aplpmgr_reset;
		reset_register(&ps->ps_rd);

		ps++;
	}
}

void
aplpmgr_enable(void *cookie, uint32_t *cells, int on)
{
	struct aplpmgr_pwrstate *ps = cookie;
	struct aplpmgr_softc *sc = ps->ps_sc;
	uint32_t pstate = on ? PMGR_PS_ACTIVE : PMGR_PS_PWRGATE;
	uint32_t val;
	int timo;

	KASSERT(on || ps->ps_enablecount > 0);
	KASSERT(!on || ps->ps_enablecount < INT_MAX);

	if (on && ps->ps_enablecount > 0) {
		power_domain_enable_all(ps->ps_pd.pd_node);
		ps->ps_enablecount++;
		return;
	}
	if (!on && ps->ps_enablecount > 1) {
		power_domain_disable_all(ps->ps_pd.pd_node);
		ps->ps_enablecount--;
		return;
	}

	/* Enable parents before enabling ourselves. */
	if (on) {
		power_domain_enable_all(ps->ps_pd.pd_node);
		ps->ps_enablecount++;
	}

	val = HREAD4(sc, ps->ps_offset);
	val &= ~PMGR_PS_TARGET_MASK;
	val |= (pstate << PMGR_PS_TARGET_SHIFT);
	HWRITE4(sc, ps->ps_offset, val);

	for (timo = 0; timo < 100; timo++) {
		val = HREAD4(sc, ps->ps_offset);
		val &= PMGR_PS_ACTUAL_MASK;
		if ((val >> PMGR_PS_ACTUAL_SHIFT) == pstate)
			break;
		delay(1);
	}

	/* Disable parents after disabling ourselves. */
	if (!on) {
		power_domain_disable_all(ps->ps_pd.pd_node);
		ps->ps_enablecount--;
	}
}

void
aplpmgr_reset(void *cookie, uint32_t *cells, int on)
{
	struct aplpmgr_pwrstate *ps = cookie;
	struct aplpmgr_softc *sc = ps->ps_sc;
	uint32_t val;

	if (on) {
		val = HREAD4(sc, ps->ps_offset);
		val &= ~(PMGR_WAS_CLKGATED | PMGR_WAS_PWRGATED);
		HWRITE4(sc, ps->ps_offset, val | PMGR_DEV_DISABLE);
		val = HREAD4(sc, ps->ps_offset);
		val &= ~(PMGR_WAS_CLKGATED | PMGR_WAS_PWRGATED);
		HWRITE4(sc, ps->ps_offset, val | PMGR_RESET);
	} else {
		val = HREAD4(sc, ps->ps_offset);
		val &= ~(PMGR_WAS_CLKGATED | PMGR_WAS_PWRGATED);
		HWRITE4(sc, ps->ps_offset, val & ~PMGR_RESET);
		val = HREAD4(sc, ps->ps_offset);
		val &= ~(PMGR_WAS_CLKGATED | PMGR_WAS_PWRGATED);
		HWRITE4(sc, ps->ps_offset, val & ~PMGR_DEV_DISABLE);
	}
}
