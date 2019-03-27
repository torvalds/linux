/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Damjan Marion <dmarion@Freebsd.org>
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
#include <machine/intr.h>

#include <arm/ti/tivar.h>
#include <arm/ti/ti_scm.h>
#include <arm/ti/ti_prcm.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include "am335x_scm.h"

#define CM_PER				0
#define CM_PER_L4LS_CLKSTCTRL		(CM_PER + 0x000)
#define CM_PER_L3S_CLKSTCTRL		(CM_PER + 0x004)
#define CM_PER_L3_CLKSTCTRL		(CM_PER + 0x00C)
#define CM_PER_CPGMAC0_CLKCTRL		(CM_PER + 0x014)
#define CM_PER_LCDC_CLKCTRL		(CM_PER + 0x018)
#define CM_PER_USB0_CLKCTRL		(CM_PER + 0x01C)
#define CM_PER_TPTC0_CLKCTRL		(CM_PER + 0x024)
#define CM_PER_UART5_CLKCTRL		(CM_PER + 0x038)
#define CM_PER_MMC0_CLKCTRL		(CM_PER + 0x03C)
#define CM_PER_I2C2_CLKCTRL		(CM_PER + 0x044)
#define CM_PER_I2C1_CLKCTRL		(CM_PER + 0x048)
#define CM_PER_SPI0_CLKCTRL		(CM_PER + 0x04C)
#define CM_PER_SPI1_CLKCTRL		(CM_PER + 0x050)
#define CM_PER_UART1_CLKCTRL		(CM_PER + 0x06C)
#define CM_PER_UART2_CLKCTRL		(CM_PER + 0x070)
#define CM_PER_UART3_CLKCTRL		(CM_PER + 0x074)
#define CM_PER_UART4_CLKCTRL		(CM_PER + 0x078)
#define CM_PER_TIMER7_CLKCTRL		(CM_PER + 0x07C)
#define CM_PER_TIMER2_CLKCTRL		(CM_PER + 0x080)
#define CM_PER_TIMER3_CLKCTRL		(CM_PER + 0x084)
#define CM_PER_TIMER4_CLKCTRL		(CM_PER + 0x088)
#define CM_PER_GPIO1_CLKCTRL		(CM_PER + 0x0AC)
#define CM_PER_GPIO2_CLKCTRL		(CM_PER + 0x0B0)
#define CM_PER_GPIO3_CLKCTRL		(CM_PER + 0x0B4)
#define CM_PER_TPCC_CLKCTRL		(CM_PER + 0x0BC)
#define CM_PER_EPWMSS1_CLKCTRL		(CM_PER + 0x0CC)
#define CM_PER_EPWMSS0_CLKCTRL		(CM_PER + 0x0D4)
#define CM_PER_EPWMSS2_CLKCTRL		(CM_PER + 0x0D8)
#define CM_PER_L3_INSTR_CLKCTRL		(CM_PER + 0x0DC)
#define CM_PER_L3_CLKCTRL		(CM_PER + 0x0E0)
#define	CM_PER_PRUSS_CLKCTRL		(CM_PER + 0x0E8)
#define CM_PER_TIMER5_CLKCTRL		(CM_PER + 0x0EC)
#define CM_PER_TIMER6_CLKCTRL		(CM_PER + 0x0F0)
#define CM_PER_MMC1_CLKCTRL		(CM_PER + 0x0F4)
#define CM_PER_MMC2_CLKCTRL		(CM_PER + 0x0F8)
#define CM_PER_TPTC1_CLKCTRL		(CM_PER + 0x0FC)
#define CM_PER_TPTC2_CLKCTRL		(CM_PER + 0x100)
#define	CM_PER_SPINLOCK0_CLKCTRL	(CM_PER + 0x10C)
#define	CM_PER_MAILBOX0_CLKCTRL		(CM_PER + 0x110)
#define CM_PER_OCPWP_L3_CLKSTCTRL	(CM_PER + 0x12C)
#define CM_PER_OCPWP_CLKCTRL		(CM_PER + 0x130)
#define CM_PER_CPSW_CLKSTCTRL		(CM_PER + 0x144)
#define	CM_PER_PRUSS_CLKSTCTRL		(CM_PER + 0x140)

