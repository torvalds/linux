/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 Nate Lawson (SDG)
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/pcpu.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <dev/pci/pcivar.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include "cpufreq_if.h"

/*
 * The SpeedStep ICH feature is a chipset-initiated voltage and frequency
 * transition available on the ICH2M, 3M, and 4M.  It is different from
 * the newer Pentium-M SpeedStep feature.  It offers only two levels of
 * frequency/voltage.  Often, the BIOS will select one of the levels via
 * SMM code during the power-on process (i.e., choose a lower level if the
 * system is off AC power.)
 */

struct ichss_softc {
	device_t	 dev;
	int		 bm_rid;	/* Bus-mastering control (PM2REG). */
	struct resource	*bm_reg;
	int		 ctrl_rid;	/* Control/status register. */
	struct resource	*ctrl_reg;
	struct cf_setting sets[2];	/* Only two settings. */
};

/* Supported PCI IDs. */
#define PCI_VENDOR_INTEL	0x8086
#define PCI_DEV_82801BA		0x244c /* ICH2M */
#define PCI_DEV_82801CA		0x248c /* ICH3M */
#define PCI_DEV_82801DB		0x24cc /* ICH4M */
#define PCI_DEV_82815_MC	0x1130 /* Unsupported/buggy part */

/* PCI config registers for finding PMBASE and enabling SpeedStep. */
#define ICHSS_PMBASE_OFFSET	0x40
#define ICHSS_PMCFG_OFFSET	0xa0

/* Values and masks. */
#define ICHSS_ENABLE		(1<<3)	/* Enable SpeedStep control. */
#define ICHSS_IO_REG		0x1	/* Access register via I/O space. */
#define ICHSS_PMBASE_MASK	0xff80	/* PMBASE address bits. */
#define ICHSS_CTRL_BIT		0x1	/* 0 is high speed, 1 is low. */
#define ICHSS_BM_DISABLE	0x1

/* Offsets from PMBASE for various registers. */
#define ICHSS_BM_OFFSET		0x20
#define ICHSS_CTRL_OFFSET	0x50

#define ICH_GET_REG(reg) 				\
	(bus_space_read_1(rman_get_bustag((reg)), 	\
	    rman_get_bushandle((reg)), 0))
#define ICH_SET_REG(reg, val)				\
	(bus_space_write_1(rman_get_bustag((reg)), 	\
	    rman_get_bushandle((reg)), 0, (val)))

static void	ichss_identify(driver_t *driver, device_t parent);
static int	ichss_probe(device_t dev);
static int	ichss_attach(device_t dev);
static int	ichss_detach(device_t dev);
static int	ichss_settings(device_t dev, struct cf_setting *sets,
		    int *count);
static int	ichss_set(device_t dev, const struct cf_setting *set);
static int	ichss_get(device_t dev, struct cf_setting *set);
static int	ichss_type(device_t dev, int *type);

static device_method_t ichss_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	ichss_identify),
	DEVMETHOD(device_probe,		ichss_probe),
	DEVMETHOD(device_attach,	ichss_attach),
	DEVMETHOD(device_detach,	ichss_detach),

	/* cpufreq interface */
	DEVMETHOD(cpufreq_drv_set,	ichss_set),
	DEVMETHOD(cpufreq_drv_get,	ichss_get),
	DEVMETHOD(cpufreq_drv_type,	ichss_type),
	DEVMETHOD(cpufreq_drv_settings,	ichss_settings),
	DEVMETHOD_END
};
static driver_t ichss_driver = {
	"ichss", ichss_methods, sizeof(struct ichss_softc)
};
static devclass_t ichss_devclass;
DRIVER_MODULE(ichss, cpu, ichss_driver, ichss_devclass, 0, 0);

static device_t ich_device;

#if 0
#define DPRINT(x...)	printf(x)
#else
#define DPRINT(x...)
#endif

