/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Mellanox I2C multiplexer support in CPLD
 *
 * Copyright (C) 2016-2020 Mellanox Technologies
 */

#ifndef _LINUX_I2C_MLXCPLD_H
#define _LINUX_I2C_MLXCPLD_H

/* Platform data for the CPLD I2C multiplexers */

/* mlxcpld_mux_plat_data - per mux data, used with i2c_register_board_info
 * @chan_ids - channels array
 * @num_adaps - number of adapters
 * @sel_reg_addr - mux select register offset in CPLD space
 * @reg_size: register size in bytes
 * @handle: handle to be passed by callback
 * @completion_notify: callback to notify when all the adapters are created
 */
struct mlxcpld_mux_plat_data {
	int *chan_ids;
	int num_adaps;
	int sel_reg_addr;
	u8 reg_size;
	void *handle;
	int (*completion_notify)(void *handle, struct i2c_adapter *parent,
				 struct i2c_adapter *adapters[]);
};

#endif /* _LINUX_I2C_MLXCPLD_H */
