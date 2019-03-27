/*-
 * Copyright (c) 2013-2018, Mellanox Technologies, Ltd.  All rights reserved.
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

#include <linux/module.h>
#include <dev/mlx5/port.h>
#include "mlx5_core.h"

int mlx5_core_access_reg(struct mlx5_core_dev *dev, void *data_in,
			 int size_in, void *data_out, int size_out,
			 u16 reg_num, int arg, int write)
{
	int outlen = MLX5_ST_SZ_BYTES(access_register_out) + size_out;
	int inlen = MLX5_ST_SZ_BYTES(access_register_in) + size_in;
	int err = -ENOMEM;
	u32 *out = NULL;
	u32 *in = NULL;
	void *data;

	in = mlx5_vzalloc(inlen);
	out = mlx5_vzalloc(outlen);
	if (!in || !out)
		goto out;

	data = MLX5_ADDR_OF(access_register_in, in, register_data);
	memcpy(data, data_in, size_in);

	MLX5_SET(access_register_in, in, opcode, MLX5_CMD_OP_ACCESS_REG);
	MLX5_SET(access_register_in, in, op_mod, !write);
	MLX5_SET(access_register_in, in, argument, arg);
	MLX5_SET(access_register_in, in, register_id, reg_num);

	err = mlx5_cmd_exec(dev, in, inlen, out, outlen);
	if (err)
		goto out;
	data = MLX5_ADDR_OF(access_register_out, out, register_data);
	memcpy(data_out, data, size_out);

out:
	kvfree(out);
	kvfree(in);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_access_reg);

int mlx5_query_qcam_reg(struct mlx5_core_dev *mdev, u32 *qcam,
			u8 feature_group, u8 access_reg_group)
{
	u32 in[MLX5_ST_SZ_DW(qcam_reg)] = {};
	int sz = MLX5_ST_SZ_BYTES(qcam_reg);

	MLX5_SET(qcam_reg, in, feature_group, feature_group);
	MLX5_SET(qcam_reg, in, access_reg_group, access_reg_group);

	return mlx5_core_access_reg(mdev, in, sz, qcam, sz, MLX5_REG_QCAM, 0, 0);
}
EXPORT_SYMBOL_GPL(mlx5_query_qcam_reg);

struct mlx5_reg_pcap {
	u8			rsvd0;
	u8			port_num;
	u8			rsvd1[2];
	__be32			caps_127_96;
	__be32			caps_95_64;
	__be32			caps_63_32;
	__be32			caps_31_0;
};

/* This function should be used after setting a port register only */
void mlx5_toggle_port_link(struct mlx5_core_dev *dev)
{
	enum mlx5_port_status ps;

	mlx5_query_port_admin_status(dev, &ps);
	mlx5_set_port_status(dev, MLX5_PORT_DOWN);
	if (ps == MLX5_PORT_UP)
		mlx5_set_port_status(dev, MLX5_PORT_UP);
}
EXPORT_SYMBOL_GPL(mlx5_toggle_port_link);

int mlx5_set_port_caps(struct mlx5_core_dev *dev, u8 port_num, u32 caps)
{
	struct mlx5_reg_pcap in;
	struct mlx5_reg_pcap out;
	int err;

	memset(&in, 0, sizeof(in));
	in.caps_127_96 = cpu_to_be32(caps);
	in.port_num = port_num;

	err = mlx5_core_access_reg(dev, &in, sizeof(in), &out,
				   sizeof(out), MLX5_REG_PCAP, 0, 1);

	return err;
}
EXPORT_SYMBOL_GPL(mlx5_set_port_caps);

int mlx5_query_port_ptys(struct mlx5_core_dev *dev, u32 *ptys,
			 int ptys_size, int proto_mask, u8 local_port)
{
	u32 in[MLX5_ST_SZ_DW(ptys_reg)];
	int err;

	memset(in, 0, sizeof(in));
	MLX5_SET(ptys_reg, in, local_port, local_port);
	MLX5_SET(ptys_reg, in, proto_mask, proto_mask);

	err = mlx5_core_access_reg(dev, in, sizeof(in), ptys,
				   ptys_size, MLX5_REG_PTYS, 0, 0);

	return err;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_ptys);

int mlx5_query_port_proto_cap(struct mlx5_core_dev *dev,
			      u32 *proto_cap, int proto_mask)
{
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];
	int err;

	err = mlx5_query_port_ptys(dev, out, sizeof(out), proto_mask, 1);
	if (err)
		return err;

	if (proto_mask == MLX5_PTYS_EN)
		*proto_cap = MLX5_GET(ptys_reg, out, eth_proto_capability);
	else
		*proto_cap = MLX5_GET(ptys_reg, out, ib_proto_capability);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_proto_cap);

int mlx5_query_port_autoneg(struct mlx5_core_dev *dev, int proto_mask,
			    u8 *an_disable_cap, u8 *an_disable_status)
{
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];
	int err;

	err = mlx5_query_port_ptys(dev, out, sizeof(out), proto_mask, 1);
	if (err)
		return err;

	*an_disable_status = MLX5_GET(ptys_reg, out, an_disable_admin);
	*an_disable_cap = MLX5_GET(ptys_reg, out, an_disable_cap);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_autoneg);

