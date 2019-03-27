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
#include <mips/atheros/ar724xreg.h>

#include <mips/atheros/ar71xx_cpudef.h>
#include <mips/atheros/ar71xx_setup.h>
#include <mips/atheros/ar71xx_chip.h>
#include <mips/atheros/ar724x_chip.h>

static void
ar724x_chip_detect_mem_size(void)
{
}

static void
ar724x_chip_detect_sys_frequency(void)
{
	uint32_t pll;
	uint32_t freq;
	uint32_t div;

	u_ar71xx_mdio_freq = u_ar71xx_refclk = AR724X_BASE_FREQ;

	pll = ATH_READ_REG(AR724X_PLL_REG_CPU_CONFIG);

	div = ((pll >> AR724X_PLL_DIV_SHIFT) & AR724X_PLL_DIV_MASK);
	freq = div * AR724X_BASE_FREQ;

	div = ((pll >> AR724X_PLL_REF_DIV_SHIFT) & AR724X_PLL_REF_DIV_MASK);
	freq *= div;

	u_ar71xx_cpu_freq = freq;

	div = ((pll >> AR724X_DDR_DIV_SHIFT) & AR724X_DDR_DIV_MASK) + 1;
	u_ar71xx_ddr_freq = freq / div;

	div = (((pll >> AR724X_AHB_DIV_SHIFT) & AR724X_AHB_DIV_MASK) + 1) * 2;
	u_ar71xx_ahb_freq = u_ar71xx_cpu_freq / div;
	u_ar71xx_wdt_freq = u_ar71xx_cpu_freq / div;
	u_ar71xx_uart_freq = u_ar71xx_cpu_freq / div;
}

static void
ar724x_chip_device_stop(uint32_t mask)
{
	uint32_t mask_inv, reg;

	mask_inv = mask & AR724X_RESET_MODULE_USB_OHCI_DLL;
	reg = ATH_READ_REG(AR724X_RESET_REG_RESET_MODULE);
	reg |= mask;
	reg &= ~mask_inv;
	ATH_WRITE_REG(AR724X_RESET_REG_RESET_MODULE, reg);
}

static void
ar724x_chip_device_start(uint32_t mask)
{
	uint32_t mask_inv, reg;

	mask_inv = mask & AR724X_RESET_MODULE_USB_OHCI_DLL;
	reg = ATH_READ_REG(AR724X_RESET_REG_RESET_MODULE);
	reg &= ~mask;
	reg |= mask_inv;
	ATH_WRITE_REG(AR724X_RESET_REG_RESET_MODULE, reg);
}

static int
ar724x_chip_device_stopped(uint32_t mask)
{
	uint32_t reg;

	reg = ATH_READ_REG(AR724X_RESET_REG_RESET_MODULE);
	return ((reg & mask) == mask);
}

static void
ar724x_chip_set_mii_speed(uint32_t unit, uint32_t speed)
{

	/* XXX TODO */
	return;
}

/*
 * XXX TODO: set the PLL for arge0 only on AR7242.
 * The PLL/clock requirements are different.
 *
 * Otherwise, it's a NULL function for AR7240, AR7241 and
 * AR7242 arge1.
 */
static void
ar724x_chip_set_pll_ge(int unit, int speed, uint32_t pll)
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
ar724x_chip_ddr_flush(ar71xx_flush_ddr_id_t id)
{

	switch (id) {
	case AR71XX_CPU_DDR_FLUSH_GE0:
		ar71xx_ddr_flush(AR724X_DDR_REG_FLUSH_GE0);
		break;
	case AR71XX_CPU_DDR_FLUSH_GE1:
		ar71xx_ddr_flush(AR724X_DDR_REG_FLUSH_GE1);
		break;
	case AR71XX_CPU_DDR_FLUSH_USB:
		ar71xx_ddr_flush(AR724X_DDR_REG_FLUSH_USB);
		break;
	case AR71XX_CPU_DDR_FLUSH_PCIE:
		ar71xx_ddr_flush(AR724X_DDR_REG_FLUSH_PCIE);
		break;
	default:
		printf("%s: invalid DDR flush id (%d)\n", __func__, id);
		break;
	}
}

static uint32_t
ar724x_chip_get_eth_pll(unsigned int mac, int speed)
{

	return (0);
}

static void
ar724x_chip_init_usb_peripheral(void)
{

	switch (ar71xx_soc) {
	case AR71XX_SOC_AR7240:
		ar71xx_device_stop(AR724X_RESET_MODULE_USB_OHCI_DLL |
		    AR724X_RESET_USB_HOST);
		DELAY(1000);

		ar71xx_device_start(AR724X_RESET_MODULE_USB_OHCI_DLL |
		    AR724X_RESET_USB_HOST);
		DELAY(1000);

		/*
		 * WAR for HW bug. Here it adjusts the duration
		 * between two SOFS.
		 */
		ATH_WRITE_REG(AR71XX_USB_CTRL_FLADJ,
		    (3 << USB_CTRL_FLADJ_A0_SHIFT));

		break;

	case AR71XX_SOC_AR7241:
	case AR71XX_SOC_AR7242:
		ar71xx_device_start(AR724X_RESET_MODULE_USB_OHCI_DLL);
		DELAY(100);

		ar71xx_device_start(AR724X_RESET_USB_HOST);
		DELAY(100);

		ar71xx_device_start(AR724X_RESET_USB_PHY);
		DELAY(100);

		break;

	default:
		break;
	}
}

struct ar71xx_cpu_def ar724x_chip_def = {
	&ar724x_chip_detect_mem_size,
	&ar724x_chip_detect_sys_frequency,
	&ar724x_chip_device_stop,
	&ar724x_chip_device_start,
	&ar724x_chip_device_stopped,
	&ar724x_chip_set_pll_ge,
	&ar724x_chip_set_mii_speed,
	&ar71xx_chip_set_mii_if,
	&ar724x_chip_get_eth_pll,
	&ar724x_chip_ddr_flush,
	&ar724x_chip_init_usb_peripheral
};
