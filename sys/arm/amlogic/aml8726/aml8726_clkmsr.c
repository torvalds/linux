/*-
 * Copyright 2014-2015 John Wehle <john@feith.com>
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
 *
 */

/*
 * Amlogic aml8726 clock measurement driver.
 *
 * This allows various clock rates to be determine at runtime.
 * The measurements are done once and are not expected to change
 * (i.e. FDT fixup provides clk81 as bus-frequency to the MMC
 * and UART drivers which use the value when programming the
 * hardware).
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timetc.h>
#include <sys/timeet.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/fdt.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/amlogic/aml8726/aml8726_soc.h>
#include <arm/amlogic/aml8726/aml8726_clkmsr.h>


static struct aml8726_clkmsr_clk {
	const char *		name;
	uint32_t		mux;
} aml8726_clkmsr_clks[] = {
	{ "clk81", 7 },
};

#define	AML_CLKMSR_CLK81	0

#define	AML_CLKMSR_NCLKS	nitems(aml8726_clkmsr_clks)

struct aml8726_clkmsr_softc {
	device_t		dev;
	struct resource	*	res[1];
};

static struct resource_spec aml8726_clkmsr_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

/*
 * Duration can range from 1uS to 65535 uS and should be chosen
 * based on the expected frequency result so to maximize resolution
 * and avoid overflowing the 16 bit result counter.
 */
#define	AML_CLKMSR_DURATION		32

#define	AML_CLKMSR_DUTY_REG		0
#define	AML_CLKMSR_0_REG		4
#define	AML_CLKMSR_0_BUSY		(1U << 31)
#define	AML_CLKMSR_0_MUX_MASK		(0x3f << 20)
#define	AML_CLKMSR_0_MUX_SHIFT		20
#define	AML_CLKMSR_0_MUX_EN		(1 << 19)
#define	AML_CLKMSR_0_MEASURE		(1 << 16)
#define	AML_CLKMSR_0_DURATION_MASK	0xffff
#define	AML_CLKMSR_0_DURATION_SHIFT	0
#define	AML_CLKMSR_1_REG		8
#define	AML_CLKMSR_2_REG		12
#define	AML_CLKMSR_2_RESULT_MASK	0xffff
#define	AML_CLKMSR_2_RESULT_SHIFT	0

#define CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)
#define CSR_BARRIER(sc, reg)		bus_barrier((sc)->res[0], reg, 4, \
    (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE))

static int
aml8726_clkmsr_clock_frequency(struct aml8726_clkmsr_softc *sc, unsigned clock)
{
	uint32_t value;

	if (clock >= AML_CLKMSR_NCLKS)
		return (0);

	/*
	 * Locking is not used as this is only expected to be called from
	 * FDT fixup (which occurs prior to driver initialization) or attach.
	 */

	CSR_WRITE_4(sc, AML_CLKMSR_0_REG, 0);

	CSR_BARRIER(sc, AML_CLKMSR_0_REG);

	value = (aml8726_clkmsr_clks[clock].mux << AML_CLKMSR_0_MUX_SHIFT)
	    | ((AML_CLKMSR_DURATION - 1) << AML_CLKMSR_0_DURATION_SHIFT)
	    | AML_CLKMSR_0_MUX_EN
	    | AML_CLKMSR_0_MEASURE;
	CSR_WRITE_4(sc, AML_CLKMSR_0_REG, value);

	CSR_BARRIER(sc, AML_CLKMSR_0_REG);

	while ((CSR_READ_4(sc, AML_CLKMSR_0_REG) & AML_CLKMSR_0_BUSY) != 0)
		cpu_spinwait();

	value &= ~AML_CLKMSR_0_MEASURE;
	CSR_WRITE_4(sc, AML_CLKMSR_0_REG, value);

	CSR_BARRIER(sc, AML_CLKMSR_0_REG);

	value = (((CSR_READ_4(sc, AML_CLKMSR_2_REG) & AML_CLKMSR_2_RESULT_MASK)
	    >> AML_CLKMSR_2_RESULT_SHIFT) + AML_CLKMSR_DURATION / 2) /
	    AML_CLKMSR_DURATION;

	return value;
}

