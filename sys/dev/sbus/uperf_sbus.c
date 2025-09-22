/*	$OpenBSD: uperf_sbus.c,v 1.11 2022/03/13 13:34:54 mpi Exp $	*/

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
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>

#ifdef DDB
#include <machine/db_machdep.h>
#endif

#include <arch/sparc64/dev/uperfvar.h>
#include <dev/sun/uperfio.h>
#include <dev/sbus/sbusvar.h>
#include <dev/sbus/uperf_sbusreg.h>

int uperf_sbus_match(struct device *, void *, void *);
void uperf_sbus_attach(struct device *, struct device *, void *);

struct uperf_sbus_softc {
	struct uperf_softc	sc_usc;
	bus_space_tag_t		sc_bus_t;	/* direct register tag */
	bus_space_handle_t	sc_bus_h;	/* direct register handle */
};

const struct cfattach uperf_sbus_ca = {
	sizeof(struct uperf_sbus_softc), uperf_sbus_match, uperf_sbus_attach
};

u_int32_t uperf_sbus_read_reg(struct uperf_sbus_softc *, bus_size_t);
void uperf_sbus_write_reg(struct uperf_sbus_softc *,
    bus_size_t, u_int32_t);

int uperf_sbus_getcnt(void *, int, u_int32_t *, u_int32_t *);
int uperf_sbus_clrcnt(void *, int);
int uperf_sbus_getcntsrc(void *, int, u_int *, u_int *);
int uperf_sbus_setcntsrc(void *, int, u_int, u_int);

#ifdef DDB
void uperf_sbus_xir(void *, int);
#endif

struct uperf_src uperf_sbus_srcs[] = {
	{ UPERFSRC_SYSCK, UPERF_CNT0|UPERF_CNT1, SEL0_SYSCK },
	{ UPERFSRC_PRALL, UPERF_CNT0|UPERF_CNT1, SEL0_PRALL },
	{ UPERFSRC_PRP0, UPERF_CNT0|UPERF_CNT1, SEL0_PRP0 },
	{ UPERFSRC_PRU2S, UPERF_CNT0|UPERF_CNT1, SEL0_PRUS },
	{ UPERFSRC_UPA128, UPERF_CNT0, SEL0_128BUSY },
	{ UPERFSRC_RP0, UPERF_CNT1, SEL1_RDP0 },
	{ UPERFSRC_UPA64, UPERF_CNT0, SEL0_64BUSY },
	{ UPERFSRC_P0CRMR, UPERF_CNT1, SEL1_CRMP0 },
	{ UPERFSRC_PIOS, UPERF_CNT0, SEL0_PIOSTALL },
	{ UPERFSRC_P0PIO, UPERF_CNT1, SEL1_PIOP0 },
	{ UPERFSRC_MEMRI, UPERF_CNT0|UPERF_CNT0, SEL0_MEMREQ },
	{ UPERFSRC_MCBUSY, UPERF_CNT0, SEL0_MCBUSY },
	{ UPERFSRC_MEMRC, UPERF_CNT1, SEL1_MRC},
	{ UPERFSRC_PXSH, UPERF_CNT0, SEL0_PENDSTALL },
	{ UPERFSRC_RDP0, UPERF_CNT0, SEL1_RDP1 },
	{ UPERFSRC_P0CWMR, UPERF_CNT0, SEL0_CWMRP0 },
	{ UPERFSRC_CRMP1, UPERF_CNT1, SEL1_CRMP1 },
	{ UPERFSRC_P1CWMR, UPERF_CNT0, SEL0_CWMRP1 },
	{ UPERFSRC_PIOP1, UPERF_CNT1, SEL1_PIOP1 },
	{ UPERFSRC_CIT, UPERF_CNT0, SEL0_CIT },
	{ UPERFSRC_CWXI, UPERF_CNT1, SEL1_CWXI },
	{ UPERFSRC_U2SDAT, UPERF_CNT0|UPERF_CNT1, SEL0_DACT },
	{ UPERFSRC_CRXI, UPERF_CNT0, SEL0_CRXI },
	{ -1, -1, 0 }
};

int
uperf_sbus_match(struct device *parent, void *vcf, void *aux)
{
	struct sbus_attach_args *sa = aux;

	return (strcmp(sa->sa_name, "sc") == 0);
}

