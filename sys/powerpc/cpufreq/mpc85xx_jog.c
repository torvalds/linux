/*-
 * Copyright (c) 2017 Justin Hibbits
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
#include <sys/smp.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/cpu.h>

#include <powerpc/mpc85xx/mpc85xx.h>

#include "cpufreq_if.h"

/* No worries about uint32_t math overflow in here, because the highest
 * multiplier supported is 4, and the highest speed part is still well below
 * 2GHz.
 */

#define	GUTS_PORPLLSR		(CCSRBAR_VA + 0xe0000)
#define	GUTS_PMJCR		(CCSRBAR_VA + 0xe007c)
#define	  PMJCR_RATIO_M		  0x3f
#define	  PMJCR_CORE_MULT(x,y)	  ((x) << (16 + ((y) * 8)))
#define	  PMJCR_GET_CORE_MULT(x,y)	  (((x) >> (16 + ((y) * 8))) & 0x3f)
#define	GUTS_POWMGTCSR		(CCSRBAR_VA + 0xe0080)
#define	  POWMGTCSR_JOG		  0x00200000
#define	  POWMGTCSR_INT_MASK	  0x00000f00

#define	MHZ	1000000

struct mpc85xx_jog_softc {
	device_t dev;
	int	cpu;
	int	low;
	int	high;
	int	min_freq;
};

static struct ofw_compat_data *mpc85xx_jog_devcompat(void);
static void	mpc85xx_jog_identify(driver_t *driver, device_t parent);
static int	mpc85xx_jog_probe(device_t dev);
static int	mpc85xx_jog_attach(device_t dev);
static int	mpc85xx_jog_settings(device_t dev, struct cf_setting *sets, int *count);
static int	mpc85xx_jog_set(device_t dev, const struct cf_setting *set);
static int	mpc85xx_jog_get(device_t dev, struct cf_setting *set);
static int	mpc85xx_jog_type(device_t dev, int *type);

static device_method_t mpc85xx_jog_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	mpc85xx_jog_identify),
	DEVMETHOD(device_probe,		mpc85xx_jog_probe),
	DEVMETHOD(device_attach,	mpc85xx_jog_attach),

	/* cpufreq interface */
	DEVMETHOD(cpufreq_drv_set,	mpc85xx_jog_set),
	DEVMETHOD(cpufreq_drv_get,	mpc85xx_jog_get),
	DEVMETHOD(cpufreq_drv_type,	mpc85xx_jog_type),
	DEVMETHOD(cpufreq_drv_settings,	mpc85xx_jog_settings),

	{0, 0}
};

static driver_t mpc85xx_jog_driver = {
	"jog",
	mpc85xx_jog_methods,
	sizeof(struct mpc85xx_jog_softc)
};

static devclass_t mpc85xx_jog_devclass;
DRIVER_MODULE(mpc85xx_jog, cpu, mpc85xx_jog_driver, mpc85xx_jog_devclass, 0, 0);

struct mpc85xx_constraints {
	int threshold; /* Threshold frequency, in MHz, for setting CORE_SPD bit. */
	int min_mult;  /* Minimum PLL multiplier. */
};

static struct mpc85xx_constraints mpc8536_constraints = {
	800,
	3
};

static struct mpc85xx_constraints p1022_constraints = {
	500,
	2
};

static struct ofw_compat_data jog_compat[] = {
    {"fsl,mpc8536-guts", (uintptr_t)&mpc8536_constraints},
    {"fsl,p1022-guts", (uintptr_t)&p1022_constraints},
    {NULL, 0}
};

static struct ofw_compat_data *
mpc85xx_jog_devcompat()
{
	phandle_t node;
	int i;

	node = OF_finddevice("/soc");
	if (node == -1)
		return (NULL);

	for (i = 0; jog_compat[i].ocd_str != NULL; i++)
		if (ofw_bus_find_compatible(node, jog_compat[i].ocd_str) > 0)
			break;

	if (jog_compat[i].ocd_str == NULL)
		return (NULL);

	return (&jog_compat[i]);
}

static void
mpc85xx_jog_identify(driver_t *driver, device_t parent)
{
	struct ofw_compat_data *compat;

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "mpc85xx_jog", -1) != NULL)
		return;

	compat = mpc85xx_jog_devcompat();
	if (compat == NULL)
		return;
	
	/*
	 * We attach a child for every CPU since settings need to
	 * be performed on every CPU in the SMP case.
	 */
	if (BUS_ADD_CHILD(parent, 10, "jog", -1) == NULL)
		device_printf(parent, "add jog child failed\n");
}

static int
mpc85xx_jog_probe(device_t dev)
{
	struct ofw_compat_data *compat;

	compat = mpc85xx_jog_devcompat();
	if (compat == NULL || compat->ocd_str == NULL)
		return (ENXIO);

	device_set_desc(dev, "Freescale CPU Jogger");
	return (0);
}

