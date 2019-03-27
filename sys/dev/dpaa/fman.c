/*-
 * Copyright (c) 2011-2012 Semihalf.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/malloc.h>

#include <dev/fdt/simplebus.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include "opt_platform.h"

#include <contrib/ncsw/inc/Peripherals/fm_ext.h>
#include <contrib/ncsw/inc/Peripherals/fm_muram_ext.h>
#include <contrib/ncsw/inc/ncsw_ext.h>
#include <contrib/ncsw/integrations/fman_ucode.h>

#include "fman.h"


static MALLOC_DEFINE(M_FMAN, "fman", "fman devices information");

/**
 * @group FMan private defines.
 * @{
 */
enum fman_irq_enum {
	FMAN_IRQ_NUM		= 0,
	FMAN_ERR_IRQ_NUM	= 1
};

enum fman_mu_ram_map {
	FMAN_MURAM_OFF		= 0x0,
	FMAN_MURAM_SIZE		= 0x28000
};

struct fman_config {
	device_t fman_device;
	uintptr_t mem_base_addr;
	uintptr_t irq_num;
	uintptr_t err_irq_num;
	uint8_t fm_id;
	t_FmExceptionsCallback *exception_callback;
	t_FmBusErrorCallback *bus_error_callback;
};

/**
 * @group FMan private methods/members.
 * @{
 */
/**
 * Frame Manager firmware.
 * We use the same firmware for both P3041 and P2041 devices.
 */
const uint32_t fman_firmware[] = FMAN_UC_IMG;
const uint32_t fman_firmware_size = sizeof(fman_firmware);

int
fman_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res)
{
	struct fman_softc *sc;
	bus_space_tag_t bt;
	bus_space_handle_t bh;
	int i, rv;

	sc = device_get_softc(bus);
	if (type != SYS_RES_IRQ) {
		for (i = 0; i < sc->sc_base.nranges; i++) {
			if (rman_is_region_manager(res, &sc->rman) != 0) {
				bt = rman_get_bustag(sc->mem_res);
				rv = bus_space_subregion(bt,
				    rman_get_bushandle(sc->mem_res),
				    rman_get_start(res) -
				    rman_get_start(sc->mem_res),
				    rman_get_size(res), &bh);
				if (rv != 0)
					return (rv);
				rman_set_bustag(res, bt);
				rman_set_bushandle(res, bh);
				return (rman_activate_resource(res));
			}
		}
		return (EINVAL);
	}
	return (bus_generic_activate_resource(bus, child, type, rid, res));
}

int
fman_release_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res)
{
	struct fman_softc *sc;
	struct resource_list *rl;
	struct resource_list_entry *rle;
	int passthrough, rv;

	passthrough = (device_get_parent(child) != bus);
	rl = BUS_GET_RESOURCE_LIST(bus, child);
	sc = device_get_softc(bus);
	if (type != SYS_RES_IRQ) {
		if ((rman_get_flags(res) & RF_ACTIVE) != 0 ){
			rv = bus_deactivate_resource(child, type, rid, res);
			if (rv != 0)
				return (rv);
		}
		rv = rman_release_resource(res);
		if (rv != 0)
			return (rv);
		if (!passthrough) {
			rle = resource_list_find(rl, type, rid);
			KASSERT(rle != NULL,
			    ("%s: resource entry not found!", __func__));
			KASSERT(rle->res != NULL,
			   ("%s: resource entry is not busy", __func__));
			rle->res = NULL;
		}
		return (0);
	}
	return (resource_list_release(rl, bus, child, type, rid, res));
}

struct resource *
fman_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct fman_softc *sc;
	struct resource_list *rl;
	struct resource_list_entry *rle = NULL;
	struct resource *res;
	int i, isdefault, passthrough;

	isdefault = RMAN_IS_DEFAULT_RANGE(start, end);
	passthrough = (device_get_parent(child) != bus);
	sc = device_get_softc(bus);
	rl = BUS_GET_RESOURCE_LIST(bus, child);
	switch (type) {
	case SYS_RES_MEMORY:
		KASSERT(!(isdefault && passthrough),
		    ("%s: passthrough of default allocation", __func__));
		if (!passthrough) {
			rle = resource_list_find(rl, type, *rid);
			if (rle == NULL)
				return (NULL);
			KASSERT(rle->res == NULL,
			    ("%s: resource entry is busy", __func__));
			if (isdefault) {
				start = rle->start;
				count = ulmax(count, rle->count);
				end = ulmax(rle->end, start + count - 1);
			}
		}

		res = NULL;
		/* Map fman ranges to nexus ranges. */
		for (i = 0; i < sc->sc_base.nranges; i++) {
			if (start >= sc->sc_base.ranges[i].bus && end <
			    sc->sc_base.ranges[i].bus + sc->sc_base.ranges[i].size) {
				start += rman_get_start(sc->mem_res);
				end += rman_get_start(sc->mem_res);
				res = rman_reserve_resource(&sc->rman, start,
				    end, count, flags & ~RF_ACTIVE, child);
				if (res == NULL)
					return (NULL);
				rman_set_rid(res, *rid);
				if ((flags & RF_ACTIVE) != 0 && bus_activate_resource(
				    child, type, *rid, res) != 0) {
					rman_release_resource(res);
					return (NULL);
				}
				break;
			}
		}
		if (!passthrough)
			rle->res = res;
		return (res);
	case SYS_RES_IRQ:
		return (resource_list_alloc(rl, bus, child, type, rid, start,
		    end, count, flags));
	}
	return (NULL);
}

