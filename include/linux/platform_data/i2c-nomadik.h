/*
 * Copyright (C) 2009 ST-Ericsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */
#ifndef __PDATA_I2C_NOMADIK_H
#define __PDATA_I2C_NOMADIK_H

enum i2c_freq_mode {
	I2C_FREQ_MODE_STANDARD,		/* up to 100 Kb/s */
	I2C_FREQ_MODE_FAST,		/* up to 400 Kb/s */
	I2C_FREQ_MODE_HIGH_SPEED,	/* up to 3.4 Mb/s */
	I2C_FREQ_MODE_FAST_PLUS,	/* up to 1 Mb/s */
};

/**
 * struct nmk_i2c_controller - client specific controller configuration
 * @clk_freq:	clock frequency for the operation mode
 * @slsu:	Slave data setup time in ns.
 *		The needed setup time for three modes of operation
 *		are 250ns, 100ns and 10ns respectively thus leading
 *		to the values of 14, 6, 2 for a 48 MHz i2c clk
 * @tft:	Tx FIFO Threshold in bytes
 * @rft:	Rx FIFO Threshold in bytes
 * @timeout	Slave response timeout(ms)
 * @sm:		speed mode
 */
struct nmk_i2c_controller {
	unsigned long	clk_freq;
	unsigned short	slsu;
	unsigned char	tft;
	unsigned char	rft;
	int timeout;
	enum i2c_freq_mode	sm;
};

#endif	/* __PDATA_I2C_NOMADIK_H */
