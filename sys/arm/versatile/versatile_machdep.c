/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2012 Oleksandr Tymoshenko.
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

#include "platform_if.h"

/* Start of address space used for bootstrap map */
#define DEVMAP_BOOTSTRAP_MAP_START	0xE0000000

static vm_offset_t
versatile_lastaddr(platform_t plat)
{

	return (DEVMAP_BOOTSTRAP_MAP_START);
}

#define FDT_DEVMAP_MAX	(2)		/* FIXME */
static struct devmap_entry fdt_devmap[FDT_DEVMAP_MAX] = {
	{ 0, 0, 0, },
	{ 0, 0, 0, }
};


/*
 * Construct devmap table with DT-derived config data.
 */
static int
versatile_devmap_init(platform_t plat)
{
	int i = 0;
	fdt_devmap[i].pd_va = 0xf0100000;
	fdt_devmap[i].pd_pa = 0x10100000;
	fdt_devmap[i].pd_size = 0x01000000;       /* 1 MB */

	devmap_register_table(&fdt_devmap[0]);
	return (0);
}

static void
versatile_cpu_reset(platform_t plat)
{
	printf("cpu_reset\n");
	while (1);
}

static platform_method_t versatile_methods[] = {
	PLATFORMMETHOD(platform_lastaddr,	versatile_lastaddr),
	PLATFORMMETHOD(platform_devmap_init,	versatile_devmap_init),
	PLATFORMMETHOD(platform_cpu_reset,	versatile_cpu_reset),

	PLATFORMMETHOD_END,
};
FDT_PLATFORM_DEF(versatile, "versatile", 0, "arm,versatile-pb", 1);
