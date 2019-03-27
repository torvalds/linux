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
#include <sys/bus.h>
#include <sys/kernel.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/vmparam.h>
#include <machine/fdt.h>

#include <arm/ti/omap4/omap4_reg.h>
#include <arm/ti/omap4/pandaboard/pandaboard.h>

/* Registers in the SCRM that control the AUX clocks */
#define SCRM_ALTCLKSRC			     (0x110)
#define SCRM_AUXCLK0                         (0x0310)
#define SCRM_AUXCLK1                         (0x0314)
#define SCRM_AUXCLK2                         (0x0318)
#define SCRM_AUXCLK3                         (0x031C)

/* Some of the GPIO register set */
#define GPIO1_OE                             (0x0134)
#define GPIO1_CLEARDATAOUT                   (0x0190)
#define GPIO1_SETDATAOUT                     (0x0194)
#define GPIO2_OE                             (0x0134)
#define GPIO2_CLEARDATAOUT                   (0x0190)
#define GPIO2_SETDATAOUT                     (0x0194)

/* Some of the PADCONF register set */
#define CONTROL_WKUP_PAD0_FREF_CLK3_OUT  (0x058)
#define CONTROL_CORE_PAD1_KPD_COL2       (0x186)
#define CONTROL_CORE_PAD0_GPMC_WAIT1     (0x08C)

#define REG_WRITE32(r, x)    *((volatile uint32_t*)(r)) = (uint32_t)(x)
#define REG_READ32(r)        *((volatile uint32_t*)(r))

#define REG_WRITE16(r, x)    *((volatile uint16_t*)(r)) = (uint16_t)(x)
#define REG_READ16(r)        *((volatile uint16_t*)(r))

/**
 *	usb_hub_init - initialises and resets the external USB hub
 *	
 *	The USB hub needs to be held in reset while the power is being applied
 *	and the reference clock is enabled at 19.2MHz.  The following is the
 *	layout of the USB hub taken from the Pandaboard reference manual.
 *
 *
 *	   .-------------.         .--------------.         .----------------.
 *	   |  OMAP4430   |         |   USB3320C   |         |    LAN9514     |
 *	   |             |         |              |         | USB Hub / Eth  |
 *	   |         CLK | <------ | CLKOUT       |         |                |
 *	   |         STP | ------> | STP          |         |                |
 *	   |         DIR | <------ | DIR          |         |                |
 *	   |         NXT | <------ | NXT          |         |                |
 *	   |        DAT0 | <-----> | DAT0         |         |                |
 *	   |        DAT1 | <-----> | DAT1      DP | <-----> | DP             |
 *	   |        DAT2 | <-----> | DAT2      DM | <-----> | DM             |
 *	   |        DAT3 | <-----> | DAT3         |         |                |
 *	   |        DAT4 | <-----> | DAT4         |         |                |
 *	   |        DAT5 | <-----> | DAT5         |  +----> | N_RESET        |
 *	   |        DAT6 | <-----> | DAT6         |  |      |                |
 *	   |        DAT7 | <-----> | DAT7         |  |      |                |
 *	   |             |         |              |  |  +-> | VDD33IO        |
 *	   |    AUX_CLK3 | ------> | REFCLK       |  |  +-> | VDD33A         |
 *	   |             |         |              |  |  |   |                |
 *	   |     GPIO_62 | --+---> | RESET        |  |  |   |                |
 *	   |             |   |     |              |  |  |   |                |
 *	   |             |   |     '--------------'  |  |   '----------------'
 *	   |             |   |     .--------------.  |  |
 *	   |             |   '---->| VOLT CONVERT |--'  |
 *	   |             |         '--------------'     |
 *	   |             |                              |
 *	   |             |         .--------------.     |
 *	   |      GPIO_1 | ------> |   TPS73633   |-----'
 *	   |             |         '--------------'
 *	   '-------------'
 *	
 *
 *	RETURNS:
 *	nothing.
 */