#define CM_WKUP				0x400
#define CM_WKUP_CLKSTCTRL		(CM_WKUP + 0x000)
#define CM_WKUP_CONTROL_CLKCTRL		(CM_WKUP + 0x004)
#define CM_WKUP_GPIO0_CLKCTRL		(CM_WKUP + 0x008)
#define CM_WKUP_CM_L3_AON_CLKSTCTRL	(CM_WKUP + 0x01C)
#define CM_WKUP_CM_CLKSEL_DPLL_MPU	(CM_WKUP + 0x02C)
#define CM_WKUP_CM_IDLEST_DPLL_DISP	(CM_WKUP + 0x048)
#define CM_WKUP_CM_CLKSEL_DPLL_DISP	(CM_WKUP + 0x054)
#define CM_WKUP_CM_CLKDCOLDO_DPLL_PER	(CM_WKUP + 0x07C)
#define CM_WKUP_CM_CLKMODE_DPLL_DISP	(CM_WKUP + 0x098)
#define CM_WKUP_I2C0_CLKCTRL		(CM_WKUP + 0x0B8)
#define CM_WKUP_ADC_TSC_CLKCTRL		(CM_WKUP + 0x0BC)

#define CM_DPLL				0x500
#define CLKSEL_TIMER7_CLK		(CM_DPLL + 0x004)
#define CLKSEL_TIMER2_CLK		(CM_DPLL + 0x008)
#define CLKSEL_TIMER3_CLK		(CM_DPLL + 0x00C)
#define CLKSEL_TIMER4_CLK		(CM_DPLL + 0x010)
#define CLKSEL_TIMER5_CLK		(CM_DPLL + 0x018)
#define CLKSEL_TIMER6_CLK		(CM_DPLL + 0x01C)
#define	CLKSEL_PRUSS_OCP_CLK		(CM_DPLL + 0x030)

#define	CM_RTC				0x800
#define	CM_RTC_RTC_CLKCTRL		(CM_RTC + 0x000)
#define	CM_RTC_CLKSTCTRL		(CM_RTC + 0x004)

#define	PRM_PER				0xC00
#define	PRM_PER_RSTCTRL			(PRM_PER + 0x00)

#define PRM_DEVICE_OFFSET		0xF00
#define PRM_RSTCTRL			(PRM_DEVICE_OFFSET + 0x00)

struct am335x_prcm_softc {
	struct resource *	res[2];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	int			attach_done;
};

static struct resource_spec am335x_prcm_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static struct am335x_prcm_softc *am335x_prcm_sc = NULL;

static int am335x_clk_noop_activate(struct ti_clock_dev *clkdev);
static int am335x_clk_generic_activate(struct ti_clock_dev *clkdev);
static int am335x_clk_gpio_activate(struct ti_clock_dev *clkdev);
static int am335x_clk_noop_deactivate(struct ti_clock_dev *clkdev);
static int am335x_clk_generic_deactivate(struct ti_clock_dev *clkdev);
static int am335x_clk_noop_set_source(struct ti_clock_dev *clkdev, clk_src_t clksrc);
static int am335x_clk_generic_set_source(struct ti_clock_dev *clkdev, clk_src_t clksrc);
static int am335x_clk_hsmmc_get_source_freq(struct ti_clock_dev *clkdev,  unsigned int *freq);
static int am335x_clk_get_sysclk_freq(struct ti_clock_dev *clkdev, unsigned int *freq);
static int am335x_clk_get_arm_fclk_freq(struct ti_clock_dev *clkdev, unsigned int *freq);
static int am335x_clk_get_arm_disp_freq(struct ti_clock_dev *clkdev, unsigned int *freq);
static int am335x_clk_set_arm_disp_freq(struct ti_clock_dev *clkdev, unsigned int freq);
static void am335x_prcm_reset(void);
static int am335x_clk_cpsw_activate(struct ti_clock_dev *clkdev);
static int am335x_clk_musb0_activate(struct ti_clock_dev *clkdev);
static int am335x_clk_lcdc_activate(struct ti_clock_dev *clkdev);
static int am335x_clk_pruss_activate(struct ti_clock_dev *clkdev);

#define AM335X_NOOP_CLOCK_DEV(i) \
	{	.id = (i), \
		.clk_activate = am335x_clk_noop_activate, \
		.clk_deactivate = am335x_clk_noop_deactivate, \
		.clk_set_source = am335x_clk_noop_set_source, \
		.clk_accessible = NULL, \
		.clk_get_source_freq = NULL, \
		.clk_set_source_freq = NULL \
	}

#define AM335X_GENERIC_CLOCK_DEV(i) \
	{	.id = (i), \
		.clk_activate = am335x_clk_generic_activate, \
		.clk_deactivate = am335x_clk_generic_deactivate, \
		.clk_set_source = am335x_clk_generic_set_source, \
		.clk_accessible = NULL, \
		.clk_get_source_freq = NULL, \
		.clk_set_source_freq = NULL \
	}

