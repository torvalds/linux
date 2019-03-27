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

#include <dev/mlx5/driver.h>
#include <linux/module.h>
#include "mlx5_core.h"

static int mlx5_cmd_query_adapter(struct mlx5_core_dev *dev, u32 *out,
				  int outlen)
{
	u32 in[MLX5_ST_SZ_DW(query_adapter_in)];
	int err;

	memset(in, 0, sizeof(in));

	MLX5_SET(query_adapter_in, in, opcode, MLX5_CMD_OP_QUERY_ADAPTER);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
	return err;
}

int mlx5_query_board_id(struct mlx5_core_dev *dev)
{
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_adapter_out);
	int err;

	out = kzalloc(outlen, GFP_KERNEL);

	err = mlx5_cmd_query_adapter(dev, out, outlen);
	if (err)
		goto out_out;

	memcpy(dev->board_id,
	       MLX5_ADDR_OF(query_adapter_out, out,
			    query_adapter_struct.vsd_contd_psid),
	       MLX5_FLD_SZ_BYTES(query_adapter_out,
				 query_adapter_struct.vsd_contd_psid));

out_out:
	kfree(out);

	return err;
}

int mlx5_core_query_vendor_id(struct mlx5_core_dev *mdev, u32 *vendor_id)
{
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_adapter_out);
	int err;

	out = kzalloc(outlen, GFP_KERNEL);

	err = mlx5_cmd_query_adapter(mdev, out, outlen);
	if (err)
		goto out_out;

	*vendor_id = MLX5_GET(query_adapter_out, out,
			      query_adapter_struct.ieee_vendor_id);

out_out:
	kfree(out);

	return err;
}
EXPORT_SYMBOL(mlx5_core_query_vendor_id);

static int mlx5_core_query_special_contexts(struct mlx5_core_dev *dev)
{
	u32 in[MLX5_ST_SZ_DW(query_special_contexts_in)];
	u32 out[MLX5_ST_SZ_DW(query_special_contexts_out)];
	int err;

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

	MLX5_SET(query_special_contexts_in, in, opcode,
		 MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	dev->special_contexts.resd_lkey = MLX5_GET(query_special_contexts_out,
						   out, resd_lkey);

	return err;
}

static int mlx5_get_qcam_reg(struct mlx5_core_dev *dev)
{
	return mlx5_query_qcam_reg(dev, dev->caps.qcam,
				   MLX5_QCAM_FEATURE_ENHANCED_FEATURES,
				   MLX5_QCAM_REGS_FIRST_128);
}

int mlx5_query_hca_caps(struct mlx5_core_dev *dev)
{
	int err;

	err = mlx5_core_get_caps(dev, MLX5_CAP_GENERAL);
	if (err)
		return err;

	if (MLX5_CAP_GEN(dev, eth_net_offloads)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ETHERNET_OFFLOADS);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, pg)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ODP);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, atomic)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ATOMIC);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, roce)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ROCE);
		if (err)
			return err;
	}

	if ((MLX5_CAP_GEN(dev, port_type) ==
	    MLX5_CMD_HCA_CAP_PORT_TYPE_ETHERNET &&
	    MLX5_CAP_GEN(dev, nic_flow_table)) ||
	    (MLX5_CAP_GEN(dev, port_type) == MLX5_CMD_HCA_CAP_PORT_TYPE_IB &&
	    MLX5_CAP_GEN(dev, ipoib_enhanced_offloads))) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_FLOW_TABLE);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, eswitch_flow_table)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ESWITCH_FLOW_TABLE);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, vport_group_manager)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ESWITCH);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, snapshot)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_SNAPSHOT);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, ipoib_enhanced_offloads)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_EOIB_OFFLOADS);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, debug)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_DEBUG);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, qos)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_QOS);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, qcam_reg)) {
		err = mlx5_get_qcam_reg(dev);
		if (err)
			return err;
	}

	err = mlx5_core_query_special_contexts(dev);
	if (err)
		return err;

	return 0;
}

int mlx5_cmd_init_hca(struct mlx5_core_dev *dev)
{
	u32 in[MLX5_ST_SZ_DW(init_hca_in)];
	u32 out[MLX5_ST_SZ_DW(init_hca_out)];

	memset(in, 0, sizeof(in));

	MLX5_SET(init_hca_in, in, opcode, MLX5_CMD_OP_INIT_HCA);

	memset(out, 0, sizeof(out));
	return mlx5_cmd_exec(dev, in,  sizeof(in), out, sizeof(out));
}

int mlx5_cmd_teardown_hca(struct mlx5_core_dev *dev)
{
	u32 in[MLX5_ST_SZ_DW(teardown_hca_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(teardown_hca_out)] = {0};

	MLX5_SET(teardown_hca_in, in, opcode, MLX5_CMD_OP_TEARDOWN_HCA);
	return mlx5_cmd_exec(dev, in,  sizeof(in), out, sizeof(out));
}

int mlx5_cmd_force_teardown_hca(struct mlx5_core_dev *dev)
{
	u32 out[MLX5_ST_SZ_DW(teardown_hca_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(teardown_hca_in)] = {0};
	int force_state;
	int ret;

	if (!MLX5_CAP_GEN(dev, force_teardown)) {
		mlx5_core_dbg(dev, "force teardown is not supported in the firmware\n");
		return -EOPNOTSUPP;
	}

	MLX5_SET(teardown_hca_in, in, opcode, MLX5_CMD_OP_TEARDOWN_HCA);
	MLX5_SET(teardown_hca_in, in, profile, MLX5_TEARDOWN_HCA_IN_PROFILE_FORCE_CLOSE);

	ret = mlx5_cmd_exec_polling(dev, in, sizeof(in), out, sizeof(out));
	if (ret)
		return ret;

	force_state = MLX5_GET(teardown_hca_out, out, force_state);
	if (force_state == MLX5_TEARDOWN_HCA_OUT_FORCE_STATE_FAIL)  {
		mlx5_core_err(dev, "teardown with force mode failed\n");
		return -EIO;
	}

	return 0;
}

int mlx5_core_set_dc_cnak_trace(struct mlx5_core_dev *dev, int enable,
				u64 addr)
{
	u32 in[MLX5_ST_SZ_DW(set_dc_cnak_trace_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(set_dc_cnak_trace_out)] = {0};
	__be64 be_addr;
	void *pas;

	MLX5_SET(set_dc_cnak_trace_in, in, opcode, MLX5_CMD_OP_SET_DC_CNAK_TRACE);
	MLX5_SET(set_dc_cnak_trace_in, in, enable, enable);
	pas = MLX5_ADDR_OF(set_dc_cnak_trace_in, in, pas);
	be_addr = cpu_to_be64(addr);
	memcpy(MLX5_ADDR_OF(cmd_pas, pas, pa_h), &be_addr, sizeof(be_addr));

	return mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
}
