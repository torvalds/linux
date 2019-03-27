/*-
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
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
 *
 * $FreeBSD$
 */

#ifndef __MLX5_FPGA_TRANS_H__
#define __MLX5_FPGA_TRANS_H__

#include <dev/mlx5/mlx5_fpga/sdk.h>
#include <dev/mlx5/mlx5_fpga/core.h>

#define MLX5_FPGA_TRANSACTION_MAX_SIZE 1008
#define MLX5_FPGA_TRANSACTION_SEND_ALIGN_BITS 3
#define MLX5_FPGA_TRANSACTION_SEND_PAGE_BITS 12
#define MLX5_FPGA_TID_COUNT 256

enum mlx5_fpga_direction {
	MLX5_FPGA_READ,
	MLX5_FPGA_WRITE,
};

struct mlx5_fpga_transaction {
	struct mlx5_fpga_conn *conn;
	enum mlx5_fpga_direction direction;
	size_t size;
	u64 addr;
	u8 *data;
	void (*complete1)(const struct mlx5_fpga_transaction *complete,
			 u8 status);
};

int mlx5_fpga_trans_device_init(struct mlx5_fpga_device *fdev);
void mlx5_fpga_trans_device_cleanup(struct mlx5_fpga_device *fdev);
int mlx5_fpga_trans_exec(const struct mlx5_fpga_transaction *trans);
void mlx5_fpga_trans_recv(void *cb_arg, struct mlx5_fpga_dma_buf *buf);

#endif /* __MLX_FPGA_TRANS_H__ */
