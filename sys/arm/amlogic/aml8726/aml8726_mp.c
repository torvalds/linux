/*-
 * Copyright 2015 John Wehle <john@feith.com>
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

/*
 * Amlogic aml8726 multiprocessor support.
 *
 * Some processors require powering on which involves poking registers
 * on the aobus and cbus ... it's expected that these locations are set
 * in stone.
 *
 * Locking is not used as these routines should only be called by the BP
 * during startup and should complete prior to the APs being released (the
 * issue being to ensure that a register such as AML_SOC_CPU_CLK_CNTL_REG
 * is not concurrently modified).
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/cpu.h>
#include <machine/bus.h>
#include <machine/smp.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/amlogic/aml8726/aml8726_soc.h>

static const char *scu_compatible[] = {
	"arm,cortex-a5-scu",
	"arm,cortex-a9-scu",
	NULL
};

static const char *scu_errata_764369[] = {
	"arm,cortex-a9-scu",
	NULL
};

static const char *cpucfg_compatible[] = {
	"amlogic,aml8726-cpuconfig",
	NULL
};

static struct {
	boolean_t errata_764369;
	u_long scu_size;
	struct resource scu_res;
	u_long cpucfg_size;
	struct resource cpucfg_res;
	struct resource aobus_res;
	struct resource cbus_res;
} aml8726_smp;

#define	AML_SCU_CONTROL_REG			0
#define	AML_SCU_CONTROL_ENABLE			1
#define	AML_SCU_CONFIG_REG			4
#define	AML_SCU_CONFIG_NCPU_MASK		0x3
#define	AML_SCU_CPU_PWR_STATUS_REG		8
#define	AML_SCU_CPU_PWR_STATUS_CPU3_MASK	(3 << 24)
#define	AML_SCU_CPU_PWR_STATUS_CPU2_MASK	(3 << 16)
#define	AML_SCU_CPU_PWR_STATUS_CPU1_MASK	(3 << 8)
#define	AML_SCU_CPU_PWR_STATUS_CPU0_MASK	3
#define	AML_SCU_INV_TAGS_REG			12
#define	AML_SCU_DIAG_CONTROL_REG		48
#define	AML_SCU_DIAG_CONTROL_DISABLE_MIGBIT	1

#define	AML_CPUCONF_CONTROL_REG			0
#define	AML_CPUCONF_CPU1_ADDR_REG		4
#define	AML_CPUCONF_CPU2_ADDR_REG		8
#define	AML_CPUCONF_CPU3_ADDR_REG		12

/* aobus */

#define	AML_M8_CPU_PWR_CNTL0_REG		0xe0
#define	AML_M8B_CPU_PWR_CNTL0_MODE_CPU3_MASK	(3 << 22)
#define	AML_M8B_CPU_PWR_CNTL0_MODE_CPU2_MASK	(3 << 20)
#define	AML_M8B_CPU_PWR_CNTL0_MODE_CPU1_MASK	(3 << 18)

#define	AML_M8_CPU_PWR_CNTL0_ISO_CPU3		(1 << 3)
#define	AML_M8_CPU_PWR_CNTL0_ISO_CPU2		(1 << 2)
#define	AML_M8_CPU_PWR_CNTL0_ISO_CPU1		(1 << 1)

#define	AML_M8_CPU_PWR_CNTL1_REG		0xe4
#define	AML_M8B_CPU_PWR_CNTL1_PWR_CPU3		(1 << 19)
#define	AML_M8B_CPU_PWR_CNTL1_PWR_CPU2		(1 << 18)
#define	AML_M8B_CPU_PWR_CNTL1_PWR_CPU1		(1 << 17)

#define	AML_M8_CPU_PWR_CNTL1_MODE_CPU3_MASK	(3 << 8)
#define	AML_M8_CPU_PWR_CNTL1_MODE_CPU2_MASK	(3 << 6)
#define	AML_M8_CPU_PWR_CNTL1_MODE_CPU1_MASK	(3 << 4)

#define	AML_M8B_CPU_PWR_MEM_PD0_REG		0xf4
#define	AML_M8B_CPU_PWR_MEM_PD0_CPU3		(0xf << 20)
#define	AML_M8B_CPU_PWR_MEM_PD0_CPU2		(0xf << 24)
#define	AML_M8B_CPU_PWR_MEM_PD0_CPU1		(0xf << 28)

