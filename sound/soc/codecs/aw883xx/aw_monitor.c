// SPDX-License-Identifier: GPL-2.0+
/*
 * awinic_monitor.c monitor_module
 *
 * Version: v0.1.17
 *
 * Copyright (c) 2019 AWINIC Technology CO., LTD
 *
 *  Author: Nick Li <liweilei@awinic.com.cn>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/of.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/power_supply.h>
#include "aw_monitor.h"
#include "aw_log.h"
#include "aw_device.h"
#include "aw883xx.h"
#include "aw_calib.h"


#define AW883XX_MONITOR_NAME "aw883xx_monitor.bin"

/*****************************************************
 * device monitor
 *****************************************************/
 #ifndef AW_SYS_BATTERY_ST
static int aw_monitor_get_chip_voltage(struct aw_device *aw_dev,
						unsigned int *vol)
{
	int ret = -1;
	uint16_t local_vol = 0;
	struct aw_voltage_desc *desc = &aw_dev->voltage_desc;

	ret = aw_dev->ops.aw_reg_read(aw_dev, desc->reg, (uint16_t *)vol);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "read voltage failed!");
		return ret;
	}

	if (desc->int_bit == 0) {
		aw_dev_err(aw_dev->dev, "desc->int_bit:%d unsupported", desc->int_bit);
		return -EINVAL;
	}

	local_vol = ((*vol) * desc->vbat_range) / desc->int_bit;

	*vol = local_vol;

	aw_dev_info(aw_dev->dev, "chip voltage is %d", *vol);

	return 0;
}

static int aw_monitor_get_chip_temperature(struct aw_device *aw_dev, int *temp)
{
	int ret = -1;
	uint16_t reg_val = 0;
	uint16_t local_temp;
	struct aw_temperature_desc *desc = &aw_dev->temp_desc;

	ret = aw_dev->ops.aw_reg_read(aw_dev, desc->reg, &reg_val);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "get temperature failed!");
		return ret;
	}

	local_temp = reg_val;

	if (local_temp & (~desc->sign_mask))
		local_temp = local_temp | desc->neg_mask;

	*temp = (int)((int16_t)local_temp);

	aw_dev_info(aw_dev->dev, "chip temperature = %d", *temp);
	return 0;
}
#endif

static int aw_monitor_get_battery_state(struct aw_device *aw_dev,
			int *data, int data_type)
{
	char name[] = "battery";
	int ret = -1;
	union power_supply_propval prop = { 0 };
	struct power_supply *psy = NULL;

	psy = power_supply_get_by_name(name);
	if (psy) {
		ret = power_supply_get_property(psy, data_type, &prop);
		if (ret < 0) {
			aw_dev_err(aw_dev->dev, "get data failed");
			return -EINVAL;
		}
		*data = prop.intval;
		aw_dev_dbg(aw_dev->dev, "get system data: %d", *data);
	} else {
		aw_dev_err(aw_dev->dev, "no struct power supply name : %s", name);
		return -EINVAL;
	}

	return 0;
}

static int aw_monitor_get_system_temperature(struct aw_device *aw_dev, int *temp)
{
	int ret = -1;
	int sys_temp;

	ret = aw_monitor_get_battery_state(aw_dev, &sys_temp, POWER_SUPPLY_PROP_TEMP);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "get system temperature failed!");
		return ret;
	}

	*temp = sys_temp / 10;
	aw_dev_dbg(aw_dev->dev, "system temperature = %d", *temp);

	return ret;
}

static int aw_monitor_set_system_temperature(struct aw_device *aw_dev, int sys_temp)
{
	int ret = -1;
	struct aw_hw_temp_desc *hw_temp_desc = &aw_dev->hw_temp_desc;

	ret = aw_dev->ops.aw_dsp_write(aw_dev, hw_temp_desc->dsp_reg,
				sys_temp, hw_temp_desc->data_type);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "set system temperature failed!");
		return ret;
	}

	return ret;
}

#ifdef AW_SYS_BATTERY_ST
static int aw_monitor_get_system_voltage(struct aw_device *aw_dev, unsigned int *vol)
{
	int ret = -1;
	unsigned int sys_vol;

	ret = aw_monitor_get_battery_state(aw_dev, &sys_vol, POWER_SUPPLY_PROP_VOLTAGE_NOW);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "get system voltage failed!");
		return ret;
	}

	*vol = sys_vol / 1000;
	aw_dev_dbg(aw_dev->dev, "system voltage = %d", *vol);

	return ret;
}
#endif

static int aw_monitor_get_temperature(struct aw_device *aw_dev, int *temp)
{
#ifdef AW_SYS_BATTERY_ST
	return aw_monitor_get_system_temperature(aw_dev, temp);
#else
	return aw_monitor_get_chip_temperature(aw_dev, temp);
#endif
}

static int aw_monitor_get_voltage(struct aw_device *aw_dev, unsigned int *vol)
{
#ifdef AW_SYS_BATTERY_ST
	return aw_monitor_get_system_voltage(aw_dev, vol);
#else
	return aw_monitor_get_chip_voltage(aw_dev, vol);
#endif
}

