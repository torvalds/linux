/*
 * Macros for vr4181 IRQ numbers.
 *
 * Copyright (C) 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

/*
 * Strategy:
 *
 * Vr4181 has conceptually three levels of interrupt controllers:
 *  1. the CPU itself with 8 intr level.
 *  2. system interrupt controller, cascaded from int0 pin in CPU, 32 intrs
 *  3. GPIO interrupts : forwarding external interrupts to sys intr controller
 */

/* decide the irq block assignment */
#define	VR4181_NUM_CPU_IRQ	8
#define	VR4181_NUM_SYS_IRQ	32
#define	VR4181_NUM_GPIO_IRQ	16

#define	VR4181_IRQ_BASE		0

#define	VR4181_CPU_IRQ_BASE	VR4181_IRQ_BASE
#define	VR4181_SYS_IRQ_BASE	(VR4181_CPU_IRQ_BASE + VR4181_NUM_CPU_IRQ)
#define	VR4181_GPIO_IRQ_BASE	(VR4181_SYS_IRQ_BASE + VR4181_NUM_SYS_IRQ)

/* CPU interrupts */

/*
   IP0 - Software interrupt
   IP1 - Software interrupt
   IP2 - All but battery, high speed modem, and real time clock
   IP3 - RTC Long1 (system timer)
   IP4 - RTC Long2
   IP5 - High Speed Modem (unused on VR4181)
   IP6 - Unused
   IP7 - Timer interrupt from CPO_COMPARE
*/

#define VR4181_IRQ_SW1       (VR4181_CPU_IRQ_BASE + 0)
#define VR4181_IRQ_SW2       (VR4181_CPU_IRQ_BASE + 1)
#define VR4181_IRQ_INT0      (VR4181_CPU_IRQ_BASE + 2)
#define VR4181_IRQ_INT1      (VR4181_CPU_IRQ_BASE + 3)
#define VR4181_IRQ_INT2      (VR4181_CPU_IRQ_BASE + 4)
#define VR4181_IRQ_INT3      (VR4181_CPU_IRQ_BASE + 5)
#define VR4181_IRQ_INT4      (VR4181_CPU_IRQ_BASE + 6)
#define VR4181_IRQ_TIMER     (VR4181_CPU_IRQ_BASE + 7)


/* Cascaded from VR4181_IRQ_INT0 (ICU mapped interrupts) */

/*
   IP2 - same as VR4181_IRQ_INT1
   IP8 - This is a cascade to GPIO IRQ's. Do not use.
   IP16 - same as VR4181_IRQ_INT2
   IP18 - CompactFlash
*/

