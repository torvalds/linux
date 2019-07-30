/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ------------------------------------------------------------------------- */
/* adap-pcf.h i2c driver algorithms for PCF8584 adapters                     */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-97 Simon G. Vogl
                   1998-99 Hans Berglund

 */
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
	void (*waitforpin) (void *data);

	void (*xfer_begin) (void *data);
	void (*xfer_end) (void *data);

	/* Multi-master lost arbitration back-off delay (msecs)
	 * This should be set by the bus adapter or knowledgable client
	 * if bus is multi-mastered, else zero
	 */
	unsigned long lab_mdelay;
};

int i2c_pcf_add_bus(struct i2c_adapter *);

#endif /* _LINUX_I2C_ALGO_PCF_H */
