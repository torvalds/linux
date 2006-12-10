/* ------------------------------------------------------------------------- */
/* adap-pcf.h i2c driver algorithms for PCF8584 adapters                     */
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

#ifndef _LINUX_I2C_ALGO_PCF_H
#define _LINUX_I2C_ALGO_PCF_H

struct i2c_algo_pcf_data {
	void *data;		/* private data for lolevel routines	*/
	void (*setpcf) (void *data, int ctl, int val);
	int  (*getpcf) (void *data, int ctl);
	int  (*getown) (void *data);
	int  (*getclock) (void *data);
	void (*waitforpin) (void);

	/* local settings */
	int udelay;
	int timeout;
};

int i2c_pcf_add_bus(struct i2c_adapter *);
int i2c_pcf_del_bus(struct i2c_adapter *);

#endif /* _LINUX_I2C_ALGO_PCF_H */