static int aw_monitor_get_temp_and_vol(struct aw_device *aw_dev)
{
	struct aw_monitor_desc *monitor = &aw_dev->monitor_desc;
	unsigned int voltage = 0;
	int current_temp = 0;
	int ret = -1;

#ifdef AW_DEBUG
	if (monitor->test_vol == 0) {
		ret = aw_monitor_get_voltage(aw_dev, &voltage);
		if (ret < 0)
			return ret;
	} else {
		voltage = monitor->test_vol;
	}

	if (monitor->test_temp == 0) {
		ret = aw_monitor_get_temperature(aw_dev, &current_temp);
		if (ret < 0)
			return ret;
	} else {
		current_temp = monitor->test_temp;
	}
#else
	ret = aw_monitor_get_voltage(aw_dev, &voltage);
	if (ret < 0)
		return ret;

	ret = aw_monitor_get_temperature(aw_dev, &current_temp);
	if (ret < 0)
		return ret;
#endif

	monitor->vol_trace.sum_val += voltage;
	monitor->temp_trace.sum_val += current_temp;
	monitor->samp_count++;

	return 0;
}

static int aw_monitor_first_get_data_form_table(struct aw_device *aw_dev,
				struct aw_table_info table_info,
			struct aw_monitor_trace *data_trace)
{
	int i;

	if (table_info.aw_table == NULL) {
		aw_dev_err(aw_dev->dev, "table_info.aw_table is null");
		return -EINVAL;
	}

	for (i = 0; i < table_info.table_num; i++) {
		if (data_trace->sum_val >= table_info.aw_table[i].min_val) {
			memcpy(&data_trace->aw_table, &table_info.aw_table[i],
				sizeof(struct aw_table));
			break;
		}
	}
	return 0;
}

static int aw_monitor_trace_data_from_table(struct aw_device *aw_dev,
			struct aw_table_info table_info,
			struct aw_monitor_trace *data_trace)
{
	int i;

	if (table_info.aw_table == NULL) {
		aw_dev_err(aw_dev->dev, "table_info.aw_table is null");
		return -EINVAL;
	}

	for (i = 0; i < table_info.table_num; i++) {
		if (data_trace->sum_val >= table_info.aw_table[i].min_val &&
			data_trace->sum_val <= table_info.aw_table[i].max_val) {
			memcpy(&data_trace->aw_table, &table_info.aw_table[i],
				sizeof(struct aw_table));
			break;
		}
	}

	return 0;
}

static int aw_monitor_get_data_from_table(struct aw_device *aw_dev,
					struct aw_table_info table_info,
					struct aw_monitor_trace *data_trace,
					uint32_t aplha)
{
	struct aw_monitor_desc *monitor = &aw_dev->monitor_desc;

	if (monitor->first_entry == AW_FIRST_ENTRY) {
		return aw_monitor_first_get_data_form_table(aw_dev,
						table_info, data_trace);
	} else {
		if (monitor->samp_count == 0) {
			aw_dev_err(aw_dev->dev, "monitor->samp_count:%d unsupported", monitor->samp_count);
			return -EINVAL;
		}

		data_trace->sum_val = data_trace->sum_val / monitor->samp_count;
		data_trace->sum_val = ((int32_t)aplha * data_trace->sum_val +
			(1000 - (int32_t)aplha) * data_trace->pre_val) / 1000;
		return aw_monitor_trace_data_from_table(aw_dev,
						table_info, data_trace);
	}

	return 0;
}

static int aw_monitor_get_data(struct aw_device *aw_dev)
{
	struct aw_monitor_desc *monitor = &aw_dev->monitor_desc;
	struct aw_monitor_cfg *monitor_cfg = &monitor->monitor_cfg;
	struct aw_monitor_trace *vol_trace = &monitor->vol_trace;
	struct aw_monitor_trace *temp_trace = &monitor->temp_trace;
	int ret;

	if (monitor_cfg->vol_switch) {
		ret = aw_monitor_get_data_from_table(aw_dev,
			monitor_cfg->vol_info, vol_trace,
			monitor_cfg->vol_aplha);
		if (ret < 0)
			return ret;
	} else {
		vol_trace->aw_table.ipeak = IPEAK_NONE;
		vol_trace->aw_table.gain = GAIN_NONE;
		vol_trace->aw_table.vmax = VMAX_NONE;
	}

	if (monitor_cfg->temp_switch) {
		ret = aw_monitor_get_data_from_table(aw_dev,
			monitor_cfg->temp_info, temp_trace,
			monitor_cfg->temp_aplha);
		if (ret < 0)
			return ret;
	} else {
		temp_trace->aw_table.ipeak = IPEAK_NONE;
		temp_trace->aw_table.gain = GAIN_NONE;
		temp_trace->aw_table.vmax = VMAX_NONE;
	}

	aw_dev_dbg(aw_dev->dev,
			"filter_vol:%d, vol: ipeak = 0x%x, gain = 0x%x, vmax = 0x%x",
			monitor->vol_trace.sum_val, vol_trace->aw_table.ipeak,
			vol_trace->aw_table.gain, vol_trace->aw_table.vmax);

	aw_dev_dbg(aw_dev->dev,
			"filter_temp:%d, temp: ipeak = 0x%x, gain = 0x%x, vmax = 0x%x",
			monitor->temp_trace.sum_val, temp_trace->aw_table.ipeak,
			temp_trace->aw_table.gain, temp_trace->aw_table.vmax);
	return 0;
}

