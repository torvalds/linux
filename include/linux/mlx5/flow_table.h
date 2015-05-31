/*
 * Copyright (c) 2013-2015, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MLX5_FLOW_TABLE_H
#define MLX5_FLOW_TABLE_H

#include <linux/mlx5/driver.h>

struct mlx5_flow_table_group {
	u8	log_sz;
	u8	match_criteria_enable;
	u32	match_criteria[MLX5_ST_SZ_DW(fte_match_param)];
};

void *mlx5_create_flow_table(struct mlx5_core_dev *dev, u8 level, u8 table_type,
			     u16 num_groups,
			     struct mlx5_flow_table_group *group);
void mlx5_destroy_flow_table(void *flow_table);
int mlx5_add_flow_table_entry(void *flow_table, u8 match_criteria_enable,
			      void *match_criteria, void *flow_context,
			      u32 *flow_index);
void mlx5_del_flow_table_entry(void *flow_table, u32 flow_index);
u32 mlx5_get_flow_table_id(void *flow_table);

#endif /* MLX5_FLOW_TABLE_H */
