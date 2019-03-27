/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1994-1998 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * from: FreeBSD: //depot/projects/arm/src/sys/arm/at91/kb920x_machdep.c, rev 45
 */

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/devmap.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/machdep.h>
#include <machine/platformvar.h>

#include <arm/ti/omap4/omap4_machdep.h>
#include <arm/ti/omap4/omap4_reg.h>
#include <arm/ti/ti_cpuid.h>

#include "platform_if.h"

#if defined(SOC_OMAP4)
#include "platform_pl310_if.h"

static platform_attach_t omap4_attach;
static platform_devmap_init_t ti_omap4_devmap_init;
#endif
#if defined(SOC_TI_AM335X)
static platform_attach_t ti_am335x_attach;
static platform_devmap_init_t ti_am335x_devmap_init;
#endif
static platform_cpu_reset_t ti_plat_cpu_reset;

void (*ti_cpu_reset)(void) = NULL;

int _ti_chip = -1;

#if defined(SOC_OMAP4)
static int
omap4_attach(platform_t plat)
{
	_ti_chip = CHIP_OMAP_4;
	return (0);
}
#endif

#if defined(SOC_TI_AM335X)
static int
ti_am335x_attach(platform_t plat)
{
	_ti_chip = CHIP_AM335X;
	return (0);
}
#endif

/*
 * Construct static devmap entries to map out the most frequently used
 * peripherals using 1mb section mappings.
 */
#if defined(SOC_OMAP4)
static int
ti_omap4_devmap_init(platform_t plat)
{
	devmap_add_entry(0x48000000, 0x01000000); /*16mb L4_PER devices */
	devmap_add_entry(0x4A000000, 0x01000000); /*16mb L4_CFG devices */
	return (0);
}
#endif

#if defined(SOC_TI_AM335X)
static int
ti_am335x_devmap_init(platform_t plat)
{

	devmap_add_entry(0x44C00000, 0x00400000); /* 4mb L4_WKUP devices*/
	devmap_add_entry(0x47400000, 0x00100000); /* 1mb USB            */
	devmap_add_entry(0x47800000, 0x00100000); /* 1mb mmchs2         */
	devmap_add_entry(0x48000000, 0x01000000); /*16mb L4_PER devices */
	devmap_add_entry(0x49000000, 0x00100000); /* 1mb edma3          */
	devmap_add_entry(0x49800000, 0x00300000); /* 3mb edma3          */
	devmap_add_entry(0x4A000000, 0x01000000); /*16mb L4_FAST devices*/
	return (0);
}
#endif

static void
ti_plat_cpu_reset(platform_t plat)
{
	if (ti_cpu_reset)
		(*ti_cpu_reset)();
	else
		printf("no cpu_reset implementation\n");
}

#if defined(SOC_OMAP4)
static platform_method_t omap4_methods[] = {
	PLATFORMMETHOD(platform_attach, 	omap4_attach),
	PLATFORMMETHOD(platform_devmap_init,	ti_omap4_devmap_init),
	PLATFORMMETHOD(platform_cpu_reset,	ti_plat_cpu_reset),

#ifdef SMP
	PLATFORMMETHOD(platform_mp_start_ap,	omap4_mp_start_ap),
	PLATFORMMETHOD(platform_mp_setmaxid,	omap4_mp_setmaxid),
#endif

	PLATFORMMETHOD(platform_pl310_init,	omap4_pl310_init),
	PLATFORMMETHOD(platform_pl310_write_ctrl, omap4_pl310_write_ctrl),
	PLATFORMMETHOD(platform_pl310_write_debug, omap4_pl310_write_debug),

	PLATFORMMETHOD_END,
};
FDT_PLATFORM_DEF(omap4, "omap4", 0, "ti,omap4430", 200);
#endif

#if defined(SOC_TI_AM335X)
static platform_method_t am335x_methods[] = {
	PLATFORMMETHOD(platform_attach, 	ti_am335x_attach),
	PLATFORMMETHOD(platform_devmap_init,	ti_am335x_devmap_init),
	PLATFORMMETHOD(platform_cpu_reset,	ti_plat_cpu_reset),

	PLATFORMMETHOD_END,
};

FDT_PLATFORM_DEF(am335x, "am335x", 0, "ti,am33xx", 200);
#endif
