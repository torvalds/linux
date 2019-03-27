/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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

#include "opt_bus.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#include <dev/usb/controller/xhci.h>
#include <dev/usb/controller/xhcireg.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <arm/samsung/exynos/exynos5_common.h>

#include "opt_platform.h"

#define	GSNPSID				0x20
#define	 GSNPSID_MASK			0xffff0000
#define	 REVISION_MASK			0xffff
#define	GCTL				0x10
#define	 GCTL_PWRDNSCALE(n)		((n) << 19)
#define	 GCTL_U2RSTECN			(1 << 16)
#define	 GCTL_CLK_BUS			(0)
#define	 GCTL_CLK_PIPE			(1)
#define	 GCTL_CLK_PIPEHALF		(2)
#define	 GCTL_CLK_M			(3)
#define	 GCTL_CLK_S			(6)
#define	 GCTL_PRTCAP(n)			(((n) & (3 << 12)) >> 12)
#define	 GCTL_PRTCAPDIR(n)		((n) << 12)
#define	 GCTL_PRTCAP_HOST		1
#define	 GCTL_PRTCAP_DEVICE		2
#define	 GCTL_PRTCAP_OTG		3
#define	 GCTL_CORESOFTRESET		(1 << 11)
#define	 GCTL_SCALEDOWN_MASK		3
#define	 GCTL_SCALEDOWN_SHIFT		4
#define	 GCTL_DISSCRAMBLE		(1 << 3)
#define	 GCTL_DSBLCLKGTNG		(1 << 0)
#define	GHWPARAMS1			0x3c
#define	 GHWPARAMS1_EN_PWROPT(n)	(((n) & (3 << 24)) >> 24)
#define	 GHWPARAMS1_EN_PWROPT_NO	0
#define	 GHWPARAMS1_EN_PWROPT_CLK	1
#define	GUSB2PHYCFG(n)			(0x100 + (n * 0x04))
#define	 GUSB2PHYCFG_PHYSOFTRST		(1 << 31)
#define	 GUSB2PHYCFG_SUSPHY		(1 << 6)
#define	GUSB3PIPECTL(n)			(0x1c0 + (n * 0x04))
#define	 GUSB3PIPECTL_PHYSOFTRST	(1 << 31)
#define	 GUSB3PIPECTL_SUSPHY		(1 << 17)

/* Forward declarations */
static device_attach_t exynos_xhci_attach;
static device_detach_t exynos_xhci_detach;
static device_probe_t exynos_xhci_probe;

struct exynos_xhci_softc {
	device_t		dev;
	struct xhci_softc	base;
	struct resource		*res[3];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
};

static struct resource_spec exynos_xhci_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static device_method_t xhci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, exynos_xhci_probe),
	DEVMETHOD(device_attach, exynos_xhci_attach),
	DEVMETHOD(device_detach, exynos_xhci_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	DEVMETHOD_END
};

/* kobj_class definition */
static driver_t xhci_driver = {
	"xhci",
	xhci_methods,
	sizeof(struct xhci_softc)
};

static devclass_t xhci_devclass;

DRIVER_MODULE(xhci, simplebus, xhci_driver, xhci_devclass, 0, 0);
MODULE_DEPEND(xhci, usb, 1, 1, 1);

/*
 * Public methods
 */
static int
exynos_xhci_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "samsung,exynos5250-dwusb3") == 0)
		return (ENXIO);

	device_set_desc(dev, "Exynos USB 3.0 controller");
	return (BUS_PROBE_DEFAULT);
}

