/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/reboot.h>
#include <sys/devmap.h>

#include <vm/vm.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>
#include <machine/machdep.h>
#include <machine/platformvar.h>

#include <dev/ofw/openfirm.h>

#include <arm/nvidia/tegra124/tegra124_mp.h>

#include "platform_if.h"

#define	PMC_PHYSBASE		0x7000e400
#define	PMC_SIZE		0x400
#define	PMC_CONTROL_REG		0x0
#define	PMC_SCRATCH0		0x50
#define	 PMC_SCRATCH0_MODE_RECOVERY	(1 << 31)
#define	 PMC_SCRATCH0_MODE_BOOTLOADER	(1 << 30)
#define	 PMC_SCRATCH0_MODE_RCM		(1 << 1)
#define	 PMC_SCRATCH0_MODE_MASK		(PMC_SCRATCH0_MODE_RECOVERY | \
					PMC_SCRATCH0_MODE_BOOTLOADER | \
					PMC_SCRATCH0_MODE_RCM)

static platform_attach_t tegra124_attach;
static platform_lastaddr_t tegra124_lastaddr;
static platform_devmap_init_t tegra124_devmap_init;
static platform_late_init_t tegra124_late_init;
static platform_cpu_reset_t tegra124_cpu_reset;

static int
tegra124_attach(platform_t plat)
{

	return (0);
}

static void
tegra124_late_init(platform_t plat)
{

}

/*
 * Set up static device mappings.
 *
 */
static int
tegra124_devmap_init(platform_t plat)
{

	devmap_add_entry(0x70000000, 0x01000000);
	return (0);
}

static void
tegra124_cpu_reset(platform_t plat)
{
	bus_space_handle_t pmc;
	uint32_t reg;

	printf("Resetting...\n");
	bus_space_map(fdtbus_bs_tag, PMC_PHYSBASE, PMC_SIZE, 0, &pmc);

	reg = bus_space_read_4(fdtbus_bs_tag, pmc, PMC_SCRATCH0);
	reg &= PMC_SCRATCH0_MODE_MASK;
	bus_space_write_4(fdtbus_bs_tag, pmc, PMC_SCRATCH0,
	   reg | PMC_SCRATCH0_MODE_BOOTLOADER); 	/* boot to bootloader */
	bus_space_read_4(fdtbus_bs_tag, pmc, PMC_SCRATCH0);

	reg = bus_space_read_4(fdtbus_bs_tag, pmc, PMC_CONTROL_REG);
	spinlock_enter();
	dsb();
	bus_space_write_4(fdtbus_bs_tag, pmc, PMC_CONTROL_REG, reg | 0x10);
	bus_space_read_4(fdtbus_bs_tag, pmc, PMC_CONTROL_REG);
	while(1)
		;

}

/*
 * Early putc routine for EARLY_PRINTF support.  To use, add to kernel config:
 *   option SOCDEV_PA=0x70000000
 *   option SOCDEV_VA=0x70000000
 *   option EARLY_PRINTF
 */
#if 0
#ifdef EARLY_PRINTF
static void
tegra124_early_putc(int c)
{

	volatile uint32_t * UART_STAT_REG = (uint32_t *)(0x70006314);
	volatile uint32_t * UART_TX_REG   = (uint32_t *)(0x70006300);
	const uint32_t      UART_TXRDY    = (1 << 6);
	while ((*UART_STAT_REG & UART_TXRDY) == 0)
		continue;
	*UART_TX_REG = c;
}
early_putc_t *early_putc = tegra124_early_putc;
#endif
#endif

static platform_method_t tegra124_methods[] = {
	PLATFORMMETHOD(platform_attach,		tegra124_attach),
	PLATFORMMETHOD(platform_devmap_init,	tegra124_devmap_init),
	PLATFORMMETHOD(platform_late_init,	tegra124_late_init),
	PLATFORMMETHOD(platform_cpu_reset,	tegra124_cpu_reset),

#ifdef SMP
	PLATFORMMETHOD(platform_mp_start_ap,	tegra124_mp_start_ap),
	PLATFORMMETHOD(platform_mp_setmaxid,	tegra124_mp_setmaxid),
#endif
	PLATFORMMETHOD_END,
};

FDT_PLATFORM_DEF(tegra124, "Nvidia Jetson-TK1", 0, "nvidia,jetson-tk1", 120);
