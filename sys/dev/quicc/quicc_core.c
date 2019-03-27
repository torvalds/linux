/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2006 by Juniper Networks.
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/serial.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/ic/quicc.h>

#include <dev/quicc/quicc_bfe.h>
#include <dev/quicc/quicc_bus.h>

#define	quicc_read2(r, o)	\
	bus_space_read_2((r)->r_bustag, (r)->r_bushandle, o)
#define	quicc_read4(r, o)	\
	bus_space_read_4((r)->r_bustag, (r)->r_bushandle, o)

#define	quicc_write2(r, o, v)	\
	bus_space_write_2((r)->r_bustag, (r)->r_bushandle, o, v)
#define	quicc_write4(r, o, v)	\
	bus_space_write_4((r)->r_bustag, (r)->r_bushandle, o, v)

devclass_t quicc_devclass;
char quicc_driver_name[] = "quicc";

static MALLOC_DEFINE(M_QUICC, "QUICC", "QUICC driver");

struct quicc_device {
	struct rman	*qd_rman;
	struct resource_list qd_rlist;
	device_t	qd_dev;
	int		qd_devtype;

	driver_filter_t	*qd_ih;
	void		*qd_ih_arg;
};

static int
quicc_bfe_intr(void *arg)
{
	struct quicc_device *qd;
	struct quicc_softc *sc = arg;
	uint32_t sipnr;

	sipnr = quicc_read4(sc->sc_rres, QUICC_REG_SIPNR_L);
	if (sipnr & 0x00f00000)
		qd = sc->sc_device;
	else
		qd = NULL;

	if (qd == NULL || qd->qd_ih == NULL) {
		device_printf(sc->sc_dev, "Stray interrupt %08x\n", sipnr);
		return (FILTER_STRAY);
	}

	return ((*qd->qd_ih)(qd->qd_ih_arg));
}

int
quicc_bfe_attach(device_t dev)
{
	struct quicc_device *qd;
	struct quicc_softc *sc;
	struct resource_list_entry *rle;
	const char *sep;
	rman_res_t size, start;
	int error;

	sc = device_get_softc(dev);

	/*
	 * Re-allocate. We expect that the softc contains the information
	 * collected by quicc_bfe_probe() intact.
	 */
	sc->sc_rres = bus_alloc_resource_any(dev, sc->sc_rtype, &sc->sc_rrid,
	    RF_ACTIVE);
	if (sc->sc_rres == NULL)
		return (ENXIO);

	start = rman_get_start(sc->sc_rres);
	size = rman_get_size(sc->sc_rres);

	sc->sc_rman.rm_start = start;
	sc->sc_rman.rm_end = start + size - 1;
	sc->sc_rman.rm_type = RMAN_ARRAY;
	sc->sc_rman.rm_descr = "QUICC resources";
	error = rman_init(&sc->sc_rman);
	if (!error)
		error = rman_manage_region(&sc->sc_rman, start,
		    start + size - 1);
	if (error) {
		bus_release_resource(dev, sc->sc_rtype, sc->sc_rrid,
		    sc->sc_rres);
		return (error);
	}

	/*
	 * Allocate interrupt resource.
	 */
	sc->sc_irid = 0;
	sc->sc_ires = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->sc_irid,
	    RF_ACTIVE | RF_SHAREABLE);

	if (sc->sc_ires != NULL) {
		error = bus_setup_intr(dev, sc->sc_ires,
		    INTR_TYPE_TTY, quicc_bfe_intr, NULL, sc, &sc->sc_icookie);
		if (error) {
			error = bus_setup_intr(dev, sc->sc_ires,
			    INTR_TYPE_TTY | INTR_MPSAFE, NULL,
			    (driver_intr_t *)quicc_bfe_intr, sc,
			    &sc->sc_icookie);
		} else
			sc->sc_fastintr = 1;
		if (error) {
			device_printf(dev, "could not activate interrupt\n");
			bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irid,
			    sc->sc_ires);
			sc->sc_ires = NULL;
		}
	}

	if (sc->sc_ires == NULL)
		sc->sc_polled = 1;

	if (bootverbose && (sc->sc_fastintr || sc->sc_polled)) {
		sep = "";
		device_print_prettyname(dev);
		if (sc->sc_fastintr) {
			printf("%sfast interrupt", sep);
			sep = ", ";
		}
		if (sc->sc_polled) {
			printf("%spolled mode", sep);
			sep = ", ";
		}
		printf("\n");
	}

	sc->sc_device = qd = malloc(sizeof(struct quicc_device), M_QUICC,
	    M_WAITOK | M_ZERO);

	qd->qd_devtype = QUICC_DEVTYPE_SCC;
	qd->qd_rman = &sc->sc_rman;
	resource_list_init(&qd->qd_rlist);

	resource_list_add(&qd->qd_rlist, sc->sc_rtype, 0, start,
	    start + size - 1, size);

	resource_list_add(&qd->qd_rlist, SYS_RES_IRQ, 0, 0xf00, 0xf00, 1);
	rle = resource_list_find(&qd->qd_rlist, SYS_RES_IRQ, 0);
	rle->res = sc->sc_ires;

	qd->qd_dev = device_add_child(dev, NULL, -1);
	device_set_ivars(qd->qd_dev, (void *)qd);
	error = device_probe_and_attach(qd->qd_dev);

	/* Enable all SCC interrupts. */
	quicc_write4(sc->sc_rres, QUICC_REG_SIMR_L, 0x00f00000);

	/* Clear all pending interrupts. */
	quicc_write4(sc->sc_rres, QUICC_REG_SIPNR_H, ~0);
	quicc_write4(sc->sc_rres, QUICC_REG_SIPNR_L, ~0);
	return (error);
}