/* cbus */

#define	AML_SOC_CPU_CLK_CNTL_REG		0x419c
#define	AML_M8_CPU_CLK_CNTL_RESET_CPU3		(1 << 27)
#define	AML_M8_CPU_CLK_CNTL_RESET_CPU2		(1 << 26)
#define	AML_M8_CPU_CLK_CNTL_RESET_CPU1		(1 << 25)

#define	SCU_WRITE_4(reg, value)		bus_write_4(&aml8726_smp.scu_res,    \
    (reg), (value))
#define	SCU_READ_4(reg)			bus_read_4(&aml8726_smp.scu_res, (reg))
#define	SCU_BARRIER(reg)		bus_barrier(&aml8726_smp.scu_res,    \
    (reg), 4, (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE))

#define	CPUCONF_WRITE_4(reg, value)	bus_write_4(&aml8726_smp.cpucfg_res, \
    (reg), (value))
#define	CPUCONF_READ_4(reg)		bus_read_4(&aml8726_smp.cpucfg_res,  \
    (reg))
#define	CPUCONF_BARRIER(reg)		bus_barrier(&aml8726_smp.cpucfg_res, \
    (reg), 4, (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE))

#define	AOBUS_WRITE_4(reg, value)	bus_write_4(&aml8726_smp.aobus_res,  \
    (reg), (value))
#define	AOBUS_READ_4(reg)		bus_read_4(&aml8726_smp.aobus_res,   \
    (reg))
#define	AOBUS_BARRIER(reg)		bus_barrier(&aml8726_smp.aobus_res,  \
    (reg), 4, (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE))

#define	CBUS_WRITE_4(reg, value)	bus_write_4(&aml8726_smp.cbus_res,   \
    (reg), (value))
#define CBUS_READ_4(reg)		bus_read_4(&aml8726_smp.cbus_res,    \
    (reg))
#define	CBUS_BARRIER(reg)		bus_barrier(&aml8726_smp.cbus_res,   \
    (reg), 4, (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE))

static phandle_t
find_node_for_device(const char *device, const char **compatible)
{
	int i;
	phandle_t node;

	/*
	 * Try to access the node directly i.e. through /aliases/.
	 */

	if ((node = OF_finddevice(device)) != -1)
		for (i = 0; compatible[i]; i++)
			if (fdt_is_compatible_strict(node, compatible[i]))
				return node;

	/*
	 * Find the node the long way.
	 */

	for (i = 0; compatible[i]; i++) {
		if ((node = OF_finddevice("/soc")) == -1)
			return (0);

		if ((node = fdt_find_compatible(node, compatible[i], 1)) != 0)
			return node;
	}

	return (0);
}


static int
alloc_resource_for_node(phandle_t node, struct resource *res, u_long *size)
{
	int err;
	u_long pbase, psize;
	u_long start;

	if ((err = fdt_get_range(OF_parent(node), 0, &pbase, &psize)) != 0 ||
	    (err = fdt_regsize(node, &start, size)) != 0)
		return (err);

	start += pbase;

	memset(res, 0, sizeof(*res));

	res->r_bustag = fdtbus_bs_tag;

	err = bus_space_map(res->r_bustag, start, *size, 0, &res->r_bushandle);

	return (err);
}