static void
aml8726_clkmsr_fixup_clk81(struct aml8726_clkmsr_softc *sc, int freq)
{
	pcell_t prop;
	ssize_t len;
	phandle_t clk_node;
	phandle_t node;

	node = ofw_bus_get_node(sc->dev);

	len = OF_getencprop(node, "clocks", &prop, sizeof(prop));
	if ((len / sizeof(prop)) != 1 || prop == 0 ||
	    (clk_node = OF_node_from_xref(prop)) == 0)
		return;

	len = OF_getencprop(clk_node, "clock-frequency", &prop, sizeof(prop));
	if ((len / sizeof(prop)) != 1 || prop != 0)
		return;

	freq = cpu_to_fdt32(freq);

	OF_setprop(clk_node, "clock-frequency", (void *)&freq, sizeof(freq));
}

static int
aml8726_clkmsr_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,aml8726-clkmsr"))
		return (ENXIO);

	device_set_desc(dev, "Amlogic aml8726 clkmsr");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_clkmsr_attach(device_t dev)
{
	struct aml8726_clkmsr_softc *sc = device_get_softc(dev);
	int freq;

	sc->dev = dev;

	if (bus_alloc_resources(dev, aml8726_clkmsr_spec, sc->res)) {
		device_printf(dev, "can not allocate resources for device\n");
		return (ENXIO);
	}

	freq = aml8726_clkmsr_clock_frequency(sc, AML_CLKMSR_CLK81);
	device_printf(sc->dev, "bus clock %u MHz\n", freq);

	aml8726_clkmsr_fixup_clk81(sc, freq * 1000000);

	return (0);
}

static int
aml8726_clkmsr_detach(device_t dev)
{
	struct aml8726_clkmsr_softc *sc = device_get_softc(dev);

	bus_release_resources(dev, aml8726_clkmsr_spec, sc->res);

	return (0);
}


static device_method_t aml8726_clkmsr_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_clkmsr_probe),
	DEVMETHOD(device_attach,	aml8726_clkmsr_attach),
	DEVMETHOD(device_detach,	aml8726_clkmsr_detach),

	DEVMETHOD_END
};

static driver_t aml8726_clkmsr_driver = {
	"clkmsr",
	aml8726_clkmsr_methods,
	sizeof(struct aml8726_clkmsr_softc),
};

static devclass_t aml8726_clkmsr_devclass;

EARLY_DRIVER_MODULE(clkmsr, simplebus, aml8726_clkmsr_driver,
    aml8726_clkmsr_devclass, 0, 0,  BUS_PASS_CPU + BUS_PASS_ORDER_EARLY);

int
aml8726_clkmsr_bus_frequency()
{
	struct resource mem;
	struct aml8726_clkmsr_softc sc;
	phandle_t node;
	u_long pbase, psize;
	u_long start, size;
	int freq;

	KASSERT(aml8726_soc_hw_rev != AML_SOC_HW_REV_UNKNOWN,
		("aml8726_soc_hw_rev isn't initialized"));

	/*
	 * Try to access the clkmsr node directly i.e. through /aliases/.
	 */

	if ((node = OF_finddevice("clkmsr")) != -1)
		if (fdt_is_compatible_strict(node, "amlogic,aml8726-clkmsr"))
			 goto moveon;

	/*
	 * Find the node the long way.
	 */
	if ((node = OF_finddevice("/soc")) == -1)
		return (0);

	if ((node = fdt_find_compatible(node,
	    "amlogic,aml8726-clkmsr", 1)) == 0)
		return (0);

moveon:
	if (fdt_get_range(OF_parent(node), 0, &pbase, &psize) != 0
	    || fdt_regsize(node, &start, &size) != 0)
		return (0);

	start += pbase;

	memset(&mem, 0, sizeof(mem));

	mem.r_bustag = fdtbus_bs_tag;

	if (bus_space_map(mem.r_bustag, start, size, 0, &mem.r_bushandle) != 0)
		return (0);

	/*
	 * Build an incomplete (however sufficient for the purpose
	 * of calling aml8726_clkmsr_clock_frequency) softc.
	 */

	memset(&sc, 0, sizeof(sc));

	sc.res[0] = &mem;

	freq = aml8726_clkmsr_clock_frequency(&sc, AML_CLKMSR_CLK81) * 1000000;

	bus_space_unmap(mem.r_bustag, mem.r_bushandle, size);

	return (freq);
}
