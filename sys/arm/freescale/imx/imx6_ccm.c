/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ian Lepore <ian@freebsd.org>
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

/*
 * Clocks and power control driver for Freescale i.MX6 family of SoCs.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include <arm/freescale/imx/imx6_anatopreg.h>
#include <arm/freescale/imx/imx6_anatopvar.h>
#include <arm/freescale/imx/imx6_ccmreg.h>
#include <arm/freescale/imx/imx_machdep.h>
#include <arm/freescale/imx/imx_ccmvar.h>

#ifndef CCGR_CLK_MODE_ALWAYS
#define	CCGR_CLK_MODE_OFF		0
#define	CCGR_CLK_MODE_RUNMODE		1
#define	CCGR_CLK_MODE_ALWAYS		3
#endif

struct ccm_softc {
	device_t	dev;
	struct resource	*mem_res;
};

static struct ccm_softc *ccm_sc;

static inline uint32_t
RD4(struct ccm_softc *sc, bus_size_t off)
{

	return (bus_read_4(sc->mem_res, off));
}

static inline void
WR4(struct ccm_softc *sc, bus_size_t off, uint32_t val)
{

	bus_write_4(sc->mem_res, off, val);
}

/*
 * Until we have a fully functional ccm driver which implements the fdt_clock
 * interface, use the age-old workaround of unconditionally enabling the clocks
 * for devices we might need to use.  The SoC defaults to most clocks enabled,
 * but the rom boot code and u-boot disable a few of them.  We turn on only
 * what's needed to run the chip plus devices we have drivers for, and turn off
 * devices we don't yet have drivers for.  (Note that USB is not turned on here
 * because that is one we do when the driver asks for it.)
 */
static void
ccm_init_gates(struct ccm_softc *sc)
{
	uint32_t reg;

 	/* ahpbdma, aipstz 1 & 2 buses */
	reg = CCGR0_AIPS_TZ1 | CCGR0_AIPS_TZ2 | CCGR0_ABPHDMA;
	WR4(sc, CCM_CCGR0, reg);

	/* enet, epit, gpt, spi */
	reg = CCGR1_ENET | CCGR1_EPIT1 | CCGR1_GPT | CCGR1_ECSPI1 |
	    CCGR1_ECSPI2 | CCGR1_ECSPI3 | CCGR1_ECSPI4 | CCGR1_ECSPI5;
	WR4(sc, CCM_CCGR1, reg);

	/* ipmux & ipsync (bridges), iomux, i2c */
	reg = CCGR2_I2C1 | CCGR2_I2C2 | CCGR2_I2C3 | CCGR2_IIM |
	    CCGR2_IOMUX_IPT | CCGR2_IPMUX1 | CCGR2_IPMUX2 | CCGR2_IPMUX3 |
	    CCGR2_IPSYNC_IP2APB_TZASC1 | CCGR2_IPSYNC_IP2APB_TZASC2 |
	    CCGR2_IPSYNC_VDOA;
	WR4(sc, CCM_CCGR2, reg);

	/* DDR memory controller */
	reg = CCGR3_OCRAM | CCGR3_MMDC_CORE_IPG |
	    CCGR3_MMDC_CORE_ACLK_FAST | CCGR3_CG11 | CCGR3_CG13;
	WR4(sc, CCM_CCGR3, reg);

	/* pl301 bus crossbar */
	reg = CCGR4_PL301_MX6QFAST1_S133 |
	    CCGR4_PL301_MX6QPER1_BCH | CCGR4_PL301_MX6QPER2_MAIN;
	WR4(sc, CCM_CCGR4, reg);

	/* uarts, ssi, sdma */
	reg = CCGR5_SDMA | CCGR5_SSI1 | CCGR5_SSI2 | CCGR5_SSI3 |
	    CCGR5_UART | CCGR5_UART_SERIAL;
	WR4(sc, CCM_CCGR5, reg);

	/* usdhc 1-4, usboh3 */
	reg = CCGR6_USBOH3 | CCGR6_USDHC1 | CCGR6_USDHC2 |
	    CCGR6_USDHC3 | CCGR6_USDHC4;
	WR4(sc, CCM_CCGR6, reg);
}

