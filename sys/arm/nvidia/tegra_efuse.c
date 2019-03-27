/*-
 * Copyright (c) 2015 Michal Meloun
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
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/nvidia/tegra_efuse.h>


#define	RD4(_sc, _r)	bus_read_4((_sc)->mem_res, (_sc)->fuse_begin + (_r))

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-efuse",	1},
	{NULL,			0}
};

struct tegra_efuse_softc {
	device_t		dev;
	struct resource		*mem_res;

	int			fuse_begin;
	clk_t			clk;
	hwreset_t			reset;
};
struct tegra_efuse_softc *dev_sc;

struct tegra_sku_info tegra_sku_info;
static char *tegra_rev_name[] = {
	[TEGRA_REVISION_UNKNOWN] = "unknown",
	[TEGRA_REVISION_A01]     = "A01",
	[TEGRA_REVISION_A02]     = "A02",
	[TEGRA_REVISION_A03]     = "A03",
	[TEGRA_REVISION_A03p]    = "A03 prime",
	[TEGRA_REVISION_A04]     = "A04",
};

/* Tegra30 and later */
#define	FUSE_VENDOR_CODE	0x100
#define	FUSE_FAB_CODE		0x104
#define	FUSE_LOT_CODE_0		0x108
#define	FUSE_LOT_CODE_1		0x10c
#define	FUSE_WAFER_ID		0x110
#define	FUSE_X_COORDINATE	0x114
#define	FUSE_Y_COORDINATE	0x118

/* ---------------------- Tegra 124 specific code & data --------------- */
#define	TEGRA124_FUSE_BEGIN		0x100

#define	TEGRA124_CPU_PROCESS_CORNERS	2
#define	TEGRA124_GPU_PROCESS_CORNERS	2
#define	TEGRA124_SOC_PROCESS_CORNERS	2

#define	TEGRA124_FUSE_SKU_INFO		0x10
#define	TEGRA124_FUSE_CPU_SPEEDO_0	0x14
#define	TEGRA124_FUSE_CPU_IDDQ		0x18
#define	TEGRA124_FUSE_FT_REV		0x28
#define	TEGRA124_FUSE_CPU_SPEEDO_1	0x2c
#define	TEGRA124_FUSE_CPU_SPEEDO_2	0x30
#define	TEGRA124_FUSE_SOC_SPEEDO_0	0x34
#define	TEGRA124_FUSE_SOC_SPEEDO_1	0x38
#define	TEGRA124_FUSE_SOC_SPEEDO_2	0x3c
#define	TEGRA124_FUSE_SOC_IDDQ		0x40
#define	TEGRA124_FUSE_GPU_IDDQ		0x128

enum {
	TEGRA124_THRESHOLD_INDEX_0,
	TEGRA124_THRESHOLD_INDEX_1,
	TEGRA124_THRESHOLD_INDEX_COUNT,
};

static uint32_t tegra124_cpu_process_speedos[][TEGRA124_CPU_PROCESS_CORNERS] =
{
	{2190,	UINT_MAX},
	{0,	UINT_MAX},
};

static uint32_t tegra124_gpu_process_speedos[][TEGRA124_GPU_PROCESS_CORNERS] =
{
	{1965,	UINT_MAX},
	{0,	UINT_MAX},
};

static uint32_t tegra124_soc_process_speedos[][TEGRA124_SOC_PROCESS_CORNERS] =
{
	{2101,	UINT_MAX},
	{0,	UINT_MAX},
};

static void
tegra124_rev_sku_to_speedo_ids(struct tegra_efuse_softc *sc,
    struct tegra_sku_info *sku, int *threshold)
{

	/* Assign to default */
	sku->cpu_speedo_id = 0;
	sku->soc_speedo_id = 0;
	sku->gpu_speedo_id = 0;
	*threshold = TEGRA124_THRESHOLD_INDEX_0;

	switch (sku->sku_id) {
	case 0x00: /* Eng sku */
	case 0x0F:
	case 0x23:
		/* Using the default */
		break;
	case 0x83:
		sku->cpu_speedo_id = 2;
		break;

	case 0x1F:
	case 0x87:
	case 0x27:
		sku->cpu_speedo_id = 2;
		sku->soc_speedo_id = 0;
		sku->gpu_speedo_id = 1;
		*threshold = TEGRA124_THRESHOLD_INDEX_0;
		break;
	case 0x81:
	case 0x21:
	case 0x07:
		sku->cpu_speedo_id = 1;
		sku->soc_speedo_id = 1;
		sku->gpu_speedo_id = 1;
		*threshold = TEGRA124_THRESHOLD_INDEX_1;
		break;
	case 0x49:
	case 0x4A:
	case 0x48:
		sku->cpu_speedo_id = 4;
		sku->soc_speedo_id = 2;
		sku->gpu_speedo_id = 3;
		*threshold = TEGRA124_THRESHOLD_INDEX_1;
		break;
	default:
		device_printf(sc->dev, " Unknown SKU ID %d\n", sku->sku_id);
		break;
	}
}