int mlx5_set_port_autoneg(struct mlx5_core_dev *dev, bool disable,
			  u32 eth_proto_admin, int proto_mask)
{
	u32 in[MLX5_ST_SZ_DW(ptys_reg)] = {0};
	u32 out[MLX5_ST_SZ_DW(ptys_reg)] = {0};
	u8 an_disable_cap;
	u8 an_disable_status;
	int err;

	err = mlx5_query_port_autoneg(dev, proto_mask, &an_disable_cap,
				      &an_disable_status);
	if (err)
		return err;
	if (!an_disable_cap)
		return -EPERM;

	MLX5_SET(ptys_reg, in, local_port, 1);
	MLX5_SET(ptys_reg, in, an_disable_admin, disable);
	MLX5_SET(ptys_reg, in, proto_mask, proto_mask);
	if (proto_mask == MLX5_PTYS_EN)
		MLX5_SET(ptys_reg, in, eth_proto_admin, eth_proto_admin);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PTYS, 0, 1);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_set_port_autoneg);

int mlx5_query_port_proto_admin(struct mlx5_core_dev *dev,
				u32 *proto_admin, int proto_mask)
{
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];
	int err;

	err = mlx5_query_port_ptys(dev, out, sizeof(out), proto_mask, 1);
	if (err)
		return err;

	if (proto_mask == MLX5_PTYS_EN)
		*proto_admin = MLX5_GET(ptys_reg, out, eth_proto_admin);
	else
		*proto_admin = MLX5_GET(ptys_reg, out, ib_proto_admin);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_proto_admin);

int mlx5_query_port_eth_proto_oper(struct mlx5_core_dev *dev,
				   u32 *proto_oper, u8 local_port)
{
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];
	int err;

	err = mlx5_query_port_ptys(dev, out, sizeof(out), MLX5_PTYS_EN,
				   local_port);
	if (err)
		return err;

	*proto_oper = MLX5_GET(ptys_reg, out, eth_proto_oper);

	return 0;
}
EXPORT_SYMBOL(mlx5_query_port_eth_proto_oper);

int mlx5_set_port_proto(struct mlx5_core_dev *dev, u32 proto_admin,
			int proto_mask)
{
	u32 in[MLX5_ST_SZ_DW(ptys_reg)] = {0};
	u32 out[MLX5_ST_SZ_DW(ptys_reg)] = {0};
	int err;

	MLX5_SET(ptys_reg, in, local_port, 1);
	MLX5_SET(ptys_reg, in, proto_mask, proto_mask);
	if (proto_mask == MLX5_PTYS_EN)
		MLX5_SET(ptys_reg, in, eth_proto_admin, proto_admin);
	else
		MLX5_SET(ptys_reg, in, ib_proto_admin, proto_admin);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PTYS, 0, 1);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_set_port_proto);

int mlx5_set_port_status(struct mlx5_core_dev *dev,
			 enum mlx5_port_status status)
{
	u32 in[MLX5_ST_SZ_DW(paos_reg)] = {0};
	u32 out[MLX5_ST_SZ_DW(paos_reg)] = {0};
	int err;

	MLX5_SET(paos_reg, in, local_port, 1);

	MLX5_SET(paos_reg, in, admin_status, status);
	MLX5_SET(paos_reg, in, ase, 1);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PAOS, 0, 1);
	return err;
}

int mlx5_query_port_status(struct mlx5_core_dev *dev, u8 *status)
{
	u32 in[MLX5_ST_SZ_DW(paos_reg)] = {0};
	u32 out[MLX5_ST_SZ_DW(paos_reg)] = {0};
	int err;

	MLX5_SET(paos_reg, in, local_port, 1);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PAOS, 0, 0);
	if (err)
		return err;

	*status = MLX5_GET(paos_reg, out, oper_status);
	return err;
}

int mlx5_query_port_admin_status(struct mlx5_core_dev *dev,
				 enum mlx5_port_status *status)
{
	u32 in[MLX5_ST_SZ_DW(paos_reg)] = {0};
	u32 out[MLX5_ST_SZ_DW(paos_reg)];
	int err;

	MLX5_SET(paos_reg, in, local_port, 1);
	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PAOS, 0, 0);
	if (err)
		return err;
	*status = MLX5_GET(paos_reg, out, admin_status);
	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_admin_status);

static int mlx5_query_port_mtu(struct mlx5_core_dev *dev,
			       int *admin_mtu, int *max_mtu, int *oper_mtu)
{
	u32 in[MLX5_ST_SZ_DW(pmtu_reg)] = {0};
	u32 out[MLX5_ST_SZ_DW(pmtu_reg)] = {0};
	int err;

	MLX5_SET(pmtu_reg, in, local_port, 1);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PMTU, 0, 0);
	if (err)
		return err;

	if (max_mtu)
		*max_mtu  = MLX5_GET(pmtu_reg, out, max_mtu);
	if (oper_mtu)
		*oper_mtu = MLX5_GET(pmtu_reg, out, oper_mtu);
	if (admin_mtu)
		*admin_mtu = MLX5_GET(pmtu_reg, out, admin_mtu);

	return err;
}

int mlx5_set_port_mtu(struct mlx5_core_dev *dev, int mtu)
{
	u32 in[MLX5_ST_SZ_DW(pmtu_reg)] = {0};
	u32 out[MLX5_ST_SZ_DW(pmtu_reg)] = {0};

	MLX5_SET(pmtu_reg, in, admin_mtu, mtu);
	MLX5_SET(pmtu_reg, in, local_port, 1);

	return mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PMTU, 0, 1);
}
EXPORT_SYMBOL_GPL(mlx5_set_port_mtu);

