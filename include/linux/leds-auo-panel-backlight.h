/*
 * Copyright (C) 2009 Motorola, Inc.
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

#ifndef _LINUX_LED_LD_AUO_PANEL_BL_H__
#define _LINUX_LED_LD_AUO_PANEL_BL_H__

#define	MANUAL		0
#define	AUTOMATIC	1
#define	MANUAL_SENSOR	2


#define LD_AUO_PANEL_BL_LED_DEV "lcd-backlight"

#define LD_AUO_PANEL_BL_NAME "auo_panel_bl_led"

#ifdef __KERNEL__
struct auo_panel_bl_platform_data {
	void (*bl_enable) (void);
	void (*bl_disable) (void);
	void (*pwm_enable) (void);
	void (*pwm_disable) (void);
};

#endif	/* __KERNEL__ */
#endif	/* _LINUX_LED_LD_AUO_PANEL_BL_H__ */
