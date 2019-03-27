/*-
 * Copyright (c) 2014-2017 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
#include <sys/devmap.h>

#include <vm/vm.h>

#include <dev/ofw/openfirm.h>

#include <machine/armreg.h>
#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/machdep.h>
#include <machine/platform.h>
#include <machine/platformvar.h>

#include <arm/altera/socfpga/socfpga_mp.h>
#include <arm/altera/socfpga/socfpga_rstmgr.h>

#include "platform_if.h"

#if defined(SOC_ALTERA_CYCLONE5)
static int
socfpga_devmap_init(platform_t plat)
{

	/* UART */
	devmap_add_entry(0xffc00000, 0x100000);

	/*
	 * USB OTG
	 *
	 * We use static device map for USB due to some bug in the Altera
	 * which throws Translation Fault (P) exception on high load.
	 * It might be caused due to some power save options being turned
	 * on or something else.
	 */
	devmap_add_entry(0xffb00000, 0x100000);

	/* dwmmc */
	devmap_add_entry(0xff700000, 0x100000);

	/* scu */
	devmap_add_entry(0xfff00000, 0x100000);

	/* FPGA memory window, 256MB */
	devmap_add_entry(0xd0000000, 0x10000000);

	return (0);
}
#endif

#if defined(SOC_ALTERA_ARRIA10)
static int
socfpga_a10_devmap_init(platform_t plat)
{

	/* UART */
	devmap_add_entry(0xffc00000, 0x100000);

	/* USB OTG */
	devmap_add_entry(0xffb00000, 0x100000);

	/* dwmmc */
	devmap_add_entry(0xff800000, 0x100000);

	/* scu */
	devmap_add_entry(0xfff00000, 0x100000);

	return (0);
}
#endif

static void
_socfpga_cpu_reset(bus_size_t reg)
{
	uint32_t paddr;
	bus_addr_t vaddr;
	phandle_t node;

	if (rstmgr_warmreset(reg) == 0)
		goto end;

	node = OF_finddevice("/soc/rstmgr");
	if (node == -1)
		goto end;

	if ((OF_getencprop(node, "reg", &paddr, sizeof(paddr))) > 0) {
		if (bus_space_map(fdtbus_bs_tag, paddr, 0x8, 0, &vaddr) == 0) {
			bus_space_write_4(fdtbus_bs_tag, vaddr,
			    reg, CTRL_SWWARMRSTREQ);
		}
	}

end:
	while (1);
}

#if defined(SOC_ALTERA_CYCLONE5)
static void
socfpga_cpu_reset(platform_t plat)
{

	_socfpga_cpu_reset(RSTMGR_CTRL);
}
#endif

#if defined(SOC_ALTERA_ARRIA10)
static void
socfpga_a10_cpu_reset(platform_t plat)
{

	_socfpga_cpu_reset(RSTMGR_A10_CTRL);
}
#endif

#if defined(SOC_ALTERA_CYCLONE5)
static platform_method_t socfpga_methods[] = {
	PLATFORMMETHOD(platform_devmap_init,	socfpga_devmap_init),
	PLATFORMMETHOD(platform_cpu_reset,	socfpga_cpu_reset),
#ifdef SMP
	PLATFORMMETHOD(platform_mp_setmaxid,	socfpga_mp_setmaxid),
	PLATFORMMETHOD(platform_mp_start_ap,	socfpga_mp_start_ap),
#endif
	PLATFORMMETHOD_END,
};
FDT_PLATFORM_DEF(socfpga, "socfpga", 0, "altr,socfpga-cyclone5", 200);
#endif

#if defined(SOC_ALTERA_ARRIA10)
static platform_method_t socfpga_a10_methods[] = {
	PLATFORMMETHOD(platform_devmap_init,	socfpga_a10_devmap_init),
	PLATFORMMETHOD(platform_cpu_reset,	socfpga_a10_cpu_reset),
#ifdef SMP
	PLATFORMMETHOD(platform_mp_setmaxid,	socfpga_mp_setmaxid),
	PLATFORMMETHOD(platform_mp_start_ap,	socfpga_a10_mp_start_ap),
#endif
	PLATFORMMETHOD_END,
};
FDT_PLATFORM_DEF(socfpga_a10, "socfpga", 0, "altr,socfpga-arria10", 200);
#endif
