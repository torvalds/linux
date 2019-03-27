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

#include <mips/mediatek/fdt_reset.h>
#include <mips/mediatek/mtk_sysctl.h>

#include "fdt_reset_if.h"

static const struct ofw_compat_data compat_data[] = {
	{ "ralink,rt2880-reset",	1 },

	/* Sentinel */
	{ NULL,				0 }
};

static int
mtk_reset_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "MTK Reset Controller");

	return (0);
}

static int
mtk_reset_attach(device_t dev)
{

	if (device_get_unit(dev) != 0) {
		device_printf(dev, "Only one reset control allowed\n");
		return (ENXIO);
	}

	fdt_reset_register_provider(dev);

	return (0);
}

#define RESET_ASSERT	1
#define RESET_DEASSERT	0

static int
mtk_reset_set(device_t dev, int index, int value)
{
	uint32_t mask;

	/* index 0 is SoC reset, indices 1 - 31 are valid peripheral resets */
	if (index < 1 || index > 31)
		return (EINVAL);

	mask = (1u << index);

	if (value == RESET_ASSERT)
		mtk_sysctl_clr_set(SYSCTL_RSTCTRL, 0, mask);
	else
		mtk_sysctl_clr_set(SYSCTL_RSTCTRL, mask, 0);

	return (0);
}

static int
mtk_reset_assert(device_t dev, int index)
{

	return mtk_reset_set(dev, index, RESET_ASSERT);
}

static int
mtk_reset_deassert(device_t dev, int index)
{

	return mtk_reset_set(dev, index, RESET_DEASSERT);
}

static device_method_t mtk_reset_methods[] = {
	DEVMETHOD(device_probe,		mtk_reset_probe),
	DEVMETHOD(device_attach,	mtk_reset_attach),

	/* fdt_reset interface */
	DEVMETHOD(fdt_reset_assert,	mtk_reset_assert),
	DEVMETHOD(fdt_reset_deassert,	mtk_reset_deassert),

	DEVMETHOD_END
};

static driver_t mtk_reset_driver = {
	"rstctrl",
	mtk_reset_methods,
	0,
};
static devclass_t mtk_reset_devclass;

EARLY_DRIVER_MODULE(mtk_reset, simplebus, mtk_reset_driver, mtk_reset_devclass,
    0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_EARLY);

MODULE_DEPEND(mtk_reset, mtk_sysctl, 1, 1, 1);
