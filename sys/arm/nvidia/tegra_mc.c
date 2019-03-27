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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Memory controller driver for Tegra SoCs.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/resource.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/extres/clk/clk.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "clock_if.h"

#define	MC_INTSTATUS			0x000
#define	MC_INTMASK			0x004
#define	 MC_INT_DECERR_MTS			(1 << 16)
#define	 MC_INT_SECERR_SEC			(1 << 13)
#define	 MC_INT_DECERR_VPR			(1 << 12)
#define	 MC_INT_INVALID_APB_ASID_UPDATE		(1 << 11)
#define	 MC_INT_INVALID_SMMU_PAGE		(1 << 10)
#define	 MC_INT_ARBITRATION_EMEM		(1 << 9)
#define	 MC_INT_SECURITY_VIOLATION		(1 << 8)
#define	 MC_INT_DECERR_EMEM			(1 << 6)
#define	 MC_INT_INT_MASK	(MC_INT_DECERR_MTS |			\
				 MC_INT_SECERR_SEC |			\
				 MC_INT_DECERR_VPR |			\
				 MC_INT_INVALID_APB_ASID_UPDATE |	\
				 MC_INT_INVALID_SMMU_PAGE |		\
				 MC_INT_ARBITRATION_EMEM |		\
				 MC_INT_SECURITY_VIOLATION |		\
				 MC_INT_DECERR_EMEM)

#define	MC_ERR_STATUS			0x008
#define	 MC_ERR_TYPE(x)				(((x) >> 28) & 0x7)
#define	 MC_ERR_TYPE_DECERR_EMEM		2
#define	 MC_ERR_TYPE_SECURITY_TRUSTZONE		3
#define	 MC_ERR_TYPE_SECURITY_CARVEOUT		4
#define	 MC_ERR_TYPE_INVALID_SMMU_PAGE		6
#define	 MC_ERR_INVALID_SMMU_PAGE_READABLE 	(1 << 27)
#define	 MC_ERR_INVALID_SMMU_PAGE_WRITABLE	(1 << 26)
#define	 MC_ERR_INVALID_SMMU_PAGE_NONSECURE	(1 << 25)
#define	 MC_ERR_ADR_HI(x)			(((x) >> 20) & 0x3)
#define	 MC_ERR_SWAP				(1 << 18)
#define	 MC_ERR_SECURITY			(1 << 17)
#define	 MC_ERR_RW				(1 << 16)
#define	 MC_ERR_ADR1(x)				(((x) >> 12) & 0x7)
#define	 MC_ERR_ID(x)				(((x) >> 0) & 07F)

#define	MC_ERR_ADDR			0x00C
#define	MC_EMEM_CFG			0x050
#define	MC_EMEM_ADR_CFG			0x054
#define	 MC_EMEM_NUMDEV(x)			(((x) >> 0 ) & 0x1)

#define	MC_EMEM_ADR_CFG_DEV0		0x058
#define	MC_EMEM_ADR_CFG_DEV1		0x05C
#define	 EMEM_DEV_DEVSIZE(x)			(((x) >> 16) & 0xF)
#define	 EMEM_DEV_BANKWIDTH(x)			(((x) >>  8) & 0x3)
#define	 EMEM_DEV_COLWIDTH(x)			(((x) >>  8) & 0x3)

#define	WR4(_sc, _r, _v)	bus_write_4((_sc)->mem_res, (_r), (_v))
#define	RD4(_sc, _r)		bus_read_4((_sc)->mem_res, (_r))

#define	LOCK(_sc)		mtx_lock(&(_sc)->mtx)
#define	UNLOCK(_sc)		mtx_unlock(&(_sc)->mtx)
#define	SLEEP(_sc, timeout)	mtx_sleep(sc, &sc->mtx, 0, "tegra_mc", timeout);
#define	LOCK_INIT(_sc)							\
	mtx_init(&_sc->mtx, device_get_nameunit(_sc->dev), "tegra_mc", MTX_DEF)
#define	LOCK_DESTROY(_sc)	mtx_destroy(&_sc->mtx)
#define	ASSERT_LOCKED(_sc)	mtx_assert(&_sc->mtx, MA_OWNED)
#define	ASSERT_UNLOCKED(_sc)	mtx_assert(&_sc->mtx, MA_NOTOWNED)

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-mc",	1},
	{NULL,			0}
};

struct tegra_mc_softc {
	device_t		dev;
	struct mtx		mtx;

	struct resource		*mem_res;
	struct resource		*irq_res;
	void			*irq_h;

	clk_t			clk;
};

static char *smmu_err_tbl[16] = {
	"reserved",		/*  0 */
	"reserved",		/*  1 */
	"DRAM decode",		/*  2 */
	"Trustzome Security",	/*  3 */
	"Security carveout",	/*  4 */
	"reserved",		/*  5 */
	"Invalid SMMU page",	/*  6 */
	"reserved",	/*  7 */
};

