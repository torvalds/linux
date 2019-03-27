/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 * 3. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BEN GRAY ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BEN GRAY BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <arm/arm/mpcore_timervar.h>
#include <arm/ti/tivar.h>
#include <arm/ti/ti_prcm.h>
#include <arm/ti/omap4/omap4_reg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

/*
 *	This file defines the clock configuration for the OMAP4xxx series of
 *	devices.
 *
 *	How This is Suppose to Work
 *	===========================
 *	- There is a top level omap_prcm module that defines all OMAP SoC drivers
 *	should use to enable/disable the system clocks regardless of the version
 *	of OMAP device they are running on.  This top level PRCM module is just
 *	a thin shim to chip specific functions that perform the donkey work of
 *	configuring the clock - this file is the 'donkey' for OMAP44xx devices.
 *
 *	- The key bit in this file is the omap_clk_devmap array, it's
 *	used by the omap_prcm driver to determine what clocks are valid and which
 *	functions to call to manipulate them.
 *
 *	- In essence you just need to define some callbacks for each of the
 *	clocks and then you're done.
 *
 *	- The other thing that is worth noting is that when the omap_prcm device
 *	is registered you typically pass in some memory ranges which are the
 *	SYS_MEMORY resources.  These resources are in turn allocated using 
 *	bus_allocate_resources(...) and the resource handles are passed to all
 *	individual clock callback handlers. 
 *
 *
 *
 *	OMAP4 devices are different from the previous OMAP3 devices in that there
 *	is no longer a separate functional and interface clock for each module,
 *	instead there is typically an interface clock that spans many modules.
 */

#define FREQ_96MHZ    96000000
#define FREQ_64MHZ    64000000
#define FREQ_48MHZ    48000000
#define FREQ_32KHZ    32000

#define PRM_INSTANCE    1
#define CM1_INSTANCE    2
#define CM2_INSTANCE    3

/**
 *	Address offsets from the PRM memory region to the top level clock control
 *	registers.
 */
#define CKGEN_PRM_OFFSET               0x00000100UL
#define MPU_PRM_OFFSET                 0x00000300UL
#define DSP_PRM_OFFSET                 0x00000400UL
#define ABE_PRM_OFFSET                 0x00000500UL
#define ALWAYS_ON_PRM_OFFSET           0x00000600UL
#define CORE_PRM_OFFSET                0x00000700UL
#define IVAHD_PRM_OFFSET               0x00000F00UL
#define CAM_PRM_OFFSET                 0x00001000UL
#define DSS_PRM_OFFSET                 0x00001100UL
#define SGX_PRM_OFFSET                 0x00001200UL
#define L3INIT_PRM_OFFSET              0x00001300UL
#define L4PER_PRM_OFFSET               0x00001400UL
#define WKUP_PRM_OFFSET                0x00001700UL
#define WKUP_CM_OFFSET                 0x00001800UL
#define EMU_PRM_OFFSET                 0x00001900UL
#define EMU_CM_OFFSET                  0x00001A00UL
#define DEVICE_PRM_OFFSET              0x00001B00UL
#define INSTR_PRM_OFFSET               0x00001F00UL

#define CM_ABE_DSS_SYS_CLKSEL_OFFSET   (CKGEN_PRM_OFFSET + 0x0000UL)
#define CM_L4_WKUP_CLKSELL_OFFSET      (CKGEN_PRM_OFFSET + 0x0008UL)
#define CM_ABE_PLL_REF_CLKSEL_OFFSET   (CKGEN_PRM_OFFSET + 0x000CUL)
#define CM_SYS_CLKSEL_OFFSET           (CKGEN_PRM_OFFSET + 0x0010UL)

/**
 *	Address offsets from the CM1 memory region to the top level clock control
 *	registers.
 */
#define CKGEN_CM1_OFFSET               0x00000100UL
#define MPU_CM1_OFFSET                 0x00000300UL
#define DSP_CM1_OFFSET                 0x00000400UL
#define ABE_CM1_OFFSET                 0x00000500UL
#define RESTORE_CM1_OFFSET             0x00000E00UL
#define INSTR_CM1_OFFSET               0x00000F00UL

#define CM_CLKSEL_DPLL_MPU             (CKGEN_CM1_OFFSET + 0x006CUL)

/**
 *	Address offsets from the CM2 memory region to the top level clock control
 *	registers.
 */
#define INTRCONN_SOCKET_CM2_OFFSET     0x00000000UL
#define CKGEN_CM2_OFFSET               0x00000100UL
#define ALWAYS_ON_CM2_OFFSET           0x00000600UL
#define CORE_CM2_OFFSET                0x00000700UL
#define IVAHD_CM2_OFFSET               0x00000F00UL
#define CAM_CM2_OFFSET                 0x00001000UL
#define DSS_CM2_OFFSET                 0x00001100UL
#define SGX_CM2_OFFSET                 0x00001200UL
#define L3INIT_CM2_OFFSET              0x00001300UL
#define L4PER_CM2_OFFSET               0x00001400UL
#define RESTORE_CM2_OFFSET             0x00001E00UL
#define INSTR_CM2_OFFSET               0x00001F00UL

#define CLKCTRL_MODULEMODE_MASK       0x00000003UL
#define CLKCTRL_MODULEMODE_DISABLE    0x00000000UL
#define CLKCTRL_MODULEMODE_AUTO       0x00000001UL
#define CLKCTRL_MODULEMODE_ENABLE     0x00000001UL

#define CLKCTRL_IDLEST_MASK           0x00030000UL
#define CLKCTRL_IDLEST_ENABLED        0x00000000UL
#define CLKCTRL_IDLEST_WAKING         0x00010000UL
#define CLKCTRL_IDLEST_IDLE           0x00020000UL
#define CLKCTRL_IDLEST_DISABLED       0x00030000UL

static struct ofw_compat_data compat_data[] = {
	{"ti,omap4-cm1",	(uintptr_t)CM1_INSTANCE},
	{"ti,omap4-cm2",	(uintptr_t)CM2_INSTANCE},
	{"ti,omap4-prm",	(uintptr_t)PRM_INSTANCE},
	{NULL,			(uintptr_t)0},
};

struct omap4_prcm_softc {
	struct resource	*sc_res;
	int		sc_rid;
	int		sc_instance;
	int		attach_done;
};

static int omap4_clk_generic_activate(struct ti_clock_dev *clkdev);
static int omap4_clk_generic_deactivate(struct ti_clock_dev *clkdev);
static int omap4_clk_generic_accessible(struct ti_clock_dev *clkdev);
static int omap4_clk_generic_set_source(struct ti_clock_dev *clkdev, clk_src_t clksrc);
static int omap4_clk_generic_get_source_freq(struct ti_clock_dev *clkdev, unsigned int *freq);

static int omap4_clk_gptimer_set_source(struct ti_clock_dev *clkdev, clk_src_t clksrc);
static int omap4_clk_gptimer_get_source_freq(struct ti_clock_dev *clkdev, unsigned int *freq);

