/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Adrian Chadd
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
#include <mips/atheros/ar71xx_chip.h>
#include <mips/atheros/ar71xx_cpudef.h>

/* XXX these should replace the current definitions in ar71xxreg.h */
/* XXX perhaps an ar71xx_chip.h header file? */
#define AR71XX_PLL_REG_CPU_CONFIG       AR71XX_PLL_CPU_BASE + 0x00
#define AR71XX_PLL_REG_SEC_CONFIG       AR71XX_PLL_CPU_BASE + 0x04
#define AR71XX_PLL_REG_ETH0_INT_CLOCK   AR71XX_PLL_CPU_BASE + 0x10
#define AR71XX_PLL_REG_ETH1_INT_CLOCK   AR71XX_PLL_CPU_BASE + 0x14

#define AR71XX_PLL_DIV_SHIFT            3
#define AR71XX_PLL_DIV_MASK             0x1f
#define AR71XX_CPU_DIV_SHIFT            16
#define AR71XX_CPU_DIV_MASK             0x3
#define AR71XX_DDR_DIV_SHIFT            18
#define AR71XX_DDR_DIV_MASK             0x3
#define AR71XX_AHB_DIV_SHIFT            20
#define AR71XX_AHB_DIV_MASK             0x7

/* XXX these shouldn't be in here - this file is a per-chip file */
/* XXX these should be in the top-level ar71xx type, not ar71xx -chip */
uint32_t u_ar71xx_cpu_freq;
uint32_t u_ar71xx_ahb_freq;
uint32_t u_ar71xx_ddr_freq;
uint32_t u_ar71xx_uart_freq;
uint32_t u_ar71xx_wdt_freq;
uint32_t u_ar71xx_refclk;
uint32_t u_ar71xx_mdio_freq;

static void
ar71xx_chip_detect_mem_size(void)
{
}

static void
ar71xx_chip_detect_sys_frequency(void)
{
	uint32_t pll;
	uint32_t freq;
	uint32_t div;

	u_ar71xx_mdio_freq = u_ar71xx_refclk = AR71XX_BASE_FREQ;

	pll = ATH_READ_REG(AR71XX_PLL_REG_CPU_CONFIG);

	div = ((pll >> AR71XX_PLL_DIV_SHIFT) & AR71XX_PLL_DIV_MASK) + 1;
	freq = div * AR71XX_BASE_FREQ;

	div = ((pll >> AR71XX_CPU_DIV_SHIFT) & AR71XX_CPU_DIV_MASK) + 1;
	u_ar71xx_cpu_freq = freq / div;

	div = ((pll >> AR71XX_DDR_DIV_SHIFT) & AR71XX_DDR_DIV_MASK) + 1;
	u_ar71xx_ddr_freq = freq / div;

	div = (((pll >> AR71XX_AHB_DIV_SHIFT) & AR71XX_AHB_DIV_MASK) + 1) * 2;
	u_ar71xx_ahb_freq = u_ar71xx_cpu_freq / div;
	u_ar71xx_wdt_freq = u_ar71xx_cpu_freq / div;
	u_ar71xx_uart_freq = u_ar71xx_cpu_freq / div;
}

/*
 * This does not lock the CPU whilst doing the work!
 */
static void
ar71xx_chip_device_stop(uint32_t mask)
{
	uint32_t reg;

	reg = ATH_READ_REG(AR71XX_RST_RESET);
	ATH_WRITE_REG(AR71XX_RST_RESET, reg | mask);
}

static void
ar71xx_chip_device_start(uint32_t mask)
{
	uint32_t reg;

	reg = ATH_READ_REG(AR71XX_RST_RESET);
	ATH_WRITE_REG(AR71XX_RST_RESET, reg & ~mask);
}

static int
ar71xx_chip_device_stopped(uint32_t mask)
{
	uint32_t reg;

	reg = ATH_READ_REG(AR71XX_RST_RESET);
	return ((reg & mask) == mask);
}

void
ar71xx_chip_set_mii_speed(uint32_t unit, uint32_t speed)
{
	uint32_t val, reg, ctrl;

	switch (unit) {
	case 0:
		reg = AR71XX_MII0_CTRL;
		break;
	case 1:
		reg = AR71XX_MII1_CTRL;
		break;
	default:
		printf("%s: invalid MII unit set for arge unit: %d\n",
		    __func__, unit);
		return;
	}

	switch (speed) {
	case 10:
		ctrl = MII_CTRL_SPEED_10;
		break;
	case 100:
		ctrl = MII_CTRL_SPEED_100;
		break;
	case 1000:
		ctrl = MII_CTRL_SPEED_1000;
		break;
	default:
		printf("%s: invalid MII speed (%d) set for arge unit: %d\n",
		    __func__, speed, unit);
		return;
	}

	val = ATH_READ_REG(reg);
	val &= ~(MII_CTRL_SPEED_MASK << MII_CTRL_SPEED_SHIFT);
	val |= (ctrl & MII_CTRL_SPEED_MASK) << MII_CTRL_SPEED_SHIFT;
	ATH_WRITE_REG(reg, val);
}

