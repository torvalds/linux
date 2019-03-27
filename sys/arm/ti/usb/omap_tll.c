/*-
 * Copyright (c) 2011
 *	Ben Gray <ben.r.gray@gmail.com>.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/rman.h>
#include <sys/module.h>

#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include <arm/ti/ti_prcm.h>
#include <arm/ti/usb/omap_usb.h>

/*
 * USB TLL Module
 */
#define	OMAP_USBTLL_REVISION                        0x0000
#define	OMAP_USBTLL_SYSCONFIG                       0x0010
#define	OMAP_USBTLL_SYSSTATUS                       0x0014
#define	OMAP_USBTLL_IRQSTATUS                       0x0018
#define	OMAP_USBTLL_IRQENABLE                       0x001C
#define	OMAP_USBTLL_TLL_SHARED_CONF                 0x0030
#define	OMAP_USBTLL_TLL_CHANNEL_CONF(i)             (0x0040 + (0x04 * (i)))
#define	OMAP_USBTLL_SAR_CNTX(i)                     (0x0400 + (0x04 * (i)))
#define	OMAP_USBTLL_ULPI_VENDOR_ID_LO(i)            (0x0800 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_VENDOR_ID_HI(i)            (0x0801 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_PRODUCT_ID_LO(i)           (0x0802 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_PRODUCT_ID_HI(i)           (0x0803 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_FUNCTION_CTRL(i)           (0x0804 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_FUNCTION_CTRL_SET(i)       (0x0805 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_FUNCTION_CTRL_CLR(i)       (0x0806 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_INTERFACE_CTRL(i)          (0x0807 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_INTERFACE_CTRL_SET(i)      (0x0808 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_INTERFACE_CTRL_CLR(i)      (0x0809 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_OTG_CTRL(i)                (0x080A + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_OTG_CTRL_SET(i)            (0x080B + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_OTG_CTRL_CLR(i)            (0x080C + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_EN_RISE(i)         (0x080D + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_EN_RISE_SET(i)     (0x080E + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_EN_RISE_CLR(i)     (0x080F + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_EN_FALL(i)         (0x0810 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_EN_FALL_SET(i)     (0x0811 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_EN_FALL_CLR(i)     (0x0812 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_STATUS(i)          (0x0813 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_LATCH(i)           (0x0814 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_DEBUG(i)                   (0x0815 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_SCRATCH_REGISTER(i)        (0x0816 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_SCRATCH_REGISTER_SET(i)    (0x0817 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_SCRATCH_REGISTER_CLR(i)    (0x0818 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_EXTENDED_SET_ACCESS(i)     (0x082F + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VCONTROL_EN(i)        (0x0830 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VCONTROL_EN_SET(i)    (0x0831 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VCONTROL_EN_CLR(i)    (0x0832 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VCONTROL_STATUS(i)    (0x0833 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VCONTROL_LATCH(i)     (0x0834 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VSTATUS(i)            (0x0835 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VSTATUS_SET(i)        (0x0836 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_UTMI_VSTATUS_CLR(i)        (0x0837 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_USB_INT_LATCH_NOCLR(i)     (0x0838 + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_VENDOR_INT_EN(i)           (0x083B + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_VENDOR_INT_EN_SET(i)       (0x083C + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_VENDOR_INT_EN_CLR(i)       (0x083D + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_VENDOR_INT_STATUS(i)       (0x083E + (0x100 * (i)))
#define	OMAP_USBTLL_ULPI_VENDOR_INT_LATCH(i)        (0x083F + (0x100 * (i)))

/* TLL Register Set */
#define	TLL_SYSCONFIG_CACTIVITY                 (1UL << 8)
#define	TLL_SYSCONFIG_SIDLE_SMART_IDLE          (2UL << 3)
#define	TLL_SYSCONFIG_SIDLE_NO_IDLE             (1UL << 3)
#define	TLL_SYSCONFIG_SIDLE_FORCED_IDLE         (0UL << 3)
#define	TLL_SYSCONFIG_ENAWAKEUP                 (1UL << 2)
#define	TLL_SYSCONFIG_SOFTRESET                 (1UL << 1)
#define	TLL_SYSCONFIG_AUTOIDLE                  (1UL << 0)

#define	TLL_SYSSTATUS_RESETDONE                 (1UL << 0)