void
uperf_sbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct uperf_sbus_softc *sc = (struct uperf_sbus_softc *)self;
	char *model;
	u_int32_t id;

	sc->sc_bus_t = sa->sa_bustag;
	sc->sc_usc.usc_cookie = sc;
	sc->sc_usc.usc_getcntsrc = uperf_sbus_getcntsrc;
	sc->sc_usc.usc_setcntsrc = uperf_sbus_setcntsrc;
	sc->sc_usc.usc_clrcnt = uperf_sbus_clrcnt;
	sc->sc_usc.usc_getcnt = uperf_sbus_getcnt;
	sc->sc_usc.usc_srcs = uperf_sbus_srcs;

	if (sa->sa_nreg != 1) {
		printf(": expected 1 register, got %d\n", sa->sa_nreg);
		return;
	}

	if (sbus_bus_map(sc->sc_bus_t, sa->sa_reg[0].sbr_slot,
	    sa->sa_reg[0].sbr_offset, sa->sa_reg[0].sbr_size, 0, 0,
	    &sc->sc_bus_h) != 0) {
		printf(": couldn't map registers\n");
		return;
	}

	id = uperf_sbus_read_reg(sc, USC_ID);
	model = getpropstring(sa->sa_node, "model");
	if (model == NULL || strlen(model) == 0)
		model = "unknown";

	printf(": model %s (%x/%x) ports %d\n", model,
	    (id & USC_ID_IMPL_M) >> USC_ID_IMPL_S,
	    (id & USC_ID_VERS_M) >> USC_ID_VERS_S,
	    (id & USC_ID_UPANUM_M) >> USC_ID_UPANUM_S);

#ifdef DDB
	db_register_xir(uperf_sbus_xir, sc);
#endif
}

/*
 * Read from an indirect register
 */
u_int32_t
uperf_sbus_read_reg(struct uperf_sbus_softc *sc, bus_size_t r)
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
 * Write to an indirect register
 */
void
uperf_sbus_write_reg(struct uperf_sbus_softc *sc, bus_size_t r, u_int32_t v)
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
uperf_sbus_clrcnt(void *vsc, int flags)
{
	struct uperf_sbus_softc *sc = vsc;
	u_int32_t clr = 0, oldsrc;

	if (flags & UPERF_CNT0)
		clr |= USC_PCTRL_CLR0;
	if (flags & UPERF_CNT1)
		clr |= USC_PCTRL_CLR1;
	if (clr) {
		oldsrc = uperf_sbus_read_reg(sc, USC_PERFCTRL);
		uperf_sbus_write_reg(sc, USC_PERFCTRL, clr | oldsrc);
	}
	return (0);
}

int
uperf_sbus_setcntsrc(void *vsc, int flags, u_int src0, u_int src1)
{
	struct uperf_sbus_softc *sc = vsc;
	u_int32_t src;

	src = uperf_sbus_read_reg(sc, USC_PERFCTRL);
	if (flags & UPERF_CNT0) {
		src &= ~USC_PCTRL_SEL0;
		src |= ((src0 << 0) & USC_PCTRL_SEL0) | USC_PCTRL_CLR0;
	}
	if (flags & UPERF_CNT1) {
		src &= ~USC_PCTRL_SEL1;
		src |= ((src1 << 8) & USC_PCTRL_SEL1) | USC_PCTRL_CLR1;
	}
	uperf_sbus_write_reg(sc, USC_PERFCTRL, src);
	return (0);
}

int
uperf_sbus_getcntsrc(void *vsc, int flags, u_int *srcp0, u_int *srcp1)
{
	struct uperf_sbus_softc *sc = vsc;
	u_int32_t src;

	src = uperf_sbus_read_reg(sc, USC_PERFCTRL);
	if (flags & UPERF_CNT0)
		*srcp0 = (src & USC_PCTRL_SEL0) >> 0;
	if (flags & UPERF_CNT1)
		*srcp1 = (src & USC_PCTRL_SEL1) >> 8;
	return (0);
}

int
uperf_sbus_getcnt(void *vsc, int flags, u_int32_t *cntp0, u_int32_t *cntp1)
{
	struct uperf_sbus_softc *sc = vsc;
	u_int32_t c0, c1;

	c0 = uperf_sbus_read_reg(sc, USC_PERF0);
	c1 = uperf_sbus_read_reg(sc, USC_PERFSHAD);
	if (flags & UPERF_CNT0)
		*cntp0 = c0;
	if (flags & UPERF_CNT1)
		*cntp1 = c1;
	return (0);
}

#ifdef DDB
void
uperf_sbus_xir(void *arg, int cpu)
{
	struct uperf_sbus_softc *sc = arg;

	uperf_sbus_write_reg(sc, USC_CTRL, USC_CTRL_XIR);
}
#endif
