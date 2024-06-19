/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Functions to access TPS6594 Power Management IC
 *
 * Copyright (C) 2023 BayLibre Incorporated - https://www.baylibre.com/
 */

#ifndef __LINUX_MFD_TPS6594_H
#define __LINUX_MFD_TPS6594_H

#include <linux/device.h>
#include <linux/regmap.h>

struct regmap_irq_chip_data;

/* Chip id list */
enum pmic_id {
	TPS6594,
	TPS6593,
	LP8764,
	TPS65224,
};

/* Macro to get page index from register address */
#define TPS6594_REG_TO_PAGE(reg)	((reg) >> 8)

/* Registers for page 0 */
#define TPS6594_REG_DEV_REV				0x01

#define TPS6594_REG_NVM_CODE_1				0x02
#define TPS6594_REG_NVM_CODE_2				0x03

#define TPS6594_REG_BUCKX_CTRL(buck_inst)		(0x04 + ((buck_inst) << 1))
#define TPS6594_REG_BUCKX_CONF(buck_inst)		(0x05 + ((buck_inst) << 1))
#define TPS6594_REG_BUCKX_VOUT_1(buck_inst)		(0x0e + ((buck_inst) << 1))
#define TPS6594_REG_BUCKX_VOUT_2(buck_inst)		(0x0f + ((buck_inst) << 1))
#define TPS6594_REG_BUCKX_PG_WINDOW(buck_inst)		(0x18 + (buck_inst))

#define TPS6594_REG_LDOX_CTRL(ldo_inst)			(0x1d + (ldo_inst))
#define TPS6594_REG_LDORTC_CTRL				0x22
#define TPS6594_REG_LDOX_VOUT(ldo_inst)			(0x23 + (ldo_inst))
#define TPS6594_REG_LDOX_PG_WINDOW(ldo_inst)		(0x27 + (ldo_inst))

#define TPS6594_REG_VCCA_VMON_CTRL			0x2b
#define TPS6594_REG_VCCA_PG_WINDOW			0x2c
#define TPS6594_REG_VMON1_PG_WINDOW			0x2d
#define TPS6594_REG_VMON1_PG_LEVEL			0x2e
#define TPS6594_REG_VMON2_PG_WINDOW			0x2f
#define TPS6594_REG_VMON2_PG_LEVEL			0x30

#define TPS6594_REG_GPIOX_CONF(gpio_inst)		(0x31 + (gpio_inst))
#define TPS6594_REG_NPWRON_CONF				0x3c
#define TPS6594_REG_GPIO_OUT_1				0x3d
#define TPS6594_REG_GPIO_OUT_2				0x3e
#define TPS6594_REG_GPIO_IN_1				0x3f
#define TPS6594_REG_GPIO_IN_2				0x40
#define TPS6594_REG_GPIOX_OUT(gpio_inst)		(TPS6594_REG_GPIO_OUT_1 + (gpio_inst) / 8)
#define TPS6594_REG_GPIOX_IN(gpio_inst)			(TPS6594_REG_GPIO_IN_1 + (gpio_inst) / 8)

#define TPS6594_REG_RAIL_SEL_1				0x41
#define TPS6594_REG_RAIL_SEL_2				0x42
#define TPS6594_REG_RAIL_SEL_3				0x43

#define TPS6594_REG_FSM_TRIG_SEL_1			0x44
#define TPS6594_REG_FSM_TRIG_SEL_2			0x45
#define TPS6594_REG_FSM_TRIG_MASK_1			0x46
#define TPS6594_REG_FSM_TRIG_MASK_2			0x47
#define TPS6594_REG_FSM_TRIG_MASK_3			0x48

#define TPS6594_REG_MASK_BUCK1_2			0x49
#define TPS65224_REG_MASK_BUCKS				0x49
#define TPS6594_REG_MASK_BUCK3_4			0x4a
#define TPS6594_REG_MASK_BUCK5				0x4b
#define TPS6594_REG_MASK_LDO1_2				0x4c
#define TPS65224_REG_MASK_LDOS				0x4c
#define TPS6594_REG_MASK_LDO3_4				0x4d
#define TPS6594_REG_MASK_VMON				0x4e
#define TPS6594_REG_MASK_GPIO_FALL			0x4f
#define TPS6594_REG_MASK_GPIO_RISE			0x50
#define TPS6594_REG_MASK_GPIO9_11			0x51
#define TPS6594_REG_MASK_STARTUP			0x52
#define TPS6594_REG_MASK_MISC				0x53
#define TPS6594_REG_MASK_MODERATE_ERR			0x54
#define TPS6594_REG_MASK_FSM_ERR			0x56
#define TPS6594_REG_MASK_COMM_ERR			0x57
#define TPS6594_REG_MASK_READBACK_ERR			0x58
#define TPS6594_REG_MASK_ESM				0x59

#define TPS6594_REG_INT_TOP				0x5a
#define TPS6594_REG_INT_BUCK				0x5b
#define TPS6594_REG_INT_BUCK1_2				0x5c
#define TPS6594_REG_INT_BUCK3_4				0x5d
#define TPS6594_REG_INT_BUCK5				0x5e
#define TPS6594_REG_INT_LDO_VMON			0x5f
#define TPS6594_REG_INT_LDO1_2				0x60
#define TPS6594_REG_INT_LDO3_4				0x61
#define TPS6594_REG_INT_VMON				0x62
#define TPS6594_REG_INT_GPIO				0x63
#define TPS6594_REG_INT_GPIO1_8				0x64
#define TPS6594_REG_INT_STARTUP				0x65
#define TPS6594_REG_INT_MISC				0x66
#define TPS6594_REG_INT_MODERATE_ERR			0x67
#define TPS6594_REG_INT_SEVERE_ERR			0x68
#define TPS6594_REG_INT_FSM_ERR				0x69
#define TPS6594_REG_INT_COMM_ERR			0x6a
#define TPS6594_REG_INT_READBACK_ERR			0x6b
#define TPS6594_REG_INT_ESM				0x6c

#define TPS6594_REG_STAT_BUCK1_2			0x6d
#define TPS6594_REG_STAT_BUCK3_4			0x6e
#define TPS6594_REG_STAT_BUCK5				0x6f
#define TPS6594_REG_STAT_LDO1_2				0x70
#define TPS6594_REG_STAT_LDO3_4				0x71
#define TPS6594_REG_STAT_VMON				0x72
#define TPS6594_REG_STAT_STARTUP			0x73
#define TPS6594_REG_STAT_MISC				0x74
#define TPS6594_REG_STAT_MODERATE_ERR			0x75
#define TPS6594_REG_STAT_SEVERE_ERR			0x76
#define TPS6594_REG_STAT_READBACK_ERR			0x77

#define TPS6594_REG_PGOOD_SEL_1				0x78
#define TPS6594_REG_PGOOD_SEL_2				0x79
#define TPS6594_REG_PGOOD_SEL_3				0x7a
#define TPS6594_REG_PGOOD_SEL_4				0x7b

#define TPS6594_REG_PLL_CTRL				0x7c

#define TPS6594_REG_CONFIG_1				0x7d
#define TPS6594_REG_CONFIG_2				0x7e

#define TPS6594_REG_ENABLE_DRV_REG			0x80

#define TPS6594_REG_MISC_CTRL				0x81

#define TPS6594_REG_ENABLE_DRV_STAT			0x82

#define TPS6594_REG_RECOV_CNT_REG_1			0x83
#define TPS6594_REG_RECOV_CNT_REG_2			0x84

#define TPS6594_REG_FSM_I2C_TRIGGERS			0x85
#define TPS6594_REG_FSM_NSLEEP_TRIGGERS			0x86

#define TPS6594_REG_BUCK_RESET_REG			0x87

#define TPS6594_REG_SPREAD_SPECTRUM_1			0x88

#define TPS6594_REG_FREQ_SEL				0x8a

#define TPS6594_REG_FSM_STEP_SIZE			0x8b

#define TPS6594_REG_LDO_RV_TIMEOUT_REG_1		0x8c
#define TPS6594_REG_LDO_RV_TIMEOUT_REG_2		0x8d

#define TPS6594_REG_USER_SPARE_REGS			0x8e

#define TPS6594_REG_ESM_MCU_START_REG			0x8f
#define TPS6594_REG_ESM_MCU_DELAY1_REG			0x90
#define TPS6594_REG_ESM_MCU_DELAY2_REG			0x91
#define TPS6594_REG_ESM_MCU_MODE_CFG			0x92
#define TPS6594_REG_ESM_MCU_HMAX_REG			0x93
#define TPS6594_REG_ESM_MCU_HMIN_REG			0x94
#define TPS6594_REG_ESM_MCU_LMAX_REG			0x95
#define TPS6594_REG_ESM_MCU_LMIN_REG			0x96
#define TPS6594_REG_ESM_MCU_ERR_CNT_REG			0x97
#define TPS6594_REG_ESM_SOC_START_REG			0x98
#define TPS6594_REG_ESM_SOC_DELAY1_REG			0x99
#define TPS6594_REG_ESM_SOC_DELAY2_REG			0x9a
#define TPS6594_REG_ESM_SOC_MODE_CFG			0x9b
#define TPS6594_REG_ESM_SOC_HMAX_REG			0x9c
#define TPS6594_REG_ESM_SOC_HMIN_REG			0x9d
#define TPS6594_REG_ESM_SOC_LMAX_REG			0x9e
#define TPS6594_REG_ESM_SOC_LMIN_REG			0x9f
#define TPS6594_REG_ESM_SOC_ERR_CNT_REG			0xa0

#define TPS6594_REG_REGISTER_LOCK			0xa1

#define TPS65224_REG_SRAM_ACCESS_1			0xa2
#define TPS65224_REG_SRAM_ACCESS_2			0xa3
#define TPS65224_REG_SRAM_ADDR_CTRL			0xa4
#define TPS65224_REG_RECOV_CNT_PFSM_INCR		0xa5
#define TPS6594_REG_MANUFACTURING_VER			0xa6

#define TPS6594_REG_CUSTOMER_NVM_ID_REG			0xa7

#define TPS6594_REG_VMON_CONF_REG			0xa8

