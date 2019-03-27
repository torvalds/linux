/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2006 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/serial.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/scc/scc_bfe.h>
#include <dev/scc/scc_bus.h>

#include "scc_if.h"

devclass_t scc_devclass;
const char scc_driver_name[] = "scc";

static MALLOC_DEFINE(M_SCC, "SCC", "SCC driver");

static int
scc_bfe_intr(void *arg)
{
	struct scc_softc *sc = arg;
	struct scc_chan *ch;
	struct scc_class *cl;
	struct scc_mode *m;
	int c, i, ipend, isrc;

	cl = sc->sc_class;
	while (!sc->sc_leaving && (ipend = SCC_IPEND(sc)) != 0) {
		i = 0, isrc = SER_INT_OVERRUN;
		while (ipend) {
			while (i < SCC_ISRCCNT && !(ipend & isrc))
				i++, isrc <<= 1;
			KASSERT(i < SCC_ISRCCNT, ("%s", __func__));
			ipend &= ~isrc;
			for (c = 0; c < cl->cl_channels; c++) {
				ch = &sc->sc_chan[c];
				if (!(ch->ch_ipend & isrc))
					continue;
				m = &ch->ch_mode[0];
				if (m->ih_src[i] == NULL)
					continue;
				if ((*m->ih_src[i])(m->ih_arg))
					ch->ch_ipend &= ~isrc;
			}
		}
		for (c = 0; c < cl->cl_channels; c++) {
			ch = &sc->sc_chan[c];
			if (!ch->ch_ipend)
				continue;
			m = &ch->ch_mode[0];
			if (m->ih != NULL)
				(*m->ih)(m->ih_arg);
			else
				SCC_ICLEAR(sc, ch);
		}
		return (FILTER_HANDLED);
	}
	return (FILTER_STRAY);
}

