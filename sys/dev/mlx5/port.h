/*-
 * Copyright (c) 2016-2018, Mellanox Technologies, Ltd.  All rights reserved.
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

#ifndef __MLX5_PORT_H__
#define	__MLX5_PORT_H__

#include <dev/mlx5/driver.h>

enum mlx5_beacon_duration {
	MLX5_BEACON_DURATION_OFF = 0x0,
	MLX5_BEACON_DURATION_INF = 0xffff,
};

enum mlx5_module_id {
	MLX5_MODULE_ID_SFP              = 0x3,
	MLX5_MODULE_ID_QSFP             = 0xC,
	MLX5_MODULE_ID_QSFP_PLUS        = 0xD,
	MLX5_MODULE_ID_QSFP28           = 0x11,
};

enum mlx5_an_status {
	MLX5_AN_UNAVAILABLE = 0,
	MLX5_AN_COMPLETE    = 1,
	MLX5_AN_FAILED      = 2,
	MLX5_AN_LINK_UP     = 3,
	MLX5_AN_LINK_DOWN   = 4,
};

#define	MLX5_EEPROM_MAX_BYTES			32
#define	MLX5_EEPROM_IDENTIFIER_BYTE_MASK	0x000000ff
#define	MLX5_EEPROM_REVISION_ID_BYTE_MASK       0x0000ff00
#define	MLX5_EEPROM_PAGE_3_VALID_BIT_MASK       0x00040000
#define	MLX5_I2C_ADDR_LOW		0x50
#define	MLX5_I2C_ADDR_HIGH		0x51
#define	MLX5_EEPROM_PAGE_LENGTH		256

enum mlx5e_link_mode {
	MLX5E_1000BASE_CX_SGMII	 = 0,
	MLX5E_1000BASE_KX	 = 1,
	MLX5E_10GBASE_CX4	 = 2,
	MLX5E_10GBASE_KX4	 = 3,
	MLX5E_10GBASE_KR	 = 4,
	MLX5E_20GBASE_KR2	 = 5,
	MLX5E_40GBASE_CR4	 = 6,
	MLX5E_40GBASE_KR4	 = 7,
	MLX5E_56GBASE_R4	 = 8,
	MLX5E_10GBASE_CR	 = 12,
	MLX5E_10GBASE_SR	 = 13,
	MLX5E_10GBASE_ER	 = 14,
	MLX5E_40GBASE_SR4	 = 15,
	MLX5E_40GBASE_LR4	 = 16,
	MLX5E_50GBASE_SR2	 = 18,
	MLX5E_100GBASE_CR4	 = 20,
	MLX5E_100GBASE_SR4	 = 21,
	MLX5E_100GBASE_KR4	 = 22,
	MLX5E_100GBASE_LR4	 = 23,
	MLX5E_100BASE_TX	 = 24,
	MLX5E_1000BASE_T	 = 25,
	MLX5E_10GBASE_T		 = 26,
	MLX5E_25GBASE_CR	 = 27,
	MLX5E_25GBASE_KR	 = 28,
	MLX5E_25GBASE_SR	 = 29,
	MLX5E_50GBASE_CR2	 = 30,
	MLX5E_50GBASE_KR2	 = 31,
	MLX5E_LINK_MODES_NUMBER,
};

enum mlx5e_connector_type {
	MLX5E_PORT_UNKNOWN	= 0,
	MLX5E_PORT_NONE			= 1,
	MLX5E_PORT_TP			= 2,
	MLX5E_PORT_AUI			= 3,
	MLX5E_PORT_BNC			= 4,
	MLX5E_PORT_MII			= 5,
	MLX5E_PORT_FIBRE		= 6,
	MLX5E_PORT_DA			= 7,
	MLX5E_PORT_OTHER		= 8,
	MLX5E_CONNECTOR_TYPE_NUMBER,
};

enum mlx5_qpts_trust_state {
	MLX5_QPTS_TRUST_PCP = 1,
	MLX5_QPTS_TRUST_DSCP = 2,
	MLX5_QPTS_TRUST_BOTH = 3,
};

#define	MLX5E_PROT_MASK(link_mode) (1 << (link_mode))

#define	PORT_MODULE_EVENT_MODULE_STATUS_MASK 0xF
#define	PORT_MODULE_EVENT_ERROR_TYPE_MASK 0xF

int mlx5_set_port_caps(struct mlx5_core_dev *dev, u8 port_num, u32 caps);
int mlx5_query_port_ptys(struct mlx5_core_dev *dev, u32 *ptys,
			 int ptys_size, int proto_mask, u8 local_port);
int mlx5_query_port_proto_cap(struct mlx5_core_dev *dev,
			      u32 *proto_cap, int proto_mask);
int mlx5_query_port_autoneg(struct mlx5_core_dev *dev, int proto_mask,
			    u8 *an_disable_cap, u8 *an_disable_status);
int mlx5_set_port_autoneg(struct mlx5_core_dev *dev, bool disable,
			  u32 eth_proto_admin, int proto_mask);
int mlx5_query_port_proto_admin(struct mlx5_core_dev *dev,
				u32 *proto_admin, int proto_mask);
int mlx5_query_port_eth_proto_oper(struct mlx5_core_dev *dev,
				   u32 *proto_oper, u8 local_port);
int mlx5_set_port_proto(struct mlx5_core_dev *dev, u32 proto_admin,
			int proto_mask);
int mlx5_set_port_status(struct mlx5_core_dev *dev,
			 enum mlx5_port_status status);
int mlx5_query_port_status(struct mlx5_core_dev *dev, u8 *status);
int mlx5_query_port_admin_status(struct mlx5_core_dev *dev,
				 enum mlx5_port_status *status);
int mlx5_set_port_pause_and_pfc(struct mlx5_core_dev *dev, u32 port,
				u8 rx_pause, u8 tx_pause,
				u8 pfc_en_rx, u8 pfc_en_tx);
int mlx5_query_port_pause(struct mlx5_core_dev *dev, u32 port,
			  u32 *rx_pause, u32 *tx_pause);
int mlx5_query_port_pfc(struct mlx5_core_dev *dev, u8 *pfc_en_tx, u8 *pfc_en_rx);

int mlx5_set_port_mtu(struct mlx5_core_dev *dev, int mtu);
int mlx5_query_port_max_mtu(struct mlx5_core_dev *dev, int *max_mtu);
int mlx5_query_port_oper_mtu(struct mlx5_core_dev *dev, int *oper_mtu);

unsigned int mlx5_query_module_status(struct mlx5_core_dev *dev, int module_num);
int mlx5_query_module_num(struct mlx5_core_dev *dev, int *module_num);
int mlx5_query_eeprom(struct mlx5_core_dev *dev, int i2c_addr, int page_num,
		      int device_addr, int size, int module_num, u32 *data,
		      int *size_read);

int mlx5_max_tc(struct mlx5_core_dev *mdev);
int mlx5_query_port_tc_rate_limit(struct mlx5_core_dev *mdev,
				   u8 *max_bw_value,
				   u8 *max_bw_units);
int mlx5_modify_port_tc_rate_limit(struct mlx5_core_dev *mdev,
				   const u8 *max_bw_value,
				   const u8 *max_bw_units);
int mlx5_query_port_prio_tc(struct mlx5_core_dev *mdev,
			    u8 prio, u8 *tc);
int mlx5_set_port_prio_tc(struct mlx5_core_dev *mdev, int prio_index,
			  const u8 prio_tc);
int mlx5_set_port_tc_group(struct mlx5_core_dev *mdev, const u8 *tc_group);
int mlx5_query_port_tc_group(struct mlx5_core_dev *mdev,
			     u8 tc, u8 *tc_group);
int mlx5_set_port_tc_bw_alloc(struct mlx5_core_dev *mdev, const u8 *tc_bw);
int mlx5_query_port_tc_bw_alloc(struct mlx5_core_dev *mdev, u8 *bw_pct);

int mlx5_set_trust_state(struct mlx5_core_dev *mdev, u8 trust_state);
int mlx5_query_trust_state(struct mlx5_core_dev *mdev, u8 *trust_state);

#define	MLX5_MAX_SUPPORTED_DSCP 64
int mlx5_set_dscp2prio(struct mlx5_core_dev *mdev, const u8 *dscp2prio);
int mlx5_query_dscp2prio(struct mlx5_core_dev *mdev, u8 *dscp2prio);

int mlx5_query_pddr_range_info(struct mlx5_core_dev *mdev, u8 local_port, u8 *is_er_type);

#endif /* __MLX5_PORT_H__ */
