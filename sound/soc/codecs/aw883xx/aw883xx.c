// SPDX-License-Identifier: GPL-2.0+
/*
 * aw883xx.c   aw883xx codec module
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
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/syscalls.h>
#include <linux/vmalloc.h>
#include <sound/tlv.h>
#include <linux/uaccess.h>
#include "aw_pid_2049_reg.h"
#include "aw883xx.h"
#include "aw_bin_parse.h"
#include "aw_device.h"
#include "aw_log.h"
#include "aw_spin.h"

/******************************************************
 *
 * Marco
 *
 ******************************************************/
#define AW883XX_I2C_NAME "aw883xx_smartpa"

#define AW883XX_DRIVER_VERSION "v1.2.0"

#define AW883XX_RATES (SNDRV_PCM_RATE_8000_48000 | \
			SNDRV_PCM_RATE_96000)
#define AW883XX_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE)


#define AW883XX_ACF_FILE	"aw883xx_acf.bin"
#define AW_REQUEST_FW_RETRIES		5	/* 5 times */

static unsigned int g_aw883xx_dev_cnt;
static DEFINE_MUTEX(g_aw883xx_lock);
static struct aw_container *g_awinic_cfg;

static const char *const aw883xx_switch[] = {"Disable", "Enable"};

static int aw883xx_platform_init(struct aw883xx *aw883xx)
{
#ifdef AW_QCOM_PLATFORM
	aw883xx->aw_pa->platform = AW_QCOM;
	return 0;
#elif defined AW_MTK_PLATFORM
	aw883xx->aw_pa->platform = AW_MTK;
	return 0;
#elif defined AW_SPRD_PLATFORM
	aw883xx->aw_pa->platform = AW_SPRD;
	return 0;
#else
	return -EINVAL;
#endif
}

int aw883xx_get_version(char *buf, int size)
{
	if (size > strlen(AW883XX_DRIVER_VERSION)) {
		memcpy(buf, AW883XX_DRIVER_VERSION, strlen(AW883XX_DRIVER_VERSION));
		return strlen(AW883XX_DRIVER_VERSION);
	} else {
		return -ENOMEM;
	}
}

/******************************************************
 *
 * aw883xx append suffix sound channel information
 *
 ******************************************************/
static void *aw883xx_devm_kstrdup(struct device *dev, char *buf)
{
	char *str = NULL;

	str = devm_kzalloc(dev, strlen(buf) + 1, GFP_KERNEL);
	if (!str) {
		aw_pr_err("devm_kzalloc %s failed", buf);
		return str;
	}
	memcpy(str, buf, strlen(buf));
	return str;
}

static int aw883xx_append_i2c_suffix(const char *format,
		const char **change_name, struct aw883xx *aw883xx)
{
	char buf[64] = { 0 };
	int i2cbus = aw883xx->i2c->adapter->nr;
	int addr = aw883xx->i2c->addr;

	snprintf(buf, sizeof(buf), format, *change_name, i2cbus, addr);
	*change_name = aw883xx_devm_kstrdup(aw883xx->dev, buf);
	if (!(*change_name))
		return -ENOMEM;

	aw_dev_info(aw883xx->dev, "change name :%s", *change_name);
	return 0;
}


/******************************************************
 *
 * aw883xx distinguish between codecs and components by version
 *
 ******************************************************/
#ifdef AW_KERNEL_VER_OVER_4_19_1
static struct aw_componet_codec_ops aw_componet_codec_ops = {
	.kcontrol_codec = snd_soc_kcontrol_component,
	.codec_get_drvdata = snd_soc_component_get_drvdata,
	.add_codec_controls = snd_soc_add_component_controls,
	.unregister_codec = snd_soc_unregister_component,
	.register_codec = snd_soc_register_component,
};
#else
static struct aw_componet_codec_ops aw_componet_codec_ops = {
	.kcontrol_codec = snd_soc_kcontrol_codec,
	.codec_get_drvdata = snd_soc_codec_get_drvdata,
	.add_codec_controls = snd_soc_add_codec_controls,
	.unregister_codec = snd_soc_unregister_codec,
	.register_codec = snd_soc_register_codec,
};
#endif

static aw_snd_soc_codec_t *aw883xx_get_codec(struct snd_soc_dai *dai)
{
#ifdef AW_KERNEL_VER_OVER_4_19_1
	return dai->component;
#else
	return dai->codec;
#endif
}

/******************************************************
 *
 * aw883xx reg write/read
 *
 ******************************************************/

int aw883xx_i2c_writes(struct aw883xx *aw883xx,
		uint8_t reg_addr, uint8_t *buf, uint16_t len)
{
	int ret = -1;
	uint8_t *data = NULL;

	data = kmalloc(len + 1, GFP_KERNEL);
	if (data == NULL) {
		aw_dev_err(aw883xx->dev, "can not allocate memory");
		return -ENOMEM;
	}

	data[0] = reg_addr;
	memcpy(&data[1], buf, len);

	ret = i2c_master_send(aw883xx->i2c, data, len + 1);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "i2c master send error");
		goto i2c_master_error;
	} else if (ret != (len + 1)) {
		aw_dev_err(aw883xx->dev, "i2c master send error(size error)");
		ret = -ENXIO;
		goto i2c_master_error;
	}

	kfree(data);
	data = NULL;
	return 0;

i2c_master_error:
	kfree(data);
	data = NULL;
	return ret;
}

static int aw883xx_i2c_reads(struct aw883xx *aw883xx,
			uint8_t reg_addr, uint8_t *data_buf,
			uint16_t data_len)
{
	int ret;
	struct i2c_msg msg[] = {
		[0] = {
				.addr = aw883xx->i2c->addr,
				.flags = 0,
				.len = sizeof(uint8_t),
				.buf = &reg_addr,
				},
		[1] = {
				.addr = aw883xx->i2c->addr,
				.flags = I2C_M_RD,
				.len = data_len,
				.buf = data_buf,
				},
	};

	ret = i2c_transfer(aw883xx->i2c->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "i2c_transfer failed");
		return ret;
	} else if (ret != AW883XX_READ_MSG_NUM) {
		aw_dev_err(aw883xx->dev, "transfer failed(size error)");
		return -ENXIO;
	}

	return 0;
}

int aw883xx_i2c_write(struct aw883xx *aw883xx,
		uint8_t reg_addr, uint16_t reg_data)
{
	int ret = -1;
	uint8_t cnt = 0;
	uint8_t buf[2] = {0};

	buf[0] = (reg_data & 0xff00) >> 8;
	buf[1] = (reg_data & 0x00ff) >> 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = aw883xx_i2c_writes(aw883xx, reg_addr, buf, 2);
		if (ret < 0)
			aw_dev_err(aw883xx->dev, "i2c_write cnt=%d error=%d",
				cnt, ret);
		else
			break;
		cnt++;
	}

	if (aw883xx->i2c_log_en)
		aw_dev_info(aw883xx->dev, "write: reg = 0x%02x, val = 0x%04x",
			reg_addr, reg_data);

	return ret;
}

int aw883xx_i2c_read(struct aw883xx *aw883xx,
			uint8_t reg_addr, uint16_t *reg_data)
{
	int ret = -1;
	uint8_t cnt = 0;
	uint8_t buf[2] = {0};

	while (cnt < AW_I2C_RETRIES) {
		ret = aw883xx_i2c_reads(aw883xx, reg_addr, buf, 2);
		if (ret < 0) {
			aw_dev_err(aw883xx->dev, "i2c_read cnt=%d error=%d",
				cnt, ret);
		} else {
			*reg_data = (buf[0] << 8) | (buf[1] << 0);
			break;
		}
		cnt++;
	}

	if (aw883xx->i2c_log_en)
		aw_dev_info(aw883xx->dev, "read: reg = 0x%02x, val = 0x%04x",
			reg_addr, *reg_data);

	return ret;
}

static int aw883xx_i2c_write_bits(struct aw883xx *aw883xx,
				uint8_t reg_addr, uint16_t mask,
				uint16_t reg_data)
{
	int ret = -1;
	uint16_t reg_val = 0;

	ret = aw883xx_i2c_read(aw883xx, reg_addr, &reg_val);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "i2c read error, ret=%d", ret);
		return ret;
	}
	reg_val &= mask;
	reg_val |= reg_data;
	ret = aw883xx_i2c_write(aw883xx, reg_addr, reg_val);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "i2c read error, ret=%d", ret);
		return ret;
	}

	return 0;
}

int aw883xx_reg_write(struct aw883xx *aw883xx,
			uint8_t reg_addr, uint16_t reg_data)
{
	int ret = -1;

	mutex_lock(&aw883xx->i2c_lock);
	ret = aw883xx_i2c_write(aw883xx, reg_addr, reg_data);
	if (ret < 0)
		aw_dev_err(aw883xx->dev,
			"write fail, reg = 0x%02x, val = 0x%04x, ret=%d",
			reg_addr, reg_data, ret);
	mutex_unlock(&aw883xx->i2c_lock);

	return ret;
}

int aw883xx_reg_read(struct aw883xx *aw883xx,
			uint8_t reg_addr, uint16_t *reg_data)
{
	int ret = -1;

	mutex_lock(&aw883xx->i2c_lock);
	ret = aw883xx_i2c_read(aw883xx, reg_addr, reg_data);
	if (ret < 0)
		aw_dev_err(aw883xx->dev,
			"read fail: reg = 0x%02x, val = 0x%04x, ret=%d",
			reg_addr, *reg_data, ret);
	mutex_unlock(&aw883xx->i2c_lock);

	return ret;
}

int aw883xx_reg_write_bits(struct aw883xx *aw883xx,
			uint8_t reg_addr, uint16_t mask, uint16_t reg_data)
{
	int ret = -1;

	mutex_lock(&aw883xx->i2c_lock);
	ret = aw883xx_i2c_write_bits(aw883xx, reg_addr, mask, reg_data);
	if (ret < 0)
		aw_dev_err(aw883xx->dev,"fail, ret=%d", ret);
	mutex_unlock(&aw883xx->i2c_lock);

	return ret;
}

