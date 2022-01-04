/*
 * aw_device.c  aw87xxx pa module
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
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/timer.h>
#include "aw87xxx.h"
#include "aw_device.h"
#include "aw_log.h"
#include "aw87xxx_pid_9b_reg.h"
#include "aw87xxx_pid_18_reg.h"
#include "aw87xxx_pid_39_reg.h"
#include "aw87xxx_pid_59_3x9_reg.h"
#include "aw87xxx_pid_59_5x9_reg.h"
#include "aw87xxx_pid_5a_reg.h"
#include "aw87xxx_pid_76_reg.h"

/*************************************************************************
 * aw87xxx variable
 ************************************************************************/
const char *g_aw_pid_9b_product[] = {
	"aw87319",
};

const char *g_aw_pid_39_product[] = {
	"aw87329",
	"aw87339",
	"aw87349",
};

const char *g_aw_pid_59_3x9_product[] = {
	"aw87359",
	"aw87389",
};

const char *g_aw_pid_59_5x9_product[] = {
	"aw87509",
	"aw87519",
	"aw87529",
	"aw87539",
};

const char *g_aw_pid_5a_product[] = {
	"aw87549",
	"aw87559",
	"aw87569",
	"aw87579",
	"aw81509",
};

const char *g_aw_pid_76_product[] = {
	"aw87390",
	"aw87320",
	"aw87401",
};

static int aw_dev_get_chipid(struct aw_device *aw_dev);

/***************************************************************************
 *
 * reading and writing of I2C bus
 *
 ***************************************************************************/
int aw_dev_i2c_write_byte(struct aw_device *aw_dev,
			uint8_t reg_addr, uint8_t reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_write_byte_data(aw_dev->i2c, reg_addr, reg_data);
		if (ret < 0)
			AW_DEV_LOGE(aw_dev->dev, "i2c_write cnt=%d error=%d",
				cnt, ret);
		else
			break;

		cnt++;
		msleep(AW_I2C_RETRY_DELAY);
	}

	return ret;
}

int aw_dev_i2c_read_byte(struct aw_device *aw_dev,
			uint8_t reg_addr, uint8_t *reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_read_byte_data(aw_dev->i2c, reg_addr);
		if (ret < 0) {
			AW_DEV_LOGE(aw_dev->dev, "i2c_read cnt=%d error=%d",
				cnt, ret);
		} else {
			*reg_data = ret;
			break;
		}
		cnt++;
		msleep(AW_I2C_RETRY_DELAY);
	}

	return ret;
}

int aw_dev_i2c_read_msg(struct aw_device *aw_dev,
	uint8_t reg_addr, uint8_t *data_buf, uint32_t data_len)
{
	int ret = -1;

	struct i2c_msg msg[] = {
	[0] = {
		.addr = aw_dev->i2c_addr,
		.flags = 0,
		.len = sizeof(uint8_t),
		.buf = &reg_addr,
		},
	[1] = {
		.addr = aw_dev->i2c_addr,
		.flags = I2C_M_RD,
		.len = data_len,
		.buf = data_buf,
		},
	};

	ret = i2c_transfer(aw_dev->i2c->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0) {
		AW_DEV_LOGE(aw_dev->dev, "transfer failed");
		return ret;
	} else if (ret != AW_I2C_READ_MSG_NUM) {
		AW_DEV_LOGE(aw_dev->dev, "transfer failed(size error)");
		return -ENXIO;
	}

	return 0;
}

int aw_dev_i2c_write_bits(struct aw_device *aw_dev,
	uint8_t reg_addr, uint8_t mask, uint8_t reg_data)
{
	int ret = -1;
	unsigned char reg_val = 0;

	ret = aw_dev_i2c_read_byte(aw_dev, reg_addr, &reg_val);
	if (ret < 0) {
		AW_DEV_LOGE(aw_dev->dev, "i2c read error, ret=%d", ret);
		return ret;
	}
	reg_val &= mask;
	reg_val |= reg_data;
	ret = aw_dev_i2c_write_byte(aw_dev, reg_addr, reg_val);
	if (ret < 0) {
		AW_DEV_LOGE(aw_dev->dev, "i2c write error, ret=%d", ret);
		return ret;
	}

	return 0;
}