#define AM335X_GPIO_CLOCK_DEV(i) \
	{	.id = (i), \
		.clk_activate = am335x_clk_gpio_activate, \
		.clk_deactivate = am335x_clk_generic_deactivate, \
		.clk_set_source = am335x_clk_generic_set_source, \
		.clk_accessible = NULL, \
		.clk_get_source_freq = NULL, \
		.clk_set_source_freq = NULL \
	}

#define AM335X_MMCHS_CLOCK_DEV(i) \
	{	.id = (i), \
		.clk_activate = am335x_clk_generic_activate, \
		.clk_deactivate = am335x_clk_generic_deactivate, \
		.clk_set_source = am335x_clk_generic_set_source, \
		.clk_accessible = NULL, \
		.clk_get_source_freq = am335x_clk_hsmmc_get_source_freq, \
		.clk_set_source_freq = NULL \
	}

struct ti_clock_dev ti_am335x_clk_devmap[] = {
	/* System clocks */
	{	.id                  = SYS_CLK,
		.clk_activate        = NULL,
		.clk_deactivate      = NULL,
		.clk_set_source      = NULL,
		.clk_accessible      = NULL,
		.clk_get_source_freq = am335x_clk_get_sysclk_freq,
		.clk_set_source_freq = NULL,
	},
	/* MPU (ARM) core clocks */
	{	.id                  = MPU_CLK,
		.clk_activate        = NULL,
		.clk_deactivate      = NULL,
		.clk_set_source      = NULL,
		.clk_accessible      = NULL,
		.clk_get_source_freq = am335x_clk_get_arm_fclk_freq,
		.clk_set_source_freq = NULL,
	},
	/* CPSW Ethernet Switch core clocks */
	{	.id                  = CPSW_CLK,
		.clk_activate        = am335x_clk_cpsw_activate,
		.clk_deactivate      = NULL,
		.clk_set_source      = NULL,
		.clk_accessible      = NULL,
		.clk_get_source_freq = NULL,
		.clk_set_source_freq = NULL,
	},

	/* Mentor USB HS controller core clocks */
	{	.id                  = MUSB0_CLK,
		.clk_activate        = am335x_clk_musb0_activate,
		.clk_deactivate      = NULL,
		.clk_set_source      = NULL,
		.clk_accessible      = NULL,
		.clk_get_source_freq = NULL,
		.clk_set_source_freq = NULL,
	},

	/* LCD controller clocks */
	{	.id                  = LCDC_CLK,
		.clk_activate        = am335x_clk_lcdc_activate,
		.clk_deactivate      = NULL,
		.clk_set_source      = NULL,
		.clk_accessible      = NULL,
		.clk_get_source_freq = am335x_clk_get_arm_disp_freq,
		.clk_set_source_freq = am335x_clk_set_arm_disp_freq,
	},

        /* UART */
	AM335X_NOOP_CLOCK_DEV(UART1_CLK),
	AM335X_GENERIC_CLOCK_DEV(UART2_CLK),
	AM335X_GENERIC_CLOCK_DEV(UART3_CLK),
	AM335X_GENERIC_CLOCK_DEV(UART4_CLK),
	AM335X_GENERIC_CLOCK_DEV(UART5_CLK),
	AM335X_GENERIC_CLOCK_DEV(UART6_CLK),

	/* DMTimer */
	AM335X_GENERIC_CLOCK_DEV(TIMER2_CLK),
	AM335X_GENERIC_CLOCK_DEV(TIMER3_CLK),
	AM335X_GENERIC_CLOCK_DEV(TIMER4_CLK),
	AM335X_GENERIC_CLOCK_DEV(TIMER5_CLK),
	AM335X_GENERIC_CLOCK_DEV(TIMER6_CLK),
	AM335X_GENERIC_CLOCK_DEV(TIMER7_CLK),

	/* GPIO, we use hwmods as reference, not units in spec */
	AM335X_GPIO_CLOCK_DEV(GPIO1_CLK),
	AM335X_GPIO_CLOCK_DEV(GPIO2_CLK),
	AM335X_GPIO_CLOCK_DEV(GPIO3_CLK),
	AM335X_GPIO_CLOCK_DEV(GPIO4_CLK),

	/* I2C we use hwmods as reference, not units in spec */
	AM335X_GENERIC_CLOCK_DEV(I2C1_CLK),
	AM335X_GENERIC_CLOCK_DEV(I2C2_CLK),
	AM335X_GENERIC_CLOCK_DEV(I2C3_CLK),

