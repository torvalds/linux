/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/nvidia/tegra_pmc.h>

#define	PMC_CNTRL			0x000
#define	 PMC_CNTRL_CPUPWRGOOD_SEL_MASK		(0x3 << 20)
#define	 PMC_CNTRL_CPUPWRGOOD_SEL_SHIFT		20
#define	 PMC_CNTRL_CPUPWRGOOD_EN		(1 << 19)
#define	 PMC_CNTRL_FUSE_OVERRIDE		(1 << 18)
#define	 PMC_CNTRL_INTR_POLARITY		(1 << 17)
#define	 PMC_CNTRL_CPU_PWRREQ_OE		(1 << 16)
#define	 PMC_CNTRL_CPU_PWRREQ_POLARITY		(1 << 15)
#define	 PMC_CNTRL_SIDE_EFFECT_LP0		(1 << 14)
#define	 PMC_CNTRL_AOINIT			(1 << 13)
#define	 PMC_CNTRL_PWRGATE_DIS			(1 << 12)
#define	 PMC_CNTRL_SYSCLK_OE			(1 << 11)
#define	 PMC_CNTRL_SYSCLK_POLARITY		(1 << 10)
#define	 PMC_CNTRL_PWRREQ_OE			(1 <<  9)
#define	 PMC_CNTRL_PWRREQ_POLARITY		(1 <<  8)
#define	 PMC_CNTRL_BLINK_EN			(1 <<  7)
#define	 PMC_CNTRL_GLITCHDET_DIS		(1 <<  6)
#define	 PMC_CNTRL_LATCHWAKE_EN			(1 <<  5)
#define	 PMC_CNTRL_MAIN_RST			(1 <<  4)
#define	 PMC_CNTRL_KBC_RST			(1 <<  3)
#define	 PMC_CNTRL_RTC_RST			(1 <<  2)
#define	 PMC_CNTRL_RTC_CLK_DIS			(1 <<  1)
#define	 PMC_CNTRL_KBC_CLK_DIS			(1 <<  0)

#define	PMC_DPD_SAMPLE			0x020

#define	PMC_CLAMP_STATUS		0x02C
#define	  PMC_CLAMP_STATUS_PARTID(x)		(1 << ((x) & 0x1F))

#define	PMC_PWRGATE_TOGGLE		0x030
#define	 PMC_PWRGATE_TOGGLE_START		(1 << 8)
#define	 PMC_PWRGATE_TOGGLE_PARTID(x)		(((x) & 0x1F) << 0)

#define	PMC_REMOVE_CLAMPING_CMD		0x034
#define	  PMC_REMOVE_CLAMPING_CMD_PARTID(x)	(1 << ((x) & 0x1F))

#define	PMC_PWRGATE_STATUS		0x038
#define	PMC_PWRGATE_STATUS_PARTID(x)		(1 << ((x) & 0x1F))

#define	PMC_SCRATCH0			0x050
#define	 PMC_SCRATCH0_MODE_RECOVERY		(1 << 31)
#define	 PMC_SCRATCH0_MODE_BOOTLOADER		(1 << 30)
#define	 PMC_SCRATCH0_MODE_RCM			(1 << 1)
#define	 PMC_SCRATCH0_MODE_MASK			(PMC_SCRATCH0_MODE_RECOVERY | \
						PMC_SCRATCH0_MODE_BOOTLOADER | \
						PMC_SCRATCH0_MODE_RCM)

#define	PMC_CPUPWRGOOD_TIMER		0x0c8
#define	PMC_CPUPWROFF_TIMER		0x0cc

#define	PMC_SCRATCH41			0x140

#define	PMC_SENSOR_CTRL			0x1b0
#define	PMC_SENSOR_CTRL_BLOCK_SCRATCH_WRITE	(1 << 2)
#define	PMC_SENSOR_CTRL_ENABLE_RST		(1 << 1)
#define	PMC_SENSOR_CTRL_ENABLE_PG		(1 << 0)

#define	PMC_IO_DPD_REQ			0x1b8
#define	 PMC_IO_DPD_REQ_CODE_IDLE		(0 << 30)
#define	 PMC_IO_DPD_REQ_CODE_OFF		(1 << 30)
#define	 PMC_IO_DPD_REQ_CODE_ON			(2 << 30)
#define	 PMC_IO_DPD_REQ_CODE_MASK		(3 << 30)

#define	PMC_IO_DPD_STATUS		0x1bc
#define	 PMC_IO_DPD_STATUS_HDMI			(1 << 28)
#define	PMC_IO_DPD2_REQ			0x1c0
#define	PMC_IO_DPD2_STATUS		0x1c4
#define	 PMC_IO_DPD2_STATUS_HV			(1 << 6)
#define	PMC_SEL_DPD_TIM			0x1c8