static int
fman_fill_ranges(phandle_t node, struct simplebus_softc *sc)
{
	int host_address_cells;
	cell_t *base_ranges;
	ssize_t nbase_ranges;
	int err;
	int i, j, k;

	err = OF_searchencprop(OF_parent(node), "#address-cells",
	    &host_address_cells, sizeof(host_address_cells));
	if (err <= 0)
		return (-1);

	nbase_ranges = OF_getproplen(node, "ranges");
	if (nbase_ranges < 0)
		return (-1);
	sc->nranges = nbase_ranges / sizeof(cell_t) /
	    (sc->acells + host_address_cells + sc->scells);
	if (sc->nranges == 0)
		return (0);

	sc->ranges = malloc(sc->nranges * sizeof(sc->ranges[0]),
	    M_DEVBUF, M_WAITOK);
	base_ranges = malloc(nbase_ranges, M_DEVBUF, M_WAITOK);
	OF_getencprop(node, "ranges", base_ranges, nbase_ranges);

	for (i = 0, j = 0; i < sc->nranges; i++) {
		sc->ranges[i].bus = 0;
		for (k = 0; k < sc->acells; k++) {
			sc->ranges[i].bus <<= 32;
			sc->ranges[i].bus |= base_ranges[j++];
		}
		sc->ranges[i].host = 0;
		for (k = 0; k < host_address_cells; k++) {
			sc->ranges[i].host <<= 32;
			sc->ranges[i].host |= base_ranges[j++];
		}
		sc->ranges[i].size = 0;
		for (k = 0; k < sc->scells; k++) {
			sc->ranges[i].size <<= 32;
			sc->ranges[i].size |= base_ranges[j++];
		}
	}

	free(base_ranges, M_DEVBUF);
	return (sc->nranges);
}

static t_Handle
fman_init(struct fman_softc *sc, struct fman_config *cfg)
{
	phandle_t node;
	t_FmParams fm_params;
	t_Handle muram_handle, fm_handle;
	t_Error error;
	t_FmRevisionInfo revision_info;
	uint16_t clock;
	uint32_t tmp, mod;

	/* MURAM configuration */
	muram_handle = FM_MURAM_ConfigAndInit(cfg->mem_base_addr +
	    FMAN_MURAM_OFF, FMAN_MURAM_SIZE);
	if (muram_handle == NULL) {
		device_printf(cfg->fman_device, "couldn't init FM MURAM module"
		    "\n");
		return (NULL);
	}
	sc->muram_handle = muram_handle;

	/* Fill in FM configuration */
	fm_params.fmId = cfg->fm_id;
	/* XXX we support only one partition thus each fman has master id */
	fm_params.guestId = NCSW_MASTER_ID;

	fm_params.baseAddr = cfg->mem_base_addr;
	fm_params.h_FmMuram = muram_handle;

	/* Get FMan clock in Hz */
	if ((tmp = fman_get_clock(sc)) == 0)
		return (NULL);

	/* Convert FMan clock to MHz */
	clock = (uint16_t)(tmp / 1000000);
	mod = tmp % 1000000;

	if (mod >= 500000)
		++clock;

	fm_params.fmClkFreq = clock;
	fm_params.f_Exception = cfg->exception_callback;
	fm_params.f_BusError = cfg->bus_error_callback;
	fm_params.h_App = cfg->fman_device;
	fm_params.irq = cfg->irq_num;
	fm_params.errIrq = cfg->err_irq_num;

	fm_params.firmware.size = fman_firmware_size;
	fm_params.firmware.p_Code = (uint32_t*)fman_firmware;

	fm_handle = FM_Config(&fm_params);
	if (fm_handle == NULL) {
		device_printf(cfg->fman_device, "couldn't configure FM "
		    "module\n");
		goto err;
	}

	FM_ConfigResetOnInit(fm_handle, TRUE);

	error = FM_Init(fm_handle);
	if (error != E_OK) {
		device_printf(cfg->fman_device, "couldn't init FM module\n");
		goto err2;
	}

	error = FM_GetRevision(fm_handle, &revision_info);
	if (error != E_OK) {
		device_printf(cfg->fman_device, "couldn't get FM revision\n");
		goto err2;
	}

	device_printf(cfg->fman_device, "Hardware version: %d.%d.\n",
	    revision_info.majorRev, revision_info.minorRev);

	/* Initialize the simplebus part of things */
	simplebus_init(sc->sc_base.dev, 0);

	node = ofw_bus_get_node(sc->sc_base.dev);
	fman_fill_ranges(node, &sc->sc_base);
	sc->rman.rm_type = RMAN_ARRAY;
	sc->rman.rm_descr = "FMan range";
	rman_init_from_resource(&sc->rman, sc->mem_res);
	for (node = OF_child(node); node > 0; node = OF_peer(node)) {
		simplebus_add_device(sc->sc_base.dev, node, 0, NULL, -1, NULL);
	}

	return (fm_handle);

err2:
	FM_Free(fm_handle);
err:
	FM_MURAM_Free(muram_handle);
	return (NULL);
}

