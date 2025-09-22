/*	$OpenBSD: uperf_ebus.c,v 1.10 2025/06/28 11:34:21 miod Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#ifdef DDB
#include <machine/db_machdep.h>
#endif

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>
#include <dev/sun/uperfio.h>
#include <dev/sbus/uperf_sbusreg.h>
#include <sparc64/dev/uperfvar.h>
#include <sparc64/dev/psychoreg.h>

struct uperf_ebus_softc {
	struct uperf_softc	sc_usc;
	bus_space_tag_t		sc_bus_t;
	bus_space_handle_t	sc_bus_h;
};

int	uperf_ebus_match(struct device *, void *, void *);
void	uperf_ebus_attach(struct device *, struct device *, void *);

const struct cfattach uperf_ebus_ca = {
	sizeof(struct uperf_ebus_softc), uperf_ebus_match, uperf_ebus_attach
};

u_int32_t uperf_ebus_read_reg(struct uperf_ebus_softc *, bus_size_t);
void uperf_ebus_write_reg(struct uperf_ebus_softc *,
    bus_size_t, u_int32_t);

int uperf_ebus_getcnt(void *, int, u_int32_t *, u_int32_t *);
int uperf_ebus_clrcnt(void *, int);
int uperf_ebus_getcntsrc(void *, int, u_int *, u_int *);
int uperf_ebus_setcntsrc(void *, int, u_int, u_int);

#ifdef DDB
void uperf_ebus_xir(void *, int);
#endif

struct uperf_src uperf_ebus_srcs[] = {
	{ UPERFSRC_SDVRA, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_SDVRA },
	{ UPERFSRC_SDVWA, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_SDVWA },
	{ UPERFSRC_CDVRA, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_CDVRA },
	{ UPERFSRC_CDVWA, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_CDVWA },
	{ UPERFSRC_SBMA, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_SBMA },
	{ UPERFSRC_DVA, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_DVA },
	{ UPERFSRC_DVWA, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_DVWA },
	{ UPERFSRC_PIOA, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_PIOA },
	{ UPERFSRC_SDVRB, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_SDVRB },
	{ UPERFSRC_SDVWB, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_SDVWB },
	{ UPERFSRC_CDVRB, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_CDVRB },
	{ UPERFSRC_CDVWB, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_CDVWB },
	{ UPERFSRC_SBMB, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_SBMB },
	{ UPERFSRC_DVB, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_DVB },
	{ UPERFSRC_DVWB, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_DVWB },
	{ UPERFSRC_PIOB, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_PIOB },
	{ UPERFSRC_TLBMISS, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_TLBMISS },
	{ UPERFSRC_NINTRS, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_NINTRS },
	{ UPERFSRC_INACK, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_INACK },
	{ UPERFSRC_PIOR, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_PIOR },
	{ UPERFSRC_PIOW, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_PIOW },
	{ UPERFSRC_MERGE, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_MERGE },
	{ UPERFSRC_TBLA, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_TBLA },
	{ UPERFSRC_STCA, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_STCA },
	{ UPERFSRC_TBLB, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_TBLB },
	{ UPERFSRC_STCB, UPERF_CNT0|UPERF_CNT1, PSY_PMCRSEL_STCB },
	{ -1, -1, 0 }
};
int
uperf_ebus_match(struct device *parent, void *match, void *aux)
{
	struct ebus_attach_args *ea = aux;

	return (strcmp(ea->ea_name, "sc") == 0);
}

void
uperf_ebus_attach(struct device *parent, struct device *self, void *aux)
{
	struct uperf_ebus_softc *sc = (void *)self;
	struct ebus_attach_args *ea = aux;
	char *model;
	u_int32_t id;

	sc->sc_bus_t = ea->ea_memtag;
	sc->sc_usc.usc_cookie = sc;
	sc->sc_usc.usc_getcntsrc = uperf_ebus_getcntsrc;
	sc->sc_usc.usc_setcntsrc = uperf_ebus_setcntsrc;
	sc->sc_usc.usc_clrcnt = uperf_ebus_clrcnt;
	sc->sc_usc.usc_getcnt = uperf_ebus_getcnt;
	sc->sc_usc.usc_srcs = uperf_ebus_srcs;

	/* Use prom address if available, otherwise map it. */
	if (ea->ea_nregs != 1) {
		printf(": expected 1 register, got %d\n", ea->ea_nregs);
		return;
	}

	if (ebus_bus_map(sc->sc_bus_t, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]), ea->ea_regs[0].size,
	    0, 0, &sc->sc_bus_h) != 0) {
		printf(": can't map register space\n");
		return;
	}

	id = uperf_ebus_read_reg(sc, USC_ID);
	model = getpropstring(ea->ea_node, "model");
	if (model == NULL || strlen(model) == 0)
		model = "unknown";

	printf(": model %s (%x/%x) ports %d\n", model,
	    (id & USC_ID_IMPL_M) >> USC_ID_IMPL_S,
	    (id & USC_ID_VERS_M) >> USC_ID_VERS_S,
	    (id & USC_ID_UPANUM_M) >> USC_ID_UPANUM_S);

#ifdef DDB
	db_register_xir(uperf_ebus_xir, sc);
#endif
}

/*
 * Read an indirect register.
 */
