/*
 * Copyright (c) 2018, Mellanox Technologies. All rights reserved.
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

#ifndef _MLX5_FS_HELPERS_
#define _MLX5_FS_HELPERS_

#include <linux/mlx5/mlx5_ifc.h>

#define MLX5_FS_IPV4_VERSION 4
#define MLX5_FS_IPV6_VERSION 6

static inline bool _mlx5_fs_is_outer_ipv_flow(struct mlx5_core_dev *mdev,
					      const u32 *match_c,
					      const u32 *match_v, int version)
{
	int match_ipv = MLX5_CAP_FLOWTABLE_NIC_RX(mdev,
						  ft_field_support.outer_ip_version);
	const void *headers_c = MLX5_ADDR_OF(fte_match_param, match_c,
					     outer_headers);
	const void *headers_v = MLX5_ADDR_OF(fte_match_param, match_v,
					     outer_headers);

	if (!match_ipv) {
		u16 ethertype;

		switch (version) {
		case MLX5_FS_IPV4_VERSION:
			ethertype = ETH_P_IP;
			break;
		case MLX5_FS_IPV6_VERSION:
			ethertype = ETH_P_IPV6;
			break;
		default:
			return false;
		}

		return MLX5_GET(fte_match_set_lyr_2_4, headers_c,
				ethertype) == 0xffff &&
			MLX5_GET(fte_match_set_lyr_2_4, headers_v,
				 ethertype) == ethertype;
	}

	return MLX5_GET(fte_match_set_lyr_2_4, headers_c,
			ip_version) == 0xf &&
		MLX5_GET(fte_match_set_lyr_2_4, headers_v,
			 ip_version) == version;
}

static inline bool
mlx5_fs_is_outer_ipv4_flow(struct mlx5_core_dev *mdev, const u32 *match_c,
			   const u32 *match_v)
{
	return _mlx5_fs_is_outer_ipv_flow(mdev, match_c, match_v,
					  MLX5_FS_IPV4_VERSION);
}

static inline bool
mlx5_fs_is_outer_ipv6_flow(struct mlx5_core_dev *mdev, const u32 *match_c,
			   const u32 *match_v)
{
	return _mlx5_fs_is_outer_ipv_flow(mdev, match_c, match_v,
					  MLX5_FS_IPV6_VERSION);
}

#endif