static int aw883xx_dsp_write_16bit(struct aw883xx *aw883xx,
		uint16_t dsp_addr, uint32_t dsp_data)
{
	int ret;
	struct aw_dsp_mem_desc *desc = &aw883xx->aw_pa->dsp_mem_desc;

	ret = aw883xx_i2c_write(aw883xx, desc->dsp_madd_reg, dsp_addr);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "i2c write error, ret=%d", ret);
		return ret;
	}

	ret = aw883xx_i2c_write(aw883xx, desc->dsp_mdat_reg, (uint16_t)dsp_data);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "i2c write error, ret=%d", ret);
		return ret;
	}

	return 0;
}

static int aw883xx_dsp_write_32bit(struct aw883xx *aw883xx,
		uint16_t dsp_addr, uint32_t dsp_data)
{
	int ret;
	uint16_t temp_data = 0;
	struct aw_dsp_mem_desc *desc = &aw883xx->aw_pa->dsp_mem_desc;

	ret = aw883xx_i2c_write(aw883xx, desc->dsp_madd_reg, dsp_addr);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "i2c write error, ret=%d", ret);
		return ret;
	}

	temp_data = dsp_data & AW883XX_DSP_16_DATA_MASK;
	ret = aw883xx_i2c_write(aw883xx, desc->dsp_mdat_reg, temp_data);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "i2c write error, ret=%d", ret);
		return ret;
	}

	temp_data = dsp_data >> 16;
	ret = aw883xx_i2c_write(aw883xx, desc->dsp_mdat_reg, temp_data);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "i2c write error, ret=%d", ret);
		return ret;
	}

	return 0;
}

/******************************************************
 * aw883xx clear dsp chip select state
 ******************************************************/
static void aw883xx_clear_dsp_sel_st(struct aw883xx *aw883xx)
{
	uint16_t reg_value;
	uint8_t reg = aw883xx->aw_pa->soft_rst.reg;

	aw883xx_i2c_read(aw883xx, reg, &reg_value);
}

int aw883xx_dsp_write(struct aw883xx *aw883xx,
		uint16_t dsp_addr, uint32_t dsp_data, uint8_t data_type)
{
	int ret = -1;

	mutex_lock(&aw883xx->i2c_lock);
	if (data_type == AW_DSP_16_DATA) {
		ret = aw883xx_dsp_write_16bit(aw883xx, dsp_addr, dsp_data);
		if (ret < 0) {
			aw_dev_err(aw883xx->dev, "write dsp_addr[0x%04x] 16 bit dsp_data[%04x] failed",
					(uint32_t)dsp_addr, dsp_data);
			goto exit;
		}
	} else if (data_type == AW_DSP_32_DATA) {
		ret =  aw883xx_dsp_write_32bit(aw883xx, dsp_addr, dsp_data);
		if (ret < 0) {
			aw_dev_err(aw883xx->dev, "write dsp_addr[0x%04x] 32 bit dsp_data[%08x] failed",
					(uint32_t)dsp_addr, dsp_data);
			goto exit;
		}
	} else {
		aw_dev_err(aw883xx->dev, "data type[%d] unsupported", data_type);
		ret = -EINVAL;
		goto exit;
	}

exit:
	aw883xx_clear_dsp_sel_st(aw883xx);
	mutex_unlock(&aw883xx->i2c_lock);
	return ret;
}


static int aw883xx_dsp_read_16bit(struct aw883xx *aw883xx,
		uint16_t dsp_addr, uint32_t *dsp_data)
{
	int ret;
	uint16_t temp_data = 0;
	struct aw_dsp_mem_desc *desc = &aw883xx->aw_pa->dsp_mem_desc;

	ret = aw883xx_i2c_write(aw883xx, desc->dsp_madd_reg, dsp_addr);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "i2c write error, ret=%d", ret);
		return ret;
	}

	ret = aw883xx_i2c_read(aw883xx, desc->dsp_mdat_reg, &temp_data);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "i2c write error, ret=%d", ret);
		return ret;
	}

	*dsp_data = temp_data;

	return 0;
}

static int aw883xx_dsp_read_32bit(struct aw883xx *aw883xx,
		uint16_t dsp_addr, uint32_t *dsp_data)
{
	int ret;
	uint16_t temp_data = 0;
	struct aw_dsp_mem_desc *desc = &aw883xx->aw_pa->dsp_mem_desc;

	/*write dsp addr*/
	ret = aw883xx_i2c_write(aw883xx, desc->dsp_madd_reg, dsp_addr);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "i2c write error, ret=%d", ret);
		return ret;
	}

	/*get Low 16 bit data*/
	ret = aw883xx_i2c_read(aw883xx, desc->dsp_mdat_reg, &temp_data);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "i2c read error, ret=%d", ret);
		return ret;
	}

	*dsp_data = temp_data;

	/*get high 16 bit data*/
	ret = aw883xx_i2c_read(aw883xx, desc->dsp_mdat_reg, &temp_data);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "i2c read error, ret=%d", ret);
		return ret;
	}
	*dsp_data |= (temp_data << 16);

	return 0;
}

int aw883xx_dsp_read(struct aw883xx *aw883xx,
		uint16_t dsp_addr, uint32_t *dsp_data, uint8_t data_type)
{
	int ret = -1;

	mutex_lock(&aw883xx->i2c_lock);
	if (data_type == AW_DSP_16_DATA) {
		ret = aw883xx_dsp_read_16bit(aw883xx, dsp_addr, dsp_data);
		if (ret < 0) {
			aw_dev_err(aw883xx->dev, "read dsp_addr[0x%04x] 16 bit dsp_data[%04x] failed",
					(uint32_t)dsp_addr, *dsp_data);
			goto exit;
		}
	} else if (data_type == AW_DSP_32_DATA) {
		ret = aw883xx_dsp_read_32bit(aw883xx, dsp_addr, dsp_data);
		if (ret < 0) {
			aw_dev_err(aw883xx->dev, "read dsp_addr[0x%04x] 32 bit dsp_data[%08x] failed",
					(uint32_t)dsp_addr, *dsp_data);
			goto exit;
		}
	} else {
		aw_dev_err(aw883xx->dev, "data type[%d] unsupported", data_type);
		ret = -EINVAL;
		goto exit;
	}

exit:
	aw883xx_clear_dsp_sel_st(aw883xx);
	mutex_unlock(&aw883xx->i2c_lock);
	return ret;
}

/******************************************************
 * aw883xx get dev num
 ******************************************************/
int aw883xx_get_dev_num(void)
{
	return g_aw883xx_dev_cnt;
}

/******************************************************
 * aw883xx interrupt
 ******************************************************/
static void aw883xx_interrupt_work(struct work_struct *work)
{
	struct aw883xx *aw883xx = container_of(work,
				struct aw883xx, interrupt_work.work);
	int16_t reg_value;
	int ret;

	aw_dev_info(aw883xx->dev, "enter");

	/*read reg value*/
	ret = aw_dev_get_int_status(aw883xx->aw_pa, &reg_value);
	if (ret < 0)
		aw_dev_err(aw883xx->dev, "get init_reg value failed");
	else
		aw_dev_info(aw883xx->dev, "int value 0x%x", reg_value);

	/*clear init reg*/
	aw_dev_clear_int_status(aw883xx->aw_pa);

	/*unmask interrupt*/
	aw_dev_set_intmask(aw883xx->aw_pa, true);
}

/******************************************************
 *
 * Digital Audio Interface
 *
 ******************************************************/
static int aw883xx_startup(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	aw_snd_soc_codec_t *codec = aw883xx_get_codec(dai);
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(codec);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		aw_dev_info(aw883xx->dev, "playback enter");
		/*load cali re*/
		if (AW_ERRO_CALI_RE_VALUE == aw883xx->aw_pa->cali_desc.cali_re)
			aw_cali_get_cali_re(&aw883xx->aw_pa->cali_desc);
	} else {
		aw_dev_info(aw883xx->dev, "capture enter");
	}

	return 0;
}

static int aw883xx_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	aw_snd_soc_codec_t *codec = aw883xx_get_codec(dai);
	/*struct aw883xx *aw883xx =
		aw_componet_codec_ops.aw_snd_soc_codec_get_drvdata(codec); */

	aw_dev_info(codec->dev, "fmt=0x%x", fmt);

	/* supported mode: regular I2S, slave, or PDM */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) !=
			SND_SOC_DAIFMT_CBS_CFS) {
			aw_dev_err(codec->dev, "invalid codec master mode");
			return -EINVAL;
		}
		break;
	default:
		aw_dev_err(codec->dev, "unsupported DAI format %d",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	return 0;
}

static int aw883xx_set_dai_sysclk(struct snd_soc_dai *dai,
				int clk_id, unsigned int freq, int dir)
{
	aw_snd_soc_codec_t *codec = aw883xx_get_codec(dai);
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(codec);

	aw_dev_info(aw883xx->dev, "freq=%d", freq);

	aw883xx->sysclk = freq;
	return 0;
}

static int aw883xx_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	aw_snd_soc_codec_t *codec = aw883xx_get_codec(dai);
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(codec);

	/* get CAPTURE rate param  bit width*/
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		aw_dev_info(aw883xx->dev,
			"STREAM_CAPTURE requested rate: %d, width = %d",
			params_rate(params), params_width(params));
	}

	/* get PLAYBACK rate param  bit width*/
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		aw_dev_info(aw883xx->dev,
			"STREAM_PLAYBACK requested rate: %d, width = %d",
			params_rate(params), params_width(params));
	}

	return 0;
}

static void aw883xx_start_pa(struct aw883xx *aw883xx)
{
	int ret, i;

	aw_dev_info(aw883xx->dev, "enter");

	if (aw883xx->allow_pw == false) {
		aw_dev_info(aw883xx->dev, "dev can not allow power");
		return;
	}

	if (aw883xx->pstream == AW883XX_STREAM_CLOSE) {
		aw_dev_info(aw883xx->dev, "pstream is close");
		return;
	}

	for (i = 0; i < AW_START_RETRIES; i++) {
		ret = aw_device_start(aw883xx->aw_pa);
		if (ret) {
			aw_dev_err(aw883xx->dev, "start failed");
			ret = aw_dev_fw_update(aw883xx->aw_pa, AW_DSP_FW_UPDATE_ON, true);
			if (ret < 0) {
				aw_dev_err(aw883xx->dev, "fw update failed");
				continue;
			}
		} else {
			aw_dev_info(aw883xx->dev, "start success");
			break;
		}
	}
}

