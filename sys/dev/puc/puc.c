/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Marcel Moolenaar
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
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/puc/puc_bus.h>
#include <dev/puc/puc_cfg.h>
#include <dev/puc/puc_bfe.h>

#define	PUC_ISRCCNT	5

struct puc_port {
	struct puc_bar	*p_bar;
	struct resource *p_rres;
	struct resource *p_ires;
	device_t	p_dev;
	int		p_nr;
	int		p_type;
	int		p_rclk;

	int		p_hasintr:1;

	serdev_intr_t	*p_ihsrc[PUC_ISRCCNT];
	void		*p_iharg;

	int		p_ipend;
};

devclass_t puc_devclass;
const char puc_driver_name[] = "puc";

static MALLOC_DEFINE(M_PUC, "PUC", "PUC driver");

SYSCTL_NODE(_hw, OID_AUTO, puc, CTLFLAG_RD, 0, "puc(9) driver configuration");

struct puc_bar *
puc_get_bar(struct puc_softc *sc, int rid)
{
	struct puc_bar *bar;
	struct rman *rm;
	rman_res_t end, start;
	int error, i;

	/* Find the BAR entry with the given RID. */
	i = 0;
	while (i < PUC_PCI_BARS && sc->sc_bar[i].b_rid != rid)
		i++;
	if (i < PUC_PCI_BARS)
		return (&sc->sc_bar[i]);

	/* Not found. If we're looking for an unused entry, return NULL. */
	if (rid == -1)
		return (NULL);

	/* Get an unused entry for us to fill.  */
	bar = puc_get_bar(sc, -1);
	if (bar == NULL)
		return (NULL);
	bar->b_rid = rid;
	bar->b_type = SYS_RES_IOPORT;
	bar->b_res = bus_alloc_resource_any(sc->sc_dev, bar->b_type,
	    &bar->b_rid, RF_ACTIVE);
	if (bar->b_res == NULL) {
		bar->b_rid = rid;
		bar->b_type = SYS_RES_MEMORY;
		bar->b_res = bus_alloc_resource_any(sc->sc_dev, bar->b_type,
		    &bar->b_rid, RF_ACTIVE);
		if (bar->b_res == NULL) {
			bar->b_rid = -1;
			return (NULL);
		}
	}

	/* Update our managed space. */
	rm = (bar->b_type == SYS_RES_IOPORT) ? &sc->sc_ioport : &sc->sc_iomem;
	start = rman_get_start(bar->b_res);
	end = rman_get_end(bar->b_res);
	error = rman_manage_region(rm, start, end);
	if (error) {
		bus_release_resource(sc->sc_dev, bar->b_type, bar->b_rid,
		    bar->b_res);
		bar->b_res = NULL;
		bar->b_rid = -1;
		bar = NULL;
	}

	return (bar);
}

static int
puc_intr(void *arg)
{
	struct puc_port *port;
	struct puc_softc *sc = arg;
	u_long ds, dev, devs;
	int i, idx, ipend, isrc, nints;
	uint8_t ilr;

	nints = 0;
	while (1) {
		/*
		 * Obtain the set of devices with pending interrupts.
		 */
		devs = sc->sc_serdevs;
		if (sc->sc_ilr == PUC_ILR_DIGI) {
			idx = 0;
			while (devs & (0xfful << idx)) {
				ilr = ~bus_read_1(sc->sc_port[idx].p_rres, 7);
				devs &= ~0ul ^ ((u_long)ilr << idx);
				idx += 8;
			}
		} else if (sc->sc_ilr == PUC_ILR_QUATECH) {
			/*
			 * Don't trust the value if it's the same as the option
			 * register. It may mean that the ILR is not active and
			 * we're reading the option register instead. This may
			 * lead to false positives on 8-port boards.
			 */
			ilr = bus_read_1(sc->sc_port[0].p_rres, 7);
			if (ilr != (sc->sc_cfg_data & 0xff))
				devs &= (u_long)ilr;
		}
		if (devs == 0UL)
			break;

		/*
		 * Obtain the set of interrupt sources from those devices
		 * that have pending interrupts.
		 */
		ipend = 0;
		idx = 0, dev = 1UL;
		ds = devs;
		while (ds != 0UL) {
			while ((ds & dev) == 0UL)
				idx++, dev <<= 1;
			ds &= ~dev;
			port = &sc->sc_port[idx];
			port->p_ipend = SERDEV_IPEND(port->p_dev);
			ipend |= port->p_ipend;
		}
		if (ipend == 0)
			break;

		i = 0, isrc = SER_INT_OVERRUN;
		while (ipend) {
			while (i < PUC_ISRCCNT && !(ipend & isrc))
				i++, isrc <<= 1;
			KASSERT(i < PUC_ISRCCNT, ("%s", __func__));
			ipend &= ~isrc;
			idx = 0, dev = 1UL;
			ds = devs;
			while (ds != 0UL) {
				while ((ds & dev) == 0UL)
					idx++, dev <<= 1;
				ds &= ~dev;
				port = &sc->sc_port[idx];
				if (!(port->p_ipend & isrc))
					continue;
				if (port->p_ihsrc[i] != NULL)
					(*port->p_ihsrc[i])(port->p_iharg);
				nints++;
			}
		}
	}

	return ((nints > 0) ? FILTER_HANDLED : FILTER_STRAY);
}

