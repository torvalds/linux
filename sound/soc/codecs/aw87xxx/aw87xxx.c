/*
 * aw87xxx.c  aw87xxx pa module
 *
 * Copyright (c) 2021 AWINIC Technology CO., LTD
 *
 * Author: Barry <zhaozhongbo@awinic.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/i2c.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/gameport.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/kthread.h>
#include <uapi/sound/asound.h>
#include <sound/control.h>
#include <sound/soc.h>
#include "aw87xxx.h"
#include "aw_device.h"
#include "aw_log.h"
#include "aw_monitor.h"
#include "aw_acf_bin.h"
#include "aw_bin_parse.h"

/*****************************************************************
* aw87xxx marco
******************************************************************/
#define AW87XXX_I2C_NAME	"aw87xxx_pa"
#define AW87XXX_DRIVER_VERSION	"v2.2.0"
#define AW87XXX_FW_BIN_NAME	"aw87xxx_acf.bin"

/*************************************************************************
 * aw87xxx variable
 ************************************************************************/
static LIST_HEAD(g_aw87xxx_list);
static DEFINE_MUTEX(g_aw87xxx_mutex_lock);
unsigned int g_aw87xxx_dev_cnt = 0;

#ifdef AW_KERNEL_VER_OVER_4_19_1
static struct aw_componet_codec_ops aw_componet_codec_ops = {
	.add_codec_controls = snd_soc_add_component_controls,
	.unregister_codec = snd_soc_unregister_component,
};
#else
static struct aw_componet_codec_ops aw_componet_codec_ops = {
	.add_codec_controls = snd_soc_add_codec_controls,
	.unregister_codec = snd_soc_unregister_codec,
};
#endif


/************************************************************************
 *
 * aw87xxx device update profile
 *
 ************************************************************************/
static int aw87xxx_update_off_prof(struct aw87xxx *aw87xxx, char *profile)
{
	int ret = 0;
	struct aw_prof_desc *prof_desc = NULL;
	struct aw_data_container *data_container = NULL;
	struct aw_device *aw_dev = &aw87xxx->aw_dev;

	AW_DEV_LOGD(aw87xxx->dev, "enter");

	mutex_lock(&aw87xxx->reg_lock);

	aw_monitor_stop(&aw87xxx->monitor);

	prof_desc = aw_acf_get_prof_desc_form_name(aw87xxx->dev, &aw87xxx->acf_info, profile);
	if (prof_desc == NULL)
		goto no_bin_pwr_off;

	if (!prof_desc->prof_st)
		goto no_bin_pwr_off;


	data_container = &prof_desc->data_container;
	AW_DEV_LOGD(aw87xxx->dev, "get profile[%s] data len [%d]",
			profile, data_container->len);

	if (aw_dev->hwen_status == AW_DEV_HWEN_OFF) {
		AW_DEV_LOGI(aw87xxx->dev, "profile[%s] has already load ", profile);
	} else {
		if (aw_dev->ops.pwr_off_func) {
			ret = aw_dev->ops.pwr_off_func(aw_dev, data_container);
			if (ret < 0) {
				AW_DEV_LOGE(aw87xxx->dev, "load profile[%s] failed ", profile);
				goto pwr_off_failed;
			}
		} else {
			ret = aw_dev_default_pwr_off(aw_dev, data_container);
			if (ret < 0) {
				AW_DEV_LOGE(aw87xxx->dev, "load profile[%s] failed ", profile);
				goto pwr_off_failed;
			}
		}
	}

	aw87xxx->current_profile = prof_desc->prof_name;
	mutex_unlock(&aw87xxx->reg_lock);

	return 0;

pwr_off_failed:
no_bin_pwr_off:
	aw_dev_hw_pwr_ctrl(&aw87xxx->aw_dev, false);
	aw87xxx->current_profile = aw87xxx->prof_off_name;
	mutex_unlock(&aw87xxx->reg_lock);
	return ret;
}

int aw87xxx_update_profile(struct aw87xxx *aw87xxx, char *profile)
{
	int ret = -EINVAL;
	struct aw_prof_desc *prof_desc = NULL;
	struct aw_prof_info *prof_info = &aw87xxx->acf_info.prof_info;
	struct aw_data_container *data_container = NULL;
	struct aw_device *aw_dev = &aw87xxx->aw_dev;

	AW_DEV_LOGD(aw87xxx->dev, "enter");

	if (!prof_info->status) {
		AW_DEV_LOGE(aw87xxx->dev, "profile_cfg not load");
		return -EINVAL;
	}

	if (0 == strncmp(profile, aw87xxx->prof_off_name, AW_PROFILE_STR_MAX))
		return aw87xxx_update_off_prof(aw87xxx, profile);

	mutex_lock(&aw87xxx->reg_lock);

	prof_desc = aw_acf_get_prof_desc_form_name(aw87xxx->dev, &aw87xxx->acf_info, profile);
	if (prof_desc == NULL) {
		AW_DEV_LOGE(aw87xxx->dev, "not found [%s] parameter", profile);
		mutex_unlock(&aw87xxx->reg_lock);
		return -EINVAL;
	}

	if (!prof_desc->prof_st) {
		AW_DEV_LOGE(aw87xxx->dev, "not found data container");
		mutex_unlock(&aw87xxx->reg_lock);
		return -EINVAL;
	}

	data_container = &prof_desc->data_container;
	AW_DEV_LOGD(aw87xxx->dev, "get profile[%s] data len [%d]",
			profile, data_container->len);

	aw_monitor_stop(&aw87xxx->monitor);

	if (aw_dev->ops.pwr_on_func) {
		ret = aw_dev->ops.pwr_on_func(aw_dev, data_container);
		if (ret < 0) {
			AW_DEV_LOGE(aw87xxx->dev, "load profile[%s] failed ",
				profile);
			mutex_unlock(&aw87xxx->reg_lock);
			return aw87xxx_update_off_prof(aw87xxx, aw87xxx->prof_off_name);
		}
	} else {
		ret = aw_dev_default_pwr_on(aw_dev, data_container);
		if (ret < 0) {
			AW_DEV_LOGE(aw87xxx->dev, "load profile[%s] failed ",
				profile);
			mutex_unlock(&aw87xxx->reg_lock);
			return aw87xxx_update_off_prof(aw87xxx, aw87xxx->prof_off_name);
		}
	}

	aw87xxx->current_profile = prof_desc->prof_name;
	aw_monitor_start(&aw87xxx->monitor);
	mutex_unlock(&aw87xxx->reg_lock);

	AW_DEV_LOGD(aw87xxx->dev, "load profile[%s] succeed", profile);

	return 0;
}

