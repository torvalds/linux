/* include/linux/regulator/rk818.h
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
#ifndef __LINUX_REGULATOR_rk818_H
#define __LINUX_REGULATOR_rk818_H

#include <linux/regulator/machine.h>
#include <linux/wakelock.h>
#include <linux/power_supply.h>
//#include <linux/power/rk818_battery.h>

//#define RK818_START 30

#define RK818_DCDC1  0                     //(0+RK818_START) 

#define RK818_LDO1 4                //(4+RK818_START)

#define RK818_SECONDS_REG 0x00
#define RK818_MINUTES_REG 0x01
#define RK818_HOURS_REG 0x02
#define RK818_DAYS_REG 0x03
#define RK818_MONTHS_REG 0x04
#define RK818_YEARS_REG 0x05
#define RK818_WEEKS_REG 0x06
#define RK818_ALARM_SECONDS_REG 0x07
#define RK818_ALARM_MINUTES_REG 0x08
#define RK818_ALARM_HOURS_REG 0x09
#define RK818_ALARM_DAYS_REG 0x0a
#define RK818_ALARM_MONTHS_REG 0x0b
#define RK818_ALARM_YEARS_REG 0x0c
#define RK818_RTC_CTRL_REG 0x10
#define RK818_RTC_STATUS_REG 0x11
#define RK818_RTC_INT_REG 0x12
#define RK818_RTC_COMP_LSB_REG 0x13
#define RK818_RTC_COMP_MSB_REG 0x14
#define RK818_CLK32OUT_REG 0x20
#define RK818_VB_MON_REG 0x21
#define RK818_THERMAL_REG 0x22
#define RK818_DCDC_EN_REG 0x23
#define RK818_LDO_EN_REG 0x24
#define RK818_SLEEP_SET_OFF_REG1 0x25
#define RK818_SLEEP_SET_OFF_REG2 0x26
#define RK818_DCDC_UV_STS_REG 0x27
#define RK818_DCDC_UV_ACT_REG 0x28
#define RK818_LDO_UV_STS_REG 0x29
#define RK818_LDO_UV_ACT_REG 0x2a
#define RK818_DCDC_PG_REG 0x2b
#define RK818_LDO_PG_REG 0x2c
#define RK818_VOUT_MON_TDB_REG 0x2d
#define RK818_BUCK1_CONFIG_REG 0x2e
#define RK818_BUCK1_ON_REG 0x2f
#define RK818_BUCK1_SLP_REG 0x30
#define RK818_BUCK2_CONFIG_REG 0x32
#define RK818_BUCK2_ON_REG 0x33
#define RK818_BUCK2_SLP_REG 0x34
#define RK818_BUCK3_CONFIG_REG 0x36
#define RK818_BUCK4_CONFIG_REG 0x37
#define RK818_BUCK4_ON_REG 0x38
#define RK818_BUCK4_SLP_VSEL_REG 0x39
#define RK818_BOOST_CONFIG_REG 0x3a
#define RK818_LDO1_ON_VSEL_REG 0x3b
#define RK818_LDO1_SLP_VSEL_REG 0x3c
#define RK818_LDO2_ON_VSEL_REG 0x3d
#define RK818_LDO2_SLP_VSEL_REG 0x3e
#define RK818_LDO3_ON_VSEL_REG 0x3f
#define RK818_LDO3_SLP_VSEL_REG 0x40
#define RK818_LDO4_ON_VSEL_REG 0x41
#define RK818_LDO4_SLP_VSEL_REG 0x42
#define RK818_LDO5_ON_VSEL_REG 0x43
#define RK818_LDO5_SLP_VSEL_REG 0x44
#define RK818_LDO6_ON_VSEL_REG 0x45
#define RK818_LDO6_SLP_VSEL_REG 0x46
#define RK818_LDO7_ON_VSEL_REG 0x47
#define RK818_LDO7_SLP_VSEL_REG 0x48
#define RK818_LDO8_ON_VSEL_REG 0x49
#define RK818_LDO8_SLP_VSEL_REG 0x4a
#define RK818_BOOST_LDO9_ON_VSEL_REG 0x54
#define RK818_BOOST_LDO9_SLP_VSEL_REG 0x55
#define RK818_DEVCTRL_REG 0x4b
#define RK818_INT_STS_REG1 0X4c
#define RK818_INT_STS_MSK_REG1 0X4d
#define RK818_INT_STS_REG2 0X4e
#define RK818_INT_STS_MSK_REG2 0X4f
#define RK818_IO_POL_REG 0X50

/* IRQ Definitions */
#define RK818_IRQ_VOUT_LO			0
#define RK818_IRQ_VB_LO				1
#define RK818_IRQ_PWRON				2
#define RK818_IRQ_PWRON_LP				3
#define RK818_IRQ_HOTDIE				4
#define RK818_IRQ_RTC_ALARM				5
#define RK818_IRQ_RTC_PERIOD				6
#define RK818_IRQ_USB_OV				7
#define RK818_IRQ_PLUG_IN				8
#define RK818_IRQ_PLUG_OUT				9
#define RK818_IRQ_CHG_OK				10
#define RK818_IRQ_CHG_TE				11
#define RK818_IRQ_CHG_TS1				12
#define RK818_IRQ_TS2				13
#define RK818_IRQ_CHG_CVTLIM		14
#define RK818_IRQ_DISCHG_ILIM				6

