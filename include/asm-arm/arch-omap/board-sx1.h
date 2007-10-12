/*
 * Siemens SX1 board definitions
 *
 * Copyright: Vovan888 at gmail com
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef __ASM_ARCH_SX1_I2C_CHIPS_H
#define __ASM_ARCH_SX1_I2C_CHIPS_H

#define SOFIA_MAX_LIGHT_VAL	0x2B

#define SOFIA_I2C_ADDR		0x32
/* Sofia reg 3 bits masks */
#define SOFIA_POWER1_REG	0x03

#define	SOFIA_USB_POWER		0x01
#define	SOFIA_MMC_POWER		0x04
#define	SOFIA_BLUETOOTH_POWER	0x08
#define	SOFIA_MMILIGHT_POWER	0x20

#define SOFIA_POWER2_REG	0x04
#define SOFIA_BACKLIGHT_REG	0x06
#define SOFIA_KEYLIGHT_REG	0x07
#define SOFIA_DIMMING_REG	0x09


/* Function Prototypes for SX1 devices control on I2C bus */

int sx1_setbacklight(u8 backlight);
int sx1_getbacklight(u8 *backlight);
int sx1_setkeylight(u8 keylight);
int sx1_getkeylight(u8 *keylight);

int sx1_setmmipower(u8 onoff);
int sx1_setusbpower(u8 onoff);
int sx1_setmmcpower(u8 onoff);

#endif /* __ASM_ARCH_SX1_I2C_CHIPS_H */