static void
tegra_mc_intr(void *arg)
{
	struct tegra_mc_softc *sc;
	uint32_t stat, err;
	uint64_t addr;

	sc = (struct tegra_mc_softc *)arg;

	stat = RD4(sc, MC_INTSTATUS);
	if ((stat & MC_INT_INT_MASK) == 0) {
		WR4(sc, MC_INTSTATUS, stat);
		return;
	}

	device_printf(sc->dev, "Memory Controller Interrupt:\n");
	if (stat & MC_INT_DECERR_MTS)
		printf(" - MTS carveout violation\n");
	if (stat & MC_INT_SECERR_SEC)
		printf(" - SEC carveout violation\n");
	if (stat & MC_INT_DECERR_VPR)
		printf(" - VPR requirements violated\n");
	if (stat & MC_INT_INVALID_APB_ASID_UPDATE)
		printf(" - ivalid APB ASID update\n");
	if (stat & MC_INT_INVALID_SMMU_PAGE)
		printf(" - SMMU address translation error\n");
	if (stat & MC_INT_ARBITRATION_EMEM)
		printf(" - arbitration deadlock-prevention threshold hit\n");
	if (stat & MC_INT_SECURITY_VIOLATION)
		printf(" - SMMU address translation security error\n");
	if (stat & MC_INT_DECERR_EMEM)
		printf(" - SMMU address decode error\n");

	if ((stat & (MC_INT_INVALID_SMMU_PAGE | MC_INT_SECURITY_VIOLATION |
	   MC_INT_DECERR_EMEM)) != 0) {
		err = RD4(sc, MC_ERR_STATUS);
		addr = RD4(sc, MC_ERR_STATUS);
		addr |= (uint64_t)(MC_ERR_ADR_HI(err)) << 32;
		printf(" at 0x%012llX [%s %s %s]  - %s error.\n",
		    addr,
		    stat & MC_ERR_SWAP ? "Swap, " : "",
		    stat & MC_ERR_SECURITY ? "Sec, " : "",
		    stat & MC_ERR_RW ? "Write" : "Read",
		    smmu_err_tbl[MC_ERR_TYPE(err)]);
	}
	WR4(sc, MC_INTSTATUS, stat);
}

static void
tegra_mc_init_hw(struct tegra_mc_softc *sc)
{

	/* Disable and acknowledge all interrupts */
	WR4(sc, MC_INTMASK, 0);
	WR4(sc, MC_INTSTATUS, MC_INT_INT_MASK);
}

static int
tegra_mc_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);
	device_set_desc(dev, "Tegra Memory Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
tegra_mc_attach(device_t dev)
{
	int rv, rid;
	struct tegra_mc_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	LOCK_INIT(sc);

	/* Get the memory resource for the register mapping. */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot map registers.\n");
		rv = ENXIO;
		goto fail;
	}

	/* Allocate our IRQ resource. */
	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "Cannot allocate interrupt.\n");
		rv = ENXIO;
		goto fail;
	}

	/* OFW resources. */
	rv = clk_get_by_ofw_name(dev, 0, "mc", &sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot get mc clock: %d\n", rv);
		goto fail;
	}
	rv = clk_enable(sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot enable clock: %d\n", rv);
		goto fail;
	}

	/* Init hardware. */
	tegra_mc_init_hw(sc);

	/* Setup  interrupt */
	rv = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, tegra_mc_intr, sc, &sc->irq_h);
	if (rv) {
		device_printf(dev, "Cannot setup interrupt.\n");
		goto fail;
	}

	/* Enable Interrupts */
	WR4(sc, MC_INTMASK, MC_INT_INT_MASK);

	return (bus_generic_attach(dev));

fail:
	if (sc->clk != NULL)
		clk_release(sc->clk);
	if (sc->irq_h != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_h);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);
	LOCK_DESTROY(sc);

	return (rv);
}

static int
tegra_mc_detach(device_t dev)
{
	struct tegra_mc_softc *sc;

	sc = device_get_softc(dev);
	if (sc->irq_h != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_h);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	LOCK_DESTROY(sc);
	return (bus_generic_detach(dev));
}

static device_method_t tegra_mc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tegra_mc_probe),
	DEVMETHOD(device_attach,	tegra_mc_attach),
	DEVMETHOD(device_detach,	tegra_mc_detach),


	DEVMETHOD_END
};

static devclass_t tegra_mc_devclass;
static DEFINE_CLASS_0(mc, tegra_mc_driver, tegra_mc_methods,
    sizeof(struct tegra_mc_softc));
DRIVER_MODULE(tegra_mc, simplebus, tegra_mc_driver, tegra_mc_devclass,
    NULL, NULL);