#define TPS6594_REG_SOFT_REBOOT_REG			0xab

#define TPS65224_REG_ADC_CTRL				0xac
#define TPS65224_REG_ADC_RESULT_REG_1			0xad
#define TPS65224_REG_ADC_RESULT_REG_2			0xae
#define TPS6594_REG_RTC_SECONDS				0xb5
#define TPS6594_REG_RTC_MINUTES				0xb6
#define TPS6594_REG_RTC_HOURS				0xb7
#define TPS6594_REG_RTC_DAYS				0xb8
#define TPS6594_REG_RTC_MONTHS				0xb9
#define TPS6594_REG_RTC_YEARS				0xba
#define TPS6594_REG_RTC_WEEKS				0xbb

#define TPS6594_REG_ALARM_SECONDS			0xbc
#define TPS6594_REG_ALARM_MINUTES			0xbd
#define TPS6594_REG_ALARM_HOURS				0xbe
#define TPS6594_REG_ALARM_DAYS				0xbf
#define TPS6594_REG_ALARM_MONTHS			0xc0
#define TPS6594_REG_ALARM_YEARS				0xc1

#define TPS6594_REG_RTC_CTRL_1				0xc2
#define TPS6594_REG_RTC_CTRL_2				0xc3
#define TPS65224_REG_STARTUP_CTRL			0xc3
#define TPS6594_REG_RTC_STATUS				0xc4
#define TPS6594_REG_RTC_INTERRUPTS			0xc5
#define TPS6594_REG_RTC_COMP_LSB			0xc6
#define TPS6594_REG_RTC_COMP_MSB			0xc7
#define TPS6594_REG_RTC_RESET_STATUS			0xc8

#define TPS6594_REG_SCRATCH_PAD_REG_1			0xc9
#define TPS6594_REG_SCRATCH_PAD_REG_2			0xca
#define TPS6594_REG_SCRATCH_PAD_REG_3			0xcb
#define TPS6594_REG_SCRATCH_PAD_REG_4			0xcc

#define TPS6594_REG_PFSM_DELAY_REG_1			0xcd
#define TPS6594_REG_PFSM_DELAY_REG_2			0xce
#define TPS6594_REG_PFSM_DELAY_REG_3			0xcf
#define TPS6594_REG_PFSM_DELAY_REG_4			0xd0
#define TPS65224_REG_ADC_GAIN_COMP_REG			0xd0
#define TPS65224_REG_CRC_CALC_CONTROL			0xef
#define TPS65224_REG_REGMAP_USER_CRC_LOW		0xf0
#define TPS65224_REG_REGMAP_USER_CRC_HIGH		0xf1

/* Registers for page 1 */
#define TPS6594_REG_SERIAL_IF_CONFIG			0x11a
#define TPS6594_REG_I2C1_ID				0x122
#define TPS6594_REG_I2C2_ID				0x123

/* Registers for page 4 */
#define TPS6594_REG_WD_ANSWER_REG			0x401
#define TPS6594_REG_WD_QUESTION_ANSW_CNT		0x402
#define TPS6594_REG_WD_WIN1_CFG				0x403
#define TPS6594_REG_WD_WIN2_CFG				0x404
#define TPS6594_REG_WD_LONGWIN_CFG			0x405
#define TPS6594_REG_WD_MODE_REG				0x406
#define TPS6594_REG_WD_QA_CFG				0x407
#define TPS6594_REG_WD_ERR_STATUS			0x408
#define TPS6594_REG_WD_THR_CFG				0x409
#define TPS6594_REG_DWD_FAIL_CNT_REG			0x40a

/* BUCKX_CTRL register field definition */
#define TPS6594_BIT_BUCK_EN				BIT(0)
#define TPS6594_BIT_BUCK_FPWM				BIT(1)
#define TPS6594_BIT_BUCK_FPWM_MP			BIT(2)
#define TPS6594_BIT_BUCK_VSEL				BIT(3)
#define TPS6594_BIT_BUCK_VMON_EN			BIT(4)
#define TPS6594_BIT_BUCK_PLDN				BIT(5)
#define TPS6594_BIT_BUCK_RV_SEL				BIT(7)

/* TPS6594 BUCKX_CONF register field definition */
#define TPS6594_MASK_BUCK_SLEW_RATE			GENMASK(2, 0)
#define TPS6594_MASK_BUCK_ILIM				GENMASK(5, 3)

/* TPS65224 BUCKX_CONF register field definition */
#define TPS65224_MASK_BUCK_SLEW_RATE			GENMASK(1, 0)

/* TPS6594 BUCKX_PG_WINDOW register field definition */
#define TPS6594_MASK_BUCK_OV_THR			GENMASK(2, 0)
#define TPS6594_MASK_BUCK_UV_THR			GENMASK(5, 3)

/* TPS65224 BUCKX_PG_WINDOW register field definition */
#define TPS65224_MASK_BUCK_VMON_THR			GENMASK(1, 0)

/* TPS6594 BUCKX_VOUT register field definition */
#define TPS6594_MASK_BUCKS_VSET				GENMASK(7, 0)

/* TPS65224 BUCKX_VOUT register field definition */
#define TPS65224_MASK_BUCK1_VSET			GENMASK(7, 0)
#define TPS65224_MASK_BUCKS_VSET			GENMASK(6, 0)

/* LDOX_CTRL register field definition */
#define TPS6594_BIT_LDO_EN				BIT(0)
#define TPS6594_BIT_LDO_SLOW_RAMP			BIT(1)
#define TPS6594_BIT_LDO_VMON_EN				BIT(4)
#define TPS6594_MASK_LDO_PLDN				GENMASK(6, 5)
#define TPS6594_BIT_LDO_RV_SEL				BIT(7)
#define TPS65224_BIT_LDO_DISCHARGE_EN			BIT(5)

/* LDORTC_CTRL register field definition */
#define TPS6594_BIT_LDORTC_DIS				BIT(0)

/* LDOX_VOUT register field definition */
#define TPS6594_MASK_LDO123_VSET			GENMASK(6, 1)
#define TPS6594_MASK_LDO4_VSET				GENMASK(6, 0)
#define TPS6594_BIT_LDO_BYPASS				BIT(7)

/* LDOX_PG_WINDOW register field definition */
#define TPS6594_MASK_LDO_OV_THR				GENMASK(2, 0)
#define TPS6594_MASK_LDO_UV_THR				GENMASK(5, 3)

/* LDOX_PG_WINDOW register field definition */
#define TPS65224_MASK_LDO_VMON_THR			GENMASK(1, 0)

/* VCCA_VMON_CTRL register field definition */
#define TPS6594_BIT_VMON_EN				BIT(0)
#define TPS6594_BIT_VMON1_EN				BIT(1)
#define TPS6594_BIT_VMON1_RV_SEL			BIT(2)
#define TPS6594_BIT_VMON2_EN				BIT(3)
#define TPS6594_BIT_VMON2_RV_SEL			BIT(4)
#define TPS6594_BIT_VMON_DEGLITCH_SEL			BIT(5)
#define TPS65224_BIT_VMON_DEGLITCH_SEL			GENMASK(7, 5)

/* VCCA_PG_WINDOW register field definition */
#define TPS6594_MASK_VCCA_OV_THR			GENMASK(2, 0)
#define TPS6594_MASK_VCCA_UV_THR			GENMASK(5, 3)
#define TPS65224_MASK_VCCA_VMON_THR			GENMASK(1, 0)
#define TPS6594_BIT_VCCA_PG_SET				BIT(6)

/* VMONX_PG_WINDOW register field definition */
#define TPS6594_MASK_VMONX_OV_THR			GENMASK(2, 0)
#define TPS6594_MASK_VMONX_UV_THR			GENMASK(5, 3)
#define TPS6594_BIT_VMONX_RANGE				BIT(6)

/* VMONX_PG_WINDOW register field definition */
#define TPS65224_MASK_VMONX_THR				GENMASK(1, 0)

/* GPIOX_CONF register field definition */
#define TPS6594_BIT_GPIO_DIR				BIT(0)
#define TPS6594_BIT_GPIO_OD				BIT(1)
#define TPS6594_BIT_GPIO_PU_SEL				BIT(2)
#define TPS6594_BIT_GPIO_PU_PD_EN			BIT(3)
#define TPS6594_BIT_GPIO_DEGLITCH_EN			BIT(4)
#define TPS6594_MASK_GPIO_SEL				GENMASK(7, 5)
#define TPS65224_MASK_GPIO_SEL				GENMASK(6, 5)
#define TPS65224_MASK_GPIO_SEL_GPIO6			GENMASK(7, 5)

/* NPWRON_CONF register field definition */
#define TPS6594_BIT_NRSTOUT_OD				BIT(0)
#define TPS6594_BIT_ENABLE_PU_SEL			BIT(2)
#define TPS6594_BIT_ENABLE_PU_PD_EN			BIT(3)
#define TPS6594_BIT_ENABLE_DEGLITCH_EN			BIT(4)
#define TPS6594_BIT_ENABLE_POL				BIT(5)
#define TPS6594_MASK_NPWRON_SEL				GENMASK(7, 6)

/* POWER_ON_CONFIG register field definition */
#define TPS65224_BIT_NINT_ENDRV_PU_SEL			BIT(0)
#define TPS65224_BIT_NINT_ENDRV_SEL			BIT(1)
#define TPS65224_BIT_EN_PB_DEGL				BIT(5)
#define TPS65224_MASK_EN_PB_VSENSE_CONFIG		GENMASK(7, 6)

/* GPIO_OUT_X register field definition */
#define TPS6594_BIT_GPIOX_OUT(gpio_inst)		BIT((gpio_inst) % 8)

/* GPIO_IN_X register field definition */
#define TPS6594_BIT_GPIOX_IN(gpio_inst)			BIT((gpio_inst) % 8)
#define TPS6594_BIT_NPWRON_IN				BIT(3)

/* GPIO_OUT_X register field definition */
#define TPS65224_BIT_GPIOX_OUT(gpio_inst)		BIT((gpio_inst))

/* GPIO_IN_X register field definition */
#define TPS65224_BIT_GPIOX_IN(gpio_inst)		BIT((gpio_inst))

