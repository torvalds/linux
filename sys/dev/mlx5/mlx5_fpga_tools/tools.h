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

#ifndef __TOOLS_H__
#define __TOOLS_H__

#include <sys/lock.h>
#include <sys/sx.h>
#include <linux/types.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <dev/mlx5/mlx5_fpga/sdk.h>

#define MLX5_FPGA_TOOLS_DRIVER_NAME "mlx5_fpga_tools"

struct mlx5_fpga_tools_dev {
	/* Core device and connection to FPGA */
	struct mlx5_fpga_device *fdev;

	/* Serializes memory accesses */
	struct sx lock;

	/* Char device state */
	void *char_device;
};

int mlx5_fpga_tools_mem_write(struct mlx5_fpga_tools_dev *tdev,
			      void *buf, size_t count, u64 address,
			      enum mlx5_fpga_access_type access_type);
int mlx5_fpga_tools_mem_read(struct mlx5_fpga_tools_dev *tdev, void *buf,
			     size_t count, u64 address,
			     enum mlx5_fpga_access_type access_type);

#endif	/* __TOOLS_H__ */