#define TLL_SHARED_CONF_USB_90D_DDR_EN          (1UL << 6)
#define TLL_SHARED_CONF_USB_180D_SDR_EN         (1UL << 5)
#define TLL_SHARED_CONF_USB_DIVRATIO_MASK       (7UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_128        (7UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_64         (6UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_32         (5UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_16         (4UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_8          (3UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_4          (2UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_2          (1UL << 2)
#define TLL_SHARED_CONF_USB_DIVRATIO_1          (0UL << 2)
#define TLL_SHARED_CONF_FCLK_REQ                (1UL << 1)
#define TLL_SHARED_CONF_FCLK_IS_ON              (1UL << 0)

#define TLL_CHANNEL_CONF_DRVVBUS                (1UL << 16)
#define TLL_CHANNEL_CONF_CHRGVBUS               (1UL << 15)
#define TLL_CHANNEL_CONF_ULPINOBITSTUFF         (1UL << 11)
#define TLL_CHANNEL_CONF_ULPIAUTOIDLE           (1UL << 10)
#define TLL_CHANNEL_CONF_UTMIAUTOIDLE           (1UL << 9)
#define TLL_CHANNEL_CONF_ULPIDDRMODE            (1UL << 8)
#define TLL_CHANNEL_CONF_ULPIOUTCLKMODE         (1UL << 7)
#define TLL_CHANNEL_CONF_TLLFULLSPEED           (1UL << 6)
#define TLL_CHANNEL_CONF_TLLCONNECT             (1UL << 5)
#define TLL_CHANNEL_CONF_TLLATTACH              (1UL << 4)
#define TLL_CHANNEL_CONF_UTMIISADEV             (1UL << 3)
#define TLL_CHANNEL_CONF_CHANEN                 (1UL << 0)

struct omap_tll_softc {
	device_t		sc_dev;

	/* TLL register set */
	struct resource*	tll_mem_res;
	int			tll_mem_rid;
};

static struct omap_tll_softc *omap_tll_sc;

static int omap_tll_attach(device_t dev);
static int omap_tll_detach(device_t dev);

static inline uint32_t
omap_tll_read_4(struct omap_tll_softc *sc, bus_size_t off)
{
	return bus_read_4(sc->tll_mem_res, off);
}

static inline void
omap_tll_write_4(struct omap_tll_softc *sc, bus_size_t off, uint32_t val)
{
	bus_write_4(sc->tll_mem_res, off, val);
}

void
omap_tll_utmi_enable(unsigned int en_mask)
{
	struct omap_tll_softc *sc;
	unsigned int i;
	uint32_t reg;

	sc = omap_tll_sc;
	if (sc == NULL)
		return;

	/* There are 3 TLL channels, one per USB controller so set them all up the
	 * same, SDR mode, bit stuffing and no autoidle.
	 */
	for (i=0; i<3; i++) {
		reg = omap_tll_read_4(sc, OMAP_USBTLL_TLL_CHANNEL_CONF(i));

		reg &= ~(TLL_CHANNEL_CONF_UTMIAUTOIDLE
				 | TLL_CHANNEL_CONF_ULPINOBITSTUFF
				 | TLL_CHANNEL_CONF_ULPIDDRMODE);

		omap_tll_write_4(sc, OMAP_USBTLL_TLL_CHANNEL_CONF(i), reg);
	}

	/* Program the common TLL register */
	reg = omap_tll_read_4(sc, OMAP_USBTLL_TLL_SHARED_CONF);

	reg &= ~( TLL_SHARED_CONF_USB_90D_DDR_EN
			| TLL_SHARED_CONF_USB_DIVRATIO_MASK);
	reg |=  ( TLL_SHARED_CONF_FCLK_IS_ON
			| TLL_SHARED_CONF_USB_DIVRATIO_2
			| TLL_SHARED_CONF_USB_180D_SDR_EN);

	omap_tll_write_4(sc, OMAP_USBTLL_TLL_SHARED_CONF, reg);

	/* Enable channels now */
	for (i = 0; i < 3; i++) {
		reg = omap_tll_read_4(sc, OMAP_USBTLL_TLL_CHANNEL_CONF(i));

		/* Enable only the reg that is needed */
		if ((en_mask & (1 << i)) == 0)
			continue;

		reg |= TLL_CHANNEL_CONF_CHANEN;
		omap_tll_write_4(sc, OMAP_USBTLL_TLL_CHANNEL_CONF(i), reg);
	}
}

