/*-
 * Copyright (c) 2013 Oleksandr Tymoshenko <gonzo@freebsd.org>
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

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_util.h>

#define	USB_DEBUG_VAR usbssdebug

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#include <dev/usb/controller/musb_otg.h>
#include <dev/usb/usb_debug.h>

#include <sys/rman.h>

#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_scm.h>
#include <arm/ti/am335x/am335x_scm.h>

#define USBCTRL_REV		0x00
#define USBCTRL_CTRL		0x14
#define USBCTRL_STAT		0x18
#define USBCTRL_IRQ_STAT0	0x30
#define		IRQ_STAT0_RXSHIFT	16
#define		IRQ_STAT0_TXSHIFT	0
#define USBCTRL_IRQ_STAT1	0x34
#define 	IRQ_STAT1_DRVVBUS	(1 << 8)
#define USBCTRL_INTEN_SET0	0x38
#define USBCTRL_INTEN_SET1	0x3C
#define 	USBCTRL_INTEN_USB_ALL	0x1ff
#define 	USBCTRL_INTEN_USB_SOF	(1 << 3)
#define USBCTRL_INTEN_CLR0	0x40
#define USBCTRL_INTEN_CLR1	0x44
#define USBCTRL_UTMI		0xE0
#define		USBCTRL_UTMI_FSDATAEXT		(1 << 1)
#define USBCTRL_MODE		0xE8
#define 	USBCTRL_MODE_IDDIG		(1 << 8)
#define 	USBCTRL_MODE_IDDIGMUX		(1 << 7)

/* USBSS resource + 2 MUSB ports */

#define RES_USBCORE	0
#define RES_USBCTRL	1

#define	USB_WRITE4(sc, idx, reg, val)	do {		\
	bus_write_4((sc)->sc_mem_res[idx], (reg), (val));	\
} while (0)

#define	USB_READ4(sc, idx, reg) bus_read_4((sc)->sc_mem_res[idx], (reg))

#define	USBCTRL_WRITE4(sc, reg, val)	\
    USB_WRITE4((sc), RES_USBCTRL, (reg), (val))
#define	USBCTRL_READ4(sc, reg)		\
    USB_READ4((sc), RES_USBCTRL, (reg))

static struct resource_spec am335x_musbotg_mem_spec[] = {
	{ SYS_RES_MEMORY,   0,  RF_ACTIVE },
	{ SYS_RES_MEMORY,   1,  RF_ACTIVE },
	{ -1,               0,  0 }
};

#ifdef USB_DEBUG
static int usbssdebug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, am335x_usbss, CTLFLAG_RW, 0, "AM335x USBSS");
SYSCTL_INT(_hw_usb_am335x_usbss, OID_AUTO, debug, CTLFLAG_RW,
    &usbssdebug, 0, "Debug level");
#endif

static device_probe_t musbotg_probe;
static device_attach_t musbotg_attach;
static device_detach_t musbotg_detach;

struct musbotg_super_softc {
	struct musbotg_softc	sc_otg;
	struct resource		*sc_mem_res[2];
	int			sc_irq_rid;
};

static void
musbotg_vbus_poll(struct musbotg_super_softc *sc)
{
	uint32_t stat;

	if (sc->sc_otg.sc_mode == MUSB2_DEVICE_MODE)
		musbotg_vbus_interrupt(&sc->sc_otg, 1);
	else {
		stat = USBCTRL_READ4(sc, USBCTRL_STAT);
		musbotg_vbus_interrupt(&sc->sc_otg, stat & 1);
	}
}

/*
 * Arg to musbotg_clocks_on and musbot_clocks_off is
 * a uint32_t * pointing to the SCM register offset.
 */
static uint32_t USB_CTRL[] = {SCM_USB_CTRL0, SCM_USB_CTRL1};

static void
musbotg_clocks_on(void *arg)
{
	struct musbotg_softc *sc;
	uint32_t c, reg;

	sc = arg;
        reg = USB_CTRL[sc->sc_id];

	ti_scm_reg_read_4(reg, &c);
	c &= ~3; /* Enable power */
	c |= 1 << 19; /* VBUS detect enable */
	c |= 1 << 20; /* Session end enable */
	ti_scm_reg_write_4(reg, c);
}

static void
musbotg_clocks_off(void *arg)
{
	struct musbotg_softc *sc;
	uint32_t c, reg;

	sc = arg;
        reg = USB_CTRL[sc->sc_id];

	/* Disable power to PHY */
	ti_scm_reg_read_4(reg, &c);
	ti_scm_reg_write_4(reg, c | 3);
}