/* RAIL_SEL_1 register field definition */
#define TPS6594_MASK_BUCK1_GRP_SEL			GENMASK(1, 0)
#define TPS6594_MASK_BUCK2_GRP_SEL			GENMASK(3, 2)
#define TPS6594_MASK_BUCK3_GRP_SEL			GENMASK(5, 4)
#define TPS6594_MASK_BUCK4_GRP_SEL			GENMASK(7, 6)

/* RAIL_SEL_2 register field definition */
#define TPS6594_MASK_BUCK5_GRP_SEL			GENMASK(1, 0)
#define TPS6594_MASK_LDO1_GRP_SEL			GENMASK(3, 2)
#define TPS6594_MASK_LDO2_GRP_SEL			GENMASK(5, 4)
#define TPS6594_MASK_LDO3_GRP_SEL			GENMASK(7, 6)

/* RAIL_SEL_3 register field definition */
#define TPS6594_MASK_LDO4_GRP_SEL			GENMASK(1, 0)
#define TPS6594_MASK_VCCA_GRP_SEL			GENMASK(3, 2)
#define TPS6594_MASK_VMON1_GRP_SEL			GENMASK(5, 4)
#define TPS6594_MASK_VMON2_GRP_SEL			GENMASK(7, 6)

/* FSM_TRIG_SEL_1 register field definition */
#define TPS6594_MASK_MCU_RAIL_TRIG			GENMASK(1, 0)
#define TPS6594_MASK_SOC_RAIL_TRIG			GENMASK(3, 2)
#define TPS6594_MASK_OTHER_RAIL_TRIG			GENMASK(5, 4)
#define TPS6594_MASK_SEVERE_ERR_TRIG			GENMASK(7, 6)

/* FSM_TRIG_SEL_2 register field definition */
#define TPS6594_MASK_MODERATE_ERR_TRIG			GENMASK(1, 0)

/* FSM_TRIG_MASK_X register field definition */
#define TPS6594_BIT_GPIOX_FSM_MASK(gpio_inst)		BIT(((gpio_inst) << 1) % 8)
#define TPS6594_BIT_GPIOX_FSM_MASK_POL(gpio_inst)	BIT(((gpio_inst) << 1) % 8 + 1)

#define TPS65224_BIT_GPIOX_FSM_MASK(gpio_inst)		BIT(((gpio_inst) << 1) % 6)
#define TPS65224_BIT_GPIOX_FSM_MASK_POL(gpio_inst)	BIT(((gpio_inst) << 1) % 6 + 1)

/* MASK_BUCKX register field definition */
#define TPS6594_BIT_BUCKX_OV_MASK(buck_inst)		BIT(((buck_inst) << 2) % 8)
#define TPS6594_BIT_BUCKX_UV_MASK(buck_inst)		BIT(((buck_inst) << 2) % 8 + 1)
#define TPS6594_BIT_BUCKX_ILIM_MASK(buck_inst)		BIT(((buck_inst) << 2) % 8 + 3)

/* MASK_LDOX register field definition */
#define TPS6594_BIT_LDOX_OV_MASK(ldo_inst)		BIT(((ldo_inst) << 2) % 8)
#define TPS6594_BIT_LDOX_UV_MASK(ldo_inst)		BIT(((ldo_inst) << 2) % 8 + 1)
#define TPS6594_BIT_LDOX_ILIM_MASK(ldo_inst)		BIT(((ldo_inst) << 2) % 8 + 3)

/* MASK_VMON register field definition */
#define TPS6594_BIT_VCCA_OV_MASK			BIT(0)
#define TPS6594_BIT_VCCA_UV_MASK			BIT(1)
#define TPS6594_BIT_VMON1_OV_MASK			BIT(2)
#define TPS6594_BIT_VMON1_UV_MASK			BIT(3)
#define TPS6594_BIT_VMON2_OV_MASK			BIT(5)
#define TPS6594_BIT_VMON2_UV_MASK			BIT(6)

/* MASK_BUCK Register field definition */
#define TPS65224_BIT_BUCK1_UVOV_MASK			BIT(0)
#define TPS65224_BIT_BUCK2_UVOV_MASK			BIT(1)
#define TPS65224_BIT_BUCK3_UVOV_MASK			BIT(2)
#define TPS65224_BIT_BUCK4_UVOV_MASK			BIT(4)

/* MASK_LDO_VMON register field definition */
#define TPS65224_BIT_LDO1_UVOV_MASK			BIT(0)
#define TPS65224_BIT_LDO2_UVOV_MASK			BIT(1)
#define TPS65224_BIT_LDO3_UVOV_MASK			BIT(2)
#define TPS65224_BIT_VCCA_UVOV_MASK			BIT(4)
#define TPS65224_BIT_VMON1_UVOV_MASK			BIT(5)
#define TPS65224_BIT_VMON2_UVOV_MASK			BIT(6)

/* MASK_GPIOX register field definition */
#define TPS6594_BIT_GPIOX_FALL_MASK(gpio_inst)		BIT((gpio_inst) < 8 ? \
							    (gpio_inst) : (gpio_inst) % 8)
#define TPS6594_BIT_GPIOX_RISE_MASK(gpio_inst)		BIT((gpio_inst) < 8 ? \
							    (gpio_inst) : (gpio_inst) % 8 + 3)
/* MASK_GPIOX register field definition */
#define TPS65224_BIT_GPIOX_FALL_MASK(gpio_inst)		BIT((gpio_inst))
#define TPS65224_BIT_GPIOX_RISE_MASK(gpio_inst)		BIT((gpio_inst))

/* MASK_STARTUP register field definition */
#define TPS6594_BIT_NPWRON_START_MASK			BIT(0)
#define TPS6594_BIT_ENABLE_MASK				BIT(1)
#define TPS6594_BIT_FSD_MASK				BIT(4)
#define TPS6594_BIT_SOFT_REBOOT_MASK			BIT(5)
#define TPS65224_BIT_VSENSE_MASK			BIT(0)
#define TPS65224_BIT_PB_SHORT_MASK			BIT(2)

/* MASK_MISC register field definition */
#define TPS6594_BIT_BIST_PASS_MASK			BIT(0)
#define TPS6594_BIT_EXT_CLK_MASK			BIT(1)
#define TPS65224_BIT_REG_UNLOCK_MASK			BIT(2)
#define TPS6594_BIT_TWARN_MASK				BIT(3)
#define TPS65224_BIT_PB_LONG_MASK			BIT(4)
#define TPS65224_BIT_PB_FALL_MASK			BIT(5)
#define TPS65224_BIT_PB_RISE_MASK			BIT(6)
#define TPS65224_BIT_ADC_CONV_READY_MASK		BIT(7)

/* MASK_MODERATE_ERR register field definition */
#define TPS6594_BIT_BIST_FAIL_MASK			BIT(1)
#define TPS6594_BIT_REG_CRC_ERR_MASK			BIT(2)
#define TPS6594_BIT_SPMI_ERR_MASK			BIT(4)
#define TPS6594_BIT_NPWRON_LONG_MASK			BIT(5)
#define TPS6594_BIT_NINT_READBACK_MASK			BIT(6)
#define TPS6594_BIT_NRSTOUT_READBACK_MASK		BIT(7)

/* MASK_FSM_ERR register field definition */
#define TPS6594_BIT_IMM_SHUTDOWN_MASK			BIT(0)
#define TPS6594_BIT_ORD_SHUTDOWN_MASK			BIT(1)
#define TPS6594_BIT_MCU_PWR_ERR_MASK			BIT(2)
#define TPS6594_BIT_SOC_PWR_ERR_MASK			BIT(3)
#define TPS65224_BIT_COMM_ERR_MASK			BIT(4)
#define TPS65224_BIT_I2C2_ERR_MASK			BIT(5)

/* MASK_COMM_ERR register field definition */
#define TPS6594_BIT_COMM_FRM_ERR_MASK			BIT(0)
#define TPS6594_BIT_COMM_CRC_ERR_MASK			BIT(1)
#define TPS6594_BIT_COMM_ADR_ERR_MASK			BIT(3)
#define TPS6594_BIT_I2C2_CRC_ERR_MASK			BIT(5)
#define TPS6594_BIT_I2C2_ADR_ERR_MASK			BIT(7)

/* MASK_READBACK_ERR register field definition */
#define TPS6594_BIT_EN_DRV_READBACK_MASK		BIT(0)
#define TPS6594_BIT_NRSTOUT_SOC_READBACK_MASK		BIT(3)

/* MASK_ESM register field definition */
#define TPS6594_BIT_ESM_SOC_PIN_MASK			BIT(0)
#define TPS6594_BIT_ESM_SOC_FAIL_MASK			BIT(1)
#define TPS6594_BIT_ESM_SOC_RST_MASK			BIT(2)
#define TPS6594_BIT_ESM_MCU_PIN_MASK			BIT(3)
#define TPS6594_BIT_ESM_MCU_FAIL_MASK			BIT(4)
#define TPS6594_BIT_ESM_MCU_RST_MASK			BIT(5)

/* INT_TOP register field definition */
#define TPS6594_BIT_BUCK_INT				BIT(0)
#define TPS6594_BIT_LDO_VMON_INT			BIT(1)
#define TPS6594_BIT_GPIO_INT				BIT(2)
#define TPS6594_BIT_STARTUP_INT				BIT(3)
#define TPS6594_BIT_MISC_INT				BIT(4)
#define TPS6594_BIT_MODERATE_ERR_INT			BIT(5)
#define TPS6594_BIT_SEVERE_ERR_INT			BIT(6)
#define TPS6594_BIT_FSM_ERR_INT				BIT(7)

/* INT_BUCK register field definition */
#define TPS6594_BIT_BUCK1_2_INT				BIT(0)
#define TPS6594_BIT_BUCK3_4_INT				BIT(1)
#define TPS6594_BIT_BUCK5_INT				BIT(2)

/* INT_BUCK register field definition */
#define TPS65224_BIT_BUCK1_UVOV_INT			BIT(0)
#define TPS65224_BIT_BUCK2_UVOV_INT			BIT(1)
#define TPS65224_BIT_BUCK3_UVOV_INT			BIT(2)
#define TPS65224_BIT_BUCK4_UVOV_INT			BIT(3)

