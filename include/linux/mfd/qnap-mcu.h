/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Core definitions for QNAP MCU MFD driver.
 * Copyright (C) 2024 Heiko Stuebner <heiko@sntech.de>
 */

#ifndef _LINUX_QNAP_MCU_H_
#define _LINUX_QNAP_MCU_H_

struct qnap_mcu;

struct qnap_mcu_variant {
	u32 baud_rate;
	int num_drives;
	int fan_pwm_min;
	int fan_pwm_max;
	bool usb_led;
};

int qnap_mcu_exec(struct qnap_mcu *mcu,
		  const u8 *cmd_data, size_t cmd_data_size,
		  u8 *reply_data, size_t reply_data_size);
int qnap_mcu_exec_with_ack(struct qnap_mcu *mcu,
			   const u8 *cmd_data, size_t cmd_data_size);

#endif /* _LINUX_QNAP_MCU_H_ */