static void
fman_exception_callback(t_Handle app_handle, e_FmExceptions exception)
{
	struct fman_softc *sc;

	sc = app_handle;
	device_printf(sc->sc_base.dev, "FMan exception occurred.\n");
}

static void
fman_error_callback(t_Handle app_handle, e_FmPortType port_type,
    uint8_t port_id, uint64_t addr, uint8_t tnum, uint16_t liodn)
{
	struct fman_softc *sc;

	sc = app_handle;
	device_printf(sc->sc_base.dev, "FMan error occurred.\n");
}
/** @} */


/**
 * @group FMan driver interface.
 * @{
 */

int
fman_get_handle(device_t dev, t_Handle *fmh)
{
	struct fman_softc *sc = device_get_softc(dev);

	*fmh = sc->fm_handle;

	return (0);
}

int
fman_get_muram_handle(device_t dev, t_Handle *muramh)
{
	struct fman_softc *sc = device_get_softc(dev);

	*muramh = sc->muram_handle;

	return (0);
}

int
fman_get_bushandle(device_t dev, vm_offset_t *fm_base)
{
	struct fman_softc *sc = device_get_softc(dev);

	*fm_base = rman_get_bushandle(sc->mem_res);

	return (0);
}

int
fman_attach(device_t dev)
{
	struct fman_softc *sc;
	struct fman_config cfg;
	pcell_t qchan_range[2];
	phandle_t node;

	sc = device_get_softc(dev);
	sc->sc_base.dev = dev;

	/* Check if MallocSmart allocator is ready */
	if (XX_MallocSmartInit() != E_OK) {
		device_printf(dev, "could not initialize smart allocator.\n");
		return (ENXIO);
	}

	node = ofw_bus_get_node(dev);
	if (OF_getencprop(node, "fsl,qman-channel-range", qchan_range,
	    sizeof(qchan_range)) <= 0) {
		device_printf(dev, "Missing QMan channel range property!\n");
		return (ENXIO);
	}
	sc->qman_chan_base = qchan_range[0];
	sc->qman_chan_count = qchan_range[1];
	sc->mem_rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->mem_rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (!sc->mem_res) {
		device_printf(dev, "could not allocate memory.\n");
		return (ENXIO);
	}

	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
	    RF_ACTIVE);
	if (!sc->irq_res) {
		device_printf(dev, "could not allocate interrupt.\n");
		goto err;
	}

	/*
	 * XXX: Fix FMan interrupt. This is workaround for the issue with
	 * interrupts directed to multiple CPUs by the interrupts subsystem.
	 * Workaround is to bind the interrupt to only one CPU0.
	 */
	XX_FmanFixIntr(rman_get_start(sc->irq_res));

	sc->err_irq_rid = 1;
	sc->err_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->err_irq_rid, RF_ACTIVE | RF_SHAREABLE);
	if (!sc->err_irq_res) {
		device_printf(dev, "could not allocate error interrupt.\n");
		goto err;
	}

	/* Set FMan configuration */
	cfg.fman_device = dev;
	cfg.fm_id = device_get_unit(dev);
	cfg.mem_base_addr = rman_get_bushandle(sc->mem_res);
	cfg.irq_num = (uintptr_t)sc->irq_res;
	cfg.err_irq_num = (uintptr_t)sc->err_irq_res;
	cfg.exception_callback = fman_exception_callback;
	cfg.bus_error_callback = fman_error_callback;

	sc->fm_handle = fman_init(sc, &cfg);
	if (sc->fm_handle == NULL) {
		device_printf(dev, "could not be configured\n");
		return (ENXIO);
	}

	return (bus_generic_attach(dev));

err:
	fman_detach(dev);
	return (ENXIO);
}

int
fman_detach(device_t dev)
{
	struct fman_softc *sc;

	sc = device_get_softc(dev);

	if (sc->muram_handle) {
		FM_MURAM_Free(sc->muram_handle);
	}

	if (sc->fm_handle) {
		FM_Free(sc->fm_handle);
	}

	if (sc->mem_res) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid,
		    sc->mem_res);
	}

	if (sc->irq_res) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
	}

	if (sc->irq_res) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->err_irq_rid,
		    sc->err_irq_res);
	}

	return (0);
}

int
fman_suspend(device_t dev)
{

	return (0);
}

int
fman_resume_dev(device_t dev)
{

	return (0);
}

int
fman_shutdown(device_t dev)
{

	return (0);
}

int
fman_qman_channel_id(device_t dev, int port)
{
	struct fman_softc *sc;
	int qman_port_id[] = {0x31, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e,
	    0x2f, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < sc->qman_chan_count; i++) {
		if (qman_port_id[i] == port)
			return (sc->qman_chan_base + i);
	}

	return (0);
}

/** @} */
