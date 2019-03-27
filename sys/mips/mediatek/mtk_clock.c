/*-
 * Copyright (c) 2016 Stanislav Galabov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/fdt/fdt_clock.h>

#include <mips/mediatek/mtk_sysctl.h>

#include "fdt_clock_if.h"

static const struct ofw_compat_data compat_data[] = {
	{ "ralink,rt2880-clock",	1 },

	/* Sentinel */
	{ NULL,				0 }
};

static int
mtk_clock_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "MTK Clock Controller");

	return (0);
}

static int
mtk_clock_attach(device_t dev)
{

	if (device_get_unit(dev) != 0) {
		device_printf(dev, "Only one clock control allowed\n");
		return (ENXIO);
	}

	fdt_clock_register_provider(dev);

	return (0);
}

#define CLOCK_ENABLE	1
#define CLOCK_DISABLE	0

static int
mtk_clock_set(device_t dev, int index, int value)
{
	uint32_t mask;

	/* Clock config register holds 32 clock gating bits */
	if (index < 0 || index > 31)
		return (EINVAL);

	mask = (1u << index);

	if (value == CLOCK_ENABLE)
		mtk_sysctl_clr_set(SYSCTL_CLKCFG1, 0, mask);
	else
		mtk_sysctl_clr_set(SYSCTL_CLKCFG1, mask, 0);

	return (0);
}

static int
mtk_clock_enable(device_t dev, int index)
{

	return mtk_clock_set(dev, index, CLOCK_ENABLE);
}

static int
mtk_clock_disable(device_t dev, int index)
{

	return mtk_clock_set(dev, index, CLOCK_DISABLE);
}

static int
mtk_clock_get_info(device_t dev, int index, struct fdt_clock_info *info)
{
	uint32_t mask;

	if (index < 0 || index > 31 || info == NULL)
		return (EINVAL);

	if (mtk_sysctl_get(SYSCTL_CLKCFG1) & mask)
		info->flags = FDT_CIFLAG_RUNNING;
	else
		info->flags = 0;

	return (0);
}

static device_method_t mtk_clock_methods[] = {
	DEVMETHOD(device_probe,		mtk_clock_probe),
	DEVMETHOD(device_attach,	mtk_clock_attach),

	/* fdt_clock interface */
	DEVMETHOD(fdt_clock_enable,	mtk_clock_enable),
	DEVMETHOD(fdt_clock_disable,	mtk_clock_disable),
	DEVMETHOD(fdt_clock_get_info,	mtk_clock_get_info),

	DEVMETHOD_END
};

static driver_t mtk_clock_driver = {
	"clkctrl",
	mtk_clock_methods,
	0,
};
static devclass_t mtk_clock_devclass;

EARLY_DRIVER_MODULE(mtk_clock, simplebus, mtk_clock_driver, mtk_clock_devclass,
    0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_EARLY);

MODULE_DEPEND(mtk_clock, mtk_sysctl, 1, 1, 1);