static void aw_monitor_get_cfg(struct aw_device *aw_dev,
					struct aw_table *set_table)
{
	struct aw_monitor_desc *monitor = &aw_dev->monitor_desc;
	struct aw_table *temp_data = &monitor->temp_trace.aw_table;
	struct aw_table *vol_data = &monitor->vol_trace.aw_table;

	if (temp_data->ipeak == IPEAK_NONE && vol_data->ipeak == IPEAK_NONE) {
		memcpy(set_table, temp_data, sizeof(struct aw_table));
	} else if (temp_data->ipeak == IPEAK_NONE) {
		memcpy(set_table, vol_data, sizeof(struct aw_table));
	} else if (vol_data->ipeak == IPEAK_NONE) {
		memcpy(set_table, temp_data, sizeof(struct aw_table));
	} else {
		if (monitor->monitor_cfg.logic_switch == AW_MON_LOGIC_OR) {
			set_table->ipeak = (temp_data->ipeak < vol_data->ipeak ?
					temp_data->ipeak : vol_data->ipeak);
			set_table->gain = (temp_data->gain < vol_data->gain ?
					vol_data->gain : temp_data->gain);
			set_table->vmax = (temp_data->vmax < vol_data->vmax ?
					vol_data->vmax : temp_data->vmax);
		} else {
			set_table->ipeak = (temp_data->ipeak < vol_data->ipeak ?
				vol_data->ipeak : temp_data->ipeak);
			set_table->gain = (temp_data->gain < vol_data->gain ?
					temp_data->gain : vol_data->gain);
			set_table->vmax = (temp_data->vmax < vol_data->vmax ?
					temp_data->vmax : vol_data->vmax);
		}
	}
}

static void aw_monitor_set_ipeak(struct aw_device *aw_dev,
				uint16_t ipeak)
{
	struct aw_monitor_cfg *monitor_cfg = &aw_dev->monitor_desc.monitor_cfg;
	uint16_t reg_val = 0;
	uint16_t read_reg_val;
	int ret;
	struct aw_ipeak_desc *desc = &aw_dev->ipeak_desc;

	if (ipeak == IPEAK_NONE || (!monitor_cfg->ipeak_switch))
		return;

	ret = aw_dev->ops.aw_reg_read(aw_dev, desc->reg, &reg_val);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "read ipeak failed");
		return;
	}

	read_reg_val = reg_val;
	read_reg_val &= (~desc->mask);

	if (read_reg_val == ipeak) {
		aw_dev_dbg(aw_dev->dev, "ipeak = 0x%x, no change",
					read_reg_val);
		return;
	}
	reg_val &= desc->mask;
	read_reg_val = ipeak;
	reg_val |= read_reg_val;

	ret = aw_dev->ops.aw_reg_write(aw_dev, desc->reg, reg_val);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "write ipeak failed");
		return;
	}
	aw_dev_info(aw_dev->dev, "set reg val = 0x%x, ipeak = 0x%x",
				reg_val, ipeak);

}

static void aw_monitor_set_gain(struct aw_device *aw_dev, uint16_t gain)
{
	struct aw_monitor_cfg *monitor_cfg = &aw_dev->monitor_desc.monitor_cfg;
	uint16_t read_volume;
	uint16_t set_volume;
	int ret;

	if (gain == GAIN_NONE || (!monitor_cfg->gain_switch))
		return;

	ret = aw_dev->ops.aw_get_volume(aw_dev, &read_volume);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "read volume failed");
		return;
	}

	gain = aw_dev->ops.aw_reg_val_to_db(gain);

	/*add offset*/
	set_volume = gain + aw_dev->volume_desc.init_volume;

	if (read_volume == set_volume) {
		aw_dev_dbg(aw_dev->dev, "gain = 0x%x, no change", read_volume);
		return;
	}

	ret = aw_dev->ops.aw_set_volume(aw_dev, set_volume);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "set gain failed");
		return;
	}
	aw_dev_info(aw_dev->dev, "set reg val = 0x%x, gain = 0x%x",
				set_volume, gain);

}

static int aw_monitor_vmax_check(struct aw_device *aw_dev)
{
	int ret = -1;

	ret = aw_dev_syspll_check(aw_dev);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "no iis signal");
		return ret;
	}

	ret = aw_dev_get_dsp_status(aw_dev);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "dsp not work");
		return ret;
	}

	return 0;
}

static void aw_monitor_set_vmax(struct aw_device *aw_dev,
						uint32_t vmax)
{
	struct aw_monitor_cfg *monitor_cfg = &aw_dev->monitor_desc.monitor_cfg;
	struct aw_vmax_desc *desc = &aw_dev->vmax_desc;
	uint16_t vmax_set;
	int ret = -1;

	if (vmax == VMAX_NONE || (!monitor_cfg->vmax_switch))
		return;

	ret = aw_monitor_vmax_check(aw_dev);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "vmax_check fail, ret=%d", ret);
		return;
	}

	if ((vmax == aw_dev->monitor_desc.pre_vmax) &&
		(aw_dev->monitor_desc.first_entry != AW_FIRST_ENTRY)) {
		aw_dev_dbg(aw_dev->dev, "vmax = 0x%x, no change", vmax);
		return;
	}

	vmax_set = (uint16_t)((int16_t)vmax +
		(int16_t)aw_dev->vmax_desc.init_vmax);


	ret = aw_dev->ops.aw_dsp_write(aw_dev,
		desc->dsp_reg, vmax_set, desc->data_type);
	if (ret < 0)
		return;

	aw_dev_info(aw_dev->dev, "get monitor vmax = 0x%x, set vmax = 0x%x",
		vmax, vmax_set);
}