static void aw883xx_startup_work(struct work_struct *work)
{
	struct aw883xx *aw883xx =
		container_of(work, struct aw883xx, start_work.work);

	mutex_lock(&aw883xx->lock);
	aw883xx_start_pa(aw883xx);
	mutex_unlock(&aw883xx->lock);
}

static void aw883xx_start(struct aw883xx *aw883xx, bool sync_start)
{
	int ret;
	int i;

	if (aw883xx->aw_pa->fw_status == AW_DEV_FW_OK) {
		if (aw883xx->allow_pw == false) {
			aw_dev_info(aw883xx->dev, "dev can not allow power");
			return;
		}

		if (aw883xx->aw_pa->status == AW_DEV_PW_ON)
			return;

		for (i = 0; i < AW_START_RETRIES; i++) {
			ret = aw_dev_fw_update(aw883xx->aw_pa, AW_DSP_FW_UPDATE_OFF,
						aw883xx->phase_sync);
			if (ret < 0) {
				aw_dev_err(aw883xx->dev, "fw update failed");
				continue;
			} else {
				/*firmware update success*/
				if (sync_start == AW_SYNC_START)
					aw883xx_start_pa(aw883xx);
				else
					queue_delayed_work(aw883xx->work_queue,
						&aw883xx->start_work,
						AW883XX_START_WORK_DELAY_MS);

				return;
			}
		}
	}
}

static int aw883xx_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	aw_snd_soc_codec_t *codec = aw883xx_get_codec(dai);
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(codec);

	aw_dev_info(aw883xx->dev, "mute state=%d", mute);

	if (stream != SNDRV_PCM_STREAM_PLAYBACK) {
		aw_dev_info(aw883xx->dev, "capture");
		return 0;
	}

	if (mute) {
		aw883xx->pstream = AW883XX_STREAM_CLOSE;
		cancel_delayed_work_sync(&aw883xx->start_work);
		mutex_lock(&aw883xx->lock);
		aw_device_stop(aw883xx->aw_pa);
		mutex_unlock(&aw883xx->lock);
	} else {
		aw883xx->pstream = AW883XX_STREAM_OPEN;
		mutex_lock(&aw883xx->lock);
		aw883xx_start(aw883xx, AW_ASYNC_START);
		aw_hold_dsp_spin_st(&aw883xx->aw_pa->spin_desc);
		mutex_unlock(&aw883xx->lock);
	}

	return 0;
}

static void aw883xx_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	aw_snd_soc_codec_t *codec = aw883xx_get_codec(dai);
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(codec);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		aw_dev_info(aw883xx->dev, "stream playback");
	else
		aw_dev_info(aw883xx->dev, "stream capture");
}

static const struct snd_soc_dai_ops aw883xx_dai_ops = {
	.startup = aw883xx_startup,
	.set_fmt = aw883xx_set_fmt,
	.set_sysclk = aw883xx_set_dai_sysclk,
	.hw_params = aw883xx_hw_params,
	.mute_stream = aw883xx_mute,
	.shutdown = aw883xx_shutdown,
};

static struct snd_soc_dai_driver aw883xx_dai[] = {
	{
	.name = "aw883xx-aif",
	.id = 1,
	.playback = {
		.stream_name = "Speaker_Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = AW883XX_RATES,
		.formats = AW883XX_FORMATS,
		},
	.capture = {
		.stream_name = "Speaker_Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = AW883XX_RATES,
		.formats = AW883XX_FORMATS,
		},
	.ops = &aw883xx_dai_ops,
	/*.symmetric_rates = 1,*/
	},
};

static int aw883xx_dai_drv_append_suffix(struct aw883xx *aw883xx,
				struct snd_soc_dai_driver *dai_drv,
				int num_dai)
{
	int ret;
	int i;

	if ((dai_drv != NULL) && (num_dai > 0)) {
		for (i = 0; i < num_dai; i++) {
			ret = aw883xx_append_i2c_suffix("%s-%x-%x",
					&dai_drv->name, aw883xx);
			if (ret < 0)
				return ret;
			ret = aw883xx_append_i2c_suffix("%s_%x_%x",
					&dai_drv->playback.stream_name, aw883xx);
			if (ret < 0)
				return ret;
			ret = aw883xx_append_i2c_suffix("%s_%x_%x",
					&dai_drv->capture.stream_name, aw883xx);
			if (ret < 0)
				return ret;

			aw_dev_info(aw883xx->dev, "dai name [%s]", dai_drv[i].name);
			aw_dev_info(aw883xx->dev, "pstream_name [%s]",
						dai_drv[i].playback.stream_name);
			aw_dev_info(aw883xx->dev, "cstream_name [%s]",
						dai_drv[i].capture.stream_name);
		}
	}

	return 0;
}

/*****************************************************
 *
 * codec driver
 *
 *****************************************************/
static int aw883xx_get_fade_in_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	unsigned int time;

	aw_dev_get_fade_time(&time, true);
	ucontrol->value.integer.value[0] = time;

	aw_pr_dbg("step time %ld", ucontrol->value.integer.value[0]);

	return 0;

}

static int aw883xx_set_fade_in_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	if ((ucontrol->value.integer.value[0] > mc->max) ||
		(ucontrol->value.integer.value[0] < mc->min)) {
		aw_pr_dbg("set val %ld overflow %d or  less than :%d",
			ucontrol->value.integer.value[0], mc->max, mc->min);
		return 0;
	}
	aw_dev_set_fade_time(ucontrol->value.integer.value[0], true);

	aw_pr_dbg("step time %ld", ucontrol->value.integer.value[0]);
	return 0;
}

static int aw883xx_get_fade_out_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	unsigned int time;

	aw_dev_get_fade_time(&time, false);
	ucontrol->value.integer.value[0] = time;

	aw_pr_dbg("step time %ld", ucontrol->value.integer.value[0]);

	return 0;
}

static int aw883xx_set_fade_out_time(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	if ((ucontrol->value.integer.value[0] > mc->max) ||
		(ucontrol->value.integer.value[0] < mc->min)) {
		aw_pr_dbg("set val %ld overflow %d or  less than :%d",
			ucontrol->value.integer.value[0], mc->max, mc->min);
		return 0;
	}

	aw_dev_set_fade_time(ucontrol->value.integer.value[0], false);

	aw_pr_dbg("step time %ld", ucontrol->value.integer.value[0]);

	return 0;
}

static struct snd_kcontrol_new aw883xx_controls[] = {
	SOC_SINGLE_EXT("aw883xx_fadein_us", 0, 0, 1000000, 0,
		aw883xx_get_fade_in_time, aw883xx_set_fade_in_time),
	SOC_SINGLE_EXT("aw883xx_fadeout_us", 0, 0, 1000000, 0,
		aw883xx_get_fade_out_time, aw883xx_set_fade_out_time),
};


static void aw883xx_add_codec_controls(struct aw883xx *aw883xx)
{
	aw_dev_info(aw883xx->dev, "enter");

	if (aw883xx->aw_pa->channel == 0) {
		aw_componet_codec_ops.add_codec_controls(aw883xx->codec,
				&aw883xx_controls[0], ARRAY_SIZE(aw883xx_controls));
		aw_add_spin_controls((void *)aw883xx);
	}
}

static int aw883xx_profile_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	int count;
	char *name = NULL;
	const char *prof_name = NULL;
	aw_snd_soc_codec_t *codec =
		aw_componet_codec_ops.kcontrol_codec(kcontrol);
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(codec);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;

	count = aw_dev_get_profile_count(aw883xx->aw_pa);
	if (count <= 0) {
		uinfo->value.enumerated.items = 0;
		aw_dev_err(aw883xx->dev, "get count[%d] failed", count);
		return 0;
	}

	uinfo->value.enumerated.items = count;

	if (uinfo->value.enumerated.item >= count)
		uinfo->value.enumerated.item = count - 1;

	name = uinfo->value.enumerated.name;
	count = uinfo->value.enumerated.item;

	prof_name = aw_dev_get_prof_name(aw883xx->aw_pa, count);
	if (prof_name == NULL) {
		strlcpy(uinfo->value.enumerated.name, "null",
						strlen("null") + 1);
		return 0;
	}

	strlcpy(name, prof_name, sizeof(uinfo->value.enumerated.name));

	return 0;
}

static int aw883xx_profile_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	aw_snd_soc_codec_t *codec =
		aw_componet_codec_ops.kcontrol_codec(kcontrol);
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = aw_dev_get_profile_index(aw883xx->aw_pa);
	aw_dev_dbg(codec->dev, "profile index [%d]",
			aw_dev_get_profile_index(aw883xx->aw_pa));
	return 0;

}

static int aw883xx_profile_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	aw_snd_soc_codec_t *codec =
		aw_componet_codec_ops.kcontrol_codec(kcontrol);
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(codec);
	int ret;
	int cur_index;

	if (aw883xx->dbg_en_prof == false) {
		aw_dev_info(codec->dev, "profile close");
		return 0;
	}

	/* check value valid */
	ret = aw_dev_check_profile_index(aw883xx->aw_pa, ucontrol->value.integer.value[0]);
	if (ret) {
		aw_dev_info(codec->dev, "unsupported index %ld",
					ucontrol->value.integer.value[0]);
		return 0;
	}

	/*check cur_index == set value*/
	cur_index = aw_dev_get_profile_index(aw883xx->aw_pa);
	if (cur_index == ucontrol->value.integer.value[0]) {
		aw_dev_info(codec->dev, "index no change");
		return 0;
	}

	/*pa stop or stopping just set profile*/
	mutex_lock(&aw883xx->lock);
	aw_dev_set_profile_index(aw883xx->aw_pa, ucontrol->value.integer.value[0]);

	if (aw883xx->pstream) {
		aw_device_stop(aw883xx->aw_pa);
		aw883xx_start(aw883xx, AW_SYNC_START);
	}

	mutex_unlock(&aw883xx->lock);

	aw_dev_info(codec->dev, "profile id %ld", ucontrol->value.integer.value[0]);
	return 0;
}

