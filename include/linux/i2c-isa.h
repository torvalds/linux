/*
 * i2c-isa.h - definitions for the i2c-isa pseudo-i2c-adapter interface
 *
 * Copyright (C) 2005 Jean Delvare <khali@linux-fr.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _LINUX_I2C_ISA_H
#define _LINUX_I2C_ISA_H

#include <linux/i2c.h>

extern int i2c_isa_add_driver(struct i2c_driver *driver);
extern int i2c_isa_del_driver(struct i2c_driver *driver);

/* Detect whether we are on the isa bus. This is only useful to hybrid
   (i2c+isa) drivers. */
#define i2c_is_isa_adapter(adapptr) \
        ((adapptr)->id == I2C_HW_ISA)
#define i2c_is_isa_client(clientptr) \
        i2c_is_isa_adapter((clientptr)->adapter)

#endif /* _LINUX_I2C_ISA_H */