void
ar71xx_chip_set_mii_if(uint32_t unit, uint32_t mii_mode)
{
	uint32_t val, reg, mii_if;

	switch (unit) {
	case 0:
		reg = AR71XX_MII0_CTRL;
		if (mii_mode == AR71XX_MII_MODE_GMII)
			mii_if = MII0_CTRL_IF_GMII;
		else if (mii_mode == AR71XX_MII_MODE_MII)
			mii_if = MII0_CTRL_IF_MII;
		else if (mii_mode == AR71XX_MII_MODE_RGMII)
			mii_if = MII0_CTRL_IF_RGMII;
		else if (mii_mode == AR71XX_MII_MODE_RMII)
			mii_if = MII0_CTRL_IF_RMII;
		else {
			printf("%s: invalid MII mode (%d) for unit %d\n",
			    __func__, mii_mode, unit);
			return;
		}
		break;
	case 1:
		reg = AR71XX_MII1_CTRL;
		if (mii_mode == AR71XX_MII_MODE_RGMII)
			mii_if = MII1_CTRL_IF_RGMII;
		else if (mii_mode == AR71XX_MII_MODE_RMII)
			mii_if = MII1_CTRL_IF_RMII;
		else {
			printf("%s: invalid MII mode (%d) for unit %d\n",
			    __func__, mii_mode, unit);
			return;
		}
		break;
	default:
		printf("%s: invalid MII unit set for arge unit: %d\n",
		    __func__, unit);
		return;
	}

	val = ATH_READ_REG(reg);
	val &= ~(MII_CTRL_IF_MASK << MII_CTRL_IF_SHIFT);
	val |= (mii_if & MII_CTRL_IF_MASK) << MII_CTRL_IF_SHIFT;
	ATH_WRITE_REG(reg, val);
}

/* Speed is either 10, 100 or 1000 */
static void
ar71xx_chip_set_pll_ge(int unit, int speed, uint32_t pll)
{

	switch (unit) {
	case 0:
		ar71xx_write_pll(AR71XX_PLL_SEC_CONFIG,
		    AR71XX_PLL_ETH_INT0_CLK, pll,
		    AR71XX_PLL_ETH0_SHIFT);
		break;
	case 1:
		ar71xx_write_pll(AR71XX_PLL_SEC_CONFIG,
		    AR71XX_PLL_ETH_INT1_CLK, pll,
		    AR71XX_PLL_ETH1_SHIFT);
		break;
	default:
		printf("%s: invalid PLL set for arge unit: %d\n",
		    __func__, unit);
		return;
	}
}

static void
ar71xx_chip_ddr_flush(ar71xx_flush_ddr_id_t id)
{

	switch (id) {
	case AR71XX_CPU_DDR_FLUSH_GE0:
		ar71xx_ddr_flush(AR71XX_WB_FLUSH_GE0);
		break;
	case AR71XX_CPU_DDR_FLUSH_GE1:
		ar71xx_ddr_flush(AR71XX_WB_FLUSH_GE1);
		break;
	case AR71XX_CPU_DDR_FLUSH_USB:
		ar71xx_ddr_flush(AR71XX_WB_FLUSH_USB);
		break;
	case AR71XX_CPU_DDR_FLUSH_PCIE:
		ar71xx_ddr_flush(AR71XX_WB_FLUSH_PCI);
		break;
	default:
		printf("%s: invalid DDR flush id (%d)\n", __func__, id);
		break;
	}
}

static uint32_t
ar71xx_chip_get_eth_pll(unsigned int mac, int speed)
{
	uint32_t pll;

	switch (speed) {
	case 10:
		pll = PLL_ETH_INT_CLK_10;
		break;
	case 100:
		pll = PLL_ETH_INT_CLK_100;
		break;
	case 1000:
		pll = PLL_ETH_INT_CLK_1000;
		break;
	default:
		printf("%s%d: invalid speed %d\n", __func__, mac, speed);
		pll = 0;
	}

	return (pll);
}

static void
ar71xx_chip_init_usb_peripheral(void)
{

	ar71xx_device_stop(RST_RESET_USB_OHCI_DLL |
	    RST_RESET_USB_HOST | RST_RESET_USB_PHY);
	DELAY(1000);

	ar71xx_device_start(RST_RESET_USB_OHCI_DLL |
	    RST_RESET_USB_HOST | RST_RESET_USB_PHY);
	DELAY(1000);

	ATH_WRITE_REG(AR71XX_USB_CTRL_CONFIG,
	    USB_CTRL_CONFIG_OHCI_DES_SWAP |
	    USB_CTRL_CONFIG_OHCI_BUF_SWAP |
	    USB_CTRL_CONFIG_EHCI_DES_SWAP |
	    USB_CTRL_CONFIG_EHCI_BUF_SWAP);

	ATH_WRITE_REG(AR71XX_USB_CTRL_FLADJ,
	    (32 << USB_CTRL_FLADJ_HOST_SHIFT) |
	    (3 << USB_CTRL_FLADJ_A5_SHIFT));

	DELAY(1000);
}

struct ar71xx_cpu_def ar71xx_chip_def = {
	&ar71xx_chip_detect_mem_size,
	&ar71xx_chip_detect_sys_frequency,
	&ar71xx_chip_device_stop,
	&ar71xx_chip_device_start,
	&ar71xx_chip_device_stopped,
	&ar71xx_chip_set_pll_ge,
	&ar71xx_chip_set_mii_speed,
	&ar71xx_chip_set_mii_if,
	&ar71xx_chip_get_eth_pll,
	&ar71xx_chip_ddr_flush,
	&ar71xx_chip_init_usb_peripheral,
};
