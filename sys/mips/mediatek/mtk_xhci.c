#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*-
 * Copyright (c) 2015 Stanislav Galabov. All rights reserved.
 * Copyright (c) 2010,2011 Aleksandr Rybalko. All rights reserved.
 * Copyright (c) 2007-2008 Hans Petter Selasky. All rights reserved.
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
#include <sys/rman.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_util.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>

#include <dev/usb/controller/xhci.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#define XHCI_HC_DEVSTR	"MTK USB 3.0 controller"

static device_probe_t	mtk_xhci_fdt_probe;
static device_attach_t	mtk_xhci_fdt_attach;
static device_detach_t	mtk_xhci_fdt_detach;

static void		mtk_xhci_fdt_init(device_t dev);

static int
mtk_xhci_fdt_probe(device_t self)
{

	if (!ofw_bus_status_okay(self))
		return (ENXIO);

	if (!ofw_bus_is_compatible(self, "mediatek,mt8173-xhci"))
		return (ENXIO);

	device_set_desc(self, XHCI_HC_DEVSTR);

	return (BUS_PROBE_DEFAULT);
}

static int
mtk_xhci_fdt_attach(device_t self)
{
	struct xhci_softc *sc = device_get_softc(self);
	int err;
	int rid;

	/* initialise some bus fields */
	sc->sc_bus.parent = self;
	sc->sc_bus.devices = sc->sc_devices;
	sc->sc_bus.devices_max = XHCI_MAX_DEVICES;

	rid = 0;
	sc->sc_io_res = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->sc_io_res) {
		device_printf(self, "Could not map memory\n");
		goto error;
	}
	sc->sc_io_tag = rman_get_bustag(sc->sc_io_res);
	sc->sc_io_hdl = rman_get_bushandle(sc->sc_io_res);
	sc->sc_io_size = rman_get_size(sc->sc_io_res);

	mtk_xhci_fdt_init(self);

	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(self, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->sc_irq_res == NULL) {
		device_printf(self, "Could not allocate irq\n");
		goto error;
	}

	sc->sc_bus.bdev = device_add_child(self, "usbus", -1);
	if (!(sc->sc_bus.bdev)) {
		device_printf(self, "Could not add USB device\n");
		goto error;
	}
	device_set_ivars(sc->sc_bus.bdev, &sc->sc_bus);
	device_set_desc(sc->sc_bus.bdev, XHCI_HC_DEVSTR);

	sprintf(sc->sc_vendor, "Mediatek");

	err = bus_setup_intr(self, sc->sc_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, (driver_intr_t *)xhci_interrupt, sc, &sc->sc_intr_hdl);
	if (err) {
		device_printf(self, "Could not setup irq, %d\n", err);
		sc->sc_intr_hdl = NULL;
		goto error;
	}

	err = xhci_init(sc, self, 1);
	if (err == 0)
		err = xhci_halt_controller(sc);
	if (err == 0)
		err = xhci_start_controller(sc);
	if (err == 0)
		err = device_probe_and_attach(sc->sc_bus.bdev);
	if (err) {
		device_printf(self, "USB init failed err=%d\n", err);
		goto error;
	}
	return (0);

error:
	mtk_xhci_fdt_detach(self);
	return (ENXIO);
}

static int
mtk_xhci_fdt_detach(device_t self)
{
	struct xhci_softc *sc = device_get_softc(self);
	int err;

	/* during module unload there are lots of children leftover */
	device_delete_children(self);

	if (sc->sc_irq_res && sc->sc_intr_hdl) {
		/*
		 * only call xhci_detach() after xhci_init()
		 */
		xhci_uninit(sc);

		err = bus_teardown_intr(self, sc->sc_irq_res, sc->sc_intr_hdl);
		if (err)
			device_printf(self, "Could not tear down irq, %d\n",
			    err);
		sc->sc_intr_hdl = NULL;
	}
	if (sc->sc_irq_res) {
		bus_release_resource(self, SYS_RES_IRQ, 0,
		    sc->sc_irq_res);
		sc->sc_irq_res = NULL;
	}
	if (sc->sc_io_res) {
		bus_release_resource(self, SYS_RES_MEMORY, 0,
		    sc->sc_io_res);
		sc->sc_io_res = NULL;
	}

	return (0);
}

