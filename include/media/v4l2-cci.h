/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MIPI Camera Control Interface (CCI) register access helpers.
 *
 * Copyright (C) 2023 Hans de Goede <hansg@kernel.org>
 */
#ifndef _V4L2_CCI_H
#define _V4L2_CCI_H

#include <linux/types.h>

struct i2c_client;
struct regmap;

/**
 * struct cci_reg_sequence - An individual write from a sequence of CCI writes
 *
 * @reg: Register address, use CCI_REG#() macros to encode reg width
 * @val: Register value
 *
 * Register/value pairs for sequences of writes.
 */
struct cci_reg_sequence {
	u32 reg;
	u64 val;
};

/*
 * Macros to define register address with the register width encoded
 * into the higher bits.
 */
#define CCI_REG_ADDR_MASK		GENMASK(15, 0)
#define CCI_REG_WIDTH_SHIFT		16
#define CCI_REG_WIDTH_MASK		GENMASK(19, 16)

#define CCI_REG8(x)			((1 << CCI_REG_WIDTH_SHIFT) | (x))
#define CCI_REG16(x)			((2 << CCI_REG_WIDTH_SHIFT) | (x))
#define CCI_REG24(x)			((3 << CCI_REG_WIDTH_SHIFT) | (x))
#define CCI_REG32(x)			((4 << CCI_REG_WIDTH_SHIFT) | (x))
#define CCI_REG64(x)			((8 << CCI_REG_WIDTH_SHIFT) | (x))

/**
 * cci_read() - Read a value from a single CCI register
 *
 * @map: Register map to read from
 * @reg: Register address to read, use CCI_REG#() macros to encode reg width
 * @val: Pointer to store read value
 * @err: Optional pointer to store errors, if a previous error is set
 *       then the read will be skipped
 *
 * Return: %0 on success or a negative error code on failure.
 */
int cci_read(struct regmap *map, u32 reg, u64 *val, int *err);

/**
 * cci_write() - Write a value to a single CCI register
 *
 * @map: Register map to write to
 * @reg: Register address to write, use CCI_REG#() macros to encode reg width
 * @val: Value to be written
 * @err: Optional pointer to store errors, if a previous error is set
 *       then the write will be skipped
 *
 * Return: %0 on success or a negative error code on failure.
 */
int cci_write(struct regmap *map, u32 reg, u64 val, int *err);

/**
 * cci_update_bits() - Perform a read/modify/write cycle on
 *                     a single CCI register
 *
 * @map: Register map to update
 * @reg: Register address to update, use CCI_REG#() macros to encode reg width
 * @mask: Bitmask to change
 * @val: New value for bitmask
 * @err: Optional pointer to store errors, if a previous error is set
 *       then the update will be skipped
 *
 * Note this uses read-modify-write to update the bits, atomicity with regards
 * to other cci_*() register access functions is NOT guaranteed.
 *
 * Return: %0 on success or a negative error code on failure.
 */
int cci_update_bits(struct regmap *map, u32 reg, u64 mask, u64 val, int *err);

/**
 * cci_multi_reg_write() - Write multiple registers to the device
 *
 * @map: Register map to write to
 * @regs: Array of structures containing register-address, -value pairs to be
 *        written, register-addresses use CCI_REG#() macros to encode reg width
 * @num_regs: Number of registers to write
 * @err: Optional pointer to store errors, if a previous error is set
 *       then the write will be skipped
 *
 * Write multiple registers to the device where the set of register, value
 * pairs are supplied in any order, possibly not all in a single range.
 *
 * Use of the CCI_REG#() macros to encode reg width is mandatory.
 *
 * For raw lists of register-address, -value pairs with only 8 bit
 * wide writes regmap_multi_reg_write() can be used instead.
 *
 * Return: %0 on success or a negative error code on failure.
 */
int cci_multi_reg_write(struct regmap *map, const struct cci_reg_sequence *regs,
			unsigned int num_regs, int *err);

#if IS_ENABLED(CONFIG_V4L2_CCI_I2C)
/**
 * devm_cci_regmap_init_i2c() - Create regmap to use with cci_*() register
 *                              access functions
 *
 * @client: i2c_client to create the regmap for
 * @reg_addr_bits: register address width to use (8 or 16)
 *
 * Note the memory for the created regmap is devm() managed, tied to the client.
 *
 * Return: %0 on success or a negative error code on failure.
 */
struct regmap *devm_cci_regmap_init_i2c(struct i2c_client *client,
					int reg_addr_bits);
#endif

#endif
