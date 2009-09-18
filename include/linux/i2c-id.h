/* ------------------------------------------------------------------------- */
/*									     */
/* i2c-id.h - identifier values for i2c drivers and adapters		     */
/*									     */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-1999 Simon G. Vogl

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.		     */
/* ------------------------------------------------------------------------- */

#ifndef LINUX_I2C_ID_H
#define LINUX_I2C_ID_H

/* Please note that I2C driver IDs are optional. They are only needed if a
   legacy chip driver needs to identify a bus or a bus driver needs to
   identify a legacy client. If you don't need them, just don't set them. */

/*
 * ---- Adapter types ----------------------------------------------------
 */

/* --- Bit algorithm adapters						*/
#define I2C_HW_B_BT848		0x010005 /* BT848 video boards */
#define I2C_HW_B_RIVA		0x010010 /* Riva based graphics cards */
#define I2C_HW_B_ZR36067	0x010019 /* Zoran-36057/36067 based boards */
#define I2C_HW_B_CX2388x	0x01001b /* connexant 2388x based tv cards */
#define I2C_HW_B_EM28XX		0x01001f /* em28xx video capture cards */
#define I2C_HW_B_CX2341X	0x010020 /* Conexant CX2341X MPEG encoder cards */
#define I2C_HW_B_CX23885	0x010022 /* conexant 23885 based tv cards (bus1) */
#define I2C_HW_B_AU0828		0x010023 /* auvitek au0828 usb bridge */
#define I2C_HW_B_CX231XX	0x010024 /* Conexant CX231XX USB based cards */
#define I2C_HW_B_HDPVR		0x010025 /* Hauppauge HD PVR */

/* --- SGI adapters							*/
#define I2C_HW_SGI_VINO		0x160000

/* --- SMBus only adapters						*/
#define I2C_HW_SMBUS_W9968CF	0x04000d
#define I2C_HW_SMBUS_OV511	0x04000e /* OV511(+) USB 1.1 webcam ICs */
#define I2C_HW_SMBUS_OV518	0x04000f /* OV518(+) USB 1.1 webcam ICs */
#define I2C_HW_SMBUS_CAFE	0x040012 /* Marvell 88ALP01 "CAFE" cam  */

/* --- Miscellaneous adapters */
#define I2C_HW_SAA7146		0x060000 /* SAA7146 video decoder bus */
#define I2C_HW_SAA7134		0x090000 /* SAA7134 video decoder bus */

#endif /* LINUX_I2C_ID_H */