/* INT_BUCKX register field definition */
#define TPS6594_BIT_BUCKX_OV_INT(buck_inst)		BIT(((buck_inst) << 2) % 8)
#define TPS6594_BIT_BUCKX_UV_INT(buck_inst)		BIT(((buck_inst) << 2) % 8 + 1)
#define TPS6594_BIT_BUCKX_SC_INT(buck_inst)		BIT(((buck_inst) << 2) % 8 + 2)
#define TPS6594_BIT_BUCKX_ILIM_INT(buck_inst)		BIT(((buck_inst) << 2) % 8 + 3)

/* INT_LDO_VMON register field definition */
#define TPS6594_BIT_LDO1_2_INT				BIT(0)
#define TPS6594_BIT_LDO3_4_INT				BIT(1)
#define TPS6594_BIT_VCCA_INT				BIT(4)

/* INT_LDO_VMON register field definition */
#define TPS65224_BIT_LDO1_UVOV_INT			BIT(0)
#define TPS65224_BIT_LDO2_UVOV_INT			BIT(1)
#define TPS65224_BIT_LDO3_UVOV_INT			BIT(2)
#define TPS65224_BIT_VCCA_UVOV_INT			BIT(4)
#define TPS65224_BIT_VMON1_UVOV_INT			BIT(5)
#define TPS65224_BIT_VMON2_UVOV_INT			BIT(6)

/* INT_LDOX register field definition */
#define TPS6594_BIT_LDOX_OV_INT(ldo_inst)		BIT(((ldo_inst) << 2) % 8)
#define TPS6594_BIT_LDOX_UV_INT(ldo_inst)		BIT(((ldo_inst) << 2) % 8 + 1)
#define TPS6594_BIT_LDOX_SC_INT(ldo_inst)		BIT(((ldo_inst) << 2) % 8 + 2)
#define TPS6594_BIT_LDOX_ILIM_INT(ldo_inst)		BIT(((ldo_inst) << 2) % 8 + 3)

/* INT_VMON register field definition */
#define TPS6594_BIT_VCCA_OV_INT				BIT(0)
#define TPS6594_BIT_VCCA_UV_INT				BIT(1)
#define TPS6594_BIT_VMON1_OV_INT			BIT(2)
#define TPS6594_BIT_VMON1_UV_INT			BIT(3)
#define TPS6594_BIT_VMON1_RV_INT			BIT(4)
#define TPS6594_BIT_VMON2_OV_INT			BIT(5)
#define TPS6594_BIT_VMON2_UV_INT			BIT(6)
#define TPS6594_BIT_VMON2_RV_INT			BIT(7)

/* INT_GPIO register field definition */
#define TPS6594_BIT_GPIO9_INT				BIT(0)
#define TPS6594_BIT_GPIO10_INT				BIT(1)
#define TPS6594_BIT_GPIO11_INT				BIT(2)
#define TPS6594_BIT_GPIO1_8_INT				BIT(3)

/* INT_GPIOX register field definition */
#define TPS6594_BIT_GPIOX_INT(gpio_inst)		BIT(gpio_inst)

/* INT_GPIO register field definition */
#define TPS65224_BIT_GPIO1_INT				BIT(0)
#define TPS65224_BIT_GPIO2_INT				BIT(1)
#define TPS65224_BIT_GPIO3_INT				BIT(2)
#define TPS65224_BIT_GPIO4_INT				BIT(3)
#define TPS65224_BIT_GPIO5_INT				BIT(4)
#define TPS65224_BIT_GPIO6_INT				BIT(5)

/* INT_STARTUP register field definition */
#define TPS6594_BIT_NPWRON_START_INT			BIT(0)
#define TPS65224_BIT_VSENSE_INT				BIT(0)
#define TPS6594_BIT_ENABLE_INT				BIT(1)
#define TPS6594_BIT_RTC_INT				BIT(2)
#define TPS65224_BIT_PB_SHORT_INT			BIT(2)
#define TPS6594_BIT_FSD_INT				BIT(4)
#define TPS6594_BIT_SOFT_REBOOT_INT			BIT(5)

/* INT_MISC register field definition */
#define TPS6594_BIT_BIST_PASS_INT			BIT(0)
#define TPS6594_BIT_EXT_CLK_INT				BIT(1)
#define TPS65224_BIT_REG_UNLOCK_INT			BIT(2)
#define TPS6594_BIT_TWARN_INT				BIT(3)
#define TPS65224_BIT_PB_LONG_INT			BIT(4)
#define TPS65224_BIT_PB_FALL_INT			BIT(5)
#define TPS65224_BIT_PB_RISE_INT			BIT(6)
#define TPS65224_BIT_ADC_CONV_READY_INT			BIT(7)

/* INT_MODERATE_ERR register field definition */
#define TPS6594_BIT_TSD_ORD_INT				BIT(0)
#define TPS6594_BIT_BIST_FAIL_INT			BIT(1)
#define TPS6594_BIT_REG_CRC_ERR_INT			BIT(2)
#define TPS6594_BIT_RECOV_CNT_INT			BIT(3)
#define TPS6594_BIT_SPMI_ERR_INT			BIT(4)
#define TPS6594_BIT_NPWRON_LONG_INT			BIT(5)
#define TPS6594_BIT_NINT_READBACK_INT			BIT(6)
#define TPS6594_BIT_NRSTOUT_READBACK_INT		BIT(7)

/* INT_SEVERE_ERR register field definition */
#define TPS6594_BIT_TSD_IMM_INT				BIT(0)
#define TPS6594_BIT_VCCA_OVP_INT			BIT(1)
#define TPS6594_BIT_PFSM_ERR_INT			BIT(2)
#define TPS65224_BIT_BG_XMON_INT			BIT(3)

/* INT_FSM_ERR register field definition */
#define TPS6594_BIT_IMM_SHUTDOWN_INT			BIT(0)
#define TPS6594_BIT_ORD_SHUTDOWN_INT			BIT(1)
#define TPS6594_BIT_MCU_PWR_ERR_INT			BIT(2)
#define TPS6594_BIT_SOC_PWR_ERR_INT			BIT(3)
#define TPS6594_BIT_COMM_ERR_INT			BIT(4)
#define TPS6594_BIT_READBACK_ERR_INT			BIT(5)
#define TPS65224_BIT_I2C2_ERR_INT			BIT(5)
#define TPS6594_BIT_ESM_INT				BIT(6)
#define TPS6594_BIT_WD_INT				BIT(7)

/* INT_COMM_ERR register field definition */
#define TPS6594_BIT_COMM_FRM_ERR_INT			BIT(0)
#define TPS6594_BIT_COMM_CRC_ERR_INT			BIT(1)
#define TPS6594_BIT_COMM_ADR_ERR_INT			BIT(3)
#define TPS6594_BIT_I2C2_CRC_ERR_INT			BIT(5)
#define TPS6594_BIT_I2C2_ADR_ERR_INT			BIT(7)

/* INT_READBACK_ERR register field definition */
#define TPS6594_BIT_EN_DRV_READBACK_INT			BIT(0)
#define TPS6594_BIT_NRSTOUT_SOC_READBACK_INT		BIT(3)

/* INT_ESM register field definition */
#define TPS6594_BIT_ESM_SOC_PIN_INT			BIT(0)
#define TPS6594_BIT_ESM_SOC_FAIL_INT			BIT(1)
#define TPS6594_BIT_ESM_SOC_RST_INT			BIT(2)
#define TPS6594_BIT_ESM_MCU_PIN_INT			BIT(3)
#define TPS6594_BIT_ESM_MCU_FAIL_INT			BIT(4)
#define TPS6594_BIT_ESM_MCU_RST_INT			BIT(5)

/* STAT_BUCKX register field definition */
#define TPS6594_BIT_BUCKX_OV_STAT(buck_inst)		BIT(((buck_inst) << 2) % 8)
#define TPS6594_BIT_BUCKX_UV_STAT(buck_inst)		BIT(((buck_inst) << 2) % 8 + 1)
#define TPS6594_BIT_BUCKX_ILIM_STAT(buck_inst)		BIT(((buck_inst) << 2) % 8 + 3)

/* STAT_LDOX register field definition */
#define TPS6594_BIT_LDOX_OV_STAT(ldo_inst)		BIT(((ldo_inst) << 2) % 8)
#define TPS6594_BIT_LDOX_UV_STAT(ldo_inst)		BIT(((ldo_inst) << 2) % 8 + 1)
#define TPS6594_BIT_LDOX_ILIM_STAT(ldo_inst)		BIT(((ldo_inst) << 2) % 8 + 3)

/* STAT_VMON register field definition */
#define TPS6594_BIT_VCCA_OV_STAT			BIT(0)
#define TPS6594_BIT_VCCA_UV_STAT			BIT(1)
#define TPS6594_BIT_VMON1_OV_STAT			BIT(2)
#define TPS6594_BIT_VMON1_UV_STAT			BIT(3)
#define TPS6594_BIT_VMON2_OV_STAT			BIT(5)
#define TPS6594_BIT_VMON2_UV_STAT			BIT(6)

/* STAT_LDO_VMON register field definition */
#define TPS65224_BIT_LDO1_UVOV_STAT			BIT(0)
#define TPS65224_BIT_LDO2_UVOV_STAT			BIT(1)
#define TPS65224_BIT_LDO3_UVOV_STAT			BIT(2)
#define TPS65224_BIT_VCCA_UVOV_STAT			BIT(4)
#define TPS65224_BIT_VMON1_UVOV_STAT			BIT(5)
#define TPS65224_BIT_VMON2_UVOV_STAT			BIT(6)

/* STAT_STARTUP register field definition */
#define TPS65224_BIT_VSENSE_STAT			BIT(0)
#define TPS6594_BIT_ENABLE_STAT				BIT(1)
#define TPS65224_BIT_PB_LEVEL_STAT			BIT(2)

/* STAT_MISC register field definition */
#define TPS6594_BIT_EXT_CLK_STAT			BIT(1)
#define TPS6594_BIT_TWARN_STAT				BIT(3)

