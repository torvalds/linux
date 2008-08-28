/*
 * Header file for I2C support on PNX010x/4008.
 *
 * Author: Dennis Kovalev <dkovalev@ru.mvista.com>
 *
 * 2004-2006 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __I2C_PNX_H__
#define __I2C_PNX_H__

#include <linux/pm.h>

struct platform_device;

struct i2c_pnx_mif {
	int			ret;		/* Return value */
	int			mode;		/* Interface mode */
	struct completion	complete;	/* I/O completion */
	struct timer_list	timer;		/* Timeout */
	char *			buf;		/* Data buffer */
	int			len;		/* Length of data buffer */
};

struct i2c_pnx_algo_data {
	u32			base;
	u32			ioaddr;
	int			irq;
	struct i2c_pnx_mif	mif;
	int			last;
};

struct i2c_pnx_data {
	int (*suspend) (struct platform_device *pdev, pm_message_t state);
	int (*resume) (struct platform_device *pdev);
	u32 (*calculate_input_freq) (struct platform_device *pdev);
	int (*set_clock_run) (struct platform_device *pdev);
	int (*set_clock_stop) (struct platform_device *pdev);
	struct i2c_adapter *adapter;
};

#endif /* __I2C_PNX_H__ */