static int
ccm_detach(device_t dev)
{
	struct ccm_softc *sc;

	sc = device_get_softc(dev);

	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	return (0);
}

static int
ccm_attach(device_t dev)
{
	struct ccm_softc *sc;
	int err, rid;
	uint32_t reg;

	sc = device_get_softc(dev);
	err = 0;

	/* Allocate bus_space resources. */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		err = ENXIO;
		goto out;
	}

	ccm_sc = sc;

	/*
	 * Configure the Low Power Mode setting to leave the ARM core power on
	 * when a WFI instruction is executed.  This lets the MPCore timers and
	 * GIC continue to run, which is helpful when the only thing that can
	 * wake you up is an MPCore Private Timer interrupt delivered via GIC.
	 *
	 * XXX Based on the docs, setting CCM_CGPR_INT_MEM_CLK_LPM shouldn't be
	 * required when the LPM bits are set to LPM_RUN.  But experimentally
	 * I've experienced a fairly rare lockup when not setting it.  I was
	 * unable to prove conclusively that the lockup was related to power
	 * management or that this definitively fixes it.  Revisit this.
	 */
	reg = RD4(sc, CCM_CGPR);
	reg |= CCM_CGPR_INT_MEM_CLK_LPM;
	WR4(sc, CCM_CGPR, reg);
	reg = RD4(sc, CCM_CLPCR);
	reg = (reg & ~CCM_CLPCR_LPM_MASK) | CCM_CLPCR_LPM_RUN;
	WR4(sc, CCM_CLPCR, reg);

	ccm_init_gates(sc);

	err = 0;

out:

	if (err != 0)
		ccm_detach(dev);

	return (err);
}

static int
ccm_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

        if (ofw_bus_is_compatible(dev, "fsl,imx6q-ccm") == 0)
		return (ENXIO);

	device_set_desc(dev, "Freescale i.MX6 Clock Control Module");

	return (BUS_PROBE_DEFAULT);
}

void
imx_ccm_ssi_configure(device_t _ssidev)
{
	struct ccm_softc *sc;
	uint32_t reg;

	sc = ccm_sc;

	/*
	 * Select PLL4 (Audio PLL) clock multiplexer as source.
	 * PLL output frequency = Fref * (DIV_SELECT + NUM/DENOM).
	 */

	reg = RD4(sc, CCM_CSCMR1);
	reg &= ~(SSI_CLK_SEL_M << SSI1_CLK_SEL_S);
	reg |= (SSI_CLK_SEL_PLL4 << SSI1_CLK_SEL_S);
	reg &= ~(SSI_CLK_SEL_M << SSI2_CLK_SEL_S);
	reg |= (SSI_CLK_SEL_PLL4 << SSI2_CLK_SEL_S);
	reg &= ~(SSI_CLK_SEL_M << SSI3_CLK_SEL_S);
	reg |= (SSI_CLK_SEL_PLL4 << SSI3_CLK_SEL_S);
	WR4(sc, CCM_CSCMR1, reg);

	/*
	 * Ensure we have set hardware-default values
	 * for pre and post dividers.
	 */

	/* SSI1 and SSI3 */
	reg = RD4(sc, CCM_CS1CDR);
	/* Divide by 2 */
	reg &= ~(SSI_CLK_PODF_MASK << SSI1_CLK_PODF_SHIFT);
	reg &= ~(SSI_CLK_PODF_MASK << SSI3_CLK_PODF_SHIFT);
	reg |= (0x1 << SSI1_CLK_PODF_SHIFT);
	reg |= (0x1 << SSI3_CLK_PODF_SHIFT);
	/* Divide by 4 */
	reg &= ~(SSI_CLK_PRED_MASK << SSI1_CLK_PRED_SHIFT);
	reg &= ~(SSI_CLK_PRED_MASK << SSI3_CLK_PRED_SHIFT);
	reg |= (0x3 << SSI1_CLK_PRED_SHIFT);
	reg |= (0x3 << SSI3_CLK_PRED_SHIFT);
	WR4(sc, CCM_CS1CDR, reg);

	/* SSI2 */
	reg = RD4(sc, CCM_CS2CDR);
	/* Divide by 2 */
	reg &= ~(SSI_CLK_PODF_MASK << SSI2_CLK_PODF_SHIFT);
	reg |= (0x1 << SSI2_CLK_PODF_SHIFT);
	/* Divide by 4 */
	reg &= ~(SSI_CLK_PRED_MASK << SSI2_CLK_PRED_SHIFT);
	reg |= (0x3 << SSI2_CLK_PRED_SHIFT);
	WR4(sc, CCM_CS2CDR, reg);
}

