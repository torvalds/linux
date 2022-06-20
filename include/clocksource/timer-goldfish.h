/* SPDX-License-Identifier: GPL-2.0 */
/*
 * goldfish-timer clocksource
 * Registers definition for the goldfish-timer device
 */

#ifndef _CLOCKSOURCE_TIMER_GOLDFISH_H
#define _CLOCKSOURCE_TIMER_GOLDFISH_H

/*
 * TIMER_TIME_LOW	 get low bits of current time and update TIMER_TIME_HIGH
 * TIMER_TIME_HIGH	 get high bits of time at last TIMER_TIME_LOW read
 * TIMER_ALARM_LOW	 set low bits of alarm and activate it
 * TIMER_ALARM_HIGH	 set high bits of next alarm
 * TIMER_IRQ_ENABLED	 enable alarm interrupt
 * TIMER_CLEAR_ALARM	 disarm an existing alarm
 * TIMER_ALARM_STATUS	 alarm status (running or not)
 * TIMER_CLEAR_INTERRUPT clear interrupt
 */
#define TIMER_TIME_LOW		0x00
#define TIMER_TIME_HIGH		0x04
#define TIMER_ALARM_LOW		0x08
#define TIMER_ALARM_HIGH	0x0c
#define TIMER_IRQ_ENABLED	0x10
#define TIMER_CLEAR_ALARM	0x14
#define TIMER_ALARM_STATUS	0x18
#define TIMER_CLEAR_INTERRUPT	0x1c

extern int goldfish_timer_init(int irq, void __iomem *base);

#endif /* _CLOCKSOURCE_TIMER_GOLDFISH_H */