static void aw_monitor_check_sysint(struct aw_device *aw_dev)
{
	int ret = -1;
	uint16_t sysint;
	struct aw_int_desc *desc = &aw_dev->int_desc;

	ret = aw_dev_get_int_status(aw_dev, &sysint);
	if (ret < 0)
		aw_dev_err(aw_dev->dev, "get_sysint fail, ret=%d", ret);

	if (sysint & (desc->intst_mask)) {
		desc->sysint_st = sysint;
		aw_dev_err(aw_dev->dev, "check sysint fail, reg=0x%04x", sysint);
	}
}

static int aw_monitor_work(struct aw_device *aw_dev)
{
	struct aw_monitor_desc *monitor = &aw_dev->monitor_desc;
	struct aw_monitor_cfg *monitor_cfg = &monitor->monitor_cfg;
	struct aw_table set_table;
	int ret = -1;

	if (aw_cali_svc_get_cali_status(&aw_dev->cali_desc)) {
		aw_dev_info(aw_dev->dev, "done nothing during calibration");
		return 0;
	}

	ret = aw_monitor_get_temp_and_vol(aw_dev);
	if (ret < 0)
		return ret;

	if (monitor->samp_count < monitor_cfg->monitor_count &&
		(monitor->first_entry == AW_NOT_FIRST_ENTRY))
		return 0;

	ret = aw_monitor_get_data(aw_dev);
	if (ret < 0)
		return ret;

	aw_monitor_get_cfg(aw_dev, &set_table);

	aw_dev_dbg(aw_dev->dev,
		"set_ipeak = 0x%x, set_gain = 0x%x, set_vmax = 0x%x",
		set_table.ipeak, set_table.gain, set_table.vmax);

	aw_monitor_set_ipeak(aw_dev, set_table.ipeak);

	aw_monitor_set_gain(aw_dev, set_table.gain);

	aw_monitor_set_vmax(aw_dev, set_table.vmax);

	aw_monitor_check_sysint(aw_dev);

	monitor->samp_count = 0;
	monitor->temp_trace.pre_val = monitor->temp_trace.sum_val;
	monitor->temp_trace.sum_val = 0;

	monitor->vol_trace.pre_val = monitor->vol_trace.sum_val;
	monitor->vol_trace.sum_val = 0;

	if (monitor->first_entry == AW_FIRST_ENTRY)
		monitor->first_entry = AW_NOT_FIRST_ENTRY;

	return 0;
}

static void aw_monitor_work_func(struct work_struct *work)
{
	struct aw_device *aw_dev  = container_of(work,
		struct aw_device, monitor_desc.delay_work.work);
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;
	struct aw_monitor_cfg *monitor_cfg = &aw_dev->monitor_desc.monitor_cfg;
	struct aw_monitor_desc *monitor = &aw_dev->monitor_desc;

	aw_dev_dbg(aw_dev->dev, "scene_mode %d,monitor_status:%d, monitor_switch:%d",
		aw_dev->cur_prof, monitor_cfg->monitor_status,
		monitor_cfg->monitor_switch);

	if ((monitor_cfg->monitor_status == AW_MON_CFG_OK) &&
		monitor_cfg->monitor_switch) {

		if (!aw_dev_get_hmute(aw_dev)) {
			aw_monitor_work(aw_dev);
			queue_delayed_work(aw883xx->work_queue,
				&monitor->delay_work,
				msecs_to_jiffies(monitor_cfg->monitor_time));
		}

	}
}

static int aw_hw_monitor_work(struct aw_device *aw_dev)
{
	int32_t sys_temp = 0;

	aw_monitor_get_system_temperature(aw_dev, &sys_temp);

	aw_monitor_set_system_temperature(aw_dev, sys_temp);

	return 0;
}

static void aw_hw_monitor_work_func(struct work_struct *work)
{
	struct aw_device *aw_dev  = container_of(work,
		struct aw_device, monitor_desc.hw_monitor_work.work);
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;
	struct aw_monitor_desc *monitor = &aw_dev->monitor_desc;

	if (!aw_dev_get_hmute(aw_dev)) {
		aw_hw_monitor_work(aw_dev);
		queue_delayed_work(aw883xx->work_queue,
					&monitor->hw_monitor_work,
					msecs_to_jiffies(monitor->hw_monitor_delay));
	}
}