static int aw883xx_switch_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	int count;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	count = 2;

	uinfo->value.enumerated.items = count;

	if (uinfo->value.enumerated.item >= count)
		uinfo->value.enumerated.item = count - 1;

	strlcpy(uinfo->value.enumerated.name,
		aw883xx_switch[uinfo->value.enumerated.item],
		strlen(aw883xx_switch[uinfo->value.enumerated.item]) + 1);

	return 0;
}

static int aw883xx_switch_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	aw_snd_soc_codec_t *codec =
		aw_componet_codec_ops.kcontrol_codec(kcontrol);
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = aw883xx->allow_pw;

	return 0;
}

static int aw883xx_switch_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	aw_snd_soc_codec_t *codec =
		aw_componet_codec_ops.kcontrol_codec(kcontrol);
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(codec);

	aw_dev_info(codec->dev, "set value:%ld", ucontrol->value.integer.value[0]);

	if (ucontrol->value.integer.value[0] == aw883xx->allow_pw) {
		aw_dev_info(aw883xx->dev, "PA switch not change");
		return 0;
	}

	if (aw883xx->pstream) {
		if (ucontrol->value.integer.value[0] == 0) {
			cancel_delayed_work_sync(&aw883xx->start_work);
			mutex_lock(&aw883xx->lock);
			aw_device_stop(aw883xx->aw_pa);
			aw883xx->allow_pw = false;
			mutex_unlock(&aw883xx->lock);
		} else {
			cancel_delayed_work_sync(&aw883xx->start_work);
			mutex_lock(&aw883xx->lock);
			aw883xx->allow_pw = true;
			aw883xx_start(aw883xx, AW_SYNC_START);
			mutex_unlock(&aw883xx->lock);
		}
	} else {
		mutex_lock(&aw883xx->lock);
		if (ucontrol->value.integer.value[0])
			aw883xx->allow_pw = true;
		else
			aw883xx->allow_pw = false;
		mutex_unlock(&aw883xx->lock);
	}

	return 0;
}

static int aw883xx_monitor_switch_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	int count;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	count = 2;

	uinfo->value.enumerated.items = count;

	if (uinfo->value.enumerated.item >= count)
		uinfo->value.enumerated.item = count - 1;

	strlcpy(uinfo->value.enumerated.name,
		aw883xx_switch[uinfo->value.enumerated.item],
		strlen(aw883xx_switch[uinfo->value.enumerated.item]) + 1);

	return 0;
}

static int aw883xx_monitor_switch_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	aw_snd_soc_codec_t *codec =
		aw_componet_codec_ops.kcontrol_codec(kcontrol);
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(codec);
	struct aw_monitor_desc *monitor_desc = &aw883xx->aw_pa->monitor_desc;

	ucontrol->value.integer.value[0] = monitor_desc->monitor_cfg.monitor_switch;

	return 0;
}

static int aw883xx_monitor_switch_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	aw_snd_soc_codec_t *codec =
		aw_componet_codec_ops.kcontrol_codec(kcontrol);
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(codec);
	struct aw_monitor_desc *monitor_desc = &aw883xx->aw_pa->monitor_desc;
	uint32_t enable = 0;

	aw_dev_info(codec->dev, "set monitor_switch:%ld", ucontrol->value.integer.value[0]);

	enable = ucontrol->value.integer.value[0];

	if (monitor_desc->monitor_cfg.monitor_switch == enable) {
		aw_dev_info(aw883xx->dev, "monitor_switch not change");
		return 0;
	} else {
		monitor_desc->monitor_cfg.monitor_switch = enable;
		if (enable)
			aw_monitor_start(monitor_desc);
	}

	return 0;
}

static int aw883xx_dynamic_create_controls(struct aw883xx *aw883xx)
{
	struct snd_kcontrol_new *aw883xx_dev_control = NULL;
	char *kctl_name = NULL;

	aw883xx_dev_control = devm_kzalloc(aw883xx->codec->dev,
			sizeof(struct snd_kcontrol_new) * AW_KCONTROL_NUM, GFP_KERNEL);
	if (aw883xx_dev_control == NULL) {
		aw_dev_err(aw883xx->codec->dev, "kcontrol malloc failed!");
		return -ENOMEM;
	}

	kctl_name = devm_kzalloc(aw883xx->codec->dev, AW_NAME_BUF_MAX, GFP_KERNEL);
	if (kctl_name == NULL)
		return -ENOMEM;

	snprintf(kctl_name, AW_NAME_BUF_MAX, "aw_dev_%u_prof",
		aw883xx->aw_pa->channel);

	aw883xx_dev_control[0].name = kctl_name;
	aw883xx_dev_control[0].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	aw883xx_dev_control[0].info = aw883xx_profile_info;
	aw883xx_dev_control[0].get = aw883xx_profile_get;
	aw883xx_dev_control[0].put = aw883xx_profile_set;

	kctl_name = devm_kzalloc(aw883xx->codec->dev, AW_NAME_BUF_MAX, GFP_KERNEL);
	if (!kctl_name)
		return -ENOMEM;

	snprintf(kctl_name, AW_NAME_BUF_MAX, "aw_dev_%u_switch", aw883xx->aw_pa->channel);

	aw883xx_dev_control[1].name = kctl_name;
	aw883xx_dev_control[1].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	aw883xx_dev_control[1].info = aw883xx_switch_info;
	aw883xx_dev_control[1].get = aw883xx_switch_get;
	aw883xx_dev_control[1].put = aw883xx_switch_set;

	kctl_name = devm_kzalloc(aw883xx->codec->dev, AW_NAME_BUF_MAX, GFP_KERNEL);
	if (!kctl_name)
		return -ENOMEM;

	snprintf(kctl_name, AW_NAME_BUF_MAX, "aw_dev_%u_monitor_switch", aw883xx->aw_pa->channel);

	aw883xx_dev_control[2].name = kctl_name;
	aw883xx_dev_control[2].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	aw883xx_dev_control[2].info = aw883xx_monitor_switch_info;
	aw883xx_dev_control[2].get = aw883xx_monitor_switch_get;
	aw883xx_dev_control[2].put = aw883xx_monitor_switch_set;

	aw_componet_codec_ops.add_codec_controls(aw883xx->codec,
						aw883xx_dev_control, AW_KCONTROL_NUM);

	return 0;
}

static int aw883xx_request_firmware_file(struct aw883xx *aw883xx)
{
	const struct firmware *cont = NULL;
	struct aw_container *aw_cfg = NULL;
	int ret = -1;
	int i;

	aw883xx->aw_pa->fw_status = AW_DEV_FW_FAILED;

	for (i = 0; i < AW_REQUEST_FW_RETRIES; i++) {
		ret = request_firmware(&cont, AW883XX_ACF_FILE, aw883xx->dev);
		if ((ret < 0) || (!cont)) {
			aw883xx->fw_retry_cnt++;
			aw_dev_err(aw883xx->dev, "load [%s] try [%d]!",
						AW883XX_ACF_FILE, aw883xx->fw_retry_cnt);

			if (aw883xx->fw_retry_cnt == AW_REQUEST_FW_RETRIES) {
				aw883xx->fw_retry_cnt = 0;
				return ret;
			}
			msleep(1000);
		} else {
			break;
		}
	}

	if (!cont)
		return -ENOMEM;

	aw_dev_info(aw883xx->dev, "loaded %s - size: %zu",
		AW883XX_ACF_FILE, cont ? cont->size : 0);

	mutex_lock(&g_aw883xx_lock);
	if (g_awinic_cfg == NULL) {
		aw_cfg = vzalloc(cont->size + sizeof(int));
		if (aw_cfg == NULL) {
			aw_dev_err(aw883xx->dev, "aw883xx_cfg devm_kzalloc failed");
			release_firmware(cont);
			mutex_unlock(&g_aw883xx_lock);
			return -ENOMEM;
		}
		aw_cfg->len = cont->size;
		memcpy(aw_cfg->data, cont->data, cont->size);
		ret = aw_dev_load_acf_check(aw_cfg);
		if (ret < 0) {
			aw_dev_err(aw883xx->dev, "Load [%s] failed ....!", AW883XX_ACF_FILE);
			vfree(aw_cfg);
			aw_cfg = NULL;
			release_firmware(cont);
			mutex_unlock(&g_aw883xx_lock);
			return ret;
		}
		g_awinic_cfg = aw_cfg;
	} else {
		aw_cfg = g_awinic_cfg;
		aw_dev_info(aw883xx->dev, "[%s] already loaded...", AW883XX_ACF_FILE);
	}
	release_firmware(cont);
	mutex_unlock(&g_aw883xx_lock);

	mutex_lock(&aw883xx->lock);
	/*aw device init*/
	ret = aw_device_init(aw883xx->aw_pa, aw_cfg);
	if (ret < 0) {
		aw_dev_info(aw883xx->dev, "dev init failed");
		mutex_unlock(&aw883xx->lock);
		return ret;
	}

	aw883xx_dynamic_create_controls(aw883xx);

	aw_check_spin_mode(&aw883xx->aw_pa->spin_desc);

	mutex_unlock(&aw883xx->lock);

	return 0;
}

static void aw883xx_fw_wrok(struct work_struct *work)
{
	struct aw883xx *aw883xx = container_of(work,
				struct aw883xx, acf_work.work);
	int ret;

	ret = aw883xx_request_firmware_file(aw883xx);
	if (ret < 0)
		aw_dev_err(aw883xx->dev, "load profile failed");

}

static void aw883xx_load_fw(struct aw883xx *aw883xx)
{

	if (aw883xx->aw_pa->platform == AW_QCOM) {
		/*QCOM sync loading*/
		aw883xx_request_firmware_file(aw883xx);
	} else {
		/*async loading*/
		queue_delayed_work(aw883xx->work_queue,
				&aw883xx->acf_work,
				msecs_to_jiffies(AW883XX_LOAD_FW_DELAY_TIME));
	}
}

#ifdef AW_MTK_PLATFORM

