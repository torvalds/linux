/*-
 * Copyright (c) 2016 Hiroki Mori
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
#include <sys/socket.h>
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

#include <mips/atheros/ar531x/ar5312reg.h>
#include <mips/atheros/ar531x/ar5315reg.h>
#include <mips/atheros/ar531x/ar5315_cpudef.h>
#include <mips/atheros/ar531x/ar5315_setup.h>

static void
ar5312_chip_detect_mem_size(void)
{
	uint32_t memsize;
	uint32_t memcfg, bank0, bank1;

	/*
	 * Determine the memory size as established by system
	 * firmware.
	 *
	 * NB: we allow compile time override
	 */
	memcfg = ATH_READ_REG(AR5312_SDRAMCTL_BASE + AR5312_SDRAMCTL_MEM_CFG1);
	bank0 = __SHIFTOUT(memcfg, AR5312_MEM_CFG1_BANK0);
	bank1 = __SHIFTOUT(memcfg, AR5312_MEM_CFG1_BANK1);

	memsize = (bank0 ? (1 << (bank0 + 1)) : 0) +
	    (bank1 ? (1 << (bank1 + 1)) : 0);
	memsize <<= 20;

	realmem = memsize;
}

static void
ar5312_chip_detect_sys_frequency(void)
{
	uint32_t	predivisor;
	uint32_t	multiplier;


	const uint32_t clockctl = ATH_READ_REG(AR5312_SYSREG_BASE + AR5312_SYSREG_CLOCKCTL);
	if(ar531x_soc == AR531X_SOC_AR5313) {
		predivisor = __SHIFTOUT(clockctl, AR2313_CLOCKCTL_PREDIVIDE);
		multiplier = __SHIFTOUT(clockctl, AR2313_CLOCKCTL_MULTIPLIER);
	} else {
		predivisor = __SHIFTOUT(clockctl, AR5312_CLOCKCTL_PREDIVIDE);
		multiplier = __SHIFTOUT(clockctl, AR5312_CLOCKCTL_MULTIPLIER);
	}

	const uint32_t divisor = (0x5421 >> (predivisor * 4)) & 15;

	const uint32_t cpufreq = (40000000 / divisor) * multiplier;

	u_ar531x_cpu_freq = cpufreq;
	u_ar531x_ahb_freq = cpufreq / 4;
	u_ar531x_ddr_freq = 0;
}

/*
 * This does not lock the CPU whilst doing the work!
 */
static void
ar5312_chip_device_reset(void)
{
	ATH_WRITE_REG(AR5312_SYSREG_BASE + AR5312_SYSREG_RESETCTL,
		AR5312_RESET_SYSTEM);
}

static void
ar5312_chip_device_start(void)
{
	uint32_t cfg0, cfg1;
	uint32_t bank0, bank1;
	uint32_t size0, size1;

	cfg0 = ATH_READ_REG(AR5312_SDRAMCTL_BASE + AR5312_SDRAMCTL_MEM_CFG0);
	cfg1 = ATH_READ_REG(AR5312_SDRAMCTL_BASE + AR5312_SDRAMCTL_MEM_CFG1);

	bank0 = __SHIFTOUT(cfg1, AR5312_MEM_CFG1_BANK0);
	bank1 = __SHIFTOUT(cfg1, AR5312_MEM_CFG1_BANK1);

	size0 = bank0 ? (1 << (bank0 + 1)) : 0;
	size1 = bank1 ? (1 << (bank1 + 1)) : 0;

	size0 <<= 20;
	size1 <<= 20;

	printf("SDRMCTL %x %x %x %x\n", cfg0, cfg1, size0, size1);

	ATH_READ_REG(AR5312_SYSREG_BASE + AR5312_SYSREG_AHBPERR);
	ATH_READ_REG(AR5312_SYSREG_BASE + AR5312_SYSREG_AHBDMAE);
//	ATH_WRITE_REG(AR5312_SYSREG_BASE + AR5312_SYSREG_WDOG_CTL, 0);
	ATH_WRITE_REG(AR5312_SYSREG_BASE + AR5312_SYSREG_ENABLE, 0);

	ATH_WRITE_REG(AR5312_SYSREG_BASE+AR5312_SYSREG_ENABLE,
		ATH_READ_REG(AR5312_SYSREG_BASE+AR5312_SYSREG_ENABLE) |
		AR5312_ENABLE_ENET0 | AR5312_ENABLE_ENET1);

}

static int
ar5312_chip_device_stopped(uint32_t mask)
{
	uint32_t reg;

	reg = ATH_READ_REG(AR5312_SYSREG_BASE + AR5312_SYSREG_RESETCTL);
	return ((reg & mask) == mask);
}

static void
ar5312_chip_set_mii_speed(uint32_t unit, uint32_t speed)
{
}

/* Speed is either 10, 100 or 1000 */
static void
ar5312_chip_set_pll_ge(int unit, int speed)
{
}

static void
ar5312_chip_ddr_flush_ge(int unit)
{
}

static void
ar5312_chip_soc_init(void)
{

	u_ar531x_uart_addr = MIPS_PHYS_TO_KSEG1(AR5312_UART0_BASE);

	u_ar531x_gpio_di = AR5312_GPIO_DI;
	u_ar531x_gpio_do = AR5312_GPIO_DO;
	u_ar531x_gpio_cr = AR5312_GPIO_CR;
	u_ar531x_gpio_pins = AR5312_GPIO_PINS;

	u_ar531x_wdog_ctl = AR5312_SYSREG_WDOG_CTL;
	u_ar531x_wdog_timer = AR5312_SYSREG_WDOG_TIMER;

}

static uint32_t
ar5312_chip_get_eth_pll(unsigned int mac, int speed)
{
	return 0;
}

struct ar5315_cpu_def ar5312_chip_def = {
	&ar5312_chip_detect_mem_size,
	&ar5312_chip_detect_sys_frequency,
	&ar5312_chip_device_reset,
	&ar5312_chip_device_start,
	&ar5312_chip_device_stopped,
	&ar5312_chip_set_pll_ge,
	&ar5312_chip_set_mii_speed,
	&ar5312_chip_ddr_flush_ge,
	&ar5312_chip_get_eth_pll,
	&ar5312_chip_soc_init,
};
