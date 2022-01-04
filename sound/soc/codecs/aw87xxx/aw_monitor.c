/*
 * aw_monitor.c
 *
 * Copyright (c) 2021 AWINIC Technology CO., LTD
 *
 * Author: Barry <zhaozhongbo@awinic.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/gameport.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include "aw87xxx.h"
#include "aw_log.h"
#include "aw_monitor.h"
#include "aw_dsp.h"
#include "aw_bin_parse.h"
#include "aw_device.h"

#define AW_MONITOT_BIN_PARSE_VERSION	"V0.1.0"

#define AW_GET_32_DATA(w, x, y, z) \
	((uint32_t)((((uint8_t)w) << 24) | (((uint8_t)x) << 16) | \
	(((uint8_t)y) << 8) | ((uint8_t)z)))

/****************************************************************************
 *
 * aw87xxx monitor bin check
 *
 ****************************************************************************/
static int aw_monitor_check_header_v_1_0_0(struct device *dev,
				char *data, uint32_t data_len)
{
	int i = 0;
	struct aw_bin_header *header = (struct aw_bin_header *)data;

	if (header->bin_data_type != DATA_TYPE_MONITOR_ANALOG) {
		AW_DEV_LOGE(dev, "monitor data_type check error!");
		return -EINVAL;
	}

	if (header->bin_data_size != AW_MONITOR_HDR_DATA_SIZE) {
		AW_DEV_LOGE(dev, "monitor data_size error!");
		return -EINVAL;
	}

	if (header->data_byte_len != AW_MONITOR_HDR_DATA_BYTE_LEN) {
		AW_DEV_LOGE(dev, "monitor data_byte_len error!");
		return -EINVAL;
	}

	for (i = 0; i < AW_MONITOR_DATA_VER_MAX; i++) {
		if (header->bin_data_ver == i) {
			AW_LOGD("monitor bin_data_ver[0x%x]", i);
			break;
		}
	}
	if (i == AW_MONITOR_DATA_VER_MAX)
		return -EINVAL;

	return 0;
}

static int aw_monitor_check_data_v1_size(struct device *dev,
				char *data, int32_t data_len)
{
	int32_t bin_header_len  = sizeof(struct aw_bin_header);
	int32_t monitor_header_len = sizeof(struct aw_monitor_header);
	int32_t monitor_data_len = sizeof(struct vmax_step_config);
	int32_t len = 0;
	struct aw_monitor_header *monitor_header = NULL;

	AW_DEV_LOGD(dev, "enter");

	if (data_len < bin_header_len + monitor_header_len) {
		AW_DEV_LOGE(dev, "bin len is less than aw_bin_header and monitoor_header,check failed");
		return -EINVAL;
	}

	monitor_header = (struct aw_monitor_header *)(data + bin_header_len);
	len = data_len - bin_header_len - monitor_header_len;
	if (len < monitor_header->step_count * monitor_data_len) {
		AW_DEV_LOGE(dev, "bin data len is not enough,check failed");
		return -EINVAL;
	}

	AW_DEV_LOGD(dev, "succeed");

	return 0;
}

static int aw_monitor_check_data_size(struct device *dev,
			char *data, int32_t data_len)
{
	int ret = -1;
	struct aw_bin_header *header = (struct aw_bin_header *)data;

	switch (header->bin_data_ver) {
	case AW_MONITOR_DATA_VER:
		ret = aw_monitor_check_data_v1_size(dev, data, data_len);
		if (ret < 0)
			return ret;
		break;
	default:
		AW_DEV_LOGE(dev, "bin data_ver[0x%x] non support",
			header->bin_data_ver);
		return -EINVAL;
	}

	return 0;
}


static int aw_monitor_check_bin_header(struct device *dev,
				char *data, int32_t data_len)
{
	int ret = -1;
	struct aw_bin_header *header = NULL;

	if (data_len < sizeof(struct aw_bin_header)) {
		AW_DEV_LOGE(dev, "bin len is less than aw_bin_header,check failed");
		return -EINVAL;
	}
	header = (struct aw_bin_header *)data;

	switch (header->header_ver) {
	case HEADER_VERSION_1_0_0:
		ret = aw_monitor_check_header_v_1_0_0(dev, data, data_len);
		if (ret < 0) {
			AW_DEV_LOGE(dev, "monitor bin haeder info check error!");
			return ret;
		}
		break;
	default:
		AW_DEV_LOGE(dev, "bin version[0x%x] non support",
			header->header_ver);
		return -EINVAL;
	}

	return 0;
}