static void
ichss_identify(driver_t *driver, device_t parent)
{
	device_t child;
	uint32_t pmbase;

	if (resource_disabled("ichss", 0))
		return;

	/*
	 * It appears that ICH SpeedStep only requires a single CPU to
	 * set the value (since the chipset is shared by all CPUs.)
	 * Thus, we only add a child to cpu 0.
	 */
	if (device_get_unit(parent) != 0)
		return;

	/* Avoid duplicates. */
	if (device_find_child(parent, "ichss", -1))
		return;

	/*
	 * ICH2/3/4-M I/O Controller Hub is at bus 0, slot 1F, function 0.
	 * E.g. see Section 6.1 "PCI Devices and Functions" and table 6.1 of
	 * Intel(r) 82801BA I/O Controller Hub 2 (ICH2) and Intel(r) 82801BAM
	 * I/O Controller Hub 2 Mobile (ICH2-M).
	 */
	ich_device = pci_find_bsf(0, 0x1f, 0);
	if (ich_device == NULL ||
	    pci_get_vendor(ich_device) != PCI_VENDOR_INTEL ||
	    (pci_get_device(ich_device) != PCI_DEV_82801BA &&
	    pci_get_device(ich_device) != PCI_DEV_82801CA &&
	    pci_get_device(ich_device) != PCI_DEV_82801DB))
		return;

	/*
	 * Certain systems with ICH2 and an Intel 82815_MC host bridge
	 * where the host bridge's revision is < 5 lockup if SpeedStep
	 * is used.
	 */
	if (pci_get_device(ich_device) == PCI_DEV_82801BA) {
		device_t hostb;

		hostb = pci_find_bsf(0, 0, 0);
		if (hostb != NULL &&
		    pci_get_vendor(hostb) == PCI_VENDOR_INTEL &&
		    pci_get_device(hostb) == PCI_DEV_82815_MC &&
		    pci_get_revid(hostb) < 5)
			return;
	}

	/* Find the PMBASE register from our PCI config header. */
	pmbase = pci_read_config(ich_device, ICHSS_PMBASE_OFFSET,
	    sizeof(pmbase));
	if ((pmbase & ICHSS_IO_REG) == 0) {
		printf("ichss: invalid PMBASE memory type\n");
		return;
	}
	pmbase &= ICHSS_PMBASE_MASK;
	if (pmbase == 0) {
		printf("ichss: invalid zero PMBASE address\n");
		return;
	}
	DPRINT("ichss: PMBASE is %#x\n", pmbase);

	child = BUS_ADD_CHILD(parent, 20, "ichss", 0);
	if (child == NULL) {
		device_printf(parent, "add SpeedStep child failed\n");
		return;
	}

	/* Add the bus master arbitration and control registers. */
	bus_set_resource(child, SYS_RES_IOPORT, 0, pmbase + ICHSS_BM_OFFSET,
	    1);
	bus_set_resource(child, SYS_RES_IOPORT, 1, pmbase + ICHSS_CTRL_OFFSET,
	    1);
}

static int
ichss_probe(device_t dev)
{
	device_t est_dev, perf_dev;
	int error, type;

	/*
	 * If the ACPI perf driver has attached and is not just offering
	 * info, let it manage things.  Also, if Enhanced SpeedStep is
	 * available, don't attach.
	 */
	perf_dev = device_find_child(device_get_parent(dev), "acpi_perf", -1);
	if (perf_dev && device_is_attached(perf_dev)) {
		error = CPUFREQ_DRV_TYPE(perf_dev, &type);
		if (error == 0 && (type & CPUFREQ_FLAG_INFO_ONLY) == 0)
			return (ENXIO);
	}
	est_dev = device_find_child(device_get_parent(dev), "est", -1);
	if (est_dev && device_is_attached(est_dev))
		return (ENXIO);

	device_set_desc(dev, "SpeedStep ICH");
	return (-1000);
}

static int
ichss_attach(device_t dev)
{
	struct ichss_softc *sc;
	uint16_t ss_en;

	sc = device_get_softc(dev);
	sc->dev = dev;

	sc->bm_rid = 0;
	sc->bm_reg = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &sc->bm_rid,
	    RF_ACTIVE);
	if (sc->bm_reg == NULL) {
		device_printf(dev, "failed to alloc BM arb register\n");
		return (ENXIO);
	}
	sc->ctrl_rid = 1;
	sc->ctrl_reg = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
	    &sc->ctrl_rid, RF_ACTIVE);
	if (sc->ctrl_reg == NULL) {
		device_printf(dev, "failed to alloc control register\n");
		bus_release_resource(dev, SYS_RES_IOPORT, sc->bm_rid,
		    sc->bm_reg);
		return (ENXIO);
	}

	/* Activate SpeedStep control if not already enabled. */
	ss_en = pci_read_config(ich_device, ICHSS_PMCFG_OFFSET, sizeof(ss_en));
	if ((ss_en & ICHSS_ENABLE) == 0) {
		device_printf(dev, "enabling SpeedStep support\n");
		pci_write_config(ich_device, ICHSS_PMCFG_OFFSET,
		    ss_en | ICHSS_ENABLE, sizeof(ss_en));
	}

	/* Setup some defaults for our exported settings. */
	sc->sets[0].freq = CPUFREQ_VAL_UNKNOWN;
	sc->sets[0].volts = CPUFREQ_VAL_UNKNOWN;
	sc->sets[0].power = CPUFREQ_VAL_UNKNOWN;
	sc->sets[0].lat = 1000;
	sc->sets[0].dev = dev;
	sc->sets[1] = sc->sets[0];
	cpufreq_register(dev);

	return (0);
}

