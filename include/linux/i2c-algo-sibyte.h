/*
 * Copyright (C) 2001,2002,2003 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef I2C_ALGO_SIBYTE_H
#define I2C_ALGO_SIBYTE_H 1

#include <linux/i2c.h>

struct i2c_algo_sibyte_data {
	void *data;		/* private data */
        int   bus;		/* which bus */
        void *reg_base;		/* CSR base */
};

int i2c_sibyte_add_bus(struct i2c_adapter *, int speed);
int i2c_sibyte_del_bus(struct i2c_adapter *);

#endif /* I2C_ALGO_SIBYTE_H */
