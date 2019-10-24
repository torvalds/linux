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

#ifndef __MLX5_VPORT_H__
#define __MLX5_VPORT_H__

#include <linux/mlx5/driver.h>
#include <linux/mlx5/device.h>

#define MLX5_VPORT_PF_PLACEHOLDER		(1u)
#define MLX5_VPORT_UPLINK_PLACEHOLDER		(1u)
#define MLX5_VPORT_ECPF_PLACEHOLDER(mdev)	(mlx5_ecpf_vport_exists(mdev))

#define MLX5_SPECIAL_VPORTS(mdev) (MLX5_VPORT_PF_PLACEHOLDER +		\
				   MLX5_VPORT_UPLINK_PLACEHOLDER +	\
				   MLX5_VPORT_ECPF_PLACEHOLDER(mdev))

#define MLX5_VPORT_MANAGER(mdev)					\
	(MLX5_CAP_GEN(mdev, vport_group_manager) &&			\
	 (MLX5_CAP_GEN(mdev, port_type) == MLX5_CAP_PORT_TYPE_ETH) &&	\
	 mlx5_core_is_pf(mdev))

enum {
	MLX5_CAP_INLINE_MODE_L2,
	MLX5_CAP_INLINE_MODE_VPORT_CONTEXT,
	MLX5_CAP_INLINE_MODE_NOT_REQUIRED,
};

/* Vport number for each function must keep unchanged */
enum {
	MLX5_VPORT_PF			= 0x0,
	MLX5_VPORT_FIRST_VF		= 0x1,
	MLX5_VPORT_ECPF			= 0xfffe,
	MLX5_VPORT_UPLINK		= 0xffff
};

u8 mlx5_query_vport_state(struct mlx5_core_dev *mdev, u8 opmod, u16 vport);
int mlx5_modify_vport_admin_state(struct mlx5_core_dev *mdev, u8 opmod,
				  u16 vport, u8 other_vport, u8 state);
int mlx5_query_nic_vport_mac_address(struct mlx5_core_dev *mdev,
				     u16 vport, bool other, u8 *addr);
int mlx5_query_mac_address(struct mlx5_core_dev *mdev, u8 *addr);
int mlx5_query_nic_vport_min_inline(struct mlx5_core_dev *mdev,
				    u16 vport, u8 *min_inline);
void mlx5_query_min_inline(struct mlx5_core_dev *mdev, u8 *min_inline);
int mlx5_modify_nic_vport_min_inline(struct mlx5_core_dev *mdev,
				     u16 vport, u8 min_inline);
int mlx5_modify_nic_vport_mac_address(struct mlx5_core_dev *dev,
				      u16 vport, u8 *addr);
int mlx5_query_nic_vport_mtu(struct mlx5_core_dev *mdev, u16 *mtu);
int mlx5_modify_nic_vport_mtu(struct mlx5_core_dev *mdev, u16 mtu);
int mlx5_query_nic_vport_system_image_guid(struct mlx5_core_dev *mdev,
					   u64 *system_image_guid);
int mlx5_query_nic_vport_node_guid(struct mlx5_core_dev *mdev, u64 *node_guid);
int mlx5_modify_nic_vport_node_guid(struct mlx5_core_dev *mdev,
				    u16 vport, u64 node_guid);
int mlx5_query_nic_vport_qkey_viol_cntr(struct mlx5_core_dev *mdev,
					u16 *qkey_viol_cntr);
int mlx5_query_hca_vport_gid(struct mlx5_core_dev *dev, u8 other_vport,
			     u8 port_num, u16  vf_num, u16 gid_index,
			     union ib_gid *gid);
int mlx5_query_hca_vport_pkey(struct mlx5_core_dev *dev, u8 other_vport,
			      u8 port_num, u16 vf_num, u16 pkey_index,
			      u16 *pkey);
int mlx5_query_hca_vport_context(struct mlx5_core_dev *dev,
				 u8 other_vport, u8 port_num,
				 u16 vf_num,
				 struct mlx5_hca_vport_context *rep);
int mlx5_query_hca_vport_system_image_guid(struct mlx5_core_dev *dev,
					   u64 *sys_image_guid);
int mlx5_query_hca_vport_node_guid(struct mlx5_core_dev *dev,
				   u64 *node_guid);
int mlx5_query_nic_vport_mac_list(struct mlx5_core_dev *dev,
				  u16 vport,
				  enum mlx5_list_type list_type,
				  u8 addr_list[][ETH_ALEN],
				  int *list_size);
int mlx5_modify_nic_vport_mac_list(struct mlx5_core_dev *dev,
				   enum mlx5_list_type list_type,
				   u8 addr_list[][ETH_ALEN],
				   int list_size);
int mlx5_query_nic_vport_promisc(struct mlx5_core_dev *mdev,
				 u16 vport,
				 int *promisc_uc,
				 int *promisc_mc,
				 int *promisc_all);
int mlx5_modify_nic_vport_promisc(struct mlx5_core_dev *mdev,
				  int promisc_uc,
				  int promisc_mc,
				  int promisc_all);
int mlx5_modify_nic_vport_vlans(struct mlx5_core_dev *dev,
				u16 vlans[],
				int list_size);

int mlx5_nic_vport_enable_roce(struct mlx5_core_dev *mdev);
int mlx5_nic_vport_disable_roce(struct mlx5_core_dev *mdev);
int mlx5_query_vport_down_stats(struct mlx5_core_dev *mdev, u16 vport,
				u8 other_vport, u64 *rx_discard_vport_down,
				u64 *tx_discard_vport_down);
int mlx5_core_query_vport_counter(struct mlx5_core_dev *dev, u8 other_vport,
				  int vf, u8 port_num, void *out,
				  size_t out_sz);
int mlx5_core_modify_hca_vport_context(struct mlx5_core_dev *dev,
				       u8 other_vport, u8 port_num,
				       int vf,
				       struct mlx5_hca_vport_context *req);
int mlx5_nic_vport_update_local_lb(struct mlx5_core_dev *mdev, bool enable);
int mlx5_nic_vport_query_local_lb(struct mlx5_core_dev *mdev, bool *status);

int mlx5_nic_vport_affiliate_multiport(struct mlx5_core_dev *master_mdev,
				       struct mlx5_core_dev *port_mdev);
int mlx5_nic_vport_unaffiliate_multiport(struct mlx5_core_dev *port_mdev);

u64 mlx5_query_nic_system_image_guid(struct mlx5_core_dev *mdev);
#endif /* __MLX5_VPORT_H__ */
