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

#define LM3559_FLAG_ERROR_CHECK	0x01

struct lm3559_platform_data {
	uint32_t flags;
	u8 flash_duration_def;
	u8 vin_monitor_def;
} __attribute__ ((packed));

#endif	/* __KERNEL__ */

#endif	/* _LINUX_LED_CPCAP_LM3559_H__ */
