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
 */
struct mlxcpld_mux_plat_data {
	int *chan_ids;
	int num_adaps;
	int sel_reg_addr;
};

#endif /* _LINUX_I2C_MLXCPLD_H */
