/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Analog Devices ADP5585 I/O expander, PWM controller and keypad controller
 *
 * Copyright 2022 NXP
 * Copyright 2024 Ideas on Board Oy
 */

#ifndef __MFD_ADP5585_H_
#define __MFD_ADP5585_H_

#include <linux/bits.h>
#include <linux/notifier.h>

#define ADP5585_ID			0x00
#define		ADP5585_MAN_ID_VALUE		0x20
#define		ADP5585_MAN_ID_MASK		GENMASK(7, 4)
#define		ADP5585_REV_ID_MASK		GENMASK(3, 0)
#define ADP5585_INT_STATUS		0x01
#define		ADP5585_OVRFLOW_INT		BIT(2)
#define		ADP5585_EVENT_INT		BIT(0)
#define ADP5585_STATUS			0x02
#define		ADP5585_EC_MASK			GENMASK(4, 0)
#define ADP5585_FIFO_1			0x03
#define		ADP5585_KEV_EV_PRESS_MASK	BIT(7)
#define		ADP5585_KEY_EVENT_MASK		GENMASK(6, 0)
#define ADP5585_FIFO_2			0x04
#define ADP5585_FIFO_3			0x05
#define ADP5585_FIFO_4			0x06
#define ADP5585_FIFO_5			0x07
#define ADP5585_FIFO_6			0x08
#define ADP5585_FIFO_7			0x09
#define ADP5585_FIFO_8			0x0a
#define ADP5585_FIFO_9			0x0b
#define ADP5585_FIFO_10			0x0c
#define ADP5585_FIFO_11			0x0d
#define ADP5585_FIFO_12			0x0e
#define ADP5585_FIFO_13			0x0f
#define ADP5585_FIFO_14			0x10
#define ADP5585_FIFO_15			0x11
#define ADP5585_FIFO_16			0x12
#define		ADP5585_EV_MAX			(ADP5585_FIFO_16 - ADP5585_FIFO_1 + 1)
#define ADP5585_GPI_INT_STAT_A		0x13
#define ADP5585_GPI_INT_STAT_B		0x14
#define ADP5585_GPI_STATUS_A		0x15
#define ADP5585_GPI_STATUS_B		0x16
#define ADP5585_RPULL_CONFIG_A		0x17
#define ADP5585_RPULL_CONFIG_B		0x18
#define ADP5585_RPULL_CONFIG_C		0x19
#define ADP5585_RPULL_CONFIG_D		0x1a
#define		ADP5585_Rx_PULL_CFG_PU_300K	0
#define		ADP5585_Rx_PULL_CFG_PD_300K	1
#define		ADP5585_Rx_PULL_CFG_PU_100K	2
#define		ADP5585_Rx_PULL_CFG_DISABLE	3
#define		ADP5585_Rx_PULL_CFG_MASK	3
#define ADP5585_GPI_INT_LEVEL_A		0x1b
#define ADP5585_GPI_INT_LEVEL_B		0x1c
#define ADP5585_GPI_EVENT_EN_A		0x1d
#define ADP5585_GPI_EVENT_EN_B		0x1e
#define ADP5585_GPI_INTERRUPT_EN_A	0x1f
#define ADP5585_GPI_INTERRUPT_EN_B	0x20
#define ADP5585_DEBOUNCE_DIS_A		0x21
#define ADP5585_DEBOUNCE_DIS_B		0x22
#define ADP5585_GPO_DATA_OUT_A		0x23
#define ADP5585_GPO_DATA_OUT_B		0x24
#define ADP5585_GPO_OUT_MODE_A		0x25
#define ADP5585_GPO_OUT_MODE_B		0x26
#define ADP5585_GPIO_DIRECTION_A	0x27
#define ADP5585_GPIO_DIRECTION_B	0x28
#define ADP5585_RESET1_EVENT_A		0x29
#define		ADP5585_RESET_EV_PRESS		BIT(7)
#define ADP5585_RESET1_EVENT_B		0x2a
#define ADP5585_RESET1_EVENT_C		0x2b
#define ADP5585_RESET2_EVENT_A		0x2c
#define ADP5585_RESET2_EVENT_B		0x2d
#define ADP5585_RESET_CFG		0x2e
#define ADP5585_PWM_OFFT_LOW		0x2f
#define ADP5585_PWM_OFFT_HIGH		0x30
#define ADP5585_PWM_ONT_LOW		0x31
#define ADP5585_PWM_ONT_HIGH		0x32
#define ADP5585_PWM_CFG			0x33
#define		ADP5585_PWM_IN_AND		BIT(2)
#define		ADP5585_PWM_MODE		BIT(1)
#define		ADP5585_PWM_EN			BIT(0)
#define ADP5585_LOGIC_CFG		0x34
#define ADP5585_LOGIC_FF_CFG		0x35
#define ADP5585_LOGIC_INT_EVENT_EN	0x36
#define ADP5585_POLL_PTIME_CFG		0x37
#define ADP5585_PIN_CONFIG_A		0x38
#define ADP5585_PIN_CONFIG_B		0x39
#define ADP5585_PIN_CONFIG_C		0x3a
#define		ADP5585_PULL_SELECT		BIT(7)
#define		ADP5585_C4_EXTEND_CFG_GPIO11	(0U << 6)
#define		ADP5585_C4_EXTEND_CFG_RESET2	(1U << 6)
#define		ADP5585_C4_EXTEND_CFG_MASK	GENMASK(6, 6)
#define		ADP5585_R4_EXTEND_CFG_GPIO5	(0U << 5)
#define		ADP5585_R4_EXTEND_CFG_RESET1	(1U << 5)
#define		ADP5585_R4_EXTEND_CFG_MASK	GENMASK(5, 5)
#define		ADP5585_R3_EXTEND_CFG_GPIO4	(0U << 2)
#define		ADP5585_R3_EXTEND_CFG_LC	(1U << 2)
#define		ADP5585_R3_EXTEND_CFG_PWM_OUT	(2U << 2)
#define		ADP5585_R3_EXTEND_CFG_MASK	GENMASK(3, 2)
#define		ADP5585_R0_EXTEND_CFG_GPIO1	(0U << 0)
#define		ADP5585_R0_EXTEND_CFG_LY	(1U << 0)
#define		ADP5585_R0_EXTEND_CFG_MASK	GENMASK(0, 0)
#define ADP5585_GENERAL_CFG		0x3b
#define		ADP5585_OSC_EN			BIT(7)
#define		ADP5585_OSC_FREQ_50KHZ		(0U << 5)
#define		ADP5585_OSC_FREQ_100KHZ		(1U << 5)
#define		ADP5585_OSC_FREQ_200KHZ		(2U << 5)
#define		ADP5585_OSC_FREQ_500KHZ		(3U << 5)
#define		ADP5585_OSC_FREQ_MASK		GENMASK(6, 5)
#define		ADP5585_INT_CFG			BIT(1)
#define		ADP5585_RST_CFG			BIT(0)
#define ADP5585_INT_EN			0x3c
#define		ADP5585_OVRFLOW_IEN		BIT(2)
#define		ADP5585_EVENT_IEN		BIT(0)