void
imx_ccm_usb_enable(device_t _usbdev)
{

	/*
	 * For imx6, the USBOH3 clock gate is bits 0-1 of CCGR6, so no need for
	 * shifting and masking here, just set the low-order two bits to ALWAYS.
	 */
	WR4(ccm_sc, CCM_CCGR6, RD4(ccm_sc, CCM_CCGR6) | CCGR_CLK_MODE_ALWAYS);
}

void
imx_ccm_usbphy_enable(device_t _phydev)
{
        /*
         * XXX Which unit?
         * Right now it's not clear how to figure from fdt data which phy unit
         * we're supposed to operate on.  Until this is worked out, just enable
         * both PHYs.
         */
#if 0
	int phy_num, regoff;

	phy_num = 0; /* XXX */

	switch (phy_num) {
	case 0:
		regoff = 0;
		break;
	case 1:
		regoff = 0x10;
		break;
	default:
		device_printf(ccm_sc->dev, "Bad PHY number %u,\n", 
		    phy_num);
		return;
	}

	imx6_anatop_write_4(IMX6_ANALOG_CCM_PLL_USB1 + regoff, 
	    IMX6_ANALOG_CCM_PLL_USB_ENABLE | 
	    IMX6_ANALOG_CCM_PLL_USB_POWER |
	    IMX6_ANALOG_CCM_PLL_USB_EN_USB_CLKS);
#else
	imx6_anatop_write_4(IMX6_ANALOG_CCM_PLL_USB1 + 0,
	    IMX6_ANALOG_CCM_PLL_USB_ENABLE | 
	    IMX6_ANALOG_CCM_PLL_USB_POWER |
	    IMX6_ANALOG_CCM_PLL_USB_EN_USB_CLKS);

	imx6_anatop_write_4(IMX6_ANALOG_CCM_PLL_USB1 + 0x10, 
	    IMX6_ANALOG_CCM_PLL_USB_ENABLE | 
	    IMX6_ANALOG_CCM_PLL_USB_POWER |
	    IMX6_ANALOG_CCM_PLL_USB_EN_USB_CLKS);
#endif
}