void aw_monitor_start(struct aw_monitor_desc *monitor_desc)
{
	uint8_t temp_flag = monitor_desc->hw_temp_flag;
	struct aw_device *aw_dev = container_of(monitor_desc,
			struct aw_device, monitor_desc);
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;

	aw_dev_info(aw_dev->dev, "enter");

	if (aw_dev->profctrl_desc.cur_mode == AW_RCV_MODE) {
		aw_dev_dbg(aw_dev->dev, "receiver mode no need to start monitor");
		return;
	}

	if (monitor_desc->hw_mon_en) {
		aw_dev_info(aw_dev->dev, "hardware monitor is enable");
		if (temp_flag == AW_EXTERNAL_TEMP) {
			queue_delayed_work(aw883xx->work_queue,
					&monitor_desc->hw_monitor_work, 0);
		} else {
			aw_dev_dbg(aw_dev->dev, "Internal chip temperature used");
		}
		return;
	}

	monitor_desc->first_entry = AW_FIRST_ENTRY;
	monitor_desc->samp_count = 0;
	monitor_desc->vol_trace.sum_val = 0;
	monitor_desc->temp_trace.sum_val = 0;

	queue_delayed_work(aw883xx->work_queue,
				&monitor_desc->delay_work, 0);
}

int aw_monitor_stop(struct aw_monitor_desc *monitor_desc)
{
	struct aw_device *aw_dev = container_of(monitor_desc,
			struct aw_device, monitor_desc);

	aw_dev_info(aw_dev->dev, "enter");

	cancel_delayed_work_sync(&monitor_desc->delay_work);
	cancel_delayed_work_sync(&monitor_desc->hw_monitor_work);

	return 0;

}

/*****************************************************
* load monitor config
*****************************************************/
static int aw_monitor_param_check_sum(struct aw_device *aw_dev,
					uint8_t *data, uint32_t data_len)
{
	int i, check_sum = 0;
	struct aw_monitor_hdr_v_0_1_1 *monitor_hdr =
		(struct aw_monitor_hdr_v_0_1_1 *)data;

	if (data_len < sizeof(struct aw_monitor_hdr_v_0_1_1)) {
		aw_dev_err(aw_dev->dev,
			"data size smaller than hdr , please check monitor bin");
		return -ENOMEM;
	}

	for (i = 4 ; i < data_len; i++)
		check_sum += (uint8_t)data[i];

	if (monitor_hdr->check_sum != check_sum) {
		aw_dev_err(aw_dev->dev,
			"check_sum[%d] is not equal to actual check_sum[%d]",
				monitor_hdr->check_sum, check_sum);
		return -ENOMEM;
	}

	return 0;
}

static void aw_monitor_write_data_to_table(struct aw_device *aw_dev,
		struct aw_table_info *table_info, const char *offset_ptr)
{
	int i;

	for (i = 0; i < table_info->table_num * AW_TABLE_SIZE; i += AW_TABLE_SIZE) {
		table_info->aw_table[i / AW_TABLE_SIZE].min_val =
			AW_GET_16_DATA(offset_ptr[1 + i], offset_ptr[i]);
		table_info->aw_table[i / AW_TABLE_SIZE].max_val =
			AW_GET_16_DATA(offset_ptr[3 + i], offset_ptr[2 + i]);
		table_info->aw_table[i / AW_TABLE_SIZE].ipeak =
			AW_GET_16_DATA(offset_ptr[5 + i], offset_ptr[4 + i]);
		table_info->aw_table[i / AW_TABLE_SIZE].gain =
			AW_GET_16_DATA(offset_ptr[7 + i], offset_ptr[6 + i]);
		table_info->aw_table[i / AW_TABLE_SIZE].vmax =
			AW_GET_32_DATA(offset_ptr[11 + i], offset_ptr[10 + i],
				offset_ptr[9 + i], offset_ptr[8 + i]);
	}

	for (i = 0; i < table_info->table_num; i++)
		aw_dev_info(aw_dev->dev,
			"min_val:%d, max_val:%d, ipeak:0x%x, gain:0x%x, vmax:0x%x",
			table_info->aw_table[i].min_val,
			table_info->aw_table[i].max_val,
			table_info->aw_table[i].ipeak,
			table_info->aw_table[i].gain,
			table_info->aw_table[i].vmax);

}

static int aw_monitor_parse_vol_data_v_0_1_1(struct aw_device *aw_dev, uint8_t *data)
{
	struct aw_monitor_hdr_v_0_1_1 *monitor_hdr =
			(struct aw_monitor_hdr_v_0_1_1 *)data;
	struct aw_table_info *vol_info =
		&aw_dev->monitor_desc.monitor_cfg.vol_info;

	aw_dev_info(aw_dev->dev, "===parse vol start ===");
	if (vol_info->aw_table != NULL) {
		devm_kfree(aw_dev->dev, vol_info->aw_table);
		vol_info->aw_table = NULL;
	}

	vol_info->aw_table = devm_kzalloc(aw_dev->dev,
					(monitor_hdr->vol_num * AW_TABLE_SIZE),
					GFP_KERNEL);
	if (vol_info->aw_table == NULL)
		return -ENOMEM;

	vol_info->table_num = monitor_hdr->vol_num;
	aw_monitor_write_data_to_table(aw_dev, vol_info,
		&data[monitor_hdr->vol_offset]);
	aw_dev_info(aw_dev->dev, "===parse vol end ===");
	return 0;
}