static const struct snd_soc_dapm_widget aw883xx_dapm_widgets[] = {
	 /* playback */
	SND_SOC_DAPM_AIF_IN("AIF_RX", "Speaker_Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("audio_out"),
	/* capture */
	SND_SOC_DAPM_AIF_OUT("AIF_TX", "Speaker_Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_INPUT("iv_in"),
};

static const struct snd_soc_dapm_route aw883xx_audio_map[] = {
	{"audio_out", NULL, "AIF_RX"},
	{"AIF_TX", NULL, "iv_in"},
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0)
static struct snd_soc_dapm_context *snd_soc_codec_get_dapm(struct snd_soc_codec *codec)
{
	return &codec->dapm;
}
#endif

static int aw883xx_add_widgets(struct aw883xx *aw883xx)
{
	int i = 0;
	int ret;
	struct snd_soc_dapm_widget *aw_widgets = NULL;
	struct snd_soc_dapm_route *aw_route = NULL;
#ifdef AW_KERNEL_VER_OVER_4_19_1
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(aw883xx->codec);
#else
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(aw883xx->codec);
#endif

	/*add widgets*/
	aw_widgets = devm_kzalloc(aw883xx->dev,
				sizeof(struct snd_soc_dapm_widget) * ARRAY_SIZE(aw883xx_dapm_widgets),
				GFP_KERNEL);
	if (!aw_widgets)
		return -ENOMEM;

	memcpy(aw_widgets, aw883xx_dapm_widgets,
			sizeof(struct snd_soc_dapm_widget) * ARRAY_SIZE(aw883xx_dapm_widgets));

	for (i = 0; i < ARRAY_SIZE(aw883xx_dapm_widgets); i++) {
		if (aw_widgets[i].name) {
			ret = aw883xx_append_i2c_suffix("%s_%x_%x", &aw_widgets[i].name, aw883xx);
			if (ret < 0) {
				aw_dev_err(aw883xx->dev, "aw_widgets.name append i2c suffix failed!\n");
				return ret;
			}
		}

		if (aw_widgets[i].sname) {
			ret = aw883xx_append_i2c_suffix("%s_%x_%x", &aw_widgets[i].sname, aw883xx);
			if (ret < 0) {
				aw_dev_err(aw883xx->dev, "aw_widgets.name append i2c suffix failed!");
				return ret;
			}
		}
	}

	snd_soc_dapm_new_controls(dapm, aw_widgets, ARRAY_SIZE(aw883xx_dapm_widgets));

	/*add route*/
	aw_route = devm_kzalloc(aw883xx->dev,
				sizeof(struct snd_soc_dapm_route) * ARRAY_SIZE(aw883xx_audio_map),
				GFP_KERNEL);
	if (!aw_route)
		return -ENOMEM;

	memcpy(aw_route, aw883xx_audio_map,
		sizeof(struct snd_soc_dapm_route) * ARRAY_SIZE(aw883xx_audio_map));

	for (i = 0; i < ARRAY_SIZE(aw883xx_audio_map); i++) {
		if (aw_route[i].sink) {
			ret = aw883xx_append_i2c_suffix("%s_%x_%x", &aw_route[i].sink, aw883xx);
			if (ret < 0) {
				aw_dev_err(aw883xx->dev, "aw_route.sink append i2c suffix failed!");
				return ret;
			}
		}

		if (aw_route[i].source) {
			ret = aw883xx_append_i2c_suffix("%s_%x_%x", &aw_route[i].source, aw883xx);
			if (ret < 0) {
				aw_dev_err(aw883xx->dev, "aw_route.source append i2c suffix failed!");
				return ret;
			}
		}
	}
	snd_soc_dapm_add_routes(dapm, aw_route, ARRAY_SIZE(aw883xx_audio_map));

	return 0;
}
#endif

static int aw883xx_codec_probe(aw_snd_soc_codec_t *aw_codec)
{
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(aw_codec);

	aw_dev_info(aw883xx->dev, "enter");

	/*destroy_workqueue(struct workqueue_struct *wq)*/
	aw883xx->work_queue = create_singlethread_workqueue("aw883xx");
	if (!aw883xx->work_queue) {
		aw_dev_err(aw883xx->dev, "create workqueue failed !");
		return -EINVAL;
	}

	INIT_DELAYED_WORK(&aw883xx->interrupt_work, aw883xx_interrupt_work);
	INIT_DELAYED_WORK(&aw883xx->start_work, aw883xx_startup_work);
	INIT_DELAYED_WORK(&aw883xx->acf_work, aw883xx_fw_wrok);

	aw883xx->codec = aw_codec;

	aw883xx_add_codec_controls(aw883xx);
#ifdef AW_MTK_PLATFORM
	aw883xx_add_widgets(aw883xx);
#endif
	aw883xx_load_fw(aw883xx);

	return 0;
}

#ifdef AW_KERNEL_VER_OVER_4_19_1
static void aw883xx_codec_remove(aw_snd_soc_codec_t *aw_codec)
{
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(aw_codec);

	aw_dev_info(aw883xx->dev, "enter");

	cancel_delayed_work_sync(&aw883xx->interrupt_work);
	cancel_delayed_work_sync(&aw883xx->acf_work);
	cancel_delayed_work_sync(&aw883xx->aw_pa->monitor_desc.delay_work);
	cancel_delayed_work_sync(&aw883xx->start_work);

	if (aw883xx->work_queue)
		destroy_workqueue(aw883xx->work_queue);

	aw_dev_deinit(aw883xx->aw_pa);
}
#else
static int aw883xx_codec_remove(aw_snd_soc_codec_t *aw_codec)
{
	struct aw883xx *aw883xx =
		aw_componet_codec_ops.codec_get_drvdata(aw_codec);

	aw_dev_info(aw883xx->dev, "enter");

	cancel_delayed_work_sync(&aw883xx->interrupt_work);
	cancel_delayed_work_sync(&aw883xx->acf_work);
	cancel_delayed_work_sync(&aw883xx->aw_pa->monitor_desc.delay_work);
	cancel_delayed_work_sync(&aw883xx->start_work);

	if (aw883xx->work_queue)
		destroy_workqueue(aw883xx->work_queue);

	aw_dev_deinit(aw883xx->aw_pa);

	return 0;
}
#endif

#ifdef AW_KERNEL_VER_OVER_4_19_1
static struct snd_soc_component_driver soc_codec_dev_aw883xx = {
	.probe = aw883xx_codec_probe,
	.remove = aw883xx_codec_remove,
};
#else
static struct snd_soc_codec_driver soc_codec_dev_aw883xx = {
	.probe = aw883xx_codec_probe,
	.remove = aw883xx_codec_remove,
};
#endif

static int aw883xx_componet_codec_register(struct aw883xx *aw883xx)
{
	struct snd_soc_dai_driver *dai_drv = NULL;
	int ret;

	dai_drv = devm_kzalloc(aw883xx->dev, sizeof(aw883xx_dai), GFP_KERNEL);
	if (dai_drv == NULL) {
		aw_dev_err(aw883xx->dev, "dai_driver malloc failed");
		return -ENOMEM;
	}

	memcpy(dai_drv, aw883xx_dai, sizeof(aw883xx_dai));

	ret = aw883xx_dai_drv_append_suffix(aw883xx, dai_drv, 1);
	if (ret < 0)
		return ret;

	ret = aw883xx->codec_ops->register_codec(aw883xx->dev,
			&soc_codec_dev_aw883xx,
			dai_drv, ARRAY_SIZE(aw883xx_dai));
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "failed to register aw883xx: %d", ret);
		return ret;
	}

	return 0;
}


static struct aw883xx *aw883xx_malloc_init(struct i2c_client *i2c)
{
	struct aw883xx *aw883xx = devm_kzalloc(&i2c->dev,
			sizeof(struct aw883xx), GFP_KERNEL);
	if (aw883xx == NULL) {
		aw_dev_err(&i2c->dev, "devm_kzalloc failed");
		return NULL;
	}

	aw883xx->dev = &i2c->dev;
	aw883xx->i2c = i2c;
	aw883xx->aw_pa = NULL;
	aw883xx->codec = NULL;
	aw883xx->codec_ops = &aw_componet_codec_ops;
	aw883xx->dbg_en_prof = true;
	aw883xx->allow_pw = true;
	aw883xx->work_queue = NULL;
	aw883xx->i2c_log_en = 0;
	mutex_init(&aw883xx->lock);
	mutex_init(&aw883xx->i2c_lock);

	return aw883xx;
}

static int aw883xx_gpio_request(struct aw883xx *aw883xx)
{
	int ret;

	if (gpio_is_valid(aw883xx->reset_gpio)) {
		ret = devm_gpio_request_one(aw883xx->dev, aw883xx->reset_gpio,
			GPIOF_OUT_INIT_LOW, "aw883xx_rst");
		if (ret) {
			aw_dev_err(aw883xx->dev, "rst request failed");
			return ret;
		}
	}

	if (gpio_is_valid(aw883xx->irq_gpio)) {
		ret = devm_gpio_request_one(aw883xx->dev, aw883xx->irq_gpio,
			GPIOF_DIR_IN, "aw883xx_int");
		if (ret) {
			aw_dev_err(aw883xx->dev, "int request failed");
			return ret;
		}
	}

	return 0;
}

/*****************************************************
 *
 * device tree
 *
 *****************************************************/
static int aw883xx_parse_gpio_dt(struct aw883xx *aw883xx)
{
	struct device_node *np = aw883xx->dev->of_node;

	aw883xx->reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);
	if (aw883xx->reset_gpio < 0) {
		aw_dev_err(aw883xx->dev, "no reset gpio provided, will not hw reset");
		/* return -EIO; */
	} else {
		aw_dev_info(aw883xx->dev, "reset gpio provided ok");
	}

	aw883xx->irq_gpio = of_get_named_gpio(np, "irq-gpio", 0);
	if (aw883xx->irq_gpio < 0)
		aw_dev_info(aw883xx->dev, "no irq gpio provided.");
	else
		aw_dev_info(aw883xx->dev, "irq gpio provided ok.");

	return 0;
}

static void aw883xx_parse_sync_flag_dt(struct aw883xx *aw883xx)
{
	int ret;
	int32_t sync_enable = 0;
	struct device_node *np = aw883xx->dev->of_node;

	ret = of_property_read_u32(np, "sync-flag", &sync_enable);
	if (ret < 0) {
		aw_dev_info(aw883xx->dev,
			"read sync flag failed,default phase sync off");
		sync_enable = false;
	} else {
		aw_dev_info(aw883xx->dev,
			"sync flag is %d", sync_enable);
	}

	aw883xx->phase_sync = sync_enable;
}