static void
musbotg_ep_int_set(struct musbotg_softc *sc, int ep, int on)
{
	struct musbotg_super_softc *ssc = sc->sc_platform_data;
	uint32_t epmask;

	epmask = ((1 << ep) << IRQ_STAT0_RXSHIFT);
	epmask |= ((1 << ep) << IRQ_STAT0_TXSHIFT);
	if (on)
		USBCTRL_WRITE4(ssc, USBCTRL_INTEN_SET0, epmask);
	else
		USBCTRL_WRITE4(ssc, USBCTRL_INTEN_CLR0, epmask);
}

static void
musbotg_wrapper_interrupt(void *arg)
{
	struct musbotg_softc *sc = arg;
	struct musbotg_super_softc *ssc = sc->sc_platform_data;
	uint32_t stat, stat0, stat1;

	stat = USBCTRL_READ4(ssc, USBCTRL_STAT);
	stat0 = USBCTRL_READ4(ssc, USBCTRL_IRQ_STAT0);
	stat1 = USBCTRL_READ4(ssc, USBCTRL_IRQ_STAT1);
	if (stat0)
		USBCTRL_WRITE4(ssc, USBCTRL_IRQ_STAT0, stat0);
	if (stat1)
		USBCTRL_WRITE4(ssc, USBCTRL_IRQ_STAT1, stat1);

	DPRINTFN(4, "port%d: stat0=%08x stat1=%08x, stat=%08x\n",
	    sc->sc_id, stat0, stat1, stat);

	if (stat1 & IRQ_STAT1_DRVVBUS)
		musbotg_vbus_interrupt(sc, stat & 1);

	musbotg_interrupt(arg, ((stat0 >> 16) & 0xffff),
	    stat0 & 0xffff, stat1 & 0xff);
}

static int
musbotg_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ti,musb-am33xx"))
		return (ENXIO);

	device_set_desc(dev, "TI AM33xx integrated USB OTG controller");

	return (BUS_PROBE_DEFAULT);
}