int mlx5_query_port_max_mtu(struct mlx5_core_dev *dev, int *max_mtu)
{
	return mlx5_query_port_mtu(dev, NULL, max_mtu, NULL);
}
EXPORT_SYMBOL_GPL(mlx5_query_port_max_mtu);

int mlx5_set_port_pause_and_pfc(struct mlx5_core_dev *dev, u32 port,
				u8 rx_pause, u8 tx_pause,
				u8 pfc_en_rx, u8 pfc_en_tx)
{
	u32 in[MLX5_ST_SZ_DW(pfcc_reg)] = {0};
	u32 out[MLX5_ST_SZ_DW(pfcc_reg)] = {0};

	if (pfc_en_rx || pfc_en_tx) {
		/* PFC and global pauseframes are incompatible features */
		if (tx_pause || rx_pause)
			return -EINVAL;
	}

	MLX5_SET(pfcc_reg, in, local_port, port);
	MLX5_SET(pfcc_reg, in, pptx, tx_pause);
	MLX5_SET(pfcc_reg, in, pprx, rx_pause);
	MLX5_SET(pfcc_reg, in, pfctx, pfc_en_tx);
	MLX5_SET(pfcc_reg, in, pfcrx, pfc_en_rx);
	MLX5_SET(pfcc_reg, in, prio_mask_tx, pfc_en_tx);
	MLX5_SET(pfcc_reg, in, prio_mask_rx, pfc_en_rx);

	return mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PFCC, 0, 1);
}

int mlx5_query_port_pause(struct mlx5_core_dev *dev, u32 port,
			  u32 *rx_pause, u32 *tx_pause)
{
	u32 in[MLX5_ST_SZ_DW(pfcc_reg)] = {0};
	u32 out[MLX5_ST_SZ_DW(pfcc_reg)] = {0};
	int err;

	MLX5_SET(pfcc_reg, in, local_port, port);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PFCC, 0, 0);
	if (err)
		return err;

	*rx_pause = MLX5_GET(pfcc_reg, out, pprx);
	*tx_pause = MLX5_GET(pfcc_reg, out, pptx);

	return 0;
}

int mlx5_query_port_pfc(struct mlx5_core_dev *dev, u8 *pfc_en_tx, u8 *pfc_en_rx)
{
	u32 in[MLX5_ST_SZ_DW(pfcc_reg)] = {};
	u32 out[MLX5_ST_SZ_DW(pfcc_reg)];
	int err;

	MLX5_SET(pfcc_reg, in, local_port, 1);
	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PFCC, 0, 0);
	if (err)
		return err;

	if (pfc_en_tx != NULL)
		*pfc_en_tx = MLX5_GET(pfcc_reg, out, pfctx);
	if (pfc_en_rx != NULL)
		*pfc_en_rx = MLX5_GET(pfcc_reg, out, pfcrx);
	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_pfc);

int mlx5_query_port_oper_mtu(struct mlx5_core_dev *dev, int *oper_mtu)
{
	return mlx5_query_port_mtu(dev, NULL, NULL, oper_mtu);
}
EXPORT_SYMBOL_GPL(mlx5_query_port_oper_mtu);

u8 mlx5_is_wol_supported(struct mlx5_core_dev *dev)
{
	u8 wol_supported = 0;

	if (MLX5_CAP_GEN(dev, wol_s))
		wol_supported |= MLX5_WOL_SECURED_MAGIC;
	if (MLX5_CAP_GEN(dev, wol_g))
		wol_supported |= MLX5_WOL_MAGIC;
	if (MLX5_CAP_GEN(dev, wol_a))
		wol_supported |= MLX5_WOL_ARP;
	if (MLX5_CAP_GEN(dev, wol_b))
		wol_supported |= MLX5_WOL_BROADCAST;
	if (MLX5_CAP_GEN(dev, wol_m))
		wol_supported |= MLX5_WOL_MULTICAST;
	if (MLX5_CAP_GEN(dev, wol_u))
		wol_supported |= MLX5_WOL_UNICAST;
	if (MLX5_CAP_GEN(dev, wol_p))
		wol_supported |= MLX5_WOL_PHY_ACTIVITY;

	return wol_supported;
}
EXPORT_SYMBOL_GPL(mlx5_is_wol_supported);