void
pandaboard_usb_hub_init(void)
{
	bus_space_handle_t scrm_addr, gpio1_addr, gpio2_addr, scm_addr;

	if (bus_space_map(fdtbus_bs_tag, OMAP44XX_SCRM_HWBASE,
	    OMAP44XX_SCRM_SIZE, 0, &scrm_addr) != 0)
		panic("Couldn't map SCRM registers");
	if (bus_space_map(fdtbus_bs_tag, OMAP44XX_GPIO1_HWBASE, 
	    OMAP44XX_GPIO1_SIZE, 0, &gpio1_addr) != 0)
		panic("Couldn't map GPIO1 registers");
	if (bus_space_map(fdtbus_bs_tag, OMAP44XX_GPIO2_HWBASE,
	    OMAP44XX_GPIO2_SIZE, 0, &gpio2_addr) != 0)
		panic("Couldn't map GPIO2 registers");
	if (bus_space_map(fdtbus_bs_tag, OMAP44XX_SCM_PADCONF_HWBASE,
	    OMAP44XX_SCM_PADCONF_SIZE, 0, &scm_addr) != 0)
		panic("Couldn't map SCM Padconf registers");

	

	/* Need to set FREF_CLK3_OUT to 19.2 MHz and pump it out on pin GPIO_WK31.
	 * We know the SYS_CLK is 38.4Mhz and therefore to get the needed 19.2Mhz,
	 * just use a 2x divider and ensure the SYS_CLK is used as the source.
	 */
	REG_WRITE32(scrm_addr + SCRM_AUXCLK3, (1 << 16) |    /* Divider of 2 */
	                          (0 << 1) |     /* Use the SYS_CLK as the source */
	                          (1 << 8));     /* Enable the clock */

	/* Enable the clock out to the pin (GPIO_WK31). 
	 *   muxmode=fref_clk3_out, pullup/down=disabled, input buffer=disabled,
	 *   wakeup=disabled.
	 */
	REG_WRITE16(scm_addr + CONTROL_WKUP_PAD0_FREF_CLK3_OUT, 0x0000);


	/* Disable the power to the USB hub, drive GPIO1 low */
	REG_WRITE32(gpio1_addr + GPIO1_OE, REG_READ32(gpio1_addr + 
	    GPIO1_OE) & ~(1UL << 1));
	REG_WRITE32(gpio1_addr + GPIO1_CLEARDATAOUT, (1UL << 1));
	REG_WRITE16(scm_addr + CONTROL_CORE_PAD1_KPD_COL2, 0x0003);
	
	
	/* Reset the USB PHY and Hub using GPIO_62 */
	REG_WRITE32(gpio2_addr + GPIO2_OE, 
	    REG_READ32(gpio2_addr + GPIO2_OE) & ~(1UL << 30));
	REG_WRITE32(gpio2_addr + GPIO2_CLEARDATAOUT, (1UL << 30));
	REG_WRITE16(scm_addr + CONTROL_CORE_PAD0_GPMC_WAIT1, 0x0003);
	DELAY(10);
	REG_WRITE32(gpio2_addr + GPIO2_SETDATAOUT, (1UL << 30));

	
	/* Enable power to the hub (GPIO_1) */
	REG_WRITE32(gpio1_addr + GPIO1_SETDATAOUT, (1UL << 1));
	bus_space_unmap(fdtbus_bs_tag, scrm_addr, OMAP44XX_SCRM_SIZE);
	bus_space_unmap(fdtbus_bs_tag, gpio1_addr, OMAP44XX_GPIO1_SIZE);
	bus_space_unmap(fdtbus_bs_tag, gpio2_addr, OMAP44XX_GPIO2_SIZE);
	bus_space_unmap(fdtbus_bs_tag, scm_addr, OMAP44XX_SCM_PADCONF_SIZE);
}
