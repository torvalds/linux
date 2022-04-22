// SPDX-License-Identifier: GPL-2.0+
/*
 * aw_spin.c   aw883xx spin module
 *
 * Copyright (c) 2020 AWINIC Technology CO., LTD
 *
 *  Author: Bruce zhao <zhaolei@awinic.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <sound/soc.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/syscalls.h>
#include <sound/control.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include "aw_spin.h"
#include "aw_device.h"
#include "aw883xx.h"
#include "aw_log.h"

static DEFINE_MUTEX(g_aw_spin_lock);

static unsigned int g_spin_angle = AW_SPIN_0;
static unsigned int g_spin_mode = AW_SPIN_OFF_MODE;

static const char *const aw_spin[] = {"spin_0", "spin_90",
					"spin_180", "spin_270"};

#ifdef AW_MTK_PLATFORM_SPIN
extern int mtk_spk_send_ipi_buf_to_dsp(void *data_buffer, uint32_t data_size);
extern int mtk_spk_recv_ipi_buf_from_dsp(int8_t *buffer, int16_t size, uint32_t *buf_len);
#elif defined AW_QCOM_PLATFORM_SPIN
extern int afe_get_topology(int port_id);
extern int aw_send_afe_cal_apr(uint32_t param_id,
	void *buf, int cmd_size, bool write);
#else
static int aw_send_afe_cal_apr(uint32_t param_id,
		void *buf, int cmd_size, bool write)
{
	return 0;
}

static int afe_get_topology(int port_id)
{
	return 0;
}
#endif
static int aw_get_msg_id(int dev_ch, uint32_t *msg_id)
{
	switch (dev_ch) {
	case AW_DEV_CH_PRI_L:
		*msg_id = AFE_MSG_ID_MSG_0;
		break;
	case AW_DEV_CH_PRI_R:
		*msg_id = AFE_MSG_ID_MSG_0;
		break;
	case AW_DEV_CH_SEC_L:
		*msg_id = AFE_MSG_ID_MSG_1;
		break;
	case AW_DEV_CH_SEC_R:
		*msg_id = AFE_MSG_ID_MSG_1;
		break;
	default:
		pr_err("%s: can not find msg num, channel %d ", __func__, dev_ch);
		return -EINVAL;
	}

	pr_debug("%s: msg id[%d] ", __func__, *msg_id);
	return 0;
}

#ifdef AW_MTK_PLATFORM_SPIN
static int aw_mtk_write_data_to_dsp(int msg_id, void *data, int size)
{
	int32_t *dsp_data = NULL;
	struct aw_msg_hdr *hdr = NULL;
	int ret;

	dsp_data = kzalloc(sizeof(struct aw_msg_hdr) + size, GFP_KERNEL);
	if (!dsp_data) {
		pr_err("%s: kzalloc dsp_msg error\n", __func__);
		return -ENOMEM;
	}

	hdr = (struct aw_msg_hdr *)dsp_data;
	hdr->type = AW_DSP_MSG_TYPE_DATA;
	hdr->opcode_id = msg_id;
	hdr->version = AW_DSP_MSG_HDR_VER;

	memcpy(((char *)dsp_data) + sizeof(struct aw_msg_hdr), data, size);

	ret = mtk_spk_send_ipi_buf_to_dsp(dsp_data,
				sizeof(struct aw_msg_hdr) + size);
	if (ret < 0) {
		pr_err("%s: write data failed\n", __func__);
		kfree(dsp_data);
		dsp_data = NULL;
		return ret;
	}

	kfree(dsp_data);
	dsp_data = NULL;
	return 0;
}

static int aw_mtk_set_spin_angle(struct aw_device *aw_dev, uint32_t spin_angle)
{
	int ret;

	ret = aw_mtk_write_data_to_dsp(AW_MSG_ID_SPIN, &spin_angle, sizeof(uint32_t));
	if (ret)
		aw_dev_err(aw_dev->dev, "write data to dsp failed");

	return ret;
}

static int aw_mtk_get_spin_angle(void *spin_angle, int size)
{
	int ret;
	struct aw_msg_hdr hdr;

	hdr.type = AW_DSP_MSG_TYPE_CMD;
	hdr.opcode_id = AW_MSG_ID_SPIN;
	hdr.version = AW_DSP_MSG_HDR_VER;

	ret = mtk_spk_send_ipi_buf_to_dsp(&hdr, sizeof(struct aw_msg_hdr));
	if (ret < 0) {
		pr_err("%s:send cmd failed\n", __func__);
		return ret;
	}

	ret = mtk_spk_recv_ipi_buf_from_dsp(spin_angle, size, &size);
	if (ret < 0) {
		pr_err("%s:get data failed\n", __func__);
		return ret;
	}
	return 0;
}

static int aw_mtk_set_mixer_en(struct aw_device *aw_dev, uint32_t msg_id, int32_t is_enable)
{
	int32_t *dsp_msg = NULL;
	struct aw_msg_hdr *hdr = NULL;
	int ret;

	dsp_msg = kzalloc(sizeof(struct aw_msg_hdr) + sizeof(int32_t), GFP_KERNEL);
	if (!dsp_msg) {
		aw_dev_err(aw_dev->dev, "kzalloc dsp_msg error");
		return -ENOMEM;
	}
	hdr = (struct aw_msg_hdr *)dsp_msg;
	hdr->type = AW_DSP_MSG_TYPE_DATA;
	hdr->opcode_id = AW_INLINE_ID_AUDIO_MIX;
	hdr->version = AW_DSP_MSG_HDR_VER;

	memcpy(((char *)dsp_msg) + sizeof(struct aw_msg_hdr),
				(char *)&is_enable, sizeof(int32_t));

	ret = aw_mtk_write_data_to_dsp(msg_id, (void *)dsp_msg,
				sizeof(struct aw_msg_hdr) + sizeof(int32_t));
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, " write data failed");
		kfree(dsp_msg);
		dsp_msg = NULL;
		return ret;
	}

	kfree(dsp_msg);
	dsp_msg = NULL;
	return 0;
}
#else
static int aw_check_dsp_ready(void)
{
	int ret;

	ret = afe_get_topology(AW_RX_PORT_ID);

	pr_debug("topo_id 0x%x ", ret);

	if (ret != AW_RX_TOPO_ID)
		return false;
	else
		return true;
}

static int aw_qcom_write_data_to_dsp(uint32_t msg_id, void *data, int size)
{
	int ret;
	int try = 0;

	while (try < AW_DSP_TRY_TIME) {
		if (aw_check_dsp_ready()) {
			ret = aw_send_afe_cal_apr(msg_id, data, size, true);
			return ret;
		} else {
			try++;
			usleep_range(AW_10000_US, AW_10000_US + 10);
			pr_info("%s: afe topo not ready try again\n", __func__);
		}
	}

	return -EINVAL;
}

static int aw_qcom_read_data_from_dsp(uint32_t msg_id, void *data, int size)
{
	int ret;
	int try = 0;

	while (try < AW_DSP_TRY_TIME) {
		if (aw_check_dsp_ready()) {
			ret = aw_send_afe_cal_apr(msg_id, data, size, false);
			return ret;
		} else {
			try++;
			usleep_range(AW_10000_US, AW_10000_US + 10);
			pr_info("%s: afe topo not ready try again\n", __func__);
		}
	}
	return -EINVAL;
}

static int aw_qcom_set_spin_angle(struct aw_device *aw_dev,
					uint32_t spin_angle)
{
	int ret;

	ret = aw_qcom_write_data_to_dsp(AW_MSG_ID_SPIN, &spin_angle, sizeof(uint32_t));
	if (ret)
		aw_dev_err(aw_dev->dev, "write spin angle to dsp failed");
	else
		aw_dev_info(aw_dev->dev, "write spin angle to dsp successful");

	return ret;
}

static int aw_qcom_get_spin_angle(uint32_t *spin_angle, int size)
{
	int ret;

	ret = aw_qcom_read_data_from_dsp(AW_MSG_ID_SPIN, spin_angle, size);
	if (ret)
		pr_err("%s: get spin angle failed\n", __func__);
	else
		pr_info("%s: get spin angle successful\n", __func__);

	return ret;
}

static int aw_qcom_set_mixer_en(struct aw_device *aw_dev,
						uint32_t msg_id, int32_t is_enable)
{
	int32_t *dsp_msg;
	int ret = 0;
	int msg_len = (int)(sizeof(struct aw_msg_hdr) + sizeof(int32_t));

	dsp_msg = kzalloc(msg_len, GFP_KERNEL);
	if (!dsp_msg) {
		aw_dev_err(aw_dev->dev, "kzalloc dsp_msg error");
		return -ENOMEM;

	}
	dsp_msg[0] = AW_DSP_MSG_TYPE_DATA;
	dsp_msg[1] = AW_INLINE_ID_AUDIO_MIX;
	dsp_msg[2] = AW_DSP_MSG_HDR_VER;

	memcpy(dsp_msg + (sizeof(struct aw_msg_hdr) / sizeof(int32_t)),
		(char *)&is_enable, sizeof(int32_t));

	ret = aw_qcom_write_data_to_dsp(msg_id, (void *)dsp_msg, msg_len);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "write data to dsp failed");
		kfree(dsp_msg);
		return ret;
	}

	aw_dev_dbg(aw_dev->dev, "write data[%d] to dsp success", msg_len);
	kfree(dsp_msg);
	return 0;
}
#endif

/***********************************spin_angle**********************************/
static int aw_set_adsp_spin_angle(struct aw_device *aw_dev, uint32_t spin_angle)
{
	if (spin_angle >= AW_SPIN_MAX) {
		aw_dev_err(aw_dev->dev, "spin_angle:%d not support",
				spin_angle);
		return -EINVAL;
	}

#ifdef AW_MTK_PLATFORM_SPIN
	return aw_mtk_set_spin_angle(aw_dev, spin_angle);
#else
	return aw_qcom_set_spin_angle(aw_dev, spin_angle);
#endif
}