static int aw_monitor_parse_temp_data_v_0_1_1(struct aw_device *aw_dev, uint8_t *data)
{
	struct aw_monitor_hdr_v_0_1_1 *monitor_hdr =
			(struct aw_monitor_hdr_v_0_1_1 *)data;
	struct aw_table_info *temp_info =
		&aw_dev->monitor_desc.monitor_cfg.temp_info;

	aw_dev_info(aw_dev->dev, "===parse temp start ===");

	if (temp_info->aw_table != NULL) {
		devm_kfree(aw_dev->dev, temp_info->aw_table);
		temp_info->aw_table = NULL;
	}

	temp_info->aw_table = devm_kzalloc(aw_dev->dev,
					(monitor_hdr->temp_num * AW_TABLE_SIZE),
					GFP_KERNEL);
	if (temp_info->aw_table == NULL)
		return -ENOMEM;

	temp_info->table_num = monitor_hdr->temp_num;
	aw_monitor_write_data_to_table(aw_dev, temp_info,
		&data[monitor_hdr->temp_offset]);
	aw_dev_info(aw_dev->dev, "===parse temp end ===");
	return 0;
}

static void aw_monitor_parse_hdr_v_0_1_1(struct aw_device *aw_dev, uint8_t *data)
{
	struct aw_monitor_hdr_v_0_1_1 *monitor_hdr =
			(struct aw_monitor_hdr_v_0_1_1 *)data;
	struct aw_monitor_cfg *monitor_cfg = &aw_dev->monitor_desc.monitor_cfg;

	monitor_cfg->monitor_switch =
		(monitor_hdr->enable_flag >> MONITOR_EN_BIT) & MONITOR_EN_MASK;
	monitor_cfg->monitor_time = monitor_hdr->monitor_time;
	monitor_cfg->monitor_count = monitor_hdr->monitor_count;
	monitor_cfg->ipeak_switch =
		(monitor_hdr->enable_flag >> MONITOR_IPEAK_EN_BIT) & MONITOR_EN_MASK;
	monitor_cfg->logic_switch =
		(monitor_hdr->enable_flag >> MONITOR_LOGIC_BIT) & MONITOR_EN_MASK;
	monitor_cfg->gain_switch =
		(monitor_hdr->enable_flag >> MONITOR_GAIN_EN_BIT) & MONITOR_EN_MASK;
	monitor_cfg->vmax_switch =
		(monitor_hdr->enable_flag >> MONITOR_VMAX_EN_BIT) & MONITOR_EN_MASK;
	monitor_cfg->temp_switch =
		(monitor_hdr->enable_flag >> MONITOR_TEMP_EN_BIT) & MONITOR_EN_MASK;
	monitor_cfg->temp_aplha = monitor_hdr->temp_aplha;
	monitor_cfg->vol_switch =
		(monitor_hdr->enable_flag >> MONITOR_VOL_EN_BIT) & MONITOR_EN_MASK;
	monitor_cfg->vol_aplha = monitor_hdr->vol_aplha;

	aw_dev_info(aw_dev->dev, "chip name:%s",
		monitor_hdr->chip_type);
	aw_dev_info(aw_dev->dev, "ui ver:0x%x",
		monitor_hdr->ui_ver);

	aw_dev_info(aw_dev->dev,
		"monitor_switch:%d, monitor_time:%d (ms), monitor_count:%d",
		monitor_cfg->monitor_switch, monitor_cfg->monitor_time,
		monitor_cfg->monitor_count);

	aw_dev_info(aw_dev->dev,
		"logic_switch:%d, ipeak_switch:%d, gain_switch:%d, vmax_switch:%d",
		monitor_cfg->logic_switch, monitor_cfg->ipeak_switch,
		monitor_cfg->gain_switch, monitor_cfg->vmax_switch);

	aw_dev_info(aw_dev->dev,
		"temp_switch:%d, temp_aplha:%d, vol_switch:%d, vol_aplha:%d",
		monitor_cfg->temp_switch, monitor_cfg->temp_aplha,
		monitor_cfg->vol_switch, monitor_cfg->vol_aplha);
}

static int aw_monitor_check_fw_v_0_1_1(struct aw_device *aw_dev,
					uint8_t *data, uint32_t data_len)
{
	struct aw_monitor_hdr_v_0_1_1 *monitor_hdr =
				(struct aw_monitor_hdr_v_0_1_1 *)data;
	int temp_size, vol_size;

	if (data_len < sizeof(struct aw_monitor_hdr_v_0_1_1)) {
		aw_dev_err(aw_dev->dev,
			"params size[%d] < struct aw_monitor_hdr size[%d]!",
			data_len, (int)sizeof(struct aw_monitor_hdr_v_0_1_1));
		return -ENOMEM;
	}

	if (monitor_hdr->temp_offset > data_len) {
		aw_dev_err(aw_dev->dev, "temp_offset[%d] overflow file size[%d]!",
			monitor_hdr->temp_offset, data_len);
		return -ENOMEM;
	}

	if (monitor_hdr->vol_offset > data_len) {
		aw_dev_err(aw_dev->dev, "vol_offset[%d] overflow file size[%d]!",
			monitor_hdr->vol_offset, data_len);
		return -ENOMEM;
	}

	temp_size = monitor_hdr->temp_num * monitor_hdr->single_temp_size;
	if (temp_size > data_len) {
		aw_dev_err(aw_dev->dev, "temp_size:[%d] overflow file size[%d]!",
			temp_size, data_len);
		return -ENOMEM;
	}

	vol_size = monitor_hdr->vol_num * monitor_hdr->single_vol_size;
	if (vol_size > data_len) {
		aw_dev_err(aw_dev->dev, "vol_size:[%d] overflow file size[%d]!",
			vol_size, data_len);
		return -ENOMEM;
	}

	return 0;
}

