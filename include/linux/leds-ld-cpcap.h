/*
 * Copyright (C) 2009 Motorola, Inc.
 *
 * This program is free dispware; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free dispware Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free dispware
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#ifndef __LED_LD_CPCAP_H__
#define __LED_LD_CPCAP_H__

#ifdef __KERNEL__

#define LD_CPCAP_LED_DRV "cpcap_led_driver"

#define LD_DISP_BUTTON_DEV "button-backlight"
#define LD_PRIVACY_LED_DEV "privacy-led"
#define LD_NOTIF_LED_DEV "notification-backlight"

struct cpcap_display_led {
	unsigned int display_reg;
	unsigned int display_mask;
	unsigned int display_on;
	unsigned int display_off;
	unsigned int display_init;
	unsigned int poll_intvl;
	unsigned int zone0;
	unsigned int zone1;
	unsigned int zone2;
	unsigned int zone3;
	unsigned int zone4;
};

struct cpcap_led {
	u8 blink_able;
	unsigned int cpcap_register;
	unsigned int cpcap_mask;
	unsigned int on_val;
	unsigned int off_val;
	unsigned int cpcap_duty_cycle;
	unsigned int cpcap_current;
	char *class_name;
	char *led_regulator;
};

#endif  /* __KERNEL__ */
#endif  /* __LED_LD_CPCAP_H__ */
