/*
 * TV Panel
 * arch/arm/mach-meson6tv/include/mach/panel.h
 *
 * Author: Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2013 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MACH_MESON6TV_PANEL_H
#define __MACH_MESON6TV_PANEL_H

/*
 * NOTES: the following function or variable must be defined
 *        in board-xxx-panel.c
 *
 */

#define PANEL_POWER_ON		1
#define PANEL_POWER_OFF		0

#define BL_POWER_ON		0
#define BL_POWER_OFF		1

#define LED_LIGHT_ON 	0
#define LED_LIGHT_OFF 	1


extern unsigned int panel_power_on_delay;
extern unsigned int panel_power_off_delay;
extern unsigned int backlight_power_on_delay;
extern unsigned int backlight_power_off_delay;

extern unsigned int clock_disable_delay;
extern unsigned int clock_enable_delay;

extern unsigned int pwm_enable_delay;
extern unsigned int pwm_disable_delay;

extern void panel_power_on(void);
extern void panel_power_off(void);
extern void backlight_power_on(void);
extern void backlight_power_off(void);

extern void pwm_enable(void);
extern void pwm_disable(void);
extern void led_light_off(void);
extern void led_light_on(void);



#endif //__MACH_MESON6TV_PANEL_H

