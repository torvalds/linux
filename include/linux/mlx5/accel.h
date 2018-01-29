/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
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
 */

#ifndef __MLX5_ACCEL_H__
#define __MLX5_ACCEL_H__

#include <linux/mlx5/driver.h>

enum mlx5_accel_ipsec_caps {
	MLX5_ACCEL_IPSEC_CAP_DEVICE		= 1 << 0,
	MLX5_ACCEL_IPSEC_CAP_ESP		= 1 << 2,
	MLX5_ACCEL_IPSEC_CAP_IPV6		= 1 << 3,
	MLX5_ACCEL_IPSEC_CAP_LSO		= 1 << 4,
	MLX5_ACCEL_IPSEC_CAP_RX_NO_TRAILER	= 1 << 5,
	MLX5_ACCEL_IPSEC_CAP_V2_CMD		= 1 << 6,
};

#ifdef CONFIG_MLX5_ACCEL

u32 mlx5_accel_ipsec_device_caps(struct mlx5_core_dev *mdev);

#else

static inline u32 mlx5_accel_ipsec_device_caps(struct mlx5_core_dev *mdev) { return 0; }

#endif
#endif