char *aw87xxx_show_current_profile(int dev_index)
{
	struct list_head *pos = NULL;
	struct aw87xxx *aw87xxx = NULL;

	list_for_each(pos, &g_aw87xxx_list) {
		aw87xxx = list_entry(pos, struct aw87xxx, list);
		if (aw87xxx == NULL) {
			AW_LOGE("struct aw87xxx not ready");
			return NULL;
		}

		if (aw87xxx->dev_index == dev_index) {
			AW_DEV_LOGI(aw87xxx->dev, "current profile is [%s]",
				aw87xxx->current_profile);
			return aw87xxx->current_profile;
		}
	}

	AW_LOGE("not found struct aw87xxx, dev_index = [%d]", dev_index);
	return NULL;
}
EXPORT_SYMBOL(aw87xxx_show_current_profile);

int aw87xxx_set_profile(int dev_index, char *profile)
{
	struct list_head *pos = NULL;
	struct aw87xxx *aw87xxx = NULL;

	list_for_each(pos, &g_aw87xxx_list) {
		aw87xxx = list_entry(pos, struct aw87xxx, list);
		if (aw87xxx == NULL) {
			AW_LOGE("struct aw87xxx not ready");
			return -EINVAL;
		}

		if (profile && aw87xxx->dev_index == dev_index)
			return aw87xxx_update_profile(aw87xxx, profile);
	}

	AW_LOGE("not found struct aw87xxx, dev_index = [%d]", dev_index);
	return -EINVAL;
}
EXPORT_SYMBOL(aw87xxx_set_profile);

/************************************************************************
 *
 * aw87xxx esd update profile
 *
 ************************************************************************/
static int aw87xxx_esd_update_off_prof(struct aw87xxx *aw87xxx, char *profile)
{
	int ret = 0;
	struct aw_prof_desc *prof_desc = NULL;
	struct aw_data_container *data_container = NULL;
	struct aw_device *aw_dev = &aw87xxx->aw_dev;

	AW_DEV_LOGD(aw87xxx->dev, "enter");
	prof_desc = aw_acf_get_prof_desc_form_name(aw87xxx->dev, &aw87xxx->acf_info, profile);
	if (prof_desc == NULL)
		goto no_bin_pwr_off;

	if (!prof_desc->prof_st)
		goto no_bin_pwr_off;

	data_container = &prof_desc->data_container;
	AW_DEV_LOGD(aw87xxx->dev, "get profile[%s] data len [%d]",
			profile, data_container->len);

	if (aw_dev->hwen_status == AW_DEV_HWEN_OFF) {
		AW_DEV_LOGI(aw87xxx->dev, "profile[%s] has already load ", profile);
	} else {
		if (aw_dev->ops.pwr_off_func) {
			ret = aw_dev->ops.pwr_off_func(aw_dev, data_container);
			if (ret < 0) {
				AW_DEV_LOGE(aw87xxx->dev, "load profile[%s] failed ", profile);
				goto pwr_off_failed;
			}
		} else {
			ret = aw_dev_default_pwr_off(aw_dev, data_container);
			if (ret < 0) {
				AW_DEV_LOGE(aw87xxx->dev, "load profile[%s] failed ", profile);
				goto pwr_off_failed;
			}
		}
	}

	aw87xxx->current_profile = prof_desc->prof_name;
	return 0;

pwr_off_failed:
no_bin_pwr_off:
	aw_dev_hw_pwr_ctrl(&aw87xxx->aw_dev, false);
	aw87xxx->current_profile = aw87xxx->prof_off_name;
	return ret;
}

int aw87xxx_esd_update_profile(struct aw87xxx *aw87xxx, char *profile)
{
	int ret = -EINVAL;
	struct aw_prof_desc *prof_desc = NULL;
	struct aw_prof_info *prof_info = &aw87xxx->acf_info.prof_info;
	struct aw_data_container *data_container = NULL;
	struct aw_device *aw_dev = &aw87xxx->aw_dev;

	AW_DEV_LOGD(aw87xxx->dev, "enter");

	if (!prof_info->status) {
		AW_DEV_LOGE(aw87xxx->dev, "profile_cfg not load");
		return -EINVAL;
	}

	if (0 == strncmp(profile, aw87xxx->prof_off_name, AW_PROFILE_STR_MAX))
		return aw87xxx_esd_update_off_prof(aw87xxx, profile);

	prof_desc = aw_acf_get_prof_desc_form_name(aw87xxx->dev, &aw87xxx->acf_info,
					profile);
	if (prof_desc == NULL) {
		AW_DEV_LOGE(aw87xxx->dev, "not found [%s] parameter", profile);
		return -EINVAL;
	}

	if (!prof_desc->prof_st) {
		AW_DEV_LOGE(aw87xxx->dev, "not found data container");
		return -EINVAL;
	}

	data_container = &prof_desc->data_container;
	AW_DEV_LOGD(aw87xxx->dev, "get profile[%s] data len [%d]",
			profile, data_container->len);

	if (aw_dev->ops.pwr_on_func) {
		ret = aw_dev->ops.pwr_on_func(aw_dev, data_container);
		if (ret < 0) {
			AW_DEV_LOGE(aw87xxx->dev, "load profile[%s] failed ",
				profile);
			return ret;
		}
	} else {
		ret = aw_dev_default_pwr_on(aw_dev, data_container);
		if (ret < 0) {
			AW_DEV_LOGE(aw87xxx->dev, "load profile[%s] failed ",
				profile);
			return ret;
		}
	}
	aw87xxx->current_profile = prof_desc->prof_name;
	AW_DEV_LOGD(aw87xxx->dev, "recover load profile[%s] succeed", profile);

	return 0;
}

/****************************************************************************
 *
 * aw87xxx Kcontrols
 *
 ****************************************************************************/