#define ADP5585_MAX_REG			ADP5585_INT_EN

#define ADP5585_PIN_MAX			11
#define ADP5585_MAX_UNLOCK_TIME_SEC	7
#define ADP5585_KEY_EVENT_START		1
#define ADP5585_KEY_EVENT_END		25
#define ADP5585_GPI_EVENT_START		37
#define ADP5585_GPI_EVENT_END		47
#define ADP5585_ROW5_KEY_EVENT_START	1
#define ADP5585_ROW5_KEY_EVENT_END	30
#define ADP5585_PWM_OUT			3
#define ADP5585_RESET1_OUT		4
#define ADP5585_RESET2_OUT		9
#define ADP5585_ROW5			5

/* ADP5589 */
#define		ADP5589_MAN_ID_VALUE		0x10
#define ADP5589_GPI_STATUS_A		0x16
#define ADP5589_GPI_STATUS_C		0x18
#define ADP5589_RPULL_CONFIG_A		0x19
#define ADP5589_GPI_INT_LEVEL_A		0x1e
#define ADP5589_GPI_EVENT_EN_A		0x21
#define ADP5589_DEBOUNCE_DIS_A		0x27
#define ADP5589_GPO_DATA_OUT_A		0x2a
#define ADP5589_GPO_OUT_MODE_A		0x2d
#define		ADP5589_GPIO_DIRECTION_A	0x30
#define ADP5589_UNLOCK1			0x33
#define		ADP5589_UNLOCK_EV_PRESS		BIT(7)
#define ADP5589_UNLOCK_TIMERS		0x36
#define		ADP5589_UNLOCK_TIMER		GENMASK(2, 0)
#define ADP5589_LOCK_CFG		0x37
#define		ADP5589_LOCK_EN			BIT(0)
#define ADP5589_RESET1_EVENT_A		0x38
#define ADP5589_RESET2_EVENT_A		0x3B
#define ADP5589_RESET_CFG		0x3D
#define		ADP5585_RESET2_POL		BIT(7)
#define		ADP5585_RESET1_POL		BIT(6)
#define		ADP5585_RST_PASSTHRU_EN		BIT(5)
#define		ADP5585_RESET_TRIG_TIME		GENMASK(4, 2)
#define		ADP5585_PULSE_WIDTH		GENMASK(1, 0)
#define ADP5589_PWM_OFFT_LOW		0x3e
#define ADP5589_PWM_ONT_LOW		0x40
#define ADP5589_PWM_CFG			0x42
#define ADP5589_POLL_PTIME_CFG		0x48
#define ADP5589_PIN_CONFIG_A		0x49
#define ADP5589_PIN_CONFIG_D		0x4C
#define ADP5589_GENERAL_CFG		0x4d
#define ADP5589_INT_EN			0x4e
#define ADP5589_MAX_REG			ADP5589_INT_EN