static int aw_monitor_parse_data_v_0_1_1(struct aw_device *aw_dev,
				uint8_t *data, uint32_t data_len)
{
	int ret;
	struct aw_monitor_cfg *monitor_cfg = &aw_dev->monitor_desc.monitor_cfg;

	ret = aw_monitor_check_fw_v_0_1_1(aw_dev, data, data_len);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "check monitor failed");
		return ret;
	}

	aw_monitor_parse_hdr_v_0_1_1(aw_dev, data);

	ret = aw_monitor_parse_temp_data_v_0_1_1(aw_dev, data);
	if (ret < 0)
		return ret;

	ret = aw_monitor_parse_vol_data_v_0_1_1(aw_dev, data);
	if (ret < 0) {
		if (monitor_cfg->temp_info.aw_table != NULL) {
			devm_kfree(aw_dev->dev, monitor_cfg->temp_info.aw_table);
			monitor_cfg->temp_info.aw_table = NULL;
			monitor_cfg->temp_info.table_num = 0;
		}
		return ret;
	}

	monitor_cfg->monitor_status = AW_MON_CFG_OK;
	return 0;
}

int aw_monitor_parse_fw(struct aw_monitor_desc *monitor_desc,
				uint8_t *data, uint32_t data_len)
{
	struct aw_monitor_hdr_v_0_1_1 *monitor_hdr = NULL;
	struct aw_device *aw_dev = NULL;
	int ret;

	if (monitor_desc == NULL || data == NULL) {
		pr_err("monitor_desc or data is NULL");
		return -EINVAL;
	}

	monitor_hdr = (struct aw_monitor_hdr_v_0_1_1 *)data;
	aw_dev = container_of(monitor_desc,
			struct aw_device, monitor_desc);

	ret = aw_monitor_param_check_sum(aw_dev, data, data_len);
	if (ret < 0)
		return ret;

	switch (monitor_hdr->monitor_ver) {
	case AW_MONITOR_HDR_VER_0_1_1:
		return aw_monitor_parse_data_v_0_1_1(aw_dev, data, data_len);
	default:
		aw_dev_err(aw_dev->dev, "cfg version:0x%x unsupported",
				monitor_hdr->monitor_ver);
		return -EINVAL;
	}
}

static void aw_monitor_free_firmware(struct aw_device *aw_dev)
{
	struct aw_monitor_cfg *monitor_cfg =
				&aw_dev->monitor_desc.monitor_cfg;

	monitor_cfg->monitor_status = AW_MON_CFG_ST;

	if (monitor_cfg->temp_info.aw_table != NULL) {
		devm_kfree(aw_dev->dev, monitor_cfg->temp_info.aw_table);
		monitor_cfg->temp_info.aw_table = NULL;
	}

	if (monitor_cfg->vol_info.aw_table != NULL) {
		devm_kfree(aw_dev->dev, monitor_cfg->vol_info.aw_table);
		monitor_cfg->vol_info.aw_table = NULL;
	}

	memset(monitor_cfg, 0, sizeof(struct aw_monitor_cfg));
}

static int aw_monitor_real_time_update_monitor(struct aw_device *aw_dev)
{
	const struct firmware *cont = NULL;
	struct aw_container *aw_monitor_cnt = NULL;
	int ret;

	ret = request_firmware(&cont, AW883XX_MONITOR_NAME, aw_dev->dev);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "failed to read %s", AW883XX_MONITOR_NAME);
		release_firmware(cont);
		return ret;
	}

	aw_monitor_cnt = devm_kzalloc(aw_dev->dev,
			cont->size + sizeof(uint32_t), GFP_KERNEL);
	if (aw_monitor_cnt == NULL) {
		aw_dev_err(aw_dev->dev, "alloc failed!");
		release_firmware(cont);
		return ret;
	}

	aw_monitor_cnt->len = cont->size;
	memcpy(aw_monitor_cnt->data, cont->data, cont->size);
	release_firmware(cont);

	ret = aw_monitor_parse_fw(&aw_dev->monitor_desc,
			aw_monitor_cnt->data, aw_monitor_cnt->len);
	if (ret < 0)
		aw_dev_err(aw_dev->dev, "parse monitor firmware failed!");

	devm_kfree(aw_dev->dev, aw_monitor_cnt);
	aw_monitor_cnt = NULL;

	return ret;
}

/*****************************************************
 * monitor init
 *****************************************************/
#ifdef AW_DEBUG
static ssize_t aw_vol_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	struct aw_device *aw_dev = aw883xx->aw_pa;
	uint32_t vol = 0;
	int ret = -1;

	if (count == 0)
		return 0;

	ret = kstrtouint(buf, 0, &vol);
	if (ret < 0)
		return ret;

	aw_dev_info(aw_dev->dev, "vol set =%d", vol);
	aw_dev->monitor_desc.test_vol = vol;

	return count;
}

static ssize_t aw_vol_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	struct aw_device *aw_dev = aw883xx->aw_pa;
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
		"vol: %d\n",
		aw_dev->monitor_desc.test_vol);
	return len;
}