int
scc_bfe_attach(device_t dev, u_int ipc)
{
	struct resource_list_entry *rle;
	struct scc_chan *ch;
	struct scc_class *cl;
	struct scc_mode *m;
	struct scc_softc *sc, *sc0;
	const char *sep;
	bus_space_handle_t bh;
	rman_res_t base, size, start, sz;
	int c, error, mode, sysdev;

	/*
	 * The sc_class field defines the type of SCC we're going to work
	 * with and thus the size of the softc. Replace the generic softc
	 * with one that matches the SCC now that we're certain we handle
	 * the device.
	 */
	sc0 = device_get_softc(dev);
	cl = sc0->sc_class;
	if (cl->size > sizeof(*sc)) {
		sc = malloc(cl->size, M_SCC, M_WAITOK|M_ZERO);
		bcopy(sc0, sc, sizeof(*sc));
		device_set_softc(dev, sc);
	} else
		sc = sc0;

	size = abs(cl->cl_range) << sc->sc_bas.regshft;

	mtx_init(&sc->sc_hwmtx, "scc_hwmtx", NULL, MTX_SPIN);

	/*
	 * Re-allocate. We expect that the softc contains the information
	 * collected by scc_bfe_probe() intact.
	 */
	sc->sc_rres = bus_alloc_resource_anywhere(dev, sc->sc_rtype,
	    &sc->sc_rrid, cl->cl_channels * size, RF_ACTIVE);
	if (sc->sc_rres == NULL)
		return (ENXIO);
	sc->sc_bas.bsh = rman_get_bushandle(sc->sc_rres);
	sc->sc_bas.bst = rman_get_bustag(sc->sc_rres);

	/*
	 * Allocate interrupt resources. There may be a different interrupt
	 * per channel. We allocate them all...
	 */
	sc->sc_chan = malloc(sizeof(struct scc_chan) * cl->cl_channels,
	    M_SCC, M_WAITOK | M_ZERO);
	for (c = 0; c < cl->cl_channels; c++) {
		ch = &sc->sc_chan[c];
		/*
		 * XXX temporary hack. If we have more than 1 interrupt
		 * per channel, allocate the first for the channel. At
		 * this time only the macio bus front-end has more than
		 * 1 interrupt per channel and we don't use the 2nd and
		 * 3rd, because we don't support DMA yet.
		 */
		ch->ch_irid = c * ipc;
		ch->ch_ires = bus_alloc_resource_any(dev, SYS_RES_IRQ,
		    &ch->ch_irid, RF_ACTIVE | RF_SHAREABLE);
		if (ipc == 0)
			break;
	}

	/*
	 * Create the control structures for our children. Probe devices
	 * and query them to see if we can reset the hardware.
	 */
	sysdev = 0;
	base = rman_get_start(sc->sc_rres);
	sz = (size != 0) ? size : rman_get_size(sc->sc_rres);
	start = base + ((cl->cl_range < 0) ? size * (cl->cl_channels - 1) : 0);
	for (c = 0; c < cl->cl_channels; c++) {
		ch = &sc->sc_chan[c];
		resource_list_init(&ch->ch_rlist);
		ch->ch_nr = c + 1;

		if (!SCC_ENABLED(sc, ch))
			goto next;

		ch->ch_enabled = 1;
		resource_list_add(&ch->ch_rlist, sc->sc_rtype, 0, start,
		    start + sz - 1, sz);
		rle = resource_list_find(&ch->ch_rlist, sc->sc_rtype, 0);
		rle->res = &ch->ch_rres;
		bus_space_subregion(rman_get_bustag(sc->sc_rres),
		    rman_get_bushandle(sc->sc_rres), start - base, sz, &bh);
		rman_set_bushandle(rle->res, bh);
		rman_set_bustag(rle->res, rman_get_bustag(sc->sc_rres));

		resource_list_add(&ch->ch_rlist, SYS_RES_IRQ, 0, c, c, 1);
		rle = resource_list_find(&ch->ch_rlist, SYS_RES_IRQ, 0);
		rle->res = (ch->ch_ires != NULL) ? ch->ch_ires :
			    sc->sc_chan[0].ch_ires;

		for (mode = 0; mode < SCC_NMODES; mode++) {
			m = &ch->ch_mode[mode];
			m->m_chan = ch;
			m->m_mode = 1U << mode;
			if ((cl->cl_modes & m->m_mode) == 0 || ch->ch_sysdev)
				continue;
			m->m_dev = device_add_child(dev, NULL, -1);
			device_set_ivars(m->m_dev, (void *)m);
			error = device_probe_child(dev, m->m_dev);
			if (!error) {
				m->m_probed = 1;
				m->m_sysdev = SERDEV_SYSDEV(m->m_dev) ? 1 : 0;
				ch->ch_sysdev |= m->m_sysdev;
			}
		}

	 next:
		start += (cl->cl_range < 0) ? -size : size;
		sysdev |= ch->ch_sysdev;
	}

	/*
	 * Have the hardware driver initialize the hardware. Tell it
	 * whether or not a hardware reset should be performed.
	 */
	if (bootverbose) {
		device_printf(dev, "%sresetting hardware\n",
		    (sysdev) ? "not " : "");
	}
	error = SCC_ATTACH(sc, !sysdev);
	if (error)
		goto fail;

	/*
	 * Setup our interrupt handler. Make it FAST under the assumption
	 * that our children's are fast as well. We make it MPSAFE as soon
	 * as a child sets up a MPSAFE interrupt handler.
	 * Of course, if we can't setup a fast handler, we make it MPSAFE
	 * right away.
	 */
	for (c = 0; c < cl->cl_channels; c++) {
		ch = &sc->sc_chan[c];
		if (ch->ch_ires == NULL)
			continue;
		error = bus_setup_intr(dev, ch->ch_ires,
		    INTR_TYPE_TTY, scc_bfe_intr, NULL, sc,
		    &ch->ch_icookie);
		if (error) {
			error = bus_setup_intr(dev, ch->ch_ires,
			    INTR_TYPE_TTY | INTR_MPSAFE, NULL,
			    (driver_intr_t *)scc_bfe_intr, sc, &ch->ch_icookie);
		} else
			sc->sc_fastintr = 1;

		if (error) {
			device_printf(dev, "could not activate interrupt\n");
			bus_release_resource(dev, SYS_RES_IRQ, ch->ch_irid,
			    ch->ch_ires);
			ch->ch_ires = NULL;
		}
	}
	sc->sc_polled = 1;
	for (c = 0; c < cl->cl_channels; c++) {
		if (sc->sc_chan[0].ch_ires != NULL)
			sc->sc_polled = 0;
	}

	/*
	 * Attach all child devices that were probed successfully.
	 */
	for (c = 0; c < cl->cl_channels; c++) {
		ch = &sc->sc_chan[c];
		for (mode = 0; mode < SCC_NMODES; mode++) {
			m = &ch->ch_mode[mode];
			if (!m->m_probed)
				continue;
			error = device_attach(m->m_dev);
			if (error)
				continue;
			m->m_attached = 1;
		}
	}

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

	return (0);

 fail:
	for (c = 0; c < cl->cl_channels; c++) {
		ch = &sc->sc_chan[c];
		if (ch->ch_ires == NULL)
			continue;
		bus_release_resource(dev, SYS_RES_IRQ, ch->ch_irid,
		    ch->ch_ires);
	}
	bus_release_resource(dev, sc->sc_rtype, sc->sc_rrid, sc->sc_rres);
	return (error);
}