static int
mpc85xx_jog_attach(device_t dev)
{
	struct ofw_compat_data *compat;
	struct mpc85xx_jog_softc *sc;
	struct mpc85xx_constraints *constraints;
	phandle_t cpu;
	uint32_t reg;

	sc = device_get_softc(dev);
	sc->dev = dev;

	compat = mpc85xx_jog_devcompat();
	constraints = (struct mpc85xx_constraints *)compat->ocd_data;
	cpu = ofw_bus_get_node(device_get_parent(dev));

	if (cpu <= 0) {
		device_printf(dev,"No CPU device tree node!\n");
		return (ENXIO);
	}

	OF_getencprop(cpu, "reg", &sc->cpu, sizeof(sc->cpu));

	reg = ccsr_read4(GUTS_PORPLLSR);
	
	/*
	 * Assume power-on PLL is the highest PLL config supported on the
	 * board.
	 */
	sc->high = PMJCR_GET_CORE_MULT(reg, sc->cpu);
	sc->min_freq = constraints->threshold;
	sc->low = constraints->min_mult;

	cpufreq_register(dev);
	return (0);
}

static int
mpc85xx_jog_settings(device_t dev, struct cf_setting *sets, int *count)
{
	struct mpc85xx_jog_softc *sc;
	uint32_t sysclk;
	int i;

	sc = device_get_softc(dev);
	if (sets == NULL || count == NULL)
		return (EINVAL);
	if (*count < sc->high - 1)
		return (E2BIG);

	sysclk = mpc85xx_get_system_clock();
	/* Return a list of valid settings for this driver. */
	memset(sets, CPUFREQ_VAL_UNKNOWN, sizeof(*sets) * sc->high);

	for (i = sc->high; i >= sc->low; --i) {
		sets[sc->high - i].freq = sysclk * i / MHZ;
		sets[sc->high - i].dev = dev;
		sets[sc->high - i].spec[0] = i;
	}
	*count = sc->high - sc->low + 1;

	return (0);
}

struct jog_rv_args {
	int cpu;
	int mult;
	int slow;
	volatile int inprogress;
};

static void
mpc85xx_jog_set_int(void *arg)
{
	struct jog_rv_args *args = arg;
	uint32_t reg;

	if (PCPU_GET(cpuid) == args->cpu) {
		reg = ccsr_read4(GUTS_PMJCR);
		reg &= ~PMJCR_CORE_MULT(PMJCR_RATIO_M, args->cpu);
		reg |= PMJCR_CORE_MULT(args->mult, args->cpu);
		if (args->slow)
			reg &= ~(1 << (12 + args->cpu));
		else
			reg |= (1 << (12 + args->cpu));

		ccsr_write4(GUTS_PMJCR, reg);

		reg = ccsr_read4(GUTS_POWMGTCSR);
		reg |= POWMGTCSR_JOG | POWMGTCSR_INT_MASK;
		ccsr_write4(GUTS_POWMGTCSR, reg);

		/* Wait for completion */
		do {
			DELAY(100);
			reg = ccsr_read4(GUTS_POWMGTCSR);
		} while (reg & POWMGTCSR_JOG);

		reg = ccsr_read4(GUTS_POWMGTCSR);
		ccsr_write4(GUTS_POWMGTCSR, reg & ~POWMGTCSR_INT_MASK);
		ccsr_read4(GUTS_POWMGTCSR);

		args->inprogress = 0;
	} else {
		while (args->inprogress)
			cpu_spinwait();
	}
}

static int
mpc85xx_jog_set(device_t dev, const struct cf_setting *set)
{
	struct mpc85xx_jog_softc *sc;
	struct jog_rv_args args;
	
	if (set == NULL)
		return (EINVAL);

	sc = device_get_softc(dev);

	args.slow = (set->freq <= sc->min_freq);
	args.mult = set->spec[0];
	args.cpu = PCPU_GET(cpuid);
	args.inprogress = 1;
	smp_rendezvous(smp_no_rendezvous_barrier, mpc85xx_jog_set_int,
	    smp_no_rendezvous_barrier, &args);

	return (0);
}

static int
mpc85xx_jog_get(device_t dev, struct cf_setting *set)
{
	struct mpc85xx_jog_softc *sc;
	uint32_t pmjcr;
	uint32_t freq;

	if (set == NULL)
		return (EINVAL);

	sc = device_get_softc(dev);
	memset(set, CPUFREQ_VAL_UNKNOWN, sizeof(*set));

	pmjcr = ccsr_read4(GUTS_PORPLLSR);
	freq = PMJCR_GET_CORE_MULT(pmjcr, sc->cpu);
	freq *= mpc85xx_get_system_clock();
	freq /= MHZ;
	
	set->freq = freq;
	set->dev = dev;

	return (0);
}

static int
mpc85xx_jog_type(device_t dev, int *type)
{

	if (type == NULL)
		return (EINVAL);

	*type = CPUFREQ_TYPE_ABSOLUTE;
	return (0);
}