static int omap4_clk_hsmmc_set_source(struct ti_clock_dev *clkdev, clk_src_t clksrc);
static int omap4_clk_hsmmc_get_source_freq(struct ti_clock_dev *clkdev, unsigned int *freq);

static int omap4_clk_hsusbhost_set_source(struct ti_clock_dev *clkdev, clk_src_t clksrc);
static int omap4_clk_hsusbhost_activate(struct ti_clock_dev *clkdev);
static int omap4_clk_hsusbhost_deactivate(struct ti_clock_dev *clkdev);
static int omap4_clk_hsusbhost_accessible(struct ti_clock_dev *clkdev);

static int omap4_clk_get_sysclk_freq(struct ti_clock_dev *clkdev, unsigned int *freq);
static int omap4_clk_get_arm_fclk_freq(struct ti_clock_dev *clkdev, unsigned int *freq);

/**
 *	omap_clk_devmap - Array of clock devices available on OMAP4xxx devices
 *
 *	This map only defines which clocks are valid and the callback functions
 *	for clock activate, deactivate, etc.  It is used by the top level omap_prcm
 *	driver.
 *
 *	The actual details of the clocks (config registers, bit fields, sources,
 *	etc) are in the private g_omap3_clk_details array below.
 *
 */

#define OMAP4_GENERIC_CLOCK_DEV(i) \
	{	.id = (i), \
		.clk_activate = omap4_clk_generic_activate, \
		.clk_deactivate = omap4_clk_generic_deactivate, \
		.clk_set_source = omap4_clk_generic_set_source, \
		.clk_accessible = omap4_clk_generic_accessible, \
		.clk_get_source_freq = omap4_clk_generic_get_source_freq, \
		.clk_set_source_freq = NULL \
	}

#define OMAP4_GPTIMER_CLOCK_DEV(i) \
	{	.id = (i), \
		.clk_activate = omap4_clk_generic_activate, \
		.clk_deactivate = omap4_clk_generic_deactivate, \
		.clk_set_source = omap4_clk_gptimer_set_source, \
		.clk_accessible = omap4_clk_generic_accessible, \
		.clk_get_source_freq = omap4_clk_gptimer_get_source_freq, \
		.clk_set_source_freq = NULL \
	}

#define OMAP4_HSMMC_CLOCK_DEV(i) \
	{	.id = (i), \
		.clk_activate = omap4_clk_generic_activate, \
		.clk_deactivate = omap4_clk_generic_deactivate, \
		.clk_set_source = omap4_clk_hsmmc_set_source, \
		.clk_accessible = omap4_clk_generic_accessible, \
		.clk_get_source_freq = omap4_clk_hsmmc_get_source_freq, \
		.clk_set_source_freq = NULL \
	}

#define OMAP4_HSUSBHOST_CLOCK_DEV(i) \
	{	.id = (i), \
		.clk_activate = omap4_clk_hsusbhost_activate, \
		.clk_deactivate = omap4_clk_hsusbhost_deactivate, \
		.clk_set_source = omap4_clk_hsusbhost_set_source, \
		.clk_accessible = omap4_clk_hsusbhost_accessible, \
		.clk_get_source_freq = NULL, \
		.clk_set_source_freq = NULL \
	}


struct ti_clock_dev ti_omap4_clk_devmap[] = {

	/* System clocks */
	{	.id                  = SYS_CLK,
		.clk_activate        = NULL,
		.clk_deactivate      = NULL,
		.clk_set_source      = NULL,
		.clk_accessible      = NULL,
		.clk_get_source_freq = omap4_clk_get_sysclk_freq,
		.clk_set_source_freq = NULL,
	},
	/* MPU (ARM) core clocks */
	{	.id                  = MPU_CLK,
		.clk_activate        = NULL,
		.clk_deactivate      = NULL,
		.clk_set_source      = NULL,
		.clk_accessible      = NULL,
		.clk_get_source_freq = omap4_clk_get_arm_fclk_freq,
		.clk_set_source_freq = NULL,
	},


	/* UART device clocks */
	OMAP4_GENERIC_CLOCK_DEV(UART1_CLK),
	OMAP4_GENERIC_CLOCK_DEV(UART2_CLK),
	OMAP4_GENERIC_CLOCK_DEV(UART3_CLK),
	OMAP4_GENERIC_CLOCK_DEV(UART4_CLK),
	
	/* Timer device source clocks */
	OMAP4_GPTIMER_CLOCK_DEV(TIMER1_CLK),
	OMAP4_GPTIMER_CLOCK_DEV(TIMER2_CLK),
	OMAP4_GPTIMER_CLOCK_DEV(TIMER3_CLK),
	OMAP4_GPTIMER_CLOCK_DEV(TIMER4_CLK),
	OMAP4_GPTIMER_CLOCK_DEV(TIMER5_CLK),
	OMAP4_GPTIMER_CLOCK_DEV(TIMER6_CLK),
	OMAP4_GPTIMER_CLOCK_DEV(TIMER7_CLK),
	OMAP4_GPTIMER_CLOCK_DEV(TIMER8_CLK),
	OMAP4_GPTIMER_CLOCK_DEV(TIMER9_CLK),
	OMAP4_GPTIMER_CLOCK_DEV(TIMER10_CLK),
	OMAP4_GPTIMER_CLOCK_DEV(TIMER11_CLK),
	
	/* MMC device clocks (MMC1 and MMC2 can have different input clocks) */
	OMAP4_HSMMC_CLOCK_DEV(MMC1_CLK),
	OMAP4_HSMMC_CLOCK_DEV(MMC2_CLK),
	OMAP4_GENERIC_CLOCK_DEV(MMC3_CLK),
	OMAP4_GENERIC_CLOCK_DEV(MMC4_CLK),
	OMAP4_GENERIC_CLOCK_DEV(MMC5_CLK),

	/* USB HS (high speed TLL, EHCI and OHCI) */
	OMAP4_HSUSBHOST_CLOCK_DEV(USBTLL_CLK),
	OMAP4_HSUSBHOST_CLOCK_DEV(USBHSHOST_CLK),
	OMAP4_HSUSBHOST_CLOCK_DEV(USBFSHOST_CLK),
	OMAP4_HSUSBHOST_CLOCK_DEV(USBP1_PHY_CLK),
	OMAP4_HSUSBHOST_CLOCK_DEV(USBP2_PHY_CLK),
	OMAP4_HSUSBHOST_CLOCK_DEV(USBP1_UTMI_CLK),
	OMAP4_HSUSBHOST_CLOCK_DEV(USBP2_UTMI_CLK),
	OMAP4_HSUSBHOST_CLOCK_DEV(USBP1_HSIC_CLK),
	OMAP4_HSUSBHOST_CLOCK_DEV(USBP2_HSIC_CLK),
	