int
scc_bfe_detach(device_t dev)
{
	struct scc_chan *ch;
	struct scc_class *cl;
	struct scc_mode *m;
	struct scc_softc *sc;
	int chan, error, mode;

	sc = device_get_softc(dev);
	cl = sc->sc_class;

	/* Detach our children. */
	error = 0;
	for (chan = 0; chan < cl->cl_channels; chan++) {
		ch = &sc->sc_chan[chan];
		for (mode = 0; mode < SCC_NMODES; mode++) {
			m = &ch->ch_mode[mode];
			if (!m->m_attached)
				continue;
			if (device_detach(m->m_dev) != 0)
				error = ENXIO;
			else
				m->m_attached = 0;
		}
	}

	if (error)
		return (error);

	for (chan = 0; chan < cl->cl_channels; chan++) {
		ch = &sc->sc_chan[chan];
		if (ch->ch_ires == NULL)
			continue;
		bus_teardown_intr(dev, ch->ch_ires, ch->ch_icookie);
		bus_release_resource(dev, SYS_RES_IRQ, ch->ch_irid,
		    ch->ch_ires);
	}
	bus_release_resource(dev, sc->sc_rtype, sc->sc_rrid, sc->sc_rres);

	free(sc->sc_chan, M_SCC);

	mtx_destroy(&sc->sc_hwmtx);
	return (0);
}

int
scc_bfe_probe(device_t dev, u_int regshft, u_int rclk, u_int rid)
{
	struct scc_softc *sc;
	struct scc_class *cl;
	u_long size, sz;
	int error;

	/*
	 * Initialize the instance. Note that the instance (=softc) does
	 * not necessarily match the hardware specific softc. We can't do
	 * anything about it now, because we may not attach to the device.
	 * Hardware drivers cannot use any of the class specific fields
	 * while probing.
	 */
	sc = device_get_softc(dev);
	cl = sc->sc_class;
	kobj_init((kobj_t)sc, (kobj_class_t)cl);
	sc->sc_dev = dev;
	if (device_get_desc(dev) == NULL)
		device_set_desc(dev, cl->name);

	size = abs(cl->cl_range) << regshft;

	/*
	 * Allocate the register resource. We assume that all SCCs have a
	 * single register window in either I/O port space or memory mapped
	 * I/O space. Any SCC that needs multiple windows will consequently
	 * not be supported by this driver as-is.
	 */
	sc->sc_rrid = rid;
	sc->sc_rtype = SYS_RES_MEMORY;
	sc->sc_rres = bus_alloc_resource_anywhere(dev, sc->sc_rtype,
	    &sc->sc_rrid, cl->cl_channels * size, RF_ACTIVE);
	if (sc->sc_rres == NULL) {
		sc->sc_rrid = rid;
		sc->sc_rtype = SYS_RES_IOPORT;
		sc->sc_rres = bus_alloc_resource_anywhere(dev, sc->sc_rtype,
		    &sc->sc_rrid, cl->cl_channels * size, RF_ACTIVE);
		if (sc->sc_rres == NULL)
			return (ENXIO);
	}

	/*
	 * Fill in the bus access structure and call the hardware specific
	 * probe method.
	 */
	sz = (size != 0) ? size : rman_get_size(sc->sc_rres);
	sc->sc_bas.bsh = rman_get_bushandle(sc->sc_rres);
	sc->sc_bas.bst = rman_get_bustag(sc->sc_rres);
	sc->sc_bas.range = sz;
	sc->sc_bas.rclk = rclk;
	sc->sc_bas.regshft = regshft;

	error = SCC_PROBE(sc);
	bus_release_resource(dev, sc->sc_rtype, sc->sc_rrid, sc->sc_rres);
	return ((error == 0) ? BUS_PROBE_DEFAULT : error);
}

