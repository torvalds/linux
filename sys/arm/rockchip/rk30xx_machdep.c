/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ganbold Tsagaankhuu <ganbold@freebsd.org>
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
 *
 * from: FreeBSD: //depot/projects/arm/src/sys/arm/ti/ti_machdep.c
 */

#include "opt_ddb.h"
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
#include <machine/platform.h>
#include <machine/platformvar.h>

#include <arm/rockchip/rk30xx_wdog.h>
#include <arm/rockchip/rk30xx_mp.h>

#include "platform_if.h"

static platform_devmap_init_t rk30xx_devmap_init;
static platform_late_init_t rk30xx_late_init;
static platform_cpu_reset_t rk30xx_cpu_reset;

static void
rk30xx_late_init(platform_t plat)
{

	/* Enable cache */
	cpufunc_control(CPU_CONTROL_DC_ENABLE|CPU_CONTROL_IC_ENABLE,
	    CPU_CONTROL_DC_ENABLE|CPU_CONTROL_IC_ENABLE);
}

/*
 * Set up static device mappings.
 */
static int
rk30xx_devmap_init(platform_t plat)
{

	devmap_add_entry(0x10000000, 0x00200000);
	devmap_add_entry(0x20000000, 0x00100000);
	
	return (0);
}

static void
rk30xx_cpu_reset(platform_t plat)
{

	rk30_wd_watchdog_reset();
	printf("Reset failed!\n");
	while (1);
}

#if defined(SOC_ROCKCHIP_RK3188)
static platform_method_t rk30xx_methods[] = {
	PLATFORMMETHOD(platform_devmap_init,	rk30xx_devmap_init),
	PLATFORMMETHOD(platform_late_init,	rk30xx_late_init),
	PLATFORMMETHOD(platform_cpu_reset,	rk30xx_cpu_reset),

#ifdef SMP
	PLATFORMMETHOD(platform_mp_start_ap,	rk30xx_mp_start_ap),
	PLATFORMMETHOD(platform_mp_setmaxid,	rk30xx_mp_setmaxid),
#endif
	PLATFORMMETHOD_END,
};
FDT_PLATFORM_DEF(rk30xx, "RK3188", 0, "rockchip,rk3188", 200);
#endif
