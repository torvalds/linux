/* ------------------------------------------------------------------------- */
/* i2c-algo-ite.h i2c driver algorithms for ITE IIC adapters                 */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-97 Simon G. Vogl
                   1998-99 Hans Berglund

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
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.                */
/* ------------------------------------------------------------------------- */

/* With some changes from Kyösti Mälkki <kmalkki@cc.hut.fi> and even
   Frodo Looijaard <frodol@dds.nl> */

/* Modifications by MontaVista Software, 2001
   Changes made to support the ITE IIC peripheral */


#ifndef I2C_ALGO_ITE_H
#define I2C_ALGO_ITE_H 1

#include <linux/types.h>

/* Example of a sequential read request:
	struct i2c_iic_msg s_msg; 

	s_msg.addr=device_address;
	s_msg.len=length;
	s_msg.buf=buffer;
	s_msg.waddr=word_address;
	ioctl(file,I2C_SREAD, &s_msg);
 */
#define I2C_SREAD	0x780	/* SREAD ioctl command */

struct i2c_iic_msg {
	__u16 addr;	/* device address */
	__u16 waddr;	/* word address */
	short len;	/* msg length */
	char *buf;	/* pointer to msg data */
};

#ifdef __KERNEL__
struct i2c_adapter;

struct i2c_algo_iic_data {
	void *data;		/* private data for lolevel routines	*/
	void (*setiic) (void *data, int ctl, int val);
	int  (*getiic) (void *data, int ctl);
	int  (*getown) (void *data);
	int  (*getclock) (void *data);
	void (*waitforpin) (void);     

	/* local settings */
	int udelay;
	int mdelay;
	int timeout;
};

int i2c_iic_add_bus(struct i2c_adapter *);
int i2c_iic_del_bus(struct i2c_adapter *);
#endif /* __KERNEL__ */
#endif /* I2C_ALGO_ITE_H */