struct resource *
scc_bus_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct resource_list_entry *rle;
	struct scc_chan *ch;
	struct scc_mode *m;

	if (device_get_parent(child) != dev)
		return (NULL);

	/* We only support default allocations. */
	if (!RMAN_IS_DEFAULT_RANGE(start, end))
		return (NULL);

	m = device_get_ivars(child);
	ch = m->m_chan;
	rle = resource_list_find(&ch->ch_rlist, type, 0);
	if (rle == NULL)
		return (NULL);
	*rid = 0;
	return (rle->res);
}

int
scc_bus_get_resource(device_t dev, device_t child, int type, int rid,
    rman_res_t *startp, rman_res_t *countp)
{
	struct resource_list_entry *rle;
	struct scc_chan *ch;
	struct scc_mode *m;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	m = device_get_ivars(child);
	ch = m->m_chan;
	rle = resource_list_find(&ch->ch_rlist, type, rid);
	if (rle == NULL)
		return (EINVAL);

	if (startp != NULL)
		*startp = rle->start;
	if (countp != NULL)
		*countp = rle->count;
	return (0);
}

int
scc_bus_read_ivar(device_t dev, device_t child, int index, uintptr_t *result)
{
	struct scc_chan *ch;
	struct scc_class *cl;
	struct scc_mode *m;
	struct scc_softc *sc;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	sc = device_get_softc(dev);
	cl = sc->sc_class;
	m = device_get_ivars(child);
	ch = m->m_chan;

	switch (index) {
	case SCC_IVAR_CHANNEL:
		*result = ch->ch_nr;
		break;
	case SCC_IVAR_CLASS:
		*result = cl->cl_class;
		break;
	case SCC_IVAR_CLOCK:
		*result = sc->sc_bas.rclk;
		break;
	case SCC_IVAR_MODE:
		*result = m->m_mode;
		break;
	case SCC_IVAR_REGSHFT:
		*result = sc->sc_bas.regshft;
		break;
	case SCC_IVAR_HWMTX:
		*result = (uintptr_t)&sc->sc_hwmtx;
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

int
scc_bus_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *res)
{
	struct resource_list_entry *rle;
	struct scc_chan *ch;
	struct scc_mode *m;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	m = device_get_ivars(child);
	ch = m->m_chan;
	rle = resource_list_find(&ch->ch_rlist, type, rid);
	return ((rle == NULL) ? EINVAL : 0);
}

int
scc_bus_setup_intr(device_t dev, device_t child, struct resource *r, int flags,
    driver_filter_t *filt, void (*ihand)(void *), void *arg, void **cookiep)
{
	struct scc_chan *ch;
	struct scc_mode *m;
	struct scc_softc *sc;
	int c, i, isrc;

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
		for (c = 0; c < sc->sc_class->cl_channels; c++) {
			ch = &sc->sc_chan[c];
			if (ch->ch_ires == NULL)
				continue;
			bus_teardown_intr(dev, ch->ch_ires, ch->ch_icookie);
			bus_setup_intr(dev, ch->ch_ires,
			    INTR_TYPE_TTY | INTR_MPSAFE, NULL,
			    (driver_intr_t *)scc_bfe_intr, sc, &ch->ch_icookie);
		}
	}

	m = device_get_ivars(child);
	m->m_hasintr = 1;
	m->m_fastintr = (filt != NULL) ? 1 : 0;
	m->ih = (filt != NULL) ? filt : (driver_filter_t *)ihand;
	m->ih_arg = arg;

	i = 0, isrc = SER_INT_OVERRUN;
	while (i < SCC_ISRCCNT) {
		m->ih_src[i] = SERDEV_IHAND(child, isrc);
		if (m->ih_src[i] != NULL)
			m->ih = NULL;
		i++, isrc <<= 1;
	}
	return (0);
}

int
scc_bus_teardown_intr(device_t dev, device_t child, struct resource *r,
    void *cookie)
{
	struct scc_mode *m;
	int i;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	m = device_get_ivars(child);
	if (!m->m_hasintr)
		return (EINVAL);

	m->m_hasintr = 0;
	m->m_fastintr = 0;
	m->ih = NULL;
	m->ih_arg = NULL;
	for (i = 0; i < SCC_ISRCCNT; i++)
		m->ih_src[i] = NULL;
	return (0);
}
