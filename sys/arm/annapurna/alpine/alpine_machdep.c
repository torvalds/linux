/*-
 * Copyright (c) 2013 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2015 Semihalf
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
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
#include <machine/fdt.h>
#include <machine/platformvar.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>

#include <arm/annapurna/alpine/alpine_mp.h>

#include "platform_if.h"

#define	WDTLOAD		0x000
#define	LOAD_MIN	0x00000001
#define	LOAD_MAX	0xFFFFFFFF
#define	WDTVALUE	0x004
#define	WDTCONTROL	0x008
/* control register masks */
#define	INT_ENABLE	(1 << 0)
#define	RESET_ENABLE	(1 << 1)
#define	WDTLOCK		0xC00
#define	UNLOCK		0x1ACCE551
#define	LOCK		0x00000001

bus_addr_t al_devmap_pa;
bus_addr_t al_devmap_size;

static int
alpine_get_devmap_base(bus_addr_t *pa, bus_addr_t *size)
{
	phandle_t node;

	if ((node = OF_finddevice("/")) == -1)
		return (ENXIO);

	if ((node = fdt_find_compatible(node, "simple-bus", 1)) == 0)
		return (ENXIO);

	return fdt_get_range(node, 0, pa, size);
}

static int
alpine_get_wdt_base(uint32_t *pbase, uint32_t *psize)
{
	phandle_t node;
	u_long base = 0;
	u_long size = 0;

	if (pbase == NULL || psize == NULL)
		return (EINVAL);

	if ((node = OF_finddevice("/")) == -1)
		return (EFAULT);

	if ((node = fdt_find_compatible(node, "simple-bus", 1)) == 0)
		return (EFAULT);

	if ((node =
	    fdt_find_compatible(node, "arm,sp805", 1)) == 0)
		return (EFAULT);

	if (fdt_regsize(node, &base, &size))
		return (EFAULT);

	*pbase = base;
	*psize = size;

	return (0);
}

/*
 * Construct devmap table with DT-derived config data.
 */
static int
alpine_devmap_init(platform_t plat)
{
	alpine_get_devmap_base(&al_devmap_pa, &al_devmap_size);
	devmap_add_entry(al_devmap_pa, al_devmap_size);
	return (0);
}

static void
alpine_cpu_reset(platform_t plat)
{
	uint32_t wdbase, wdsize;
	bus_addr_t wdbaddr;
	int ret;

	ret = alpine_get_wdt_base(&wdbase, &wdsize);
	if (ret) {
		printf("Unable to get WDT base, do power down manually...");
		goto infinite;
	}

	ret = bus_space_map(fdtbus_bs_tag, al_devmap_pa + wdbase,
	    wdsize, 0, &wdbaddr);
	if (ret) {
		printf("Unable to map WDT base, do power down manually...");
		goto infinite;
	}

	bus_space_write_4(fdtbus_bs_tag, wdbaddr, WDTLOCK, UNLOCK);
	bus_space_write_4(fdtbus_bs_tag, wdbaddr, WDTLOAD, LOAD_MIN);
	bus_space_write_4(fdtbus_bs_tag, wdbaddr, WDTCONTROL,
	    INT_ENABLE | RESET_ENABLE);

infinite:
	while (1) {}
}

static platform_method_t alpine_methods[] = {
	PLATFORMMETHOD(platform_devmap_init,	alpine_devmap_init),
	PLATFORMMETHOD(platform_cpu_reset,	alpine_cpu_reset),

#ifdef SMP
	PLATFORMMETHOD(platform_mp_start_ap,	alpine_mp_start_ap),
	PLATFORMMETHOD(platform_mp_setmaxid,	alpine_mp_setmaxid),
#endif
	PLATFORMMETHOD_END,
};
FDT_PLATFORM_DEF(alpine, "alpine", 0, "annapurna,alpine", 200);