static void aw_get_adsp_spin_angle(uint32_t *spin_angle)
{
#ifdef AW_MTK_PLATFORM_SPIN
	aw_mtk_get_spin_angle(spin_angle, sizeof(uint32_t));
#else
	aw_qcom_get_spin_angle(spin_angle, sizeof(uint32_t));
#endif
}
/*******************************************************************************/

/**********************************mixer_status*********************************/
static int aw_set_mixer_en(struct aw_device *aw_dev, int32_t is_enable)
{
	int ret;
	uint32_t msg_id;

	ret = aw_get_msg_id(aw_dev->channel, &msg_id);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "get msg_num failed");
		return ret;
	}

#ifdef AW_MTK_PLATFORM_SPIN
	ret = aw_mtk_set_mixer_en(aw_dev, msg_id, is_enable);
#else
	ret = aw_qcom_set_mixer_en(aw_dev, msg_id, is_enable);
#endif
	if (ret)
		aw_dev_err(aw_dev->dev, "set mixer status failed");

	return ret;
}

int aw_hold_reg_spin_st(struct aw_spin_desc *spin_desc)
{
	struct aw_device *aw_dev = container_of(spin_desc,
						struct aw_device, spin_desc);
	uint16_t reg_val;

	if (aw_dev == NULL) {
		aw_pr_err("aw_dev is NULL");
		return -EINVAL;
	}

	mutex_lock(&g_aw_spin_lock);
	if ((g_spin_mode == AW_REG_SPIN_MODE) ||
		(g_spin_mode == AW_REG_MIXER_SPIN_MODE)) {
		/*set rx*/
		aw_dev->ops.aw_reg_read(aw_dev,
			aw_dev->chansel_desc.rxchan_reg, &reg_val);
		reg_val &= aw_dev->chansel_desc.rxchan_mask;
		reg_val |= spin_desc->spin_table[g_spin_angle].rx_val;
		aw_dev->ops.aw_reg_write(aw_dev,
			aw_dev->chansel_desc.rxchan_reg, reg_val);

		/*set tx*/
		aw_dev->ops.aw_reg_read(aw_dev,
			aw_dev->chansel_desc.txchan_reg, &reg_val);
		reg_val &= aw_dev->chansel_desc.txchan_mask;
		reg_val |= spin_desc->spin_table[g_spin_angle].tx_val;
		aw_dev->ops.aw_reg_write(aw_dev,
			aw_dev->chansel_desc.txchan_reg, reg_val);
	}
	mutex_unlock(&g_aw_spin_lock);

	return 0;
}