	/* McSPI we use hwmods as reference, not units in spec */
	AM335X_GENERIC_CLOCK_DEV(SPI0_CLK),
	AM335X_GENERIC_CLOCK_DEV(SPI1_CLK),

	/* TSC_ADC */
	AM335X_GENERIC_CLOCK_DEV(TSC_ADC_CLK),

	/* EDMA */
	AM335X_GENERIC_CLOCK_DEV(EDMA_TPCC_CLK),
	AM335X_GENERIC_CLOCK_DEV(EDMA_TPTC0_CLK),
	AM335X_GENERIC_CLOCK_DEV(EDMA_TPTC1_CLK),
	AM335X_GENERIC_CLOCK_DEV(EDMA_TPTC2_CLK),

	/* MMCHS */
	AM335X_MMCHS_CLOCK_DEV(MMC1_CLK),
	AM335X_MMCHS_CLOCK_DEV(MMC2_CLK),
	AM335X_MMCHS_CLOCK_DEV(MMC3_CLK),

	/* PWMSS */
	AM335X_GENERIC_CLOCK_DEV(PWMSS0_CLK),
	AM335X_GENERIC_CLOCK_DEV(PWMSS1_CLK),
	AM335X_GENERIC_CLOCK_DEV(PWMSS2_CLK),

	/* System Mailbox clock */
	AM335X_GENERIC_CLOCK_DEV(MAILBOX0_CLK),

	/* SPINLOCK */
	AM335X_GENERIC_CLOCK_DEV(SPINLOCK0_CLK),

	/* PRU-ICSS */
	{	.id		     = PRUSS_CLK,
		.clk_activate	     = am335x_clk_pruss_activate,
		.clk_deactivate      = NULL,
		.clk_set_source      = NULL,
		.clk_accessible      = NULL,
		.clk_get_source_freq = NULL,
		.clk_set_source_freq = NULL,
	},

	/* RTC */
	AM335X_GENERIC_CLOCK_DEV(RTC_CLK),

	{  INVALID_CLK_IDENT, NULL, NULL, NULL, NULL }
};

struct am335x_clk_details {
	clk_ident_t	id;
	uint32_t	clkctrl_reg;
	uint32_t	clksel_reg;
};

#define _CLK_DETAIL(i, c, s) \
	{	.id = (i), \
		.clkctrl_reg = (c), \
		.clksel_reg = (s), \
	}

static struct am335x_clk_details g_am335x_clk_details[] = {

        /* UART. UART0 clock not controllable. */
	_CLK_DETAIL(UART1_CLK, 0, 0),
	_CLK_DETAIL(UART2_CLK, CM_PER_UART1_CLKCTRL, 0),
	_CLK_DETAIL(UART3_CLK, CM_PER_UART2_CLKCTRL, 0),
	_CLK_DETAIL(UART4_CLK, CM_PER_UART3_CLKCTRL, 0),
	_CLK_DETAIL(UART5_CLK, CM_PER_UART4_CLKCTRL, 0),
	_CLK_DETAIL(UART6_CLK, CM_PER_UART5_CLKCTRL, 0),

	/* DMTimer modules */
	_CLK_DETAIL(TIMER2_CLK, CM_PER_TIMER2_CLKCTRL, CLKSEL_TIMER2_CLK),
	_CLK_DETAIL(TIMER3_CLK, CM_PER_TIMER3_CLKCTRL, CLKSEL_TIMER3_CLK),
	_CLK_DETAIL(TIMER4_CLK, CM_PER_TIMER4_CLKCTRL, CLKSEL_TIMER4_CLK),
	_CLK_DETAIL(TIMER5_CLK, CM_PER_TIMER5_CLKCTRL, CLKSEL_TIMER5_CLK),
	_CLK_DETAIL(TIMER6_CLK, CM_PER_TIMER6_CLKCTRL, CLKSEL_TIMER6_CLK),
	_CLK_DETAIL(TIMER7_CLK, CM_PER_TIMER7_CLKCTRL, CLKSEL_TIMER7_CLK),

	/* GPIO modules, hwmods start with gpio1 */
	_CLK_DETAIL(GPIO1_CLK, CM_WKUP_GPIO0_CLKCTRL, 0),
	_CLK_DETAIL(GPIO2_CLK, CM_PER_GPIO1_CLKCTRL, 0),
	_CLK_DETAIL(GPIO3_CLK, CM_PER_GPIO2_CLKCTRL, 0),
	_CLK_DETAIL(GPIO4_CLK, CM_PER_GPIO3_CLKCTRL, 0),

