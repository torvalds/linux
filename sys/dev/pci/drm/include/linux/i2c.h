/*	$OpenBSD: i2c.h,v 1.8 2023/03/21 09:44:35 jsg Exp $	*/
/*
 * Copyright (c) 2017 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _LINUX_I2C_H
#define _LINUX_I2C_H

#include <sys/stdint.h>
#include <sys/rwlock.h>
/*
 * normally seq_file.h is indirectly included via
 *
 * linux/regulator/consumer.h
 * linux/suspend.h
 * linux/swap.h
 * linux/memcontrol.h
 * linux/cgroup.h
 * linux/seq_file.h
 */
#include <linux/seq_file.h>
#include <linux/acpi.h>
#include <linux/device.h>

#include <dev/i2c/i2cvar.h>

struct i2c_algorithm;

#define I2C_FUNC_I2C			0
#define I2C_FUNC_SMBUS_EMUL		0
#define I2C_FUNC_SMBUS_READ_BLOCK_DATA	0
#define I2C_FUNC_SMBUS_BLOCK_PROC_CALL	0
#define I2C_FUNC_10BIT_ADDR		0

#define I2C_AQ_COMB			0
#define I2C_AQ_COMB_SAME_ADDR		0
#define I2C_AQ_NO_ZERO_LEN		0

struct i2c_lock_operations;

struct i2c_adapter_quirks {
	unsigned int flags;
	uint16_t max_read_len;
	uint16_t max_write_len;
	uint16_t max_comb_1st_msg_len;
	uint16_t max_comb_2nd_msg_len;
};

struct i2c_adapter {
	struct i2c_controller ic;

	char name[48];
	const struct i2c_algorithm *algo;
	void *algo_data;
	int retries;
	const struct i2c_lock_operations *lock_ops;
	const struct i2c_adapter_quirks *quirks;

	void *data;
};

struct i2c_lock_operations {
	void (*lock_bus)(struct i2c_adapter *, unsigned int);
	int (*trylock_bus)(struct i2c_adapter *, unsigned int);
	void (*unlock_bus)(struct i2c_adapter *, unsigned int);
};

#define I2C_NAME_SIZE	20

struct i2c_msg {
	uint16_t addr;
	uint16_t flags;
	uint16_t len;
	uint8_t *buf;
};

#define I2C_M_RD	0x0001
#define I2C_M_NOSTART	0x0002
#define I2C_M_STOP	0x0004

struct i2c_algorithm {
	int (*master_xfer)(struct i2c_adapter *, struct i2c_msg *, int);
	uint32_t (*functionality)(struct i2c_adapter *);
};

extern struct i2c_algorithm i2c_bit_algo;

struct i2c_algo_bit_data {
	struct i2c_controller ic;
};

int __i2c_transfer(struct i2c_adapter *, struct i2c_msg *, int);
int i2c_transfer(struct i2c_adapter *, struct i2c_msg *, int);

static inline int
i2c_add_adapter(struct i2c_adapter *adap)
{
	return 0;
}

static inline void
i2c_del_adapter(struct i2c_adapter *adap)
{
}

static inline void *
i2c_get_adapdata(struct i2c_adapter *adap)
{
	return adap->data;
}

static inline void
i2c_set_adapdata(struct i2c_adapter *adap, void *data)
{
	adap->data = data;
}

int i2c_bit_add_bus(struct i2c_adapter *);

#endif
