/*
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Vadim Pasternak <vadimp@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __LINUX_PLATFORM_DATA_MLXREG_H
#define __LINUX_PLATFORM_DATA_MLXREG_H

#define MLXREG_CORE_LABEL_MAX_SIZE	32

/**
 * struct mlxreg_hotplug_device - I2C device data:
 *
 * @adapter: I2C device adapter;
 * @client: I2C device client;
 * @brdinfo: device board information;
 * @nr: I2C device adapter number, to which device is to be attached;
 *
 * Structure represents I2C hotplug device static data (board topology) and
 * dynamic data (related kernel objects handles).
 */
struct mlxreg_hotplug_device {
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	struct i2c_board_info *brdinfo;
	int nr;
};

/**
 * struct mlxreg_core_data - attributes control data:
 *
 * @label: attribute label;
 * @label: attribute register offset;
 * @reg: attribute register;
 * @mask: attribute access mask;
 * @mode: access mode;
 * @bit: attribute effective bit;
 * @np - pointer to node platform associated with attribute;
 * @hpdev - hotplug device data;
 * @health_cntr: dynamic device health indication counter;
 * @attached: true if device has been attached after good health indication;
 */
struct mlxreg_core_data {
	char label[MLXREG_CORE_LABEL_MAX_SIZE];
	u32 reg;
	u32 mask;
	u32 bit;
	umode_t	mode;
	struct device_node *np;
	struct mlxreg_hotplug_device hpdev;
	u8 health_cntr;
	bool attached;
};

/**
 * struct mlxreg_core_item - same type components controlled by the driver:
 *
 * @data: component data;
 * @aggr_mask: group aggregation mask;
 * @reg: group interrupt status register;
 * @mask: group interrupt mask;
 * @cache: last status value for elements fro the same group;
 * @count: number of available elements in the group;
 * @ind: element's index inside the group;
 * @inversed: if 0: 0 for signal status is OK, if 1 - 1 is OK;
 * @health: true if device has health indication, false in other case;
 */
struct mlxreg_core_item {
	struct mlxreg_core_data *data;
	u32 aggr_mask;
	u32 reg;
	u32 mask;
	u32 cache;
	u8 count;
	u8 ind;
	u8 inversed;
	u8 health;
};

/**
 * struct mlxreg_core_platform_data - platform data:
 *
 * @led_data: led private data;
 * @regmap: register map of parent device;
 * @counter: number of led instances;
 */
struct mlxreg_core_platform_data {
	struct mlxreg_core_data *data;
	void *regmap;
	int counter;
};

/**
 * struct mlxreg_core_hotplug_platform_data - hotplug platform data:
 *
 * @items: same type components with the hotplug capability;
 * @irq: platform interrupt number;
 * @regmap: register map of parent device;
 * @counter: number of the components with the hotplug capability;
 * @cell: location of top aggregation interrupt register;
 * @mask: top aggregation interrupt common mask;
 * @cell_low: location of low aggregation interrupt register;
 * @mask_low: low aggregation interrupt common mask;
 */
struct mlxreg_core_hotplug_platform_data {
	struct mlxreg_core_item *items;
	int irq;
	void *regmap;
	int counter;
	u32 cell;
	u32 mask;
	u32 cell_low;
	u32 mask_low;
};

#endif /* __LINUX_PLATFORM_DATA_MLXREG_H */