#define	PMC_SCRATCH54			0x258
#define	PMC_SCRATCH54_DATA_SHIFT		8
#define	PMC_SCRATCH54_ADDR_SHIFT		0

#define	PMC_SCRATCH55			0x25c
#define	PMC_SCRATCH55_RST_ENABLE		(1 << 31)
#define	PMC_SCRATCH55_CNTRL_TYPE		(1 << 30)
#define	PMC_SCRATCH55_CNTRL_ID_SHIFT		27
#define	PMC_SCRATCH55_CNTRL_ID_MASK		0x07
#define	PMC_SCRATCH55_PINMUX_SHIFT		24
#define	PMC_SCRATCH55_PINMUX_MASK		0x07
#define	PMC_SCRATCH55_CHECKSUM_SHIFT		16
#define	PMC_SCRATCH55_CHECKSUM_MASK		0xFF
#define	PMC_SCRATCH55_16BITOP			(1 << 15)
#define	PMC_SCRATCH55_I2CSLV1_SHIFT		0
#define	PMC_SCRATCH55_I2CSLV1_MASK		0x7F

#define	PMC_GPU_RG_CNTRL		0x2d4

#define	WR4(_sc, _r, _v)	bus_write_4((_sc)->mem_res, (_r), (_v))
#define	RD4(_sc, _r)		bus_read_4((_sc)->mem_res, (_r))

#define	PMC_LOCK(_sc)		mtx_lock(&(_sc)->mtx)
#define	PMC_UNLOCK(_sc)		mtx_unlock(&(_sc)->mtx)
#define	PMC_LOCK_INIT(_sc)	mtx_init(&(_sc)->mtx, 			\
	    device_get_nameunit(_sc->dev), "tegra124_pmc", MTX_DEF)
#define	PMC_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->mtx);
#define	PMC_ASSERT_LOCKED(_sc)	mtx_assert(&(_sc)->mtx, MA_OWNED);
#define	PMC_ASSERT_UNLOCKED(_sc) mtx_assert(&(_sc)->mtx, MA_NOTOWNED);

struct tegra124_pmc_softc {
	device_t		dev;
	struct resource		*mem_res;
	clk_t			clk;
	struct mtx		mtx;

	uint32_t		rate;
	enum tegra_suspend_mode suspend_mode;
	uint32_t		cpu_good_time;
	uint32_t		cpu_off_time;
	uint32_t		core_osc_time;
	uint32_t		core_pmu_time;
	uint32_t		core_off_time;
	int			corereq_high;
	int			sysclkreq_high;
	int			combined_req;
	int			cpu_pwr_good_en;
	uint32_t		lp0_vec_phys;
	uint32_t		lp0_vec_size;
};

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-pmc",		1},
	{NULL,				0},
};

static struct tegra124_pmc_softc *pmc_sc;

static inline struct tegra124_pmc_softc *
tegra124_pmc_get_sc(void)
{
	if (pmc_sc == NULL)
		panic("To early call to Tegra PMC driver.\n");
	return (pmc_sc);
}

static int
tegra124_pmc_set_powergate(struct tegra124_pmc_softc *sc,
    enum tegra_powergate_id id, int ena)
{
	uint32_t reg;
	int i;

	PMC_LOCK(sc);

	reg = RD4(sc, PMC_PWRGATE_STATUS) & PMC_PWRGATE_STATUS_PARTID(id);
	if (((reg != 0) && ena) || ((reg == 0) && !ena)) {
		PMC_UNLOCK(sc);
		return (0);
	}

	for (i = 100; i > 0; i--) {
		reg = RD4(sc, PMC_PWRGATE_TOGGLE);
		if ((reg & PMC_PWRGATE_TOGGLE_START) == 0)
			break;
		DELAY(1);
	}
	if (i <= 0)
		device_printf(sc->dev,
		    "Timeout when waiting for TOGGLE_START\n");

	WR4(sc, PMC_PWRGATE_TOGGLE,
	    PMC_PWRGATE_TOGGLE_START | PMC_PWRGATE_TOGGLE_PARTID(id));

	for (i = 100; i > 0; i--) {
		reg = RD4(sc, PMC_PWRGATE_TOGGLE);
		if ((reg & PMC_PWRGATE_TOGGLE_START) == 0)
			break;
		DELAY(1);
	}
	if (i <= 0)
		device_printf(sc->dev,
		    "Timeout when waiting for TOGGLE_START\n");
		PMC_UNLOCK(sc);
	return (0);
}