static int aw87xxx_profile_switch_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	int count = 0;
	char *name = NULL;
	char *profile_name = NULL;
	struct aw87xxx *aw87xxx = (struct aw87xxx *)kcontrol->private_value;

	if (aw87xxx == NULL) {
		AW_LOGE("get struct aw87xxx failed");
		return -EINVAL;
	}

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;

	/*make sure have prof */
	count = aw_acf_get_profile_count(aw87xxx->dev, &aw87xxx->acf_info);
	if (count <= 0) {
		uinfo->value.enumerated.items = 0;
		AW_DEV_LOGE(aw87xxx->dev, "get count[%d] failed", count);
		return 0;
	}

	uinfo->value.enumerated.items = count;
	if (uinfo->value.enumerated.item >= count)
		uinfo->value.enumerated.item = count - 1;

	name = uinfo->value.enumerated.name;
	count = uinfo->value.enumerated.item;
	profile_name = aw_acf_get_prof_name_form_index(aw87xxx->dev,
		&aw87xxx->acf_info, count);
	if (profile_name == NULL) {
		strlcpy(uinfo->value.enumerated.name, "NULL",
			strlen("NULL") + 1);
		return 0;
	}

	strlcpy(name, profile_name, sizeof(uinfo->value.enumerated.name));

	return 0;
}

static int aw87xxx_profile_switch_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	int ret = -1;
	char *profile_name = NULL;
	int index = ucontrol->value.integer.value[0];
	struct aw87xxx *aw87xxx = (struct aw87xxx *)kcontrol->private_value;
	struct acf_bin_info *acf_info = &aw87xxx->acf_info;

	if (aw87xxx == NULL) {
		AW_LOGE("get struct aw87xxx failed");
		return -EINVAL;
	}

	profile_name = aw_acf_get_prof_name_form_index(aw87xxx->dev, acf_info, index);
	if (!profile_name) {
		AW_DEV_LOGE(aw87xxx->dev, "not found profile name,index=[%d]",
				index);
		return -EINVAL;
	}

	AW_DEV_LOGI(aw87xxx->dev, "set profile [%s]", profile_name);

	ret = aw87xxx_update_profile(aw87xxx, profile_name);
	if (ret < 0) {
		AW_DEV_LOGE(aw87xxx->dev, "set dev_index[%d] profile failed, profile = %s",
			aw87xxx->dev_index, profile_name);
		return ret;
	}

	return 0;
}

static int aw87xxx_profile_switch_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	int index = 0;
	char *profile;
	struct aw87xxx *aw87xxx = (struct aw87xxx *)kcontrol->private_value;

	if (aw87xxx == NULL) {
		AW_LOGE("get struct aw87xxx failed");
		return -EINVAL;
	}

	if (!aw87xxx->current_profile) {
		AW_DEV_LOGE(aw87xxx->dev, "profile not init");
		return -EINVAL;
	}

	profile = aw87xxx->current_profile;
	AW_DEV_LOGI(aw87xxx->dev, "current profile:[%s]",
		aw87xxx->current_profile);


	index = aw_acf_get_prof_index_form_name(aw87xxx->dev,
		&aw87xxx->acf_info, aw87xxx->current_profile);
	if (index < 0) {
		AW_DEV_LOGE(aw87xxx->dev, "get profile index failed");
		return index;
	}

	ucontrol->value.integer.value[0] = index;

	return 0;
}

static int aw87xxx_vmax_get_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = INT_MIN;
	uinfo->value.integer.max = AW_VMAX_MAX;

	return 0;
}

static int aw87xxxx_vmax_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	int ret = -1;
	int vmax_val = 0;
	struct aw87xxx *aw87xxx = (struct aw87xxx *)kcontrol->private_value;

	if (aw87xxx == NULL) {
		AW_LOGE("get struct aw87xxx failed");
		return -EINVAL;
	}

	ret = aw_monitor_no_dsp_get_vmax(&aw87xxx->monitor, &vmax_val);
	if (ret < 0)
		return ret;

	ucontrol->value.integer.value[0] = vmax_val;
	AW_DEV_LOGI(aw87xxx->dev, "get vmax = [0x%x]", vmax_val);

	return 0;
}

static int aw87xxx_kcontrol_dynamic_create(struct aw87xxx *aw87xxx,
						void *codec)
{
	struct snd_kcontrol_new *aw87xxx_kcontrol = NULL;
	aw_snd_soc_codec_t *soc_codec = (aw_snd_soc_codec_t *)codec;
	char *kctl_name[AW87XXX_KCONTROL_NUM];
	int kcontrol_num = AW87XXX_KCONTROL_NUM;
	int ret = -1;

	AW_DEV_LOGD(aw87xxx->dev, "enter");
	aw87xxx->codec = soc_codec;

	aw87xxx_kcontrol = devm_kzalloc(aw87xxx->dev,
			sizeof(struct snd_kcontrol_new) * kcontrol_num,
			GFP_KERNEL);
	if (aw87xxx_kcontrol == NULL) {
		AW_DEV_LOGE(aw87xxx->dev, "aw87xxx_kcontrol devm_kzalloc failed");
		return -ENOMEM;
	}

	kctl_name[0] = devm_kzalloc(aw87xxx->dev, AW_NAME_BUF_MAX,
			GFP_KERNEL);
	if (kctl_name[0] == NULL)
		return -ENOMEM;

	snprintf(kctl_name[0], AW_NAME_BUF_MAX, "aw87xxx_profile_switch_%d",
			aw87xxx->dev_index);

	aw87xxx_kcontrol[0].name = kctl_name[0];
	aw87xxx_kcontrol[0].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	aw87xxx_kcontrol[0].info = aw87xxx_profile_switch_info;
	aw87xxx_kcontrol[0].get = aw87xxx_profile_switch_get;
	aw87xxx_kcontrol[0].put = aw87xxx_profile_switch_put;
	aw87xxx_kcontrol[0].private_value = (unsigned long)aw87xxx;

	kctl_name[1] = devm_kzalloc(aw87xxx->codec->dev, AW_NAME_BUF_MAX,
			GFP_KERNEL);
	if (kctl_name[1] == NULL)
		return -ENOMEM;

	snprintf(kctl_name[1], AW_NAME_BUF_MAX, "aw87xxx_vmax_get_%d",
			aw87xxx->dev_index);

	aw87xxx_kcontrol[1].name = kctl_name[1];
	aw87xxx_kcontrol[1].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	aw87xxx_kcontrol[1].access = SNDRV_CTL_ELEM_ACCESS_READ;
	aw87xxx_kcontrol[1].info = aw87xxx_vmax_get_info;
	aw87xxx_kcontrol[1].get = aw87xxxx_vmax_get;
	aw87xxx_kcontrol[1].private_value = (unsigned long)aw87xxx;

	ret = aw_componet_codec_ops.add_codec_controls(aw87xxx->codec,
				aw87xxx_kcontrol, kcontrol_num);
	if (ret < 0) {
		AW_DEV_LOGE(aw87xxx->dev, "add codec controls failed, ret = %d",
			ret);
		return ret;
	}

	AW_DEV_LOGI(aw87xxx->dev, "add codec controls[%s,%s]",
		aw87xxx_kcontrol[0].name,
		aw87xxx_kcontrol[1].name);

	return 0;
}

