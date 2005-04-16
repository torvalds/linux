/* 
 *  include/asm-ppc/pmac_low_i2c.h
 *
 *  Copyright (C) 2003 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */
#ifndef __PMAC_LOW_I2C_H__
#define __PMAC_LOW_I2C_H__

/* i2c mode (based on the platform functions format) */
enum {
	pmac_low_i2c_mode_dumb		= 1,
	pmac_low_i2c_mode_std		= 2,
	pmac_low_i2c_mode_stdsub	= 3,
	pmac_low_i2c_mode_combined	= 4,
};

/* RW bit in address */
enum {
	pmac_low_i2c_read		= 0x01,
	pmac_low_i2c_write		= 0x00
};

/* Init, called early during boot */
extern void pmac_init_low_i2c(void);

/* Locking functions exposed to i2c-keywest */
int pmac_low_i2c_lock(struct device_node *np);
int pmac_low_i2c_unlock(struct device_node *np);

/* Access functions for platform code */
int pmac_low_i2c_open(struct device_node *np, int channel);
int pmac_low_i2c_close(struct device_node *np);
int pmac_low_i2c_setmode(struct device_node *np, int mode);
int pmac_low_i2c_xfer(struct device_node *np, u8 addrdir, u8 subaddr, u8 *data, int len);


#endif /* __PMAC_LOW_I2C_H__ */