u_int32_t
uperf_ebus_read_reg(struct uperf_ebus_softc *sc, bus_size_t r)
{
	u_int32_t v;
	int s;

	s = splhigh();
	bus_space_write_1(sc->sc_bus_t, sc->sc_bus_h, USC_ADDR, r);
	bus_space_barrier(sc->sc_bus_t, sc->sc_bus_h, USC_ADDR, 1,
	    BUS_SPACE_BARRIER_WRITE);

	/* Can't use multi reads because we have to guarantee order */

	v = bus_space_read_1(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 0);
	bus_space_barrier(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 0, 1,
	    BUS_SPACE_BARRIER_READ);

	v <<= 8;
	v |= bus_space_read_1(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 1);
	bus_space_barrier(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 1, 1,
	    BUS_SPACE_BARRIER_READ);

	v <<= 8;
	v |= bus_space_read_1(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 2);
	bus_space_barrier(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 2, 1,
	    BUS_SPACE_BARRIER_READ);

	v <<= 8;
	v |= bus_space_read_1(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 3);
	bus_space_barrier(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 3, 1,
	    BUS_SPACE_BARRIER_READ);

	splx(s);
	return (v);
}

/*
 * Write an indirect register.
 */
void
uperf_ebus_write_reg(struct uperf_ebus_softc *sc, bus_size_t r, u_int32_t v)
{
	int s;

	s = splhigh();
	bus_space_write_1(sc->sc_bus_t, sc->sc_bus_h, USC_ADDR, r);
	bus_space_barrier(sc->sc_bus_t, sc->sc_bus_h, USC_ADDR, 1,
	    BUS_SPACE_BARRIER_WRITE);

	/* Can't use multi writes because we have to guarantee order */

	bus_space_write_1(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 0,
	    (v >> 24) & 0xff);
	bus_space_barrier(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 0, 1,
	    BUS_SPACE_BARRIER_WRITE);

	bus_space_write_1(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 1,
	    (v >> 16) & 0xff);
	bus_space_barrier(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 1, 1,
	    BUS_SPACE_BARRIER_WRITE);

	bus_space_write_1(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 2,
	    (v >> 8) & 0xff);
	bus_space_barrier(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 2, 1,
	    BUS_SPACE_BARRIER_WRITE);

	bus_space_write_1(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 3,
	    (v >> 0) & 0xff);
	bus_space_barrier(sc->sc_bus_t, sc->sc_bus_h, USC_DATA + 3, 1,
	    BUS_SPACE_BARRIER_WRITE);
	splx(s);
}

int
uperf_ebus_clrcnt(void *vsc, int flags)
{
	struct uperf_ebus_softc *sc = vsc;
	u_int32_t clr = 0, oldsrc;

	if (flags & UPERF_CNT0)
		clr |= USC_PCTRL_CLR0;
	if (flags & UPERF_CNT1)
		clr |= USC_PCTRL_CLR1;
	if (clr) {
		oldsrc = uperf_ebus_read_reg(sc, USC_PERFCTRL);
		uperf_ebus_write_reg(sc, USC_PERFCTRL, clr | oldsrc);
	}
	return (0);
}

int
uperf_ebus_setcntsrc(void *vsc, int flags, u_int src0, u_int src1)
{
	struct uperf_ebus_softc *sc = vsc;
	u_int32_t src;

	src = uperf_ebus_read_reg(sc, USC_PERFCTRL);
	if (flags & UPERF_CNT0) {
		src &= ~USC_PCTRL_SEL0;
		src |= ((src0 << 0) & USC_PCTRL_SEL0) | USC_PCTRL_CLR0;
	}
	if (flags & UPERF_CNT1) {
		src &= ~USC_PCTRL_SEL1;
		src |= ((src1 << 8) & USC_PCTRL_SEL1) | USC_PCTRL_CLR1;
	}
	uperf_ebus_write_reg(sc, USC_PERFCTRL, src);
	return (0);
}

int
uperf_ebus_getcntsrc(void *vsc, int flags, u_int *srcp0, u_int *srcp1)
{
	struct uperf_ebus_softc *sc = vsc;
	u_int32_t src;

	src = uperf_ebus_read_reg(sc, USC_PERFCTRL);
	if (flags & UPERF_CNT0)
		*srcp0 = (src & USC_PCTRL_SEL0) >> 0;
	if (flags & UPERF_CNT1)
		*srcp1 = (src & USC_PCTRL_SEL1) >> 8;
	return (0);
}

int
uperf_ebus_getcnt(void *vsc, int flags, u_int32_t *cntp0, u_int32_t *cntp1)
{
	struct uperf_ebus_softc *sc = vsc;
	u_int32_t c0, c1;

	c0 = uperf_ebus_read_reg(sc, USC_PERF0);
	c1 = uperf_ebus_read_reg(sc, USC_PERFSHAD);
	if (flags & UPERF_CNT0)
		*cntp0 = c0;
	if (flags & UPERF_CNT1)
		*cntp1 = c1;
	return (0);
}

#ifdef DDB
void
uperf_ebus_xir(void *arg, int cpu)
{
	struct uperf_ebus_softc *sc = arg;

	uperf_ebus_write_reg(sc, USC_CTRL, USC_CTRL_XIR);
}
#endif