static int aw_monitor_bin_check_sum(struct device *dev,
			char *data, int32_t data_len)
{
	int i, data_sum = 0;
	uint32_t *check_sum = (uint32_t *)data;

	for (i = 4; i < data_len; i++)
		data_sum += data[i];

	if (*check_sum != data_sum) {
		AW_DEV_LOGE(dev, "check_sum[%d] is not equal to data_sum[%d]",
				*check_sum, data_sum);
		return -ENOMEM;
	}

	AW_DEV_LOGD(dev, "succeed");

	return 0;
}

static int aw_monitor_bin_check(struct device *dev,
				char *monitor_data, uint32_t data_len)
{
	int ret = -1;

	if (monitor_data == NULL || data_len == 0) {
		AW_DEV_LOGE(dev, "none data to parse");
		return -EINVAL;
	}

	ret = aw_monitor_bin_check_sum(dev, monitor_data, data_len);
	if (ret < 0) {
		AW_DEV_LOGE(dev, "bin data check sum failed");
		return ret;
	}

	ret = aw_monitor_check_bin_header(dev, monitor_data, data_len);
	if (ret < 0) {
		AW_DEV_LOGE(dev, "bin data len check failed");
		return ret;
	}

	ret = aw_monitor_check_data_size(dev, monitor_data, data_len);
	if (ret < 0) {
		AW_DEV_LOGE(dev, "bin header info check failed");
		return ret;
	}

	return 0;
}

/*****************************************************************************
 *
 * aw87xxx monitor header bin parse
 *
 *****************************************************************************/
static void aw_monitor_write_to_table_v1(struct device *dev,
			struct vmax_step_config *vmax_step,
			char *vmax_data, uint32_t step_count)
{
	int i = 0;
	int index = 0;
	int vmax_step_size = (int)sizeof(struct vmax_step_config);

	for (i = 0; i < step_count; i++) {
		index = vmax_step_size * i;
		vmax_step[i].vbat_min =
			AW_GET_32_DATA(vmax_data[index + 3],
					vmax_data[index + 2],
					vmax_data[index + 1],
					vmax_data[index + 0]);
		vmax_step[i].vbat_max =
			AW_GET_32_DATA(vmax_data[index + 7],
					vmax_data[index + 6],
					vmax_data[index + 5],
					vmax_data[index + 4]);
		vmax_step[i].vmax_vol =
			AW_GET_32_DATA(vmax_data[index + 11],
					vmax_data[index + 10],
					vmax_data[index + 9],
					vmax_data[index + 8]);
	}

	for (i = 0; i < step_count; i++)
		AW_DEV_LOGI(dev, "vbat_min:%d, vbat_max%d, vmax_vol:0x%x",
			vmax_step[i].vbat_min,
			vmax_step[i].vbat_max,
			vmax_step[i].vmax_vol);
}

static int aw_monitor_parse_vol_data_v1(struct device *dev,
			struct aw_monitor *monitor, char *monitor_data)
{
	uint32_t step_count = 0;
	char *vmax_data = NULL;
	struct vmax_step_config *vmax_step = NULL;

	AW_DEV_LOGD(dev, "enter");

	step_count = monitor->monitor_hdr.step_count;
	if (step_count) {
		vmax_step = devm_kzalloc(dev, sizeof(struct vmax_step_config) * step_count,
					GFP_KERNEL);
		if (vmax_step == NULL) {
			AW_DEV_LOGE(dev, "vmax_cfg vmalloc failed");
			return -ENOMEM;
		}
		memset(vmax_step, 0,
			sizeof(struct vmax_step_config) * step_count);
	}

	vmax_data = monitor_data + sizeof(struct aw_bin_header) +
		sizeof(struct aw_monitor_header);
	aw_monitor_write_to_table_v1(dev, vmax_step, vmax_data, step_count);
	monitor->vmax_cfg = vmax_step;

	AW_DEV_LOGI(dev, "vmax_data parse succeed");

	return 0;
}

static int aw_monitor_parse_data_v1(struct device *dev,
			struct aw_monitor *monitor, char *monitor_data)
{
	int ret = -1;
	int header_len = 0;
	struct aw_monitor_header *monitor_hdr = &monitor->monitor_hdr;

	header_len = sizeof(struct aw_bin_header);
	memcpy(monitor_hdr, monitor_data + header_len,
		sizeof(struct aw_monitor_header));

	AW_DEV_LOGI(dev, "monitor_switch:%d, monitor_time:%d (ms), monitor_count:%d, step_count:%d",
		monitor_hdr->monitor_switch, monitor_hdr->monitor_time,
		monitor_hdr->monitor_count, monitor_hdr->step_count);

	ret = aw_monitor_parse_vol_data_v1(dev, monitor, monitor_data);
	if (ret < 0) {
		AW_DEV_LOGE(dev, "vmax_data parse failed");
		return ret;
	}

	monitor->bin_status = AW_MONITOR_CFG_OK;

