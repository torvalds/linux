/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Nathan Whitehorn
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include "cpufreq_if.h"

struct dfs_softc {
	device_t dev;
	int	 dfs4;
};

static void	dfs_identify(driver_t *driver, device_t parent);
static int	dfs_probe(device_t dev);
static int	dfs_attach(device_t dev);
static int	dfs_settings(device_t dev, struct cf_setting *sets, int *count);
static int	dfs_set(device_t dev, const struct cf_setting *set);
static int	dfs_get(device_t dev, struct cf_setting *set);
static int	dfs_type(device_t dev, int *type);

static device_method_t dfs_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	dfs_identify),
	DEVMETHOD(device_probe,		dfs_probe),
	DEVMETHOD(device_attach,	dfs_attach),

	/* cpufreq interface */
	DEVMETHOD(cpufreq_drv_set,	dfs_set),
	DEVMETHOD(cpufreq_drv_get,	dfs_get),
	DEVMETHOD(cpufreq_drv_type,	dfs_type),
	DEVMETHOD(cpufreq_drv_settings,	dfs_settings),

	{0, 0}
};

static driver_t dfs_driver = {
	"dfs",
	dfs_methods,
	sizeof(struct dfs_softc)
};

static devclass_t dfs_devclass;
DRIVER_MODULE(dfs, cpu, dfs_driver, dfs_devclass, 0, 0);

/*
 * Bits of the HID1 register to enable DFS. See page 2-24 of "MPC7450
 * RISC Microprocessor Family Reference Manual", rev. 5.
 */

#define	HID1_DFS2	(1UL << 22)
#define	HID1_DFS4	(1UL << 23)

static void
dfs_identify(driver_t *driver, device_t parent)
{
	uint16_t vers;
	vers = mfpvr() >> 16;

	/* Check for an MPC 7447A or 7448 CPU */
	switch (vers) {
		case MPC7447A:
		case MPC7448:
			break;
		default:
			return;
	}

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "dfs", -1) != NULL)
		return;

	/*
	 * We attach a child for every CPU since settings need to
	 * be performed on every CPU in the SMP case.
	 */
	if (BUS_ADD_CHILD(parent, 10, "dfs", -1) == NULL)
		device_printf(parent, "add dfs child failed\n");
}

static int
dfs_probe(device_t dev)
{
	if (resource_disabled("dfs", 0))
		return (ENXIO);

	device_set_desc(dev, "Dynamic Frequency Switching");
	return (0);
}

static int
dfs_attach(device_t dev)
{
	struct dfs_softc *sc;
	uint16_t vers;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->dfs4 = 0;
	vers = mfpvr() >> 16;

	/* The 7448 supports divide-by-four as well */
	if (vers == MPC7448)
		sc->dfs4 = 1;

	cpufreq_register(dev);
	return (0);
}

static int
dfs_settings(device_t dev, struct cf_setting *sets, int *count)
{
	struct dfs_softc *sc;
	int states;

	sc = device_get_softc(dev);
	states = sc->dfs4 ? 3 : 2;
	if (sets == NULL || count == NULL)
		return (EINVAL);
	if (*count < states)
		return (E2BIG);

	/* Return a list of valid settings for this driver. */
	memset(sets, CPUFREQ_VAL_UNKNOWN, sizeof(*sets) * states);

	sets[0].freq = 10000; sets[0].dev = dev;
	sets[1].freq = 5000; sets[1].dev = dev;
	if (sc->dfs4) {
		sets[2].freq = 2500;
		sets[2].dev = dev;
	}
	*count = states;

	return (0);
}

static int
dfs_set(device_t dev, const struct cf_setting *set)
{
	register_t hid1;
	
	if (set == NULL)
		return (EINVAL);

	hid1 = mfspr(SPR_HID1);
	hid1 &= ~(HID1_DFS2 | HID1_DFS4);

	if (set->freq == 5000)
		hid1 |= HID1_DFS2;
	else if (set->freq == 2500)
		hid1 |= HID1_DFS4;
	
	/*
	 * Now set the HID1 register with new values. Calling sequence
	 * taken from page 2-26 of the MPC7450 family CPU manual.
	 */

	powerpc_sync();
	mtspr(SPR_HID1, hid1);
	powerpc_sync(); isync();

	return (0);
}

static int
dfs_get(device_t dev, struct cf_setting *set)
{
	struct dfs_softc *sc;
	register_t hid1;

	if (set == NULL)
		return (EINVAL);
	sc = device_get_softc(dev);

	memset(set, CPUFREQ_VAL_UNKNOWN, sizeof(*set));

	hid1 = mfspr(SPR_HID1);

	set->freq = 10000;
	if (hid1 & HID1_DFS2)
		set->freq = 5000;
	else if (sc->dfs4 && (hid1 & HID1_DFS4))
		set->freq = 2500;

	set->dev = dev;

	return (0);
}

static int
dfs_type(device_t dev, int *type)
{

	if (type == NULL)
		return (EINVAL);

	*type = CPUFREQ_TYPE_RELATIVE;
	return (0);
}