static void
power_on_cpu(int cpu)
{
	uint32_t scpsr;
	uint32_t value;

	if (cpu <= 0)
		return;

	/*
	 * Power on the CPU if the intricate details are known, otherwise
	 * just hope for the best (it may have already be powered on by
	 * the hardware / firmware).
	 */

	switch (aml8726_soc_hw_rev) {
	case AML_SOC_HW_REV_M8:
	case AML_SOC_HW_REV_M8B:
		/*
		 * Set the SCU power status for the CPU to normal mode.
		 */
		scpsr = SCU_READ_4(AML_SCU_CPU_PWR_STATUS_REG);
		scpsr &= ~(AML_SCU_CPU_PWR_STATUS_CPU1_MASK << ((cpu - 1) * 8));
		SCU_WRITE_4(AML_SCU_CPU_PWR_STATUS_REG, scpsr);
		SCU_BARRIER(AML_SCU_CPU_PWR_STATUS_REG);

		if (aml8726_soc_hw_rev == AML_SOC_HW_REV_M8B) {
			/*
			 * Reset may cause the current power status from the
			 * actual CPU to be written to the SCU (over-writing
			 * the value  we've just written) so set it to normal
			 * mode as well.
			 */
			 value = AOBUS_READ_4(AML_M8_CPU_PWR_CNTL0_REG);
			 value &= ~(AML_M8B_CPU_PWR_CNTL0_MODE_CPU1_MASK <<
			    ((cpu - 1) * 2));
			 AOBUS_WRITE_4(AML_M8_CPU_PWR_CNTL0_REG, value);
			 AOBUS_BARRIER(AML_M8_CPU_PWR_CNTL0_REG);
		 }

		DELAY(5);

		/*
		 * Assert reset.
		 */
		value = CBUS_READ_4(AML_SOC_CPU_CLK_CNTL_REG);
		value |= AML_M8_CPU_CLK_CNTL_RESET_CPU1 << (cpu - 1); 
		CBUS_WRITE_4(AML_SOC_CPU_CLK_CNTL_REG, value);
		CBUS_BARRIER(AML_SOC_CPU_CLK_CNTL_REG);

		if (aml8726_soc_hw_rev == AML_SOC_HW_REV_M8B) {
			/*
			 * Release RAM pull-down.
			 */
			 value = AOBUS_READ_4(AML_M8B_CPU_PWR_MEM_PD0_REG);
			 value &= ~((uint32_t)AML_M8B_CPU_PWR_MEM_PD0_CPU1 >>
			    ((cpu - 1) * 4));
			 AOBUS_WRITE_4(AML_M8B_CPU_PWR_MEM_PD0_REG, value);
			 AOBUS_BARRIER(AML_M8B_CPU_PWR_MEM_PD0_REG);
		 }

		/*
		 * Power on CPU.
		 */
		value = AOBUS_READ_4(AML_M8_CPU_PWR_CNTL1_REG);
		value &= ~(AML_M8_CPU_PWR_CNTL1_MODE_CPU1_MASK <<
		    ((cpu - 1) * 2));
		AOBUS_WRITE_4(AML_M8_CPU_PWR_CNTL1_REG, value);
		AOBUS_BARRIER(AML_M8_CPU_PWR_CNTL1_REG);

		DELAY(10);

		if (aml8726_soc_hw_rev == AML_SOC_HW_REV_M8B) {
			/*
			 * Wait for power on confirmation.
			 */
			for ( ; ; ) {
				value = AOBUS_READ_4(AML_M8_CPU_PWR_CNTL1_REG);
				value &= AML_M8B_CPU_PWR_CNTL1_PWR_CPU1 <<
				    (cpu - 1);
				if (value)
					break;
				DELAY(10);
			}
		}

		/*
		 * Release peripheral clamp.
		 */
		value = AOBUS_READ_4(AML_M8_CPU_PWR_CNTL0_REG);
		value &= ~(AML_M8_CPU_PWR_CNTL0_ISO_CPU1 << (cpu - 1));
		AOBUS_WRITE_4(AML_M8_CPU_PWR_CNTL0_REG, value);
		AOBUS_BARRIER(AML_M8_CPU_PWR_CNTL0_REG);

		/*
		 * Release reset.
		 */
		value = CBUS_READ_4(AML_SOC_CPU_CLK_CNTL_REG);
		value &= ~(AML_M8_CPU_CLK_CNTL_RESET_CPU1 << (cpu - 1));
		CBUS_WRITE_4(AML_SOC_CPU_CLK_CNTL_REG, value);
		CBUS_BARRIER(AML_SOC_CPU_CLK_CNTL_REG);

		if (aml8726_soc_hw_rev == AML_SOC_HW_REV_M8B) {
			/*
			 * The Amlogic Linux platform code sets the SCU power
			 * status for the CPU again for some reason so we
			 * follow suit (perhaps in case the reset caused
			 * a stale power status from the actual CPU to be
			 * written to the SCU).
			 */
			SCU_WRITE_4(AML_SCU_CPU_PWR_STATUS_REG, scpsr);
			SCU_BARRIER(AML_SCU_CPU_PWR_STATUS_REG);
		}
		break;
	default:
		break;
	}
}