int
puc_bfe_attach(device_t dev)
{
	char buffer[64];
	struct puc_bar *bar;
	struct puc_port *port;
	struct puc_softc *sc;
	struct rman *rm;
	intptr_t res;
	bus_addr_t ofs, start;
	bus_size_t size;
	bus_space_handle_t bsh;
	bus_space_tag_t bst;
	int error, idx;

	sc = device_get_softc(dev);

	for (idx = 0; idx < PUC_PCI_BARS; idx++)
		sc->sc_bar[idx].b_rid = -1;

	do {
		sc->sc_ioport.rm_type = RMAN_ARRAY;
		error = rman_init(&sc->sc_ioport);
		if (!error) {
			sc->sc_iomem.rm_type = RMAN_ARRAY;
			error = rman_init(&sc->sc_iomem);
			if (!error) {
				sc->sc_irq.rm_type = RMAN_ARRAY;
				error = rman_init(&sc->sc_irq);
				if (!error)
					break;
				rman_fini(&sc->sc_iomem);
			}
			rman_fini(&sc->sc_ioport);
		}
		return (error);
	} while (0);

	snprintf(buffer, sizeof(buffer), "%s I/O port mapping",
	    device_get_nameunit(dev));
	sc->sc_ioport.rm_descr = strdup(buffer, M_PUC);
	snprintf(buffer, sizeof(buffer), "%s I/O memory mapping",
	    device_get_nameunit(dev));
	sc->sc_iomem.rm_descr = strdup(buffer, M_PUC);
	snprintf(buffer, sizeof(buffer), "%s port numbers",
	    device_get_nameunit(dev));
	sc->sc_irq.rm_descr = strdup(buffer, M_PUC);

	error = puc_config(sc, PUC_CFG_GET_NPORTS, 0, &res);
	KASSERT(error == 0, ("%s %d", __func__, __LINE__));
	sc->sc_nports = (int)res;
	sc->sc_port = malloc(sc->sc_nports * sizeof(struct puc_port),
	    M_PUC, M_WAITOK|M_ZERO);

	error = rman_manage_region(&sc->sc_irq, 1, sc->sc_nports);
	if (error)
		goto fail;

	error = puc_config(sc, PUC_CFG_SETUP, 0, &res);
	if (error)
		goto fail;

	for (idx = 0; idx < sc->sc_nports; idx++) {
		port = &sc->sc_port[idx];
		port->p_nr = idx + 1;
		error = puc_config(sc, PUC_CFG_GET_TYPE, idx, &res);
		if (error)
			goto fail;
		port->p_type = res;
		error = puc_config(sc, PUC_CFG_GET_RID, idx, &res);
		if (error)
			goto fail;
		bar = puc_get_bar(sc, res);
		if (bar == NULL) {
			error = ENXIO;
			goto fail;
		}
		port->p_bar = bar;
		start = rman_get_start(bar->b_res);
		error = puc_config(sc, PUC_CFG_GET_OFS, idx, &res);
		if (error)
			goto fail;
		ofs = res;
		error = puc_config(sc, PUC_CFG_GET_LEN, idx, &res);
		if (error)
			goto fail;
		size = res;
		rm = (bar->b_type == SYS_RES_IOPORT)
		    ? &sc->sc_ioport: &sc->sc_iomem;
		port->p_rres = rman_reserve_resource(rm, start + ofs,
		    start + ofs + size - 1, size, 0, NULL);
		if (port->p_rres != NULL) {
			bsh = rman_get_bushandle(bar->b_res);
			bst = rman_get_bustag(bar->b_res);
			bus_space_subregion(bst, bsh, ofs, size, &bsh);
			rman_set_bushandle(port->p_rres, bsh);
			rman_set_bustag(port->p_rres, bst);
		}
		port->p_ires = rman_reserve_resource(&sc->sc_irq, port->p_nr,
		    port->p_nr, 1, 0, NULL);
		if (port->p_ires == NULL) {
			error = ENXIO;
			goto fail;
		}
		error = puc_config(sc, PUC_CFG_GET_CLOCK, idx, &res);
		if (error)
			goto fail;
		port->p_rclk = res;

		port->p_dev = device_add_child(dev, NULL, -1);
		if (port->p_dev != NULL)
			device_set_ivars(port->p_dev, (void *)port);
	}

	error = puc_config(sc, PUC_CFG_GET_ILR, 0, &res);
	if (error)
		goto fail;
	sc->sc_ilr = res;
	if (bootverbose && sc->sc_ilr != 0)
		device_printf(dev, "using interrupt latch register\n");

	sc->sc_ires = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->sc_irid,
	    RF_ACTIVE|RF_SHAREABLE);
	if (sc->sc_ires != NULL) {
		error = bus_setup_intr(dev, sc->sc_ires,
		    INTR_TYPE_TTY, puc_intr, NULL, sc, &sc->sc_icookie);
		if (error)
			error = bus_setup_intr(dev, sc->sc_ires,
			    INTR_TYPE_TTY | INTR_MPSAFE, NULL,
			    (driver_intr_t *)puc_intr, sc, &sc->sc_icookie);
		else
			sc->sc_fastintr = 1;

		if (error) {
			device_printf(dev, "could not activate interrupt\n");
			bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irid,
			    sc->sc_ires);
			sc->sc_ires = NULL;
		}
	}
	if (sc->sc_ires == NULL) {
		/* XXX no interrupt resource. Force polled mode. */
		sc->sc_polled = 1;
	}

	/* Probe and attach our children. */
	for (idx = 0; idx < sc->sc_nports; idx++) {
		port = &sc->sc_port[idx];
		if (port->p_dev == NULL)
			continue;
		error = device_probe_and_attach(port->p_dev);
		if (error) {
			device_delete_child(dev, port->p_dev);
			port->p_dev = NULL;
		}
	}

	/*
	 * If there are no serdev devices, then our interrupt handler
	 * will do nothing. Tear it down.
	 */
	if (sc->sc_serdevs == 0UL)
		bus_teardown_intr(dev, sc->sc_ires, sc->sc_icookie);

	return (0);