int aw_check_spin_mode(struct aw_spin_desc *spin_desc)
{
	struct list_head *pos = NULL;
	struct list_head *dev_list = NULL;
	struct aw_device *local_pa = NULL;
	int ret = -1;
	int spin_mode = AW_SPIN_OFF_MODE;
	struct aw_device *aw_dev = container_of(spin_desc,
						struct aw_device, spin_desc);
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev->private_data;

	if (g_spin_mode == AW_SPIN_OFF_MODE) {
		aw883xx->spin_flag = AW_SPIN_OFF;
		return 0;
	}

	ret = aw_dev_get_list_head(&dev_list);
	if (ret) {
		aw_pr_err("get dev list failed");
		return ret;
	}

	list_for_each(pos, dev_list) {
		local_pa = container_of(pos, struct aw_device, list_node);
		spin_mode = local_pa->spin_desc.spin_mode;
		if (g_spin_mode != spin_mode) {
			aw_pr_err("dev[%d] spin mode:%d not equal g_spin_mode:%d, check failed",
				local_pa->channel, spin_mode, g_spin_mode);
			aw883xx->spin_flag = AW_SPIN_OFF;
			return -EINVAL;
		}
	}
	aw883xx->spin_flag = AW_SPIN_ON;

	return 0;
}