#define VR4181_IRQ_BATTERY   (VR4181_SYS_IRQ_BASE + 0)
#define VR4181_IRQ_POWER     (VR4181_SYS_IRQ_BASE + 1)
#define VR4181_IRQ_RTCL1     (VR4181_SYS_IRQ_BASE + 2)
#define VR4181_IRQ_ETIMER    (VR4181_SYS_IRQ_BASE + 3)
#define VR4181_IRQ_RFU12     (VR4181_SYS_IRQ_BASE + 4)
#define VR4181_IRQ_PIU       (VR4181_SYS_IRQ_BASE + 5)
#define VR4181_IRQ_AIU       (VR4181_SYS_IRQ_BASE + 6)
#define VR4181_IRQ_KIU       (VR4181_SYS_IRQ_BASE + 7)
#define VR4181_IRQ_GIU       (VR4181_SYS_IRQ_BASE + 8)
#define VR4181_IRQ_SIU       (VR4181_SYS_IRQ_BASE + 9)
#define VR4181_IRQ_RFU18     (VR4181_SYS_IRQ_BASE + 10)
#define VR4181_IRQ_SOFT      (VR4181_SYS_IRQ_BASE + 11)
#define VR4181_IRQ_RFU20     (VR4181_SYS_IRQ_BASE + 12)
#define VR4181_IRQ_DOZEPIU   (VR4181_SYS_IRQ_BASE + 13)
#define VR4181_IRQ_RFU22     (VR4181_SYS_IRQ_BASE + 14)
#define VR4181_IRQ_RFU23     (VR4181_SYS_IRQ_BASE + 15)
#define VR4181_IRQ_RTCL2     (VR4181_SYS_IRQ_BASE + 16)
#define VR4181_IRQ_LED       (VR4181_SYS_IRQ_BASE + 17)
#define VR4181_IRQ_ECU       (VR4181_SYS_IRQ_BASE + 18)
#define VR4181_IRQ_CSU       (VR4181_SYS_IRQ_BASE + 19)
#define VR4181_IRQ_USB       (VR4181_SYS_IRQ_BASE + 20)
#define VR4181_IRQ_DMA       (VR4181_SYS_IRQ_BASE + 21)
#define VR4181_IRQ_LCD       (VR4181_SYS_IRQ_BASE + 22)
#define VR4181_IRQ_RFU31     (VR4181_SYS_IRQ_BASE + 23)
#define VR4181_IRQ_RFU32     (VR4181_SYS_IRQ_BASE + 24)
#define VR4181_IRQ_RFU33     (VR4181_SYS_IRQ_BASE + 25)
#define VR4181_IRQ_RFU34     (VR4181_SYS_IRQ_BASE + 26)
#define VR4181_IRQ_RFU35     (VR4181_SYS_IRQ_BASE + 27)
#define VR4181_IRQ_RFU36     (VR4181_SYS_IRQ_BASE + 28)
#define VR4181_IRQ_RFU37     (VR4181_SYS_IRQ_BASE + 29)
#define VR4181_IRQ_RFU38     (VR4181_SYS_IRQ_BASE + 30)
#define VR4181_IRQ_RFU39     (VR4181_SYS_IRQ_BASE + 31)

/* Cascaded from VR4181_IRQ_GIU */
#define VR4181_IRQ_GPIO0     (VR4181_GPIO_IRQ_BASE + 0)
#define VR4181_IRQ_GPIO1     (VR4181_GPIO_IRQ_BASE + 1)
#define VR4181_IRQ_GPIO2     (VR4181_GPIO_IRQ_BASE + 2)
#define VR4181_IRQ_GPIO3     (VR4181_GPIO_IRQ_BASE + 3)
#define VR4181_IRQ_GPIO4     (VR4181_GPIO_IRQ_BASE + 4)
#define VR4181_IRQ_GPIO5     (VR4181_GPIO_IRQ_BASE + 5)
#define VR4181_IRQ_GPIO6     (VR4181_GPIO_IRQ_BASE + 6)
#define VR4181_IRQ_GPIO7     (VR4181_GPIO_IRQ_BASE + 7)
#define VR4181_IRQ_GPIO8     (VR4181_GPIO_IRQ_BASE + 8)
#define VR4181_IRQ_GPIO9     (VR4181_GPIO_IRQ_BASE + 9)
#define VR4181_IRQ_GPIO10    (VR4181_GPIO_IRQ_BASE + 10)
#define VR4181_IRQ_GPIO11    (VR4181_GPIO_IRQ_BASE + 11)
#define VR4181_IRQ_GPIO12    (VR4181_GPIO_IRQ_BASE + 12)
#define VR4181_IRQ_GPIO13    (VR4181_GPIO_IRQ_BASE + 13)
#define VR4181_IRQ_GPIO14    (VR4181_GPIO_IRQ_BASE + 14)
#define VR4181_IRQ_GPIO15    (VR4181_GPIO_IRQ_BASE + 15)


// Alternative to above GPIO IRQ defines
#define VR4181_IRQ_GPIO(pin) ((VR4181_IRQ_GPIO0) + (pin))

#define VR4181_IRQ_MAX       (VR4181_IRQ_BASE + VR4181_NUM_CPU_IRQ + \
                              VR4181_NUM_SYS_IRQ + VR4181_NUM_GPIO_IRQ)
