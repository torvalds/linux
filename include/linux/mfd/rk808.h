/* include/linux/regulator/rk808.h
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __LINUX_REGULATOR_rk808_H
#define __LINUX_REGULATOR_rk808_H

#include <linux/regulator/machine.h>
#include <linux/wakelock.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

//#define RK808_START 30

#define RK808_DCDC1  0                     //(0+RK808_START) 

#define RK808_LDO1 4                //(4+RK808_START)

#define RK808_SECONDS_REG 0x00
#define RK808_MINUTES_REG 0x01
#define RK808_HOURS_REG 0x02
#define RK808_DAYS_REG 0x03
#define RK808_MONTHS_REG 0x04
#define RK808_YEARS_REG 0x05
#define RK808_WEEKS_REG 0x06
#define RK808_ALARM_SECONDS_REG 0x07
#define RK808_ALARM_MINUTES_REG 0x08
#define RK808_ALARM_HOURS_REG 0x09
#define RK808_ALARM_DAYS_REG 0x0a
#define RK808_ALARM_MONTHS_REG 0x0b
#define RK808_ALARM_YEARS_REG 0x0c
#define RK808_RTC_CTRL_REG 0x10
#define RK808_RTC_STATUS_REG 0x11
#define RK808_RTC_INT_REG 0x12
#define RK808_RTC_COMP_LSB_REG 0x13
#define RK808_RTC_COMP_MSB_REG 0x14
#define RK808_CLK32OUT_REG 0x20
#define RK808_VB_MON_REG 0x21
#define RK808_THERMAL_REG 0x22
#define RK808_DCDC_EN_REG 0x23
#define RK808_LDO_EN_REG 0x24
#define RK808_SLEEP_SET_OFF_REG1 0x25
#define RK808_SLEEP_SET_OFF_REG2 0x26
#define RK808_DCDC_UV_STS_REG 0x27
#define RK808_DCDC_UV_ACT_REG 0x28
#define RK808_LDO_UV_STS_REG 0x29
#define RK808_LDO_UV_ACT_REG 0x2a
#define RK808_DCDC_PG_REG 0x2b
#define RK808_LDO_PG_REG 0x2c
#define RK808_VOUT_MON_TDB_REG 0x2d
#define RK808_BUCK1_CONFIG_REG 0x2e
#define RK808_BUCK1_ON_REG 0x2f
#define RK808_BUCK1_SLP_REG 0x30
#define RK808_BUCK1_DVS_REG 0x31
#define RK808_BUCK2_CONFIG_REG 0x32
#define RK808_BUCK2_ON_REG 0x33
#define RK808_BUCK2_SLP_REG 0x34
#define RK808_BUCK2_DVS_REG 0x35
#define RK808_BUCK3_CONFIG_REG 0x36
#define RK808_BUCK4_CONFIG_REG 0x37
#define RK808_BUCK4_ON_REG 0x38
#define RK808_BUCK4_SLP_VSEL_REG 0x39
#define RK808_BOOST_CONFIG_REG 0x3a
#define RK808_LDO1_ON_VSEL_REG 0x3b
#define RK808_LDO1_SLP_VSEL_REG 0x3c
#define RK808_LDO2_ON_VSEL_REG 0x3d
#define RK808_LDO2_SLP_VSEL_REG 0x3e
#define RK808_LDO3_ON_VSEL_REG 0x3f
#define RK808_LDO3_SLP_VSEL_REG 0x40
#define RK808_LDO4_ON_VSEL_REG 0x41
#define RK808_LDO4_SLP_VSEL_REG 0x42
#define RK808_LDO5_ON_VSEL_REG 0x43
#define RK808_LDO5_SLP_VSEL_REG 0x44
#define RK808_LDO6_ON_VSEL_REG 0x45
#define RK808_LDO6_SLP_VSEL_REG 0x46
#define RK808_LDO7_ON_VSEL_REG 0x47
#define RK808_LDO7_SLP_VSEL_REG 0x48
#define RK808_LDO8_ON_VSEL_REG 0x49
#define RK808_LDO8_SLP_VSEL_REG 0x4a
#define RK808_DEVCTRL_REG 0x4b
#define RK808_INT_STS_REG1 0X4c
#define RK808_INT_STS_MSK_REG1 0X4d
#define RK808_INT_STS_REG2 0X4e
#define RK808_INT_STS_MSK_REG2 0X4d
#define RK808_IO_POL_REG 0X50

/* IRQ Definitions */
#define RK808_IRQ_VOUT_LO			0
#define RK808_IRQ_VB_LO				1
#define RK808_IRQ_PWRON				2
#define RK808_IRQ_PWRON_LP				3
#define RK808_IRQ_HOTDIE				4
#define RK808_IRQ_RTC_ALARM				5
#define RK808_IRQ_RTC_PERIOD				6

#define RK808_NUM_IRQ  9

#define rk808_NUM_REGULATORS 12
struct rk808;

struct rk808_regulator_subdev {
	int id;
	struct regulator_init_data *initdata;
};

struct rk808 {
	struct device *dev;
	struct mutex io_lock;
	struct i2c_client *i2c;
	int num_regulators;
	struct regulator_dev **rdev;
	struct wake_lock 	irq_wake;
	struct early_suspend rk808_suspend;
	struct mutex irq_lock;
	int irq_base;
	int irq_num;
	int chip_irq;
	u32 irq_mask;
	int (*read)(struct rk808 *rk808, u8 reg, int size, void *dest);
	int (*write)(struct rk808 *rk808, u8 reg, int size, void *src);
};

struct rk808_platform_data {
	int num_regulators;
	int (*pre_init)(struct rk808 *rk808);
	int (*set_init)(struct rk808 *rk808);
	struct rk808_regulator_subdev *regulators;
	int irq;
	int irq_base;
};

int rk808_irq_init(struct rk808 *rk808, int irq,struct rk808_platform_data *pdata);
 int rk808_i2c_read(struct rk808 *rk808, char reg, int count,u8 *dest);
//int rk808_i2c_read(struct i2c_client *i2c, char reg, int count,u16 *dest);
// int rk808_i2c_read(struct rk808 *rk808 , u8 reg, int bytes,void *dest); 
int rk808_i2c_write(struct rk808 *rk808, char reg, int count, const u8 src);
int rk808_set_bits(struct rk808 *rk808, u8 reg, u8 mask, u8 val);
int rk808_clear_bits(struct rk808 *rk808, u8 reg, u8 mask);
u8 rk808_reg_read(struct rk808 *rk808, u8 reg);
int rk808_reg_write(struct rk808 *rk808, u8 reg, u8 val);
int rk808_bulk_read(struct rk808 *rk808, u8 reg,
		     int count, u8 *buf);
int rk808_bulk_write(struct rk808 *rk808, u8 reg,
		     int count, u8 *buf);
int rk808_device_shutdown(void);

#endif

