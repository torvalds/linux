/*
 * aw87xxx_dsp.c
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

#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/hrtimer.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include "aw_log.h"
#include "aw_dsp.h"

static DEFINE_MUTEX(g_dsp_lock);

#ifdef AW_MTK_OPEN_DSP_PLATFORM
extern int mtk_spk_send_ipi_buf_to_dsp(void *data_buffer,
				uint32_t data_size);
extern int mtk_spk_recv_ipi_buf_from_dsp(int8_t *buffer,
				int16_t size, uint32_t *buf_len);
/*
static int mtk_spk_send_ipi_buf_to_dsp(void *data_buffer,
				uint32_t data_size)
{
	AW_LOGI("enter");
	return 0;
}

static int mtk_spk_recv_ipi_buf_from_dsp(int8_t *buffer,
				int16_t size, uint32_t *buf_len)
{
	AW_LOGI("enter");
	return 0;
}
*/
#elif defined AW_QCOM_OPEN_DSP_PLATFORM
extern int afe_get_topology(int port_id);
extern int aw_send_afe_cal_apr(uint32_t param_id,
	void *buf, int cmd_size, bool write);
/*
static int afe_get_topology(int port_id)
{
	return -EPERM;
}

static int aw_send_afe_cal_apr(uint32_t param_id,
	void *buf, int cmd_size, bool write)
{
	AW_LOGI("enter, no define AWINIC_ADSP_ENABLE", __func__);
	return 0;
}
*/
#endif

uint8_t aw_dsp_isEnable(void)
{
#if (defined AW_QCOM_OPEN_DSP_PLATFORM) || (defined AW_MTK_OPEN_DSP_PLATFORM)
	return true;
#else
	return false;
#endif
}

/*****************mtk dsp communication function start**********************/
#ifdef AW_MTK_OPEN_DSP_PLATFORM
static int aw_mtk_write_data_to_dsp(int32_t param_id,
			void *data, int size)
{
	int32_t *dsp_data = NULL;
	mtk_dsp_hdr_t *hdr = NULL;
	int ret;

	dsp_data = kzalloc(sizeof(mtk_dsp_hdr_t) + size, GFP_KERNEL);
	if (!dsp_data) {
		AW_LOGE("kzalloc dsp_msg error");
		return -ENOMEM;
	}

	hdr = (mtk_dsp_hdr_t *)dsp_data;
	hdr->type = DSP_MSG_TYPE_DATA;
	hdr->opcode_id = param_id;
	hdr->version = AW_DSP_MSG_HDR_VER;

	memcpy(((char *)dsp_data) + sizeof(mtk_dsp_hdr_t),
		data, size);

	ret = mtk_spk_send_ipi_buf_to_dsp(dsp_data,
				sizeof(mtk_dsp_hdr_t) + size);
	if (ret < 0) {
		AW_LOGE("write data failed");
		kfree(dsp_data);
		dsp_data = NULL;
		return ret;
	}

	kfree(dsp_data);
	dsp_data = NULL;
	return 0;
}

static int aw_mtk_read_data_from_dsp(int32_t param_id, void *data,
					int data_size)
{
	int ret;
	mtk_dsp_hdr_t hdr;

	mutex_lock(&g_dsp_lock);
	hdr.type = DSP_MSG_TYPE_CMD;
	hdr.opcode_id = param_id;
	hdr.version = AW_DSP_MSG_HDR_VER;

	ret = mtk_spk_send_ipi_buf_to_dsp(&hdr, sizeof(mtk_dsp_hdr_t));
	if (ret < 0)
		goto failed;

	ret = mtk_spk_recv_ipi_buf_from_dsp(data, data_size, &data_size);
	if (ret < 0)
		goto failed;

	mutex_unlock(&g_dsp_lock);
	return 0;

failed:
	mutex_unlock(&g_dsp_lock);
	return ret;
}

#endif
/********************mtk dsp communication function end***********************/

/******************qcom dsp communication function start**********************/
#ifdef AW_QCOM_OPEN_DSP_PLATFORM
static int aw_check_dsp_ready(void)
{
	int ret;

	ret = afe_get_topology(AFE_PORT_ID_AWDSP_RX);
	AW_LOGD("topo_id 0x%x", ret);

	if (ret <= 0)
		return 0;
	else
		return 1;
}