#define RK818_NUM_IRQ  16

#define rk818_NUM_REGULATORS 14
struct rk818;


#define RK818_VBAT_LOW_2V8  0x00
#define RK818_VBAT_LOW_2V9  0x01
#define RK818_VBAT_LOW_3V0  0x02
#define RK818_VBAT_LOW_3V1  0x03
#define RK818_VBAT_LOW_3V2  0x04
#define RK818_VBAT_LOW_3V3  0x05
#define RK818_VBAT_LOW_3V4  0x06
#define RK818_VBAT_LOW_3V5  0x07
#define VBAT_LOW_VOL_MASK (0x07 << 0)
#define EN_VABT_LOW_SHUT_DOWN (0x00 << 4)
#define EN_VBAT_LOW_IRQ (0x1 <<4 )
#define VBAT_LOW_ACT_MASK (0x1 << 4)

struct rk818_board {
	int irq;
	int irq_base;
	int irq_gpio;
	int wakeup;
	struct regulator_init_data *rk818_init_data[rk818_NUM_REGULATORS];
	struct device_node *of_node[rk818_NUM_REGULATORS];
	int pmic_sleep_gpio; /* */
	unsigned int dcdc_slp_voltage[3]; /* buckx_voltage in uV */
	bool pmic_sleep;
	unsigned int ldo_slp_voltage[7];
	bool pm_off;
};

struct rk818_regulator_subdev {
	int id;
	struct regulator_init_data *initdata;
};

struct rk818 {
	struct device *dev;
	struct mutex io_lock;
	struct i2c_client *i2c;
	int num_regulators;
	struct regulator_dev **rdev;
	struct wake_lock 	irq_wake;
	struct mutex irq_lock;
	int irq_base;
	int irq_num;
	int chip_irq;
	int irq_gpio;
	int wakeup;
	u32 irq_mask;
	struct irq_domain *irq_domain;
	int (*read)(struct rk818 *rk818, u8 reg, int size, void *dest);
	int (*write)(struct rk818 *rk818, u8 reg, int size, void *src);
	struct battery_platform_data *battery_data;
	int pmic_sleep_gpio; /* */
	unsigned int dcdc_slp_voltage[3]; /* buckx_voltage in uV */
	bool pmic_sleep;
	unsigned int ldo_slp_voltage[7];
};

struct rk818_platform_data {
	int num_regulators;
	struct rk818_regulator_subdev *regulators;
	struct battery_platform_data *battery_data;
	int irq;
	int irq_base;
	struct irq_domain *irq_domain;
};

int rk818_irq_init(struct rk818 *rk818, int irq,struct rk818_board *pdata);
int rk818_i2c_read(struct rk818 *rk818, char reg, int count,u8 *dest);
//int rk818_i2c_read(struct i2c_client *i2c, char reg, int count,u16 *dest);
// int rk818_i2c_read(struct rk818 *rk818 , u8 reg, int bytes,void *dest); 
int rk818_i2c_write(struct rk818 *rk818, char reg, int count, const u8 src);
int rk818_set_bits(struct rk818 *rk818, u8 reg, u8 mask, u8 val);
int rk818_clear_bits(struct rk818 *rk818, u8 reg, u8 mask);
u8 rk818_reg_read(struct rk818 *rk818, u8 reg);
int rk818_reg_write(struct rk818 *rk818, u8 reg, u8 val);
int rk818_bulk_read(struct rk818 *rk818, u8 reg,
		     int count, u8 *buf);
int rk818_bulk_write(struct rk818 *rk818, u8 reg,
		     int count, u8 *buf);
void rk818_device_shutdown(void);

#endif

