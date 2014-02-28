/*
 *
 * Copyright (C) 2014 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/device.h>

#if 0
#define IO_DOMAIN_DBG(fmt, args...) printk( "IO_DOMAIN_DBG:\t"fmt, ##args)
#else
#define IO_DOMAIN_DBG(fmt, args...) {while(0);}
#endif

#define io_domain_regulator_get(dev,id) regulator_get((dev),(id))
#define io_domain_regulator_put(regu) regulator_put((regu))
#define io_domain_regulator_get_voltage(regu) regulator_get_voltage((regu))

int io_domain_regulator_set_voltage(struct regulator *regulator, int min_uV, int max_uV);

#define IO_VOL_DOMAIN_3V3 2800000
#define IO_VOL_DOMAIN_1V8 1500000

struct vd_node {
	const char		*name;
	const char		*regulator_name;
	struct regulator	*regulator;
	struct list_head	node;
};
struct io_domain_port {
	struct pinctrl		*pctl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_1v8;
	struct pinctrl_state	*pins_3v3;
};

struct io_domain_device{
	struct device *dev;
	struct device_node *of_node;
	
};