fail:
	for (idx = 0; idx < sc->sc_nports; idx++) {
		port = &sc->sc_port[idx];
		if (port->p_dev != NULL)
			device_delete_child(dev, port->p_dev);
		if (port->p_rres != NULL)
			rman_release_resource(port->p_rres);
		if (port->p_ires != NULL)
			rman_release_resource(port->p_ires);
	}
	for (idx = 0; idx < PUC_PCI_BARS; idx++) {
		bar = &sc->sc_bar[idx];
		if (bar->b_res != NULL)
			bus_release_resource(sc->sc_dev, bar->b_type,
			    bar->b_rid, bar->b_res);
	}
	rman_fini(&sc->sc_irq);
	free(__DECONST(void *, sc->sc_irq.rm_descr), M_PUC);
	rman_fini(&sc->sc_iomem);
	free(__DECONST(void *, sc->sc_iomem.rm_descr), M_PUC);
	rman_fini(&sc->sc_ioport);
	free(__DECONST(void *, sc->sc_ioport.rm_descr), M_PUC);
	free(sc->sc_port, M_PUC);
	return (error);
}

int
puc_bfe_detach(device_t dev)
{
	struct puc_bar *bar;
	struct puc_port *port;
	struct puc_softc *sc;
	int error, idx;

	sc = device_get_softc(dev);

	/* Detach our children. */
	error = 0;
	for (idx = 0; idx < sc->sc_nports; idx++) {
		port = &sc->sc_port[idx];
		if (port->p_dev == NULL)
			continue;
		if (device_delete_child(dev, port->p_dev) == 0) {
			if (port->p_rres != NULL)
				rman_release_resource(port->p_rres);
			if (port->p_ires != NULL)
				rman_release_resource(port->p_ires);
		} else
			error = ENXIO;
	}
	if (error)
		return (error);

	if (sc->sc_serdevs != 0UL)
		bus_teardown_intr(dev, sc->sc_ires, sc->sc_icookie);
	bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irid, sc->sc_ires);

	for (idx = 0; idx < PUC_PCI_BARS; idx++) {
		bar = &sc->sc_bar[idx];
		if (bar->b_res != NULL)
			bus_release_resource(sc->sc_dev, bar->b_type,
			    bar->b_rid, bar->b_res);
	}

	rman_fini(&sc->sc_irq);
	free(__DECONST(void *, sc->sc_irq.rm_descr), M_PUC);
	rman_fini(&sc->sc_iomem);
	free(__DECONST(void *, sc->sc_iomem.rm_descr), M_PUC);
	rman_fini(&sc->sc_ioport);
	free(__DECONST(void *, sc->sc_ioport.rm_descr), M_PUC);
	free(sc->sc_port, M_PUC);
	return (0);
}

