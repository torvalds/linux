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

#include <dev/ofw/ofw_bus.h>

#include "cpufreq_if.h"

struct pcr_softc {
	device_t dev;
	uint32_t pcr_vals[3];
	int nmodes;
};

static void	pcr_identify(driver_t *driver, device_t parent);
static int	pcr_probe(device_t dev);
static int	pcr_attach(device_t dev);
static int	pcr_settings(device_t dev, struct cf_setting *sets, int *count);
static int	pcr_set(device_t dev, const struct cf_setting *set);
static int	pcr_get(device_t dev, struct cf_setting *set);
static int	pcr_type(device_t dev, int *type);

static device_method_t pcr_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	pcr_identify),
	DEVMETHOD(device_probe,		pcr_probe),
	DEVMETHOD(device_attach,	pcr_attach),

	/* cpufreq interface */
	DEVMETHOD(cpufreq_drv_set,	pcr_set),
	DEVMETHOD(cpufreq_drv_get,	pcr_get),
	DEVMETHOD(cpufreq_drv_type,	pcr_type),
	DEVMETHOD(cpufreq_drv_settings,	pcr_settings),

	{0, 0}
};

static driver_t pcr_driver = {
	"pcr",
	pcr_methods,
	sizeof(struct pcr_softc)
};

static devclass_t pcr_devclass;
DRIVER_MODULE(pcr, cpu, pcr_driver, pcr_devclass, 0, 0);

/*
 * States
 */

#define PCR_TO_FREQ(a)	((a >> 17) & 3)

#define	PCR_FULL	0
#define PCR_HALF	1
#define PCR_QUARTER	2		/* Only on 970MP */

#define PSR_RECEIVED	(1ULL << 61)
#define PSR_COMPLETED	(1ULL << 61)

/*
 * SCOM addresses
 */

#define	SCOM_PCR	0x0aa00100	/* Power Control Register */
#define SCOM_PCR_BIT	0x80000000	/* Data bit for PCR */
#define SCOM_PSR	0x40800100	/* Power Status Register */

/*
 * SCOM Glue
 */

#define SCOMC_READ	0x00008000
#define SCOMC_WRITE	0x00000000 

static void
write_scom(register_t address, uint64_t value)
{
	register_t msr;
	#ifndef __powerpc64__
	register_t hi, lo, scratch;
	#endif

	msr = mfmsr();
	mtmsr(msr & ~PSL_EE); isync();

	#ifdef __powerpc64__
	mtspr(SPR_SCOMD, value);
	#else
	hi = (value >> 32) & 0xffffffff;
	lo = value & 0xffffffff;
	mtspr64(SPR_SCOMD, hi, lo, scratch); 
	#endif
	isync();
	mtspr(SPR_SCOMC, address | SCOMC_WRITE);
	isync();

	mtmsr(msr); isync();
}

static uint64_t
read_scom(register_t address)
{
	register_t msr;
	uint64_t ret;

	msr = mfmsr();
	mtmsr(msr & ~PSL_EE); isync();

	mtspr(SPR_SCOMC, address | SCOMC_READ);
	isync();

	__asm __volatile ("mfspr %0,%1;"
            " mr %0+1, %0; srdi %0,%0,32" : "=r" (ret) : "K" (SPR_SCOMD));

	(void)mfspr(SPR_SCOMC); /* Complete transcation */

	mtmsr(msr); isync();

	return (ret);
}

static void
pcr_identify(driver_t *driver, device_t parent)
{
	uint16_t vers;
	vers = mfpvr() >> 16;

	/* Check for an IBM 970-class CPU */
	switch (vers) {
		case IBM970FX:
		case IBM970GX:
		case IBM970MP:
			break;
		default:
			return;
	}

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "pcr", -1) != NULL)
		return;

	/*
	 * We attach a child for every CPU since settings need to
	 * be performed on every CPU in the SMP case.
	 */
	if (BUS_ADD_CHILD(parent, 10, "pcr", -1) == NULL)
		device_printf(parent, "add pcr child failed\n");
}

static int
pcr_probe(device_t dev)
{
	if (resource_disabled("pcr", 0))
		return (ENXIO);

	device_set_desc(dev, "PPC 970 Power Control Register");
	return (0);
}