	/* I2C modules, hwmods start with i2c1 */
	_CLK_DETAIL(I2C1_CLK, CM_WKUP_I2C0_CLKCTRL, 0),
	_CLK_DETAIL(I2C2_CLK, CM_PER_I2C1_CLKCTRL, 0),
	_CLK_DETAIL(I2C3_CLK, CM_PER_I2C2_CLKCTRL, 0),

	/* McSPI modules, hwmods start with spi0 */
	_CLK_DETAIL(SPI0_CLK, CM_PER_SPI0_CLKCTRL, 0),
	_CLK_DETAIL(SPI1_CLK, CM_PER_SPI1_CLKCTRL, 0),

	/* TSC_ADC module */
	_CLK_DETAIL(TSC_ADC_CLK, CM_WKUP_ADC_TSC_CLKCTRL, 0),

	/* EDMA modules */
	_CLK_DETAIL(EDMA_TPCC_CLK, CM_PER_TPCC_CLKCTRL, 0),
	_CLK_DETAIL(EDMA_TPTC0_CLK, CM_PER_TPTC0_CLKCTRL, 0),
	_CLK_DETAIL(EDMA_TPTC1_CLK, CM_PER_TPTC1_CLKCTRL, 0),
	_CLK_DETAIL(EDMA_TPTC2_CLK, CM_PER_TPTC2_CLKCTRL, 0),

	/* MMCHS modules, hwmods start with mmc1*/
	_CLK_DETAIL(MMC1_CLK, CM_PER_MMC0_CLKCTRL, 0),
	_CLK_DETAIL(MMC2_CLK, CM_PER_MMC1_CLKCTRL, 0),
	_CLK_DETAIL(MMC3_CLK, CM_PER_MMC1_CLKCTRL, 0),

	/* PWMSS modules */
	_CLK_DETAIL(PWMSS0_CLK, CM_PER_EPWMSS0_CLKCTRL, 0),
	_CLK_DETAIL(PWMSS1_CLK, CM_PER_EPWMSS1_CLKCTRL, 0),
	_CLK_DETAIL(PWMSS2_CLK, CM_PER_EPWMSS2_CLKCTRL, 0),

	_CLK_DETAIL(MAILBOX0_CLK, CM_PER_MAILBOX0_CLKCTRL, 0),
	_CLK_DETAIL(SPINLOCK0_CLK, CM_PER_SPINLOCK0_CLKCTRL, 0),

	/* RTC module */
	_CLK_DETAIL(RTC_CLK, CM_RTC_RTC_CLKCTRL, 0),

	{ INVALID_CLK_IDENT, 0},
};

/* Read/Write macros */
#define prcm_read_4(reg)		\
	bus_space_read_4(am335x_prcm_sc->bst, am335x_prcm_sc->bsh, reg)
#define prcm_write_4(reg, val)		\
	bus_space_write_4(am335x_prcm_sc->bst, am335x_prcm_sc->bsh, reg, val)

void am335x_prcm_setup_dmtimer(int);