	/* GPIO */
	OMAP4_GENERIC_CLOCK_DEV(GPIO1_CLK),
	OMAP4_GENERIC_CLOCK_DEV(GPIO2_CLK),
	OMAP4_GENERIC_CLOCK_DEV(GPIO3_CLK),
	OMAP4_GENERIC_CLOCK_DEV(GPIO4_CLK),
	OMAP4_GENERIC_CLOCK_DEV(GPIO5_CLK),
	OMAP4_GENERIC_CLOCK_DEV(GPIO6_CLK),
	
	/* sDMA */
	OMAP4_GENERIC_CLOCK_DEV(SDMA_CLK),	

	/* I2C */
	OMAP4_GENERIC_CLOCK_DEV(I2C1_CLK),
	OMAP4_GENERIC_CLOCK_DEV(I2C2_CLK),
	OMAP4_GENERIC_CLOCK_DEV(I2C3_CLK),
	OMAP4_GENERIC_CLOCK_DEV(I2C4_CLK),

	{  INVALID_CLK_IDENT, NULL, NULL, NULL, NULL }
};

/**
 *	omap4_clk_details - Stores details for all the different clocks supported
 *
 *	Whenever an operation on a clock is being performed (activated, deactivated,
 *	etc) this array is looked up to find the correct register and bit(s) we
 *	should be modifying.
 *
 */
struct omap4_clk_details {
	clk_ident_t id;
	
	uint32_t    instance;
	uint32_t    clksel_reg;
	
	int32_t     src_freq;
	
	uint32_t    enable_mode;
};

#define OMAP4_GENERIC_CLOCK_DETAILS(i, f, di, r, e) \
	{	.id = (i), \
		.instance = (di), \
		.clksel_reg = (r), \
		.src_freq = (f), \
		.enable_mode = (e), \
	}

static struct omap4_clk_details g_omap4_clk_details[] = {

	/* UART */
	OMAP4_GENERIC_CLOCK_DETAILS(UART1_CLK, FREQ_48MHZ, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x0140), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(UART2_CLK, FREQ_48MHZ, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x0148), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(UART3_CLK, FREQ_48MHZ, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x0150), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(UART4_CLK, FREQ_48MHZ, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x0158), CLKCTRL_MODULEMODE_ENABLE),

	/* General purpose timers */
	OMAP4_GENERIC_CLOCK_DETAILS(TIMER1_CLK,  -1, PRM_INSTANCE,
		(WKUP_CM_OFFSET + 0x040), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(TIMER2_CLK,  -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x038), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(TIMER3_CLK,  -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x040), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(TIMER4_CLK,  -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x048), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(TIMER5_CLK,  -1, CM1_INSTANCE,
		(ABE_CM1_OFFSET + 0x068), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(TIMER6_CLK,  -1, CM1_INSTANCE,
		(ABE_CM1_OFFSET + 0x070), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(TIMER7_CLK,  -1, CM1_INSTANCE,
		(ABE_CM1_OFFSET + 0x078), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(TIMER8_CLK,  -1, CM1_INSTANCE,
		(ABE_CM1_OFFSET + 0x080), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(TIMER9_CLK,  -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x050), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(TIMER10_CLK, -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x028), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(TIMER11_CLK, -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x030), CLKCTRL_MODULEMODE_ENABLE),

	/* HSMMC (MMC1 and MMC2 can have different input clocks) */
	OMAP4_GENERIC_CLOCK_DETAILS(MMC1_CLK, -1, CM2_INSTANCE,
		(L3INIT_CM2_OFFSET + 0x028), /*CLKCTRL_MODULEMODE_ENABLE*/2),
	OMAP4_GENERIC_CLOCK_DETAILS(MMC2_CLK, -1, CM2_INSTANCE,
		(L3INIT_CM2_OFFSET + 0x030), /*CLKCTRL_MODULEMODE_ENABLE*/2),
	OMAP4_GENERIC_CLOCK_DETAILS(MMC3_CLK, FREQ_48MHZ, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x120), /*CLKCTRL_MODULEMODE_ENABLE*/2),
	OMAP4_GENERIC_CLOCK_DETAILS(MMC4_CLK, FREQ_48MHZ, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x128), /*CLKCTRL_MODULEMODE_ENABLE*/2),
	OMAP4_GENERIC_CLOCK_DETAILS(MMC5_CLK, FREQ_48MHZ, CM2_INSTANCE,
	       (L4PER_CM2_OFFSET + 0x160), /*CLKCTRL_MODULEMODE_ENABLE*/1),
	
	/* GPIO modules */
	OMAP4_GENERIC_CLOCK_DETAILS(GPIO1_CLK, -1, PRM_INSTANCE,
		(WKUP_CM_OFFSET + 0x038), CLKCTRL_MODULEMODE_AUTO),
	OMAP4_GENERIC_CLOCK_DETAILS(GPIO2_CLK, -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x060), CLKCTRL_MODULEMODE_AUTO),
	OMAP4_GENERIC_CLOCK_DETAILS(GPIO3_CLK, -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x068), CLKCTRL_MODULEMODE_AUTO),
	OMAP4_GENERIC_CLOCK_DETAILS(GPIO4_CLK, -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x070), CLKCTRL_MODULEMODE_AUTO),
	OMAP4_GENERIC_CLOCK_DETAILS(GPIO5_CLK, -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x078), CLKCTRL_MODULEMODE_AUTO),
	OMAP4_GENERIC_CLOCK_DETAILS(GPIO6_CLK, -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x080), CLKCTRL_MODULEMODE_AUTO),
		
	/* sDMA block */
	OMAP4_GENERIC_CLOCK_DETAILS(SDMA_CLK, -1, CM2_INSTANCE,
		(CORE_CM2_OFFSET + 0x300), CLKCTRL_MODULEMODE_AUTO),

	/* I2C modules */
	OMAP4_GENERIC_CLOCK_DETAILS(I2C1_CLK, -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x0A0), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(I2C2_CLK, -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x0A8), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(I2C3_CLK, -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x0B0), CLKCTRL_MODULEMODE_ENABLE),
	OMAP4_GENERIC_CLOCK_DETAILS(I2C4_CLK, -1, CM2_INSTANCE,
		(L4PER_CM2_OFFSET + 0x0B8), CLKCTRL_MODULEMODE_ENABLE),

	{ INVALID_CLK_IDENT, 0, 0, 0, 0 },
};

/**
 *	MAX_MODULE_ENABLE_WAIT - the number of loops to wait for the module to come
 *	alive.
 *
 */
#define MAX_MODULE_ENABLE_WAIT    100
	
/**
 *	ARRAY_SIZE - Macro to return the number of elements in a static const array.
 *
 */
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

/**
 *	omap4_clk_details - writes a 32-bit value to one of the timer registers
 *	@timer: Timer device context
 *	@off: The offset of a register from the timer register address range
 *	@val: The value to write into the register
 *
 *
 *	RETURNS:
 *	nothing
 */