int
puc_bfe_probe(device_t dev, const struct puc_cfg *cfg)
{
	struct puc_softc *sc;
	intptr_t res;
	int error;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_cfg = cfg;

	/* We don't attach to single-port serial cards. */
	if (cfg->ports == PUC_PORT_1S || cfg->ports == PUC_PORT_1P)
		return (EDOOFUS);
	error = puc_config(sc, PUC_CFG_GET_NPORTS, 0, &res);
	if (error)
		return (error);
	error = puc_config(sc, PUC_CFG_GET_DESC, 0, &res);
	if (error)
		return (error);
	if (res != 0)
		device_set_desc(dev, (const char *)res);
	return (BUS_PROBE_DEFAULT);
}

struct resource *
puc_bus_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct puc_port *port;
	struct resource *res;
	device_t assigned, originator;
	int error;

	/* Get our immediate child. */
	originator = child;
	while (child != NULL && device_get_parent(child) != dev)
		child = device_get_parent(child);
	if (child == NULL)
		return (NULL);

	port = device_get_ivars(child);
	KASSERT(port != NULL, ("%s %d", __func__, __LINE__));

	if (rid == NULL || *rid != 0)
		return (NULL);

	/* We only support default allocations. */
	if (!RMAN_IS_DEFAULT_RANGE(start, end))
		return (NULL);

	if (type == port->p_bar->b_type)
		res = port->p_rres;
	else if (type == SYS_RES_IRQ)
		res = port->p_ires;
	else
		return (NULL);

	if (res == NULL)
		return (NULL);

	assigned = rman_get_device(res);
	if (assigned == NULL)	/* Not allocated */
		rman_set_device(res, originator);
	else if (assigned != originator)
		return (NULL);

	if (flags & RF_ACTIVE) {
		error = rman_activate_resource(res);
		if (error) {
			if (assigned == NULL)
				rman_set_device(res, NULL);
			return (NULL);
		}
	}

	return (res);
}

int
puc_bus_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *res)
{
	struct puc_port *port;
	device_t originator;

	/* Get our immediate child. */
	originator = child;
	while (child != NULL && device_get_parent(child) != dev)
		child = device_get_parent(child);
	if (child == NULL)
		return (EINVAL);

	port = device_get_ivars(child);
	KASSERT(port != NULL, ("%s %d", __func__, __LINE__));

	if (rid != 0 || res == NULL)
		return (EINVAL);

	if (type == port->p_bar->b_type) {
		if (res != port->p_rres)
			return (EINVAL);
	} else if (type == SYS_RES_IRQ) {
		if (res != port->p_ires)
			return (EINVAL);
		if (port->p_hasintr)
			return (EBUSY);
	} else
		return (EINVAL);

	if (rman_get_device(res) != originator)
		return (ENXIO);
	if (rman_get_flags(res) & RF_ACTIVE)
		rman_deactivate_resource(res);
	rman_set_device(res, NULL);
	return (0);
}

int
puc_bus_get_resource(device_t dev, device_t child, int type, int rid,
    rman_res_t *startp, rman_res_t *countp)
{
	struct puc_port *port;
	struct resource *res;
	rman_res_t start;

	/* Get our immediate child. */
	while (child != NULL && device_get_parent(child) != dev)
		child = device_get_parent(child);
	if (child == NULL)
		return (EINVAL);

	port = device_get_ivars(child);
	KASSERT(port != NULL, ("%s %d", __func__, __LINE__));

	if (type == port->p_bar->b_type)
		res = port->p_rres;
	else if (type == SYS_RES_IRQ)
		res = port->p_ires;
	else
		return (ENXIO);

	if (rid != 0 || res == NULL)
		return (ENXIO);

	start = rman_get_start(res);
	if (startp != NULL)
		*startp = start;
	if (countp != NULL)
		*countp = rman_get_end(res) - start + 1;
	return (0);
}