	return 0;
}


static int aw_monitor_parse_v_1_0_0(struct device *dev,
			struct aw_monitor *monitor, char *monitor_data)
{
	int ret = -1;
	struct aw_bin_header *header = (struct aw_bin_header *)monitor_data;

	switch (header->bin_data_ver) {
	case AW_MONITOR_DATA_VER:
		ret = aw_monitor_parse_data_v1(dev, monitor, monitor_data);
		if (ret < 0)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

void aw_monitor_cfg_free(struct aw_monitor *monitor)
{
	struct aw87xxx *aw87xxx =
		container_of(monitor, struct aw87xxx, monitor);

	monitor->bin_status = AW_MONITOR_CFG_WAIT;
	memset(&monitor->monitor_hdr, 0,
		sizeof(struct aw_monitor_header));
	if (monitor->vmax_cfg) {
		devm_kfree(aw87xxx->dev, monitor->vmax_cfg);
		monitor->vmax_cfg = NULL;
	}
}

int aw_monitor_bin_parse(struct device *dev,
				char *monitor_data, uint32_t data_len)
{
	int ret = -1;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = NULL;
	struct aw_bin_header *bin_header = NULL;

	if (aw87xxx == NULL) {
		AW_DEV_LOGE(dev, "get struct aw87xxx failed");
		return -EINVAL;
	}

	monitor = &aw87xxx->monitor;
	monitor->bin_status = AW_MONITOR_CFG_WAIT;

	AW_DEV_LOGI(dev, "monitor bin parse version: %s",
			AW_MONITOT_BIN_PARSE_VERSION);

	ret = aw_monitor_bin_check(dev, monitor_data, data_len);
	if (ret < 0) {
		AW_DEV_LOGE(dev, "monitor bin check failed");
		return ret;
	}

	bin_header = (struct aw_bin_header *)monitor_data;
	switch (bin_header->bin_data_ver) {
	case DATA_VERSION_V1:
		ret = aw_monitor_parse_v_1_0_0(dev, monitor,
				monitor_data);
		if (ret < 0) {
			aw_monitor_cfg_free(monitor);
			return ret;
		}
		break;
	default:
		AW_DEV_LOGE(dev, "Unrecognized this bin data version[0x%x]",
			bin_header->bin_data_ver);
	}

	return 0;
}

/***************************************************************************
 *
 * aw87xxx monitor get adjustment vmax of power
 *
 ***************************************************************************/
static int aw_monitor_get_battery_capacity(struct device *dev,
				struct aw_monitor *monitor,
				uint32_t *vbat_capacity)
{
	char name[] = "battery";
	int ret = -1;
	union power_supply_propval prop = { 0 };
	struct power_supply *psy = NULL;

	psy = power_supply_get_by_name(name);
	if (psy == NULL) {
		AW_DEV_LOGE(dev, "no struct power supply name:%s", name);
		return -EINVAL;
	}

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &prop);
	if (ret < 0) {
		AW_DEV_LOGE(dev, "get vbat capacity failed");
		return -EINVAL;
	}
	*vbat_capacity = prop.intval;
	AW_DEV_LOGI(dev, "The percentage is %d",
		*vbat_capacity);

	return 0;
}

static int aw_search_vmax_from_table(struct device *dev,
				struct aw_monitor *monitor,
				const int vbat_vol, int *vmax_vol)
{
	int i = 0;
	int vmax_set = 0;
	uint32_t vmax_flag = 0;
	struct aw_monitor_header *monitor_hdr = &monitor->monitor_hdr;
	struct vmax_step_config *vmax_cfg = monitor->vmax_cfg;

	if (monitor->bin_status == AW_MONITOR_CFG_WAIT) {
		AW_DEV_LOGE(dev, "vmax_cfg not loaded or parse failed");
		return -ENODATA;
	}

	for (i = 0; i < monitor_hdr->step_count; i++) {
		if (vbat_vol == AW_VBAT_MAX) {
			vmax_set = AW_VMAX_MAX;
			vmax_flag = 1;
			AW_DEV_LOGD(dev, "vbat=%d, setting vmax=0x%x",
				vbat_vol, vmax_set);
			break;
		}

		if (vbat_vol >= vmax_cfg[i].vbat_min &&
			vbat_vol < vmax_cfg[i].vbat_max) {
			vmax_set = vmax_cfg[i].vmax_vol;
			vmax_flag = 1;
			AW_DEV_LOGD(dev, "read setting vmax=0x%x, step[%d]: vbat_min=%d,vbat_max=%d",
				vmax_set, i,
				vmax_cfg[i].vbat_min,
				vmax_cfg[i].vbat_max);
			break;
		}
	}

	if (!vmax_flag) {
		AW_DEV_LOGE(dev, "vmax_cfg not found");
		return -ENODATA;
	}

	*vmax_vol = vmax_set;
	return 0;
}


/***************************************************************************
 *
 *monitor_esd_func
 *
 ***************************************************************************/
static int aw_chip_status_recover(struct aw87xxx *aw87xxx)
{
	int ret = -1;
	struct aw_monitor *monitor = &aw87xxx->monitor;
	char *profile = aw87xxx->current_profile;

	AW_DEV_LOGD(aw87xxx->dev, "enter");

	ret = aw87xxx_esd_update_profile(aw87xxx, profile);
	if (ret < 0) {
		AW_DEV_LOGE(aw87xxx->dev, "load profile[%s] failed ",
			profile);
		return ret;
	}

	AW_DEV_LOGI(aw87xxx->dev, "current prof[%s], dev_index[%d] ",
			profile, aw87xxx->dev_index);

	monitor->pre_vmax = AW_VMAX_INIT_VAL;
	monitor->first_entry = AW_FIRST_ENTRY;
	monitor->timer_cnt = 0;
	monitor->vbat_sum = 0;

	return 0;
}

static int aw_monitor_chip_esd_check_work(struct aw87xxx *aw87xxx)
{
	int ret = 0;
	int i = 0;

	for (i = 0; i < REG_STATUS_CHECK_MAX; i++) {
		AW_DEV_LOGD(aw87xxx->dev, "reg_status_check[%d]", i);

		ret = aw_dev_esd_reg_status_check(&aw87xxx->aw_dev);
		if (ret < 0) {
			aw_chip_status_recover(aw87xxx);
		} else {
			AW_DEV_LOGD(aw87xxx->dev, "chip status check succeed");
			break;
		}
		msleep(AW_ESD_CHECK_DELAY);
	}

	if (ret < 0) {
		AW_DEV_LOGE(aw87xxx->dev, "chip status recover failed,chip off");
		aw87xxx_esd_update_profile(aw87xxx, aw87xxx->prof_off_name);
		return ret;
	}

	return 0;
}


/***************************************************************************
 *
 * aw87xxx monitor work with dsp
 *
 ***************************************************************************/
static int aw_monitor_update_vmax_to_dsp(struct device *dev,
				struct aw_monitor *monitor, int vmax_set)
{
	int ret = -1;
	uint32_t enable = 0;