/****************************************************************************
 *
 *aw87xxx kcontrol create
 *
 ****************************************************************************/
int aw87xxx_add_codec_controls(void *codec)
{
	struct list_head *pos = NULL;
	struct aw87xxx *aw87xxx = NULL;
	int ret = -1;

	list_for_each(pos, &g_aw87xxx_list) {
		aw87xxx = list_entry(pos, struct aw87xxx, list);
		if (aw87xxx == NULL) {
			AW_LOGE("struct aw87xxx not ready");
			return ret;
		}

		ret = aw87xxx_kcontrol_dynamic_create(aw87xxx, codec);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(aw87xxx_add_codec_controls);


/****************************************************************************
 *
 * aw87xxx firmware cfg load
 *
 ***************************************************************************/
static void aw87xxx_fw_cfg_free(struct aw87xxx *aw87xxx)
{
	AW_DEV_LOGD(aw87xxx->dev, "enter");
	aw_acf_profile_free(aw87xxx->dev, &aw87xxx->acf_info);
	aw_monitor_cfg_free(&aw87xxx->monitor);
}

static int aw87xxx_init_default_prof(struct aw87xxx *aw87xxx)
{
	char *profile = NULL;

	profile = aw_acf_get_prof_off_name(aw87xxx->dev, &aw87xxx->acf_info);
	if (profile == NULL) {
		AW_DEV_LOGE(aw87xxx->dev, "get profile off name failed");
		return -EINVAL;
	}

	snprintf(aw87xxx->prof_off_name, AW_PROFILE_STR_MAX, "%s", profile);
	aw87xxx->current_profile = profile;
	AW_DEV_LOGI(aw87xxx->dev, "init profile name [%s]",
		aw87xxx->current_profile);

	return 0;
}

static void aw87xxx_fw_load_retry(struct aw87xxx *aw87xxx)
{
	struct acf_bin_info *acf_info = &aw87xxx->acf_info;
	int ram_timer_val = 2000;

	AW_DEV_LOGD(aw87xxx->dev, "failed to read [%s]",
			aw87xxx->fw_name);

	if (acf_info->load_count < AW_LOAD_FW_RETRIES) {
		AW_DEV_LOGD(aw87xxx->dev,
			"restart hrtimer to load firmware");
		schedule_delayed_work(&aw87xxx->fw_load_work,
			msecs_to_jiffies(ram_timer_val));
	} else {
		acf_info->load_count = 0;
		AW_DEV_LOGE(aw87xxx->dev,
			"can not load firmware,please check name or file exists");
		return;
	}
	acf_info->load_count++;
}

static void aw87xxx_fw_load(const struct firmware *fw, void *context)
{
	int ret = -1;
	struct aw87xxx *aw87xxx = context;
	struct acf_bin_info *acf_info = &aw87xxx->acf_info;

	AW_DEV_LOGD(aw87xxx->dev, "enter");

	if (!fw) {
		aw87xxx_fw_load_retry(aw87xxx);
		return;
	}

	AW_DEV_LOGD(aw87xxx->dev, "loaded %s - size: %ld",
		aw87xxx->fw_name, (u_long)(fw ? fw->size : 0));

	mutex_lock(&aw87xxx->reg_lock);
	acf_info->fw_data = vmalloc(fw->size);
	if (!acf_info->fw_data) {
		AW_DEV_LOGE(aw87xxx->dev, "fw_data kzalloc memory failed");
		goto exit_vmalloc_failed;
	}
	memset(acf_info->fw_data, 0, fw->size);
	memcpy(acf_info->fw_data, fw->data, fw->size);
	acf_info->fw_size = fw->size;

	ret = aw_acf_parse(aw87xxx->dev, &aw87xxx->acf_info);
	if (ret < 0) {
		AW_DEV_LOGE(aw87xxx->dev, "fw_data parse failed");
		goto exit_acf_parse_failed;
	}

	ret = aw87xxx_init_default_prof(aw87xxx);
	if (ret < 0) {
		aw87xxx_fw_cfg_free(aw87xxx);
		goto exit_acf_parse_failed;
	}

	AW_DEV_LOGI(aw87xxx->dev, "acf parse succeed");
	mutex_unlock(&aw87xxx->reg_lock);
	release_firmware(fw);
	return;

exit_acf_parse_failed:
exit_vmalloc_failed:
	release_firmware(fw);
	mutex_unlock(&aw87xxx->reg_lock);
}

static void aw87xxx_fw_load_work_routine(struct work_struct *work)
{
	struct aw87xxx *aw87xxx = container_of(work,
			struct aw87xxx, fw_load_work.work);
	struct aw_prof_info *prof_info = &aw87xxx->acf_info.prof_info;

	AW_DEV_LOGD(aw87xxx->dev, "enter");

	if (prof_info->status == AW_ACF_WAIT) {
		const struct firmware *fw;
		int ret;

		ret = request_firmware(&fw, aw87xxx->fw_name, aw87xxx->dev);
		if (!ret) {
			AW_DEV_LOGD(aw87xxx->dev, "loader firmware %s success\n", aw87xxx->fw_name);
			aw87xxx_fw_load(fw, aw87xxx);
		}
	}
}

static void aw87xxx_fw_load_init(struct aw87xxx *aw87xxx)
{
#ifdef AW_CFG_UPDATE_DELAY
	int cfg_timer_val = AW_CFG_UPDATE_DELAY_TIMER;
#else
	int cfg_timer_val = 0;
#endif
	AW_DEV_LOGI(aw87xxx->dev, "enter");
	snprintf(aw87xxx->fw_name, AW87XXX_FW_NAME_MAX, "%s", AW87XXX_FW_BIN_NAME);
	aw_acf_init(&aw87xxx->aw_dev, &aw87xxx->acf_info, aw87xxx->dev_index);

	INIT_DELAYED_WORK(&aw87xxx->fw_load_work, aw87xxx_fw_load_work_routine);
	schedule_delayed_work(&aw87xxx->fw_load_work,
			msecs_to_jiffies(cfg_timer_val));
}

/****************************************************************************
 *
 *aw87xxx attribute node
 *
 ****************************************************************************/
static ssize_t aw87xxx_attr_get_reg(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	int ret = 0;
	unsigned int i = 0;
	unsigned char reg_val = 0;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_device *aw_dev = &aw87xxx->aw_dev;

	mutex_lock(&aw87xxx->reg_lock);
	for (i = 0; i < aw_dev->reg_max_addr; i++) {
		if (!(aw_dev->reg_access[i] & AW_DEV_REG_RD_ACCESS))
			continue;
		ret = aw_dev_i2c_read_byte(&aw87xxx->aw_dev, i, &reg_val);
		if (ret < 0) {
			len += snprintf(buf + len, PAGE_SIZE - len,
					"read reg [0x%x] failed\n", i);
			AW_DEV_LOGE(aw87xxx->dev, "read reg [0x%x] failed", i);
		} else {
			len += snprintf(buf + len, PAGE_SIZE - len,
					"reg:0x%02X=0x%02X\n", i, reg_val);
			AW_DEV_LOGD(aw87xxx->dev, "reg:0x%02X=0x%02X",
					i, reg_val);
		}
	}
	mutex_unlock(&aw87xxx->reg_lock);

	return len;
}

static ssize_t aw87xxx_attr_set_reg(struct device *dev,
			struct device_attribute *attr, const char *buf,
			size_t len)
{
	unsigned int databuf[2] = { 0 };
	int ret = 0;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);

	mutex_lock(&aw87xxx->reg_lock);
	if (sscanf(buf, "0x%x 0x%x", &databuf[0], &databuf[1]) == 2) {
		if (databuf[0] >= aw87xxx->aw_dev.reg_max_addr) {
			AW_DEV_LOGE(aw87xxx->dev, "set reg[0x%x] error,is out of reg_addr_max[0x%x]",
				databuf[0], aw87xxx->aw_dev.reg_max_addr);
			mutex_unlock(&aw87xxx->reg_lock);
			return -EINVAL;
		}

		ret = aw_dev_i2c_write_byte(&aw87xxx->aw_dev,
					databuf[0], databuf[1]);
		if (ret < 0)
			AW_DEV_LOGE(aw87xxx->dev, "set [0x%x]=0x%x failed",
				databuf[0], databuf[1]);
		else
			AW_DEV_LOGD(aw87xxx->dev, "set [0x%x]=0x%x succeed",
				databuf[0], databuf[1]);
	} else {
		AW_DEV_LOGE(aw87xxx->dev, "i2c write cmd input error");
	}
	mutex_unlock(&aw87xxx->reg_lock);

	return len;
}

static ssize_t aw87xxx_attr_get_profile(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	unsigned int i = 0;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_prof_info *prof_info = &aw87xxx->acf_info.prof_info;

	if (!prof_info->status) {
		len += snprintf(buf + len, PAGE_SIZE - len,
				"profile_cfg not load\n");
		return len;
	}

	AW_DEV_LOGI(aw87xxx->dev, "current profile:[%s]", aw87xxx->current_profile);

	for (i = 0; i < prof_info->count; i++) {
		if (!strncmp(aw87xxx->current_profile, prof_info->prof_name_list[i],
				AW_PROFILE_STR_MAX))
			len += snprintf(buf + len, PAGE_SIZE - len,
				">%s\n", prof_info->prof_name_list[i]);
		else
			len += snprintf(buf + len, PAGE_SIZE - len,
				" %s\n", prof_info->prof_name_list[i]);
	}

	return len;
}

static ssize_t aw87xxx_attr_set_profile(struct device *dev,
			struct device_attribute *attr, const char *buf,
			size_t len)
{
	char profile[AW_PROFILE_STR_MAX] = {0};
	int ret = 0;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);

	if (sscanf(buf, "%s", profile) == 1) {
		AW_DEV_LOGD(aw87xxx->dev, "set profile [%s]", profile);
		ret = aw87xxx_update_profile(aw87xxx, profile);
		if (ret < 0) {
			AW_DEV_LOGE(aw87xxx->dev, "set profile[%s] failed",
				profile);
			return ret;
		}
	}

	return len;
}

