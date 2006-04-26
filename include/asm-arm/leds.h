/*
 *  linux/include/asm-arm/leds.h
 *
 *  Copyright (C) 1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Event-driven interface for LEDs on machines
 *  Added led_start and led_stop- Alex Holden, 28th Dec 1998.
 */
#ifndef ASM_ARM_LEDS_H
#define ASM_ARM_LEDS_H


typedef enum {
	led_idle_start,
	led_idle_end,
	led_timer,
	led_start,
	led_stop,
	led_claim,		/* override idle & timer leds */
	led_release,		/* restore idle & timer leds */
	led_start_timer_mode,
	led_stop_timer_mode,
	led_green_on,
	led_green_off,
	led_amber_on,
	led_amber_off,
	led_red_on,
	led_red_off,
	led_blue_on,
	led_blue_off,
	/*
	 * I want this between led_timer and led_start, but
	 * someone has decided to export this to user space
	 */
	led_halted
} led_event_t;

/* Use this routine to handle LEDs */

#ifdef CONFIG_LEDS
extern void (*leds_event)(led_event_t);
#else
#define leds_event(e)
#endif

#endif
