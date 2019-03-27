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

#include <dev/fdt/fdt_pinctrl.h>
#include <mips/mediatek/mtk_sysctl.h>
#include <mips/mediatek/mtk_soc.h>
#include <mips/mediatek/mtk_pinctrl.h>

#include "fdt_pinctrl_if.h"

static const struct ofw_compat_data compat_data[] = {
	{ "ralink,rt2880-pinmux",	1 },

	/* Sentinel */
	{ NULL,				0 }
};

static int
mtk_pinctrl_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "MTK Pin Controller");

	return (0);
}

static int
mtk_pinctrl_attach(device_t dev)
{

	if (device_get_unit(dev) != 0) {
		device_printf(dev, "Only one pin control allowed\n");
		return (ENXIO);
	}

	if (bootverbose)
		device_printf(dev, "GPIO mode start: 0x%08x\n",
		    mtk_sysctl_get(SYSCTL_GPIOMODE));

	fdt_pinctrl_register(dev, NULL);
	fdt_pinctrl_configure_tree(dev);

	if (bootverbose)
		device_printf(dev, "GPIO mode end  : 0x%08x\n",
		    mtk_sysctl_get(SYSCTL_GPIOMODE));

	return (0);
}

static int
mtk_pinctrl_process_entry(device_t dev, struct mtk_pin_group *table,
    const char *group, char *func)
{
	uint32_t val;
	int found = 0, i, j;

	for (i = 0; table[i].name != NULL; i++) {
                if (strcmp(table[i].name, group) == 0) {
			found = 1;
                        break;
		}
        }

	if (!found)
		return (ENOENT);

        for (j = 0; j < table[i].funcnum; j++) {
                if (strcmp(table[i].functions[j].name, func) == 0) {
                        val = mtk_sysctl_get(table[i].sysc_reg);
                        val &= ~(table[i].mask << table[i].offset);
                        val |= (table[i].functions[j].value << table[i].offset);
                        mtk_sysctl_set(table[i].sysc_reg, val);
                        return (0);
		}
	}

	return (ENOENT);
}

static int
mtk_pinctrl_process_node(device_t dev, struct mtk_pin_group *table,
    phandle_t node)
{
	const char **group_list = NULL;
	char *pin_function = NULL;
	int ret, num_groups, i;

	ret = 0;

	num_groups = ofw_bus_string_list_to_array(node, "ralink,group",
	    &group_list);

	if (num_groups <= 0)
		return (ENOENT);

	if (OF_getprop_alloc_multi(node, "ralink,function", sizeof(*pin_function),
			     (void **)&pin_function) == -1) {
		ret = ENOENT;
		goto out;
	}

	for (i = 0; i < num_groups; i++) {
		if ((ret = mtk_pinctrl_process_entry(dev, table, group_list[i],
		    pin_function)) != 0)
			goto out;
	}

out:
	OF_prop_free(group_list);
	OF_prop_free(pin_function);
	return (ret);
}

static int
mtk_pinctrl_configure(device_t dev, phandle_t cfgxref)
{
	struct mtk_pin_group *pintable;
	phandle_t node, child;
	uint32_t socid;
	int ret;

	node = OF_node_from_xref(cfgxref);
	ret = 0;

	/* Now, get the system type, so we can get the proper GPIO mode array */
	socid = mtk_soc_get_socid();

	switch (socid) {
	case MTK_SOC_RT2880:
		pintable = rt2880_pintable;
		break;
	case MTK_SOC_RT3050: /* fallthrough */
	case MTK_SOC_RT3052:
	case MTK_SOC_RT3350:
		pintable = rt3050_pintable;
		break;
	case MTK_SOC_RT3352:
		pintable = rt3352_pintable;
		break;
	case MTK_SOC_RT3662: /* fallthrough */
	case MTK_SOC_RT3883:
		pintable = rt3883_pintable;
		break;
	case MTK_SOC_RT5350:
		pintable = rt5350_pintable;
		break;
	case MTK_SOC_MT7620A: /* fallthrough */
	case MTK_SOC_MT7620N:
		pintable = mt7620_pintable;
		break;
	case MTK_SOC_MT7628: /* fallthrough */
	case MTK_SOC_MT7688:
		pintable = mt7628_pintable;
		break;
	case MTK_SOC_MT7621:
		pintable = mt7621_pintable;
		break;
	default:
		ret = ENOENT;
		goto out;
	}

	/*
	 * OpenWRT dts files have single child within the pinctrl nodes, which
	 * contains the 'ralink,group' and 'ralink,function' properties.
	 */ 
	for (child = OF_child(node); child != 0 && child != -1;
	    child = OF_peer(child)) {
		if ((ret = mtk_pinctrl_process_node(dev, pintable, child)) != 0)
			return (ret);
	}

out:
	return (ret);
}

static device_method_t mtk_pinctrl_methods[] = {
	DEVMETHOD(device_probe,			mtk_pinctrl_probe),
	DEVMETHOD(device_attach,		mtk_pinctrl_attach),

	/* fdt_pinctrl interface */
	DEVMETHOD(fdt_pinctrl_configure,	mtk_pinctrl_configure),

	DEVMETHOD_END
};

static driver_t mtk_pinctrl_driver = {
	"pinctrl",
	mtk_pinctrl_methods,
	0,
};
static devclass_t mtk_pinctrl_devclass;

EARLY_DRIVER_MODULE(mtk_pinctrl, simplebus, mtk_pinctrl_driver,
    mtk_pinctrl_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_EARLY);

MODULE_DEPEND(mtk_pinctrl, mtk_sysctl, 1, 1, 1);