static struct omap4_clk_details*
omap4_clk_details(clk_ident_t id)
{
	struct omap4_clk_details *walker;

	for (walker = g_omap4_clk_details; walker->id != INVALID_CLK_IDENT; walker++) {
		if (id == walker->id)
			return (walker);
	}

	return NULL;
}

static struct omap4_prcm_softc *
omap4_prcm_get_instance_softc(int module_instance)
{
	int i, maxunit;
	devclass_t prcm_devclass;
	device_t dev;
	struct omap4_prcm_softc *sc;

	prcm_devclass = devclass_find("omap4_prcm");
	maxunit = devclass_get_maxunit(prcm_devclass);

	for (i = 0; i < maxunit; i++) {
		dev = devclass_get_device(prcm_devclass, i);
		sc = device_get_softc(dev);
		if (sc->sc_instance == module_instance)
			return (sc);
	}

	return (NULL);
}
	
/**
 *	omap4_clk_generic_activate - checks if a module is accessible
 *	@module: identifier for the module to check, see omap3_prcm.h for a list
 *	         of possible modules.
 *	         Example: OMAP3_MODULE_MMC1
 *	
 *	
 *
 *	LOCKING:
 *	Inherits the locks from the omap_prcm driver, no internal locking.
 *
 *	RETURNS:
 *	Returns 0 on success or a positive error code on failure.
 */
static int
omap4_clk_generic_activate(struct ti_clock_dev *clkdev)
{
	struct omap4_prcm_softc *sc;
	struct omap4_clk_details* clk_details;
	struct resource* clk_mem_res;
	uint32_t clksel;
	unsigned int i;
	clk_details = omap4_clk_details(clkdev->id);

	if (clk_details == NULL)
		return (ENXIO);

	sc = omap4_prcm_get_instance_softc(clk_details->instance);
	if (sc == NULL)
		return ENXIO;

	clk_mem_res = sc->sc_res;

	if (clk_mem_res == NULL)
		return (EINVAL);
	
	/* All the 'generic' clocks have a CLKCTRL register which is more or less
	 * generic - the have at least two fielda called MODULEMODE and IDLEST.
	 */
	clksel = bus_read_4(clk_mem_res, clk_details->clksel_reg);
	clksel &= ~CLKCTRL_MODULEMODE_MASK;
	clksel |=  clk_details->enable_mode;
	bus_write_4(clk_mem_res, clk_details->clksel_reg, clksel);

	/* Now poll on the IDLEST register to tell us if the module has come up.
	 * TODO: We need to take into account the parent clocks.
	 */
	
	/* Try MAX_MODULE_ENABLE_WAIT number of times to check if enabled */
	for (i = 0; i < MAX_MODULE_ENABLE_WAIT; i++) {
		clksel = bus_read_4(clk_mem_res, clk_details->clksel_reg);
		if ((clksel & CLKCTRL_IDLEST_MASK) == CLKCTRL_IDLEST_ENABLED)
			break;
		DELAY(10);
	}
		
	/* Check the enabled state */
	if ((clksel & CLKCTRL_IDLEST_MASK) != CLKCTRL_IDLEST_ENABLED) {
		printf("Error: failed to enable module with clock %d\n", clkdev->id);
		printf("Error: 0x%08x => 0x%08x\n", clk_details->clksel_reg, clksel);
		return (ETIMEDOUT);
	}
	
	return (0);
}

/**
 *	omap4_clk_generic_deactivate - checks if a module is accessible
 *	@module: identifier for the module to check, see omap3_prcm.h for a list
 *	         of possible modules.
 *	         Example: OMAP3_MODULE_MMC1
 *	
 *	
 *
 *	LOCKING:
 *	Inherits the locks from the omap_prcm driver, no internal locking.
 *
 *	RETURNS:
 *	Returns 0 on success or a positive error code on failure.
 */
static int
omap4_clk_generic_deactivate(struct ti_clock_dev *clkdev)
{
	struct omap4_prcm_softc *sc;
	struct omap4_clk_details* clk_details;
	struct resource* clk_mem_res;
	uint32_t clksel;

	clk_details = omap4_clk_details(clkdev->id);

	if (clk_details == NULL)
		return (ENXIO);

	sc = omap4_prcm_get_instance_softc(clk_details->instance);
	if (sc == NULL)
		return ENXIO;

	clk_mem_res = sc->sc_res;

	if (clk_mem_res == NULL)
		return (EINVAL);
	
	/* All the 'generic' clocks have a CLKCTRL register which is more or less
	 * generic - the have at least two fielda called MODULEMODE and IDLEST.
	 */
	clksel = bus_read_4(clk_mem_res, clk_details->clksel_reg);
	clksel &= ~CLKCTRL_MODULEMODE_MASK;
	clksel |=  CLKCTRL_MODULEMODE_DISABLE;
	bus_write_4(clk_mem_res, clk_details->clksel_reg, clksel);

	return (0);
}

/**
 *	omap4_clk_generic_set_source - checks if a module is accessible
 *	@module: identifier for the module to check, see omap3_prcm.h for a list
 *	         of possible modules.
 *	         Example: OMAP3_MODULE_MMC1
 *	
 *	
 *
 *	LOCKING:
 *	Inherits the locks from the omap_prcm driver, no internal locking.
 *
 *	RETURNS:
 *	Returns 0 on success or a positive error code on failure.
 */
static int
omap4_clk_generic_set_source(struct ti_clock_dev *clkdev,
                             clk_src_t clksrc)
{

	return (0);
}

/**
 *	omap4_clk_generic_accessible - checks if a module is accessible
 *	@module: identifier for the module to check, see omap3_prcm.h for a list
 *	         of possible modules.
 *	         Example: OMAP3_MODULE_MMC1
 *	
 *	
 *
 *	LOCKING:
 *	Inherits the locks from the omap_prcm driver, no internal locking.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code on failure.
 */
static int
omap4_clk_generic_accessible(struct ti_clock_dev *clkdev)
{
	struct omap4_prcm_softc *sc;
	struct omap4_clk_details* clk_details;
	struct resource* clk_mem_res;
	uint32_t clksel;

	clk_details = omap4_clk_details(clkdev->id);

	if (clk_details == NULL)
		return (ENXIO);

	sc = omap4_prcm_get_instance_softc(clk_details->instance);
	if (sc == NULL)
		return ENXIO;

	clk_mem_res = sc->sc_res;

	if (clk_mem_res == NULL)
		return (EINVAL);
	
	clksel = bus_read_4(clk_mem_res, clk_details->clksel_reg);
		
	/* Check the enabled state */
	if ((clksel & CLKCTRL_IDLEST_MASK) != CLKCTRL_IDLEST_ENABLED)
		return (0);
	
	return (1);
}

