/*
 *  linux/include/asm-arm/arch-aaec2000/aaed2000.h
 *
 *  AAED-2000 specific bits definition
 *
 *  Copyright (c) 2005 Nicolas Bellido Y Ortega
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_AAED2000_H
#define __ASM_ARCH_AAED2000_H

/* External GPIOs. */

#define EXT_GPIO_PBASE	AAEC_CS3
#define EXT_GPIO_VBASE	0xf8100000
#define EXT_GPIO_LENGTH	0x00001000

#define __ext_gpio_p2v(x)	((x) - EXT_GPIO_PBASE + EXT_GPIO_VBASE)
#define __ext_gpio_v2p(x)	((x) + EXT_GPIO_PBASE - EXT_GPIO_VBASE)

#define __EXT_GPIO_REG(x)	(*((volatile u32 *)__ext_gpio_p2v(x)))
#define __EXT_GPIO_PREG(x)	(__ext_gpio_v2p((u32)&(x)))

#define AAED_EXT_GPIO	__EXT_GPIO_REG(EXT_GPIO_PBASE)

#define AAED_EGPIO_KBD_SCAN	0x00003fff /* Keyboard scan data */
#define AAED_EGPIO_PWR_INT	0x00008fff /* Smart battery charger interrupt */
#define AAED_EGPIO_SWITCHED	0x000f0000 /* DIP Switches */
#define AAED_EGPIO_USB_VBUS	0x00400000 /* USB Vbus sense */
#define AAED_EGPIO_LCD_PWR_EN	0x02000000 /* LCD and backlight PWR enable */
#define AAED_EGPIO_nLED0	0x20000000 /* LED 0 */
#define AAED_EGPIO_nLED1	0x20000000 /* LED 1 */
#define AAED_EGPIO_nLED2	0x20000000 /* LED 2 */


#endif /* __ARM_ARCH_AAED2000_H */
