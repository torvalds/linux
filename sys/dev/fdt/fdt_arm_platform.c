/*-
 * Copyright (c) 2013 Andrew Turner
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
 */

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/smp.h>

#include <arm/include/platform.h>
#include <arm/include/platformvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofw_cpu.h>
#include <dev/fdt/fdt_common.h>

#include "platform_if.h"

#define	FDT_PLATFORM(plat)	\
    ((fdt_platform_def_t *)(plat)->cls->baseclasses[0])

#if defined(SMP)
static platform_mp_setmaxid_t fdt_platform_mp_setmaxid;
#endif

static int
fdt_platform_probe(platform_t plat)
{
	const char *compat;
	phandle_t root;

	/*
	 * TODO: Make these KASSERTs, we should only be here if we
	 * are using the FDT platform magic.
	 */
	if (plat->cls == NULL || FDT_PLATFORM(plat) == NULL)
		return 1;

	/* Is the device is compatible? */
	root = OF_finddevice("/");
	compat = FDT_PLATFORM(plat)->fdt_compatible;
	if (ofw_bus_node_is_compatible(root, compat) != 0)
		return 0;

	/* Not compatible, return an error */
	return 1;
}

#if defined(SMP)
static boolean_t
fdt_platform_maxid(u_int id, phandle_t node, u_int addr_cells, pcell_t *reg)
{

	if (mp_maxid < id)
		mp_maxid = id;

	return (true);
}

static void
fdt_platform_mp_setmaxid(platform_t plat)
{

	mp_maxid = PCPU_GET(cpuid);
	mp_ncpus = ofw_cpu_early_foreach(fdt_platform_maxid, true);
	if (mp_ncpus < 1)
		mp_ncpus = 1;
	mp_ncpus = MIN(mp_ncpus, MAXCPU);
}
#endif

platform_method_t fdt_platform_methods[] = {
	PLATFORMMETHOD(platform_probe,	fdt_platform_probe),

#if defined(SMP)
	PLATFORMMETHOD(platform_mp_setmaxid, fdt_platform_mp_setmaxid),
#endif

	PLATFORMMETHOD_END
};