int
tegra_powergate_remove_clamping(enum tegra_powergate_id  id)
{
	struct tegra124_pmc_softc *sc;
	uint32_t reg;
	enum tegra_powergate_id swid;
	int i;

	sc = tegra124_pmc_get_sc();

	if (id == TEGRA_POWERGATE_3D) {
		WR4(sc, PMC_GPU_RG_CNTRL, 0);
		return (0);
	}

	reg = RD4(sc, PMC_PWRGATE_STATUS);
	if ((reg & PMC_PWRGATE_STATUS_PARTID(id)) == 0)
		panic("Attempt to remove clamping for unpowered partition.\n");

	if (id == TEGRA_POWERGATE_PCX)
		swid = TEGRA_POWERGATE_VDE;
	else if (id == TEGRA_POWERGATE_VDE)
		swid = TEGRA_POWERGATE_PCX;
	else
		swid = id;
	WR4(sc, PMC_REMOVE_CLAMPING_CMD, PMC_REMOVE_CLAMPING_CMD_PARTID(swid));

	for (i = 100; i > 0; i--) {
		reg = RD4(sc, PMC_REMOVE_CLAMPING_CMD);
		if ((reg & PMC_REMOVE_CLAMPING_CMD_PARTID(swid)) == 0)
			break;
		DELAY(1);
	}
	if (i <= 0)
		device_printf(sc->dev, "Timeout when remove clamping\n");

	reg = RD4(sc, PMC_CLAMP_STATUS);
	if ((reg & PMC_CLAMP_STATUS_PARTID(id)) != 0)
		panic("Cannot remove clamping\n");

	return (0);
}

int
tegra_powergate_is_powered(enum tegra_powergate_id id)
{
	struct tegra124_pmc_softc *sc;
	uint32_t reg;

	sc = tegra124_pmc_get_sc();

	reg = RD4(sc, PMC_PWRGATE_STATUS);
	return ((reg & PMC_PWRGATE_STATUS_PARTID(id)) ? 1 : 0);
}

int
tegra_powergate_power_on(enum tegra_powergate_id id)
{
	struct tegra124_pmc_softc *sc;
	int rv, i;

	sc = tegra124_pmc_get_sc();

	rv = tegra124_pmc_set_powergate(sc, id, 1);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot set powergate: %d\n", id);
		return (rv);
	}

	for (i = 100; i > 0; i--) {
		if (tegra_powergate_is_powered(id))
			break;
		DELAY(1);
	}
	if (i <= 0)
		device_printf(sc->dev, "Timeout when waiting on power up\n");

	return (rv);
}

int
tegra_powergate_power_off(enum tegra_powergate_id id)
{
	struct tegra124_pmc_softc *sc;
	int rv, i;

	sc = tegra124_pmc_get_sc();

	rv = tegra124_pmc_set_powergate(sc, id, 0);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot set powergate: %d\n", id);
		return (rv);
	}
	for (i = 100; i > 0; i--) {
		if (!tegra_powergate_is_powered(id))
			break;
		DELAY(1);
	}
	if (i <= 0)
		device_printf(sc->dev, "Timeout when waiting on power off\n");

	return (rv);
}

int
tegra_powergate_sequence_power_up(enum tegra_powergate_id id, clk_t clk,
    hwreset_t rst)
{
	struct tegra124_pmc_softc *sc;
	int rv;

	sc = tegra124_pmc_get_sc();

	rv = hwreset_assert(rst);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot assert reset\n");
		return (rv);
	}

	rv = clk_stop(clk);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot stop clock\n");
		goto clk_fail;
	}

	rv = tegra_powergate_power_on(id);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot power on powergate\n");
		goto clk_fail;
	}

	rv = clk_enable(clk);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable clock\n");
		goto clk_fail;
	}
	DELAY(20);

	rv = tegra_powergate_remove_clamping(id);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot remove clamping\n");
		goto fail;
	}
	rv = hwreset_deassert(rst);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot unreset reset\n");
		goto fail;
	}
	return 0;

fail:
	clk_disable(clk);
clk_fail:
	hwreset_assert(rst);
	tegra_powergate_power_off(id);
	return (rv);
}

