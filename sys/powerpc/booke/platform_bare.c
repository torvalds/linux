/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2012 Semihalf.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/smp.h>

#include <dev/ofw/openfirm.h>

#include <machine/platform.h>
#include <machine/platformvar.h>

#include "platform_if.h"

extern uint32_t *bootinfo;

static int bare_probe(platform_t);
static void bare_mem_regions(platform_t, struct mem_region *phys, int *physsz,
    struct mem_region *avail, int *availsz);
static u_long bare_timebase_freq(platform_t, struct cpuref *cpuref);

static void bare_reset(platform_t);

static platform_method_t bare_methods[] = {
	PLATFORMMETHOD(platform_probe,		bare_probe),
	PLATFORMMETHOD(platform_mem_regions,	bare_mem_regions),
	PLATFORMMETHOD(platform_timebase_freq,	bare_timebase_freq),

	PLATFORMMETHOD(platform_reset,		bare_reset),

	PLATFORMMETHOD_END
};

static platform_def_t bare_platform = {
	"bare",
	bare_methods,
	0
};

PLATFORM_DEF(bare_platform);

static int
bare_probe(platform_t plat)
{

	if (OF_peer(0) == -1) /* Needs device tree to work */
		return (ENXIO);

	return (BUS_PROBE_GENERIC);
}

void
bare_mem_regions(platform_t plat, struct mem_region *phys, int *physsz,
    struct mem_region *avail, int *availsz)
{

	ofw_mem_regions(phys, physsz, avail, availsz);
}

static u_long
bare_timebase_freq(platform_t plat, struct cpuref *cpuref)
{
	u_long ticks;
	phandle_t cpus, child;
	pcell_t freq;

	if (bootinfo != NULL) {
		if (bootinfo[0] == 1) {
			/* Backward compatibility. See 8-STABLE. */
			ticks = bootinfo[3] >> 3;
		} else {
			/* Compatibility with Juniper's loader. */
			ticks = bootinfo[5] >> 3;
		}
	} else
		ticks = 0;

	if ((cpus = OF_finddevice("/cpus")) == -1)
		goto out;

	if ((child = OF_child(cpus)) == 0)
		goto out;

	switch (OF_getproplen(child, "timebase-frequency")) {
	case 4:
	{
		uint32_t tbase;
		OF_getprop(child, "timebase-frequency", &tbase, sizeof(tbase));
		ticks = tbase;
		return (ticks);
	}
	case 8:
	{
		uint64_t tbase;
		OF_getprop(child, "timebase-frequency", &tbase, sizeof(tbase));
		ticks = tbase;
		return (ticks);
	}
	default:
		break;
	}

	freq = 0;
	if (OF_getprop(child, "bus-frequency", (void *)&freq,
	    sizeof(freq)) <= 0)
		goto out;

	/*
	 * Time Base and Decrementer are updated every 8 CCB bus clocks.
	 * HID0[SEL_TBCLK] = 0
	 */
	if (freq != 0)
		ticks = freq / 8;

out:
	if (ticks <= 0)
		panic("Unable to determine timebase frequency!");

	return (ticks);
}

static void
bare_reset(platform_t plat)
{

	printf("Reset failed...\n");
	while (1)
		;
}