static ssize_t aw87xxx_attr_get_hwen(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	int hwen = aw87xxx->aw_dev.hwen_status;

	if (hwen >= AW_DEV_HWEN_INVALID)
		len += snprintf(buf + len, PAGE_SIZE - len, "hwen_status: invalid\n");
	else if (hwen == AW_DEV_HWEN_ON)
		len += snprintf(buf + len, PAGE_SIZE - len, "hwen_status: on\n");
	else if (hwen == AW_DEV_HWEN_OFF)
		len += snprintf(buf + len, PAGE_SIZE - len, "hwen_status: off\n");

	return len;
}

static ssize_t aw87xxx_attr_set_hwen(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t len)
{
	int ret = -1;
	unsigned int state;
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);

	ret = kstrtouint(buf, 0, &state);
	if (ret) {
		AW_DEV_LOGE(aw87xxx->dev, "fail to channelge str to int");
		return ret;
	}

	mutex_lock(&aw87xxx->reg_lock);
	if (state == AW_DEV_HWEN_OFF)
		aw_dev_hw_pwr_ctrl(&aw87xxx->aw_dev, false); /*OFF*/
	else if (state == AW_DEV_HWEN_ON)
		aw_dev_hw_pwr_ctrl(&aw87xxx->aw_dev, true); /*ON*/
	else
		AW_DEV_LOGE(aw87xxx->dev, "input [%d] error, hwen_on=[%d],hwen_off=[%d]",
			state, AW_DEV_HWEN_ON, AW_DEV_HWEN_OFF);
	mutex_unlock(&aw87xxx->reg_lock);
	return len;
}