static int
musbotg_attach(device_t dev)
{
	struct musbotg_super_softc *sc = device_get_softc(dev);
	char mode[16];
	int err;
	uint32_t reg;

	sc->sc_otg.sc_id = device_get_unit(dev);

	/* Request the memory resources */
	err = bus_alloc_resources(dev, am335x_musbotg_mem_spec,
		sc->sc_mem_res);
	if (err) {
		device_printf(dev,
		    "Error: could not allocate mem resources\n");
		return (ENXIO);
	}

	/* Request the IRQ resources */
	sc->sc_otg.sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->sc_irq_rid, RF_ACTIVE);
	if (sc->sc_otg.sc_irq_res == NULL) {
		device_printf(dev,
		    "Error: could not allocate irq resources\n");
		return (ENXIO);
	}

	/* setup MUSB OTG USB controller interface softc */
	sc->sc_otg.sc_clocks_on = &musbotg_clocks_on;
	sc->sc_otg.sc_clocks_off = &musbotg_clocks_off;
	sc->sc_otg.sc_clocks_arg = &sc->sc_otg;

	sc->sc_otg.sc_ep_int_set = musbotg_ep_int_set;

	/* initialise some bus fields */
	sc->sc_otg.sc_bus.parent = dev;
	sc->sc_otg.sc_bus.devices = sc->sc_otg.sc_devices;
	sc->sc_otg.sc_bus.devices_max = MUSB2_MAX_DEVICES;
	sc->sc_otg.sc_bus.dma_bits = 32;

	/* get all DMA memory */
	if (usb_bus_mem_alloc_all(&sc->sc_otg.sc_bus,
	    USB_GET_DMA_TAG(dev), NULL)) {
		device_printf(dev,
		    "Failed allocate bus mem for musb\n");
		return (ENOMEM);
	}
	sc->sc_otg.sc_io_res = sc->sc_mem_res[RES_USBCORE];
	sc->sc_otg.sc_io_tag =
	    rman_get_bustag(sc->sc_otg.sc_io_res);
	sc->sc_otg.sc_io_hdl =
	    rman_get_bushandle(sc->sc_otg.sc_io_res);
	sc->sc_otg.sc_io_size =
	    rman_get_size(sc->sc_otg.sc_io_res);

	sc->sc_otg.sc_bus.bdev = device_add_child(dev, "usbus", -1);
	if (!(sc->sc_otg.sc_bus.bdev)) {
		device_printf(dev, "No busdev for musb\n");
		goto error;
	}
	device_set_ivars(sc->sc_otg.sc_bus.bdev,
	    &sc->sc_otg.sc_bus);

	err = bus_setup_intr(dev, sc->sc_otg.sc_irq_res,
	    INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, (driver_intr_t *)musbotg_wrapper_interrupt,
	    &sc->sc_otg, &sc->sc_otg.sc_intr_hdl);
	if (err) {
		sc->sc_otg.sc_intr_hdl = NULL;
		device_printf(dev,
		    "Failed to setup interrupt for musb\n");
		goto error;
	}

	sc->sc_otg.sc_platform_data = sc;
	if (OF_getprop(ofw_bus_get_node(dev), "dr_mode", mode,
	    sizeof(mode)) > 0) {
		if (strcasecmp(mode, "host") == 0)
			sc->sc_otg.sc_mode = MUSB2_HOST_MODE;
		else
			sc->sc_otg.sc_mode = MUSB2_DEVICE_MODE;
	} else {
		/* Beaglebone defaults: USB0 device, USB1 HOST. */
		if (sc->sc_otg.sc_id == 0)
			sc->sc_otg.sc_mode = MUSB2_DEVICE_MODE;
		else
			sc->sc_otg.sc_mode = MUSB2_HOST_MODE;
	}

	/*
	 * software-controlled function
	 */

	if (sc->sc_otg.sc_mode == MUSB2_HOST_MODE) {
		reg = USBCTRL_READ4(sc, USBCTRL_MODE);
		reg |= USBCTRL_MODE_IDDIGMUX;
		reg &= ~USBCTRL_MODE_IDDIG;
		USBCTRL_WRITE4(sc, USBCTRL_MODE, reg);
		USBCTRL_WRITE4(sc, USBCTRL_UTMI,
		    USBCTRL_UTMI_FSDATAEXT);
	} else {
		reg = USBCTRL_READ4(sc, USBCTRL_MODE);
		reg |= USBCTRL_MODE_IDDIGMUX;
		reg |= USBCTRL_MODE_IDDIG;
		USBCTRL_WRITE4(sc, USBCTRL_MODE, reg);
	}

	reg = USBCTRL_INTEN_USB_ALL & ~USBCTRL_INTEN_USB_SOF;
	USBCTRL_WRITE4(sc, USBCTRL_INTEN_SET1, reg);
	USBCTRL_WRITE4(sc, USBCTRL_INTEN_CLR0, 0xffffffff);

	err = musbotg_init(&sc->sc_otg);
	if (!err)
		err = device_probe_and_attach(sc->sc_otg.sc_bus.bdev);

	if (err)
		goto error;

	/* poll VBUS one time */
	musbotg_vbus_poll(sc);

	return (0);

error:
	musbotg_detach(dev);
	return (ENXIO);
}

static int
musbotg_detach(device_t dev)
{
	struct musbotg_super_softc *sc = device_get_softc(dev);
	int err;

	/* during module unload there are lots of children leftover */
	device_delete_children(dev);

	if (sc->sc_otg.sc_irq_res && sc->sc_otg.sc_intr_hdl) {
		/*
		 * only call musbotg_uninit() after musbotg_init()
		 */
		musbotg_uninit(&sc->sc_otg);

		err = bus_teardown_intr(dev, sc->sc_otg.sc_irq_res,
		    sc->sc_otg.sc_intr_hdl);
		sc->sc_otg.sc_intr_hdl = NULL;
	}

	usb_bus_mem_free_all(&sc->sc_otg.sc_bus, NULL);

	/* Free resources if any */
	if (sc->sc_mem_res[0])
		bus_release_resources(dev, am335x_musbotg_mem_spec,
		    sc->sc_mem_res);

	if (sc->sc_otg.sc_irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid,
		    sc->sc_otg.sc_irq_res);

	return (0);
}

static device_method_t musbotg_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, musbotg_probe),
	DEVMETHOD(device_attach, musbotg_attach),
	DEVMETHOD(device_detach, musbotg_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	DEVMETHOD_END
};

static driver_t musbotg_driver = {
	.name = "musbotg",
	.methods = musbotg_methods,
	.size = sizeof(struct musbotg_super_softc),
};

static devclass_t musbotg_devclass;

DRIVER_MODULE(musbotg, usbss, musbotg_driver, musbotg_devclass, 0, 0);
MODULE_DEPEND(musbotg, usbss, 1, 1, 1);