int
puc_bus_setup_intr(device_t dev, device_t child, struct resource *res,
    int flags, driver_filter_t *filt, void (*ihand)(void *), void *arg, void **cookiep)
{
	struct puc_port *port;
	struct puc_softc *sc;
	device_t originator;
	int i, isrc, serdev;

	sc = device_get_softc(dev);

	/* Get our immediate child. */
	originator = child;
	while (child != NULL && device_get_parent(child) != dev)
		child = device_get_parent(child);
	if (child == NULL)
		return (EINVAL);

	port = device_get_ivars(child);
	KASSERT(port != NULL, ("%s %d", __func__, __LINE__));

	if (cookiep == NULL || res != port->p_ires)
		return (EINVAL);
	/* We demand that serdev devices use filter_only interrupts. */
	if (port->p_type == PUC_TYPE_SERIAL && ihand != NULL)
		return (ENXIO);
	if (rman_get_device(port->p_ires) != originator)
		return (ENXIO);

	/*
	 * Have non-serdev ports handled by the bus implementation. It
	 * supports multiple handlers for a single interrupt as it is,
	 * so we wouldn't add value if we did it ourselves.
	 */
	serdev = 0;
	if (port->p_type == PUC_TYPE_SERIAL) {
		i = 0, isrc = SER_INT_OVERRUN;
		while (i < PUC_ISRCCNT) {
			port->p_ihsrc[i] = SERDEV_IHAND(originator, isrc);
			if (port->p_ihsrc[i] != NULL)
				serdev = 1;
			i++, isrc <<= 1;
		}
	}
	if (!serdev)
		return (BUS_SETUP_INTR(device_get_parent(dev), originator,
		    sc->sc_ires, flags, filt, ihand, arg, cookiep));

	sc->sc_serdevs |= 1UL << (port->p_nr - 1);

	port->p_hasintr = 1;
	port->p_iharg = arg;

	*cookiep = port;
	return (0);
}

int
puc_bus_teardown_intr(device_t dev, device_t child, struct resource *res,
    void *cookie)
{
	struct puc_port *port;
	struct puc_softc *sc;
	device_t originator;
	int i;

	sc = device_get_softc(dev);

	/* Get our immediate child. */
	originator = child;
	while (child != NULL && device_get_parent(child) != dev)
		child = device_get_parent(child);
	if (child == NULL)
		return (EINVAL);

	port = device_get_ivars(child);
	KASSERT(port != NULL, ("%s %d", __func__, __LINE__));

	if (res != port->p_ires)
		return (EINVAL);
	if (rman_get_device(port->p_ires) != originator)
		return (ENXIO);

	if (!port->p_hasintr)
		return (BUS_TEARDOWN_INTR(device_get_parent(dev), originator,
		    sc->sc_ires, cookie));

	if (cookie != port)
		return (EINVAL);

	port->p_hasintr = 0;
	port->p_iharg = NULL;

	for (i = 0; i < PUC_ISRCCNT; i++)
		port->p_ihsrc[i] = NULL;

	return (0);
}

int
puc_bus_read_ivar(device_t dev, device_t child, int index, uintptr_t *result)
{
	struct puc_port *port;

	/* Get our immediate child. */
	while (child != NULL && device_get_parent(child) != dev)
		child = device_get_parent(child);
	if (child == NULL)
		return (EINVAL);

	port = device_get_ivars(child);
	KASSERT(port != NULL, ("%s %d", __func__, __LINE__));

	if (result == NULL)
		return (EINVAL);

	switch(index) {
	case PUC_IVAR_CLOCK:
		*result = port->p_rclk;
		break;
	case PUC_IVAR_TYPE:
		*result = port->p_type;
		break;
	default:
		return (ENOENT);
	}
	return (0);
}

int
puc_bus_print_child(device_t dev, device_t child)
{
	struct puc_port *port;
	int retval;

	port = device_get_ivars(child);
	retval = 0;

	retval += bus_print_child_header(dev, child);
	retval += printf(" at port %d", port->p_nr);
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

int
puc_bus_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	struct puc_port *port;

	port = device_get_ivars(child);
	snprintf(buf, buflen, "port=%d", port->p_nr);
	return (0);
}

int
puc_bus_child_pnpinfo_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	struct puc_port *port;

	port = device_get_ivars(child);
	snprintf(buf, buflen, "type=%d", port->p_type);
	return (0);
}
