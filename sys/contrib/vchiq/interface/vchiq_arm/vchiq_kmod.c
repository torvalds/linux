/*-
 * Copyright (c) 2012-2015 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/frame.h>
#include <machine/intr.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include "vchiq_arm.h"
#include "vchiq_2835.h"

#define	VCHIQ_LOCK	do {		\
	mtx_lock(&bcm_vchiq_sc->lock);	\
} while(0)

#define	VCHIQ_UNLOCK	do {		\
	mtx_unlock(&bcm_vchiq_sc->lock);	\
} while(0)

#ifdef  DEBUG
#define dprintf(fmt, args...) printf(fmt, ##args)
#else
#define dprintf(fmt, args...)
#endif

struct bcm_vchiq_softc {
	struct mtx		lock;
	struct resource *	mem_res;
	struct resource *	irq_res;
	void*			intr_hl;
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	int			regs_offset;
};

static struct bcm_vchiq_softc *bcm_vchiq_sc = NULL;

#define	BSD_DTB			1
#define	UPSTREAM_DTB		2
static struct ofw_compat_data compat_data[] = {
	{"broadcom,bcm2835-vchiq",	BSD_DTB},
	{"brcm,bcm2835-vchiq",		UPSTREAM_DTB},
	{NULL,				0}
};

#define	vchiq_read_4(reg)		\
    bus_space_read_4(bcm_vchiq_sc->bst, bcm_vchiq_sc->bsh, (reg) + \
    bcm_vchiq_sc->regs_offset)
#define	vchiq_write_4(reg, val)		\
    bus_space_write_4(bcm_vchiq_sc->bst, bcm_vchiq_sc->bsh, (reg) + \
    bcm_vchiq_sc->regs_offset, val)

/* 
 * Extern functions */
void vchiq_exit(void);
int vchiq_init(void);

extern VCHIQ_STATE_T g_state;
extern int g_cache_line_size;

static void
bcm_vchiq_intr(void *arg)
{
	VCHIQ_STATE_T *state = &g_state;
	unsigned int status;

	/* Read (and clear) the doorbell */
	status = vchiq_read_4(0x40);

	if (status & 0x4) {  /* Was the doorbell rung? */
		remote_event_pollall(state);
	}
}

void
remote_event_signal(REMOTE_EVENT_T *event)
{
	event->fired = 1;

	/* The test on the next line also ensures the write on the previous line
		has completed */
	if (event->armed) {
		/* trigger vc interrupt */
		dsb();
		vchiq_write_4(0x48, 0);
	}
}

static int
bcm_vchiq_probe(device_t dev)
{

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "BCM2835 VCHIQ");
	return (BUS_PROBE_DEFAULT);
}

static int
bcm_vchiq_attach(device_t dev)
{
	struct bcm_vchiq_softc *sc = device_get_softc(dev);
	phandle_t node;
	pcell_t cell;
	int rid = 0;

	if (bcm_vchiq_sc != NULL)
		return (EINVAL);

	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	sc->bst = rman_get_bustag(sc->mem_res);
	sc->bsh = rman_get_bushandle(sc->mem_res);

	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "could not allocate interrupt resource\n");
		return (ENXIO);
	}

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == UPSTREAM_DTB)
		sc->regs_offset = -0x40;

	node = ofw_bus_get_node(dev);
	if ((OF_getencprop(node, "cache-line-size", &cell, sizeof(cell))) > 0)
		g_cache_line_size = cell;

	vchiq_core_initialize();

	/* Setup and enable the timer */
	if (bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
			NULL, bcm_vchiq_intr, sc,
			&sc->intr_hl) != 0) {
		bus_release_resource(dev, SYS_RES_IRQ, rid,
			sc->irq_res);
		device_printf(dev, "Unable to setup the clock irq handler.\n");
		return (ENXIO);
	}

	mtx_init(&sc->lock, "vchiq", 0, MTX_DEF);
	bcm_vchiq_sc = sc;

	vchiq_init();

	bus_generic_probe(dev);
	bus_generic_attach(dev);

	return (0);
}

static int
bcm_vchiq_detach(device_t dev)
{
	struct bcm_vchiq_softc *sc = device_get_softc(dev);

	vchiq_exit();

	if (sc->intr_hl)
                bus_teardown_intr(dev, sc->irq_res, sc->intr_hl);
	bus_release_resource(dev, SYS_RES_IRQ, 0,
		sc->irq_res);
	bus_release_resource(dev, SYS_RES_MEMORY, 0,
		sc->mem_res);

	mtx_destroy(&sc->lock);

	return (0);
}


static device_method_t bcm_vchiq_methods[] = {
	DEVMETHOD(device_probe,		bcm_vchiq_probe),
	DEVMETHOD(device_attach,	bcm_vchiq_attach),
	DEVMETHOD(device_detach,	bcm_vchiq_detach),

        /* Bus interface */
        DEVMETHOD(bus_add_child,        bus_generic_add_child),

	{ 0, 0 }
};

static driver_t bcm_vchiq_driver = {
	"vchiq",
	bcm_vchiq_methods,
	sizeof(struct bcm_vchiq_softc),
};

static devclass_t bcm_vchiq_devclass;

DRIVER_MODULE(vchiq, simplebus, bcm_vchiq_driver, bcm_vchiq_devclass, 0, 0);
MODULE_VERSION(vchiq, 1);
