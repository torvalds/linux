/* SPDX-License-Identifier: GPL-2.0 */
/*
 * I2C register access helpers for Omnivision OVxxxx image sensors which expect
 * a 16 bit register address in big-endian format and which have 1-3 byte
 * wide registers, in big-endian format (for the higher width registers).
 *
 * Based on the register helpers from drivers/media/i2c/ov2680.c which is:
 * Copyright (C) 2018 Linaro Ltd
 */
#ifndef __OV_16BIT_ADDR_REG_HELPERS_H
#define __OV_16BIT_ADDR_REG_HELPERS_H

#include <asm/unaligned.h>
#include <linux/dev_printk.h>
#include <linux/i2c.h>

static inline int ov_read_reg(struct i2c_client *client, u16 reg,
				  unsigned int len, u32 *val)
{
	u8 addr_buf[2], data_buf[4] = { };
	struct i2c_msg msgs[2];
	int ret;

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, addr_buf);

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		dev_err(&client->dev, "read error: reg=0x%4x: %d\n", reg, ret);
		return -EIO;
	}

	*val = get_unaligned_be32(data_buf);

	return 0;
}

#define ov_read_reg8(s, r, v)	ov_read_reg(s, r, 1, v)
#define ov_read_reg16(s, r, v)	ov_read_reg(s, r, 2, v)
#define ov_read_reg24(s, r, v)	ov_read_reg(s, r, 3, v)

static inline int ov_write_reg(struct i2c_client *client, u16 reg,
				   unsigned int len, u32 val)
{
	u8 buf[6];
	int ret;

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);
	ret = i2c_master_send(client, buf, len + 2);
	if (ret != len + 2) {
		dev_err(&client->dev, "write error: reg=0x%4x: %d\n", reg, ret);
		return -EIO;
	}

	return 0;
}

#define ov_write_reg8(s, r, v)	ov_write_reg(s, r, 1, v)
#define ov_write_reg16(s, r, v)	ov_write_reg(s, r, 2, v)
#define ov_write_reg24(s, r, v)	ov_write_reg(s, r, 3, v)

static inline int ov_update_reg(struct i2c_client *client, u16 reg, u8 mask, u8 val)
{
	u32 readval;
	int ret;

	ret = ov_read_reg8(client, reg, &readval);
	if (ret < 0)
		return ret;

	val = (readval & ~mask) | (val & mask);

	return ov_write_reg8(client, reg, val);
}

#endif
