/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#ifndef _LINUX_LED_CPCAP_LM3559_H__
#define _LINUX_LED_CPCAP_LM3559_H__

#define LM3559_LED_DEV "torch-flash"
#define LM3559_LED_SPOTLIGHT "spotlight"

#define LM3559_NAME "lm3559_led"



#ifdef __KERNEL__

#define LED_RED                  0x01
#define LED_GREEN                0x02
#define LED_BLUE                 0x04

#define LM3559_ERROR_CHECK	(1 << 0)
#define LM3559_PRIVACY		(1 << 1)
#define LM3559_TORCH		(1 << 2)
#define LM3559_FLASH		(1 << 3)
#define LM3559_FLASH_LIGHT	(1 << 4)
#define LM3559_MSG_IND_RED	(1 << 5)
#define LM3559_MSG_IND_GREEN	(1 << 6)
#define LM3559_MSG_IND_BLUE	(1 << 7)


struct lm3559_platform_data {
	uint32_t flags;
	u8 enable_reg_def;
	u8 gpio_reg_def;
	u8 adc_delay_reg_def;
	u8 vin_monitor_def;
	u8 torch_brightness_def;
	u8 flash_brightness_def;
	u8 flash_duration_def;
	u8 flag_reg_def;
	u8 config_reg_1_def;
	u8 config_reg_2_def;
	u8 privacy_reg_def;
	u8 msg_ind_reg_def;
	u8 msg_ind_blink_reg_def;
	u8 pwm_reg_def;
	u8 torch_enable_val;
	u8 flash_enable_val;
	u8 privacy_enable_val;
	u8 pwm_val;
	u8 msg_ind_val;
	u8 msg_ind_blink_val;
} __attribute__ ((packed));

#endif	/* __KERNEL__ */

#endif	/* _LINUX_LED_CPCAP_LM3559_H__ */
