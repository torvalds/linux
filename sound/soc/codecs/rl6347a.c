// SPDX-License-Identifier: GPL-2.0-only
/*
 * rl6347a.c - RL6347A class device shared support
 *
 * Copyright 2015 Realtek Semiconductor Corp.
 *
 * Author: Oder Chiou <oder_chiou@realtek.com>
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

#include "rl6347a.h"

int rl6347a_hw_write(void *context, unsigned int reg, unsigned int value)
{
	struct i2c_client *client = context;
	struct rl6347a_priv *rl6347a = i2c_get_clientdata(client);
	u8 data[4];
	int ret, i;

	/* handle index registers */
	if (reg <= 0xff) {
		rl6347a_hw_write(client, RL6347A_COEF_INDEX, reg);
		for (i = 0; i < rl6347a->index_cache_size; i++) {
			if (reg == rl6347a->index_cache[i].reg) {
				rl6347a->index_cache[i].def = value;
				break;
			}

		}
		reg = RL6347A_PROC_COEF;
	}

	data[0] = (reg >> 24) & 0xff;
	data[1] = (reg >> 16) & 0xff;
	/*
	 * 4 bit VID: reg should be 0
	 * 12 bit VID: value should be 0
	 * So we use an OR operator to handle it rather than use if condition.
	 */
	data[2] = ((reg >> 8) & 0xff) | ((value >> 8) & 0xff);
	data[3] = value & 0xff;

	ret = i2c_master_send(client, data, 4);

	if (ret == 4)
		return 0;
	else
		dev_err(&client->dev, "I2C error %d\n", ret);
	if (ret < 0)
		return ret;
	else
		return -EIO;
}
EXPORT_SYMBOL_GPL(rl6347a_hw_write);

int rl6347a_hw_read(void *context, unsigned int reg, unsigned int *value)
{
	struct i2c_client *client = context;
	struct i2c_msg xfer[2];
	int ret;
	__be32 be_reg, buf = 0x0;
	unsigned int index, vid;

	/* handle index registers */
	if (reg <= 0xff) {
		rl6347a_hw_write(client, RL6347A_COEF_INDEX, reg);
		reg = RL6347A_PROC_COEF;
	}

	reg = reg | 0x80000;
	vid = (reg >> 8) & 0xfff;

	if (AC_VERB_GET_AMP_GAIN_MUTE == (vid & 0xf00)) {
		index = (reg >> 8) & 0xf;
		reg = (reg & ~0xf0f) | index;
	}
	be_reg = cpu_to_be32(reg);

	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 4;
	xfer[0].buf = (u8 *)&be_reg;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 4;
	xfer[1].buf = (u8 *)&buf;

	ret = i2c_transfer(client->adapter, xfer, 2);
	if (ret < 0)
		return ret;
	else if (ret != 2)
		return -EIO;

	*value = be32_to_cpu(buf);

	return 0;
}
EXPORT_SYMBOL_GPL(rl6347a_hw_read);

MODULE_DESCRIPTION("RL6347A class device shared support");
MODULE_AUTHOR("Oder Chiou <oder_chiou@realtek.com>");
MODULE_LICENSE("GPL v2");
