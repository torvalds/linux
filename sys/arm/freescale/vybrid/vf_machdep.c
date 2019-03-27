/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ruslan Bukin <br@bsdpad.com>
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
#include <sys/devmap.h>

#include <vm/vm.h>

#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/platform.h> 
#include <machine/platformvar.h>

#include <arm/freescale/vybrid/vf_src.h>

#include "platform_if.h"

static int
vf_devmap_init(platform_t plat)
{

	devmap_add_entry(0x40000000, 0x100000);

	return (0);
}

static void
vf_cpu_reset(platform_t plat)
{
	phandle_t src;
	uint32_t paddr;
	bus_addr_t vaddr;

	if (src_swreset() == 0)
		goto end;

	src = OF_finddevice("src");
	if ((src != -1) && (OF_getencprop(src, "reg", &paddr, sizeof(paddr))) > 0) {
		if (bus_space_map(fdtbus_bs_tag, paddr, 0x10, 0, &vaddr) == 0) {
			bus_space_write_4(fdtbus_bs_tag, vaddr, 0x00, SW_RST);
		}
	}

end:
	while (1);
}

static platform_method_t vf_methods[] = {
	PLATFORMMETHOD(platform_devmap_init,	vf_devmap_init),
	PLATFORMMETHOD(platform_cpu_reset,	vf_cpu_reset),

	PLATFORMMETHOD_END,
};

FDT_PLATFORM_DEF(vf, "vybrid", 0, "freescale,vybrid", 200);