void
platform_mp_setmaxid(void)
{
	int err;
	int i;
	int ncpu;
	phandle_t cpucfg_node;
	phandle_t scu_node;
	uint32_t value;

	if (mp_ncpus != 0)
		return;

	ncpu = 1;

	/*
	 * Is the hardware necessary for SMP present?
	 */

	if ((scu_node = find_node_for_device("scu", scu_compatible)) == 0)
		goto moveon;

	if ((cpucfg_node = find_node_for_device("cpuconfig",
	    cpucfg_compatible)) == 0)
		goto moveon;

	if (alloc_resource_for_node(scu_node, &aml8726_smp.scu_res,
	    &aml8726_smp.scu_size) != 0)
		panic("Could not allocate resource for SCU");

	if (alloc_resource_for_node(cpucfg_node, &aml8726_smp.cpucfg_res,
	    &aml8726_smp.cpucfg_size) != 0)
		panic("Could not allocate resource for CPUCONFIG");


	/*
	 * Strictly speaking the aobus and cbus may not be required in
	 * order to start an AP (it depends on the processor), however
	 * always mapping them in simplifies the code.
	 */

	aml8726_smp.aobus_res.r_bustag = fdtbus_bs_tag;

	err = bus_space_map(aml8726_smp.aobus_res.r_bustag,
	    AML_SOC_AOBUS_BASE_ADDR, 0x100000,
	    0, &aml8726_smp.aobus_res.r_bushandle);

	if (err)
		panic("Could not allocate resource for AOBUS");

	aml8726_smp.cbus_res.r_bustag = fdtbus_bs_tag;

	err = bus_space_map(aml8726_smp.cbus_res.r_bustag,
	    AML_SOC_CBUS_BASE_ADDR, 0x100000,
	    0, &aml8726_smp.cbus_res.r_bushandle);

	if (err)
		panic("Could not allocate resource for CBUS");

	aml8726_smp.errata_764369 = false;
	for (i = 0; scu_errata_764369[i]; i++)
		if (fdt_is_compatible_strict(scu_node, scu_errata_764369[i])) {
			aml8726_smp.errata_764369 = true;
			break;
		}

	/*
	 * Read the number of CPUs present.
	 */
	value = SCU_READ_4(AML_SCU_CONFIG_REG);
	ncpu = (value & AML_SCU_CONFIG_NCPU_MASK) + 1;

moveon:
	mp_ncpus = ncpu;
	mp_maxid = ncpu - 1;
}

void
platform_mp_start_ap(void)
{
	int i;
	uint32_t reg;
	uint32_t value;
	vm_paddr_t paddr;

	if (mp_ncpus < 2)
		return;

	/*
	 * Invalidate SCU cache tags.  The 0x0000ffff constant invalidates
	 * all ways on all cores 0-3.  Per the ARM docs, it's harmless to
	 * write to the bits for cores that are not present.
	 */
	SCU_WRITE_4(AML_SCU_INV_TAGS_REG, 0x0000ffff);

	if (aml8726_smp.errata_764369) {
		/*
		 * Erratum ARM/MP: 764369 (problems with cache maintenance).
		 * Setting the "disable-migratory bit" in the undocumented SCU
		 * Diagnostic Control Register helps work around the problem.
		 */
		value = SCU_READ_4(AML_SCU_DIAG_CONTROL_REG);
		value |= AML_SCU_DIAG_CONTROL_DISABLE_MIGBIT;
		SCU_WRITE_4(AML_SCU_DIAG_CONTROL_REG, value);
	}

	/*
	 * Enable the SCU, then clean the cache on this core.  After these
	 * two operations the cache tag ram in the SCU is coherent with
	 * the contents of the cache on this core.  The other cores aren't
	 * running yet so their caches can't contain valid data yet, however
	 * we've initialized their SCU tag ram above, so they will be
	 * coherent from startup.
	 */
	value = SCU_READ_4(AML_SCU_CONTROL_REG);
	value |= AML_SCU_CONTROL_ENABLE;
	SCU_WRITE_4(AML_SCU_CONTROL_REG, value);
	SCU_BARRIER(AML_SCU_CONTROL_REG);
	dcache_wbinv_poc_all();

	/* Set the boot address and power on each AP. */
	paddr = pmap_kextract((vm_offset_t)mpentry);
	for (i = 1; i < mp_ncpus; i++) {
		reg = AML_CPUCONF_CPU1_ADDR_REG + ((i - 1) * 4);
		CPUCONF_WRITE_4(reg, paddr);
		CPUCONF_BARRIER(reg);

		power_on_cpu(i);
	}

	/*
	 * Enable the APs.
	 *
	 * The Amlogic Linux platform code sets the lsb for some reason
	 * in addition to the enable bit for each AP so we follow suit
	 * (the lsb may be the enable bit for the BP, though in that case
	 * it should already be set since it's currently running).
	 */
	value = CPUCONF_READ_4(AML_CPUCONF_CONTROL_REG);
	value |= 1;
	for (i = 1; i < mp_ncpus; i++)
		value |= (1 << i);
	CPUCONF_WRITE_4(AML_CPUCONF_CONTROL_REG, value);
	CPUCONF_BARRIER(AML_CPUCONF_CONTROL_REG);

	/* Wakeup the now enabled APs */
	dsb();
	sev();

	/*
	 * Free the resources which are not needed after startup.
	 */
	bus_space_unmap(aml8726_smp.scu_res.r_bustag,
	    aml8726_smp.scu_res.r_bushandle,
	    aml8726_smp.scu_size);
	bus_space_unmap(aml8726_smp.cpucfg_res.r_bustag,
	    aml8726_smp.cpucfg_res.r_bushandle,
	    aml8726_smp.cpucfg_size);
	bus_space_unmap(aml8726_smp.aobus_res.r_bustag,
	    aml8726_smp.aobus_res.r_bushandle,
	    0x100000);
	bus_space_unmap(aml8726_smp.cbus_res.r_bustag,
	    aml8726_smp.cbus_res.r_bushandle,
	    0x100000);
	memset(&aml8726_smp, 0, sizeof(aml8726_smp));
}

