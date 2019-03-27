/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ian Lepore <ian@freebsd.org>
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
#include <machine/machdep.h>
#include <machine/platformvar.h> 

#include <arm/freescale/imx/imx_machdep.h>

#include "platform_if.h"

static platform_attach_t imx53_attach;
static platform_devmap_init_t imx53_devmap_init;
static platform_cpu_reset_t imx53_cpu_reset;

static int
imx53_attach(platform_t plat)
{

	return (0);
}

/*
 * Set up static device mappings.  This is hand-optimized platform-specific
 * config data which covers most of the common on-chip devices with a few 1MB
 * section mappings.
 *
 * Notably missing are entries for GPU, IPU, in general anything video related.
 */
static int
imx53_devmap_init(platform_t plat)
{

	devmap_add_entry(0x50000000, 0x00100000);
	devmap_add_entry(0x53f00000, 0x00100000);
	devmap_add_entry(0x63f00000, 0x00100000);

	return (0);
}

static void
imx53_cpu_reset(platform_t plat)
{

	imx_wdog_cpu_reset(0x53F98000);
}

u_int
imx_soc_type(void)
{
	return (IMXSOC_53);
}

static platform_method_t imx53_methods[] = {
	PLATFORMMETHOD(platform_attach,		imx53_attach),
	PLATFORMMETHOD(platform_devmap_init,	imx53_devmap_init),
	PLATFORMMETHOD(platform_cpu_reset,	imx53_cpu_reset),

	PLATFORMMETHOD_END,
};

FDT_PLATFORM_DEF(imx53, "i.MX53", 0, "fsl,imx53", 100);