int
imx6_ccm_sata_enable(void)
{
	uint32_t v;
	int timeout;

	/* Un-gate the sata controller. */
	WR4(ccm_sc, CCM_CCGR5, RD4(ccm_sc, CCM_CCGR5) | CCGR5_SATA);

	/* Power up the PLL that feeds ENET/SATA/PCI phys, wait for lock. */
	v = RD4(ccm_sc, CCM_ANALOG_PLL_ENET);
	v &= ~CCM_ANALOG_PLL_ENET_POWERDOWN;
	WR4(ccm_sc, CCM_ANALOG_PLL_ENET, v);

	for (timeout = 100000; timeout > 0; timeout--) {
		if (RD4(ccm_sc, CCM_ANALOG_PLL_ENET) &
		   CCM_ANALOG_PLL_ENET_LOCK) {
			break;
		}
	}
	if (timeout <= 0) {
		return ETIMEDOUT;
	}

	/* Enable the PLL, and enable its 100mhz output. */
	v |= CCM_ANALOG_PLL_ENET_ENABLE;
	v &= ~CCM_ANALOG_PLL_ENET_BYPASS;
	WR4(ccm_sc, CCM_ANALOG_PLL_ENET, v);

	v |= CCM_ANALOG_PLL_ENET_ENABLE_100M;
	WR4(ccm_sc, CCM_ANALOG_PLL_ENET, v);

	return 0;
}

uint32_t
imx_ccm_ecspi_hz(void)
{

	return (60000000);
}

uint32_t
imx_ccm_ipg_hz(void)
{

	return (66000000);
}

uint32_t
imx_ccm_perclk_hz(void)
{

	return (66000000);
}

uint32_t
imx_ccm_sdhci_hz(void)
{

	return (200000000);
}

uint32_t
imx_ccm_uart_hz(void)
{

	return (80000000);
}

uint32_t
imx_ccm_ahb_hz(void)
{
	return (132000000);
}

void
imx_ccm_ipu_enable(int ipu)
{
	struct ccm_softc *sc;
	uint32_t reg;

	sc = ccm_sc;
	reg = RD4(sc, CCM_CCGR3);
	if (ipu == 1)
		reg |= CCGR3_IPU1_IPU | CCGR3_IPU1_DI0;
	else
		reg |= CCGR3_IPU2_IPU | CCGR3_IPU2_DI0;
	WR4(sc, CCM_CCGR3, reg);
}

void
imx_ccm_hdmi_enable(void)
{
	struct ccm_softc *sc;
	uint32_t reg;

	sc = ccm_sc;
	reg = RD4(sc, CCM_CCGR2);
	reg |= CCGR2_HDMI_TX | CCGR2_HDMI_TX_ISFR;
	WR4(sc, CCM_CCGR2, reg);

	/* Set HDMI clock to 280MHz */
	reg = RD4(sc, CCM_CHSCCDR);
	reg &= ~(CHSCCDR_IPU1_DI0_PRE_CLK_SEL_MASK |
	    CHSCCDR_IPU1_DI0_PODF_MASK | CHSCCDR_IPU1_DI0_CLK_SEL_MASK);
	reg |= (CHSCCDR_PODF_DIVIDE_BY_3 << CHSCCDR_IPU1_DI0_PODF_SHIFT);
	reg |= (CHSCCDR_IPU_PRE_CLK_540M_PFD << CHSCCDR_IPU1_DI0_PRE_CLK_SEL_SHIFT);
	WR4(sc, CCM_CHSCCDR, reg);
	reg |= (CHSCCDR_CLK_SEL_LDB_DI0 << CHSCCDR_IPU1_DI0_CLK_SEL_SHIFT);
	WR4(sc, CCM_CHSCCDR, reg);
}

uint32_t
imx_ccm_get_cacrr(void)
{

	return (RD4(ccm_sc, CCM_CACCR));
}

void
imx_ccm_set_cacrr(uint32_t divisor)
{

	WR4(ccm_sc, CCM_CACCR, divisor);
}

static device_method_t ccm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,  ccm_probe),
	DEVMETHOD(device_attach, ccm_attach),
	DEVMETHOD(device_detach, ccm_detach),

	DEVMETHOD_END
};

static driver_t ccm_driver = {
	"ccm",
	ccm_methods,
	sizeof(struct ccm_softc)
};

static devclass_t ccm_devclass;

EARLY_DRIVER_MODULE(ccm, simplebus, ccm_driver, ccm_devclass, 0, 0, 
    BUS_PASS_CPU + BUS_PASS_ORDER_EARLY);