int mlx5_set_wol(struct mlx5_core_dev *dev, u8 wol_mode)
{
	u32 in[MLX5_ST_SZ_DW(set_wol_rol_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(set_wol_rol_out)] = {0};

	MLX5_SET(set_wol_rol_in, in, opcode, MLX5_CMD_OP_SET_WOL_ROL);
	MLX5_SET(set_wol_rol_in, in, wol_mode_valid, 1);
	MLX5_SET(set_wol_rol_in, in, wol_mode, wol_mode);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL_GPL(mlx5_set_wol);

int mlx5_query_dropless_mode(struct mlx5_core_dev *dev, u16 *timeout)
{
	u32 in[MLX5_ST_SZ_DW(query_delay_drop_params_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(query_delay_drop_params_out)] = {0};
	int err = 0;

	MLX5_SET(query_delay_drop_params_in, in, opcode,
		 MLX5_CMD_OP_QUERY_DELAY_DROP_PARAMS);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	*timeout = MLX5_GET(query_delay_drop_params_out, out,
			    delay_drop_timeout);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_dropless_mode);

int mlx5_set_dropless_mode(struct mlx5_core_dev *dev, u16 timeout)
{
	u32 in[MLX5_ST_SZ_DW(set_delay_drop_params_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(set_delay_drop_params_out)] = {0};

	MLX5_SET(set_delay_drop_params_in, in, opcode,
		 MLX5_CMD_OP_SET_DELAY_DROP_PARAMS);
	MLX5_SET(set_delay_drop_params_in, in, delay_drop_timeout, timeout);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL_GPL(mlx5_set_dropless_mode);

int mlx5_core_access_pvlc(struct mlx5_core_dev *dev,
			  struct mlx5_pvlc_reg *pvlc, int write)
{
	int sz = MLX5_ST_SZ_BYTES(pvlc_reg);
	u8 in[MLX5_ST_SZ_BYTES(pvlc_reg)] = {0};
	u8 out[MLX5_ST_SZ_BYTES(pvlc_reg)] = {0};
	int err;

	MLX5_SET(pvlc_reg, in, local_port, pvlc->local_port);
	if (write)
		MLX5_SET(pvlc_reg, in, vl_admin, pvlc->vl_admin);

	err = mlx5_core_access_reg(dev, in, sz, out, sz, MLX5_REG_PVLC, 0,
				   !!write);
	if (err)
		return err;

	if (!write) {
		pvlc->local_port = MLX5_GET(pvlc_reg, out, local_port);
		pvlc->vl_hw_cap = MLX5_GET(pvlc_reg, out, vl_hw_cap);
		pvlc->vl_admin = MLX5_GET(pvlc_reg, out, vl_admin);
		pvlc->vl_operational = MLX5_GET(pvlc_reg, out, vl_operational);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_core_access_pvlc);

int mlx5_core_access_ptys(struct mlx5_core_dev *dev,
			  struct mlx5_ptys_reg *ptys, int write)
{
	int sz = MLX5_ST_SZ_BYTES(ptys_reg);
	void *out = NULL;
	void *in = NULL;
	int err;

	in = mlx5_vzalloc(sz);
	if (!in)
		return -ENOMEM;

	out = mlx5_vzalloc(sz);
	if (!out) {
		kfree(in);
		return -ENOMEM;
	}

	MLX5_SET(ptys_reg, in, local_port, ptys->local_port);
	MLX5_SET(ptys_reg, in, proto_mask, ptys->proto_mask);
	if (write) {
		MLX5_SET(ptys_reg, in, eth_proto_capability,
			 ptys->eth_proto_cap);
		MLX5_SET(ptys_reg, in, ib_link_width_capability,
			 ptys->ib_link_width_cap);
		MLX5_SET(ptys_reg, in, ib_proto_capability,
			 ptys->ib_proto_cap);
		MLX5_SET(ptys_reg, in, eth_proto_admin, ptys->eth_proto_admin);
		MLX5_SET(ptys_reg, in, ib_link_width_admin,
			 ptys->ib_link_width_admin);
		MLX5_SET(ptys_reg, in, ib_proto_admin, ptys->ib_proto_admin);
		MLX5_SET(ptys_reg, in, eth_proto_oper, ptys->eth_proto_oper);
		MLX5_SET(ptys_reg, in, ib_link_width_oper,
			 ptys->ib_link_width_oper);
		MLX5_SET(ptys_reg, in, ib_proto_oper, ptys->ib_proto_oper);
		MLX5_SET(ptys_reg, in, eth_proto_lp_advertise,
			 ptys->eth_proto_lp_advertise);
	}

	err = mlx5_core_access_reg(dev, in, sz, out, sz, MLX5_REG_PTYS, 0,
				   !!write);
	if (err)
		goto out;

	if (!write) {
		ptys->local_port = MLX5_GET(ptys_reg, out, local_port);
		ptys->proto_mask = MLX5_GET(ptys_reg, out, proto_mask);
		ptys->eth_proto_cap = MLX5_GET(ptys_reg, out,
					       eth_proto_capability);
		ptys->ib_link_width_cap = MLX5_GET(ptys_reg, out,
					   ib_link_width_capability);
		ptys->ib_proto_cap = MLX5_GET(ptys_reg, out,
					      ib_proto_capability);
		ptys->eth_proto_admin = MLX5_GET(ptys_reg, out,
						 eth_proto_admin);
		ptys->ib_link_width_admin = MLX5_GET(ptys_reg, out,
						     ib_link_width_admin);
		ptys->ib_proto_admin = MLX5_GET(ptys_reg, out, ib_proto_admin);
		ptys->eth_proto_oper = MLX5_GET(ptys_reg, out, eth_proto_oper);
		ptys->ib_link_width_oper = MLX5_GET(ptys_reg, out,
						    ib_link_width_oper);
		ptys->ib_proto_oper = MLX5_GET(ptys_reg, out, ib_proto_oper);
		ptys->eth_proto_lp_advertise = MLX5_GET(ptys_reg, out,
							eth_proto_lp_advertise);
	}

out:
	kvfree(in);
	kvfree(out);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_access_ptys);

static int mtu_to_ib_mtu(int mtu)
{
	switch (mtu) {
	case 256: return 1;
	case 512: return 2;
	case 1024: return 3;
	case 2048: return 4;
	case 4096: return 5;
	default:
		printf("mlx5_core: WARN: ""invalid mtu\n");
		return -1;
	}
}

int mlx5_core_access_pmtu(struct mlx5_core_dev *dev,
			  struct mlx5_pmtu_reg *pmtu, int write)
{
	int sz = MLX5_ST_SZ_BYTES(pmtu_reg);
	void *out = NULL;
	void *in = NULL;
	int err;

	in = mlx5_vzalloc(sz);
	if (!in)
		return -ENOMEM;

	out = mlx5_vzalloc(sz);
	if (!out) {
		kfree(in);
		return -ENOMEM;
	}

	MLX5_SET(pmtu_reg, in, local_port, pmtu->local_port);
	if (write)
		MLX5_SET(pmtu_reg, in, admin_mtu, pmtu->admin_mtu);

	err = mlx5_core_access_reg(dev, in, sz, out, sz, MLX5_REG_PMTU, 0,
				   !!write);
	if (err)
		goto out;

	if (!write) {
		pmtu->local_port = MLX5_GET(pmtu_reg, out, local_port);
		pmtu->max_mtu = mtu_to_ib_mtu(MLX5_GET(pmtu_reg, out,
						       max_mtu));
		pmtu->admin_mtu = mtu_to_ib_mtu(MLX5_GET(pmtu_reg, out,
							 admin_mtu));
		pmtu->oper_mtu = mtu_to_ib_mtu(MLX5_GET(pmtu_reg, out,
							oper_mtu));
	}

out:
	kvfree(in);
	kvfree(out);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_core_access_pmtu);

int mlx5_query_module_num(struct mlx5_core_dev *dev, int *module_num)
{
	u32 in[MLX5_ST_SZ_DW(pmlp_reg)] = {0};
	u32 out[MLX5_ST_SZ_DW(pmlp_reg)] = {0};
	int lane = 0;
	int err;

	MLX5_SET(pmlp_reg, in, local_port, 1);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PMLP, 0, 0);
	if (err)
		return err;

	lane = MLX5_GET(pmlp_reg, out, lane0_module_mapping);
	*module_num = lane & MLX5_EEPROM_IDENTIFIER_BYTE_MASK;

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_module_num);

int mlx5_query_eeprom(struct mlx5_core_dev *dev,
		      int i2c_addr, int page_num, int device_addr,
		      int size, int module_num, u32 *data, int *size_read)
{
	u32 in[MLX5_ST_SZ_DW(mcia_reg)] = {0};
	u32 out[MLX5_ST_SZ_DW(mcia_reg)] = {0};
	u32 *ptr = (u32 *)MLX5_ADDR_OF(mcia_reg, out, dword_0);
	int status;
	int err;

	size = min_t(int, size, MLX5_EEPROM_MAX_BYTES);

	MLX5_SET(mcia_reg, in, l, 0);
	MLX5_SET(mcia_reg, in, module, module_num);
	MLX5_SET(mcia_reg, in, i2c_device_address, i2c_addr);
	MLX5_SET(mcia_reg, in, page_number, page_num);
	MLX5_SET(mcia_reg, in, device_address, device_addr);
	MLX5_SET(mcia_reg, in, size, size);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_MCIA, 0, 0);
	if (err)
		return err;

	status = MLX5_GET(mcia_reg, out, status);
	if (status)
		return status;

	memcpy(data, ptr, size);
	*size_read = size;
	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_eeprom);

int mlx5_vxlan_udp_port_add(struct mlx5_core_dev *dev, u16 port)
{
	u32 in[MLX5_ST_SZ_DW(add_vxlan_udp_dport_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(add_vxlan_udp_dport_out)] = {0};
	int err;

	MLX5_SET(add_vxlan_udp_dport_in, in, opcode,
		 MLX5_CMD_OP_ADD_VXLAN_UDP_DPORT);
	MLX5_SET(add_vxlan_udp_dport_in, in, vxlan_udp_port, port);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err) {
		mlx5_core_err(dev, "Failed %s, port %u, err - %d",
			      mlx5_command_str(MLX5_CMD_OP_ADD_VXLAN_UDP_DPORT),
			      port, err);
	}

	return err;
}

int mlx5_vxlan_udp_port_delete(struct mlx5_core_dev *dev, u16 port)
{
	u32 in[MLX5_ST_SZ_DW(delete_vxlan_udp_dport_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(delete_vxlan_udp_dport_out)] = {0};
	int err;

	MLX5_SET(delete_vxlan_udp_dport_in, in, opcode,
		 MLX5_CMD_OP_DELETE_VXLAN_UDP_DPORT);
	MLX5_SET(delete_vxlan_udp_dport_in, in, vxlan_udp_port, port);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err) {
		mlx5_core_err(dev, "Failed %s, port %u, err - %d",
			      mlx5_command_str(MLX5_CMD_OP_DELETE_VXLAN_UDP_DPORT),
			      port, err);
	}

	return err;
}

int mlx5_query_wol(struct mlx5_core_dev *dev, u8 *wol_mode)
{
	u32 in[MLX5_ST_SZ_DW(query_wol_rol_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(query_wol_rol_out)] = {0};
	int err;

	MLX5_SET(query_wol_rol_in, in, opcode, MLX5_CMD_OP_QUERY_WOL_ROL);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));

	if (!err)
		*wol_mode = MLX5_GET(query_wol_rol_out, out, wol_mode);

	return err;
}
EXPORT_SYMBOL_GPL(mlx5_query_wol);

int mlx5_query_port_cong_status(struct mlx5_core_dev *mdev, int protocol,
				int priority, int *is_enable)
{
	u32 in[MLX5_ST_SZ_DW(query_cong_status_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(query_cong_status_out)] = {0};
	int err;

	*is_enable = 0;

	MLX5_SET(query_cong_status_in, in, opcode,
		 MLX5_CMD_OP_QUERY_CONG_STATUS);
	MLX5_SET(query_cong_status_in, in, cong_protocol, protocol);
	MLX5_SET(query_cong_status_in, in, priority, priority);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*is_enable = MLX5_GET(query_cong_status_out, out, enable);
	return err;
}

int mlx5_modify_port_cong_status(struct mlx5_core_dev *mdev, int protocol,
				 int priority, int enable)
{
	u32 in[MLX5_ST_SZ_DW(modify_cong_status_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(modify_cong_status_out)] = {0};

	MLX5_SET(modify_cong_status_in, in, opcode,
		 MLX5_CMD_OP_MODIFY_CONG_STATUS);
	MLX5_SET(modify_cong_status_in, in, cong_protocol, protocol);
	MLX5_SET(modify_cong_status_in, in, priority, priority);
	MLX5_SET(modify_cong_status_in, in, enable, enable);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

int mlx5_query_port_cong_params(struct mlx5_core_dev *mdev, int protocol,
				void *out, int out_size)
{
	u32 in[MLX5_ST_SZ_DW(query_cong_params_in)] = {0};

	MLX5_SET(query_cong_params_in, in, opcode,
		 MLX5_CMD_OP_QUERY_CONG_PARAMS);
	MLX5_SET(query_cong_params_in, in, cong_protocol, protocol);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, out_size);
}

static int mlx5_query_port_qetcr_reg(struct mlx5_core_dev *mdev, u32 *out,
				     int outlen)
{
	u32 in[MLX5_ST_SZ_DW(qetc_reg)];

	if (!MLX5_CAP_GEN(mdev, ets))
		return -ENOTSUPP;

	memset(in, 0, sizeof(in));
	return mlx5_core_access_reg(mdev, in, sizeof(in), out, outlen,
				    MLX5_REG_QETCR, 0, 0);
}

int mlx5_max_tc(struct mlx5_core_dev *mdev)
{
	u8 num_tc = MLX5_CAP_GEN(mdev, max_tc) ? : 8;

	return num_tc - 1;
}
EXPORT_SYMBOL_GPL(mlx5_max_tc);

static int mlx5_set_port_qetcr_reg(struct mlx5_core_dev *mdev, u32 *in,
				   int inlen)
{
	u32 out[MLX5_ST_SZ_DW(qetc_reg)];

	if (!MLX5_CAP_GEN(mdev, ets))
		return -ENOTSUPP;

	return mlx5_core_access_reg(mdev, in, inlen, out, sizeof(out),
				    MLX5_REG_QETCR, 0, 1);
}

int mlx5_query_port_tc_rate_limit(struct mlx5_core_dev *mdev,
				   u8 *max_bw_value,
				   u8 *max_bw_units)
{
	u32 out[MLX5_ST_SZ_DW(qetc_reg)];
	void *ets_tcn_conf;
	int err;
	int i;

	err = mlx5_query_port_qetcr_reg(mdev, out, sizeof(out));
	if (err)
		return err;

	for (i = 0; i <= mlx5_max_tc(mdev); i++) {
		ets_tcn_conf = MLX5_ADDR_OF(qetc_reg, out, tc_configuration[i]);

		max_bw_value[i] = MLX5_GET(ets_tcn_config_reg, ets_tcn_conf,
					   max_bw_value);
		max_bw_units[i] = MLX5_GET(ets_tcn_config_reg, ets_tcn_conf,
					   max_bw_units);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_tc_rate_limit);

int mlx5_modify_port_tc_rate_limit(struct mlx5_core_dev *mdev,
				   const u8 *max_bw_value,
				   const u8 *max_bw_units)
{
	u32 in[MLX5_ST_SZ_DW(qetc_reg)] = {};
	void *ets_tcn_conf;
	int i;

	MLX5_SET(qetc_reg, in, port_number, 1);

	for (i = 0; i <= mlx5_max_tc(mdev); i++) {
		ets_tcn_conf = MLX5_ADDR_OF(qetc_reg, in, tc_configuration[i]);

		MLX5_SET(ets_tcn_config_reg, ets_tcn_conf, r, 1);
		MLX5_SET(ets_tcn_config_reg, ets_tcn_conf, max_bw_units,
			 max_bw_units[i]);
		MLX5_SET(ets_tcn_config_reg, ets_tcn_conf, max_bw_value,
			 max_bw_value[i]);
	}

	return mlx5_set_port_qetcr_reg(mdev, in, sizeof(in));
}
EXPORT_SYMBOL_GPL(mlx5_modify_port_tc_rate_limit);

int mlx5_query_port_prio_tc(struct mlx5_core_dev *mdev,
			    u8 prio, u8 *tc)
{
	u32 in[MLX5_ST_SZ_DW(qtct_reg)];
	u32 out[MLX5_ST_SZ_DW(qtct_reg)];
	int err;

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

	MLX5_SET(qtct_reg, in, port_number, 1);
	MLX5_SET(qtct_reg, in, prio, prio);

	err = mlx5_core_access_reg(mdev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_QTCT, 0, 0);
	if (!err)
		*tc = MLX5_GET(qtct_reg, out, tclass);

	return err;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_prio_tc);

int mlx5_set_port_prio_tc(struct mlx5_core_dev *mdev, int prio_index,
			  const u8 prio_tc)
{
	u32 in[MLX5_ST_SZ_DW(qtct_reg)] = {};
	u32 out[MLX5_ST_SZ_DW(qtct_reg)];
	int err;

	if (prio_tc > mlx5_max_tc(mdev))
		return -EINVAL;

	MLX5_SET(qtct_reg, in, prio, prio_index);
	MLX5_SET(qtct_reg, in, tclass, prio_tc);

	err = mlx5_core_access_reg(mdev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_QTCT, 0, 1);

	return (err);
}
EXPORT_SYMBOL_GPL(mlx5_set_port_prio_tc);

int mlx5_set_port_tc_group(struct mlx5_core_dev *mdev, const u8 *tc_group)
{
	u32 in[MLX5_ST_SZ_DW(qetc_reg)] = {};
	int i;

	for (i = 0; i <= mlx5_max_tc(mdev); i++) {
		MLX5_SET(qetc_reg, in, tc_configuration[i].g, 1);
		MLX5_SET(qetc_reg, in, tc_configuration[i].group, tc_group[i]);
	}

	return mlx5_set_port_qetcr_reg(mdev, in, sizeof(in));
}
EXPORT_SYMBOL_GPL(mlx5_set_port_tc_group);

int mlx5_query_port_tc_group(struct mlx5_core_dev *mdev,
			     u8 tc, u8 *tc_group)
{
	u32 out[MLX5_ST_SZ_DW(qetc_reg)];
	void *ets_tcn_conf;
	int err;

	err = mlx5_query_port_qetcr_reg(mdev, out, sizeof(out));
	if (err)
		return err;

	ets_tcn_conf = MLX5_ADDR_OF(qetc_reg, out,
				    tc_configuration[tc]);

	*tc_group = MLX5_GET(ets_tcn_config_reg, ets_tcn_conf,
			     group);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_tc_group);

int mlx5_set_port_tc_bw_alloc(struct mlx5_core_dev *mdev, const u8 *tc_bw)
{
	u32 in[MLX5_ST_SZ_DW(qetc_reg)] = {};
	int i;

	for (i = 0; i <= mlx5_max_tc(mdev); i++) {
		MLX5_SET(qetc_reg, in, tc_configuration[i].b, 1);
		MLX5_SET(qetc_reg, in, tc_configuration[i].bw_allocation, tc_bw[i]);
	}

	return mlx5_set_port_qetcr_reg(mdev, in, sizeof(in));
}
EXPORT_SYMBOL_GPL(mlx5_set_port_tc_bw_alloc);

int mlx5_query_port_tc_bw_alloc(struct mlx5_core_dev *mdev, u8 *bw_pct)
{
	u32 out[MLX5_ST_SZ_DW(qetc_reg)];
	void *ets_tcn_conf;
	int err;
	int i;

	err = mlx5_query_port_qetcr_reg(mdev, out, sizeof(out));
	if (err)
		return err;

	for (i = 0; i <= mlx5_max_tc(mdev); i++) {
		ets_tcn_conf = MLX5_ADDR_OF(qetc_reg, out, tc_configuration[i]);
		bw_pct[i] = MLX5_GET(ets_tcn_config_reg, ets_tcn_conf, bw_allocation);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_query_port_tc_bw_alloc);

int mlx5_modify_port_cong_params(struct mlx5_core_dev *mdev,
				 void *in, int in_size)
{
	u32 out[MLX5_ST_SZ_DW(modify_cong_params_out)] = {0};

	MLX5_SET(modify_cong_params_in, in, opcode,
		 MLX5_CMD_OP_MODIFY_CONG_PARAMS);

	return mlx5_cmd_exec(mdev, in, in_size, out, sizeof(out));
}

int mlx5_query_port_cong_statistics(struct mlx5_core_dev *mdev, int clear,
				    void *out, int out_size)
{
	u32 in[MLX5_ST_SZ_DW(query_cong_statistics_in)] = {0};

	MLX5_SET(query_cong_statistics_in, in, opcode,
		 MLX5_CMD_OP_QUERY_CONG_STATISTICS);
	MLX5_SET(query_cong_statistics_in, in, clear, clear);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, out_size);
}

int mlx5_set_diagnostic_params(struct mlx5_core_dev *mdev, void *in,
			       int in_size)
{
	u32 out[MLX5_ST_SZ_DW(set_diagnostic_params_out)] = {0};

	MLX5_SET(set_diagnostic_params_in, in, opcode,
		 MLX5_CMD_OP_SET_DIAGNOSTICS);

	return mlx5_cmd_exec(mdev, in, in_size, out, sizeof(out));
}

int mlx5_query_diagnostic_counters(struct mlx5_core_dev *mdev,
				   u8 num_of_samples, u16 sample_index,
				   void *out, int out_size)
{
	u32 in[MLX5_ST_SZ_DW(query_diagnostic_counters_in)] = {0};

	MLX5_SET(query_diagnostic_counters_in, in, opcode,
		 MLX5_CMD_OP_QUERY_DIAGNOSTICS);
	MLX5_SET(query_diagnostic_counters_in, in, num_of_samples,
		 num_of_samples);
	MLX5_SET(query_diagnostic_counters_in, in, sample_index, sample_index);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, out_size);
}

int mlx5_set_trust_state(struct mlx5_core_dev *mdev, u8 trust_state)
{
	u32 out[MLX5_ST_SZ_DW(qpts_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(qpts_reg)] = {};
	int err;

	MLX5_SET(qpts_reg, in, local_port, 1);
	MLX5_SET(qpts_reg, in, trust_state, trust_state);

	err = mlx5_core_access_reg(mdev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_QPTS, 0, 1);
	return err;
}

int mlx5_query_trust_state(struct mlx5_core_dev *mdev, u8 *trust_state)
{
	u32 out[MLX5_ST_SZ_DW(qpts_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(qpts_reg)] = {};
	int err;

	MLX5_SET(qpts_reg, in, local_port, 1);

	err = mlx5_core_access_reg(mdev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_QPTS, 0, 0);
	if (!err)
		*trust_state = MLX5_GET(qpts_reg, out, trust_state);

	return err;
}

int mlx5_set_dscp2prio(struct mlx5_core_dev *mdev, const u8 *dscp2prio)
{
	int sz = MLX5_ST_SZ_BYTES(qpdpm_reg);
	void *qpdpm_dscp;
	void *out;
	void *in;
	int err;
	int i;

	in = kzalloc(sz, GFP_KERNEL);
	out = kzalloc(sz, GFP_KERNEL);
	if (!in || !out) {
		err = -ENOMEM;
		goto out;
	}

	MLX5_SET(qpdpm_reg, in, local_port, 1);
	err = mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_QPDPM, 0, 0);
	if (err)
		goto out;

	memcpy(in, out, sz);
	MLX5_SET(qpdpm_reg, in, local_port, 1);

	/* Update the corresponding dscp entry */
	for (i = 0; i < MLX5_MAX_SUPPORTED_DSCP; i++) {
		qpdpm_dscp = MLX5_ADDR_OF(qpdpm_reg, in, dscp[i]);
		MLX5_SET16(qpdpm_dscp_reg, qpdpm_dscp, prio, dscp2prio[i]);
		MLX5_SET16(qpdpm_dscp_reg, qpdpm_dscp, e, 1);
	}
	err = mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_QPDPM, 0, 1);
out:
	kfree(in);
	kfree(out);
	return err;
}

int mlx5_query_dscp2prio(struct mlx5_core_dev *mdev, u8 *dscp2prio)
{
	int sz = MLX5_ST_SZ_BYTES(qpdpm_reg);
	void *qpdpm_dscp;
	void *out;
	void *in;
	int err;
	int i;

	in = kzalloc(sz, GFP_KERNEL);
	out = kzalloc(sz, GFP_KERNEL);
	if (!in || !out) {
		err = -ENOMEM;
		goto out;
	}

	MLX5_SET(qpdpm_reg, in, local_port, 1);
	err = mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_QPDPM, 0, 0);
	if (err)
		goto out;

	for (i = 0; i < MLX5_MAX_SUPPORTED_DSCP; i++) {
		qpdpm_dscp = MLX5_ADDR_OF(qpdpm_reg, out, dscp[i]);
		dscp2prio[i] = MLX5_GET16(qpdpm_dscp_reg, qpdpm_dscp, prio);
	}
out:
	kfree(in);
	kfree(out);
	return err;
}

int mlx5_query_pddr_range_info(struct mlx5_core_dev *mdev, u8 local_port, u8 *is_er_type)
{
	u32 pddr_reg[MLX5_ST_SZ_DW(pddr_reg)] = {};
	int sz = MLX5_ST_SZ_BYTES(pddr_reg);
	int error;
	u8 ecc;
	u8 ci;

	MLX5_SET(pddr_reg, pddr_reg, local_port, local_port);
	MLX5_SET(pddr_reg, pddr_reg, page_select, 3 /* module info page */);

	error = mlx5_core_access_reg(mdev, pddr_reg, sz, pddr_reg, sz,
	    MLX5_ACCESS_REG_SUMMARY_CTRL_ID_PDDR, 0, 0);
	if (error != 0)
		return (error);

	ecc = MLX5_GET(pddr_reg, pddr_reg, page_data.pddr_module_info.ethernet_compliance_code);
	ci = MLX5_GET(pddr_reg, pddr_reg, page_data.pddr_module_info.cable_identifier);

	switch (ci) {
	case 0:	/* QSFP28 */
	case 1:	/* QSFP+ */
		*is_er_type = 0;
		break;
	case 2:	/* SFP28/SFP+ */
	case 3: /* QSA (QSFP->SFP) */
		*is_er_type = ((ecc & (1 << 7)) != 0);
		break;
	default:
		*is_er_type = 0;
		break;
	}
	return (0);
}
EXPORT_SYMBOL_GPL(mlx5_query_pddr_range_info);
