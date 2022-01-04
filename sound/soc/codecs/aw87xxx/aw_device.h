/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __AW_DEVICE_H__
#define __AW_DEVICE_H__
#include <linux/version.h>
#include <linux/kernel.h>
#include <sound/control.h>
#include <sound/soc.h>
#include "aw_acf_bin.h"

#define AW87XXX_PID_9B_PRODUCT_MAX	(1)
#define AW87XXX_PID_39_PRODUCT_MAX	(3)
#define AW87XXX_PID_59_3X9_PRODUCT_MAX	(2)
#define AW87XXX_PID_59_5X9_PRODUCT_MAX	(4)
#define AW87XXX_PID_5A_PRODUCT_MAX	(5)
#define AW87XXX_PID_76_PROFUCT_MAX	(3)
#define AW_PRODUCT_NAME_LEN		(8)

#define AW_GPIO_HIGHT_LEVEL		(1)
#define AW_GPIO_LOW_LEVEL		(0)

#define AW_I2C_RETRIES			(5)
#define AW_I2C_RETRY_DELAY		(2)
#define AW_I2C_READ_MSG_NUM		(2)

#define AW_READ_CHIPID_RETRIES		(5)
#define AW_READ_CHIPID_RETRY_DELAY	(2)
#define AW_DEV_REG_CHIPID		(0x00)

#define AW_DEV_REG_INVALID_MASK		(0xff)

#define AW_NO_RESET_GPIO		(-1)

#define AW_PID_9B_BIN_REG_CFG_COUNT	(10)

/********************************************
 *
 * aw87xxx devices attributes
 *
 *******************************************/
struct aw_device;

struct aw_device_ops {
	int (*pwr_on_func)(struct aw_device *aw_dev, struct aw_data_container *data);
	int (*pwr_off_func)(struct aw_device *aw_dev, struct aw_data_container *data);
};

enum aw_dev_chipid {
	AW_DEV_CHIPID_18 = 0x18,
	AW_DEV_CHIPID_39 = 0x39,
	AW_DEV_CHIPID_59 = 0x59,
	AW_DEV_CHIPID_69 = 0x69,
	AW_DEV_CHIPID_5A = 0x5A,
	AW_DEV_CHIPID_9A = 0x9A,
	AW_DEV_CHIPID_9B = 0x9B,
	AW_DEV_CHIPID_76 = 0x76,
};

enum aw_dev_hw_status {
	AW_DEV_HWEN_OFF = 0,
	AW_DEV_HWEN_ON,
	AW_DEV_HWEN_INVALID,
	AW_DEV_HWEN_STATUS_MAX,
};

enum aw_dev_soft_off_enable {
	AW_DEV_SOFT_OFF_DISENABLE = 0,
	AW_DEV_SOFT_OFF_ENABLE = 1,
};

enum aw_dev_soft_rst_enable {
	AW_DEV_SOFT_RST_DISENABLE = 0,
	AW_DEV_SOFT_RST_ENABLE = 1,
};

enum aw_reg_receiver_mode {
	AW_NOT_REC_MODE = 0,
	AW_IS_REC_MODE = 1,
};

struct aw_mute_desc {
	uint8_t addr;
	uint8_t enable;
	uint8_t disable;
	uint16_t mask;
};

struct aw_soft_rst_desc {
	int len;
	unsigned char *access;
};

struct aw_esd_check_desc {
	uint8_t first_update_reg_addr;
	uint8_t first_update_reg_val;
};

struct aw_rec_mode_desc {
	uint8_t addr;
	uint8_t enable;
	uint8_t disable;
	uint8_t mask;
};

struct aw_device {
	uint8_t i2c_addr;
	uint8_t chipid;
	uint8_t soft_rst_enable;
	uint8_t soft_off_enable;
	uint8_t is_rec_mode;
	int hwen_status;
	int i2c_bus;
	int rst_gpio;
	int rst_shared_gpio;
	int reg_max_addr;
	int product_cnt;
	const char **product_tab;
	const unsigned char *reg_access;

	struct device *dev;
	struct i2c_client *i2c;
	struct aw_mute_desc mute_desc;
	struct aw_soft_rst_desc soft_rst_desc;
	struct aw_esd_check_desc esd_desc;
	struct aw_rec_mode_desc rec_desc;

	struct aw_device_ops ops;
};


int aw_dev_i2c_write_byte(struct aw_device *aw_dev,
			uint8_t reg_addr, uint8_t reg_data);
int aw_dev_i2c_read_byte(struct aw_device *aw_dev,
			uint8_t reg_addr, uint8_t *reg_data);
int aw_dev_i2c_read_msg(struct aw_device *aw_dev,
	uint8_t reg_addr, uint8_t *data_buf, uint32_t data_len);
int aw_dev_i2c_write_bits(struct aw_device *aw_dev,
	uint8_t reg_addr, uint8_t mask, uint8_t reg_data);
void aw_dev_soft_reset(struct aw_device *aw_dev);
void aw_dev_hw_pwr_ctrl(struct aw_device *aw_dev, bool enable);
int aw_dev_default_profile_check(struct aw_device *aw_dev,
		int profile, struct aw_data_container *profile_data);
int aw_dev_default_pwr_on(struct aw_device *aw_dev,
			struct aw_data_container *profile_data);
int aw_dev_default_pwr_off(struct aw_device *aw_dev,
			struct aw_data_container *profile_data);
int aw_dev_esd_reg_status_check(struct aw_device *aw_dev);
int aw_dev_check_reg_is_rec_mode(struct aw_device *aw_dev);
int aw_dev_init(struct aw_device *aw_dev);

#endif