int aw_hold_dsp_spin_st(struct aw_spin_desc *spin_desc)
{
	struct aw_device *aw_dev = container_of(spin_desc,
						struct aw_device, spin_desc);
	int ret = -1;

	if (aw_dev == NULL) {
		aw_pr_err("aw_dev is NULL");
		return -EINVAL;
	}

	if (aw_dev->channel == 0) {
		if (g_spin_mode == AW_ADSP_SPIN_MODE) {
			ret = aw_set_adsp_spin_angle(aw_dev,
							g_spin_angle);
			if (ret < 0)
				return ret;
		}
	}

	return ret;
}

static int aw_set_channal_mode(struct aw_device *aw_pa,
					uint32_t spin_angle)
{
	int ret;
	struct aw_chansel_desc *chansel_desc = &aw_pa->chansel_desc;
	struct aw_spin_ch *spin_ch = &aw_pa->spin_desc.spin_table[spin_angle];
	ret = aw_pa->ops.aw_reg_write_bits(aw_pa, chansel_desc->rxchan_reg,
				chansel_desc->rxchan_mask, spin_ch->rx_val);
	if (ret < 0) {
		aw_dev_err(aw_pa->dev, "set rx failed");
		return ret;
	}

	ret = aw_pa->ops.aw_reg_write_bits(aw_pa, chansel_desc->txchan_reg,
				chansel_desc->txchan_mask, spin_ch->tx_val);
	if (ret < 0) {
		aw_dev_err(aw_pa->dev, "set tx failed");
		return ret;
	}

	aw_dev_dbg(aw_pa->dev, "set channel mode done!");

	return 0;
}

static int aw_set_reg_spin_angle(struct aw883xx *aw883xx, uint32_t spin_angle)
{
	struct list_head *pos = NULL;
	struct list_head *dev_list = NULL;
	struct aw_device *local_pa = NULL;
	int ret;

	if (spin_angle >= ARRAY_SIZE(aw_spin)) {
		aw_dev_err(aw883xx->dev, "spin_angle:%d not support",
					spin_angle);
		return -EINVAL;
	}

	ret = aw_dev_get_list_head(&dev_list);
	if (ret) {
		aw_dev_err(aw883xx->dev, "get dev list failed");
		return ret;
	}

	list_for_each(pos, dev_list) {
		local_pa = container_of(pos, struct aw_device, list_node);
		ret = aw_set_channal_mode(local_pa, spin_angle);
		if (ret < 0) {
			aw_dev_err(aw883xx->dev, "set channal mode failed");
			return ret;
		}
	}

	return 0;
}

static int aw_set_reg_mixer_spin_angle(struct aw883xx *aw883xx, uint32_t spin_angle)
{
	int ret;

	if (spin_angle >= ARRAY_SIZE(aw_spin)) {
		aw_dev_err(aw883xx->dev, "spin_angle:%d not support",
					spin_angle);
		return -EINVAL;
	}

	ret = aw_set_mixer_en(aw883xx->aw_pa, AW_AUDIO_MIX_ENABLE);
	if (ret)
		return ret;

	usleep_range(AW_100000_US, AW_100000_US + 10);

	aw_set_reg_spin_angle(aw883xx, spin_angle);

	ret = aw_set_mixer_en(aw883xx->aw_pa, AW_AUDIO_MIX_DISABLE);
	if (ret)
		return ret;

	return ret;
}

static void aw_get_reg_spin_angle(uint32_t *spin_angle)
{
	*spin_angle = g_spin_angle;

	pr_debug("%s: get spin:%s\n", __func__, aw_spin[g_spin_angle]);
}

static int aw_set_spin_angle(struct aw883xx *aw883xx, uint32_t spin_angle)
{
	switch (g_spin_mode) {
	case AW_REG_SPIN_MODE:
		return aw_set_reg_spin_angle(aw883xx, spin_angle);
	case AW_ADSP_SPIN_MODE:
		return aw_set_adsp_spin_angle(aw883xx->aw_pa, spin_angle);
	case AW_REG_MIXER_SPIN_MODE:
		return aw_set_reg_mixer_spin_angle(aw883xx, spin_angle);
	default:
		aw_pr_err("unsupported spin mode:%d", g_spin_mode);
		return -EINVAL;
	}
}