static int
am335x_prcm_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "ti,am3-prcm")) {
		device_set_desc(dev, "AM335x Power and Clock Management");
		return(BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
am335x_prcm_attach(device_t dev)
{
	struct am335x_prcm_softc *sc = device_get_softc(dev);

	if (am335x_prcm_sc)
		return (ENXIO);

	if (bus_alloc_resources(dev, am335x_prcm_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	am335x_prcm_sc = sc;
	ti_cpu_reset = am335x_prcm_reset;

	return (0);
}

static void
am335x_prcm_new_pass(device_t dev)
{
	struct am335x_prcm_softc *sc = device_get_softc(dev);
	unsigned int sysclk, fclk;

	sc = device_get_softc(dev);
	if (sc->attach_done ||
	    bus_current_pass < (BUS_PASS_TIMER + BUS_PASS_ORDER_EARLY)) {
		bus_generic_new_pass(dev);
		return;
	}

	sc->attach_done = 1;

	if (am335x_clk_get_sysclk_freq(NULL, &sysclk) != 0)
		sysclk = 0;
	if (am335x_clk_get_arm_fclk_freq(NULL, &fclk) != 0)
		fclk = 0;
	if (sysclk && fclk)
		device_printf(dev, "Clocks: System %u.%01u MHz, CPU %u MHz\n",
		    sysclk/1000000, (sysclk % 1000000)/100000, fclk/1000000);
	else {
		device_printf(dev, "can't read frequencies yet (SCM device not ready?)\n");
		goto fail;
	}

	return;

fail:
	device_detach(dev);
	return;
}

static device_method_t am335x_prcm_methods[] = {
	DEVMETHOD(device_probe,		am335x_prcm_probe),
	DEVMETHOD(device_attach,	am335x_prcm_attach),

	/* Bus interface */
	DEVMETHOD(bus_new_pass,		am335x_prcm_new_pass),
	{ 0, 0 }
};

static driver_t am335x_prcm_driver = {
	"am335x_prcm",
	am335x_prcm_methods,
	sizeof(struct am335x_prcm_softc),
};

static devclass_t am335x_prcm_devclass;

EARLY_DRIVER_MODULE(am335x_prcm, simplebus, am335x_prcm_driver,
	am335x_prcm_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
MODULE_VERSION(am335x_prcm, 1);
MODULE_DEPEND(am335x_prcm, ti_scm, 1, 1, 1);

static struct am335x_clk_details*
am335x_clk_details(clk_ident_t id)
{
	struct am335x_clk_details *walker;

	for (walker = g_am335x_clk_details; walker->id != INVALID_CLK_IDENT; walker++) {
		if (id == walker->id)
			return (walker);
	}

	return NULL;
}

static int
am335x_clk_noop_activate(struct ti_clock_dev *clkdev)
{

	return (0);
}

static int
am335x_clk_generic_activate(struct ti_clock_dev *clkdev)
{
	struct am335x_prcm_softc *sc = am335x_prcm_sc;
	struct am335x_clk_details* clk_details;

	if (sc == NULL)
		return ENXIO;

	clk_details = am335x_clk_details(clkdev->id);

	if (clk_details == NULL)
		return (ENXIO);

	/* set *_CLKCTRL register MODULEMODE[1:0] to enable(2) */
	prcm_write_4(clk_details->clkctrl_reg, 2);
	while ((prcm_read_4(clk_details->clkctrl_reg) & 0x3) != 2)
		DELAY(10);

	return (0);
}

static int
am335x_clk_gpio_activate(struct ti_clock_dev *clkdev)
{
	struct am335x_prcm_softc *sc = am335x_prcm_sc;
	struct am335x_clk_details* clk_details;

	if (sc == NULL)
		return ENXIO;

	clk_details = am335x_clk_details(clkdev->id);

	if (clk_details == NULL)
		return (ENXIO);

	/* set *_CLKCTRL register MODULEMODE[1:0] to enable(2) */
	/* set *_CLKCTRL register OPTFCLKEN_GPIO_1_G DBCLK[18] to FCLK_EN(1) */
	prcm_write_4(clk_details->clkctrl_reg, 2 | (1 << 18));
	while ((prcm_read_4(clk_details->clkctrl_reg) &
	    (3 | (1 << 18) )) != (2 | (1 << 18)))
		DELAY(10);

	return (0);
}

static int
am335x_clk_noop_deactivate(struct ti_clock_dev *clkdev)
{

	return(0);
}

static int
am335x_clk_generic_deactivate(struct ti_clock_dev *clkdev)
{
	struct am335x_prcm_softc *sc = am335x_prcm_sc;
	struct am335x_clk_details* clk_details;

	if (sc == NULL)
		return ENXIO;

	clk_details = am335x_clk_details(clkdev->id);

	if (clk_details == NULL)
		return (ENXIO);

	/* set *_CLKCTRL register MODULEMODE[1:0] to disable(0) */
	prcm_write_4(clk_details->clkctrl_reg, 0);
	while ((prcm_read_4(clk_details->clkctrl_reg) & 0x3) != 0)
		DELAY(10);

	return (0);
}

static int
am335x_clk_noop_set_source(struct ti_clock_dev *clkdev, clk_src_t clksrc)
{

	return (0);
}

static int
am335x_clk_generic_set_source(struct ti_clock_dev *clkdev, clk_src_t clksrc)
{
	struct am335x_prcm_softc *sc = am335x_prcm_sc;
	struct am335x_clk_details* clk_details;
	uint32_t reg;

	if (sc == NULL)
		return ENXIO;

	clk_details = am335x_clk_details(clkdev->id);

	if (clk_details == NULL)
		return (ENXIO);

	switch (clksrc) {
		case EXT_CLK:
			reg = 0; /* SEL2: TCLKIN clock */
			break;
		case SYSCLK_CLK:
			reg = 1; /* SEL1: CLK_M_OSC clock */
			break;
		case F32KHZ_CLK:
			reg = 2; /* SEL3: CLK_32KHZ clock */
			break;
		default:
			return (ENXIO);
	}

	prcm_write_4(clk_details->clksel_reg, reg);
	while ((prcm_read_4(clk_details->clksel_reg) & 0x3) != reg)
		DELAY(10);

	return (0);
}

static int
am335x_clk_hsmmc_get_source_freq(struct ti_clock_dev *clkdev,  unsigned int *freq)
{
	*freq = 96000000;
	return (0);
}

static int
am335x_clk_get_sysclk_freq(struct ti_clock_dev *clkdev, unsigned int *freq)
{
	uint32_t ctrl_status;

	/* Read the input clock freq from the control module. */
	if (ti_scm_reg_read_4(SCM_CTRL_STATUS, &ctrl_status))
		return (ENXIO);

	switch ((ctrl_status>>22) & 0x3) {
	case 0x0:
		/* 19.2Mhz */
		*freq = 19200000;
		break;
	case 0x1:
		/* 24Mhz */
		*freq = 24000000;
		break;
	case 0x2:
		/* 25Mhz */
		*freq = 25000000;
		break;
	case 0x3:
		/* 26Mhz */
		*freq = 26000000;
		break;
	}

	return (0);
}

#define DPLL_BYP_CLKSEL(reg)	((reg>>23) & 1)
#define DPLL_DIV(reg)		((reg & 0x7f)+1)
#define DPLL_MULT(reg)		((reg>>8) & 0x7FF)
#define	DPLL_MAX_MUL		0x800
#define	DPLL_MAX_DIV		0x80

static int
am335x_clk_get_arm_fclk_freq(struct ti_clock_dev *clkdev, unsigned int *freq)
{
	uint32_t reg;
	uint32_t sysclk;

	reg = prcm_read_4(CM_WKUP_CM_CLKSEL_DPLL_MPU);

	/*Check if we are running in bypass */
	if (DPLL_BYP_CLKSEL(reg))
		return ENXIO;

	am335x_clk_get_sysclk_freq(NULL, &sysclk);
	*freq = DPLL_MULT(reg) * (sysclk / DPLL_DIV(reg));
	return(0);
}

static int
am335x_clk_get_arm_disp_freq(struct ti_clock_dev *clkdev, unsigned int *freq)
{
	uint32_t reg;
	uint32_t sysclk;

	reg = prcm_read_4(CM_WKUP_CM_CLKSEL_DPLL_DISP);

	/*Check if we are running in bypass */
	if (DPLL_BYP_CLKSEL(reg))
		return ENXIO;

	am335x_clk_get_sysclk_freq(NULL, &sysclk);
	*freq = DPLL_MULT(reg) * (sysclk / DPLL_DIV(reg));
	return(0);
}

static int
am335x_clk_set_arm_disp_freq(struct ti_clock_dev *clkdev, unsigned int freq)
{
	uint32_t sysclk;
	uint32_t mul, div;
	uint32_t i, j;
	unsigned int delta, min_delta;

	am335x_clk_get_sysclk_freq(NULL, &sysclk);

	/* Bypass mode */
	prcm_write_4(CM_WKUP_CM_CLKMODE_DPLL_DISP, 0x4);

	/* Make sure it's in bypass mode */
	while (!(prcm_read_4(CM_WKUP_CM_IDLEST_DPLL_DISP)
	    & (1 << 8)))
		DELAY(10);

	/* Dumb and non-optimal implementation */
	min_delta = freq;
	for (i = 1; i < DPLL_MAX_MUL; i++) {
		for (j = 1; j < DPLL_MAX_DIV; j++) {
			delta = abs(freq - i*(sysclk/j));
			if (delta < min_delta) {
				mul = i;
				div = j;
				min_delta = delta;
			}
			if (min_delta == 0)
				break;
		}
	}

	prcm_write_4(CM_WKUP_CM_CLKSEL_DPLL_DISP, (mul << 8) | (div - 1));

	/* Locked mode */
	prcm_write_4(CM_WKUP_CM_CLKMODE_DPLL_DISP, 0x7);

	int timeout = 10000;
	while ((!(prcm_read_4(CM_WKUP_CM_IDLEST_DPLL_DISP)
	    & (1 << 0))) && timeout--)
		DELAY(10);

	return(0);
}

static void
am335x_prcm_reset(void)
{
	prcm_write_4(PRM_RSTCTRL, (1<<1));
}

static int
am335x_clk_cpsw_activate(struct ti_clock_dev *clkdev)
{
	struct am335x_prcm_softc *sc = am335x_prcm_sc;

	if (sc == NULL)
		return ENXIO;

	/* set MODULENAME to ENABLE */
	prcm_write_4(CM_PER_CPGMAC0_CLKCTRL, 2);

	/* wait for IDLEST to become Func(0) */
	while(prcm_read_4(CM_PER_CPGMAC0_CLKCTRL) & (3<<16));

	/*set CLKTRCTRL to SW_WKUP(2) */
	prcm_write_4(CM_PER_CPSW_CLKSTCTRL, 2);

	/* wait for 125 MHz OCP clock to become active */
	while((prcm_read_4(CM_PER_CPSW_CLKSTCTRL) & (1<<4)) == 0);
	return(0);
}

static int
am335x_clk_musb0_activate(struct ti_clock_dev *clkdev)
{
	struct am335x_prcm_softc *sc = am335x_prcm_sc;

	if (sc == NULL)
		return ENXIO;

	/* set ST_DPLL_CLKDCOLDO(9) to CLK_GATED(1) */
	/* set DPLL_CLKDCOLDO_GATE_CTRL(8) to CLK_ENABLE(1)*/
        prcm_write_4(CM_WKUP_CM_CLKDCOLDO_DPLL_PER, 0x300);

	/*set MODULEMODE to ENABLE(2) */
	prcm_write_4(CM_PER_USB0_CLKCTRL, 2);

	/* wait for MODULEMODE to become ENABLE(2) */
	while ((prcm_read_4(CM_PER_USB0_CLKCTRL) & 0x3) != 2)
		DELAY(10);

	/* wait for IDLEST to become Func(0) */
	while(prcm_read_4(CM_PER_USB0_CLKCTRL) & (3<<16))
		DELAY(10);

	return(0);
}

static int
am335x_clk_lcdc_activate(struct ti_clock_dev *clkdev)
{
	struct am335x_prcm_softc *sc = am335x_prcm_sc;

	if (sc == NULL)
		return (ENXIO);

	/*
	 * For now set frequency to 2*VGA_PIXEL_CLOCK 
	 */
	am335x_clk_set_arm_disp_freq(clkdev, 25175000*2);

	/*set MODULEMODE to ENABLE(2) */
	prcm_write_4(CM_PER_LCDC_CLKCTRL, 2);

	/* wait for MODULEMODE to become ENABLE(2) */
	while ((prcm_read_4(CM_PER_LCDC_CLKCTRL) & 0x3) != 2)
		DELAY(10);

	/* wait for IDLEST to become Func(0) */
	while(prcm_read_4(CM_PER_LCDC_CLKCTRL) & (3<<16))
		DELAY(10);

	return (0);
}

static int
am335x_clk_pruss_activate(struct ti_clock_dev *clkdev)
{
	struct am335x_prcm_softc *sc = am335x_prcm_sc;

	if (sc == NULL)
		return (ENXIO);

	/* Set MODULEMODE to ENABLE(2) */
	prcm_write_4(CM_PER_PRUSS_CLKCTRL, 2);

	/* Wait for MODULEMODE to become ENABLE(2) */
	while ((prcm_read_4(CM_PER_PRUSS_CLKCTRL) & 0x3) != 2)
		DELAY(10);

	/* Set CLKTRCTRL to SW_WKUP(2) */
	prcm_write_4(CM_PER_PRUSS_CLKSTCTRL, 2);

	/* Wait for the 200 MHz OCP clock to become active */
	while ((prcm_read_4(CM_PER_PRUSS_CLKSTCTRL) & (1<<4)) == 0)
		DELAY(10);

	/* Wait for the 200 MHz IEP clock to become active */
	while ((prcm_read_4(CM_PER_PRUSS_CLKSTCTRL) & (1<<5)) == 0)
		DELAY(10);

	/* Wait for the 192 MHz UART clock to become active */
	while ((prcm_read_4(CM_PER_PRUSS_CLKSTCTRL) & (1<<6)) == 0)
		DELAY(10);

	/* Select L3F as OCP clock */
	prcm_write_4(CLKSEL_PRUSS_OCP_CLK, 0);
	while ((prcm_read_4(CLKSEL_PRUSS_OCP_CLK) & 0x3) != 0)
		DELAY(10);

	/* Clear the RESET bit */
	prcm_write_4(PRM_PER_RSTCTRL, prcm_read_4(PRM_PER_RSTCTRL) & ~2);

	return (0);
}