int aw87xxx_awrw_write(struct aw87xxx *aw87xxx,
			const char *buf, size_t count)
{
	int i = 0, ret = -1;
	char *data_buf = NULL;
	int buf_len = 0;
	int temp_data = 0;
	int data_str_size = 0;
	char *reg_data;
	struct aw_i2c_packet *packet = &aw87xxx->i2c_packet;

	AW_DEV_LOGD(aw87xxx->dev, "enter");
	/* one addr or one data string Composition of Contains two bytes of symbol(0X)*/
	/* and two byte of hexadecimal data*/
	data_str_size = 2 + 2 * AWRW_DATA_BYTES;

	/* The buf includes the first address of the register to be written and all data */
	buf_len = AWRW_ADDR_BYTES + packet->reg_num * AWRW_DATA_BYTES;
	AW_DEV_LOGI(aw87xxx->dev, "buf_len = %d,reg_num = %d", buf_len, packet->reg_num);
	data_buf = vmalloc(buf_len);
	if (data_buf == NULL) {
		AW_DEV_LOGE(aw87xxx->dev, "alloc memory failed");
		return -ENOMEM;
	}
	memset(data_buf, 0, buf_len);

	data_buf[0] = packet->reg_addr;
	reg_data = data_buf + 1;

	AW_DEV_LOGD(aw87xxx->dev, "reg_addr: 0x%02x", data_buf[0]);

	/*ag:0x00 0x01 0x01 0x01 0x01 0x00\x0a*/
	for (i = 0; i < packet->reg_num; i++) {
		ret = sscanf(buf + AWRW_HDR_LEN + 1 + i * (data_str_size + 1),
			"0x%x", &temp_data);
		if (ret != 1) {
			AW_DEV_LOGE(aw87xxx->dev, "sscanf failed,ret=%d", ret);
			return ret;
		}
		reg_data[i] = temp_data;
		AW_DEV_LOGD(aw87xxx->dev, "[%d] : 0x%02x", i, reg_data[i]);
	}

	mutex_lock(&aw87xxx->reg_lock);
	ret = i2c_master_send(aw87xxx->aw_dev.i2c, data_buf, buf_len);
	if (ret < 0) {
		AW_DEV_LOGE(aw87xxx->dev, "write failed");
		vfree(data_buf);
		data_buf = NULL;
		mutex_unlock(&aw87xxx->reg_lock);
		return -EFAULT;
	}
	mutex_unlock(&aw87xxx->reg_lock);

	vfree(data_buf);
	data_buf = NULL;

	AW_DEV_LOGD(aw87xxx->dev, "down");
	return 0;
}

static int aw87xxx_awrw_data_check(struct aw87xxx *aw87xxx,
			int *data, size_t count)
{
	struct aw_i2c_packet *packet = &aw87xxx->i2c_packet;
	int req_data_len = 0;
	int act_data_len = 0;
	int data_str_size = 0;

	if ((data[AWRW_HDR_ADDR_BYTES] != AWRW_ADDR_BYTES) ||
		(data[AWRW_HDR_DATA_BYTES] != AWRW_DATA_BYTES)) {
		AW_DEV_LOGE(aw87xxx->dev, "addr_bytes [%d] or data_bytes [%d] unsupport",
			data[AWRW_HDR_ADDR_BYTES], data[AWRW_HDR_DATA_BYTES]);
		return -EINVAL;
	}

	/* one data string Composition of Contains two bytes of symbol(0x)*/
	/* and two byte of hexadecimal data*/
	data_str_size = 2 + 2 * AWRW_DATA_BYTES;
	act_data_len = count - AWRW_HDR_LEN - 1;

	/* There is a comma(,) or space between each piece of data */
	if (data[AWRW_HDR_WR_FLAG] == AWRW_FLAG_WRITE) {
		/*ag:0x00 0x01 0x01 0x01 0x01 0x00\x0a*/
		req_data_len = (data_str_size + 1) * packet->reg_num;
		if (req_data_len > act_data_len) {
			AW_DEV_LOGE(aw87xxx->dev, "data_len checkfailed,requeset data_len [%d],actaul data_len [%d]",
				req_data_len, act_data_len);
			return -EINVAL;
		}
	}

	return 0;
}

/* flag addr_bytes data_bytes reg_num reg_addr*/
static int aw87xxx_awrw_parse_buf(struct aw87xxx *aw87xxx,
			const char *buf, size_t count, int *wr_status)
{
	int data[AWRW_HDR_MAX] = {0};
	struct aw_i2c_packet *packet = &aw87xxx->i2c_packet;
	int ret = -1;

	if (sscanf(buf, "0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
		&data[AWRW_HDR_WR_FLAG], &data[AWRW_HDR_ADDR_BYTES],
		&data[AWRW_HDR_DATA_BYTES], &data[AWRW_HDR_REG_NUM],
		&data[AWRW_HDR_REG_ADDR]) == 5) {

		packet->reg_addr = data[AWRW_HDR_REG_ADDR];
		packet->reg_num = data[AWRW_HDR_REG_NUM];
		*wr_status = data[AWRW_HDR_WR_FLAG];
		ret = aw87xxx_awrw_data_check(aw87xxx, data, count);
		if (ret < 0)
			return ret;

		return 0;
	}

	return -EINVAL;
}

static ssize_t aw87xxx_attr_awrw_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_i2c_packet *packet = &aw87xxx->i2c_packet;
	int wr_status = 0;
	int ret = -1;

	if (count < AWRW_HDR_LEN) {
		AW_DEV_LOGE(aw87xxx->dev, "data count too smaller, please check write format");
		AW_DEV_LOGE(aw87xxx->dev, "string %s,count=%ld",
			buf, (u_long)count);
		return -EINVAL;
	}

	AW_DEV_LOGI(aw87xxx->dev, "string:[%s],count=%ld", buf, (u_long)count);
	ret = aw87xxx_awrw_parse_buf(aw87xxx, buf, count, &wr_status);
	if (ret < 0) {
		AW_DEV_LOGE(aw87xxx->dev, "can not parse string");
		return ret;
	}

	if (wr_status == AWRW_FLAG_WRITE) {
		ret = aw87xxx_awrw_write(aw87xxx, buf, count);
		if (ret < 0)
			return ret;
	} else if (wr_status == AWRW_FLAG_READ) {
		packet->status = AWRW_I2C_ST_READ;
		AW_DEV_LOGI(aw87xxx->dev, "read_cmd:reg_addr[0x%02x], reg_num[%d]",
			packet->reg_addr, packet->reg_num);
	} else {
		AW_DEV_LOGE(aw87xxx->dev, "please check str format, unsupport read_write_status: %d",
			wr_status);
		return -EINVAL;
	}

	return count;
}