static int
omap_tll_init(struct omap_tll_softc *sc)
{
	unsigned long timeout;
	int ret = 0;

	/* Enable the USB TLL */
	ti_prcm_clk_enable(USBTLL_CLK);

	/* Perform TLL soft reset, and wait until reset is complete */
	omap_tll_write_4(sc, OMAP_USBTLL_SYSCONFIG, TLL_SYSCONFIG_SOFTRESET);

	/* Set the timeout to 100ms*/
	timeout = (hz < 10) ? 1 : ((100 * hz) / 1000);

	/* Wait for TLL reset to complete */
	while ((omap_tll_read_4(sc, OMAP_USBTLL_SYSSTATUS) &
	        TLL_SYSSTATUS_RESETDONE) == 0x00) {

		/* Sleep for a tick */
		pause("USBRESET", 1);

		if (timeout-- == 0) {
			device_printf(sc->sc_dev, "TLL reset operation timed out\n");
			ret = EINVAL;
			goto err_sys_status;
		}
	}

	/* CLOCKACTIVITY = 1 : OCP-derived internal clocks ON during idle
	 * SIDLEMODE = 2     : Smart-idle mode. Sidleack asserted after Idlereq
	 *                     assertion when no more activity on the USB.
	 * ENAWAKEUP = 1     : Wakeup generation enabled
	 */
	omap_tll_write_4(sc, OMAP_USBTLL_SYSCONFIG, TLL_SYSCONFIG_ENAWAKEUP |
	                                            TLL_SYSCONFIG_AUTOIDLE |
	                                            TLL_SYSCONFIG_SIDLE_SMART_IDLE |
	                                            TLL_SYSCONFIG_CACTIVITY);

	return(0);

err_sys_status:
	/* Disable the TLL clocks */
	ti_prcm_clk_disable(USBTLL_CLK);

	return(ret);
}

static void
omap_tll_disable(struct omap_tll_softc *sc)
{
	unsigned long timeout;

	timeout = (hz < 10) ? 1 : ((100 * hz) / 1000);

	/* Reset the TLL module */
	omap_tll_write_4(sc, OMAP_USBTLL_SYSCONFIG, 0x0002);
	while ((omap_tll_read_4(sc, OMAP_USBTLL_SYSSTATUS) & (0x01)) == 0x00) {
		/* Sleep for a tick */
		pause("USBRESET", 1);

		if (timeout-- == 0) {
			device_printf(sc->sc_dev, "operation timed out\n");
			break;
		}
	}

	/* Disable functional and interface clocks for the TLL and HOST modules */
	ti_prcm_clk_disable(USBTLL_CLK);
}

static int
omap_tll_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ti,usbhs-tll"))
		return (ENXIO);

	device_set_desc(dev, "TI OMAP USB 2.0 TLL module");

	return (BUS_PROBE_DEFAULT);
}

static int
omap_tll_attach(device_t dev)
{
	struct omap_tll_softc *sc;

	sc = device_get_softc(dev);
	/* save the device */
	sc->sc_dev = dev;

	/* Allocate resource for the TLL register set */
	sc->tll_mem_rid = 0;
	sc->tll_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->tll_mem_rid, RF_ACTIVE);
	if (!sc->tll_mem_res) {
		device_printf(dev, "Error: Could not map TLL memory\n");
		goto error;
	}

	omap_tll_init(sc);

	omap_tll_sc = sc;

	return (0);

error:
	omap_tll_detach(dev);
	return (ENXIO);
}

static int
omap_tll_detach(device_t dev)
{
	struct omap_tll_softc *sc;

	sc = device_get_softc(dev);
	omap_tll_disable(sc);

	/* Release the other register set memory maps */
	if (sc->tll_mem_res) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->tll_mem_rid, sc->tll_mem_res);
		sc->tll_mem_res = NULL;
	}

	omap_tll_sc = NULL;

	return (0);
}

static device_method_t omap_tll_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, omap_tll_probe),
	DEVMETHOD(device_attach, omap_tll_attach),
	DEVMETHOD(device_detach, omap_tll_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	{0, 0}
};

static driver_t omap_tll_driver = {
	"omap_tll",
	omap_tll_methods,
	sizeof(struct omap_tll_softc),
};

static devclass_t omap_tll_devclass;

DRIVER_MODULE(omap_tll, simplebus, omap_tll_driver, omap_tll_devclass, 0, 0);