/* STAT_MODERATE_ERR register field definition */
#define TPS6594_BIT_TSD_ORD_STAT			BIT(0)

/* STAT_SEVERE_ERR register field definition */
#define TPS6594_BIT_TSD_IMM_STAT			BIT(0)
#define TPS6594_BIT_VCCA_OVP_STAT			BIT(1)
#define TPS65224_BIT_BG_XMON_STAT			BIT(3)

/* STAT_READBACK_ERR register field definition */
#define TPS6594_BIT_EN_DRV_READBACK_STAT		BIT(0)
#define TPS6594_BIT_NINT_READBACK_STAT			BIT(1)
#define TPS6594_BIT_NRSTOUT_READBACK_STAT		BIT(2)
#define TPS6594_BIT_NRSTOUT_SOC_READBACK_STAT		BIT(3)

/* PGOOD_SEL_1 register field definition */
#define TPS6594_MASK_PGOOD_SEL_BUCK1			GENMASK(1, 0)
#define TPS6594_MASK_PGOOD_SEL_BUCK2			GENMASK(3, 2)
#define TPS6594_MASK_PGOOD_SEL_BUCK3			GENMASK(5, 4)
#define TPS6594_MASK_PGOOD_SEL_BUCK4			GENMASK(7, 6)

/* PGOOD_SEL_2 register field definition */
#define TPS6594_MASK_PGOOD_SEL_BUCK5			GENMASK(1, 0)

/* PGOOD_SEL_3 register field definition */
#define TPS6594_MASK_PGOOD_SEL_LDO1			GENMASK(1, 0)
#define TPS6594_MASK_PGOOD_SEL_LDO2			GENMASK(3, 2)
#define TPS6594_MASK_PGOOD_SEL_LDO3			GENMASK(5, 4)
#define TPS6594_MASK_PGOOD_SEL_LDO4			GENMASK(7, 6)

/* PGOOD_SEL_4 register field definition */
#define TPS6594_BIT_PGOOD_SEL_VCCA			BIT(0)
#define TPS6594_BIT_PGOOD_SEL_VMON1			BIT(1)
#define TPS6594_BIT_PGOOD_SEL_VMON2			BIT(2)
#define TPS6594_BIT_PGOOD_SEL_TDIE_WARN			BIT(3)
#define TPS6594_BIT_PGOOD_SEL_NRSTOUT			BIT(4)
#define TPS6594_BIT_PGOOD_SEL_NRSTOUT_SOC		BIT(5)
#define TPS6594_BIT_PGOOD_POL				BIT(6)
#define TPS6594_BIT_PGOOD_WINDOW			BIT(7)

/* PLL_CTRL register field definition */
#define TPS6594_MASK_EXT_CLK_FREQ			GENMASK(1, 0)

/* CONFIG_1 register field definition */
#define TPS6594_BIT_TWARN_LEVEL				BIT(0)
#define TPS6594_BIT_TSD_ORD_LEVEL			BIT(1)
#define TPS6594_BIT_I2C1_HS				BIT(3)
#define TPS6594_BIT_I2C2_HS				BIT(4)
#define TPS6594_BIT_EN_ILIM_FSM_CTRL			BIT(5)
#define TPS6594_BIT_NSLEEP1_MASK			BIT(6)
#define TPS6594_BIT_NSLEEP2_MASK			BIT(7)

/* CONFIG_2 register field definition */
#define TPS6594_BIT_BB_CHARGER_EN			BIT(0)
#define TPS6594_BIT_BB_ICHR				BIT(1)
#define TPS6594_MASK_BB_VEOC				GENMASK(3, 2)
#define TPS65224_BIT_I2C1_SPI_CRC_EN			BIT(4)
#define TPS65224_BIT_I2C2_CRC_EN			BIT(5)
#define TPS6594_BB_EOC_RDY				BIT(7)

/* ENABLE_DRV_REG register field definition */
#define TPS6594_BIT_ENABLE_DRV				BIT(0)

/* MISC_CTRL register field definition */
#define TPS6594_BIT_NRSTOUT				BIT(0)
#define TPS6594_BIT_NRSTOUT_SOC				BIT(1)
#define TPS6594_BIT_LPM_EN				BIT(2)
#define TPS6594_BIT_CLKMON_EN				BIT(3)
#define TPS6594_BIT_AMUXOUT_EN				BIT(4)
#define TPS6594_BIT_SEL_EXT_CLK				BIT(5)
#define TPS6594_MASK_SYNCCLKOUT_FREQ_SEL		GENMASK(7, 6)

/* ENABLE_DRV_STAT register field definition */
#define TPS6594_BIT_EN_DRV_IN				BIT(0)
#define TPS6594_BIT_NRSTOUT_IN				BIT(1)
#define TPS6594_BIT_NRSTOUT_SOC_IN			BIT(2)
#define TPS6594_BIT_FORCE_EN_DRV_LOW			BIT(3)
#define TPS6594_BIT_SPMI_LPM_EN				BIT(4)
#define TPS65224_BIT_TSD_DISABLE			BIT(5)

/* RECOV_CNT_REG_1 register field definition */
#define TPS6594_MASK_RECOV_CNT				GENMASK(3, 0)

/* RECOV_CNT_REG_2 register field definition */
#define TPS6594_MASK_RECOV_CNT_THR			GENMASK(3, 0)
#define TPS6594_BIT_RECOV_CNT_CLR			BIT(4)

/* FSM_I2C_TRIGGERS register field definition */
#define TPS6594_BIT_TRIGGER_I2C(bit)			BIT(bit)

/* FSM_NSLEEP_TRIGGERS register field definition */
#define TPS6594_BIT_NSLEEP1B				BIT(0)
#define TPS6594_BIT_NSLEEP2B				BIT(1)

/* BUCK_RESET_REG register field definition */
#define TPS6594_BIT_BUCKX_RESET(buck_inst)		BIT(buck_inst)

/* SPREAD_SPECTRUM_1 register field definition */
#define TPS6594_MASK_SS_DEPTH				GENMASK(1, 0)
#define TPS6594_BIT_SS_EN				BIT(2)

/* FREQ_SEL register field definition */
#define TPS6594_BIT_BUCKX_FREQ_SEL(buck_inst)		BIT(buck_inst)

/* FSM_STEP_SIZE register field definition */
#define TPS6594_MASK_PFSM_DELAY_STEP			GENMASK(4, 0)

/* LDO_RV_TIMEOUT_REG_1 register field definition */
#define TPS6594_MASK_LDO1_RV_TIMEOUT			GENMASK(3, 0)
#define TPS6594_MASK_LDO2_RV_TIMEOUT			GENMASK(7, 4)

/* LDO_RV_TIMEOUT_REG_2 register field definition */
#define TPS6594_MASK_LDO3_RV_TIMEOUT			GENMASK(3, 0)
#define TPS6594_MASK_LDO4_RV_TIMEOUT			GENMASK(7, 4)

/* USER_SPARE_REGS register field definition */
#define TPS6594_BIT_USER_SPARE(bit)			BIT(bit)

/* ESM_MCU_START_REG register field definition */
#define TPS6594_BIT_ESM_MCU_START			BIT(0)

/* ESM_MCU_MODE_CFG register field definition */
#define TPS6594_MASK_ESM_MCU_ERR_CNT_TH			GENMASK(3, 0)
#define TPS6594_BIT_ESM_MCU_ENDRV			BIT(5)
#define TPS6594_BIT_ESM_MCU_EN				BIT(6)
#define TPS6594_BIT_ESM_MCU_MODE			BIT(7)

/* ESM_MCU_ERR_CNT_REG register field definition */
#define TPS6594_MASK_ESM_MCU_ERR_CNT			GENMASK(4, 0)

/* ESM_SOC_START_REG register field definition */
#define TPS6594_BIT_ESM_SOC_START			BIT(0)

/* ESM_MCU_START_REG register field definition */
#define TPS65224_BIT_ESM_MCU_START			BIT(0)

/* ESM_SOC_MODE_CFG register field definition */
#define TPS6594_MASK_ESM_SOC_ERR_CNT_TH			GENMASK(3, 0)
#define TPS6594_BIT_ESM_SOC_ENDRV			BIT(5)
#define TPS6594_BIT_ESM_SOC_EN				BIT(6)
#define TPS6594_BIT_ESM_SOC_MODE			BIT(7)

/* ESM_MCU_MODE_CFG register field definition */
#define TPS65224_MASK_ESM_MCU_ERR_CNT_TH		GENMASK(3, 0)
#define TPS65224_BIT_ESM_MCU_ENDRV			BIT(5)
#define TPS65224_BIT_ESM_MCU_EN				BIT(6)
#define TPS65224_BIT_ESM_MCU_MODE			BIT(7)

/* ESM_SOC_ERR_CNT_REG register field definition */
#define TPS6594_MASK_ESM_SOC_ERR_CNT			GENMASK(4, 0)

/* ESM_MCU_ERR_CNT_REG register field definition */
#define TPS6594_MASK_ESM_MCU_ERR_CNT			GENMASK(4, 0)

/* REGISTER_LOCK register field definition */
#define TPS6594_BIT_REGISTER_LOCK_STATUS		BIT(0)

/* VMON_CONF register field definition */
#define TPS6594_MASK_VMON1_SLEW_RATE			GENMASK(2, 0)
#define TPS6594_MASK_VMON2_SLEW_RATE			GENMASK(5, 3)

/* SRAM_ACCESS_1 Register field definition */
#define TPS65224_MASk_SRAM_UNLOCK_SEQ			GENMASK(7, 0)

/* SRAM_ACCESS_2 Register field definition */
#define TPS65224_BIT_SRAM_WRITE_MODE			BIT(0)
#define TPS65224_BIT_OTP_PROG_USER			BIT(1)
#define TPS65224_BIT_OTP_PROG_PFSM			BIT(2)
#define TPS65224_BIT_OTP_PROG_STATUS			BIT(3)
#define TPS65224_BIT_SRAM_UNLOCKED			BIT(6)
#define TPS65224_USER_PROG_ALLOWED			BIT(7)

/* SRAM_ADDR_CTRL Register field definition */
#define TPS65224_MASk_SRAM_SEL				GENMASK(1, 0)