static ssize_t aw87xxx_attr_awrw_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(dev);
	struct aw_i2c_packet *packet = &aw87xxx->i2c_packet;
	int data_len = 0;
	size_t len = 0;
	int ret = -1, i = 0;
	char *reg_data = NULL;

	if (packet->status != AWRW_I2C_ST_READ) {
		AW_DEV_LOGE(aw87xxx->dev, "please write read cmd first");
		return -EINVAL;
	}

	data_len = AWRW_DATA_BYTES * packet->reg_num;
	reg_data = (char *)vmalloc(data_len);
	if (reg_data == NULL) {
		AW_DEV_LOGE(aw87xxx->dev, "memory alloc failed");
		ret = -EINVAL;
		goto exit;
	}

	mutex_lock(&aw87xxx->reg_lock);
	ret = aw_dev_i2c_read_msg(&aw87xxx->aw_dev, packet->reg_addr,
				(char *)reg_data, data_len);
	if (ret < 0) {
		ret = -EFAULT;
		mutex_unlock(&aw87xxx->reg_lock);
		goto exit;
	}
	mutex_unlock(&aw87xxx->reg_lock);

	AW_DEV_LOGI(aw87xxx->dev, "reg_addr 0x%02x, reg_num %d",
		packet->reg_addr, packet->reg_num);

	for (i = 0; i < data_len; i++) {
		len += snprintf(buf + len, PAGE_SIZE - len,
			"0x%02x,", reg_data[i]);
		AW_DEV_LOGI(aw87xxx->dev, "0x%02x", reg_data[i]);
	}

	ret = len;

exit:
	if (reg_data) {
		vfree(reg_data);
		reg_data = NULL;
	}
	packet->status = AWRW_I2C_ST_NONE;
	return ret;
}

static ssize_t aw87xxx_drv_ver_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
		"driver_ver: %s \n", AW87XXX_DRIVER_VERSION);

	return len;
}

static DEVICE_ATTR(reg, S_IWUSR | S_IRUGO,
		aw87xxx_attr_get_reg, aw87xxx_attr_set_reg);
static DEVICE_ATTR(profile, S_IWUSR | S_IRUGO,
		aw87xxx_attr_get_profile, aw87xxx_attr_set_profile);
static DEVICE_ATTR(hwen, S_IWUSR | S_IRUGO,
		aw87xxx_attr_get_hwen, aw87xxx_attr_set_hwen);
static DEVICE_ATTR(awrw, S_IWUSR | S_IRUGO,
	aw87xxx_attr_awrw_show, aw87xxx_attr_awrw_store);
static DEVICE_ATTR(drv_ver, S_IRUGO, aw87xxx_drv_ver_show, NULL);

static struct attribute *aw87xxx_attributes[] = {
	&dev_attr_reg.attr,
	&dev_attr_profile.attr,
	&dev_attr_hwen.attr,
	&dev_attr_awrw.attr,
	&dev_attr_drv_ver.attr,
	NULL
};

static struct attribute_group aw87xxx_attribute_group = {
	.attrs = aw87xxx_attributes
};

/****************************************************************************
 *
 *aw87xxx device probe
 *
 ****************************************************************************/

int aw87xxx_dtsi_dev_index_check(struct aw87xxx *cur_aw87xxx)
{
	struct list_head *pos = NULL;
	struct aw87xxx *list_aw87xxx = NULL;

	list_for_each(pos, &g_aw87xxx_list) {
		list_aw87xxx = list_entry(pos, struct aw87xxx, list);
		if (list_aw87xxx == NULL)
			continue;

		if (list_aw87xxx->dev_index == cur_aw87xxx->dev_index) {
			AW_DEV_LOGE(cur_aw87xxx->dev, "dev_index has already existing,check failed");
			return -EINVAL;
		}
	}

	return 0;
}

static int aw87xxx_dtsi_parse(struct aw87xxx *aw87xxx,
				struct device_node *dev_node)
{
	int ret = -1;
	int32_t dev_index = -EINVAL;

	ret = of_property_read_u32(dev_node, "dev_index", &dev_index);
	if (ret < 0) {
		AW_DEV_LOGI(aw87xxx->dev, "dev_index parse failed, user default[%d], ret=%d",
				g_aw87xxx_dev_cnt, ret);
		aw87xxx->dev_index = g_aw87xxx_dev_cnt;
	} else {
		aw87xxx->dev_index = dev_index;
		AW_DEV_LOGI(aw87xxx->dev, "parse dev_index=[%d]",
				aw87xxx->dev_index);
	}

	ret = aw87xxx_dtsi_dev_index_check(aw87xxx);
	if (ret < 0)
		return ret;

	ret = of_get_named_gpio(dev_node, "reset-gpio", 0);
	if (ret < 0) {
		AW_DEV_LOGI(aw87xxx->dev, "no reset gpio provided, hardware reset unavailable");
		aw87xxx->aw_dev.rst_gpio = AW_NO_RESET_GPIO;
		aw87xxx->aw_dev.hwen_status = AW_DEV_HWEN_INVALID;
	} else {
		aw87xxx->aw_dev.rst_gpio = ret;
		aw87xxx->aw_dev.hwen_status = AW_DEV_HWEN_OFF;
		AW_DEV_LOGI(aw87xxx->dev, "reset gpio[%d] parse succeed", ret);
		if (gpio_is_valid(aw87xxx->aw_dev.rst_gpio)) {
			ret = devm_gpio_request_one(aw87xxx->dev,
					aw87xxx->aw_dev.rst_gpio,
					GPIOF_OUT_INIT_LOW, "aw87xxx_reset");
			if (ret < 0) {
				AW_DEV_LOGE(aw87xxx->dev, "reset request failed");
				return ret;
			}
		}
		return 0;
	}

	/* In order to compatible with stereo share only one reset gpio */
	ret = of_get_named_gpio(dev_node, "reset-shared-gpio", 0);
	if (ret < 0) {
		AW_DEV_LOGI(aw87xxx->dev, "no reset shared gpio provided, hardware reset unavailable");
		aw87xxx->aw_dev.rst_shared_gpio = AW_NO_RESET_GPIO;
	} else {
		aw87xxx->aw_dev.rst_shared_gpio = ret;
		AW_DEV_LOGI(aw87xxx->dev, "reset shared gpio[%d] parse succeed", ret);
		if (gpio_is_valid(aw87xxx->aw_dev.rst_shared_gpio)) {
			ret = devm_gpio_request_one(aw87xxx->dev,
					aw87xxx->aw_dev.rst_shared_gpio,
					GPIOF_OUT_INIT_LOW, "aw87xxx_reset-shared");
			if (ret < 0) {
				AW_DEV_LOGE(aw87xxx->dev, "aw87xxx_reset-shared reset request failed");
				return ret;
			}
			msleep(20);
			gpio_set_value_cansleep(aw87xxx->aw_dev.rst_shared_gpio, AW_GPIO_HIGHT_LEVEL);
		}
	}
	return 0;
}

static struct aw87xxx *aw87xxx_malloc_init(struct i2c_client *client)
{
	struct aw87xxx *aw87xxx = NULL;

