/*-
 * Copyright (c) 2013-2017, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __MLX5_VPORT_H__
#define __MLX5_VPORT_H__

#include <dev/mlx5/driver.h>

enum {
	MLX5_CAP_INLINE_MODE_L2,
	MLX5_CAP_INLINE_MODE_VPORT_CONTEXT,
	MLX5_CAP_INLINE_MODE_NOT_REQUIRED,
};

int mlx5_vport_alloc_q_counter(struct mlx5_core_dev *mdev, int client_id,
			       u16 *counter_set_id);
int mlx5_vport_dealloc_q_counter(struct mlx5_core_dev *mdev, int client_id,
				 u16 counter_set_id);
int mlx5_vport_query_q_counter(struct mlx5_core_dev *mdev,
			       u16 counter_set_id,
			       int reset,
			       void *out,
			       int out_size);
int mlx5_vport_query_out_of_rx_buffer(struct mlx5_core_dev *mdev,
				      u16 counter_set_id,
				      u32 *out_of_rx_buffer);
enum mlx5_local_lb_selection {
	MLX5_LOCAL_MC_LB,
	MLX5_LOCAL_UC_LB
};

int mlx5_nic_vport_query_local_lb(struct mlx5_core_dev *mdev,
				  enum mlx5_local_lb_selection selection,
				  u8 *value);
int mlx5_nic_vport_modify_local_lb(struct mlx5_core_dev *mdev,
				   enum mlx5_local_lb_selection selection,
				   u8 value);
u8 mlx5_query_vport_state(struct mlx5_core_dev *mdev, u8 opmod, u16 vport);
u8 mlx5_query_vport_admin_state(struct mlx5_core_dev *mdev, u8 opmod,
				u16 vport);
int mlx5_modify_vport_admin_state(struct mlx5_core_dev *mdev, u8 opmod,
				  u16 vport, u8 state);

int mlx5_query_vport_mtu(struct mlx5_core_dev *mdev, int *mtu);
int mlx5_set_vport_mtu(struct mlx5_core_dev *mdev, int mtu);
int mlx5_query_min_wqe_header(struct mlx5_core_dev *dev, int *min_header);
int mlx5_set_vport_min_wqe_header(struct mlx5_core_dev *mdev, u8 vport,
				  int min_header);
int mlx5_query_nic_vport_promisc(struct mlx5_core_dev *mdev,
				 u16 vport,
				 int *promisc_uc,
				 int *promisc_mc,
				 int *promisc_all);

int mlx5_modify_nic_vport_promisc(struct mlx5_core_dev *mdev,
				  int promisc_uc,
				  int promisc_mc,
				  int promisc_all);
int mlx5_query_nic_vport_mac_address(struct mlx5_core_dev *mdev,
				     u16 vport, u8 *addr);
int mlx5_modify_nic_vport_mac_address(struct mlx5_core_dev *dev,
				      u16 vport, u8 mac[ETH_ALEN]);
int mlx5_set_nic_vport_current_mac(struct mlx5_core_dev *mdev, int vport,
				   bool other_vport, u8 *addr);
int mlx5_query_nic_vport_min_inline(struct mlx5_core_dev *mdev,
				    u16 vport, u8 *min_inline);
int mlx5_query_min_inline(struct mlx5_core_dev *mdev, u8 *min_inline);
int mlx5_modify_nic_vport_min_inline(struct mlx5_core_dev *mdev,
				     u16 vport, u8 min_inline);
int mlx5_modify_nic_vport_port_guid(struct mlx5_core_dev *mdev,
				    u32 vport, u64 port_guid);
int mlx5_modify_nic_vport_node_guid(struct mlx5_core_dev *mdev,
				    u32 vport, u64 node_guid);
int mlx5_set_nic_vport_vlan_list(struct mlx5_core_dev *dev, u16 vport,
				 u16 *vlan_list, int list_len);
int mlx5_set_nic_vport_mc_list(struct mlx5_core_dev *mdev, int vport,
			       u64 *addr_list, size_t addr_list_len);
int mlx5_set_nic_vport_promisc(struct mlx5_core_dev *mdev, int vport,
			       bool promisc_mc, bool promisc_uc,
			       bool promisc_all);
int mlx5_query_nic_vport_mac_list(struct mlx5_core_dev *dev,
				  u16 vport,
				  enum mlx5_list_type list_type,
				  u8 addr_list[][ETH_ALEN],
				  int *list_size);
int mlx5_query_nic_vport_vlans(struct mlx5_core_dev *dev,
			       u16 vport,
			       u16 vlans[],
			       int *size);
int mlx5_modify_nic_vport_vlans(struct mlx5_core_dev *dev,
				u16 vlans[],
				int list_size);
int mlx5_query_nic_vport_roce_en(struct mlx5_core_dev *mdev, u8 *enable);
int mlx5_modify_nic_vport_mac_list(struct mlx5_core_dev *dev,
				   enum mlx5_list_type list_type,
				   u8 addr_list[][ETH_ALEN],
				   int list_size);
int mlx5_set_nic_vport_permanent_mac(struct mlx5_core_dev *mdev, int vport,
				     u8 *addr);
int mlx5_nic_vport_enable_roce(struct mlx5_core_dev *mdev);
int mlx5_nic_vport_disable_roce(struct mlx5_core_dev *mdev);
int mlx5_core_query_vport_counter(struct mlx5_core_dev *dev, u8 other_vport,
                                  int vf, u8 port_num, void *out,
                                  size_t out_sz);
int mlx5_query_nic_vport_system_image_guid(struct mlx5_core_dev *mdev,
					   u64 *system_image_guid);
int mlx5_query_vport_system_image_guid(struct mlx5_core_dev *dev,
				       u64 *sys_image_guid);
int mlx5_query_vport_node_guid(struct mlx5_core_dev *dev, u64 *node_guid);
int mlx5_query_vport_port_guid(struct mlx5_core_dev *dev, u64 *port_guid);
int mlx5_query_hca_vport_state(struct mlx5_core_dev *dev, u8 *vport_state);
int mlx5_query_nic_vport_node_guid(struct mlx5_core_dev *mdev, u64 *node_guid);
int mlx5_query_nic_vport_qkey_viol_cntr(struct mlx5_core_dev *mdev,
					u16 *qkey_viol_cntr);
int mlx5_query_hca_vport_node_guid(struct mlx5_core_dev *mdev, u64 *node_guid);
int mlx5_query_hca_vport_system_image_guid(struct mlx5_core_dev *mdev,
					   u64 *system_image_guid);
int mlx5_query_hca_vport_context(struct mlx5_core_dev *mdev,
				 u8 port_num, u8 vport_num, u32 *out,
				 int outlen);
int mlx5_query_hca_vport_pkey(struct mlx5_core_dev *dev, u8 other_vport,
			      u8 port_num, u16 vf_num, u16 pkey_index,
			      u16 *pkey);
int mlx5_query_hca_vport_gid(struct mlx5_core_dev *dev, u8 port_num,
			     u16 vport_num, u16 gid_index, union ib_gid *gid);
int mlx5_set_eswitch_cvlan_info(struct mlx5_core_dev *mdev, u8 vport,
				u8 insert_mode, u8 strip_mode,
				u16 vlan, u8 cfi, u8 pcp);
int mlx5_query_vport_counter(struct mlx5_core_dev *dev,
			     u8 port_num, u16 vport_num,
			     void *out, int out_size);
int mlx5_get_vport_counters(struct mlx5_core_dev *dev, u8 port_num,
			    struct mlx5_vport_counters *vc);
int mlx5_core_query_ib_ppcnt(struct mlx5_core_dev *dev,
			     u8 port_num, void *out, size_t sz);
#endif /* __MLX5_VPORT_H__ */