	if (monitor->pre_vmax != vmax_set) {
		ret = aw_dsp_get_rx_module_enable(&enable);
		if (!enable || ret < 0) {
			AW_DEV_LOGE(dev, "get rx failed or rx disable, ret=%d, enable=%d",
				ret, enable);
			return -EPERM;
		}

		ret = aw_dsp_set_vmax(vmax_set, monitor->dev_index);
		if (ret) {
			AW_DEV_LOGE(dev, "set dsp msg fail, ret=%d", ret);
			return ret;
		}

		AW_DEV_LOGI(dev, "set dsp vmax=0x%x sucess", vmax_set);
		monitor->pre_vmax = vmax_set;
	} else {
		AW_DEV_LOGI(dev, "vmax=0x%x no change", vmax_set);
	}

	return 0;
}

static void aw_monitor_with_dsp_vmax_work(struct device *dev,
					struct aw_monitor *monitor)
{
	int ret = -1;
	int vmax_set = 0;
	uint32_t vbat_capacity = 0;
	uint32_t ave_capacity = 0;
	struct aw_monitor_header *monitor_hdr = &monitor->monitor_hdr;

	AW_DEV_LOGD(dev, "enter with dsp monitor");

	ret = aw_monitor_get_battery_capacity(dev, monitor, &vbat_capacity);
	if (ret < 0)
		return;