/*
 * Stub drivers for cosmetic purposes.
 */
struct aml8726_scu_softc {
	device_t	dev;
};

static int
aml8726_scu_probe(device_t dev)
{
	int i;

	for (i = 0; scu_compatible[i]; i++)
		if (ofw_bus_is_compatible(dev, scu_compatible[i]))
			break;

	if (!scu_compatible[i])
		return (ENXIO);

	device_set_desc(dev, "ARM Snoop Control Unit");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_scu_attach(device_t dev)
{
	struct aml8726_scu_softc *sc = device_get_softc(dev);

	sc->dev = dev;

	return (0);
}

static int
aml8726_scu_detach(device_t dev)
{

	return (0);
}

static device_method_t aml8726_scu_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_scu_probe),
	DEVMETHOD(device_attach,	aml8726_scu_attach),
	DEVMETHOD(device_detach,	aml8726_scu_detach),

	DEVMETHOD_END
};

static driver_t aml8726_scu_driver = {
	"scu",
	aml8726_scu_methods,
	sizeof(struct aml8726_scu_softc),
};

static devclass_t aml8726_scu_devclass;

EARLY_DRIVER_MODULE(scu, simplebus, aml8726_scu_driver, aml8726_scu_devclass,
    0, 0, BUS_PASS_CPU + BUS_PASS_ORDER_MIDDLE);

struct aml8726_cpucfg_softc {
	device_t	dev;
};

static int
aml8726_cpucfg_probe(device_t dev)
{
	int i;

	for (i = 0; cpucfg_compatible[i]; i++)
		if (ofw_bus_is_compatible(dev, cpucfg_compatible[i]))
			break;

	if (!cpucfg_compatible[i])
		return (ENXIO);

	device_set_desc(dev, "Amlogic CPU Config");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_cpucfg_attach(device_t dev)
{
	struct aml8726_cpucfg_softc *sc = device_get_softc(dev);

	sc->dev = dev;

	return (0);
}

static int
aml8726_cpucfg_detach(device_t dev)
{

	return (0);
}

static device_method_t aml8726_cpucfg_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_cpucfg_probe),
	DEVMETHOD(device_attach,	aml8726_cpucfg_attach),
	DEVMETHOD(device_detach,	aml8726_cpucfg_detach),

	DEVMETHOD_END
};

static driver_t aml8726_cpucfg_driver = {
	"cpuconfig",
	aml8726_cpucfg_methods,
	sizeof(struct aml8726_cpucfg_softc),
};

static devclass_t aml8726_cpucfg_devclass;

EARLY_DRIVER_MODULE(cpuconfig, simplebus, aml8726_cpucfg_driver,
    aml8726_cpucfg_devclass, 0, 0, BUS_PASS_CPU + BUS_PASS_ORDER_MIDDLE);
