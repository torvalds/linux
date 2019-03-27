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

#include <linux/kernel.h>
#include <linux/module.h>
#include <dev/mlx5/driver.h>
#include "mlx5_core.h"

static int mlx5_relaxed_ordering_write;
SYSCTL_INT(_hw_mlx5, OID_AUTO, relaxed_ordering_write, CTLFLAG_RWTUN,
    &mlx5_relaxed_ordering_write, 0,
    "Set to enable relaxed ordering for PCIe writes");

void mlx5_init_mr_table(struct mlx5_core_dev *dev)
{
	struct mlx5_mr_table *table = &dev->priv.mr_table;

	memset(table, 0, sizeof(*table));
	spin_lock_init(&table->lock);
	INIT_RADIX_TREE(&table->tree, GFP_ATOMIC);
}

void mlx5_cleanup_mr_table(struct mlx5_core_dev *dev)
{
}

int mlx5_core_create_mkey_cb(struct mlx5_core_dev *dev,
			     struct mlx5_core_mr *mkey,
			     u32 *in, int inlen,
			     u32 *out, int outlen,
			     mlx5_cmd_cbk_t callback, void *context)
{
	struct mlx5_mr_table *table = &dev->priv.mr_table;
	u32 lout[MLX5_ST_SZ_DW(create_mkey_out)] = {0};
	u32 mkey_index;
	void *mkc;
	unsigned long flags;
	int err;
	u8 key;

	spin_lock_irq(&dev->priv.mkey_lock);
	key = dev->priv.mkey_key++;
	spin_unlock_irq(&dev->priv.mkey_lock);
	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(create_mkey_in, in, opcode, MLX5_CMD_OP_CREATE_MKEY);
	MLX5_SET(mkc, mkc, mkey_7_0, key);

	if (mlx5_relaxed_ordering_write != 0) {
		if (MLX5_CAP_GEN(dev, relaxed_ordering_write))
			MLX5_SET(mkc, mkc, relaxed_ordering_write, 1);
		else
			return (-EPROTONOSUPPORT);
	}

	if (callback)
		return mlx5_cmd_exec_cb(dev, in, inlen, out, outlen,
					callback, context);

	err = mlx5_cmd_exec(dev, in, inlen, lout, sizeof(lout));
	if (err) {
		mlx5_core_dbg(dev, "cmd exec failed %d\n", err);
		return err;
	}

	mkey_index = MLX5_GET(create_mkey_out, lout, mkey_index);
	mkey->iova = MLX5_GET64(mkc, mkc, start_addr);
	mkey->size = MLX5_GET64(mkc, mkc, len);
	mkey->key = mlx5_idx_to_mkey(mkey_index) | key;
	mkey->pd = MLX5_GET(mkc, mkc, pd);

	mlx5_core_dbg(dev, "out 0x%x, key 0x%x, mkey 0x%x\n",
		      mkey_index, key, mkey->key);

	/* connect to MR tree */
	spin_lock_irqsave(&table->lock, flags);
	err = radix_tree_insert(&table->tree, mlx5_mkey_to_idx(mkey->key), mkey);
	spin_unlock_irqrestore(&table->lock, flags);
	if (err) {
		mlx5_core_warn(dev, "failed radix tree insert of mr 0x%x, %d\n",
			       mkey->key, err);
		mlx5_core_destroy_mkey(dev, mkey);
	}

	return err;
}
EXPORT_SYMBOL(mlx5_core_create_mkey_cb);

int mlx5_core_create_mkey(struct mlx5_core_dev *dev,
			  struct mlx5_core_mr *mkey,
			  u32 *in, int inlen)
{
	return mlx5_core_create_mkey_cb(dev, mkey, in, inlen,
					NULL, 0, NULL, NULL);
}
EXPORT_SYMBOL(mlx5_core_create_mkey);

int mlx5_core_destroy_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *mkey)
{
	struct mlx5_mr_table *table = &dev->priv.mr_table;
	u32 out[MLX5_ST_SZ_DW(destroy_mkey_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(destroy_mkey_in)] = {0};
	struct mlx5_core_mr *deleted_mr;
	unsigned long flags;

	spin_lock_irqsave(&table->lock, flags);
	deleted_mr = radix_tree_delete(&table->tree, mlx5_mkey_to_idx(mkey->key));
	spin_unlock_irqrestore(&table->lock, flags);
	if (!deleted_mr) {
		mlx5_core_warn(dev, "failed radix tree delete of mr 0x%x\n", mkey->key);
		return -ENOENT;
	}

	MLX5_SET(destroy_mkey_in, in, opcode, MLX5_CMD_OP_DESTROY_MKEY);
	MLX5_SET(destroy_mkey_in, in, mkey_index, mlx5_mkey_to_idx(mkey->key));

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_destroy_mkey);

int mlx5_core_query_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *mkey,
			 u32 *out, int outlen)
{
	u32 in[MLX5_ST_SZ_DW(query_mkey_in)] = {0};

	memset(out, 0, outlen);
	MLX5_SET(query_mkey_in, in, opcode, MLX5_CMD_OP_QUERY_MKEY);
	MLX5_SET(query_mkey_in, in, mkey_index, mlx5_mkey_to_idx(mkey->key));

	return mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
}
EXPORT_SYMBOL(mlx5_core_query_mkey);

int mlx5_core_dump_fill_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *_mkey,
			     u32 *mkey)
{
	u32 out[MLX5_ST_SZ_DW(query_special_contexts_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(query_special_contexts_in)]   = {0};
	int err;

	MLX5_SET(query_special_contexts_in, in, opcode,
		 MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*mkey = MLX5_GET(query_special_contexts_out, out, dump_fill_mkey);

	return err;
}
EXPORT_SYMBOL(mlx5_core_dump_fill_mkey);

static inline u32 mlx5_get_psv(u32 *out, int psv_index)
{
	switch (psv_index) {
	case 1: return MLX5_GET(create_psv_out, out, psv1_index);
	case 2: return MLX5_GET(create_psv_out, out, psv2_index);
	case 3: return MLX5_GET(create_psv_out, out, psv3_index);
	default: return MLX5_GET(create_psv_out, out, psv0_index);
	}
}

int mlx5_core_create_psv(struct mlx5_core_dev *dev, u32 pdn,
			 int npsvs, u32 *sig_index)
{
	u32 out[MLX5_ST_SZ_DW(create_psv_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(create_psv_in)]   = {0};
	int i, err;

	if (npsvs > MLX5_MAX_PSVS)
		return -EINVAL;

	MLX5_SET(create_psv_in, in, opcode, MLX5_CMD_OP_CREATE_PSV);
	MLX5_SET(create_psv_in, in, pd, pdn);
	MLX5_SET(create_psv_in, in, num_psv, npsvs);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err) {
		mlx5_core_err(dev, "create_psv cmd exec failed %d\n", err);
		return err;
	}

	for (i = 0; i < npsvs; i++)
		sig_index[i] = mlx5_get_psv(out, i);

	return err;
}
EXPORT_SYMBOL(mlx5_core_create_psv);

int mlx5_core_destroy_psv(struct mlx5_core_dev *dev, int psv_num)
{
	u32 out[MLX5_ST_SZ_DW(destroy_psv_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(destroy_psv_in)]	= {0};

	MLX5_SET(destroy_psv_in, in, opcode, MLX5_CMD_OP_DESTROY_PSV);
	MLX5_SET(destroy_psv_in, in, psvn, psv_num);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_core_destroy_psv);