	if (monitor->timer_cnt < monitor_hdr->monitor_count) {
		monitor->timer_cnt++;
		monitor->vbat_sum += vbat_capacity;
			AW_DEV_LOGI(dev, "timer_cnt = %d",
			monitor->timer_cnt);
	}
	if ((monitor->timer_cnt >= monitor_hdr->monitor_count) ||
	    (monitor->first_entry == AW_FIRST_ENTRY)) {
		if (monitor->first_entry == AW_FIRST_ENTRY)
			monitor->first_entry = AW_NOT_FIRST_ENTRY;
		ave_capacity = monitor->vbat_sum / monitor->timer_cnt;

		if (monitor->custom_capacity)
			ave_capacity = monitor->custom_capacity;

		AW_DEV_LOGI(dev, "get average capacity = %d", ave_capacity);

		ret = aw_search_vmax_from_table(dev, monitor,
				ave_capacity, &vmax_set);
		if (ret < 0)
			AW_DEV_LOGE(dev, "not find vmax_vol");
		else
			aw_monitor_update_vmax_to_dsp(dev, monitor, vmax_set);

		monitor->timer_cnt = 0;
		monitor->vbat_sum = 0;
	}
}

static void aw_monitor_work_func(struct work_struct *work)
{
	int ret = 0;
	struct aw87xxx *aw87xxx = container_of(work,
				struct aw87xxx, monitor.with_dsp_work.work);
	struct device *dev = aw87xxx->dev;
	struct aw_monitor *monitor = &aw87xxx->monitor;
	struct aw_monitor_header *monitor_hdr = &monitor->monitor_hdr;

	AW_DEV_LOGD(dev, "enter");

	if (monitor->esd_enable) {
		ret = aw_monitor_chip_esd_check_work(aw87xxx);
		if (ret < 0)
			return;
	}

	if (monitor_hdr->monitor_switch && !(aw87xxx->aw_dev.is_rec_mode) &&
		monitor->open_dsp_en && monitor->bin_status == AW_ACF_UPDATE) {
		AW_DEV_LOGD(dev, "start low power protection");
		aw_monitor_with_dsp_vmax_work(dev, monitor);
	}

	if (monitor->esd_enable || (monitor_hdr->monitor_switch &&
		!(aw87xxx->aw_dev.is_rec_mode) && monitor->open_dsp_en &&
		monitor->bin_status == AW_ACF_UPDATE)) {
		schedule_delayed_work(&monitor->with_dsp_work,
			msecs_to_jiffies(monitor_hdr->monitor_time));
	}
}

void aw_monitor_stop(struct aw_monitor *monitor)
{
	struct aw87xxx *aw87xxx =
		container_of(monitor, struct aw87xxx, monitor);

	AW_DEV_LOGD(aw87xxx->dev, "enter");
	cancel_delayed_work_sync(&monitor->with_dsp_work);
}

void aw_monitor_start(struct aw_monitor *monitor)
{
	struct aw87xxx *aw87xxx =
		container_of(monitor, struct aw87xxx, monitor);
	int ret = 0;

	ret = aw_dev_check_reg_is_rec_mode(&aw87xxx->aw_dev);
	if (ret < 0) {
		AW_DEV_LOGE(aw87xxx->dev, "get reg current mode failed");
		return;
	}

	if (monitor->esd_enable || (monitor->monitor_hdr.monitor_switch &&
			!(aw87xxx->aw_dev.is_rec_mode) && monitor->open_dsp_en
			&& monitor->bin_status == AW_ACF_UPDATE)) {

		AW_DEV_LOGD(aw87xxx->dev, "enter");
		monitor->pre_vmax = AW_VMAX_INIT_VAL;
		monitor->first_entry = AW_FIRST_ENTRY;
		monitor->timer_cnt = 0;
		monitor->vbat_sum = 0;

		schedule_delayed_work(&monitor->with_dsp_work,
				msecs_to_jiffies(monitor->monitor_hdr.monitor_time));
	}
}
/***************************************************************************
 *
 * aw87xxx no dsp monitor func
 *
 ***************************************************************************/
int aw_monitor_no_dsp_get_vmax(struct aw_monitor *monitor, int32_t *vmax)
{
	int vbat_capacity = 0;
	int ret = -1;
	int vmax_vol = 0;
	struct aw87xxx *aw87xxx =
		container_of(monitor, struct aw87xxx, monitor);
	struct device *dev = aw87xxx->dev;

	ret = aw_monitor_get_battery_capacity(dev, monitor, &vbat_capacity);
	if (ret < 0)
		return ret;

	if (monitor->custom_capacity)
		vbat_capacity = monitor->custom_capacity;
	AW_DEV_LOGI(dev, "get_battery_capacity is[%d]", vbat_capacity);

	ret = aw_search_vmax_from_table(dev, monitor,
				vbat_capacity, &vmax_vol);
	if (ret < 0) {
		AW_DEV_LOGE(dev, "not find vmax_vol");
		return ret;
	}

	*vmax = vmax_vol;
	return 0;
}


/***************************************************************************
 *
 * aw87xxx monitor sysfs nodes
 *
 ***************************************************************************/
static ssize_t aw_attr_get_esd_enable(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = &aw87xxx->monitor;

	if (monitor->esd_enable) {
		AW_DEV_LOGI(aw87xxx->dev, "esd-enable=true");
		len += snprintf(buf + len, PAGE_SIZE - len,
			"esd-enable=true\n");
	} else {
		AW_DEV_LOGI(aw87xxx->dev, "esd-enable=false");
		len += snprintf(buf + len, PAGE_SIZE - len,
			"esd-enable=false\n");
	}

	return len;
}

static ssize_t aw_attr_set_esd_enable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	char esd_enable[16] = {0};
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = &aw87xxx->monitor;