static int aw883xx_parse_dt(struct aw883xx *aw883xx)
{
	aw883xx_parse_sync_flag_dt(aw883xx);

	return aw883xx_parse_gpio_dt(aw883xx);
}

static int aw883xx_hw_reset(struct aw883xx *aw883xx)
{
	aw_dev_info(aw883xx->dev, "enter");

	if (gpio_is_valid(aw883xx->reset_gpio)) {
		gpio_set_value_cansleep(aw883xx->reset_gpio, 0);
		usleep_range(AW_1000_US, AW_1000_US + 10);
		gpio_set_value_cansleep(aw883xx->reset_gpio, 1);
		usleep_range(AW_1000_US, AW_1000_US + 10);
	} else {
		aw_dev_err(aw883xx->dev, "failed");
	}
	return 0;
}

static int aw883xx_read_chipid(struct aw883xx *aw883xx)
{
	int ret = -1;
	uint16_t reg_val = 0;

	ret = aw883xx_reg_read(aw883xx, AW883XX_CHIP_ID_REG, &reg_val);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev,
			"failed to read chip id, ret=%d", ret);
		return -EIO;
	}

	aw883xx->chip_id = reg_val;
	aw_dev_info(aw883xx->dev,
			"read chip id: 0x%x", reg_val);

	return 0;
}

/******************************************************
 *
 * irq
 *
 ******************************************************/
static irqreturn_t aw883xx_irq(int irq, void *data)
{
	struct aw883xx *aw883xx = data;

	if (aw883xx == NULL) {
		aw_pr_err("pointer is NULL");
		return -EINVAL;
	}

	aw_dev_info(aw883xx->dev, "enter");
	/*mask all irq*/
	aw_dev_set_intmask(aw883xx->aw_pa, false);

	/*upload workqueue*/
	if (aw883xx->work_queue)
		queue_delayed_work(aw883xx->work_queue,
				&aw883xx->interrupt_work, 0);

	return IRQ_HANDLED;
}

static int aw883xx_interrupt_init(struct aw883xx *aw883xx)
{
	int irq_flags;
	int ret;

	if (gpio_is_valid(aw883xx->irq_gpio)) {
		irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
		ret = devm_request_threaded_irq(aw883xx->dev,
					gpio_to_irq(aw883xx->irq_gpio),
					NULL, aw883xx_irq, irq_flags,
					"aw883xx", aw883xx);
		if (ret) {
			aw_dev_err(aw883xx->dev, "Failed to request IRQ %d: %d",
					gpio_to_irq(aw883xx->irq_gpio), ret);
			return ret;
		}
	} else {
		aw_dev_info(aw883xx->dev, "skipping IRQ registration");
	}

	return 0;
}


/******************************************************
 *
 * sys group attribute: reg
 *
 ******************************************************/
static ssize_t aw883xx_reg_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	int reg_num = aw883xx->aw_pa->ops.aw_get_reg_num();
	ssize_t len = 0;
	uint8_t i = 0;
	uint16_t reg_val = 0;

	for (i = 0; i < reg_num; i++) {
		if (aw883xx->aw_pa->ops.aw_check_rd_access(i)) {
			aw883xx_reg_read(aw883xx, i, &reg_val);
			len += snprintf(buf + len, PAGE_SIZE - len,
					"reg:0x%02x=0x%04x\n", i, reg_val);
		}
	}

	return len;
}

static ssize_t aw883xx_reg_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	unsigned int databuf[2] = { 0 };

	if (2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1]))
		aw883xx_reg_write(aw883xx, databuf[0], databuf[1]);

	return count;
}

static ssize_t aw883xx_rw_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	unsigned int databuf[2] = { 0 };

	if (2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
		aw883xx->reg_addr = (uint8_t)databuf[0];
		if (aw883xx->aw_pa->ops.aw_check_rd_access(databuf[0]))
			aw883xx_reg_write(aw883xx, databuf[0], databuf[1]);
	} else if (1 == sscanf(buf, "%x", &databuf[0])) {
		aw883xx->reg_addr = (uint8_t)databuf[0];
	}

	return count;
}

static ssize_t aw883xx_rw_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint16_t reg_val = 0;

	if (aw883xx->aw_pa->ops.aw_check_rd_access(aw883xx->reg_addr)) {
		aw883xx_reg_read(aw883xx, aw883xx->reg_addr, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"reg:0x%02x=0x%04x\n", aw883xx->reg_addr,
				reg_val);
	}

	return len;
}

static ssize_t aw883xx_drv_ver_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
		"driver_ver: %s \n", AW883XX_DRIVER_VERSION);

	return len;
}

static ssize_t aw883xx_dsp_rw_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint16_t reg_val = 0;

	mutex_lock(&aw883xx->i2c_lock);
	aw883xx_i2c_write(aw883xx, aw883xx->aw_pa->dsp_mem_desc.dsp_madd_reg, aw883xx->dsp_addr);
	aw883xx_i2c_read(aw883xx, aw883xx->aw_pa->dsp_mem_desc.dsp_mdat_reg, &reg_val);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"dsp:0x%04x=0x%04x\n", aw883xx->dsp_addr, reg_val);
	aw883xx_i2c_read(aw883xx, aw883xx->aw_pa->dsp_mem_desc.dsp_mdat_reg, &reg_val);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"dsp:0x%04x=0x%04x\n", aw883xx->dsp_addr + 1, reg_val);
	aw883xx_clear_dsp_sel_st(aw883xx);
	mutex_unlock(&aw883xx->i2c_lock);

	return len;
}

static ssize_t aw883xx_dsp_rw_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	unsigned int databuf[2] = { 0 };

	if (2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
		aw883xx->dsp_addr = (unsigned int)databuf[0];
		aw883xx_dsp_write(aw883xx, databuf[0], databuf[1], AW_DSP_16_DATA);
		aw_dev_dbg(aw883xx->dev, "get param: %x %x",
			databuf[0], databuf[1]);
	} else if (1 == sscanf(buf, "%x", &databuf[0])) {
		aw883xx->dsp_addr = (unsigned int)databuf[0];
		aw_dev_dbg(aw883xx->dev, "get param: %x",
			databuf[0]);
	}
	aw883xx_clear_dsp_sel_st(aw883xx);

	return count;
}

static int aw883xx_awrw_write(struct aw883xx *aw883xx, const char *buf, size_t count)
{
	int  i, ret;
	char *data_buf = NULL;
	int str_len, data_len, temp_data;
	struct aw883xx_i2c_packet *packet = &aw883xx->i2c_packet;
	uint32_t dsp_addr_h = 0, dsp_addr_l = 0;

	data_len = AWRW_DATA_BYTES * packet->reg_num;

	str_len = count - AWRW_HDR_LEN - 1;
	if ((data_len * 5 - 1) > str_len) {
		aw_dev_err(aw883xx->dev, "data_str_len [%d], requeset len [%d]",
					str_len, (data_len * 5 - 1));
		return -EINVAL;
	}

	if (packet->reg_addr == aw883xx->aw_pa->dsp_mem_desc.dsp_madd_reg) {
		if (sscanf(buf + AWRW_HDR_LEN + 1, "0x%02x 0x%02x", &dsp_addr_h, &dsp_addr_l) == 2) {
			packet->dsp_addr = (dsp_addr_h << 8) | dsp_addr_l;
			      packet->dsp_status = AWRW_DSP_READY;
			aw_dev_dbg(aw883xx->dev, "write:reg_addr[0x%02x], dsp_base_addr:[0x%02x]",
							packet->reg_addr, packet->dsp_addr);
			return 0;
		} else {
			aw_dev_err(aw883xx->dev, "get reg 0x%x data failed", packet->reg_addr);
			return -EINVAL;
		}
	}

	mutex_lock(&aw883xx->i2c_lock);
	if (packet->reg_addr == aw883xx->aw_pa->dsp_mem_desc.dsp_mdat_reg) {
		if (packet->dsp_status != AWRW_DSP_READY) {
			aw_dev_err(aw883xx->dev, "please write reg[0x40] first");
			ret = -EINVAL;
			goto exit;
		}
		aw883xx_i2c_write(aw883xx, aw883xx->aw_pa->dsp_mem_desc.dsp_madd_reg, packet->dsp_addr);
		packet->dsp_status = AWRW_DSP_ST_NONE;
	}

	aw_dev_info(aw883xx->dev, "write:reg_addr[0x%02x], reg_num[%d]",
			packet->reg_addr, packet->reg_num);

	data_buf = devm_kzalloc(aw883xx->dev, data_len, GFP_KERNEL);
	if (data_buf == NULL) {
		aw_dev_err(aw883xx->dev, "alloc memory failed");
		ret = -ENOMEM;
		goto exit;
	}

	for (i = 0; i < data_len; i++) {
		sscanf(buf + AWRW_HDR_LEN + 1 + i * 5, "0x%02x", &temp_data);
		data_buf[i] = temp_data;

	}

	ret = aw883xx_i2c_writes(aw883xx, packet->reg_addr, data_buf, data_len);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "write failed");
		devm_kfree(aw883xx->dev, data_buf);
		data_buf = NULL;
		goto exit;
	}

	devm_kfree(aw883xx->dev, data_buf);
	data_buf = NULL;
	aw_dev_info(aw883xx->dev, "write success");
exit:
	mutex_unlock(&aw883xx->i2c_lock);
	return ret;
}

static int aw883xx_awrw_data_check(struct aw883xx *aw883xx, int *data)
{
	if ((data[AWRW_HDR_ADDR_BYTES] != AWRW_ADDR_BYTES) ||
			(data[AWRW_HDR_DATA_BYTES] != AWRW_DATA_BYTES)) {
		aw_dev_err(aw883xx->dev, "addr_bytes [%d] or data_bytes [%d] unsupport",
				data[AWRW_HDR_ADDR_BYTES], data[AWRW_HDR_DATA_BYTES]);
		return -EINVAL;
	}

	return 0;
}