int
quicc_bfe_detach(device_t dev)
{
	struct quicc_softc *sc;

	sc = device_get_softc(dev);

	bus_teardown_intr(dev, sc->sc_ires, sc->sc_icookie);
	bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irid, sc->sc_ires);
	bus_release_resource(dev, sc->sc_rtype, sc->sc_rrid, sc->sc_rres);
	return (0);
}

int
quicc_bfe_probe(device_t dev, u_int clock)
{
	struct quicc_softc *sc;
	uint16_t rev;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	if (device_get_desc(dev) == NULL)
		device_set_desc(dev,
		    "Quad integrated communications controller");

	sc->sc_rrid = 0;
	sc->sc_rtype = SYS_RES_MEMORY;
	sc->sc_rres = bus_alloc_resource_any(dev, sc->sc_rtype, &sc->sc_rrid,
	    RF_ACTIVE);
	if (sc->sc_rres == NULL) {
		sc->sc_rrid = 0;
		sc->sc_rtype = SYS_RES_IOPORT;
		sc->sc_rres = bus_alloc_resource_any(dev, sc->sc_rtype,
		    &sc->sc_rrid, RF_ACTIVE);
		if (sc->sc_rres == NULL)
			return (ENXIO);
	}

	sc->sc_clock = clock;

	/*
	 * Check that the microcode revision is 0x00e8, as documented
	 * in the MPC8555E PowerQUICC III Integrated Processor Family
	 * Reference Manual.
	 */
	rev = quicc_read2(sc->sc_rres, QUICC_PRAM_REV_NUM);

	bus_release_resource(dev, sc->sc_rtype, sc->sc_rrid, sc->sc_rres);
	return ((rev == 0x00e8) ? BUS_PROBE_DEFAULT : ENXIO);
}

struct resource *
quicc_bus_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct quicc_device *qd;
	struct resource_list_entry *rle;

	if (device_get_parent(child) != dev)
		return (NULL);

	/* We only support default allocations. */
	if (!RMAN_IS_DEFAULT_RANGE(start, end))
		return (NULL);

	qd = device_get_ivars(child);
	rle = resource_list_find(&qd->qd_rlist, type, *rid);
	if (rle == NULL)
		return (NULL);

	if (rle->res == NULL) {
		rle->res = rman_reserve_resource(qd->qd_rman, rle->start,
		    rle->start + rle->count - 1, rle->count, flags, child);
		if (rle->res != NULL) {
			rman_set_bustag(rle->res, &bs_be_tag);
			rman_set_bushandle(rle->res, rle->start);
		}
	}
	return (rle->res);
}

int
quicc_bus_get_resource(device_t dev, device_t child, int type, int rid,
    rman_res_t *startp, rman_res_t *countp)
{
	struct quicc_device *qd;
	struct resource_list_entry *rle;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	qd = device_get_ivars(child);
	rle = resource_list_find(&qd->qd_rlist, type, rid);
	if (rle == NULL)
		return (EINVAL);

	if (startp != NULL)
		*startp = rle->start;
	if (countp != NULL)
		*countp = rle->count;
	return (0);
}

int
quicc_bus_read_ivar(device_t dev, device_t child, int index, uintptr_t *result)
{
	struct quicc_device *qd;
	struct quicc_softc *sc;
	uint32_t sccr;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	sc = device_get_softc(dev);
	qd = device_get_ivars(child);

	switch (index) {
	case QUICC_IVAR_CLOCK:
		*result = sc->sc_clock;
		break;
	case QUICC_IVAR_BRGCLK:
		sccr = quicc_read4(sc->sc_rres, QUICC_REG_SCCR) & 3;
		*result = sc->sc_clock / ((1 << (sccr + 1)) << sccr);
		break;
	case QUICC_IVAR_DEVTYPE:
		*result = qd->qd_devtype;
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

int
quicc_bus_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *res)
{
	struct quicc_device *qd;
	struct resource_list_entry *rle;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	qd = device_get_ivars(child);
	rle = resource_list_find(&qd->qd_rlist, type, rid);
	return ((rle == NULL) ? EINVAL : 0);
}

int
quicc_bus_setup_intr(device_t dev, device_t child, struct resource *r,
    int flags, driver_filter_t *filt, void (*ihand)(void *), void *arg,
    void **cookiep)
{
	struct quicc_device *qd;
	struct quicc_softc *sc;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	/* Interrupt handlers must be FAST or MPSAFE. */
	if (filt == NULL && !(flags & INTR_MPSAFE))
		return (EINVAL);

	sc = device_get_softc(dev);
	if (sc->sc_polled)
		return (ENXIO);

	if (sc->sc_fastintr && filt == NULL) {
		sc->sc_fastintr = 0;
		bus_teardown_intr(dev, sc->sc_ires, sc->sc_icookie);
		bus_setup_intr(dev, sc->sc_ires, INTR_TYPE_TTY | INTR_MPSAFE,
		    NULL, (driver_intr_t *)quicc_bfe_intr, sc, &sc->sc_icookie);
	}

	qd = device_get_ivars(child);
	qd->qd_ih = (filt != NULL) ? filt : (driver_filter_t *)ihand;
	qd->qd_ih_arg = arg;
	*cookiep = ihand;
	return (0);
}

int
quicc_bus_teardown_intr(device_t dev, device_t child, struct resource *r,
    void *cookie)
{
	struct quicc_device *qd;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	qd = device_get_ivars(child);
	if (qd->qd_ih != cookie)
		return (EINVAL);

	qd->qd_ih = NULL;
	qd->qd_ih_arg = NULL;
	return (0);
}