static int
tegra124_pmc_parse_fdt(struct tegra124_pmc_softc *sc, phandle_t node)
{
	int rv;
	uint32_t tmp;
	uint32_t tmparr[2];

	rv = OF_getencprop(node, "nvidia,suspend-mode", &tmp, sizeof(tmp));
	if (rv > 0) {
		switch (tmp) {
		case 0:
			sc->suspend_mode = TEGRA_SUSPEND_LP0;
			break;

		case 1:
			sc->suspend_mode = TEGRA_SUSPEND_LP1;
			break;

		case 2:
			sc->suspend_mode = TEGRA_SUSPEND_LP2;
			break;

		default:
			sc->suspend_mode = TEGRA_SUSPEND_NONE;
			break;
		}
	}

	rv = OF_getencprop(node, "nvidia,cpu-pwr-good-time", &tmp, sizeof(tmp));
	if (rv > 0) {
		sc->cpu_good_time = tmp;
		sc->suspend_mode = TEGRA_SUSPEND_NONE;
	}

	rv = OF_getencprop(node, "nvidia,cpu-pwr-off-time", &tmp, sizeof(tmp));
	if (rv > 0) {
		sc->cpu_off_time = tmp;
		sc->suspend_mode = TEGRA_SUSPEND_NONE;
	}

	rv = OF_getencprop(node, "nvidia,core-pwr-good-time", tmparr,
	    sizeof(tmparr));
	if (rv == sizeof(tmparr)) {
		sc->core_osc_time = tmparr[0];
		sc->core_pmu_time = tmparr[1];
		sc->suspend_mode = TEGRA_SUSPEND_NONE;
	}

	rv = OF_getencprop(node, "nvidia,core-pwr-off-time", &tmp, sizeof(tmp));
	if (rv > 0) {
		sc->core_off_time = tmp;
		sc->suspend_mode = TEGRA_SUSPEND_NONE;
	}

	sc->corereq_high =
	    OF_hasprop(node, "nvidia,core-power-req-active-high");
	sc->sysclkreq_high =
	    OF_hasprop(node, "nvidia,sys-clock-req-active-high");
	sc->combined_req =
	    OF_hasprop(node, "nvidia,combined-power-req");
	sc->cpu_pwr_good_en =
	    OF_hasprop(node, "nvidia,cpu-pwr-good-en");

	rv = OF_getencprop(node, "nvidia,lp0-vec", tmparr, sizeof(tmparr));
	if (rv == sizeof(tmparr)) {

		sc->lp0_vec_phys = tmparr[0];
		sc->core_pmu_time = tmparr[1];
		sc->lp0_vec_size = TEGRA_SUSPEND_NONE;
		if (sc->suspend_mode == TEGRA_SUSPEND_LP0)
			sc->suspend_mode = TEGRA_SUSPEND_LP1;
	}
	return 0;
}

static int
tegra124_pmc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Tegra PMC");
	return (BUS_PROBE_DEFAULT);
}

static int
tegra124_pmc_detach(device_t dev)
{

	/* This device is always present. */
	return (EBUSY);
}

static int
tegra124_pmc_attach(device_t dev)
{
	struct tegra124_pmc_softc *sc;
	int rid, rv;
	uint32_t reg;
	phandle_t node;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node = ofw_bus_get_node(dev);

	rv = tegra124_pmc_parse_fdt(sc, node);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot parse FDT data\n");
		return (rv);
	}

	rv = clk_get_by_ofw_name(sc->dev, 0, "pclk", &sc->clk);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get \"pclk\" clock\n");
		return (ENXIO);
	}

	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		return (ENXIO);
	}

	PMC_LOCK_INIT(sc);

	/* Enable CPU power request. */
	reg = RD4(sc, PMC_CNTRL);
	reg |= PMC_CNTRL_CPU_PWRREQ_OE;
	WR4(sc, PMC_CNTRL, reg);

	/* Set sysclk output polarity */
	reg = RD4(sc, PMC_CNTRL);
	if (sc->sysclkreq_high)
		reg &= ~PMC_CNTRL_SYSCLK_POLARITY;
	else
		reg |= PMC_CNTRL_SYSCLK_POLARITY;
	WR4(sc, PMC_CNTRL, reg);

	/* Enable sysclk request. */
	reg = RD4(sc, PMC_CNTRL);
	reg |= PMC_CNTRL_SYSCLK_OE;
	WR4(sc, PMC_CNTRL, reg);

	/*
	 * Remove HDMI from deep power down mode.
	 * XXX mote this to HDMI driver
	 */
	reg = RD4(sc, PMC_IO_DPD_STATUS);
	reg &= ~ PMC_IO_DPD_STATUS_HDMI;
	WR4(sc, PMC_IO_DPD_STATUS, reg);

	reg = RD4(sc, PMC_IO_DPD2_STATUS);
	reg &= ~ PMC_IO_DPD2_STATUS_HV;
	WR4(sc, PMC_IO_DPD2_STATUS, reg);

	if (pmc_sc != NULL)
		panic("tegra124_pmc: double driver attach");
	pmc_sc = sc;
	return (0);
}

static device_method_t tegra124_pmc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tegra124_pmc_probe),
	DEVMETHOD(device_attach,	tegra124_pmc_attach),
	DEVMETHOD(device_detach,	tegra124_pmc_detach),

	DEVMETHOD_END
};

static devclass_t tegra124_pmc_devclass;
static DEFINE_CLASS_0(pmc, tegra124_pmc_driver, tegra124_pmc_methods,
    sizeof(struct tegra124_pmc_softc));
EARLY_DRIVER_MODULE(tegra124_pmc, simplebus, tegra124_pmc_driver,
    tegra124_pmc_devclass, NULL, NULL, 70);