	if (sscanf(buf, "%s", esd_enable) == 1) {
		AW_DEV_LOGD(aw87xxx->dev, "input esd-enable=[%s]", esd_enable);
		if (!strcmp(esd_enable, "true"))
			monitor->esd_enable = AW_ESD_ENABLE;
		else
			monitor->esd_enable = AW_ESD_DISABLE;
		AW_DEV_LOGI(dev, "set esd-enable=[%s]",
				monitor->esd_enable ? "true" : "false");
	} else {
		AW_DEV_LOGE(aw87xxx->dev, "input esd-enable error");
		return -EINVAL;
	}

	return len;
}

static ssize_t aw_attr_get_vbat(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int ret = -1;
	int vbat_capacity = 0;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = &aw87xxx->monitor;

	if (monitor->custom_capacity == 0) {
		ret = aw_monitor_get_battery_capacity(dev, monitor,
					&vbat_capacity);
		if (ret < 0) {
			AW_DEV_LOGE(aw87xxx->dev, "get battery_capacity failed");
			return ret;
		}
		len += snprintf(buf + len, PAGE_SIZE - len,
			"vbat capacity=%d\n", vbat_capacity);
	} else {
		len += snprintf(buf + len, PAGE_SIZE - len,
				"vbat capacity=%d\n",
				monitor->custom_capacity);
	}

	return len;
}

static ssize_t aw_attr_set_vbat(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = -1;
	uint32_t capacity = 0;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = &aw87xxx->monitor;

	ret = kstrtouint(buf, 0, &capacity);
	if (ret < 0)
		return ret;
	AW_DEV_LOGI(aw87xxx->dev, "set capacity = %d", capacity);
	if (capacity >= AW_VBAT_CAPACITY_MIN &&
			capacity <= AW_VBAT_CAPACITY_MAX){
		monitor->custom_capacity = capacity;
	} else {
		AW_DEV_LOGE(aw87xxx->dev, "vbat_set=invalid,please input value [%d-%d]",
			AW_VBAT_CAPACITY_MIN, AW_VBAT_CAPACITY_MAX);
		return -EINVAL;
	}

	return len;
}

static ssize_t aw_attr_get_vmax(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int ret = -1;
	uint32_t vbat_capacity = 0;
	int vmax_get = 0;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = &aw87xxx->monitor;

	if (monitor->open_dsp_en) {
		ret = aw_dsp_get_vmax(&vmax_get, aw87xxx->dev_index);
		if (ret < 0) {
			AW_DEV_LOGE(aw87xxx->dev,
				"get dsp vmax fail, ret=%d", ret);
			return ret;
		}
		len += snprintf(buf + len, PAGE_SIZE - len,
				"get_vmax=0x%x\n", vmax_get);
	} else {
		ret = aw_monitor_get_battery_capacity(dev, monitor,
						&vbat_capacity);
		if (ret < 0)
			return ret;
		AW_DEV_LOGI(aw87xxx->dev, "get_battery_capacity is [%d]",
			vbat_capacity);

		if (monitor->custom_capacity) {
			vbat_capacity = monitor->custom_capacity;
			AW_DEV_LOGI(aw87xxx->dev, "get custom_capacity is [%d]",
				vbat_capacity);
		}

		ret = aw_search_vmax_from_table(aw87xxx->dev, monitor,
					vbat_capacity, &vmax_get);
		if (ret < 0) {
			AW_DEV_LOGE(aw87xxx->dev, "not find vmax_vol");
			len += snprintf(buf + len, PAGE_SIZE - len,
				"not_find_vmax_vol\n");
			return len;
		}
		len += snprintf(buf + len, PAGE_SIZE - len,
			"0x%x\n", vmax_get);
		AW_DEV_LOGI(aw87xxx->dev, "0x%x", vmax_get);
	}

	return len;
}

static ssize_t aw_attr_set_vmax(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint32_t vmax_set = 0;
	int ret = -1;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = &aw87xxx->monitor;

	ret = kstrtouint(buf, 0, &vmax_set);
	if (ret < 0)
		return ret;

	AW_DEV_LOGI(aw87xxx->dev, "vmax_set=0x%x", vmax_set);

	if (monitor->open_dsp_en) {
		ret = aw_dsp_set_vmax(vmax_set, aw87xxx->dev_index);
		if (ret < 0) {
			AW_DEV_LOGE(aw87xxx->dev, "send dsp_msg error, ret = %d",
				ret);
			return ret;
		}
		msleep(2);
	} else {
		AW_DEV_LOGE(aw87xxx->dev, "no_dsp system,vmax_set invalid");
		return -EINVAL;
	}

	return count;
}