/**
 *	omap4_clk_generic_get_source_freq - checks if a module is accessible
 *	@module: identifier for the module to check, see omap3_prcm.h for a list
 *	         of possible modules.
 *	         Example: OMAP3_MODULE_MMC1
 *	
 *	
 *
 *	LOCKING:
 *	Inherits the locks from the omap_prcm driver, no internal locking.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code on failure.
 */
static int
omap4_clk_generic_get_source_freq(struct ti_clock_dev *clkdev,
                                  unsigned int *freq
                                  )
{
	struct omap4_clk_details* clk_details = omap4_clk_details(clkdev->id);

	if (clk_details == NULL)
		return (ENXIO);
	
	/* Simply return the stored frequency */
	if (freq)
		*freq = (unsigned int)clk_details->src_freq;
	
	return (0);
}


/**
 *	omap4_clk_gptimer_set_source - checks if a module is accessible
 *	@module: identifier for the module to check, see omap3_prcm.h for a list
 *	         of possible modules.
 *	         Example: OMAP3_MODULE_MMC1
 *	
 *	
 *
 *	LOCKING:
 *	Inherits the locks from the omap_prcm driver, no internal locking.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code on failure.
 */
static int
omap4_clk_gptimer_set_source(struct ti_clock_dev *clkdev,
                             clk_src_t clksrc)
{
	struct omap4_prcm_softc *sc;
	struct omap4_clk_details* clk_details;
	struct resource* clk_mem_res;

	clk_details = omap4_clk_details(clkdev->id);

	if (clk_details == NULL)
		return (ENXIO);

	sc = omap4_prcm_get_instance_softc(clk_details->instance);
	if (sc == NULL)
		return ENXIO;

	clk_mem_res = sc->sc_res;

	if (clk_mem_res == NULL)
		return (EINVAL);
	
	/* TODO: Implement */
	
	return (0);
}

/**
 *	omap4_clk_gptimer_get_source_freq - checks if a module is accessible
 *	@module: identifier for the module to check, see omap3_prcm.h for a list
 *	         of possible modules.
 *	         Example: OMAP3_MODULE_MMC1
 *	
 *	
 *
 *	LOCKING:
 *	Inherits the locks from the omap_prcm driver, no internal locking.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code on failure.
 */
static int
omap4_clk_gptimer_get_source_freq(struct ti_clock_dev *clkdev,
                                  unsigned int *freq
                                  )
{
	struct omap4_prcm_softc *sc;
	struct omap4_clk_details* clk_details;
	struct resource* clk_mem_res;
	uint32_t clksel;
	unsigned int src_freq;

	clk_details = omap4_clk_details(clkdev->id);

	if (clk_details == NULL)
		return (ENXIO);

	sc = omap4_prcm_get_instance_softc(clk_details->instance);
	if (sc == NULL)
		return ENXIO;

	clk_mem_res = sc->sc_res;

	if (clk_mem_res == NULL)
		return (EINVAL);
	
	/* Need to read the CLKSEL field to determine the clock source */
	clksel = bus_read_4(clk_mem_res, clk_details->clksel_reg);
	if (clksel & (0x1UL << 24))
		src_freq = FREQ_32KHZ;
	else
		omap4_clk_get_sysclk_freq(NULL, &src_freq);
	
	/* Return the frequency */
	if (freq)
		*freq = src_freq;
	
	return (0);
}

/**
 *	omap4_clk_hsmmc_set_source - sets the source clock (freq)
 *	@clkdev: pointer to the clockdev structure (id field will contain clock id)
 *	
 *	The MMC 1 and 2 clocks can be source from either a 64MHz or 96MHz clock.
 *
 *	LOCKING:
 *	Inherits the locks from the omap_prcm driver, no internal locking.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code on failure.
 */
static int
omap4_clk_hsmmc_set_source(struct ti_clock_dev *clkdev,
                           clk_src_t clksrc)
{
	struct omap4_prcm_softc *sc;
	struct omap4_clk_details* clk_details;
	struct resource* clk_mem_res;
	uint32_t clksel;

	clk_details = omap4_clk_details(clkdev->id);

	if (clk_details == NULL)
		return (ENXIO);


	sc = omap4_prcm_get_instance_softc(clk_details->instance);
	if (sc == NULL)
		return ENXIO;

	clk_mem_res = sc->sc_res;

	if (clk_mem_res == NULL)
		return (EINVAL);
		
	/* For MMC modules 3, 4 & 5 you can't change the freq, it's always 48MHz */
	if ((clkdev->id == MMC3_CLK) || (clkdev->id == MMC4_CLK) ||
	    (clkdev->id == MMC5_CLK)) {
		if (clksrc != F48MHZ_CLK)
			return (EINVAL);
		return 0;
	}

	
	clksel = bus_read_4(clk_mem_res, clk_details->clksel_reg);

	/* Bit 24 is set if 96MHz clock or cleared for 64MHz clock */
	if (clksrc == F64MHZ_CLK)
		clksel &= ~(0x1UL << 24);
	else if (clksrc == F96MHZ_CLK)
		clksel |= (0x1UL << 24);
	else
		return (EINVAL);
		
	bus_write_4(clk_mem_res, clk_details->clksel_reg, clksel);
	
	return (0);
}

/**
 *	omap4_clk_hsmmc_get_source_freq - checks if a module is accessible
 *	@clkdev: pointer to the clockdev structure (id field will contain clock id)
 *	
 *	
 *
 *	LOCKING:
 *	Inherits the locks from the omap_prcm driver, no internal locking.
 *
 *	RETURNS:
 *	Returns 0 on success or a negative error code on failure.
 */
static int
omap4_clk_hsmmc_get_source_freq(struct ti_clock_dev *clkdev,
                                unsigned int *freq
                                )
{
	struct omap4_prcm_softc *sc;
	struct omap4_clk_details* clk_details;
	struct resource* clk_mem_res;
	uint32_t clksel;
	unsigned int src_freq;

	clk_details = omap4_clk_details(clkdev->id);

	if (clk_details == NULL)
		return (ENXIO);

	sc = omap4_prcm_get_instance_softc(clk_details->instance);
	if (sc == NULL)
		return ENXIO;

	clk_mem_res = sc->sc_res;

	if (clk_mem_res == NULL)
		return (EINVAL);
	
	switch (clkdev->id) {
	case MMC1_CLK:
	case MMC2_CLK:
		/* Need to read the CLKSEL field to determine the clock source */
		clksel = bus_read_4(clk_mem_res, clk_details->clksel_reg);
		if (clksel & (0x1UL << 24))
			src_freq = FREQ_96MHZ;
		else
			src_freq = FREQ_64MHZ;
		break;
	case MMC3_CLK:
	case MMC4_CLK:
	case MMC5_CLK:
		src_freq = FREQ_48MHZ;
		break;
	default:
		return (EINVAL);
	}
		
	/* Return the frequency */
	if (freq)
		*freq = src_freq;
	
	return (0);
}

