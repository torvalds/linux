/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Adrian Chadd <adrian@FreeBSD.org>
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

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cons.h>
#include <sys/kdb.h>
#include <sys/reboot.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#include <net/ethernet.h>

#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cpuregs.h>
#include <machine/hwfunc.h>
#include <machine/md_var.h>
#include <machine/trap.h>
#include <machine/vmparam.h>

#include <mips/atheros/ar71xxreg.h>
#include <mips/atheros/ar933xreg.h>

#include <mips/atheros/ar71xx_cpudef.h>
#include <mips/atheros/ar71xx_setup.h>

#include <mips/atheros/ar71xx_chip.h>
#include <mips/atheros/ar933x_chip.h>

static void
ar933x_chip_detect_mem_size(void)
{
}

static void
ar933x_chip_detect_sys_frequency(void)
{
	uint32_t clock_ctrl;
	uint32_t cpu_config;
	uint32_t freq;
	uint32_t t;

	t = ATH_READ_REG(AR933X_RESET_REG_BOOTSTRAP);
	if (t & AR933X_BOOTSTRAP_REF_CLK_40)
		u_ar71xx_refclk = (40 * 1000 * 1000);
	else
		u_ar71xx_refclk = (25 * 1000 * 1000);

	clock_ctrl = ATH_READ_REG(AR933X_PLL_CLOCK_CTRL_REG);
	if (clock_ctrl & AR933X_PLL_CLOCK_CTRL_BYPASS) {
		u_ar71xx_cpu_freq = u_ar71xx_refclk;
		u_ar71xx_ahb_freq = u_ar71xx_refclk;
		u_ar71xx_ddr_freq = u_ar71xx_refclk;
	} else {
		cpu_config = ATH_READ_REG(AR933X_PLL_CPU_CONFIG_REG);

		t = (cpu_config >> AR933X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
		    AR933X_PLL_CPU_CONFIG_REFDIV_MASK;
		freq = u_ar71xx_refclk / t;

		t = (cpu_config >> AR933X_PLL_CPU_CONFIG_NINT_SHIFT) &
		    AR933X_PLL_CPU_CONFIG_NINT_MASK;
		freq *= t;

		t = (cpu_config >> AR933X_PLL_CPU_CONFIG_OUTDIV_SHIFT) &
		    AR933X_PLL_CPU_CONFIG_OUTDIV_MASK;
		if (t == 0)
			t = 1;

		freq >>= t;

		t = ((clock_ctrl >> AR933X_PLL_CLOCK_CTRL_CPU_DIV_SHIFT) &
		     AR933X_PLL_CLOCK_CTRL_CPU_DIV_MASK) + 1;
		u_ar71xx_cpu_freq = freq / t;

		t = ((clock_ctrl >> AR933X_PLL_CLOCK_CTRL_DDR_DIV_SHIFT) &
		      AR933X_PLL_CLOCK_CTRL_DDR_DIV_MASK) + 1;
		u_ar71xx_ddr_freq = freq / t;

		t = ((clock_ctrl >> AR933X_PLL_CLOCK_CTRL_AHB_DIV_SHIFT) &
		     AR933X_PLL_CLOCK_CTRL_AHB_DIV_MASK) + 1;
		u_ar71xx_ahb_freq = freq / t;
	}

	/*
	 * On the AR933x, the UART frequency is the reference clock,
	 * not the AHB bus clock.
	 */
	u_ar71xx_uart_freq = u_ar71xx_refclk;

	/*
	 * XXX TODO: check whether the mdio frequency is always the
	 * refclock frequency, or whether it's variable like on the
	 * AR934x.
	 */
	u_ar71xx_mdio_freq = u_ar71xx_refclk;

	/*
	 * XXX check what the watchdog frequency should be?
	 */
	u_ar71xx_wdt_freq = u_ar71xx_ahb_freq;
}

static void
ar933x_chip_device_stop(uint32_t mask)
{
	uint32_t reg;

	reg = ATH_READ_REG(AR933X_RESET_REG_RESET_MODULE);
	ATH_WRITE_REG(AR933X_RESET_REG_RESET_MODULE, reg | mask);
}

static void
ar933x_chip_device_start(uint32_t mask)
{
	uint32_t reg;

	reg = ATH_READ_REG(AR933X_RESET_REG_RESET_MODULE);
	ATH_WRITE_REG(AR933X_RESET_REG_RESET_MODULE, reg & ~mask);
}

static int
ar933x_chip_device_stopped(uint32_t mask)
{
	uint32_t reg;

	reg = ATH_READ_REG(AR933X_RESET_REG_RESET_MODULE);
	return ((reg & mask) == mask);
}

static void
ar933x_chip_set_mii_speed(uint32_t unit, uint32_t speed)
{

	/* XXX TODO */
	return;
}

/*
 * XXX TODO !!
 */
static void
ar933x_chip_set_pll_ge(int unit, int speed, uint32_t pll)
{

	switch (unit) {
	case 0:
		/* XXX TODO */
		break;
	case 1:
		/* XXX TODO */
		break;
	default:
		printf("%s: invalid PLL set for arge unit: %d\n",
		    __func__, unit);
		return;
	}
}

static void
ar933x_chip_ddr_flush(ar71xx_flush_ddr_id_t id)
{

	switch (id) {
	case AR71XX_CPU_DDR_FLUSH_GE0:
		ar71xx_ddr_flush(AR933X_DDR_REG_FLUSH_GE0);
		break;
	case AR71XX_CPU_DDR_FLUSH_GE1:
		ar71xx_ddr_flush(AR933X_DDR_REG_FLUSH_GE1);
		break;
	case AR71XX_CPU_DDR_FLUSH_USB:
		ar71xx_ddr_flush(AR933X_DDR_REG_FLUSH_USB);
		break;
	case AR71XX_CPU_DDR_FLUSH_WMAC:
		ar71xx_ddr_flush(AR933X_DDR_REG_FLUSH_WMAC);
		break;
	default:
		printf("%s: invalid DDR flush id (%d)\n", __func__, id);
		break;
	}
}


static uint32_t
ar933x_chip_get_eth_pll(unsigned int mac, int speed)
{
	uint32_t pll;

	switch (speed) {
	case 10:
		pll = AR933X_PLL_VAL_10;
		break;
	case 100:
		pll = AR933X_PLL_VAL_100;
		break;
	case 1000:
		pll = AR933X_PLL_VAL_1000;
		break;
	default:
		printf("%s%d: invalid speed %d\n", __func__, mac, speed);
		pll = 0;
	}
	return (pll);
}

static void
ar933x_chip_init_usb_peripheral(void)
{
	ar71xx_device_stop(AR933X_RESET_USBSUS_OVERRIDE);
	DELAY(100);

	ar71xx_device_start(AR933X_RESET_USB_HOST);
	DELAY(100);

	ar71xx_device_start(AR933X_RESET_USB_PHY);
	DELAY(100);
}

static void
ar933x_configure_gmac(uint32_t gmac_cfg)
{
	uint32_t reg;

	reg = ATH_READ_REG(AR933X_GMAC_REG_ETH_CFG);

	/*
	 * The relevant bits here include:
	 *
	 * + AR933X_ETH_CFG_SW_PHY_SWAP
	 * + AR933X_ETH_CFG_SW_PHY_ADDR_SWAP
	 *
	 * There are other things; look at what openwrt exposes so
	 * it can be correctly exposed.
	 *
	 * TODO: what about ethernet switch support? How's that work?
	 */
	if (bootverbose)
		printf("%s: GMAC config was 0x%08x\n", __func__, reg);
        reg &= ~(AR933X_ETH_CFG_SW_PHY_SWAP | AR933X_ETH_CFG_SW_PHY_ADDR_SWAP);
	reg |= gmac_cfg;
	if (bootverbose)
		printf("%s: GMAC setting is 0x%08x; register is now 0x%08x\n",
		    __func__,
		    gmac_cfg,
		    reg);
	ATH_WRITE_REG(AR933X_GMAC_REG_ETH_CFG, reg);
}

static void
ar933x_chip_init_gmac(void)
{
	int val;
	uint32_t gmac_cfg = 0;

	/*
	 * These two bits need a bit better explanation.
	 *
	 * The default configuration in the hardware is to map both
	 * ports to the internal switch.
	 *
	 * Here, GE0 == arge0, GE1 == arge1.
	 *
	 * The internal switch has:
	 * + 5 MAC ports, MAC0->MAC4.
	 * + 5 PHY ports, PHY0->PHY4,
	 * + MAC0 connects to GE1;
	 * + GE0 connects to PHY4;
	 * + The other mappings are MAC1->PHY0, MAC2->PHY1 .. MAC4->PHY3.
	 *
	 * The GE1 port is linked in via 1000MBit/full, supplying what is
	 * normally the 'WAN' switch ports.
	 *
	 * The switch is connected the MDIO bus on GE1.  It looks like
	 * a normal AR7240 on-board switch.
	 *
	 * The GE0 port is connected via MII to PHY4, and can operate in
	 * 10/100mbit, full/half duplex.  Ie, you can speak to PHY4 on
	 * the MDIO bus and everything will simply 'work'.
	 *
	 * So far so good.  This looks just like an AR7240 SoC.
	 *
	 * However, some configurations will just expose one or two
	 * physical ports.  In this case, some configuration bits can
	 * be set to tweak this.
	 *
	 * + CFG_SW_PHY_ADDR_SWAP swaps PHY port 0 with PHY port 4.
	 *   Ie, GE0's PHY shows up as PHY 0.  So if there's only
	 *   one physical port, there's no need to involve the
	 *   switch framework - it can just show up as a default,
	 *   normal single PHY.
	 *
	 * + CFG_SW_PHY_SWAP swaps the internal switch connection
	 *   between PHY0 and PHY4.  Ie, PHY4 connects to MAc1,
	 *   PHY0 connects to GE0.
	 */
	if ((resource_int_value("ar933x_gmac", 0, "override_phy", &val) == 0)
	    && (val == 0))
		return;
	if ((resource_int_value("ar933x_gmac", 0, "swap_phy", &val) == 0)
	    && (val == 1))
		gmac_cfg |= AR933X_ETH_CFG_SW_PHY_SWAP;
	if ((resource_int_value("ar933x_gmac", 0, "swap_phy_addr", &val) == 0)
	    && (val == 1))
		gmac_cfg |= AR933X_ETH_CFG_SW_PHY_ADDR_SWAP;
	ar933x_configure_gmac(gmac_cfg);
}

struct ar71xx_cpu_def ar933x_chip_def = {
	&ar933x_chip_detect_mem_size,
	&ar933x_chip_detect_sys_frequency,
	&ar933x_chip_device_stop,
	&ar933x_chip_device_start,
	&ar933x_chip_device_stopped,
	&ar933x_chip_set_pll_ge,
	&ar933x_chip_set_mii_speed,
	&ar71xx_chip_set_mii_if,
	&ar933x_chip_get_eth_pll,
	&ar933x_chip_ddr_flush,
	&ar933x_chip_init_usb_peripheral,
	NULL,
	NULL,
	&ar933x_chip_init_gmac,
};