/* flag addr_bytes data_bytes reg_num reg_addr*/
static int aw883xx_awrw_parse_buf(struct aw883xx *aw883xx, const char *buf, size_t count)
{
	int data[AWRW_HDR_MAX] = { 0 };
	struct aw883xx_i2c_packet *packet = &aw883xx->i2c_packet;
	int ret;

	if (sscanf(buf, "0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
		&data[AWRW_HDR_WR_FLAG], &data[AWRW_HDR_ADDR_BYTES], &data[AWRW_HDR_DATA_BYTES],
		&data[AWRW_HDR_REG_NUM], &data[AWRW_HDR_REG_ADDR]) == 5) {

		ret = aw883xx_awrw_data_check(aw883xx, data);
		if (ret < 0)
			return ret;

		packet->reg_addr = data[AWRW_HDR_REG_ADDR];
		packet->reg_num = data[AWRW_HDR_REG_NUM];

		if (data[AWRW_HDR_WR_FLAG] == AWRW_FLAG_WRITE) {
			return aw883xx_awrw_write(aw883xx, buf, count);
		} else if (data[AWRW_HDR_WR_FLAG] == AWRW_FLAG_READ) {
			packet->i2c_status = AWRW_I2C_ST_READ;
			aw_dev_info(aw883xx->dev, "read_cmd:reg_addr[0x%02x], reg_num[%d]",
					packet->reg_addr, packet->reg_num);

		} else {
			aw_dev_err(aw883xx->dev, "please check str format, unsupport flag %d", data[AWRW_HDR_WR_FLAG]);
			return -EINVAL;
		}
	} else {
		aw_dev_err(aw883xx->dev, "can not parse string");
		return -EINVAL;
	}

	return 0;
}

static ssize_t aw883xx_awrw_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	int ret;

	if (count < AWRW_HDR_LEN) {
		aw_dev_err(dev, "data count too smaller, please check write format");
		aw_dev_err(dev, "string %s", buf);
		return -EINVAL;
	}

	ret = aw883xx_awrw_parse_buf(aw883xx, buf, count);
	if (ret)
		return -EINVAL;


	return count;
}

static ssize_t aw883xx_awrw_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	struct aw883xx_i2c_packet *packet = &aw883xx->i2c_packet;
	int data_len, len = 0;
	int ret, i;
	uint8_t *reg_data = NULL;

	if (packet->i2c_status != AWRW_I2C_ST_READ) {
		aw_dev_err(aw883xx->dev, "please write read cmd first");
		return -EINVAL;
	}

	mutex_lock(&aw883xx->i2c_lock);
	if (packet->reg_addr == aw883xx->aw_pa->dsp_mem_desc.dsp_mdat_reg) {
		if (packet->dsp_status != AWRW_DSP_READY) {
			aw_dev_err(aw883xx->dev, "please write reg[0x40] first");
			mutex_unlock(&aw883xx->i2c_lock);
			return -EINVAL;
		}
		ret = aw883xx_i2c_write(aw883xx, aw883xx->aw_pa->dsp_mem_desc.dsp_madd_reg, packet->dsp_addr);
		if (ret < 0) {
			mutex_unlock(&aw883xx->i2c_lock);
			return ret;
		}
		packet->dsp_status = AWRW_DSP_ST_NONE;
	}

	data_len = AWRW_DATA_BYTES * packet->reg_num;
	reg_data = devm_kzalloc(dev, data_len, GFP_KERNEL);
	if (reg_data == NULL) {
		aw_dev_err(aw883xx->dev, "memory alloc failed");
		ret = -EINVAL;
		goto exit;
	}

	ret = aw883xx_i2c_reads(aw883xx, packet->reg_addr, reg_data, data_len);
	if (ret < 0) {
		ret = -EFAULT;
		goto exit;
	}

	aw_dev_info(aw883xx->dev, "reg_addr 0x%02x, reg_num %d",
			packet->reg_addr, packet->reg_num);

	for (i = 0; i < data_len; i++) {
		len += snprintf(buf + len, PAGE_SIZE - len,
			"0x%02x,", reg_data[i]);

	}
	ret = len;

exit:
	if (reg_data) {
		devm_kfree(dev, reg_data);
		reg_data = NULL;
	}
	mutex_unlock(&aw883xx->i2c_lock);
	packet->i2c_status = AWRW_I2C_ST_NONE;
	return ret;
}

static ssize_t aw883xx_fade_step_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	unsigned int databuf = 0;

	if (1 == sscanf(buf, "%d", &databuf)) {
		if (databuf > (aw883xx->aw_pa->volume_desc.mute_volume)) {
			aw_dev_info(aw883xx->dev, "step overflow %d Db", databuf);
			return count;
		}
		aw_dev_set_fade_vol_step(aw883xx->aw_pa, databuf);
	}
	aw_dev_info(aw883xx->dev, "set step %d DB Done", databuf);

	return count;
}

static ssize_t aw883xx_fade_step_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct aw883xx *aw883xx = dev_get_drvdata(dev);

	len += snprintf(buf+len, PAGE_SIZE-len,
		"step: %d \n", aw_dev_get_fade_vol_step(aw883xx->aw_pa));

	return len;
}

static ssize_t aw883xx_dbg_prof_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	unsigned int databuf = 0;

	if (1 == sscanf(buf, "%d", &databuf)) {
		if (databuf)
			aw883xx->dbg_en_prof = true;
		else
			aw883xx->dbg_en_prof = false;
	}
	aw_dev_info(aw883xx->dev, "en_prof %d  Done", databuf);

	return count;
}

static ssize_t aw883xx_dbg_prof_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
		" %d\n", aw883xx->dbg_en_prof);

	return len;
}

static ssize_t aw883xx_spk_temp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	ssize_t len = 0;
	int ret;
	int32_t te;

	ret = aw_cali_svc_get_dev_te(&aw883xx->aw_pa->cali_desc, &te);
	if (ret < 0)
		return ret;

	len += snprintf(buf + len, PAGE_SIZE - len,
			"Temp:%d\n", te);

	return len;
}

static ssize_t aw883xx_sync_flag_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	unsigned int flag = 0;
	int ret;

	ret = kstrtouint(buf, 0, &flag);
	if (ret < 0)
		return ret;

	flag = ((flag == false) ? false : true);

	aw_dev_info(aw883xx->dev, "set phase sync flag : [%d]", flag);

	aw883xx->phase_sync = flag;

	return count;
}

static ssize_t aw883xx_sync_flag_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len,
				"sync flag : %d\n", aw883xx->phase_sync);

	return len;
}

static ssize_t aw883xx_fade_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	uint32_t fade_en = 0;

	if (1 == sscanf(buf, "%u", &fade_en))
		aw883xx->aw_pa->fade_en = fade_en;

	aw_dev_info(aw883xx->dev, "set fade_en %d", aw883xx->aw_pa->fade_en);

	return count;
}

static ssize_t aw883xx_fade_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len,
		"fade_en: %d\n", aw883xx->aw_pa->fade_en);

	return len;
}

static ssize_t aw883xx_dsp_re_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	ssize_t len = 0;
	int ret;
	uint32_t read_re = 0;

	ret = aw_cali_read_cali_re_from_dsp(&aw883xx->aw_pa->cali_desc, &read_re);
	if (ret < 0) {
		aw_dev_err(aw883xx->dev, "%s:read dsp re fail\n", __func__);
		return ret;
	}

	len += snprintf((char *)(buf + len),
		PAGE_SIZE - len,
		"dsp_re: %d\n", read_re);

	return len;
}

static ssize_t aw883xx_log_en_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "i2c_log_en: %d\n",
		aw883xx->i2c_log_en);

	return len;
}

static ssize_t aw883xx_log_en_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	uint32_t log_en = 0;

	if (1 == sscanf(buf, "%u", &log_en))
		aw883xx->i2c_log_en = log_en;

	aw_dev_info(aw883xx->dev, "set i2c_log_en: %d",
		aw883xx->i2c_log_en);

	return count;
}

static int aw883xx_dsp_log_info(struct aw883xx *aw883xx, unsigned int base_addr,
				uint32_t data_len, char *format)
{
	uint16_t reg_val = 0;
	char *dsp_reg_info = NULL;
	ssize_t dsp_info_len = 0;
	int i;

	dsp_reg_info = devm_kzalloc(aw883xx->dev, AW_NAME_BUF_MAX, GFP_KERNEL);
	if (dsp_reg_info == NULL) {
		aw_dev_err(aw883xx->dev, "dsp_reg_info kzalloc failed");
		return -ENOMEM;
	}

	mutex_lock(&aw883xx->i2c_lock);
	aw883xx_i2c_write(aw883xx, aw883xx->aw_pa->dsp_mem_desc.dsp_madd_reg, base_addr);

	for (i = 0; i < data_len; i += 2) {
		aw883xx_i2c_read(aw883xx, aw883xx->aw_pa->dsp_mem_desc.dsp_mdat_reg, &reg_val);
		dsp_info_len += snprintf(dsp_reg_info + dsp_info_len, AW_NAME_BUF_MAX - dsp_info_len,
			"%02x,%02x,", (reg_val >> 0) & 0xff,
					(reg_val >> 8) & 0xff);
		if ((i / 2 + 1) % 8 == 0) {
			aw_dev_info(aw883xx->dev, "%s: %s", format, dsp_reg_info);
			dsp_info_len = 0;
			memset(dsp_reg_info, 0, AW_NAME_BUF_MAX);
		}

		if (((data_len) % 8 != 0) &&
			(i == (data_len - 2))) {
			aw_dev_info(aw883xx->dev, "%s: %s", format, dsp_reg_info);
			dsp_info_len = 0;
			memset(dsp_reg_info, 0, AW_NAME_BUF_MAX);
		}
	}

	memset(dsp_reg_info, 0, AW_NAME_BUF_MAX);
	devm_kfree(aw883xx->dev, dsp_reg_info);
	dsp_reg_info = NULL;
	mutex_unlock(&aw883xx->i2c_lock);

	return 0;
}

