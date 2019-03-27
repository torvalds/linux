/*-
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
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

#include <dev/mlx5/cmd.h>
#include <dev/mlx5/driver.h>
#include <dev/mlx5/device.h>
#include <dev/mlx5/mlx5_core/mlx5_core.h>
#include <dev/mlx5/mlx5_fpga/cmd.h>
#include <dev/mlx5/mlx5_fpga/core.h>

#define MLX5_FPGA_ACCESS_REG_SZ (MLX5_ST_SZ_DW(fpga_access_reg) + \
				 MLX5_FPGA_ACCESS_REG_SIZE_MAX)

int mlx5_fpga_access_reg(struct mlx5_core_dev *dev, u8 size, u64 addr,
			 void *buf, bool write)
{
	u32 in[MLX5_FPGA_ACCESS_REG_SZ] = {0};
	u32 out[MLX5_FPGA_ACCESS_REG_SZ];
	int err;

	if (size & 3)
		return -EINVAL;
	if (addr & 3)
		return -EINVAL;
	if (size > MLX5_FPGA_ACCESS_REG_SIZE_MAX)
		return -EINVAL;

	MLX5_SET(fpga_access_reg, in, size, size);
	MLX5_SET64(fpga_access_reg, in, address, addr);
	if (write)
		memcpy(MLX5_ADDR_OF(fpga_access_reg, in, data), buf, size);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out),
				   MLX5_REG_FPGA_ACCESS_REG, 0, write);
	if (err)
		return err;

	if (!write)
		memcpy(buf, MLX5_ADDR_OF(fpga_access_reg, out, data), size);

	return 0;
}

int mlx5_fpga_caps(struct mlx5_core_dev *dev)
{
	u32 in[MLX5_ST_SZ_DW(fpga_cap)] = {0};

	return mlx5_core_access_reg(dev, in, sizeof(in), dev->caps.fpga,
				    MLX5_ST_SZ_BYTES(fpga_cap),
				    MLX5_REG_FPGA_CAP, 0, 0);
}

int mlx5_fpga_ctrl_op(struct mlx5_core_dev *dev, u8 op)
{
	u32 in[MLX5_ST_SZ_DW(fpga_ctrl)] = {0};
	u32 out[MLX5_ST_SZ_DW(fpga_ctrl)];

	MLX5_SET(fpga_ctrl, in, operation, op);

	return mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out),
				    MLX5_REG_FPGA_CTRL, 0, true);
}

int mlx5_fpga_sbu_caps(struct mlx5_core_dev *dev, void *caps, int size)
{
	unsigned int cap_size = MLX5_CAP_FPGA(dev, sandbox_extended_caps_len);
	u64 addr = MLX5_CAP64_FPGA(dev, sandbox_extended_caps_addr);
	unsigned int read;
	int ret = 0;

	if (cap_size > size) {
		mlx5_core_warn(dev, "Not enough buffer %u for FPGA SBU caps %u",
			       size, cap_size);
		return -EINVAL;
	}

	while (cap_size > 0) {
		read = min_t(unsigned int, cap_size,
			     MLX5_FPGA_ACCESS_REG_SIZE_MAX);

		ret = mlx5_fpga_access_reg(dev, read, addr, caps, false);
		if (ret) {
			mlx5_core_warn(dev, "Error reading FPGA SBU caps %u bytes at address %#jx: %d",
				       read, (uintmax_t)addr, ret);
			return ret;
		}

		cap_size -= read;
		addr += read;
		caps += read;
	}

	return ret;
}

static int mlx5_fpga_ctrl_write(struct mlx5_core_dev *dev, u8 op,
				enum mlx5_fpga_image image)
{
	u32 in[MLX5_ST_SZ_DW(fpga_ctrl)] = {0};
	u32 out[MLX5_ST_SZ_DW(fpga_ctrl)];

	MLX5_SET(fpga_ctrl, in, operation, op);
	MLX5_SET(fpga_ctrl, in, flash_select_admin, image);

	return mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out),
				    MLX5_REG_FPGA_CTRL, 0, true);
}

int mlx5_fpga_load(struct mlx5_core_dev *dev, enum mlx5_fpga_image image)
{
	return mlx5_fpga_ctrl_write(dev, MLX5_FPGA_CTRL_OPERATION_LOAD, image);
}

int mlx5_fpga_image_select(struct mlx5_core_dev *dev,
			   enum mlx5_fpga_image image)
{
	return mlx5_fpga_ctrl_write(dev, MLX5_FPGA_CTRL_OPERATION_FLASH_SELECT, image);
}

int mlx5_fpga_query(struct mlx5_core_dev *dev, struct mlx5_fpga_query *query)
{
	u32 in[MLX5_ST_SZ_DW(fpga_ctrl)] = {0};
	u32 out[MLX5_ST_SZ_DW(fpga_ctrl)];
	int err;

	err = mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out),
				   MLX5_REG_FPGA_CTRL, 0, false);
	if (err)
		return err;

	query->image_status = MLX5_GET(fpga_ctrl, out, status);
	query->admin_image = MLX5_GET(fpga_ctrl, out, flash_select_admin);
	query->oper_image = MLX5_GET(fpga_ctrl, out, flash_select_oper);
	return 0;
}

int mlx5_fpga_ctrl_connect(struct mlx5_core_dev *dev,
			   enum mlx5_fpga_connect *connect)
{
	u32 in[MLX5_ST_SZ_DW(fpga_ctrl)] = {0};
	u32 out[MLX5_ST_SZ_DW(fpga_ctrl)];
	int status;
	int err;

	if (*connect == MLX5_FPGA_CONNECT_QUERY) {
		err = mlx5_core_access_reg(dev, in, sizeof(in), out,
					   sizeof(out), MLX5_REG_FPGA_CTRL,
					   0, false);
		if (err)
			return err;
		status = MLX5_GET(fpga_ctrl, out, status);
		*connect = (status == MLX5_FDEV_STATE_DISCONNECTED) ?
			MLX5_FPGA_CONNECT_DISCONNECT :
			MLX5_FPGA_CONNECT_CONNECT;
	} else {
		MLX5_SET(fpga_ctrl, in, operation, *connect);
		err = mlx5_core_access_reg(dev, in, sizeof(in), out,
					   sizeof(out), MLX5_REG_FPGA_CTRL,
					   0, true);
	}
	return err;
}

int mlx5_fpga_query_mtmp(struct mlx5_core_dev *dev,
			 struct mlx5_fpga_temperature *temp)
{
	u32 in[MLX5_ST_SZ_DW(mtmp_reg)] = {0};
	u32 out[MLX5_ST_SZ_DW(mtmp_reg)] = {0};
	int err;

	MLX5_SET(mtmp_reg, in, sensor_index, temp->index);
	MLX5_SET(mtmp_reg, in, i,
		 ((temp->index < MLX5_FPGA_INTERNAL_SENSORS_LOW) ||
		 (temp->index > MLX5_FPGA_INTERNAL_SENSORS_HIGH)) ? 1 : 0);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out),
				   MLX5_REG_MTMP, 0, false);
	if (err)
		return err;

	temp->index = MLX5_GET(mtmp_reg, out, sensor_index);
	temp->temperature = MLX5_GET(mtmp_reg, out, temperature);
	temp->mte = MLX5_GET(mtmp_reg, out, mte);
	temp->max_temperature = MLX5_GET(mtmp_reg, out, max_temperature);
	temp->tee = MLX5_GET(mtmp_reg, out, tee);
	temp->temperature_threshold_hi = MLX5_GET(mtmp_reg, out,
		temperature_threshold_hi);
	temp->temperature_threshold_lo = MLX5_GET(mtmp_reg, out,
		temperature_threshold_lo);
	memcpy(temp->sensor_name, MLX5_ADDR_OF(mtmp_reg, out, sensor_name),
	       MLX5_FLD_SZ_BYTES(mtmp_reg, sensor_name));

	return 0;
}

int mlx5_fpga_create_qp(struct mlx5_core_dev *dev, void *fpga_qpc,
			u32 *fpga_qpn)
{
	u32 in[MLX5_ST_SZ_DW(fpga_create_qp_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(fpga_create_qp_out)];
	int ret;

	MLX5_SET(fpga_create_qp_in, in, opcode, MLX5_CMD_OP_FPGA_CREATE_QP);
	memcpy(MLX5_ADDR_OF(fpga_create_qp_in, in, fpga_qpc), fpga_qpc,
	       MLX5_FLD_SZ_BYTES(fpga_create_qp_in, fpga_qpc));

	ret = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (ret)
		return ret;

	memcpy(fpga_qpc, MLX5_ADDR_OF(fpga_create_qp_out, out, fpga_qpc),
	       MLX5_FLD_SZ_BYTES(fpga_create_qp_out, fpga_qpc));
	*fpga_qpn = MLX5_GET(fpga_create_qp_out, out, fpga_qpn);
	return ret;
}

int mlx5_fpga_modify_qp(struct mlx5_core_dev *dev, u32 fpga_qpn,
			enum mlx5_fpga_qpc_field_select fields,
			void *fpga_qpc)
{
	u32 in[MLX5_ST_SZ_DW(fpga_modify_qp_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(fpga_modify_qp_out)];

	MLX5_SET(fpga_modify_qp_in, in, opcode, MLX5_CMD_OP_FPGA_MODIFY_QP);
	MLX5_SET(fpga_modify_qp_in, in, field_select, fields);
	MLX5_SET(fpga_modify_qp_in, in, fpga_qpn, fpga_qpn);
	memcpy(MLX5_ADDR_OF(fpga_modify_qp_in, in, fpga_qpc), fpga_qpc,
	       MLX5_FLD_SZ_BYTES(fpga_modify_qp_in, fpga_qpc));

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_fpga_query_qp(struct mlx5_core_dev *dev,
		       u32 fpga_qpn, void *fpga_qpc)
{
	u32 in[MLX5_ST_SZ_DW(fpga_query_qp_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(fpga_query_qp_out)];
	int ret;

	MLX5_SET(fpga_query_qp_in, in, opcode, MLX5_CMD_OP_FPGA_QUERY_QP);
	MLX5_SET(fpga_query_qp_in, in, fpga_qpn, fpga_qpn);

	ret = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (ret)
		return ret;

	memcpy(fpga_qpc, MLX5_ADDR_OF(fpga_query_qp_out, out, fpga_qpc),
	       MLX5_FLD_SZ_BYTES(fpga_query_qp_out, fpga_qpc));
	return ret;
}

int mlx5_fpga_destroy_qp(struct mlx5_core_dev *dev, u32 fpga_qpn)
{
	u32 in[MLX5_ST_SZ_DW(fpga_destroy_qp_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(fpga_destroy_qp_out)];

	MLX5_SET(fpga_destroy_qp_in, in, opcode, MLX5_CMD_OP_FPGA_DESTROY_QP);
	MLX5_SET(fpga_destroy_qp_in, in, fpga_qpn, fpga_qpn);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_fpga_query_qp_counters(struct mlx5_core_dev *dev, u32 fpga_qpn,
				bool clear, struct mlx5_fpga_qp_counters *data)
{
	u32 in[MLX5_ST_SZ_DW(fpga_query_qp_counters_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(fpga_query_qp_counters_out)];
	int ret;

	MLX5_SET(fpga_query_qp_counters_in, in, opcode,
		 MLX5_CMD_OP_FPGA_QUERY_QP_COUNTERS);
	MLX5_SET(fpga_query_qp_counters_in, in, clear, clear);
	MLX5_SET(fpga_query_qp_counters_in, in, fpga_qpn, fpga_qpn);

	ret = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (ret)
		return ret;

	data->rx_ack_packets = MLX5_GET64(fpga_query_qp_counters_out, out,
					  rx_ack_packets);
	data->rx_send_packets = MLX5_GET64(fpga_query_qp_counters_out, out,
					   rx_send_packets);
	data->tx_ack_packets = MLX5_GET64(fpga_query_qp_counters_out, out,
					  tx_ack_packets);
	data->tx_send_packets = MLX5_GET64(fpga_query_qp_counters_out, out,
					   tx_send_packets);
	data->rx_total_drop = MLX5_GET64(fpga_query_qp_counters_out, out,
					 rx_total_drop);

	return ret;
}

int mlx5_fpga_shell_counters(struct mlx5_core_dev *dev, bool clear,
			     struct mlx5_fpga_shell_counters *data)
{
	u32 in[MLX5_ST_SZ_DW(fpga_shell_counters)] = {0};
	u32 out[MLX5_ST_SZ_DW(fpga_shell_counters)];
	int err;

	MLX5_SET(fpga_shell_counters, in, clear, clear);
	err = mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out),
				   MLX5_REG_FPGA_SHELL_CNTR, 0, false);
	if (err)
		goto out;
	if (data) {
		data->ddr_read_requests = MLX5_GET64(fpga_shell_counters, out,
						     ddr_read_requests);
		data->ddr_write_requests = MLX5_GET64(fpga_shell_counters, out,
						      ddr_write_requests);
		data->ddr_read_bytes = MLX5_GET64(fpga_shell_counters, out,
						  ddr_read_bytes);
		data->ddr_write_bytes = MLX5_GET64(fpga_shell_counters, out,
						   ddr_write_bytes);
	}

out:
	return err;
}