	aw87xxx = devm_kzalloc(&client->dev, sizeof(struct aw87xxx),
			GFP_KERNEL);
	if (aw87xxx == NULL) {
		AW_DEV_LOGE(&client->dev, "failed to devm_kzalloc aw87xxx");
		return NULL;
	}
	memset(aw87xxx, 0, sizeof(struct aw87xxx));

	aw87xxx->dev = &client->dev;
	aw87xxx->aw_dev.dev = &client->dev;
	aw87xxx->aw_dev.i2c_bus = client->adapter->nr;
	aw87xxx->aw_dev.i2c_addr = client->addr;
	aw87xxx->aw_dev.i2c = client;
	aw87xxx->aw_dev.hwen_status = false;
	aw87xxx->aw_dev.reg_access = NULL;
	aw87xxx->aw_dev.hwen_status = AW_DEV_HWEN_INVALID;
	aw87xxx->off_bin_status = AW87XXX_NO_OFF_BIN;
	aw87xxx->codec = NULL;
	aw87xxx->current_profile = aw87xxx->prof_off_name;

	mutex_init(&aw87xxx->reg_lock);

	AW_DEV_LOGI(&client->dev, "struct aw87xxx devm_kzalloc and init down");
	return aw87xxx;
}

static int aw87xxx_probe(struct snd_soc_component *component)
{
	struct aw87xxx *aw87xxx = dev_get_drvdata(component->dev);
	int ret;

	ret = aw87xxx_kcontrol_dynamic_create(aw87xxx, component);
	if (ret < 0) {
		dev_err(component->dev, "%s: aw87xxx_add_codec_controls failed, ret= %d\n",
			__func__, ret);
		return ret;
	};
	return 0;
}

static const struct snd_soc_component_driver aw87xxx_component_driver = {
	.probe = aw87xxx_probe,
};

static int aw87xxx_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct device_node *dev_node = client->dev.of_node;
	struct aw87xxx *aw87xxx = NULL;
	int ret = -1;

	dev_info(&client->dev, "%s\n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		AW_DEV_LOGE(&client->dev, "check_functionality failed");
		return -ENODEV;
	}

	/* aw87xxx i2c_dev struct init */
	aw87xxx = aw87xxx_malloc_init(client);
	if (aw87xxx == NULL)
		return -ENOMEM;

	i2c_set_clientdata(client, aw87xxx);

	/* aw87xxx dev_node parse */
	ret = aw87xxx_dtsi_parse(aw87xxx, dev_node);
	if (ret < 0)
		goto exit;

	/*hw power on PA*/
	aw_dev_hw_pwr_ctrl(&aw87xxx->aw_dev, true);

	/* aw87xxx devices private attributes init */
	ret = aw_dev_init(&aw87xxx->aw_dev);
	if (ret < 0)
		goto exit;

	ret = devm_snd_soc_register_component(aw87xxx->dev, &aw87xxx_component_driver,
					      NULL, 0);
	if (ret < 0) {
		dev_err(aw87xxx->dev, "%s() register codec error %d\n",
			__func__, ret);
		goto exit;
	}

	/*product register reset */
	aw_dev_soft_reset(&aw87xxx->aw_dev);

	/*hw power off */
	aw_dev_hw_pwr_ctrl(&aw87xxx->aw_dev, false);

	/* create debug attrbute nodes */
	ret = sysfs_create_group(&aw87xxx->dev->kobj, &aw87xxx_attribute_group);
	if (ret < 0)
		AW_DEV_LOGE(aw87xxx->dev, "failed to create sysfs nodes, will not allowed to use");

	/* cfg_load init */
	aw87xxx_fw_load_init(aw87xxx);

	/*monitor init*/
	aw_monitor_init(aw87xxx->dev, &aw87xxx->monitor, dev_node);

	/*add device to total list */
	mutex_lock(&g_aw87xxx_mutex_lock);
	g_aw87xxx_dev_cnt++;
	list_add(&aw87xxx->list, &g_aw87xxx_list);
	mutex_unlock(&g_aw87xxx_mutex_lock);

	AW_DEV_LOGI(aw87xxx->dev, "succeed");

	return 0;
exit:
	AW_DEV_LOGE(aw87xxx->dev, "pa init failed");
	return ret;
}

static int aw87xxx_i2c_remove(struct i2c_client *client)
{
	struct aw87xxx *aw87xxx = i2c_get_clientdata(client);

	aw_monitor_exit(&aw87xxx->monitor);

	/*rm attr node*/
	sysfs_remove_group(&aw87xxx->dev->kobj, &aw87xxx_attribute_group);

	aw87xxx_fw_cfg_free(aw87xxx);

	mutex_lock(&g_aw87xxx_mutex_lock);
	g_aw87xxx_dev_cnt--;
	list_del(&aw87xxx->list);
	mutex_unlock(&g_aw87xxx_mutex_lock);

	return 0;
}

static void aw87xxx_i2c_shutdown(struct i2c_client *client)
{
	struct aw87xxx *aw87xxx = i2c_get_clientdata(client);

	AW_DEV_LOGI(&client->dev, "enter");

	/*soft and hw power off*/
	aw87xxx_update_profile(aw87xxx, aw87xxx->prof_off_name);
}


static const struct i2c_device_id aw87xxx_i2c_id[] = {
	{AW87XXX_I2C_NAME, 0},
	{},
};

static const struct of_device_id extpa_of_match[] = {
	{.compatible = "awinic,aw87xxx_pa"},
	{},
};

static struct i2c_driver aw87xxx_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = AW87XXX_I2C_NAME,
		.of_match_table = extpa_of_match,
		},
	.probe = aw87xxx_i2c_probe,
	.remove = aw87xxx_i2c_remove,
	.shutdown = aw87xxx_i2c_shutdown,
	.id_table = aw87xxx_i2c_id,
};

static int __init aw87xxx_pa_init(void)
{
	int ret;

	AW_LOGI("driver version: %s", AW87XXX_DRIVER_VERSION);

	ret = i2c_add_driver(&aw87xxx_i2c_driver);
	if (ret < 0) {
		AW_LOGE("Unable to register driver, ret= %d", ret);
		return ret;
	}
	return 0;
}

static void __exit aw87xxx_pa_exit(void)
{
	AW_LOGI("enter");
	i2c_del_driver(&aw87xxx_i2c_driver);
}

module_init(aw87xxx_pa_init);
module_exit(aw87xxx_pa_exit);

MODULE_AUTHOR("<zhaozhongbo@awinic.com>");
MODULE_DESCRIPTION("awinic aw87xxx pa driver");
MODULE_LICENSE("GPL v2");