/* RECOV_CNT_PFSM_INCR Register field definition */
#define TPS65224_BIT_INCREMENT_RECOV_CNT		BIT(0)

/* MANUFACTURING_VER Register field definition */
#define TPS65224_MASK_SILICON_REV			GENMASK(7, 0)

/* CUSTOMER_NVM_ID_REG Register field definition */
#define TPS65224_MASK_CUSTOMER_NVM_ID			GENMASK(7, 0)

/* SOFT_REBOOT_REG register field definition */
#define TPS6594_BIT_SOFT_REBOOT				BIT(0)

/* RTC_SECONDS & ALARM_SECONDS register field definition */
#define TPS6594_MASK_SECOND_0				GENMASK(3, 0)
#define TPS6594_MASK_SECOND_1				GENMASK(6, 4)

/* RTC_MINUTES & ALARM_MINUTES register field definition */
#define TPS6594_MASK_MINUTE_0				GENMASK(3, 0)
#define TPS6594_MASK_MINUTE_1				GENMASK(6, 4)

/* RTC_HOURS & ALARM_HOURS register field definition */
#define TPS6594_MASK_HOUR_0				GENMASK(3, 0)
#define TPS6594_MASK_HOUR_1				GENMASK(5, 4)
#define TPS6594_BIT_PM_NAM				BIT(7)

/* RTC_DAYS & ALARM_DAYS register field definition */
#define TPS6594_MASK_DAY_0				GENMASK(3, 0)
#define TPS6594_MASK_DAY_1				GENMASK(5, 4)

/* RTC_MONTHS & ALARM_MONTHS register field definition */
#define TPS6594_MASK_MONTH_0				GENMASK(3, 0)
#define TPS6594_BIT_MONTH_1				BIT(4)

/* RTC_YEARS & ALARM_YEARS register field definition */
#define TPS6594_MASK_YEAR_0				GENMASK(3, 0)
#define TPS6594_MASK_YEAR_1				GENMASK(7, 4)

/* RTC_WEEKS register field definition */
#define TPS6594_MASK_WEEK				GENMASK(2, 0)

/* RTC_CTRL_1 register field definition */
#define TPS6594_BIT_STOP_RTC				BIT(0)
#define TPS6594_BIT_ROUND_30S				BIT(1)
#define TPS6594_BIT_AUTO_COMP				BIT(2)
#define TPS6594_BIT_MODE_12_24				BIT(3)
#define TPS6594_BIT_SET_32_COUNTER			BIT(5)
#define TPS6594_BIT_GET_TIME				BIT(6)
#define TPS6594_BIT_RTC_V_OPT				BIT(7)

/* RTC_CTRL_2 register field definition */
#define TPS6594_BIT_XTAL_EN				BIT(0)
#define TPS6594_MASK_XTAL_SEL				GENMASK(2, 1)
#define TPS6594_BIT_LP_STANDBY_SEL			BIT(3)
#define TPS6594_BIT_FAST_BIST				BIT(4)
#define TPS6594_MASK_STARTUP_DEST			GENMASK(6, 5)
#define TPS6594_BIT_FIRST_STARTUP_DONE			BIT(7)

/* RTC_STATUS register field definition */
#define TPS6594_BIT_RUN					BIT(1)
#define TPS6594_BIT_TIMER				BIT(5)
#define TPS6594_BIT_ALARM				BIT(6)
#define TPS6594_BIT_POWER_UP				BIT(7)

/* RTC_INTERRUPTS register field definition */
#define TPS6594_MASK_EVERY				GENMASK(1, 0)
#define TPS6594_BIT_IT_TIMER				BIT(2)
#define TPS6594_BIT_IT_ALARM				BIT(3)

/* RTC_RESET_STATUS register field definition */
#define TPS6594_BIT_RESET_STATUS_RTC			BIT(0)

/* SERIAL_IF_CONFIG register field definition */
#define TPS6594_BIT_I2C_SPI_SEL				BIT(0)
#define TPS6594_BIT_I2C1_SPI_CRC_EN			BIT(1)
#define TPS6594_BIT_I2C2_CRC_EN				BIT(2)
#define TPS6594_MASK_T_CRC				GENMASK(7, 3)

/* ADC_CTRL Register field definition */
#define TPS65224_BIT_ADC_START				BIT(0)
#define TPS65224_BIT_ADC_CONT_CONV			BIT(1)
#define TPS65224_BIT_ADC_THERMAL_SEL			BIT(2)
#define TPS65224_BIT_ADC_RDIV_EN			BIT(3)
#define TPS65224_BIT_ADC_STATUS				BIT(7)

/* ADC_RESULT_REG_1 Register field definition */
#define TPS65224_MASK_ADC_RESULT_11_4			GENMASK(7, 0)

/* ADC_RESULT_REG_2 Register field definition */
#define TPS65224_MASK_ADC_RESULT_3_0			GENMASK(7, 4)

/* STARTUP_CTRL Register field definition */
#define TPS65224_MASK_STARTUP_DEST			GENMASK(6, 5)
#define TPS65224_BIT_FIRST_STARTUP_DONE			BIT(7)

/* SCRATCH_PAD_REG_1 Register field definition */
#define TPS6594_MASK_SCRATCH_PAD_1			GENMASK(7, 0)

/* SCRATCH_PAD_REG_2 Register field definition */
#define TPS6594_MASK_SCRATCH_PAD_2			GENMASK(7, 0)

/* SCRATCH_PAD_REG_3 Register field definition */
#define TPS6594_MASK_SCRATCH_PAD_3			GENMASK(7, 0)

/* SCRATCH_PAD_REG_4 Register field definition */
#define TPS6594_MASK_SCRATCH_PAD_4			GENMASK(7, 0)

/* PFSM_DELAY_REG_1 Register field definition */
#define TPS6594_MASK_PFSM_DELAY1			GENMASK(7, 0)

/* PFSM_DELAY_REG_2 Register field definition */
#define TPS6594_MASK_PFSM_DELAY2			GENMASK(7, 0)

/* PFSM_DELAY_REG_3 Register field definition */
#define TPS6594_MASK_PFSM_DELAY3			GENMASK(7, 0)

/* PFSM_DELAY_REG_4 Register field definition */
#define TPS6594_MASK_PFSM_DELAY4			GENMASK(7, 0)

/* CRC_CALC_CONTROL Register field definition */
#define TPS65224_BIT_RUN_CRC_BIST			BIT(0)
#define TPS65224_BIT_RUN_CRC_UPDATE			BIT(1)

/* ADC_GAIN_COMP_REG Register field definition */
#define TPS65224_MASK_ADC_GAIN_COMP			GENMASK(7, 0)

/* REGMAP_USER_CRC_LOW Register field definition */
#define TPS65224_MASK_REGMAP_USER_CRC16_LOW		GENMASK(7, 0)

/* REGMAP_USER_CRC_HIGH Register field definition */
#define TPS65224_MASK_REGMAP_USER_CRC16_HIGH		GENMASK(7, 0)

/* WD_ANSWER_REG Register field definition */
#define TPS6594_MASK_WD_ANSWER				GENMASK(7, 0)

/* WD_QUESTION_ANSW_CNT register field definition */
#define TPS6594_MASK_WD_QUESTION			GENMASK(3, 0)
#define TPS6594_MASK_WD_ANSW_CNT			GENMASK(5, 4)
#define TPS65224_BIT_INT_TOP_STATUS			BIT(7)

/* WD WIN1_CFG register field definition */
#define TPS6594_MASK_WD_WIN1_CFG			GENMASK(6, 0)

/* WD WIN2_CFG register field definition */
#define TPS6594_MASK_WD_WIN2_CFG			GENMASK(6, 0)

/* WD LongWin register field definition */
#define TPS6594_MASK_WD_LONGWIN_CFG			GENMASK(7, 0)

/* WD_MODE_REG register field definition */
#define TPS6594_BIT_WD_RETURN_LONGWIN			BIT(0)
#define TPS6594_BIT_WD_MODE_SELECT			BIT(1)
#define TPS6594_BIT_WD_PWRHOLD				BIT(2)
#define TPS65224_BIT_WD_ENDRV_SEL			BIT(6)
#define TPS65224_BIT_WD_CNT_SEL				BIT(7)

/* WD_QA_CFG register field definition */
#define TPS6594_MASK_WD_QUESTION_SEED			GENMASK(3, 0)
#define TPS6594_MASK_WD_QA_LFSR				GENMASK(5, 4)
#define TPS6594_MASK_WD_QA_FDBK				GENMASK(7, 6)

/* WD_ERR_STATUS register field definition */
#define TPS6594_BIT_WD_LONGWIN_TIMEOUT_INT		BIT(0)
#define TPS6594_BIT_WD_TIMEOUT				BIT(1)
#define TPS6594_BIT_WD_TRIG_EARLY			BIT(2)
#define TPS6594_BIT_WD_ANSW_EARLY			BIT(3)
#define TPS6594_BIT_WD_SEQ_ERR				BIT(4)
#define TPS6594_BIT_WD_ANSW_ERR				BIT(5)
#define TPS6594_BIT_WD_FAIL_INT				BIT(6)
#define TPS6594_BIT_WD_RST_INT				BIT(7)

/* WD_THR_CFG register field definition */
#define TPS6594_MASK_WD_RST_TH				GENMASK(2, 0)
#define TPS6594_MASK_WD_FAIL_TH				GENMASK(5, 3)
#define TPS6594_BIT_WD_EN				BIT(6)
#define TPS6594_BIT_WD_RST_EN				BIT(7)

/* WD_FAIL_CNT_REG register field definition */
#define TPS6594_MASK_WD_FAIL_CNT			GENMASK(3, 0)
#define TPS6594_BIT_WD_FIRST_OK				BIT(5)
#define TPS6594_BIT_WD_BAD_EVENT			BIT(6)

/* CRC8 polynomial for I2C & SPI protocols */
#define TPS6594_CRC8_POLYNOMIAL	0x07

