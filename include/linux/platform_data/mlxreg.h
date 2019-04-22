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
#define MLXREG_CORE_WD_FEATURE_NOWAYOUT		BIT(0)
#define MLXREG_CORE_WD_FEATURE_START_AT_BOOT	BIT(1)

/**
 * enum mlxreg_wdt_type - type of HW watchdog
 *
 * TYPE1 HW watchdog implementation exist in old systems.
 * All new systems have TYPE2 HW watchdog.
 */
enum mlxreg_wdt_type {
	MLX_WDT_TYPE1,
	MLX_WDT_TYPE2,
};

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
 * @reg: attribute register;
 * @mask: attribute access mask;
 * @bit: attribute effective bit;
 * @capability: attribute capability register;
 * @mode: access mode;
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
	u32 capability;
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
 * @data: instance private data;
 * @regmap: register map of parent device;
 * @counter: number of instances;
 * @features: supported features of device;
 * @version: implementation version;
 * @identity: device identity name;
 */
struct mlxreg_core_platform_data {
	struct mlxreg_core_data *data;
	void *regmap;
	int counter;
	u32 features;
	u32 version;
	char identity[MLXREG_CORE_LABEL_MAX_SIZE];
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
 * @deferred_nr: I2C adapter number must be exist prior probing execution;
 * @shift_nr: I2C adapter numbers must be incremented by this value;
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
	int deferred_nr;
	int shift_nr;
};

#endif /* __LINUX_PLATFORM_DATA_MLXREG_H */
