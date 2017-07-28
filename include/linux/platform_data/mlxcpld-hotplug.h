/*
 * include/linux/platform_data/mlxcpld-hotplug.h
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Vadim Pasternak <vadimp@mellanox.com>
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

#ifndef __LINUX_PLATFORM_DATA_MLXCPLD_HOTPLUG_H
#define __LINUX_PLATFORM_DATA_MLXCPLD_HOTPLUG_H

/**
 * struct mlxcpld_hotplug_device - I2C device data:
 * @adapter: I2C device adapter;
 * @client: I2C device client;
 * @brdinfo: device board information;
 * @bus: I2C bus, where device is attached;
 *
 * Structure represents I2C hotplug device static data (board topology) and
 * dynamic data (related kernel objects handles).
 */
struct mlxcpld_hotplug_device {
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	struct i2c_board_info brdinfo;
	u16 bus;
};

/**
 * struct mlxcpld_hotplug_platform_data - device platform data:
 * @top_aggr_offset: offset of top aggregation interrupt register;
 * @top_aggr_mask: top aggregation interrupt common mask;
 * @top_aggr_psu_mask: top aggregation interrupt PSU mask;
 * @psu_reg_offset: offset of PSU interrupt register;
 * @psu_mask: PSU interrupt mask;
 * @psu_count: number of equipped replaceable PSUs;
 * @psu: pointer to PSU devices data array;
 * @top_aggr_pwr_mask: top aggregation interrupt power mask;
 * @pwr_reg_offset: offset of power interrupt register
 * @pwr_mask: power interrupt mask;
 * @pwr_count: number of power sources;
 * @pwr: pointer to power devices data array;
 * @top_aggr_fan_mask: top aggregation interrupt FAN mask;
 * @fan_reg_offset: offset of FAN interrupt register;
 * @fan_mask: FAN interrupt mask;
 * @fan_count: number of equipped replaceable FANs;
 * @fan: pointer to FAN devices data array;
 *
 * Structure represents board platform data, related to system hotplug events,
 * like FAN, PSU, power cable insertion and removing. This data provides the
 * number of hot-pluggable devices and hardware description for event handling.
 */
struct mlxcpld_hotplug_platform_data {
	u16 top_aggr_offset;
	u8 top_aggr_mask;
	u8 top_aggr_psu_mask;
	u16 psu_reg_offset;
	u8 psu_mask;
	u8 psu_count;
	struct mlxcpld_hotplug_device *psu;
	u8 top_aggr_pwr_mask;
	u16 pwr_reg_offset;
	u8 pwr_mask;
	u8 pwr_count;
	struct mlxcpld_hotplug_device *pwr;
	u8 top_aggr_fan_mask;
	u16 fan_reg_offset;
	u8 fan_mask;
	u8 fan_count;
	struct mlxcpld_hotplug_device *fan;
};

#endif /* __LINUX_PLATFORM_DATA_MLXCPLD_HOTPLUG_H */