static ssize_t aw_attr_get_monitor_switch(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = &aw87xxx->monitor;
	struct aw_monitor_header *monitor_hdr = &monitor->monitor_hdr;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"aw87xxx monitor switch: %d\n",
			monitor_hdr->monitor_switch);
	return len;
}

static ssize_t aw_attr_set_monitor_switch(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint32_t enable = 0;
	int ret = -1;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = &aw87xxx->monitor;
	struct aw_monitor_header *monitor_hdr = &monitor->monitor_hdr;

	ret = kstrtouint(buf, 0, &enable);
	if (ret < 0)
		return ret;
	AW_DEV_LOGI(aw87xxx->dev, "monitor switch set =%d", enable);

	if (!monitor->bin_status) {
		AW_DEV_LOGE(aw87xxx->dev, "bin parse faile or not loaded,set invalid");
		return -EINVAL;
	}

	if (enable > 0)
		monitor_hdr->monitor_switch = 1;
	else
		monitor_hdr->monitor_switch = 0;

	if (monitor->open_dsp_en && enable) {
		monitor_hdr->monitor_switch = 1;
		monitor->pre_vmax = AW_VMAX_INIT_VAL;
		monitor->first_entry = AW_FIRST_ENTRY;
		monitor->timer_cnt = 0;
		monitor->vbat_sum = 0;
	} else if (monitor->open_dsp_en && !enable) {
		monitor_hdr->monitor_switch = 0;
	}
	return count;
}

static ssize_t aw_attr_get_monitor_time(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = &aw87xxx->monitor;
	struct aw_monitor_header *monitor_hdr = &monitor->monitor_hdr;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"aw_monitor_timer = %d(ms)\n",
			monitor_hdr->monitor_time);
	return len;
}

static ssize_t aw_attr_set_monitor_time(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int timer_val = 0;
	int ret = -1;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = &aw87xxx->monitor;
	struct aw_monitor_header *monitor_hdr = &monitor->monitor_hdr;

	ret = kstrtouint(buf, 0, &timer_val);
	if (ret < 0)
		return ret;

	AW_DEV_LOGI(aw87xxx->dev, "input monitor timer=%d(ms)", timer_val);

	if (!monitor->bin_status) {
		AW_DEV_LOGE(aw87xxx->dev, "bin parse faile or not loaded,set invalid");
		return -EINVAL;
	}

	if (timer_val != monitor_hdr->monitor_time)
		monitor_hdr->monitor_time = timer_val;
	else
		AW_DEV_LOGI(aw87xxx->dev, "no_change monitor_time");

	return count;
}

static ssize_t aw_attr_get_monitor_count(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = &aw87xxx->monitor;
	struct aw_monitor_header *monitor_hdr = &monitor->monitor_hdr;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"aw_monitor_count = %d\n",
			monitor_hdr->monitor_count);
	return len;
}

static ssize_t aw_attr_set_monitor_count(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int monitor_count = 0;
	int ret = -1;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = &aw87xxx->monitor;
	struct aw_monitor_header *monitor_hdr = &monitor->monitor_hdr;

	ret = kstrtouint(buf, 0, &monitor_count);
	if (ret < 0)
		return ret;
	AW_DEV_LOGI(aw87xxx->dev, "input monitor count=%d", monitor_count);

	if (!monitor->bin_status) {
		AW_DEV_LOGE(aw87xxx->dev, "bin parse faile or not loaded,set invalid");
		return -EINVAL;
	}

	if (monitor_count != monitor_hdr->monitor_count)
		monitor_hdr->monitor_count = monitor_count;
	else
		AW_DEV_LOGI(aw87xxx->dev, "no_change monitor_count");

	return count;
}


static ssize_t aw_attr_get_rx(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = &aw87xxx->monitor;
	ssize_t len = 0;
	int ret = -1;
	uint32_t enable = 0;

	if (monitor->open_dsp_en) {
		ret = aw_dsp_get_rx_module_enable(&enable);
		if (ret) {
			AW_DEV_LOGE(aw87xxx->dev, "dsp_msg error, ret=%d", ret);
			return ret;
		}
		len += snprintf(buf + len, PAGE_SIZE - len,
			"aw87xxx rx: %d\n", enable);
	} else {
		len += snprintf(buf + len, PAGE_SIZE - len,
				"command is invalid\n");
	}

	return len;
}

static ssize_t aw_attr_set_rx(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_monitor *monitor = &aw87xxx->monitor;
	int ret = -1;
	uint32_t enable;

	ret = kstrtouint(buf, 0, &enable);
	if (ret < 0)
		return ret;

	if (monitor->open_dsp_en) {
		AW_DEV_LOGI(aw87xxx->dev, "set rx enable=%d", enable);

		ret = aw_dsp_set_rx_module_enable(enable);
		if (ret < 0) {
			AW_DEV_LOGE(aw87xxx->dev, "dsp_msg error, ret=%d",
				ret);
			return ret;
		}
	} else {
		AW_DEV_LOGE(aw87xxx->dev, "command is invalid");
		return -EINVAL;
	}

	return count;
}