static ssize_t aw883xx_dsp_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct aw883xx *aw883xx = dev_get_drvdata(dev);
	ssize_t len = 0;
	int ret = -1;
	uint32_t data_len;

	if (aw883xx->aw_pa->dsp_cfg == AW_DEV_DSP_BYPASS) {
		len += snprintf((char *)(buf + len), PAGE_SIZE - len,
				"%s: dsp bypass\n", __func__);
	} else {
		len += snprintf((char *)(buf + len), PAGE_SIZE - len,
				"%s: dsp working\n", __func__);
		ret = aw_dev_get_iis_status(aw883xx->aw_pa);
		if (ret < 0) {
			len += snprintf((char *)(buf + len),
					PAGE_SIZE - len,
					"%s: no iis signal\n",
					__func__);
			aw_dev_err(aw883xx->dev, "no iis signal, dsp show failed");
			return len;
		}

		len += snprintf(buf + len, PAGE_SIZE - len,
				"dsp firmware and config info is displayed in the kernel log\n");

		aw_dev_info(aw883xx->dev, "dsp_firmware_len:%d", aw883xx->aw_pa->dsp_fw_len);
		ret = aw883xx_dsp_log_info(aw883xx, aw883xx->aw_pa->dsp_mem_desc.dsp_fw_base_addr,
			aw883xx->aw_pa->dsp_fw_len, "dsp_fw");
		if (ret < 0) {
			aw_dev_err(aw883xx->dev, "dsp_fw display failed");
			return len;
		}

		aw_dev_info(aw883xx->dev, "dsp_config_len:%d", aw883xx->aw_pa->dsp_cfg_len);
		ret = aw883xx_dsp_log_info(aw883xx, aw883xx->aw_pa->dsp_mem_desc.dsp_cfg_base_addr,
			aw883xx->aw_pa->dsp_cfg_len, "dsp_config");
		if (ret < 0) {
			aw_dev_err(aw883xx->dev, "dsp_config display failed");
			return len;
		}

		aw_dev_info(aw883xx->dev, "dsp_config:0x8180-0x83fc");
		data_len = 2 * (aw883xx->aw_pa->dsp_st_desc.dsp_reg_e1 -
			aw883xx->aw_pa->dsp_st_desc.dsp_reg_s1);
		ret = aw883xx_dsp_log_info(aw883xx, aw883xx->aw_pa->dsp_st_desc.dsp_reg_s1,
			data_len, "dsp_st");
		if (ret < 0) {
			aw_dev_err(aw883xx->dev, "dsp_config:0x8180-0x83fc failed");
			return len;
		}

		aw_dev_info(aw883xx->dev, "dsp_config:0x9c00-0x9c5c");
		data_len = 2 * (aw883xx->aw_pa->dsp_st_desc.dsp_reg_e2 -
			aw883xx->aw_pa->dsp_st_desc.dsp_reg_s2);
		ret = aw883xx_dsp_log_info(aw883xx, aw883xx->aw_pa->dsp_st_desc.dsp_reg_s2,
					   data_len, "dsp_st");
		if (ret < 0) {
			aw_dev_err(aw883xx->dev, "dsp_config:0x9c00-0x9c5c display failed");
			return len;
		}
	}
	return len;
}

static DEVICE_ATTR(reg, S_IWUSR | S_IRUGO,
		aw883xx_reg_show, aw883xx_reg_store);
static DEVICE_ATTR(rw, S_IWUSR | S_IRUGO,
		aw883xx_rw_show, aw883xx_rw_store);
static DEVICE_ATTR(drv_ver, S_IRUGO,
		aw883xx_drv_ver_show, NULL);
static DEVICE_ATTR(dsp_rw, S_IWUSR | S_IRUGO,
		aw883xx_dsp_rw_show, aw883xx_dsp_rw_store);
static DEVICE_ATTR(awrw, S_IWUSR | S_IRUGO,
		aw883xx_awrw_show, aw883xx_awrw_store);
static DEVICE_ATTR(fade_step, S_IWUSR | S_IRUGO,
		aw883xx_fade_step_show, aw883xx_fade_step_store);
static DEVICE_ATTR(dbg_prof, S_IWUSR | S_IRUGO,
		aw883xx_dbg_prof_show, aw883xx_dbg_prof_store);
static DEVICE_ATTR(spk_temp, S_IRUGO,
		aw883xx_spk_temp_show, NULL);
static DEVICE_ATTR(phase_sync, S_IWUSR | S_IRUGO,
		aw883xx_sync_flag_show, aw883xx_sync_flag_store);
static DEVICE_ATTR(fade_en, S_IWUSR | S_IRUGO,
		aw883xx_fade_enable_show, aw883xx_fade_enable_store);
static DEVICE_ATTR(dsp_re,     S_IRUGO,
		aw883xx_dsp_re_show, NULL);
static DEVICE_ATTR(i2c_log_en, S_IWUSR | S_IRUGO,
		aw883xx_log_en_show, aw883xx_log_en_store);
static DEVICE_ATTR(dsp, S_IRUGO,
		aw883xx_dsp_show, NULL);


static struct attribute *aw883xx_attributes[] = {
	&dev_attr_reg.attr,
	&dev_attr_rw.attr,
	&dev_attr_drv_ver.attr,
	&dev_attr_dsp_rw.attr,
	&dev_attr_awrw.attr,
	&dev_attr_fade_step.attr,
	&dev_attr_dbg_prof.attr,
	&dev_attr_spk_temp.attr,
	&dev_attr_phase_sync.attr,
	&dev_attr_fade_en.attr,
	&dev_attr_dsp_re.attr,
	&dev_attr_i2c_log_en.attr,
	&dev_attr_dsp.attr,
	NULL
};

static struct attribute_group aw883xx_attribute_group = {
	.attrs = aw883xx_attributes
};



/******************************************************
 *
 * i2c driver
 *
 ******************************************************/
static int aw883xx_i2c_probe(struct i2c_client *i2c,
				const struct i2c_device_id *id)
{
	struct aw883xx *aw883xx = NULL;
	int ret = -1;

	aw_dev_info(&i2c->dev, "enter");

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		aw_dev_err(&i2c->dev, "check_functionality failed");
		return -EIO;
	}

	aw883xx = aw883xx_malloc_init(i2c);
	if (aw883xx == NULL) {
		aw_dev_err(&i2c->dev, "malloc aw883xx failed");
		return -ENOMEM;
	}
	i2c_set_clientdata(i2c, aw883xx);

	ret = aw883xx_parse_dt(aw883xx);
	if (ret < 0) {
		aw_dev_err(&i2c->dev, "parse dts failed");
		return ret;
	}

	/*get gpio resource*/
	ret = aw883xx_gpio_request(aw883xx);
	if (ret)
		return ret;

	/* hardware reset */
	aw883xx_hw_reset(aw883xx);

	/* aw883xx chip id */
	ret = aw883xx_read_chipid(aw883xx);
	if (ret < 0) {
		aw_dev_err(&i2c->dev, "aw883xx_read_chipid failed ret=%d", ret);
		return ret;
	}

	/*aw pa init*/
	ret = aw883xx_init(aw883xx);
	if (ret < 0)
		return ret;

	ret = aw883xx_platform_init(aw883xx);
	if (ret < 0) {
		aw_dev_err(&i2c->dev, "get platform failed");
		return ret;
	}

	ret = aw883xx_interrupt_init(aw883xx);
	if (ret < 0)
		return ret;

	ret = aw883xx_componet_codec_register(aw883xx);
	if (ret) {
		aw_dev_err(&i2c->dev, "codec register failed");
		return ret;
	}

	ret = sysfs_create_group(&i2c->dev.kobj, &aw883xx_attribute_group);
	if (ret < 0) {
		aw_dev_info(&i2c->dev, "error creating sysfs attr files");
		goto err_sysfs;
	}

	dev_set_drvdata(&i2c->dev, aw883xx);

	/*add device to total list*/
	mutex_lock(&g_aw883xx_lock);
	g_aw883xx_dev_cnt++;
	mutex_unlock(&g_aw883xx_lock);

	aw_dev_info(&i2c->dev, "dev_cnt %d probe completed successfully",
		g_aw883xx_dev_cnt);

	return 0;


err_sysfs:
	aw_componet_codec_ops.unregister_codec(&i2c->dev);
	return ret;
}

static int aw883xx_i2c_remove(struct i2c_client *i2c)
{
	struct aw883xx *aw883xx = i2c_get_clientdata(i2c);

	aw_dev_info(aw883xx->dev, "enter");

	if (gpio_to_irq(aw883xx->irq_gpio))
		devm_free_irq(&i2c->dev,
			gpio_to_irq(aw883xx->irq_gpio),
			aw883xx);

	if (gpio_is_valid(aw883xx->irq_gpio))
		devm_gpio_free(&i2c->dev, aw883xx->irq_gpio);
	if (gpio_is_valid(aw883xx->reset_gpio))
		devm_gpio_free(&i2c->dev, aw883xx->reset_gpio);

	sysfs_remove_group(&aw883xx->dev->kobj,
			&aw883xx_attribute_group);

	/*free device resource */
	aw_device_remove(aw883xx->aw_pa);

	aw_componet_codec_ops.unregister_codec(&i2c->dev);

	mutex_lock(&g_aw883xx_lock);
	g_aw883xx_dev_cnt--;
	if (g_aw883xx_dev_cnt == 0) {
		vfree(g_awinic_cfg);
		g_awinic_cfg = NULL;
	}
	mutex_unlock(&g_aw883xx_lock);

	return 0;
}

static const struct i2c_device_id aw883xx_i2c_id[] = {
	{AW883XX_I2C_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, aw883xx_i2c_id);

static struct of_device_id aw883xx_dt_match[] = {
	{.compatible = "awinic,aw883xx_smartpa"},
	{},
};

static struct i2c_driver aw883xx_i2c_driver = {
	.driver = {
		.name = AW883XX_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(aw883xx_dt_match),
	},
	.probe = aw883xx_i2c_probe,
	.remove = aw883xx_i2c_remove,
	.id_table = aw883xx_i2c_id,
};

static int __init aw883xx_i2c_init(void)
{
	int ret = -1;

	aw_pr_info("aw883xx driver version %s", AW883XX_DRIVER_VERSION);

	ret = i2c_add_driver(&aw883xx_i2c_driver);
	if (ret)
		aw_pr_err("fail to add aw883xx device into i2c");

	return ret;
}
module_init(aw883xx_i2c_init);

static void __exit aw883xx_i2c_exit(void)
{
	i2c_del_driver(&aw883xx_i2c_driver);
}
module_exit(aw883xx_i2c_exit);

MODULE_DESCRIPTION("ASoC AW883XX Smart PA Driver");
MODULE_LICENSE("GPL v2");