static int
ichss_detach(device_t dev)
{
	/* TODO: teardown BM and CTRL registers. */
	return (ENXIO);
}

static int
ichss_settings(device_t dev, struct cf_setting *sets, int *count)
{
	struct ichss_softc *sc;
	struct cf_setting set;
	int first, i;

	if (sets == NULL || count == NULL)
		return (EINVAL);
	if (*count < 2) {
		*count = 2;
		return (E2BIG);
	}
	sc = device_get_softc(dev);

	/*
	 * Estimate frequencies for both levels, temporarily switching to
	 * the other one if we haven't calibrated it yet.
	 */
	ichss_get(dev, &set);
	for (i = 0; i < 2; i++) {
		if (sc->sets[i].freq == CPUFREQ_VAL_UNKNOWN) {
			first = (i == 0) ? 1 : 0;
			ichss_set(dev, &sc->sets[i]);
			ichss_set(dev, &sc->sets[first]);
		}
	}

	bcopy(sc->sets, sets, sizeof(sc->sets));
	*count = 2;

	return (0);
}

static int
ichss_set(device_t dev, const struct cf_setting *set)
{
	struct ichss_softc *sc;
	uint8_t bmval, new_val, old_val, req_val;
	uint64_t rate;
	register_t regs;

	/* Look up appropriate bit value based on frequency. */
	sc = device_get_softc(dev);
	if (CPUFREQ_CMP(set->freq, sc->sets[0].freq))
		req_val = 0;
	else if (CPUFREQ_CMP(set->freq, sc->sets[1].freq))
		req_val = ICHSS_CTRL_BIT;
	else
		return (EINVAL);
	DPRINT("ichss: requested setting %d\n", req_val);

	/* Disable interrupts and get the other register contents. */
	regs = intr_disable();
	old_val = ICH_GET_REG(sc->ctrl_reg) & ~ICHSS_CTRL_BIT;

	/*
	 * Disable bus master arbitration, write the new value to the control
	 * register, and then re-enable bus master arbitration.
	 */
	bmval = ICH_GET_REG(sc->bm_reg) | ICHSS_BM_DISABLE;
	ICH_SET_REG(sc->bm_reg, bmval);
	ICH_SET_REG(sc->ctrl_reg, old_val | req_val);
	ICH_SET_REG(sc->bm_reg, bmval & ~ICHSS_BM_DISABLE);

	/* Get the new value and re-enable interrupts. */
	new_val = ICH_GET_REG(sc->ctrl_reg);
	intr_restore(regs);

	/* Check if the desired state was indeed selected. */
	if (req_val != (new_val & ICHSS_CTRL_BIT)) {
	    device_printf(sc->dev, "transition to %d failed\n", req_val);
	    return (ENXIO);
	}

	/* Re-initialize our cycle counter if we don't know this new state. */
	if (sc->sets[req_val].freq == CPUFREQ_VAL_UNKNOWN) {
		cpu_est_clockrate(0, &rate);
		sc->sets[req_val].freq = rate / 1000000;
		DPRINT("ichss: set calibrated new rate of %d\n",
		    sc->sets[req_val].freq);
	}

	return (0);
}

static int
ichss_get(device_t dev, struct cf_setting *set)
{
	struct ichss_softc *sc;
	uint64_t rate;
	uint8_t state;

	sc = device_get_softc(dev);
	state = ICH_GET_REG(sc->ctrl_reg) & ICHSS_CTRL_BIT;

	/* If we haven't changed settings yet, estimate the current value. */
	if (sc->sets[state].freq == CPUFREQ_VAL_UNKNOWN) {
		cpu_est_clockrate(0, &rate);
		sc->sets[state].freq = rate / 1000000;
		DPRINT("ichss: get calibrated new rate of %d\n",
		    sc->sets[state].freq);
	}
	*set = sc->sets[state];

	return (0);
}

static int
ichss_type(device_t dev, int *type)
{

	if (type == NULL)
		return (EINVAL);

	*type = CPUFREQ_TYPE_ABSOLUTE;
	return (0);
}