/* IRQs */
enum tps6594_irqs {
	/* INT_BUCK1_2 register */
	TPS6594_IRQ_BUCK1_OV,
	TPS6594_IRQ_BUCK1_UV,
	TPS6594_IRQ_BUCK1_SC,
	TPS6594_IRQ_BUCK1_ILIM,
	TPS6594_IRQ_BUCK2_OV,
	TPS6594_IRQ_BUCK2_UV,
	TPS6594_IRQ_BUCK2_SC,
	TPS6594_IRQ_BUCK2_ILIM,
	/* INT_BUCK3_4 register */
	TPS6594_IRQ_BUCK3_OV,
	TPS6594_IRQ_BUCK3_UV,
	TPS6594_IRQ_BUCK3_SC,
	TPS6594_IRQ_BUCK3_ILIM,
	TPS6594_IRQ_BUCK4_OV,
	TPS6594_IRQ_BUCK4_UV,
	TPS6594_IRQ_BUCK4_SC,
	TPS6594_IRQ_BUCK4_ILIM,
	/* INT_BUCK5 register */
	TPS6594_IRQ_BUCK5_OV,
	TPS6594_IRQ_BUCK5_UV,
	TPS6594_IRQ_BUCK5_SC,
	TPS6594_IRQ_BUCK5_ILIM,
	/* INT_LDO1_2 register */
	TPS6594_IRQ_LDO1_OV,
	TPS6594_IRQ_LDO1_UV,
	TPS6594_IRQ_LDO1_SC,
	TPS6594_IRQ_LDO1_ILIM,
	TPS6594_IRQ_LDO2_OV,
	TPS6594_IRQ_LDO2_UV,
	TPS6594_IRQ_LDO2_SC,
	TPS6594_IRQ_LDO2_ILIM,
	/* INT_LDO3_4 register */
	TPS6594_IRQ_LDO3_OV,
	TPS6594_IRQ_LDO3_UV,
	TPS6594_IRQ_LDO3_SC,
	TPS6594_IRQ_LDO3_ILIM,
	TPS6594_IRQ_LDO4_OV,
	TPS6594_IRQ_LDO4_UV,
	TPS6594_IRQ_LDO4_SC,
	TPS6594_IRQ_LDO4_ILIM,
	/* INT_VMON register */
	TPS6594_IRQ_VCCA_OV,
	TPS6594_IRQ_VCCA_UV,
	TPS6594_IRQ_VMON1_OV,
	TPS6594_IRQ_VMON1_UV,
	TPS6594_IRQ_VMON1_RV,
	TPS6594_IRQ_VMON2_OV,
	TPS6594_IRQ_VMON2_UV,
	TPS6594_IRQ_VMON2_RV,
	/* INT_GPIO register */
	TPS6594_IRQ_GPIO9,
	TPS6594_IRQ_GPIO10,
	TPS6594_IRQ_GPIO11,
	/* INT_GPIO1_8 register */
	TPS6594_IRQ_GPIO1,
	TPS6594_IRQ_GPIO2,
	TPS6594_IRQ_GPIO3,
	TPS6594_IRQ_GPIO4,
	TPS6594_IRQ_GPIO5,
	TPS6594_IRQ_GPIO6,
	TPS6594_IRQ_GPIO7,
	TPS6594_IRQ_GPIO8,
	/* INT_STARTUP register */
	TPS6594_IRQ_NPWRON_START,
	TPS6594_IRQ_ENABLE,
	TPS6594_IRQ_FSD,
	TPS6594_IRQ_SOFT_REBOOT,
	/* INT_MISC register */
	TPS6594_IRQ_BIST_PASS,
	TPS6594_IRQ_EXT_CLK,
	TPS6594_IRQ_TWARN,
	/* INT_MODERATE_ERR register */
	TPS6594_IRQ_TSD_ORD,
	TPS6594_IRQ_BIST_FAIL,
	TPS6594_IRQ_REG_CRC_ERR,
	TPS6594_IRQ_RECOV_CNT,
	TPS6594_IRQ_SPMI_ERR,
	TPS6594_IRQ_NPWRON_LONG,
	TPS6594_IRQ_NINT_READBACK,
	TPS6594_IRQ_NRSTOUT_READBACK,
	/* INT_SEVERE_ERR register */
	TPS6594_IRQ_TSD_IMM,
	TPS6594_IRQ_VCCA_OVP,
	TPS6594_IRQ_PFSM_ERR,
	/* INT_FSM_ERR register */
	TPS6594_IRQ_IMM_SHUTDOWN,
	TPS6594_IRQ_ORD_SHUTDOWN,
	TPS6594_IRQ_MCU_PWR_ERR,
	TPS6594_IRQ_SOC_PWR_ERR,
	/* INT_COMM_ERR register */
	TPS6594_IRQ_COMM_FRM_ERR,
	TPS6594_IRQ_COMM_CRC_ERR,
	TPS6594_IRQ_COMM_ADR_ERR,
	TPS6594_IRQ_I2C2_CRC_ERR,
	TPS6594_IRQ_I2C2_ADR_ERR,
	/* INT_READBACK_ERR register */
	TPS6594_IRQ_EN_DRV_READBACK,
	TPS6594_IRQ_NRSTOUT_SOC_READBACK,
	/* INT_ESM register */
	TPS6594_IRQ_ESM_SOC_PIN,
	TPS6594_IRQ_ESM_SOC_FAIL,
	TPS6594_IRQ_ESM_SOC_RST,
	/* RTC_STATUS register */
	TPS6594_IRQ_TIMER,
	TPS6594_IRQ_ALARM,
	TPS6594_IRQ_POWER_UP,
};

#define TPS6594_IRQ_NAME_BUCK1_OV		"buck1_ov"
#define TPS6594_IRQ_NAME_BUCK1_UV		"buck1_uv"
#define TPS6594_IRQ_NAME_BUCK1_SC		"buck1_sc"
#define TPS6594_IRQ_NAME_BUCK1_ILIM		"buck1_ilim"
#define TPS6594_IRQ_NAME_BUCK2_OV		"buck2_ov"
#define TPS6594_IRQ_NAME_BUCK2_UV		"buck2_uv"
#define TPS6594_IRQ_NAME_BUCK2_SC		"buck2_sc"
#define TPS6594_IRQ_NAME_BUCK2_ILIM		"buck2_ilim"
#define TPS6594_IRQ_NAME_BUCK3_OV		"buck3_ov"
#define TPS6594_IRQ_NAME_BUCK3_UV		"buck3_uv"
#define TPS6594_IRQ_NAME_BUCK3_SC		"buck3_sc"
#define TPS6594_IRQ_NAME_BUCK3_ILIM		"buck3_ilim"
#define TPS6594_IRQ_NAME_BUCK4_OV		"buck4_ov"
#define TPS6594_IRQ_NAME_BUCK4_UV		"buck4_uv"
#define TPS6594_IRQ_NAME_BUCK4_SC		"buck4_sc"
#define TPS6594_IRQ_NAME_BUCK4_ILIM		"buck4_ilim"
#define TPS6594_IRQ_NAME_BUCK5_OV		"buck5_ov"
#define TPS6594_IRQ_NAME_BUCK5_UV		"buck5_uv"
#define TPS6594_IRQ_NAME_BUCK5_SC		"buck5_sc"
#define TPS6594_IRQ_NAME_BUCK5_ILIM		"buck5_ilim"
#define TPS6594_IRQ_NAME_LDO1_OV		"ldo1_ov"
#define TPS6594_IRQ_NAME_LDO1_UV		"ldo1_uv"
#define TPS6594_IRQ_NAME_LDO1_SC		"ldo1_sc"
#define TPS6594_IRQ_NAME_LDO1_ILIM		"ldo1_ilim"
#define TPS6594_IRQ_NAME_LDO2_OV		"ldo2_ov"
#define TPS6594_IRQ_NAME_LDO2_UV		"ldo2_uv"
#define TPS6594_IRQ_NAME_LDO2_SC		"ldo2_sc"
#define TPS6594_IRQ_NAME_LDO2_ILIM		"ldo2_ilim"
#define TPS6594_IRQ_NAME_LDO3_OV		"ldo3_ov"
#define TPS6594_IRQ_NAME_LDO3_UV		"ldo3_uv"
#define TPS6594_IRQ_NAME_LDO3_SC		"ldo3_sc"
#define TPS6594_IRQ_NAME_LDO3_ILIM		"ldo3_ilim"
#define TPS6594_IRQ_NAME_LDO4_OV		"ldo4_ov"
#define TPS6594_IRQ_NAME_LDO4_UV		"ldo4_uv"
#define TPS6594_IRQ_NAME_LDO4_SC		"ldo4_sc"
#define TPS6594_IRQ_NAME_LDO4_ILIM		"ldo4_ilim"
#define TPS6594_IRQ_NAME_VCCA_OV		"vcca_ov"
#define TPS6594_IRQ_NAME_VCCA_UV		"vcca_uv"
#define TPS6594_IRQ_NAME_VMON1_OV		"vmon1_ov"
#define TPS6594_IRQ_NAME_VMON1_UV		"vmon1_uv"
#define TPS6594_IRQ_NAME_VMON1_RV		"vmon1_rv"
#define TPS6594_IRQ_NAME_VMON2_OV		"vmon2_ov"
#define TPS6594_IRQ_NAME_VMON2_UV		"vmon2_uv"
#define TPS6594_IRQ_NAME_VMON2_RV		"vmon2_rv"
#define TPS6594_IRQ_NAME_GPIO9			"gpio9"
#define TPS6594_IRQ_NAME_GPIO10			"gpio10"
#define TPS6594_IRQ_NAME_GPIO11			"gpio11"
#define TPS6594_IRQ_NAME_GPIO1			"gpio1"
#define TPS6594_IRQ_NAME_GPIO2			"gpio2"
#define TPS6594_IRQ_NAME_GPIO3			"gpio3"
#define TPS6594_IRQ_NAME_GPIO4			"gpio4"
#define TPS6594_IRQ_NAME_GPIO5			"gpio5"
#define TPS6594_IRQ_NAME_GPIO6			"gpio6"
#define TPS6594_IRQ_NAME_GPIO7			"gpio7"
#define TPS6594_IRQ_NAME_GPIO8			"gpio8"
#define TPS6594_IRQ_NAME_NPWRON_START		"npwron_start"
#define TPS6594_IRQ_NAME_ENABLE			"enable"
#define TPS6594_IRQ_NAME_FSD			"fsd"
#define TPS6594_IRQ_NAME_SOFT_REBOOT		"soft_reboot"
#define TPS6594_IRQ_NAME_BIST_PASS		"bist_pass"
#define TPS6594_IRQ_NAME_EXT_CLK		"ext_clk"
#define TPS6594_IRQ_NAME_TWARN			"twarn"
#define TPS6594_IRQ_NAME_TSD_ORD		"tsd_ord"
#define TPS6594_IRQ_NAME_BIST_FAIL		"bist_fail"
#define TPS6594_IRQ_NAME_REG_CRC_ERR		"reg_crc_err"
#define TPS6594_IRQ_NAME_RECOV_CNT		"recov_cnt"
#define TPS6594_IRQ_NAME_SPMI_ERR		"spmi_err"
#define TPS6594_IRQ_NAME_NPWRON_LONG		"npwron_long"
#define TPS6594_IRQ_NAME_NINT_READBACK		"nint_readback"
#define TPS6594_IRQ_NAME_NRSTOUT_READBACK	"nrstout_readback"
#define TPS6594_IRQ_NAME_TSD_IMM		"tsd_imm"
#define TPS6594_IRQ_NAME_VCCA_OVP		"vcca_ovp"
#define TPS6594_IRQ_NAME_PFSM_ERR		"pfsm_err"
#define TPS6594_IRQ_NAME_IMM_SHUTDOWN		"imm_shutdown"
#define TPS6594_IRQ_NAME_ORD_SHUTDOWN		"ord_shutdown"
#define TPS6594_IRQ_NAME_MCU_PWR_ERR		"mcu_pwr_err"
#define TPS6594_IRQ_NAME_SOC_PWR_ERR		"soc_pwr_err"
#define TPS6594_IRQ_NAME_COMM_FRM_ERR		"comm_frm_err"
#define TPS6594_IRQ_NAME_COMM_CRC_ERR		"comm_crc_err"
#define TPS6594_IRQ_NAME_COMM_ADR_ERR		"comm_adr_err"
#define TPS6594_IRQ_NAME_EN_DRV_READBACK	"en_drv_readback"
#define TPS6594_IRQ_NAME_NRSTOUT_SOC_READBACK	"nrstout_soc_readback"
#define TPS6594_IRQ_NAME_ESM_SOC_PIN		"esm_soc_pin"
#define TPS6594_IRQ_NAME_ESM_SOC_FAIL		"esm_soc_fail"
#define TPS6594_IRQ_NAME_ESM_SOC_RST		"esm_soc_rst"
#define TPS6594_IRQ_NAME_TIMER			"timer"
#define TPS6594_IRQ_NAME_ALARM			"alarm"
#define TPS6594_IRQ_NAME_POWERUP		"powerup"