static int
pcr_attach(device_t dev)
{
	struct pcr_softc *sc;
	phandle_t cpu;
	uint32_t modes[3];
	int i;

	sc = device_get_softc(dev);
	sc->dev = dev;

	cpu = ofw_bus_get_node(device_get_parent(dev));

	if (cpu <= 0) {
		device_printf(dev,"No CPU device tree node!\n");
		return (ENXIO);
	}

	if (OF_getproplen(cpu, "power-mode-data") <= 0) {
		/* Use the first CPU's node */
		cpu = OF_child(OF_parent(cpu));
	}

	/*
	 * Collect the PCR values for each mode from the device tree.
	 * These include bus timing information, and so cannot be
	 * directly computed.
	 */
	sc->nmodes = OF_getproplen(cpu, "power-mode-data");
	if (sc->nmodes <= 0 || sc->nmodes > sizeof(sc->pcr_vals)) {
		device_printf(dev,"No power mode data in device tree!\n");
		return (ENXIO);
	}
	OF_getprop(cpu, "power-mode-data", modes, sc->nmodes);
	sc->nmodes /= sizeof(modes[0]);

	/* Sort the modes */
	for (i = 0; i < sc->nmodes; i++)
		sc->pcr_vals[PCR_TO_FREQ(modes[i])] = modes[i];

	cpufreq_register(dev);
	return (0);
}

static int
pcr_settings(device_t dev, struct cf_setting *sets, int *count)
{
	struct pcr_softc *sc;

	sc = device_get_softc(dev);
	if (sets == NULL || count == NULL)
		return (EINVAL);
	if (*count < sc->nmodes)
		return (E2BIG);

	/* Return a list of valid settings for this driver. */
	memset(sets, CPUFREQ_VAL_UNKNOWN, sizeof(*sets) * sc->nmodes);

	sets[0].freq = 10000; sets[0].dev = dev;
	sets[1].freq = 5000; sets[1].dev = dev;
	if (sc->nmodes > 2) {
		sets[2].freq = 2500;
		sets[2].dev = dev;
	}
	*count = sc->nmodes;

	return (0);
}

static int
pcr_set(device_t dev, const struct cf_setting *set)
{
	struct pcr_softc *sc;
	register_t pcr, msr;
	uint64_t psr;
	
	if (set == NULL)
		return (EINVAL);
	sc = device_get_softc(dev);

	/* Construct the new PCR */

	pcr = SCOM_PCR_BIT;

	if (set->freq == 10000)
		pcr |= sc->pcr_vals[0];
	else if (set->freq == 5000)
		pcr |= sc->pcr_vals[1];
	else if (set->freq == 2500)
		pcr |= sc->pcr_vals[2];

	msr = mfmsr(); 
	mtmsr(msr & ~PSL_EE); isync();

	/* 970MP requires PCR and PCRH to be cleared first */

	write_scom(SCOM_PCR,0);			/* Clear PCRH */
	write_scom(SCOM_PCR,SCOM_PCR_BIT);	/* Clear PCR */

	/* Set PCR */

	write_scom(SCOM_PCR, pcr);

	/* Wait for completion */

	do {
		DELAY(100);
		psr = read_scom(SCOM_PSR);
	} while ((psr & PSR_RECEIVED) && !(psr & PSR_COMPLETED));

	mtmsr(msr); isync();

	return (0);
}

static int
pcr_get(device_t dev, struct cf_setting *set)
{
	uint64_t psr;

	if (set == NULL)
		return (EINVAL);

	memset(set, CPUFREQ_VAL_UNKNOWN, sizeof(*set));

	psr = read_scom(SCOM_PSR);

	/* We want bits 6 and 7 */
	psr = (psr >> 56) & 3;

	set->freq = 10000;
	if (psr == PCR_HALF)
		set->freq = 5000;
	else if (psr == PCR_QUARTER)
		set->freq = 2500;

	set->dev = dev;

	return (0);
}

static int
pcr_type(device_t dev, int *type)
{

	if (type == NULL)
		return (EINVAL);

	*type = CPUFREQ_TYPE_RELATIVE;
	return (0);
}

