/*
 * include/asm-arm/arch-sa1100/cerf.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Apr-2003 : Removed some old PDA crud [FB]
 */
#ifndef _INCLUDE_CERF_H_
#define _INCLUDE_CERF_H_

#include <linux/config.h>

#define CERF_ETH_IO			0xf0000000
#define CERF_ETH_IRQ IRQ_GPIO26

#define CERF_GPIO_CF_BVD2		GPIO_GPIO (19)
#define CERF_GPIO_CF_BVD1		GPIO_GPIO (20)
#define CERF_GPIO_CF_RESET		GPIO_GPIO (21)
#define CERF_GPIO_CF_IRQ		GPIO_GPIO (22)
#define CERF_GPIO_CF_CD			GPIO_GPIO (23)

#define CERF_IRQ_GPIO_CF_BVD2		IRQ_GPIO19
#define CERF_IRQ_GPIO_CF_BVD1		IRQ_GPIO20
#define CERF_IRQ_GPIO_CF_IRQ		IRQ_GPIO22
#define CERF_IRQ_GPIO_CF_CD		IRQ_GPIO23

#endif // _INCLUDE_CERF_H_