#define ADP5589_PIN_MAX			19
#define ADP5589_KEY_EVENT_START		1
#define ADP5589_KEY_EVENT_END		88
#define ADP5589_GPI_EVENT_START		97
#define ADP5589_GPI_EVENT_END		115
#define ADP5589_UNLOCK_WILDCARD		127
#define ADP5589_RESET2_OUT		12

struct regmap;

enum adp5585_variant {
	ADP5585_00 = 1,
	ADP5585_01,
	ADP5585_02,
	ADP5585_03,
	ADP5585_04,
	ADP5589_00,
	ADP5589_01,
	ADP5589_02,
	ADP5585_MAX
};

struct adp5585_regs {
	unsigned int gen_cfg;
	unsigned int ext_cfg;
	unsigned int int_en;
	unsigned int poll_ptime_cfg;
	unsigned int reset_cfg;
	unsigned int reset1_event_a;
	unsigned int reset2_event_a;
	unsigned int pin_cfg_a;
};

struct adp5585_dev {
	struct device *dev;
	struct regmap *regmap;
	const struct adp5585_regs *regs;
	struct blocking_notifier_head event_notifier;
	unsigned long *pin_usage;
	unsigned int n_pins;
	unsigned int reset2_out;
	enum adp5585_variant variant;
	unsigned int id;
	bool has_unlock;
	bool has_pin6;
	int irq;
	unsigned int ev_poll_time;
	unsigned int unlock_time;
	unsigned int unlock_keys[2];
	unsigned int nkeys_unlock;
	unsigned int reset1_keys[3];
	unsigned int nkeys_reset1;
	unsigned int reset2_keys[2];
	unsigned int nkeys_reset2;
	u8 reset_cfg;
};

#endif