static device_method_t mtk_xhci_fdt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mtk_xhci_fdt_probe),
	DEVMETHOD(device_attach,	mtk_xhci_fdt_attach),
	DEVMETHOD(device_detach,	mtk_xhci_fdt_detach),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),

	DEVMETHOD_END
};

static driver_t mtk_xhci_fdt_driver = {
	.name = "xhci",
	.methods = mtk_xhci_fdt_methods,
	.size = sizeof(struct xhci_softc),
};

static devclass_t mtk_xhci_fdt_devclass;

DRIVER_MODULE(xhci, simplebus, mtk_xhci_fdt_driver, mtk_xhci_fdt_devclass, 0,
    0);

#define	USB_HDMA_CFG		0x950
#define	USB_HDMA_CFG_MT7621_VAL	0x10E0E0C

#define	U3_LTSSM_TIMING_PARAM3	0x2514
#define	U3_LTSSM_TIMING_VAL	0x3E8012C

#define	SYNC_HS_EOF		0x938
#define	SYNC_HS_EOF_VAL		0x201F3

#define	USB_IP_SPAR0		0x107C8
#define	USB_IP_SPAR0_VAL	1

#define	U2_PHY_BASE_P0		0x10800
#define	U2_PHY_BASE_P1		0x11000
#define	U2_PHYD_CR1		0x64
#define	U2_PHYD_CR1_MASK	(3<<18)
#define	U2_PHYD_CR1_VAL		(1<<18)

#define	USB_IP_PW_CTRL		0x10700
#define	USB_IP_PW_CTRL_1	0x10704
#define	USB_IP_CAP		0x10724
#define	USB_U3_CTRL(p)		(0x10730 + ((p) * 0x08))
#define	USB_U2_CTRL(p)		(0x10750 + ((p) * 0x08))

#define	USB_IP_SW_RST		(1 << 0)
#define	USB_IP_PDN		(1 << 0)

#define	USB_PORT_DIS		(1 << 0)
#define	USB_PORT_PDN		(1 << 1)

#define	U3_PORT_NUM(p)		(p & 0xFF)
#define	U2_PORT_NUM(p)		((p>>8) & 0xFF)

#define	RD4(_sc, _reg)		bus_read_4((_sc)->sc_io_res, (_reg))
#define	WR4(_sc, _reg, _val)	bus_write_4((_sc)->sc_io_res, (_reg), (_val))
#define	CLRSET4(_sc, _reg, _clr, _set)	\
    WR4((_sc), (_reg), (RD4((_sc), (_reg)) & ~(_clr)) | (_set))

static void
mtk_xhci_fdt_init(device_t dev)
{
	struct xhci_softc *sc;
	uint32_t temp, u3_ports, u2_ports, i;

	sc = device_get_softc(dev);

	temp = RD4(sc, USB_IP_CAP);
	u3_ports = U3_PORT_NUM(temp);
	u2_ports = U2_PORT_NUM(temp);

	device_printf(dev, "%d USB3 ports, %d USB2 ports\n",
	    u3_ports, u2_ports);

	CLRSET4(sc, USB_IP_PW_CTRL, 0, USB_IP_SW_RST);
	CLRSET4(sc, USB_IP_PW_CTRL, USB_IP_SW_RST, 0);
	CLRSET4(sc, USB_IP_PW_CTRL_1, USB_IP_PDN, 0);

	for (i = 0; i < u3_ports; i++)
		CLRSET4(sc, USB_U3_CTRL(i), USB_PORT_PDN | USB_PORT_DIS, 0);

	for (i = 0; i < u2_ports; i++)
		CLRSET4(sc, USB_U2_CTRL(i), USB_PORT_PDN | USB_PORT_DIS, 0);

	DELAY(100000);

	WR4(sc, USB_HDMA_CFG, USB_HDMA_CFG_MT7621_VAL);
	WR4(sc, U3_LTSSM_TIMING_PARAM3, U3_LTSSM_TIMING_VAL);
	WR4(sc, SYNC_HS_EOF, SYNC_HS_EOF_VAL);
	WR4(sc, USB_IP_SPAR0, USB_IP_SPAR0_VAL);
	CLRSET4(sc, U2_PHY_BASE_P0 + U2_PHYD_CR1, U2_PHYD_CR1_MASK,
	    U2_PHYD_CR1_VAL);
	CLRSET4(sc, U2_PHY_BASE_P1 + U2_PHYD_CR1, U2_PHYD_CR1_MASK,
	    U2_PHYD_CR1_VAL);
}