/* IRQs */
enum tps65224_irqs {
	/* INT_BUCK register */
	TPS65224_IRQ_BUCK1_UVOV,
	TPS65224_IRQ_BUCK2_UVOV,
	TPS65224_IRQ_BUCK3_UVOV,
	TPS65224_IRQ_BUCK4_UVOV,
	/* INT_LDO_VMON register */
	TPS65224_IRQ_LDO1_UVOV,
	TPS65224_IRQ_LDO2_UVOV,
	TPS65224_IRQ_LDO3_UVOV,
	TPS65224_IRQ_VCCA_UVOV,
	TPS65224_IRQ_VMON1_UVOV,
	TPS65224_IRQ_VMON2_UVOV,
	/* INT_GPIO register */
	TPS65224_IRQ_GPIO1,
	TPS65224_IRQ_GPIO2,
	TPS65224_IRQ_GPIO3,
	TPS65224_IRQ_GPIO4,
	TPS65224_IRQ_GPIO5,
	TPS65224_IRQ_GPIO6,
	/* INT_STARTUP register */
	TPS65224_IRQ_VSENSE,
	TPS65224_IRQ_ENABLE,
	TPS65224_IRQ_PB_SHORT,
	TPS65224_IRQ_FSD,
	TPS65224_IRQ_SOFT_REBOOT,
	/* INT_MISC register */
	TPS65224_IRQ_BIST_PASS,
	TPS65224_IRQ_EXT_CLK,
	TPS65224_IRQ_REG_UNLOCK,
	TPS65224_IRQ_TWARN,
	TPS65224_IRQ_PB_LONG,
	TPS65224_IRQ_PB_FALL,
	TPS65224_IRQ_PB_RISE,
	TPS65224_IRQ_ADC_CONV_READY,
	/* INT_MODERATE_ERR register */
	TPS65224_IRQ_TSD_ORD,
	TPS65224_IRQ_BIST_FAIL,
	TPS65224_IRQ_REG_CRC_ERR,
	TPS65224_IRQ_RECOV_CNT,
	/* INT_SEVERE_ERR register */
	TPS65224_IRQ_TSD_IMM,
	TPS65224_IRQ_VCCA_OVP,
	TPS65224_IRQ_PFSM_ERR,
	TPS65224_IRQ_BG_XMON,
	/* INT_FSM_ERR register */
	TPS65224_IRQ_IMM_SHUTDOWN,
	TPS65224_IRQ_ORD_SHUTDOWN,
	TPS65224_IRQ_MCU_PWR_ERR,
	TPS65224_IRQ_SOC_PWR_ERR,
	TPS65224_IRQ_COMM_ERR,
	TPS65224_IRQ_I2C2_ERR,
};

#define TPS65224_IRQ_NAME_BUCK1_UVOV		"buck1_uvov"
#define TPS65224_IRQ_NAME_BUCK2_UVOV		"buck2_uvov"
#define TPS65224_IRQ_NAME_BUCK3_UVOV		"buck3_uvov"
#define TPS65224_IRQ_NAME_BUCK4_UVOV		"buck4_uvov"
#define TPS65224_IRQ_NAME_LDO1_UVOV		"ldo1_uvov"
#define TPS65224_IRQ_NAME_LDO2_UVOV		"ldo2_uvov"
#define TPS65224_IRQ_NAME_LDO3_UVOV		"ldo3_uvov"
#define TPS65224_IRQ_NAME_VCCA_UVOV		"vcca_uvov"
#define TPS65224_IRQ_NAME_VMON1_UVOV		"vmon1_uvov"
#define TPS65224_IRQ_NAME_VMON2_UVOV		"vmon2_uvov"
#define TPS65224_IRQ_NAME_GPIO1			"gpio1"
#define TPS65224_IRQ_NAME_GPIO2			"gpio2"
#define TPS65224_IRQ_NAME_GPIO3			"gpio3"
#define TPS65224_IRQ_NAME_GPIO4			"gpio4"
#define TPS65224_IRQ_NAME_GPIO5			"gpio5"
#define TPS65224_IRQ_NAME_GPIO6			"gpio6"
#define TPS65224_IRQ_NAME_VSENSE	        "vsense"
#define TPS65224_IRQ_NAME_ENABLE		"enable"
#define TPS65224_IRQ_NAME_PB_SHORT		"pb_short"
#define TPS65224_IRQ_NAME_FSD			"fsd"
#define TPS65224_IRQ_NAME_SOFT_REBOOT		"soft_reboot"
#define TPS65224_IRQ_NAME_BIST_PASS		"bist_pass"
#define TPS65224_IRQ_NAME_EXT_CLK		"ext_clk"
#define TPS65224_IRQ_NAME_REG_UNLOCK		"reg_unlock"
#define TPS65224_IRQ_NAME_TWARN			"twarn"
#define TPS65224_IRQ_NAME_PB_LONG		"pb_long"
#define TPS65224_IRQ_NAME_PB_FALL		"pb_fall"
#define TPS65224_IRQ_NAME_PB_RISE		"pb_rise"
#define TPS65224_IRQ_NAME_ADC_CONV_READY	"adc_conv_ready"
#define TPS65224_IRQ_NAME_TSD_ORD		"tsd_ord"
#define TPS65224_IRQ_NAME_BIST_FAIL		"bist_fail"
#define TPS65224_IRQ_NAME_REG_CRC_ERR		"reg_crc_err"
#define TPS65224_IRQ_NAME_RECOV_CNT		"recov_cnt"
#define TPS65224_IRQ_NAME_TSD_IMM		"tsd_imm"
#define TPS65224_IRQ_NAME_VCCA_OVP		"vcca_ovp"
#define TPS65224_IRQ_NAME_PFSM_ERR		"pfsm_err"
#define TPS65224_IRQ_NAME_BG_XMON		"bg_xmon"
#define TPS65224_IRQ_NAME_IMM_SHUTDOWN		"imm_shutdown"
#define TPS65224_IRQ_NAME_ORD_SHUTDOWN		"ord_shutdown"
#define TPS65224_IRQ_NAME_MCU_PWR_ERR		"mcu_pwr_err"
#define TPS65224_IRQ_NAME_SOC_PWR_ERR		"soc_pwr_err"
#define TPS65224_IRQ_NAME_COMM_ERR		"comm_err"
#define TPS65224_IRQ_NAME_I2C2_ERR		"i2c2_err"
#define TPS65224_IRQ_NAME_POWERUP		"powerup"

/**
 * struct tps6594 - device private data structure
 *
 * @dev:      MFD parent device
 * @chip_id:  chip ID
 * @reg:      I2C slave address or SPI chip select number
 * @use_crc:  if true, use CRC for I2C and SPI interface protocols
 * @regmap:   regmap for accessing the device registers
 * @irq:      irq generated by the device
 * @irq_data: regmap irq data used for the irq chip
 */
struct tps6594 {
	struct device *dev;
	unsigned long chip_id;
	unsigned short reg;
	bool use_crc;
	struct regmap *regmap;
	int irq;
	struct regmap_irq_chip_data *irq_data;
};

extern const struct regmap_access_table tps6594_volatile_table;
extern const struct regmap_access_table tps65224_volatile_table;

int tps6594_device_init(struct tps6594 *tps, bool enable_crc);

#endif /*  __LINUX_MFD_TPS6594_H */
