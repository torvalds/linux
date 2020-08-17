/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Khadas System control Microcontroller Register map
 *
 * Copyright (C) 2020 BayLibre SAS
 *
 * Author(s): Neil Armstrong <narmstrong@baylibre.com>
 */

#ifndef MFD_KHADAS_MCU_H
#define MFD_KHADAS_MCU_H

#define KHADAS_MCU_PASSWD_VEN_0_REG		0x00 /* RO */
#define KHADAS_MCU_PASSWD_VEN_1_REG		0x01 /* RO */
#define KHADAS_MCU_PASSWD_VEN_2_REG		0x02 /* RO */
#define KHADAS_MCU_PASSWD_VEN_3_REG		0x03 /* RO */
#define KHADAS_MCU_PASSWD_VEN_4_REG		0x04 /* RO */
#define KHADAS_MCU_PASSWD_VEN_5_REG		0x05 /* RO */
#define KHADAS_MCU_MAC_0_REG			0x06 /* RO */
#define KHADAS_MCU_MAC_1_REG			0x07 /* RO */
#define KHADAS_MCU_MAC_2_REG			0x08 /* RO */
#define KHADAS_MCU_MAC_3_REG			0x09 /* RO */
#define KHADAS_MCU_MAC_4_REG			0x0a /* RO */
#define KHADAS_MCU_MAC_5_REG			0x0b /* RO */
#define KHADAS_MCU_USID_0_REG			0x0c /* RO */
#define KHADAS_MCU_USID_1_REG			0x0d /* RO */
#define KHADAS_MCU_USID_2_REG			0x0e /* RO */
#define KHADAS_MCU_USID_3_REG			0x0f /* RO */
#define KHADAS_MCU_USID_4_REG			0x10 /* RO */
#define KHADAS_MCU_USID_5_REG			0x11 /* RO */
#define KHADAS_MCU_VERSION_0_REG		0x12 /* RO */
#define KHADAS_MCU_VERSION_1_REG		0x13 /* RO */
#define KHADAS_MCU_DEVICE_NO_0_REG		0x14 /* RO */
#define KHADAS_MCU_DEVICE_NO_1_REG		0x15 /* RO */
#define KHADAS_MCU_FACTORY_TEST_REG		0x16 /* R */
#define KHADAS_MCU_BOOT_MODE_REG		0x20 /* RW */
#define KHADAS_MCU_BOOT_EN_WOL_REG		0x21 /* RW */
#define KHADAS_MCU_BOOT_EN_RTC_REG		0x22 /* RW */
#define KHADAS_MCU_BOOT_EN_EXP_REG		0x23 /* RW */
#define KHADAS_MCU_BOOT_EN_IR_REG		0x24 /* RW */
#define KHADAS_MCU_BOOT_EN_DCIN_REG		0x25 /* RW */
#define KHADAS_MCU_BOOT_EN_KEY_REG		0x26 /* RW */
#define KHADAS_MCU_KEY_MODE_REG			0x27 /* RW */
#define KHADAS_MCU_LED_MODE_ON_REG		0x28 /* RW */
#define KHADAS_MCU_LED_MODE_OFF_REG		0x29 /* RW */
#define KHADAS_MCU_SHUTDOWN_NORMAL_REG		0x2c /* RW */
#define KHADAS_MCU_MAC_SWITCH_REG		0x2d /* RW */
#define KHADAS_MCU_MCU_SLEEP_MODE_REG		0x2e /* RW */
#define KHADAS_MCU_IR_CODE1_0_REG		0x2f /* RW */
#define KHADAS_MCU_IR_CODE1_1_REG		0x30 /* RW */
#define KHADAS_MCU_IR_CODE1_2_REG		0x31 /* RW */
#define KHADAS_MCU_IR_CODE1_3_REG		0x32 /* RW */
#define KHADAS_MCU_USB_PCIE_SWITCH_REG		0x33 /* RW */
#define KHADAS_MCU_IR_CODE2_0_REG		0x34 /* RW */
#define KHADAS_MCU_IR_CODE2_1_REG		0x35 /* RW */
#define KHADAS_MCU_IR_CODE2_2_REG		0x36 /* RW */
#define KHADAS_MCU_IR_CODE2_3_REG		0x37 /* RW */
#define KHADAS_MCU_PASSWD_USER_0_REG		0x40 /* RW */
#define KHADAS_MCU_PASSWD_USER_1_REG		0x41 /* RW */
#define KHADAS_MCU_PASSWD_USER_2_REG		0x42 /* RW */
#define KHADAS_MCU_PASSWD_USER_3_REG		0x43 /* RW */
#define KHADAS_MCU_PASSWD_USER_4_REG		0x44 /* RW */
#define KHADAS_MCU_PASSWD_USER_5_REG		0x45 /* RW */
#define KHADAS_MCU_USER_DATA_0_REG		0x46 /* RW 56 bytes */
#define KHADAS_MCU_PWR_OFF_CMD_REG		0x80 /* WO */
#define KHADAS_MCU_PASSWD_START_REG		0x81 /* WO */
#define KHADAS_MCU_CHECK_VEN_PASSWD_REG		0x82 /* WO */
#define KHADAS_MCU_CHECK_USER_PASSWD_REG	0x83 /* WO */
#define KHADAS_MCU_SHUTDOWN_NORMAL_STATUS_REG	0x86 /* RO */
#define KHADAS_MCU_WOL_INIT_START_REG		0x87 /* WO */
#define KHADAS_MCU_CMD_FAN_STATUS_CTRL_REG	0x88 /* WO */

enum {
	KHADAS_BOARD_VIM1 = 0x1,
	KHADAS_BOARD_VIM2,
	KHADAS_BOARD_VIM3,
	KHADAS_BOARD_EDGE = 0x11,
	KHADAS_BOARD_EDGE_V,
};

/**
 * struct khadas_mcu - Khadas MCU structure
 * @device:		device reference used for logs
 * @regmap:		register map
 */
struct khadas_mcu {
	struct device *dev;
	struct regmap *regmap;
};

#endif /* MFD_KHADAS_MCU_H */
