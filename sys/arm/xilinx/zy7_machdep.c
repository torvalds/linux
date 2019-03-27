/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Thomas Skibo
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
 *
 * $FreeBSD$
 */

/*
 * Machine dependent code for Xilinx Zynq-7000 Soc.
 *
 * Reference: Zynq-7000 All Programmable SoC Technical Reference Manual.
 * (v1.4) November 16, 2012.  Xilinx doc UG585.
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
#include <machine/platform.h> 
#include <machine/platformvar.h>

#include <arm/xilinx/zy7_machdep.h>
#include <arm/xilinx/zy7_reg.h>

#include "platform_if.h"
#include "platform_pl310_if.h"

void (*zynq7_cpu_reset)(void);

/*
 * Set up static device mappings.  Not strictly necessary -- simplebus will
 * dynamically establish mappings as needed -- but doing it this way gets us
 * nice efficient 1MB section mappings.
 */
static int
zynq7_devmap_init(platform_t plat)
{

	devmap_add_entry(ZYNQ7_PSIO_HWBASE, ZYNQ7_PSIO_SIZE);
	devmap_add_entry(ZYNQ7_PSCTL_HWBASE, ZYNQ7_PSCTL_SIZE);

	return (0);
}

static void
zynq7_do_cpu_reset(platform_t plat)
{
	if (zynq7_cpu_reset != NULL)
		(*zynq7_cpu_reset)();

	printf("cpu_reset: no platform cpu_reset.  hanging.\n");
	for (;;)
		;
}

static platform_method_t zynq7_methods[] = {
	PLATFORMMETHOD(platform_devmap_init,	zynq7_devmap_init),
	PLATFORMMETHOD(platform_cpu_reset,	zynq7_do_cpu_reset),

#ifdef SMP
	PLATFORMMETHOD(platform_mp_setmaxid,	zynq7_mp_setmaxid),
	PLATFORMMETHOD(platform_mp_start_ap,	zynq7_mp_start_ap),
#endif

	PLATFORMMETHOD(platform_pl310_init,	zynq7_pl310_init),

	PLATFORMMETHOD_END,
};

FDT_PLATFORM_DEF(zynq7, "zynq7", 0, "xlnx,zynq-7000", 200);