static void
tegra124_init_speedo(struct tegra_efuse_softc *sc, struct tegra_sku_info *sku)
{
	int i, threshold;

	sku->sku_id = RD4(sc, TEGRA124_FUSE_SKU_INFO);
	sku->soc_iddq_value = RD4(sc, TEGRA124_FUSE_SOC_IDDQ);
	sku->cpu_iddq_value = RD4(sc, TEGRA124_FUSE_CPU_IDDQ);
	sku->gpu_iddq_value = RD4(sc, TEGRA124_FUSE_GPU_IDDQ);
	sku->soc_speedo_value = RD4(sc, TEGRA124_FUSE_SOC_SPEEDO_0);
	sku->cpu_speedo_value = RD4(sc, TEGRA124_FUSE_CPU_SPEEDO_0);
	sku->gpu_speedo_value = RD4(sc, TEGRA124_FUSE_CPU_SPEEDO_2);

	if (sku->cpu_speedo_value == 0) {
		device_printf(sc->dev, "CPU Speedo value is not fused.\n");
		return;
	}

	tegra124_rev_sku_to_speedo_ids(sc, sku, &threshold);

	for (i = 0; i < TEGRA124_SOC_PROCESS_CORNERS; i++) {
		if (sku->soc_speedo_value <
			tegra124_soc_process_speedos[threshold][i])
			break;
	}
	sku->soc_process_id = i;

	for (i = 0; i < TEGRA124_CPU_PROCESS_CORNERS; i++) {
		if (sku->cpu_speedo_value <
			tegra124_cpu_process_speedos[threshold][i])
				break;
	}
	sku->cpu_process_id = i;

	for (i = 0; i < TEGRA124_GPU_PROCESS_CORNERS; i++) {
		if (sku->gpu_speedo_value <
			tegra124_gpu_process_speedos[threshold][i])
			break;
	}
	sku->gpu_process_id = i;

}

/* ----------------- End of Tegra 124 specific code & data --------------- */

uint32_t
tegra_fuse_read_4(int addr) {

	if (dev_sc == NULL)
		panic("tegra_fuse_read_4 called too early");
	return (RD4(dev_sc, addr));
}


static void
tegra_efuse_dump_sku(void)
{
	printf(" TEGRA SKU Info:\n");
	printf("  chip_id: %u\n", tegra_sku_info.chip_id);
	printf("  sku_id: %u\n", tegra_sku_info.sku_id);
	printf("  cpu_process_id: %u\n", tegra_sku_info.cpu_process_id);
	printf("  cpu_speedo_id: %u\n", tegra_sku_info.cpu_speedo_id);
	printf("  cpu_speedo_value: %u\n", tegra_sku_info.cpu_speedo_value);
	printf("  cpu_iddq_value: %u\n", tegra_sku_info.cpu_iddq_value);
	printf("  soc_process_id: %u\n", tegra_sku_info.soc_process_id);
	printf("  soc_speedo_id: %u\n", tegra_sku_info.soc_speedo_id);
	printf("  soc_speedo_value: %u\n", tegra_sku_info.soc_speedo_value);
	printf("  soc_iddq_value: %u\n", tegra_sku_info.soc_iddq_value);
	printf("  gpu_process_id: %u\n", tegra_sku_info.gpu_process_id);
	printf("  gpu_speedo_id: %u\n", tegra_sku_info.gpu_speedo_id);
	printf("  gpu_speedo_value: %u\n", tegra_sku_info.gpu_speedo_value);
	printf("  gpu_iddq_value: %u\n", tegra_sku_info.gpu_iddq_value);
	printf("  revision: %s\n", tegra_rev_name[tegra_sku_info.revision]);
}

static int
tegra_efuse_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	return (BUS_PROBE_DEFAULT);
}

static int
tegra_efuse_attach(device_t dev)
{
	int rv, rid;
	phandle_t node;
	struct tegra_efuse_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;
	node = ofw_bus_get_node(dev);

	/* Get the memory resource for the register mapping. */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot map registers.\n");
		rv = ENXIO;
		goto fail;
	}

	/* OFW resources. */
	rv = clk_get_by_ofw_name(dev, 0, "fuse", &sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot get fuse clock: %d\n", rv);
		goto fail;
	}
	rv = clk_enable(sc->clk);
	if (rv != 0) {
		device_printf(dev, "Cannot enable clock: %d\n", rv);
		goto fail;
	}
	rv = hwreset_get_by_ofw_name(sc->dev, 0, "fuse", &sc->reset);
	if (rv != 0) {
		device_printf(dev, "Cannot get fuse reset\n");
		goto fail;
	}
	rv = hwreset_deassert(sc->reset);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot clear reset\n");
		goto fail;
	}

	/* Tegra124 specific init. */
	sc->fuse_begin = TEGRA124_FUSE_BEGIN;
	tegra124_init_speedo(sc, &tegra_sku_info);

	dev_sc = sc;

	if (bootverbose)
		tegra_efuse_dump_sku();
	return (bus_generic_attach(dev));

fail:
	dev_sc = NULL;
	if (sc->clk != NULL)
		clk_release(sc->clk);
	if (sc->reset != NULL)
		hwreset_release(sc->reset);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	return (rv);
}

static int
tegra_efuse_detach(device_t dev)
{
	struct tegra_efuse_softc *sc;

	sc = device_get_softc(dev);
	dev_sc = NULL;
	if (sc->clk != NULL)
		clk_release(sc->clk);
	if (sc->reset != NULL)
		hwreset_release(sc->reset);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);

	return (bus_generic_detach(dev));
}

static device_method_t tegra_efuse_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tegra_efuse_probe),
	DEVMETHOD(device_attach,	tegra_efuse_attach),
	DEVMETHOD(device_detach,	tegra_efuse_detach),

	DEVMETHOD_END
};

static devclass_t tegra_efuse_devclass;
static DEFINE_CLASS_0(efuse, tegra_efuse_driver, tegra_efuse_methods,
    sizeof(struct tegra_efuse_softc));
EARLY_DRIVER_MODULE(tegra_efuse, simplebus, tegra_efuse_driver,
    tegra_efuse_devclass, NULL, NULL, BUS_PASS_TIMER);