static void aw_set_spin_mode(int mode)
{
	g_spin_mode = mode;
}

static int aw_set_spin(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct aw883xx *aw883xx = (struct aw883xx *)kcontrol->private_value;
	uint32_t ctrl_value;
	int ret;

	aw_dev_dbg(aw883xx->dev, "ucontrol->value.integer.value[0]=%ld",
			ucontrol->value.integer.value[0]);

	if (aw883xx->spin_flag == AW_SPIN_OFF) {
		aw_dev_dbg(aw883xx->dev, "spin func not enable");
		return 0;
	}

	ctrl_value = ucontrol->value.integer.value[0];

	mutex_lock(&g_aw_spin_lock);
	if (aw883xx->pstream == AW883XX_STREAM_OPEN) {
		ret = aw_set_spin_angle(aw883xx, ctrl_value);
		if (ret < 0)
			aw_dev_err(aw883xx->dev, "set spin error, ret=%d\n",
				ret);
	} else {
		if ((g_spin_mode == AW_REG_SPIN_MODE) || (g_spin_mode == AW_REG_MIXER_SPIN_MODE))
			aw_set_reg_spin_angle(aw883xx, ctrl_value);
		else
			aw_dev_info(aw883xx->dev, "stream no start only record spin angle");
	}
	g_spin_angle = ctrl_value;
	mutex_unlock(&g_aw_spin_lock);

	return 0;
}

static void aw_get_spin_angle(uint32_t *spin_angle)
{
	if ((g_spin_mode == AW_REG_SPIN_MODE) || (g_spin_mode == AW_REG_MIXER_SPIN_MODE))
		aw_get_reg_spin_angle(spin_angle);
	else if (g_spin_mode == AW_ADSP_SPIN_MODE)
		aw_get_adsp_spin_angle(spin_angle);
}

static int aw_get_spin(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct aw883xx *aw883xx = (struct aw883xx *)kcontrol->private_value;
	uint32_t ctrl_value = 0;

	mutex_lock(&g_aw_spin_lock);
	if (aw883xx->pstream == AW883XX_STREAM_OPEN) {
		aw_get_spin_angle(&ctrl_value);
		ucontrol->value.integer.value[0] = ctrl_value;
	} else {
		ucontrol->value.integer.value[0] = g_spin_angle;
		aw_dev_dbg(aw883xx->dev, "no stream, use record value");
	}
	mutex_unlock(&g_aw_spin_lock);

	return 0;
}

static int aw_spin_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	struct aw883xx *aw883xx = (struct aw883xx *)kcontrol->private_value;
	int count = 0;

	if (aw883xx == NULL) {
		aw_pr_err("get struct aw883xx failed");
		return -EINVAL;
	}

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	count = ARRAY_SIZE(aw_spin);

	uinfo->value.enumerated.items = count;
		if (uinfo->value.enumerated.item >= count)
			uinfo->value.enumerated.item = count - 1;

	strlcpy(uinfo->value.enumerated.name,
			aw_spin[uinfo->value.enumerated.item],
			strlen(aw_spin[uinfo->value.enumerated.item]) + 1);

	return 0;
}

static int aw_spin_control_create(struct aw883xx *aw883xx)
{
	int kcontrol_num = 1;
	struct snd_kcontrol_new *aw_spin_control = NULL;
	char *kctl_name = NULL;

	aw_spin_control = devm_kzalloc(aw883xx->codec->dev,
			sizeof(struct snd_kcontrol_new) * 1, GFP_KERNEL);
	if (aw_spin_control == NULL) {
		aw_dev_err(aw883xx->codec->dev, "kcontrol malloc failed!");
		return -ENOMEM;
	}

	kctl_name = devm_kzalloc(aw883xx->codec->dev, AW_NAME_BUF_MAX, GFP_KERNEL);
	if (kctl_name == NULL)
		return -ENOMEM;

	snprintf(kctl_name, AW_NAME_BUF_MAX, "aw_spin_switch");

	aw_spin_control[0].name = kctl_name;
	aw_spin_control[0].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	aw_spin_control[0].info = aw_spin_info;
	aw_spin_control[0].get = aw_get_spin;
	aw_spin_control[0].put = aw_set_spin;
	aw_spin_control[0].private_value = (unsigned long)aw883xx;

	kctl_name = devm_kzalloc(aw883xx->codec->dev, AW_NAME_BUF_MAX, GFP_KERNEL);
	if (!kctl_name)
		return -ENOMEM;

	aw883xx->codec_ops->add_codec_controls(aw883xx->codec,
		aw_spin_control, kcontrol_num);

	return 0;
}

