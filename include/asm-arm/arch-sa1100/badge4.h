/*
 * linux/include/asm-arm/arch-sa1100/badge4.h
 *
 *   Tim Connors <connors@hpl.hp.com>
 *   Christopher Hoover <ch@hpl.hp.com>
 *
 * Copyright (C) 2002 Hewlett-Packard Company
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __ASM_ARCH_HARDWARE_H
#error "include <asm/hardware.h> instead"
#endif

#define BADGE4_SA1111_BASE		(0x48000000)

/* GPIOs on the BadgePAD 4 */
#define BADGE4_GPIO_INT_1111		GPIO_GPIO0   /* SA-1111 IRQ */

#define BADGE4_GPIO_INT_VID		GPIO_GPIO1   /* Video expansion */
#define BADGE4_GPIO_LGP2		GPIO_GPIO2   /* GPIO_LDD8 */
#define BADGE4_GPIO_LGP3		GPIO_GPIO3   /* GPIO_LDD9 */
#define BADGE4_GPIO_LGP4		GPIO_GPIO4   /* GPIO_LDD10 */
#define BADGE4_GPIO_LGP5		GPIO_GPIO5   /* GPIO_LDD11 */
#define BADGE4_GPIO_LGP6		GPIO_GPIO6   /* GPIO_LDD12 */
#define BADGE4_GPIO_LGP7		GPIO_GPIO7   /* GPIO_LDD13 */
#define BADGE4_GPIO_LGP8		GPIO_GPIO8   /* GPIO_LDD14 */
#define BADGE4_GPIO_LGP9		GPIO_GPIO9   /* GPIO_LDD15 */
#define BADGE4_GPIO_GPA_VID		GPIO_GPIO10  /* Video expansion */
#define BADGE4_GPIO_GPB_VID		GPIO_GPIO11  /* Video expansion */
#define BADGE4_GPIO_GPC_VID		GPIO_GPIO12  /* Video expansion */

#define BADGE4_GPIO_UART_HS1		GPIO_GPIO13
#define BADGE4_GPIO_UART_HS2		GPIO_GPIO14

#define BADGE4_GPIO_MUXSEL0		GPIO_GPIO15
#define BADGE4_GPIO_TESTPT_J7		GPIO_GPIO16

#define BADGE4_GPIO_SDSDA		GPIO_GPIO17  /* SDRAM SPD Data */
#define BADGE4_GPIO_SDSCL		GPIO_GPIO18  /* SDRAM SPD Clock */
#define BADGE4_GPIO_SDTYP0		GPIO_GPIO19  /* SDRAM Type Control */
#define BADGE4_GPIO_SDTYP1		GPIO_GPIO20  /* SDRAM Type Control */

#define BADGE4_GPIO_BGNT_1111		GPIO_GPIO21  /* GPIO_MBGNT */
#define BADGE4_GPIO_BREQ_1111		GPIO_GPIO22  /* GPIO_TREQA */

#define BADGE4_GPIO_TESTPT_J6		GPIO_GPIO23

#define BADGE4_GPIO_PCMEN5V		GPIO_GPIO24  /* 5V power */

#define BADGE4_GPIO_SA1111_NRST		GPIO_GPIO25  /* SA-1111 nRESET */

#define BADGE4_GPIO_TESTPT_J5		GPIO_GPIO26

#define BADGE4_GPIO_CLK_1111		GPIO_GPIO27  /* GPIO_32_768kHz */

/* Interrupts on the BadgePAD 4 */
#define BADGE4_IRQ_GPIO_SA1111		IRQ_GPIO0    /* SA-1111 interrupt */


/* PCM5ENV Usage tracking */

#define BADGE4_5V_PCMCIA_SOCK0		(1<<0)
#define BADGE4_5V_PCMCIA_SOCK1		(1<<1)
#define BADGE4_5V_PCMCIA_SOCK(n)	(1<<(n))
#define BADGE4_5V_USB			(1<<2)
#define BADGE4_5V_INITIALLY		(1<<3)

#ifndef __ASSEMBLY__
extern void badge4_set_5V(unsigned subsystem, int on);
#endif
