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

#include <linux/types.h>
#include <linux/module.h>
#include <dev/mlx5/mlx5_ifc.h>
#include <dev/mlx5/device.h>
#include <dev/mlx5/fs.h>

#include "fs_core.h"
#include "mlx5_core.h"

int mlx5_cmd_update_root_ft(struct mlx5_core_dev *dev,
			    enum fs_ft_type type,
			    unsigned int id)
{
	u32 in[MLX5_ST_SZ_DW(set_flow_table_root_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(set_flow_table_root_out)] = {0};

	if (!dev)
		return -EINVAL;

	MLX5_SET(set_flow_table_root_in, in, opcode,
		 MLX5_CMD_OP_SET_FLOW_TABLE_ROOT);
	MLX5_SET(set_flow_table_root_in, in, table_type, type);
	MLX5_SET(set_flow_table_root_in, in, table_id, id);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_cmd_fs_create_ft(struct mlx5_core_dev *dev,
			  u16 vport,
			  enum fs_ft_type type, unsigned int level,
			  unsigned int log_size, unsigned int *table_id)
{
	u32 in[MLX5_ST_SZ_DW(create_flow_table_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(create_flow_table_out)] = {0};
	int err;

	if (!dev)
		return -EINVAL;

	MLX5_SET(create_flow_table_in, in, opcode,
		 MLX5_CMD_OP_CREATE_FLOW_TABLE);

	MLX5_SET(create_flow_table_in, in, table_type, type);
	MLX5_SET(create_flow_table_in, in, flow_table_context.level, level);
	MLX5_SET(create_flow_table_in, in, flow_table_context.log_size,
		 log_size);
	if (vport) {
		MLX5_SET(create_flow_table_in, in, vport_number, vport);
		MLX5_SET(create_flow_table_in, in, other_vport, 1);
	}

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*table_id = MLX5_GET(create_flow_table_out, out, table_id);

	return err;
}

int mlx5_cmd_fs_destroy_ft(struct mlx5_core_dev *dev,
			   u16 vport,
			   enum fs_ft_type type, unsigned int table_id)
{
	u32 in[MLX5_ST_SZ_DW(destroy_flow_table_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_flow_table_out)] = {0};

	if (!dev)
		return -EINVAL;

	MLX5_SET(destroy_flow_table_in, in, opcode,
		 MLX5_CMD_OP_DESTROY_FLOW_TABLE);
	MLX5_SET(destroy_flow_table_in, in, table_type, type);
	MLX5_SET(destroy_flow_table_in, in, table_id, table_id);
	if (vport) {
		MLX5_SET(destroy_flow_table_in, in, vport_number, vport);
		MLX5_SET(destroy_flow_table_in, in, other_vport, 1);
	}

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_cmd_fs_create_fg(struct mlx5_core_dev *dev,
			  u32 *in,
			  u16 vport,
			  enum fs_ft_type type, unsigned int table_id,
			  unsigned int *group_id)
{
	u32 out[MLX5_ST_SZ_DW(create_flow_group_out)] = {0};
	int err;
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	if (!dev)
		return -EINVAL;

	MLX5_SET(create_flow_group_in, in, opcode,
		 MLX5_CMD_OP_CREATE_FLOW_GROUP);
	MLX5_SET(create_flow_group_in, in, table_type, type);
	MLX5_SET(create_flow_group_in, in, table_id, table_id);
	if (vport) {
		MLX5_SET(create_flow_group_in, in, vport_number, vport);
		MLX5_SET(create_flow_group_in, in, other_vport, 1);
	}

	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (!err)
		*group_id = MLX5_GET(create_flow_group_out, out, group_id);

	return err;
}

int mlx5_cmd_fs_destroy_fg(struct mlx5_core_dev *dev,
			   u16 vport,
			   enum fs_ft_type type, unsigned int table_id,
			   unsigned int group_id)
{
	u32 in[MLX5_ST_SZ_DW(destroy_flow_group_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_flow_group_out)] = {0};

	if (!dev)
		return -EINVAL;

	MLX5_SET(destroy_flow_group_in, in, opcode,
		 MLX5_CMD_OP_DESTROY_FLOW_GROUP);
	MLX5_SET(destroy_flow_group_in, in, table_type, type);
	MLX5_SET(destroy_flow_group_in, in, table_id,   table_id);
	MLX5_SET(destroy_flow_group_in, in, group_id, group_id);
	if (vport) {
		MLX5_SET(destroy_flow_group_in, in, vport_number, vport);
		MLX5_SET(destroy_flow_group_in, in, other_vport, 1);
	}

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_cmd_fs_set_fte(struct mlx5_core_dev *dev,
			u16 vport,
			enum fs_fte_status *fte_status,
			u32 *match_val,
			enum fs_ft_type type, unsigned int table_id,
			unsigned int index, unsigned int group_id,
			unsigned int flow_tag,
			unsigned short action, int dest_size,
			struct list_head *dests)  /* mlx5_flow_desination */
{
	u32 out[MLX5_ST_SZ_DW(set_fte_out)] = {0};
	u32 *in;
	unsigned int inlen;
	struct mlx5_flow_rule *dst;
	void *in_flow_context;
	void *in_match_value;
	void *in_dests;
	int err;
	int opmod = 0;
	int modify_mask = 0;
	int atomic_mod_cap;

	if (action != MLX5_FLOW_CONTEXT_ACTION_FWD_DEST)
		dest_size = 0;

	inlen = MLX5_ST_SZ_BYTES(set_fte_in) +
		dest_size * MLX5_ST_SZ_BYTES(dest_format_struct);

	if (!dev)
		return -EINVAL;

	if (*fte_status & FS_FTE_STATUS_EXISTING) {
		atomic_mod_cap = MLX5_CAP_FLOWTABLE(dev,
						    flow_table_properties_nic_receive.
						    flow_modify_en);
		if (!atomic_mod_cap)
			return -ENOTSUPP;
		opmod = 1;
		modify_mask = 1 <<
			MLX5_SET_FTE_MODIFY_ENABLE_MASK_DESTINATION_LIST;
	}

	in = mlx5_vzalloc(inlen);
	if (!in) {
		mlx5_core_warn(dev, "failed to allocate inbox\n");
		return -ENOMEM;
	}

	MLX5_SET(set_fte_in, in, opcode, MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY);
	MLX5_SET(set_fte_in, in, op_mod, opmod);
	MLX5_SET(set_fte_in, in, modify_enable_mask, modify_mask);
	MLX5_SET(set_fte_in, in, table_type, type);
	MLX5_SET(set_fte_in, in, table_id,   table_id);
	MLX5_SET(set_fte_in, in, flow_index, index);
	if (vport) {
		MLX5_SET(set_fte_in, in, vport_number, vport);
		MLX5_SET(set_fte_in, in, other_vport, 1);
	}

	in_flow_context = MLX5_ADDR_OF(set_fte_in, in, flow_context);
	MLX5_SET(flow_context, in_flow_context, group_id, group_id);
	MLX5_SET(flow_context, in_flow_context, flow_tag, flow_tag);
	MLX5_SET(flow_context, in_flow_context, action, action);
	MLX5_SET(flow_context, in_flow_context, destination_list_size,
		 dest_size);
	in_match_value = MLX5_ADDR_OF(flow_context, in_flow_context,
				      match_value);
	memcpy(in_match_value, match_val, MLX5_ST_SZ_BYTES(fte_match_param));
	if (dest_size) {
		in_dests = MLX5_ADDR_OF(flow_context, in_flow_context, destination);
		list_for_each_entry(dst, dests, base.list) {
			unsigned int id;

			MLX5_SET(dest_format_struct, in_dests, destination_type,
				 dst->dest_attr.type);
			if (dst->dest_attr.type ==
				MLX5_FLOW_CONTEXT_DEST_TYPE_FLOW_TABLE)
				id = dst->dest_attr.ft->id;
			else
				id = dst->dest_attr.tir_num;
			MLX5_SET(dest_format_struct, in_dests, destination_id, id);
			in_dests += MLX5_ST_SZ_BYTES(dest_format_struct);
		}
	}

	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (!err)
		*fte_status |= FS_FTE_STATUS_EXISTING;

	kvfree(in);

	return err;
}

int mlx5_cmd_fs_delete_fte(struct mlx5_core_dev *dev,
			   u16 vport,
			   enum fs_fte_status *fte_status,
			   enum fs_ft_type type, unsigned int table_id,
			   unsigned int index)
{
	u32 in[MLX5_ST_SZ_DW(delete_fte_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(delete_fte_out)] = {0};
	int err;

	if (!(*fte_status & FS_FTE_STATUS_EXISTING))
		return 0;

	if (!dev)
		return -EINVAL;

	MLX5_SET(delete_fte_in, in, opcode, MLX5_CMD_OP_DELETE_FLOW_TABLE_ENTRY);
	MLX5_SET(delete_fte_in, in, table_type, type);
	MLX5_SET(delete_fte_in, in, table_id, table_id);
	MLX5_SET(delete_fte_in, in, flow_index, index);
	if (vport) {
		MLX5_SET(delete_fte_in, in, vport_number,  vport);
		MLX5_SET(delete_fte_in, in, other_vport, 1);
	}

	err =  mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*fte_status = 0;

	return err;
}