/**
 *	omap4_clk_get_sysclk_freq - gets the sysclk frequency
 *	@sc: pointer to the clk module/device context
 *
 *	Read the clocking information from the power-control/boot-strap registers,
 *  and stored in two global variables.
 *
 *	RETURNS:
 *	nothing, values are saved in global variables
 */
static int
omap4_clk_get_sysclk_freq(struct ti_clock_dev *clkdev,
                          unsigned int *freq)
{
	uint32_t clksel;
	uint32_t sysclk;
	struct omap4_prcm_softc *sc;
	
	sc = omap4_prcm_get_instance_softc(PRM_INSTANCE);
	if (sc == NULL)
		return ENXIO;

	/* Read the input clock freq from the configuration register (CM_SYS_CLKSEL) */
	clksel = bus_read_4(sc->sc_res, CM_SYS_CLKSEL_OFFSET);
	switch (clksel & 0x7) {
	case 0x1:
		/* 12Mhz */
		sysclk = 12000000;
		break;
	case 0x3:
		/* 16.8Mhz */
		sysclk = 16800000;
		break;
	case 0x4:
		/* 19.2Mhz */
		sysclk = 19200000;
		break;
	case 0x5:
		/* 26Mhz */
		sysclk = 26000000;
		break;
	case 0x7:
		/* 38.4Mhz */
		sysclk = 38400000;
		break;
	default:
		panic("%s: Invalid clock freq", __func__);
	}

	/* Return the value */
	if (freq)
		*freq = sysclk;
		
	return (0);
}

/**
 *	omap4_clk_get_arm_fclk_freq - gets the MPU clock frequency
 *	@clkdev: ignored
 *	@freq: pointer which upon return will contain the freq in hz
 *	@mem_res: array of allocated memory resources
 *
 *	Reads the frequency setting information registers and returns the value
 *	in the freq variable.
 *
 *	RETURNS:
 *	returns 0 on success, a positive error code on failure.
 */
static int
omap4_clk_get_arm_fclk_freq(struct ti_clock_dev *clkdev,
                            unsigned int *freq)
{
	uint32_t clksel;
	uint32_t pll_mult, pll_div;
	uint32_t mpuclk, sysclk;
	struct omap4_prcm_softc *sc;

	sc = omap4_prcm_get_instance_softc(CM1_INSTANCE);
	if (sc == NULL)
		return ENXIO;

	/* Read the clksel register which contains the DPLL multiple and divide
	 * values.  These are applied to the sysclk.
	 */
	clksel = bus_read_4(sc->sc_res, CM_CLKSEL_DPLL_MPU);

	pll_mult = ((clksel >> 8) & 0x7ff);
	pll_div = (clksel & 0x7f) + 1;
	
	
	/* Get the system clock freq */
	omap4_clk_get_sysclk_freq(NULL, &sysclk);


	/* Calculate the MPU freq */
	mpuclk = ((uint64_t)sysclk * pll_mult) / pll_div;

	/* Return the value */
	if (freq)
		*freq = mpuclk;
		
	return (0);
}

/**
 *	omap4_clk_hsusbhost_activate - activates the USB clocks for the given module
 *	@clkdev: pointer to the clock device structure.
 *	@mem_res: array of memory resources allocated by the top level PRCM driver.
 *	
 *	The USB clocking setup seems to be a bit more tricky than the other modules,
 *	to start with the clocking diagram for the HS host module shows 13 different
 *	clocks.  So to try and make it easier to follow the clocking activation
 *	and deactivation is handled in its own set of callbacks.
 *
 *	LOCKING:
 *	Inherits the locks from the omap_prcm driver, no internal locking.
 *
 *	RETURNS:
 *	Returns 0 on success or a positive error code on failure.
 */