void aw_add_spin_controls(void *aw_dev)
{
	struct aw883xx *aw883xx = (struct aw883xx *)aw_dev;

	if (aw883xx->aw_pa->spin_desc.spin_mode != AW_SPIN_OFF_MODE)
		aw_spin_control_create(aw883xx);
}

static int aw_parse_spin_table_dt(struct aw_device *aw_dev,
					struct device_node *np)
{
	int ret = -1;
	const char *str_data = NULL;
	char spin_table_str[AW_SPIN_MAX] = { 0 };
	int i, spin_count = 0;

	ret = of_property_read_string(np, "spin-data", &str_data);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "get spin_data failed, close spin function");
		return ret;
	}

	ret = sscanf(str_data, "%c %c %c %c",
				&spin_table_str[AW_SPIN_0], &spin_table_str[AW_SPIN_90],
				&spin_table_str[AW_SPIN_180], &spin_table_str[AW_SPIN_270]);
	if  (ret != AW_SPIN_MAX) {
		aw_dev_err(aw_dev->dev, "unsupported str:%s, close spin function",
				str_data);
		return -EINVAL;
	}

	for (i = 0; i < AW_SPIN_MAX; i++) {
		if (spin_table_str[i] == 'l' || spin_table_str[i] == 'L') {
			aw_dev->spin_desc.spin_table[i].rx_val = aw_dev->chansel_desc.rx_left;
			aw_dev->spin_desc.spin_table[i].tx_val = aw_dev->chansel_desc.tx_left;
			spin_count++;
		} else if (spin_table_str[i] == 'r' || spin_table_str[i] == 'R') {
			aw_dev->spin_desc.spin_table[i].rx_val = aw_dev->chansel_desc.rx_right;
			aw_dev->spin_desc.spin_table[i].tx_val = aw_dev->chansel_desc.tx_right;
			spin_count++;
		} else {
			aw_dev_err(aw_dev->dev, "unsupported str:%s, close spin function",
				str_data);
			return -EINVAL;
		}
	}

	if (spin_count != ARRAY_SIZE(aw_spin)) {
		aw_dev_err(aw_dev->dev, "get spin_data failed, spin_count:%d", spin_count);
		return -EINVAL;
	}

	return 0;
}

static int aw_parse_spin_mode_dt(struct aw_device *aw_dev)
{
	int ret = -1;
	const char *spin_mode = NULL;
	int mode;
	struct device_node *np = aw_dev->dev->of_node;

	ret = of_property_read_string(np, "spin-mode", &spin_mode);
	if (ret < 0) {
		aw_dev_info(aw_dev->dev,
			"spin-mode get failed, spin switch off");
		aw_dev->spin_desc.spin_mode = AW_SPIN_OFF_MODE;
		return 0;
	}

	if (!strcmp(spin_mode, "dsp_spin"))
		mode = AW_ADSP_SPIN_MODE;
	else if (!strcmp(spin_mode, "reg_spin"))
		mode = AW_REG_SPIN_MODE;
	else if (!strcmp(spin_mode, "reg_mixer_spin"))
		mode = AW_REG_MIXER_SPIN_MODE;
	else
		mode = AW_SPIN_OFF_MODE;

	aw_dev->spin_desc.spin_mode = mode;

	aw_set_spin_mode(mode);

	if ((mode == AW_REG_SPIN_MODE) || (mode == AW_REG_MIXER_SPIN_MODE)) {
		ret = aw_parse_spin_table_dt(aw_dev, np);
		if (ret < 0) {
			aw_dev->spin_desc.spin_mode = AW_SPIN_OFF_MODE;
			aw_dev_err(aw_dev->dev,
				"spin-table get failed, ret = %d", ret);
			return ret;
		}
	}

	aw_dev_info(aw_dev->dev, "spin mode is %d", mode);

	return 0;
}

void aw_spin_init(struct aw_spin_desc *spin_desc)
{
	struct aw_device *aw_dev = container_of(spin_desc,
					struct aw_device, spin_desc);

	aw_parse_spin_mode_dt(aw_dev);
}