static int aw_qcom_write_data_to_dsp(int32_t param_id,
				void *data, int data_size)
{
	int ret = 0;
	int try = 0;

	AW_LOGI("enter");
	mutex_lock(&g_dsp_lock);
	while (try < AW_DSP_TRY_TIME) {
		if (aw_check_dsp_ready()) {
			ret = aw_send_afe_cal_apr(param_id, data,
				data_size, true);
			mutex_unlock(&g_dsp_lock);
			return ret;
		} else {
			try++;
			msleep(AW_DSP_SLEEP_TIME);
			AW_LOGD("afe not ready try again");
		}
	}
	mutex_unlock(&g_dsp_lock);

	return -EINVAL;
}

static int aw_qcom_read_data_from_dsp(int32_t param_id,
				void *data, int data_size)
{
	int ret = 0;
	int try = 0;

	AW_LOGI("enter");

	mutex_lock(&g_dsp_lock);
	while (try < AW_DSP_TRY_TIME) {
		if (aw_check_dsp_ready()) {
			ret = aw_send_afe_cal_apr(param_id, data,
					data_size, false);
			mutex_unlock(&g_dsp_lock);
			return ret;
		} else {
			try++;
			msleep(AW_DSP_SLEEP_TIME);
			AW_LOGD("afe not ready try again");
		}
	}
	mutex_unlock(&g_dsp_lock);

	return -EINVAL;
}

#endif
/*****************qcom dsp communication function end*********************/

int aw_dsp_get_rx_module_enable(int *enable)
{
	int ret = 0;

	if (!enable) {
		AW_LOGE("enable is NULL");
		return -EINVAL;
	}

#ifdef AW_QCOM_OPEN_DSP_PLATFORM
	ret = aw_qcom_read_data_from_dsp(AWDSP_RX_SET_ENABLE,
			(void *)enable, sizeof(uint32_t));
#elif defined AW_MTK_OPEN_DSP_PLATFORM
	ret = aw_mtk_read_data_from_dsp(AWDSP_RX_SET_ENABLE,
			(void *)enable, sizeof(uint32_t));
#endif

	return ret;
}

int aw_dsp_set_rx_module_enable(int enable)
{
	int ret = 0;

	switch (enable) {
	case AW_RX_MODULE_DISENABLE:
		AW_LOGD("set enable=%d", enable);
		break;
	case AW_RX_MODULE_ENABLE:
		AW_LOGD("set enable=%d", enable);
		break;
	default:
		AW_LOGE("unsupport enable=%d", enable);
		return -EINVAL;
	}

#ifdef AW_QCOM_OPEN_DSP_PLATFORM
	ret = aw_qcom_write_data_to_dsp(AWDSP_RX_SET_ENABLE,
			&enable, sizeof(uint32_t));
#elif defined AW_MTK_OPEN_DSP_PLATFORM
	ret = aw_mtk_write_data_to_dsp(AWDSP_RX_SET_ENABLE,
			&enable, sizeof(uint32_t));
#endif

	return ret;
}


int aw_dsp_get_vmax(uint32_t *vmax, int dev_index)
{
	int ret  = 0;
	int32_t param_id = 0;

	switch (dev_index % AW_DSP_CHANNEL_MAX) {
	case AW_DSP_CHANNEL_0:
		param_id = AWDSP_RX_VMAX_0;
		break;
	case AW_DSP_CHANNEL_1:
		param_id = AWDSP_RX_VMAX_1;
		break;
	default:
		AW_LOGE("algo only support double PA channel:%d unsupport",
			dev_index);
		return -EINVAL;
	}
#ifdef AW_QCOM_OPEN_DSP_PLATFORM
	ret = aw_qcom_read_data_from_dsp(param_id,
			(void *)vmax, sizeof(uint32_t));
#elif defined AW_MTK_OPEN_DSP_PLATFORM
	ret = aw_mtk_read_data_from_dsp(param_id,
			(void *)vmax, sizeof(uint32_t));
#endif

	return ret;
}

int aw_dsp_set_vmax(uint32_t vmax, int dev_index)
{
	int ret = 0;
	int32_t param_id = 0;

	switch (dev_index % AW_DSP_CHANNEL_MAX) {
	case AW_DSP_CHANNEL_0:
		param_id = AWDSP_RX_VMAX_0;
		break;
	case AW_DSP_CHANNEL_1:
		param_id = AWDSP_RX_VMAX_1;
		break;
	default:
		AW_LOGE("algo only support double PA channel:%d unsupport",
			dev_index);
		return -EINVAL;
	}
#ifdef AW_QCOM_OPEN_DSP_PLATFORM
	ret = aw_qcom_write_data_to_dsp(param_id, &vmax, sizeof(uint32_t));
#elif defined AW_MTK_OPEN_DSP_PLATFORM
	ret = aw_mtk_write_data_to_dsp(param_id, &vmax, sizeof(uint32_t));
#endif

	return ret;
}