/************************************************************************
 *
 * aw87xxx device update profile data to registers
 *
 ************************************************************************/
static int aw_dev_reg_update(struct aw_device *aw_dev,
			struct aw_data_container *profile_data)
{
	int i = 0;
	int ret = -1;

	if (profile_data == NULL)
		return -EINVAL;

	if (aw_dev->hwen_status == AW_DEV_HWEN_OFF) {
		AW_DEV_LOGE(aw_dev->dev, "dev is pwr_off,can not update reg");
		return -EINVAL;
	}

	for (i = 0; i < profile_data->len; i = i + 2) {
		AW_DEV_LOGI(aw_dev->dev, "reg=0x%02x, val = 0x%02x",
			profile_data->data[i], profile_data->data[i + 1]);

		ret = aw_dev_i2c_write_byte(aw_dev, profile_data->data[i],
				profile_data->data[i + 1]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void aw_dev_reg_mute_bits_set(struct aw_device *aw_dev,
				uint8_t *reg_val, bool enable)
{
	if (enable) {
		*reg_val &= aw_dev->mute_desc.mask;
		*reg_val |= aw_dev->mute_desc.enable;
	} else {
		*reg_val &= aw_dev->mute_desc.mask;
		*reg_val |= aw_dev->mute_desc.disable;
	}
}

/************************************************************************
 *
 * aw87xxx device hadware and soft contols
 *
 ************************************************************************/
static bool aw_dev_gpio_is_valid(struct aw_device *aw_dev)
{
	if (gpio_is_valid(aw_dev->rst_gpio))
		return true;
	else
		return false;
}

void aw_dev_hw_pwr_ctrl(struct aw_device *aw_dev, bool enable)
{
	if (aw_dev->hwen_status == AW_DEV_HWEN_INVALID) {
		AW_DEV_LOGD(aw_dev->dev, "product not have reset-pin,hardware pwd control invalid");
		return;
	}
	if (enable) {
		if (aw_dev_gpio_is_valid(aw_dev)) {
			gpio_set_value_cansleep(aw_dev->rst_gpio, AW_GPIO_LOW_LEVEL);
			mdelay(2);
			gpio_set_value_cansleep(aw_dev->rst_gpio, AW_GPIO_HIGHT_LEVEL);
			mdelay(2);
			aw_dev->hwen_status = AW_DEV_HWEN_ON;
			AW_DEV_LOGI(aw_dev->dev, "hw power on");
		} else {
			AW_DEV_LOGI(aw_dev->dev, "hw already power on");
		}
	} else {
		if (aw_dev_gpio_is_valid(aw_dev)) {
			gpio_set_value_cansleep(aw_dev->rst_gpio, AW_GPIO_LOW_LEVEL);
			mdelay(2);
			aw_dev->hwen_status = AW_DEV_HWEN_OFF;
			AW_DEV_LOGI(aw_dev->dev, "hw power off");
		} else {
			AW_DEV_LOGI(aw_dev->dev, "hw already power off");
		}
	}
}

int aw_dev_mute_ctrl(struct aw_device *aw_dev, bool enable)
{
	int ret = 0;

	if (enable) {
		ret = aw_dev_i2c_write_bits(aw_dev, aw_dev->mute_desc.addr,
				aw_dev->mute_desc.mask, aw_dev->mute_desc.enable);
		if (ret < 0)
			return ret;
		AW_DEV_LOGI(aw_dev->dev, "set mute down");
	} else {
		ret = aw_dev_i2c_write_bits(aw_dev, aw_dev->mute_desc.addr,
				aw_dev->mute_desc.mask, aw_dev->mute_desc.disable);
		if (ret < 0)
			return ret;
		AW_DEV_LOGI(aw_dev->dev, "close mute down");
	}

	return 0;
}

void aw_dev_soft_reset(struct aw_device *aw_dev)
{
	int i = 0;
	int ret = -1;
	struct aw_soft_rst_desc *soft_rst = &aw_dev->soft_rst_desc;

	AW_DEV_LOGD(aw_dev->dev, "enter");

	if (aw_dev->hwen_status == AW_DEV_HWEN_OFF) {
		AW_DEV_LOGE(aw_dev->dev, "hw is off,can not softrst");
		return;
	}

	if (aw_dev->soft_rst_enable == AW_DEV_SOFT_RST_DISENABLE) {
		AW_DEV_LOGD(aw_dev->dev, "softrst is disenable");
		return;
	}

	if (soft_rst->access == NULL || soft_rst->len == 0) {
		AW_DEV_LOGE(aw_dev->dev, "softrst_info not init");
		return;
	}

	if (soft_rst->len % 2) {
		AW_DEV_LOGE(aw_dev->dev, "softrst data_len[%d] is odd number,data not available",
			aw_dev->soft_rst_desc.len);
		return;
	}

	for (i = 0; i < soft_rst->len / sizeof(uint8_t); i += 2) {
		AW_DEV_LOGD(aw_dev->dev, "softrst_reg=0x%02x, val = 0x%02x",
			soft_rst->access[i], soft_rst->access[i + 1]);

		ret = aw_dev_i2c_write_byte(aw_dev, soft_rst->access[i],
				soft_rst->access[i + 1]);
		if (ret < 0) {
			AW_DEV_LOGE(aw_dev->dev, "write failed,ret = %d,cnt=%d",
				ret, i);
			return;
		}
	}
	AW_DEV_LOGD(aw_dev->dev, "down");
}


int aw_dev_default_pwr_off(struct aw_device *aw_dev,
		struct aw_data_container *profile_data)
{
	int ret = 0;

	AW_DEV_LOGD(aw_dev->dev, "enter");
	if (aw_dev->hwen_status == AW_DEV_HWEN_OFF) {
		AW_DEV_LOGE(aw_dev->dev, "hwen is already off");
		return 0;
	}

	if (aw_dev->soft_off_enable && profile_data) {
		ret = aw_dev_reg_update(aw_dev, profile_data);
		if (ret < 0) {
			AW_DEV_LOGE(aw_dev->dev, "update profile[Off] fw config failed");
			goto reg_off_update_failed;
		}
	}

	aw_dev_hw_pwr_ctrl(aw_dev, false);
	AW_DEV_LOGD(aw_dev->dev, "down");
	return 0;

reg_off_update_failed:
	aw_dev_hw_pwr_ctrl(aw_dev, false);
	return ret;
}


/************************************************************************
 *
 * aw87xxx device power on process function
 *
 ************************************************************************/

int aw_dev_default_pwr_on(struct aw_device *aw_dev,
			struct aw_data_container *profile_data)
{
	int ret = 0;

	/*hw power on*/
	aw_dev_hw_pwr_ctrl(aw_dev, true);

	ret = aw_dev_reg_update(aw_dev, profile_data);
	if (ret < 0)
		return ret;

	return 0;
}

/****************************************************************************
 *
 * aw87xxx chip esd status check
 *
 ****************************************************************************/
int aw_dev_esd_reg_status_check(struct aw_device *aw_dev)
{
	int ret;
	unsigned char reg_val = 0;
	struct aw_esd_check_desc *esd_desc = &aw_dev->esd_desc;

	AW_DEV_LOGD(aw_dev->dev, "enter");

	if (!esd_desc->first_update_reg_addr) {
		AW_DEV_LOGE(aw_dev->dev, "esd check info if not init,please check");
		return -EINVAL;
	}

	ret = aw_dev_i2c_read_byte(aw_dev, esd_desc->first_update_reg_addr,
			&reg_val);
	if (ret < 0) {
		AW_DEV_LOGE(aw_dev->dev, "read reg 0x%02x failed",
			esd_desc->first_update_reg_addr);
		return ret;
	}

	AW_DEV_LOGD(aw_dev->dev, "0x%02x:default val=0x%02x real val=0x%02x",
		esd_desc->first_update_reg_addr,
		esd_desc->first_update_reg_val, reg_val);

	if (reg_val == esd_desc->first_update_reg_val) {
		AW_DEV_LOGE(aw_dev->dev, "reg status check failed");
		return -EINVAL;
	}
	return 0;
}

int aw_dev_check_reg_is_rec_mode(struct aw_device *aw_dev)
{
	int ret;
	unsigned char reg_val = 0;
	struct aw_rec_mode_desc *rec_desc = &aw_dev->rec_desc;

	if (!rec_desc->addr) {
		AW_DEV_LOGE(aw_dev->dev, "rec check info if not init,please check");
		return -EINVAL;
	}

	ret = aw_dev_i2c_read_byte(aw_dev, rec_desc->addr, &reg_val);
	if (ret < 0) {
		AW_DEV_LOGE(aw_dev->dev, "read reg 0x%02x failed",
			rec_desc->addr);
		return ret;
	}

	if (rec_desc->enable) {
		if (reg_val & ~(rec_desc->mask)) {
			AW_DEV_LOGI(aw_dev->dev, "reg status is receiver mode");
			aw_dev->is_rec_mode = AW_IS_REC_MODE;
		} else {
			aw_dev->is_rec_mode = AW_NOT_REC_MODE;
		}
	} else {
		if (!(reg_val & ~(rec_desc->mask))) {
			AW_DEV_LOGI(aw_dev->dev, "reg status is receiver mode");
			aw_dev->is_rec_mode = AW_IS_REC_MODE;
		} else {
			aw_dev->is_rec_mode = AW_NOT_REC_MODE;
		}
	}
	return 0;
}


/****************************************************************************
 *
 * aw87xxx product attributes init info
 *
 ****************************************************************************/

/********************** aw87xxx_pid_9A attributes ***************************/

static int aw_dev_pid_9b_reg_update(struct aw_device *aw_dev,
			struct aw_data_container *profile_data)
{
	int i = 0;
	int ret = -1;
	uint8_t reg_val = 0;

	if (profile_data == NULL)
		return -EINVAL;

	if (aw_dev->hwen_status == AW_DEV_HWEN_OFF) {
		AW_DEV_LOGE(aw_dev->dev, "dev is pwr_off,can not update reg");
		return -EINVAL;
	}

	if (profile_data->len != AW_PID_9B_BIN_REG_CFG_COUNT) {
		AW_DEV_LOGE(aw_dev->dev, "reg_config count of bin is error,can not update reg");
		return -EINVAL;
	}
	ret = aw_dev_i2c_write_byte(aw_dev, AW87XXX_PID_9B_ENCRYPTION_REG,
		AW87XXX_PID_9B_ENCRYPTION_BOOST_OUTPUT_SET);
	if (ret < 0)
		return ret;

	for (i = 1; i < AW_PID_9B_BIN_REG_CFG_COUNT; i++) {
		AW_DEV_LOGI(aw_dev->dev, "reg=0x%02x, val = 0x%02x",
			i, profile_data->data[i]);
		reg_val = profile_data->data[i];
		if (i == AW87XXX_PID_9B_SYSCTRL_REG) {
			aw_dev_reg_mute_bits_set(aw_dev, &reg_val, true);
			AW_DEV_LOGD(aw_dev->dev, "change mute_mask, val = 0x%02x",
				reg_val);
		}

		ret = aw_dev_i2c_write_byte(aw_dev, i, reg_val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int aw_dev_pid_9b_pwr_on(struct aw_device *aw_dev, struct aw_data_container *data)
{
	int ret = 0;

	/*hw power on*/
	aw_dev_hw_pwr_ctrl(aw_dev, true);

	/* open the mute */
	ret = aw_dev_mute_ctrl(aw_dev, true);
	if (ret < 0)
		return ret;

	/* Update scene parameters in mute mode */
	ret = aw_dev_pid_9b_reg_update(aw_dev, data);
	if (ret < 0)
		return ret;

	/* close the mute */
	aw_dev_mute_ctrl(aw_dev, false);
	if (ret < 0)
		return ret;

	return 0;
}

static void aw_dev_pid_9b_init(struct aw_device *aw_dev)
{
	/* Product register permission info */
	aw_dev->reg_max_addr = AW87XXX_PID_9B_REG_MAX;
	aw_dev->reg_access = aw87xxx_pid_9b_reg_access;

	aw_dev->mute_desc.addr = AW87XXX_PID_9B_SYSCTRL_REG;
	aw_dev->mute_desc.mask = AW87XXX_PID_9B_REG_EN_SW_MASK;
	aw_dev->mute_desc.enable = AW87XXX_PID_9B_REG_EN_SW_DISABLE_VALUE;
	aw_dev->mute_desc.disable = AW87XXX_PID_9B_REG_EN_SW_ENABLE_VALUE;
	aw_dev->ops.pwr_on_func = aw_dev_pid_9b_pwr_on;

	/* software reset control info */
	aw_dev->soft_rst_desc.len = sizeof(aw87xxx_pid_9b_softrst_access);
	aw_dev->soft_rst_desc.access = aw87xxx_pid_9b_softrst_access;
	aw_dev->soft_rst_enable = AW_DEV_SOFT_RST_ENABLE;

	/* Whether to allow register operation to power off */
	aw_dev->soft_off_enable = AW_DEV_SOFT_OFF_DISENABLE;

	aw_dev->product_tab = g_aw_pid_9b_product;
	aw_dev->product_cnt = AW87XXX_PID_9B_PRODUCT_MAX;

	aw_dev->rec_desc.addr = AW87XXX_PID_9B_SYSCTRL_REG;
	aw_dev->rec_desc.disable = AW87XXX_PID_9B_SPK_MODE_ENABLE;
	aw_dev->rec_desc.enable = AW87XXX_PID_9B_SPK_MODE_DISABLE;
	aw_dev->rec_desc.mask = AW87XXX_PID_9B_SPK_MODE_MASK;

	/* esd reg info */
	aw_dev->esd_desc.first_update_reg_addr = AW87XXX_PID_9B_SYSCTRL_REG;
	aw_dev->esd_desc.first_update_reg_val = AW87XXX_PID_9B_SYSCTRL_DEFAULT;
}

static int aw_dev_pid_9a_init(struct aw_device *aw_dev)
{
	int ret = 0;

	ret = aw_dev_i2c_write_byte(aw_dev, AW87XXX_PID_9B_ENCRYPTION_REG,
		AW87XXX_PID_9B_ENCRYPTION_BOOST_OUTPUT_SET);
	if (ret < 0) {
		AW_DEV_LOGE(aw_dev->dev, "write 0x64=0x2C error");
		return -EINVAL;
	}

	ret = aw_dev_get_chipid(aw_dev);
	if (ret < 0) {
		AW_DEV_LOGE(aw_dev->dev, "read chipid is failed,ret=%d", ret);
		return ret;
	}

	if (aw_dev->chipid == AW_DEV_CHIPID_9B) {
		AW_DEV_LOGI(aw_dev->dev, "product is pid_9B class");
		aw_dev_pid_9b_init(aw_dev);
	} else {
		AW_DEV_LOGE(aw_dev->dev, "product is not pid_9B class, not support");
		return -EINVAL;
	}

	return 0;
}

/********************** aw87xxx_pid_9b attributes end ***********************/

/********************** aw87xxx_pid_39 attributes ***************************/
static void aw_dev_chipid_39_init(struct aw_device *aw_dev)
{
	/* Product register permission info */
	aw_dev->reg_max_addr = AW87XXX_PID_39_REG_MAX;
	aw_dev->reg_access = aw87xxx_pid_39_reg_access;

	/* software reset control info */
	aw_dev->soft_rst_desc.len = sizeof(aw87xxx_pid_39_softrst_access);
	aw_dev->soft_rst_desc.access = aw87xxx_pid_39_softrst_access;
	aw_dev->soft_rst_enable = AW_DEV_SOFT_RST_ENABLE;

	/* Whether to allow register operation to power off */
	aw_dev->soft_off_enable = AW_DEV_SOFT_OFF_ENABLE;

	aw_dev->product_tab = g_aw_pid_39_product;
	aw_dev->product_cnt = AW87XXX_PID_39_PRODUCT_MAX;

	aw_dev->rec_desc.addr = AW87XXX_PID_39_REG_MODECTRL;
	aw_dev->rec_desc.disable = AW87XXX_PID_39_REC_MODE_DISABLE;
	aw_dev->rec_desc.enable = AW87XXX_PID_39_REC_MODE_ENABLE;
	aw_dev->rec_desc.mask = AW87XXX_PID_39_REC_MODE_MASK;

	/* esd reg info */
	aw_dev->esd_desc.first_update_reg_addr = AW87XXX_PID_39_REG_MODECTRL;
	aw_dev->esd_desc.first_update_reg_val = AW87XXX_PID_39_MODECTRL_DEFAULT;
}
/********************* aw87xxx_pid_39 attributes end *************************/


/********************* aw87xxx_pid_59_5x9 attributes *************************/
static void aw_dev_chipid_59_5x9_init(struct aw_device *aw_dev)
{
	/* Product register permission info */
	aw_dev->reg_max_addr = AW87XXX_PID_59_5X9_REG_MAX;
	aw_dev->reg_access = aw87xxx_pid_59_5x9_reg_access;

	/* software reset control info */
	aw_dev->soft_rst_desc.len = sizeof(aw87xxx_pid_59_5x9_softrst_access);
	aw_dev->soft_rst_desc.access = aw87xxx_pid_59_5x9_softrst_access;
	aw_dev->soft_rst_enable = AW_DEV_SOFT_RST_ENABLE;

	/* Whether to allow register operation to power off */
	aw_dev->soft_off_enable = AW_DEV_SOFT_OFF_ENABLE;

	aw_dev->product_tab = g_aw_pid_59_5x9_product;
	aw_dev->product_cnt = AW87XXX_PID_59_5X9_PRODUCT_MAX;

	aw_dev->rec_desc.addr = AW87XXX_PID_59_5X9_REG_SYSCTRL;
	aw_dev->rec_desc.disable = AW87XXX_PID_59_5X9_REC_MODE_DISABLE;
	aw_dev->rec_desc.enable = AW87XXX_PID_59_5X9_REC_MODE_ENABLE;
	aw_dev->rec_desc.mask = AW87XXX_PID_59_5X9_REC_MODE_MASK;

	/* esd reg info */
	aw_dev->esd_desc.first_update_reg_addr = AW87XXX_PID_59_5X9_REG_ENCR;
	aw_dev->esd_desc.first_update_reg_val = AW87XXX_PID_59_5X9_ENCRY_DEFAULT;
}
/******************* aw87xxx_pid_59_5x9 attributes end ***********************/

/********************* aw87xxx_pid_59_3x9 attributes *************************/
static void aw_dev_chipid_59_3x9_init(struct aw_device *aw_dev)
{
	/* Product register permission info */
	aw_dev->reg_max_addr = AW87XXX_PID_59_3X9_REG_MAX;
	aw_dev->reg_access = aw87xxx_pid_59_3x9_reg_access;

	/* software reset control info */
	aw_dev->soft_rst_desc.len = sizeof(aw87xxx_pid_59_3x9_softrst_access);
	aw_dev->soft_rst_desc.access = aw87xxx_pid_59_3x9_softrst_access;
	aw_dev->soft_rst_enable = AW_DEV_SOFT_RST_ENABLE;

	/* Whether to allow register operation to power off */
	aw_dev->soft_off_enable = AW_DEV_SOFT_OFF_ENABLE;

	aw_dev->product_tab = g_aw_pid_59_3x9_product;
	aw_dev->product_cnt = AW87XXX_PID_59_3X9_PRODUCT_MAX;

	aw_dev->rec_desc.addr = AW87XXX_PID_59_3X9_REG_MDCRTL;
	aw_dev->rec_desc.disable = AW87XXX_PID_59_3X9_SPK_MODE_ENABLE;
	aw_dev->rec_desc.enable = AW87XXX_PID_59_3X9_SPK_MODE_DISABLE;
	aw_dev->rec_desc.mask = AW87XXX_PID_59_3X9_SPK_MODE_MASK;

	/* esd reg info */
	aw_dev->esd_desc.first_update_reg_addr = AW87XXX_PID_59_3X9_REG_ENCR;
	aw_dev->esd_desc.first_update_reg_val = AW87XXX_PID_59_3X9_ENCR_DEFAULT;
}
/******************* aw87xxx_pid_59_3x9 attributes end ***********************/

/********************** aw87xxx_pid_5a attributes ****************************/
static void aw_dev_chipid_5a_init(struct aw_device *aw_dev)
{
	/* Product register permission info */
	aw_dev->reg_max_addr = AW87XXX_PID_5A_REG_MAX;
	aw_dev->reg_access = aw87xxx_pid_5a_reg_access;

	/* software reset control info */
	aw_dev->soft_rst_desc.len = sizeof(aw87xxx_pid_5a_softrst_access);
	aw_dev->soft_rst_desc.access = aw87xxx_pid_5a_softrst_access;
	aw_dev->soft_rst_enable = AW_DEV_SOFT_RST_ENABLE;

	/* Whether to allow register operation to power off */
	aw_dev->soft_off_enable = AW_DEV_SOFT_OFF_ENABLE;

	aw_dev->product_tab = g_aw_pid_5a_product;
	aw_dev->product_cnt = AW87XXX_PID_5A_PRODUCT_MAX;

	aw_dev->rec_desc.addr = AW87XXX_PID_5A_REG_SYSCTRL_REG;
	aw_dev->rec_desc.disable = AW87XXX_PID_5A_REG_RCV_MODE_DISABLE;
	aw_dev->rec_desc.enable = AW87XXX_PID_5A_REG_RCV_MODE_ENABLE;
	aw_dev->rec_desc.mask = AW87XXX_PID_5A_REG_RCV_MODE_MASK;

	/* esd reg info */
	aw_dev->esd_desc.first_update_reg_addr = AW87XXX_PID_5A_REG_DFT3R_REG;
	aw_dev->esd_desc.first_update_reg_val = AW87XXX_PID_5A_DFT3R_DEFAULT;
}

static void aw_dev_chipid_76_init(struct aw_device *aw_dev)
{
	/* Product register permission info */
	aw_dev->reg_max_addr = AW87XXX_PID_76_REG_MAX;
	aw_dev->reg_access = aw87xxx_pid_76_reg_access;

	/* software reset control info */
	aw_dev->soft_rst_desc.len = sizeof(aw87xxx_pid_76_softrst_access);
	aw_dev->soft_rst_desc.access = aw87xxx_pid_76_softrst_access;
	aw_dev->soft_rst_enable = AW_DEV_SOFT_RST_ENABLE;

	/* software power off control info */
	aw_dev->soft_off_enable = AW_DEV_SOFT_OFF_ENABLE;

	aw_dev->product_tab = g_aw_pid_76_product;
	aw_dev->product_cnt = AW87XXX_PID_76_PROFUCT_MAX;

	aw_dev->rec_desc.addr = AW87XXX_PID_76_MDCTRL_REG;
	aw_dev->rec_desc.disable = AW87XXX_PID_76_EN_SPK_ENABLE;
	aw_dev->rec_desc.enable = AW87XXX_PID_76_EN_SPK_DISABLE;
	aw_dev->rec_desc.mask = AW87XXX_PID_76_EN_SPK_MASK;

	/* esd reg info */
	aw_dev->esd_desc.first_update_reg_addr = AW87XXX_PID_76_DFT_ADP1_REG;
	aw_dev->esd_desc.first_update_reg_val = AW87XXX_PID_76_DFT_ADP1_CHECK;
}

/********************** aw87xxx_pid_5a attributes end ************************/

static void aw_dev_chip_init(struct aw_device *aw_dev)
{
	int ret  = 0;

	/*get info by chipid*/
	switch (aw_dev->chipid) {
	case AW_DEV_CHIPID_9A:
		ret = aw_dev_pid_9a_init(aw_dev);
		if (ret < 0)
			AW_DEV_LOGE(aw_dev->dev, "product is pid_9B init failed");
		break;
	case AW_DEV_CHIPID_9B:
		aw_dev_pid_9b_init(aw_dev);
		AW_DEV_LOGI(aw_dev->dev, "product is pid_9B class");
		break;
	case AW_DEV_CHIPID_39:
		aw_dev_chipid_39_init(aw_dev);
		AW_DEV_LOGI(aw_dev->dev, "product is pid_39 class");
		break;
	case AW_DEV_CHIPID_59:
		if (aw_dev_gpio_is_valid(aw_dev)) {
			aw_dev_chipid_59_5x9_init(aw_dev);
			AW_DEV_LOGI(aw_dev->dev, "product is pid_59_5x9 class");
		} else {
			aw_dev_chipid_59_3x9_init(aw_dev);
			AW_DEV_LOGI(aw_dev->dev, "product is pid_59_3x9 class");
		}
		break;
	case AW_DEV_CHIPID_5A:
		aw_dev_chipid_5a_init(aw_dev);
		AW_DEV_LOGI(aw_dev->dev, "product is pid_5A class");
		break;
	case AW_DEV_CHIPID_76:
		aw_dev_chipid_76_init(aw_dev);
		AW_DEV_LOGI(aw_dev->dev, "product is pid_76 class");
		break;
	default:
		AW_DEV_LOGE(aw_dev->dev, "unsupported device revision [0x%x]",
			aw_dev->chipid);
		return;
	}
}

static int aw_dev_get_chipid(struct aw_device *aw_dev)
{
	int ret = -1;
	unsigned int cnt = 0;
	unsigned char reg_val = 0;

	for (cnt = 0; cnt < AW_READ_CHIPID_RETRIES; cnt++) {
		ret = aw_dev_i2c_read_byte(aw_dev, AW_DEV_REG_CHIPID, &reg_val);
		if (ret < 0) {
			AW_DEV_LOGE(aw_dev->dev, "[%d] read chip is failed, ret=%d",
				cnt, ret);
			continue;
		}
		break;
	}


	if (cnt == AW_READ_CHIPID_RETRIES) {
		AW_DEV_LOGE(aw_dev->dev, "read chip is failed,cnt=%d", cnt);
		return -EINVAL;
	}

	AW_DEV_LOGI(aw_dev->dev, "read chipid[0x%x] succeed", reg_val);
	aw_dev->chipid = reg_val;

	return 0;
}

int aw_dev_init(struct aw_device *aw_dev)
{
	int ret = -1;

	ret = aw_dev_get_chipid(aw_dev);
	if (ret < 0) {
		AW_DEV_LOGE(aw_dev->dev, "read chipid is failed,ret=%d", ret);
		return ret;
	}

	aw_dev_chip_init(aw_dev);

	return 0;
}


