/*
 *
 * i2c-mux.h - functions for the i2c-bus mux support
 *
 * Copyright (c) 2008-2009 Rodolfo Giometti <giometti@linux.it>
 * Copyright (c) 2008-2009 Eurotech S.p.A. <info@eurotech.it>
 * Michael Lawnick <michael.lawnick.ext@nsn.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA.
 */

#ifndef _LINUX_I2C_MUX_H
#define _LINUX_I2C_MUX_H

#ifdef __KERNEL__

#include <linux/bitops.h>

struct i2c_mux_core {
	struct i2c_adapter *parent;
	struct device *dev;
	unsigned int mux_locked:1;
	unsigned int arbitrator:1;
	unsigned int gate:1;

	void *priv;

	int (*select)(struct i2c_mux_core *, u32 chan_id);
	int (*deselect)(struct i2c_mux_core *, u32 chan_id);

	int num_adapters;
	int max_adapters;
	struct i2c_adapter *adapter[0];
};

struct i2c_mux_core *i2c_mux_alloc(struct i2c_adapter *parent,
				   struct device *dev, int max_adapters,
				   int sizeof_priv, u32 flags,
				   int (*select)(struct i2c_mux_core *, u32),
				   int (*deselect)(struct i2c_mux_core *, u32));

/* flags for i2c_mux_alloc */
#define I2C_MUX_LOCKED     BIT(0)
#define I2C_MUX_ARBITRATOR BIT(1)
#define I2C_MUX_GATE       BIT(2)

static inline void *i2c_mux_priv(struct i2c_mux_core *muxc)
{
	return muxc->priv;
}

struct i2c_adapter *i2c_root_adapter(struct device *dev);

/*
 * Called to create an i2c bus on a multiplexed bus segment.
 * The chan_id parameter is passed to the select and deselect
 * callback functions to perform hardware-specific mux control.
 */
int i2c_mux_add_adapter(struct i2c_mux_core *muxc,
			u32 force_nr, u32 chan_id,
			unsigned int class);

void i2c_mux_del_adapters(struct i2c_mux_core *muxc);

#endif /* __KERNEL__ */

#endif /* _LINUX_I2C_MUX_H */