struct dpll_param {
	unsigned int m;
	unsigned int n;
	unsigned int m2;
	unsigned int m3;
	unsigned int m4;
	unsigned int m5;
	unsigned int m6;
	unsigned int m7;
};
/* USB parameters */
struct dpll_param usb_dpll_param[7] = {
	/* 12M values */
	{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
	/* 13M values */
	{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
	/* 16.8M values */
	{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
	/* 19.2M values */
	{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
	/* 26M values */
	{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
	/* 27M values */
	{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
	/* 38.4M values */
#ifdef CONFIG_OMAP4_SDC
	{0x32, 0x1, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0},
#else
	{0x32, 0x1, 0x2, 0x0, 0x0, 0x0, 0x0, 0x0},
#endif
};
static int
omap4_clk_hsusbhost_activate(struct ti_clock_dev *clkdev)
{
	struct omap4_prcm_softc *sc;
	struct resource* clk_mem_res;
	uint32_t clksel_reg_off;
	uint32_t clksel;
	unsigned int i;

	sc = omap4_prcm_get_instance_softc(CM2_INSTANCE);
	if (sc == NULL)
		return ENXIO;

	switch (clkdev->id) {
	case USBTLL_CLK:
		/* For the USBTLL module we need to enable the following clocks:
		 *  - INIT_L4_ICLK  (will be enabled by bootloader)
		 *  - TLL_CH0_FCLK
		 *  - TLL_CH1_FCLK
		 */

		/* We need the CM_L3INIT_HSUSBTLL_CLKCTRL register in CM2 register set */
		clk_mem_res = sc->sc_res;
		clksel_reg_off = L3INIT_CM2_OFFSET + 0x68;
	
		/* Enable the module and also enable the optional func clocks for
		 * channels 0 & 1 (is this needed ?)
		 */
		clksel = bus_read_4(clk_mem_res, clksel_reg_off);
		clksel &= ~CLKCTRL_MODULEMODE_MASK;
		clksel |=  CLKCTRL_MODULEMODE_ENABLE;
		
		clksel |= (0x1 << 8); /* USB-HOST optional clock: USB_CH0_CLK */
		clksel |= (0x1 << 9); /* USB-HOST optional clock: USB_CH1_CLK */
		break;

	case USBHSHOST_CLK:
	case USBP1_PHY_CLK:
	case USBP2_PHY_CLK:
	case USBP1_UTMI_CLK:
	case USBP2_UTMI_CLK:
	case USBP1_HSIC_CLK:
	case USBP2_HSIC_CLK:
		/* For the USB HS HOST module we need to enable the following clocks:
		 *  - INIT_L4_ICLK     (will be enabled by bootloader)
		 *  - INIT_L3_ICLK     (will be enabled by bootloader)
		 *  - INIT_48MC_FCLK
		 *  - UTMI_ROOT_GFCLK  (UTMI only, create a new clock for that ?)
		 *  - UTMI_P1_FCLK     (UTMI only, create a new clock for that ?)
		 *  - UTMI_P2_FCLK     (UTMI only, create a new clock for that ?)
		 *  - HSIC_P1_60       (HSIC only, create a new clock for that ?)
		 *  - HSIC_P1_480      (HSIC only, create a new clock for that ?)
		 *  - HSIC_P2_60       (HSIC only, create a new clock for that ?)
		 *  - HSIC_P2_480      (HSIC only, create a new clock for that ?)
		 */

		/* We need the CM_L3INIT_HSUSBHOST_CLKCTRL register in CM2 register set */
		clk_mem_res = sc->sc_res;
		clksel_reg_off = L3INIT_CM2_OFFSET + 0x58;
		clksel = bus_read_4(clk_mem_res, clksel_reg_off);	
		/* Enable the module and also enable the optional func clocks */
		if (clkdev->id == USBHSHOST_CLK) {
			clksel &= ~CLKCTRL_MODULEMODE_MASK;
			clksel |=  /*CLKCTRL_MODULEMODE_ENABLE*/2;

			clksel |= (0x1 << 15); /* USB-HOST clock control: FUNC48MCLK */
		}
		
		else if (clkdev->id == USBP1_UTMI_CLK)
			clksel |= (0x1 << 8);  /* UTMI_P1_CLK */
		else if (clkdev->id == USBP2_UTMI_CLK)
			clksel |= (0x1 << 9);  /* UTMI_P2_CLK */

		else if (clkdev->id == USBP1_HSIC_CLK)
			clksel |= (0x5 << 11);  /* HSIC60M_P1_CLK + HSIC480M_P1_CLK */
		else if (clkdev->id == USBP2_HSIC_CLK)
			clksel |= (0x5 << 12);  /* HSIC60M_P2_CLK + HSIC480M_P2_CLK */
		
		break;
	
	default:
		return (EINVAL);
	}
	
	bus_write_4(clk_mem_res, clksel_reg_off, clksel);
	
	/* Try MAX_MODULE_ENABLE_WAIT number of times to check if enabled */
	for (i = 0; i < MAX_MODULE_ENABLE_WAIT; i++) {
		clksel = bus_read_4(clk_mem_res, clksel_reg_off);
		if ((clksel & CLKCTRL_IDLEST_MASK) == CLKCTRL_IDLEST_ENABLED)
			break;
	}
		
	/* Check the enabled state */
	if ((clksel & CLKCTRL_IDLEST_MASK) != CLKCTRL_IDLEST_ENABLED) {
		printf("Error: HERE failed to enable module with clock %d\n", clkdev->id);
		printf("Error: 0x%08x => 0x%08x\n", clksel_reg_off, clksel);
		return (ETIMEDOUT);
	}
	
	return (0);
}

/**
 *	omap4_clk_generic_deactivate - checks if a module is accessible
 *	@clkdev: pointer to the clock device structure.
 *	@mem_res: array of memory resources allocated by the top level PRCM driver.
 *	
 *	
 *
 *	LOCKING:
 *	Inherits the locks from the omap_prcm driver, no internal locking.
 *
 *	RETURNS:
 *	Returns 0 on success or a positive error code on failure.
 */
static int
omap4_clk_hsusbhost_deactivate(struct ti_clock_dev *clkdev)
{
	struct omap4_prcm_softc *sc;
	struct resource* clk_mem_res;
	uint32_t clksel_reg_off;
	uint32_t clksel;

	sc = omap4_prcm_get_instance_softc(CM2_INSTANCE);
	if (sc == NULL)
		return ENXIO;

	switch (clkdev->id) {
	case USBTLL_CLK:
		/* We need the CM_L3INIT_HSUSBTLL_CLKCTRL register in CM2 register set */
		clk_mem_res = sc->sc_res;
		clksel_reg_off = L3INIT_CM2_OFFSET + 0x68;
	
		clksel = bus_read_4(clk_mem_res, clksel_reg_off);
		clksel &= ~CLKCTRL_MODULEMODE_MASK;
		clksel |=  CLKCTRL_MODULEMODE_DISABLE;
		break;

	case USBHSHOST_CLK:
	case USBP1_PHY_CLK:
	case USBP2_PHY_CLK:
	case USBP1_UTMI_CLK:
	case USBP2_UTMI_CLK:
	case USBP1_HSIC_CLK:
	case USBP2_HSIC_CLK:
		/* For the USB HS HOST module we need to enable the following clocks:
		 *  - INIT_L4_ICLK     (will be enabled by bootloader)
		 *  - INIT_L3_ICLK     (will be enabled by bootloader)
		 *  - INIT_48MC_FCLK
		 *  - UTMI_ROOT_GFCLK  (UTMI only, create a new clock for that ?)
		 *  - UTMI_P1_FCLK     (UTMI only, create a new clock for that ?)
		 *  - UTMI_P2_FCLK     (UTMI only, create a new clock for that ?)
		 *  - HSIC_P1_60       (HSIC only, create a new clock for that ?)
		 *  - HSIC_P1_480      (HSIC only, create a new clock for that ?)
		 *  - HSIC_P2_60       (HSIC only, create a new clock for that ?)
		 *  - HSIC_P2_480      (HSIC only, create a new clock for that ?)
		 */

		/* We need the CM_L3INIT_HSUSBHOST_CLKCTRL register in CM2 register set */
		clk_mem_res = sc->sc_res;
		clksel_reg_off = L3INIT_CM2_OFFSET + 0x58;
		clksel = bus_read_4(clk_mem_res, clksel_reg_off);

		/* Enable the module and also enable the optional func clocks */
		if (clkdev->id == USBHSHOST_CLK) {
			clksel &= ~CLKCTRL_MODULEMODE_MASK;
			clksel |=  CLKCTRL_MODULEMODE_DISABLE;

			clksel &= ~(0x1 << 15); /* USB-HOST clock control: FUNC48MCLK */
		}
		
		else if (clkdev->id == USBP1_UTMI_CLK)
			clksel &= ~(0x1 << 8);  /* UTMI_P1_CLK */
		else if (clkdev->id == USBP2_UTMI_CLK)
			clksel &= ~(0x1 << 9);  /* UTMI_P2_CLK */

		else if (clkdev->id == USBP1_HSIC_CLK)
			clksel &= ~(0x5 << 11);  /* HSIC60M_P1_CLK + HSIC480M_P1_CLK */
		else if (clkdev->id == USBP2_HSIC_CLK)
			clksel &= ~(0x5 << 12);  /* HSIC60M_P2_CLK + HSIC480M_P2_CLK */
		
		break;
	
	default:
		return (EINVAL);
	}
	
	bus_write_4(clk_mem_res, clksel_reg_off, clksel);

	return (0);
}

/**
 *	omap4_clk_hsusbhost_accessible - checks if a module is accessible
 *	@clkdev: pointer to the clock device structure.
 *	@mem_res: array of memory resources allocated by the top level PRCM driver.
 *	
 *	
 *
 *	LOCKING:
 *	Inherits the locks from the omap_prcm driver, no internal locking.
 *
 *	RETURNS:
 *	Returns 0 if module is not enable, 1 if module is enabled or a negative
 *	error code on failure.
 */
static int
omap4_clk_hsusbhost_accessible(struct ti_clock_dev *clkdev)
{
	struct omap4_prcm_softc *sc;
	struct resource* clk_mem_res;
	uint32_t clksel_reg_off;
	uint32_t clksel;

	sc = omap4_prcm_get_instance_softc(CM2_INSTANCE);
	if (sc == NULL)
		return ENXIO;

	if (clkdev->id == USBTLL_CLK) {
		/* We need the CM_L3INIT_HSUSBTLL_CLKCTRL register in CM2 register set */
		clk_mem_res = sc->sc_res;
		clksel_reg_off = L3INIT_CM2_OFFSET + 0x68;
	}
	else if (clkdev->id == USBHSHOST_CLK) {
		/* We need the CM_L3INIT_HSUSBHOST_CLKCTRL register in CM2 register set */
		clk_mem_res = sc->sc_res;
		clksel_reg_off = L3INIT_CM2_OFFSET + 0x58;
	}
	else {
		return (EINVAL);
	}

	clksel = bus_read_4(clk_mem_res, clksel_reg_off);
		
	/* Check the enabled state */
	if ((clksel & CLKCTRL_IDLEST_MASK) != CLKCTRL_IDLEST_ENABLED)
		return (0);
	
	return (1);
}

/**
 *	omap4_clk_hsusbhost_set_source - sets the source clocks
 *	@clkdev: pointer to the clock device structure.
 *	@clksrc: the clock source ID for the given clock.
 *	@mem_res: array of memory resources allocated by the top level PRCM driver.
 *	
 *	
 *
 *	LOCKING:
 *	Inherits the locks from the omap_prcm driver, no internal locking.
 *
 *	RETURNS:
 *	Returns 0 if successful otherwise a negative error code on failure.
 */
static int
omap4_clk_hsusbhost_set_source(struct ti_clock_dev *clkdev,
                               clk_src_t clksrc)
{
	struct omap4_prcm_softc *sc;
	struct resource* clk_mem_res;
	uint32_t clksel_reg_off;
	uint32_t clksel;
	unsigned int bit;

	sc = omap4_prcm_get_instance_softc(CM2_INSTANCE);
	if (sc == NULL)
		return ENXIO;

	if (clkdev->id == USBP1_PHY_CLK)
		bit = 24;
	else if (clkdev->id != USBP2_PHY_CLK)
		bit = 25;
	else
		return (EINVAL);
	
	/* We need the CM_L3INIT_HSUSBHOST_CLKCTRL register in CM2 register set */
	clk_mem_res = sc->sc_res;
	clksel_reg_off = L3INIT_CM2_OFFSET + 0x58;
	clksel = bus_read_4(clk_mem_res, clksel_reg_off);
	
	/* Set the clock source to either external or internal */
	if (clksrc == EXT_CLK)
		clksel |= (0x1 << bit);
	else
		clksel &= ~(0x1 << bit);
	
	bus_write_4(clk_mem_res, clksel_reg_off, clksel);

	return (0);
}

#define PRM_RSTCTRL		0x1b00
#define PRM_RSTCTRL_RESET	0x2

static void
omap4_prcm_reset(void)
{
	struct omap4_prcm_softc *sc;
	
	sc = omap4_prcm_get_instance_softc(PRM_INSTANCE);
	if (sc == NULL)
		return;

	bus_write_4(sc->sc_res, PRM_RSTCTRL,
	    bus_read_4(sc->sc_res, PRM_RSTCTRL) | PRM_RSTCTRL_RESET);
	bus_read_4(sc->sc_res, PRM_RSTCTRL);
}

/**
 *	omap4_prcm_probe - probe function for the driver
 *	@dev: prcm device handle
 *
 *	Simply sets the name of the driver module.
 *
 *	LOCKING:
 *	None
 *
 *	RETURNS:
 *	Always returns 0
 */
static int
omap4_prcm_probe(device_t dev)
{
	const struct ofw_compat_data *ocd;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	ocd = ofw_bus_search_compatible(dev, compat_data);
	if ((int)ocd->ocd_data == 0)
		return (ENXIO);

	switch ((int)ocd->ocd_data) {
		case PRM_INSTANCE:
			device_set_desc(dev, "TI OMAP Power, Reset and Clock Management (PRM)");
			break;
		case CM1_INSTANCE:
			device_set_desc(dev, "TI OMAP Power, Reset and Clock Management (C1)");
			break;
		case CM2_INSTANCE:
			device_set_desc(dev, "TI OMAP Power, Reset and Clock Management (C2)");
			break;
		default:
			device_printf(dev, "unknown instance type: %d\n", (int)ocd->ocd_data);
			return (ENXIO);
	}

	return (BUS_PROBE_DEFAULT);
}

/**
 *	omap_prcm_attach - attach function for the driver
 *	@dev: prcm device handle
 *
 *	Allocates and sets up the driver context, this simply entails creating a
 *	bus mappings for the PRCM register set.
 *
 *	LOCKING:
 *	None
 *
 *	RETURNS:
 *	Always returns 0
 */

extern uint32_t platform_arm_tmr_freq;

static int
omap4_prcm_attach(device_t dev)
{
	struct omap4_prcm_softc *sc;
	const struct ofw_compat_data *ocd;

	sc = device_get_softc(dev);
	ocd = ofw_bus_search_compatible(dev, compat_data);
	sc->sc_instance = (int)ocd->ocd_data;

	sc->sc_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->sc_rid,
	    RF_ACTIVE);
	if (sc->sc_res == NULL) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	ti_cpu_reset = omap4_prcm_reset;

	return (0);
}

static void
omap4_prcm_new_pass(device_t dev)
{
	struct omap4_prcm_softc *sc = device_get_softc(dev);
	unsigned int freq;

	if (sc->attach_done ||
	  bus_current_pass < (BUS_PASS_TIMER + BUS_PASS_ORDER_EARLY)) {
		bus_generic_new_pass(dev);
		return;
	}
	sc->attach_done = 1;

	/*
	 * In order to determine ARM frequency we need both RPM and CM1 
	 * instances up and running. So wait until all CRM devices are
	 * initialized. Should be replaced with proper clock framework
	 */
	if (device_get_unit(dev) == 2) {
		omap4_clk_get_arm_fclk_freq(NULL, &freq);
		arm_tmr_change_frequency(freq / 2);
	}

	return;
}

static device_method_t omap4_prcm_methods[] = {
	DEVMETHOD(device_probe, omap4_prcm_probe),
	DEVMETHOD(device_attach, omap4_prcm_attach),

	/* Bus interface */
	DEVMETHOD(bus_new_pass, omap4_prcm_new_pass),

	{0, 0},
};

static driver_t omap4_prcm_driver = {
	"omap4_prcm",
	omap4_prcm_methods,
	sizeof(struct omap4_prcm_softc),
};

static devclass_t omap4_prcm_devclass;

EARLY_DRIVER_MODULE(omap4_prcm, simplebus, omap4_prcm_driver,
    omap4_prcm_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
MODULE_VERSION(omap4_prcm, 1);