static DEVICE_ATTR(esd_enable, S_IWUSR | S_IRUGO,
	aw_attr_get_esd_enable, aw_attr_set_esd_enable);
static DEVICE_ATTR(vbat, S_IWUSR | S_IRUGO,
	aw_attr_get_vbat, aw_attr_set_vbat);
static DEVICE_ATTR(vmax, S_IWUSR | S_IRUGO,
	aw_attr_get_vmax, aw_attr_set_vmax);

static DEVICE_ATTR(monitor_switch, S_IWUSR | S_IRUGO,
	aw_attr_get_monitor_switch, aw_attr_set_monitor_switch);
static DEVICE_ATTR(monitor_time, S_IWUSR | S_IRUGO,
	aw_attr_get_monitor_time, aw_attr_set_monitor_time);
static DEVICE_ATTR(monitor_count, S_IWUSR | S_IRUGO,
	aw_attr_get_monitor_count, aw_attr_set_monitor_count);
static DEVICE_ATTR(rx, S_IWUSR | S_IRUGO,
	aw_attr_get_rx, aw_attr_set_rx);

static struct attribute *aw_monitor_vol_adjust[] = {
	&dev_attr_esd_enable.attr,
	&dev_attr_vbat.attr,
	&dev_attr_vmax.attr,
	NULL
};

static struct attribute_group aw_monitor_vol_adjust_group = {
	.attrs = aw_monitor_vol_adjust,
};

static struct attribute *aw_monitor_control[] = {
	&dev_attr_monitor_switch.attr,
	&dev_attr_monitor_time.attr,
	&dev_attr_monitor_count.attr,
	&dev_attr_rx.attr,
	NULL
};

static struct attribute_group aw_monitor_control_group = {
	.attrs = aw_monitor_control,
};

/***************************************************************************
 *
 * aw87xxx monitor init
 *
 ***************************************************************************/
static void aw_monitor_dtsi_parse(struct device *dev,
				struct aw_monitor *monitor,
				struct device_node *dev_node)
{
	int ret = -1;
	const char *esd_enable;

	ret = of_property_read_string(dev_node, "esd-enable", &esd_enable);
	if (ret < 0) {
		AW_DEV_LOGI(dev, "esd_enable parse failed, user default[disable]");
		monitor->esd_enable = AW_ESD_DISABLE;
	} else {
		if (!strcmp(esd_enable, "true"))
			monitor->esd_enable = AW_ESD_ENABLE;
		else
			monitor->esd_enable = AW_ESD_DISABLE;

		AW_DEV_LOGI(dev, "parse esd-enable=[%s]",
				monitor->esd_enable ? "true" : "false");
	}
}

void aw_monitor_init(struct device *dev, struct aw_monitor *monitor,
				struct device_node *dev_node)
{
	int ret = -1;
	struct aw87xxx *aw87xxx =
		container_of(monitor, struct aw87xxx, monitor);

	monitor->dev_index = aw87xxx->dev_index;
	monitor->monitor_hdr.monitor_time = AW_DEFAULT_MONITOR_TIME;

	aw_monitor_dtsi_parse(dev, monitor, dev_node);

	/* get platform open dsp type */
	monitor->open_dsp_en = aw_dsp_isEnable();

	ret = sysfs_create_group(&dev->kobj, &aw_monitor_vol_adjust_group);
	if (ret < 0)
		AW_DEV_LOGE(dev, "failed to create monitor vol_adjust sysfs nodes");

	INIT_DELAYED_WORK(&monitor->with_dsp_work, aw_monitor_work_func);

	if (monitor->open_dsp_en) {
		ret = sysfs_create_group(&dev->kobj, &aw_monitor_control_group);
		if (ret < 0)
			AW_DEV_LOGE(dev, "failed to create monitor dsp control sysfs nodes");
	}

	if (!ret)
		AW_DEV_LOGI(dev, "monitor init succeed");
}

void aw_monitor_exit(struct aw_monitor *monitor)
{
	struct aw87xxx *aw87xxx =
		container_of(monitor, struct aw87xxx, monitor);
	/*rm attr node*/
	sysfs_remove_group(&aw87xxx->dev->kobj,
			&aw_monitor_vol_adjust_group);

	aw_monitor_stop(monitor);

	if (monitor->open_dsp_en) {
		sysfs_remove_group(&aw87xxx->dev->kobj,
				&aw_monitor_control_group);
	}
}