static int
dwc3_init(struct exynos_xhci_softc *esc)
{
	int hwparams1;
	int rev;
	int reg;

	rev = READ4(esc, GSNPSID);
	if ((rev & GSNPSID_MASK) != 0x55330000) {
		printf("It is not DWC3 controller\n");
		return (-1);
	}

	/* Reset controller */
	WRITE4(esc, GCTL, GCTL_CORESOFTRESET);
	WRITE4(esc, GUSB3PIPECTL(0), GUSB3PIPECTL_PHYSOFTRST);
	WRITE4(esc, GUSB2PHYCFG(0), GUSB2PHYCFG_PHYSOFTRST);

	DELAY(100000);

	reg = READ4(esc, GUSB3PIPECTL(0));
	reg &= ~(GUSB3PIPECTL_PHYSOFTRST);
	WRITE4(esc, GUSB3PIPECTL(0), reg);

	reg = READ4(esc, GUSB2PHYCFG(0));
	reg &= ~(GUSB2PHYCFG_PHYSOFTRST);
	WRITE4(esc, GUSB2PHYCFG(0), reg);

	reg = READ4(esc, GCTL);
	reg &= ~GCTL_CORESOFTRESET;
	WRITE4(esc, GCTL, reg);

	hwparams1 = READ4(esc, GHWPARAMS1);

	reg = READ4(esc, GCTL);
	reg &= ~(GCTL_SCALEDOWN_MASK << GCTL_SCALEDOWN_SHIFT);
	reg &= ~(GCTL_DISSCRAMBLE);

	if (GHWPARAMS1_EN_PWROPT(hwparams1) == \
	    GHWPARAMS1_EN_PWROPT_CLK)
		reg &= ~(GCTL_DSBLCLKGTNG);

	if ((rev & REVISION_MASK) < 0x190a)
		reg |= (GCTL_U2RSTECN);
	WRITE4(esc, GCTL, reg);

	/* Set host mode */
	reg = READ4(esc, GCTL);
	reg &= ~(GCTL_PRTCAPDIR(GCTL_PRTCAP_OTG));
	reg |= GCTL_PRTCAPDIR(GCTL_PRTCAP_HOST);
	WRITE4(esc, GCTL, reg);

	return (0);
}

static int
exynos_xhci_attach(device_t dev)
{
	struct exynos_xhci_softc *esc = device_get_softc(dev);
	bus_space_handle_t bsh;
	int err;

	esc->dev = dev;

	if (bus_alloc_resources(dev, exynos_xhci_spec, esc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* XHCI registers */
	esc->base.sc_io_tag = rman_get_bustag(esc->res[0]);
	bsh = rman_get_bushandle(esc->res[0]);
	esc->base.sc_io_size = rman_get_size(esc->res[0]);

	/* DWC3 ctrl registers */
	esc->bst = rman_get_bustag(esc->res[1]);
	esc->bsh = rman_get_bushandle(esc->res[1]);

	/*
	 * Set handle to USB related registers subregion used by
	 * generic XHCI driver.
	 */
	err = bus_space_subregion(esc->base.sc_io_tag, bsh, 0x0,
	    esc->base.sc_io_size, &esc->base.sc_io_hdl);
	if (err != 0) {
		device_printf(dev, "Subregion failed\n");
		bus_release_resources(dev, exynos_xhci_spec, esc->res);
		return (ENXIO);
	}

	if (xhci_init(&esc->base, dev, 0)) {
		device_printf(dev, "Could not initialize softc\n");
		bus_release_resources(dev, exynos_xhci_spec, esc->res);
		return (ENXIO);
	}

	/* Setup interrupt handler */
	err = bus_setup_intr(dev, esc->res[2], INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, (driver_intr_t *)xhci_interrupt, &esc->base,
	    &esc->base.sc_intr_hdl);
	if (err) {
		device_printf(dev, "Could not setup irq, %d\n", err);
		esc->base.sc_intr_hdl = NULL;
		goto error;
	}

	/* Add USB device */
	esc->base.sc_bus.bdev = device_add_child(dev, "usbus", -1);
	if (esc->base.sc_bus.bdev == NULL) {
		device_printf(dev, "Could not add USB device\n");
		goto error;
	}
	device_set_ivars(esc->base.sc_bus.bdev, &esc->base.sc_bus);
	strlcpy(esc->base.sc_vendor, "Samsung", sizeof(esc->base.sc_vendor));

	dwc3_init(esc);

	err = xhci_halt_controller(&esc->base);
	if (err == 0) {
		device_printf(dev, "Starting controller\n");
		err = xhci_start_controller(&esc->base);
	}
	if (err == 0) {
		device_printf(dev, "Controller started\n");
		err = device_probe_and_attach(esc->base.sc_bus.bdev);
	}
	if (err != 0)
		goto error;
	return (0);

error:
	exynos_xhci_detach(dev);
	return (ENXIO);
}

static int
exynos_xhci_detach(device_t dev)
{
	struct exynos_xhci_softc *esc = device_get_softc(dev);
	int err;

	/* During module unload there are lots of children leftover */
	device_delete_children(dev);

	xhci_halt_controller(&esc->base);
	
	if (esc->res[2] && esc->base.sc_intr_hdl) {
		err = bus_teardown_intr(dev, esc->res[2],
		    esc->base.sc_intr_hdl);
		if (err) {
			device_printf(dev, "Could not tear down IRQ,"
			    " %d\n", err);
			return (err);
		}
	}

	bus_release_resources(dev, exynos_xhci_spec, esc->res);

	xhci_uninit(&esc->base);
	
	return (0);
}