static ssize_t aw_temp_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	struct aw_device *aw_dev = aw883xx->aw_pa;
	int32_t temp = 0;
	int ret = -1;

	if (count == 0)
		return 0;

	ret = kstrtoint(buf, 0, &temp);
	if (ret < 0)
		return ret;

	aw_dev_info(aw_dev->dev, "temp set =%d", temp);

	aw_dev->monitor_desc.test_temp = temp;

	return count;
}

static ssize_t aw_temp_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	struct aw_device *aw_dev = aw883xx->aw_pa;
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
		"aw883xx temp: %d\n",
		aw_dev->monitor_desc.test_temp);

	return len;
}

static DEVICE_ATTR(vol, S_IWUSR | S_IRUGO,
	aw_vol_show, aw_vol_store);
static DEVICE_ATTR(temp, S_IWUSR | S_IRUGO,
	aw_temp_show, aw_temp_store);
#endif

static ssize_t aw_monitor_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	struct aw_device *aw_dev = aw883xx->aw_pa;
	uint32_t enable = 0;
	int ret = -1;

	if (count == 0)
		return 0;

	ret = kstrtouint(buf, 0, &enable);
	if (ret < 0)
		return ret;

	aw_dev_info(aw_dev->dev, "monitor enable set =%d", enable);

	if (aw_dev->monitor_desc.monitor_cfg.monitor_switch == enable) {
		return count;
	} else {
		aw_dev->monitor_desc.monitor_cfg.monitor_switch = enable;
		if (enable)
			aw_monitor_start(&aw_dev->monitor_desc);
	}

	return count;
}

static ssize_t aw_monitor_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	struct aw_device *aw_dev = aw883xx->aw_pa;
	ssize_t len = 0;


	len += snprintf(buf+len, PAGE_SIZE-len,
		"aw883xx monitor_flag=%u\n",
		aw_dev->monitor_desc.monitor_cfg.monitor_switch);
	return len;
}

static ssize_t aw_monitor_update_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	struct aw_device *aw_dev = aw883xx->aw_pa;

	uint32_t update = 0;
	int ret = -1;

	if (count == 0)
		return 0;

	ret = kstrtouint(buf, 0, &update);
	if (ret < 0)
		return ret;

	aw_dev_info(aw_dev->dev, "monitor update = %d", update);

	if (update) {
		aw_monitor_stop(&aw_dev->monitor_desc);
		aw_monitor_free_firmware(aw_dev);
		ret = aw_monitor_real_time_update_monitor(aw_dev);
		if (ret < 0)
			return ret;
		aw_monitor_start(&aw_dev->monitor_desc);
	}

	return count;
}

static DEVICE_ATTR(monitor, S_IWUSR | S_IRUGO,
	aw_monitor_show, aw_monitor_store);
static DEVICE_ATTR(monitor_update, S_IWUSR,
	NULL, aw_monitor_update_store);


static struct attribute *aw_monitor_attr[] = {
	&dev_attr_monitor.attr,
	&dev_attr_monitor_update.attr,
#ifdef AW_DEBUG
	&dev_attr_vol.attr,
	&dev_attr_temp.attr,
#endif
	NULL
};

static struct attribute_group aw_monitor_attr_group = {
	.attrs = aw_monitor_attr,
};

static void aw_monitor_parse_dt(struct aw_device *aw_dev)
{
	int ret = -1;
	uint32_t monitor_delay;
	struct device_node *np = aw_dev->dev->of_node;

	ret = of_property_read_u32(np, "hw-monitor-delay", &monitor_delay);
	if (ret < 0) {
		aw_dev_info(aw_dev->dev,
			"read hw-monitor-delay failed, set deafult value:[%d]ms",
			aw_dev->monitor_desc.hw_monitor_delay);
	} else {
		aw_dev_info(aw_dev->dev,
			"parse hw-monitor-delay:[%d]", monitor_delay);
		aw_dev->monitor_desc.hw_monitor_delay = monitor_delay;
	}
}

void aw_monitor_init(struct aw_monitor_desc *monitor_desc)
{
	int ret;
	struct aw_device *aw_dev = container_of(monitor_desc,
				struct aw_device, monitor_desc);

	aw_dev_info(aw_dev->dev, "enter");

	aw_monitor_parse_dt(aw_dev);

#ifdef AW_DEBUG
	monitor_desc->test_vol = 0;
	monitor_desc->test_temp = 0;
#endif

	INIT_DELAYED_WORK(&monitor_desc->delay_work, aw_monitor_work_func);
	INIT_DELAYED_WORK(&monitor_desc->hw_monitor_work, aw_hw_monitor_work_func);

	ret = sysfs_create_group(&aw_dev->dev->kobj,
				&aw_monitor_attr_group);
	if (ret < 0)
		aw_dev_err(aw_dev->dev, "error creating sysfs attr files");
}

void aw_monitor_deinit(struct aw_monitor_desc *monitor_desc)
{
	struct aw_device *aw_dev =
		container_of(monitor_desc, struct aw_device, monitor_desc);

	aw_monitor_stop(monitor_desc);

	sysfs_remove_group(&aw_dev->dev->kobj, &aw_monitor_attr_group);
}

