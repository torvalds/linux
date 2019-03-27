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

#include <linux/kernel.h>
#include <linux/module.h>
#include <dev/mlx5/driver.h>
#include "mlx5_core.h"

#ifdef RATELIMIT

/* Finds an entry where we can register the given rate
 * If the rate already exists, return the entry where it is registered,
 * otherwise return the first available entry.
 * If the table is full, return NULL
 */
static struct mlx5_rl_entry *find_rl_entry(struct mlx5_rl_table *table,
					   u32 rate, u16 burst)
{
	struct mlx5_rl_entry *ret_entry = NULL;
	struct mlx5_rl_entry *entry;
	u16 i;

	for (i = 0; i < table->max_size; i++) {
		entry = table->rl_entry + i;
		if (entry->rate == rate && entry->burst == burst)
			return entry;
		if (ret_entry == NULL && entry->rate == 0)
			ret_entry = entry;
	}

	return ret_entry;
}

static int mlx5_set_rate_limit_cmd(struct mlx5_core_dev *dev,
				   u32 rate, u32 burst, u16 index)
{
	u32 in[MLX5_ST_SZ_DW(set_rate_limit_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(set_rate_limit_out)] = {0};

	MLX5_SET(set_rate_limit_in, in, opcode,
		 MLX5_CMD_OP_SET_RATE_LIMIT);
	MLX5_SET(set_rate_limit_in, in, rate_limit_index, index);
	MLX5_SET(set_rate_limit_in, in, rate_limit, rate);

	if (MLX5_CAP_QOS(dev, packet_pacing_burst_bound))
		MLX5_SET(set_rate_limit_in, in, burst_upper_bound, burst);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

bool mlx5_rl_is_in_range(const struct mlx5_core_dev *dev, u32 rate, u32 burst)
{
	const struct mlx5_rl_table *table = &dev->priv.rl_table;

	return (rate <= table->max_rate && rate >= table->min_rate &&
		burst <= 65535);
}
EXPORT_SYMBOL(mlx5_rl_is_in_range);

int mlx5_rl_add_rate(struct mlx5_core_dev *dev, u32 rate, u32 burst, u16 *index)
{
	struct mlx5_rl_table *table = &dev->priv.rl_table;
	struct mlx5_rl_entry *entry;
	int err = 0;

	mutex_lock(&table->rl_lock);

	if (!rate || !mlx5_rl_is_in_range(dev, rate, burst)) {
		mlx5_core_err(dev, "Invalid rate: %u, should be %u to %u\n",
			      rate, table->min_rate, table->max_rate);
		err = -ERANGE;
		goto out;
	}

	entry = find_rl_entry(table, rate, burst);
	if (!entry) {
		mlx5_core_err(dev, "Max number of %u rates reached\n",
			      table->max_size);
		err = -ENOSPC;
		goto out;
	}
	if (entry->refcount == 0xFFFFFFFFU) {
		/* out of refcounts */
		err = -ENOMEM;
		goto out;
	} else if (entry->refcount != 0) {
		/* rate already configured */
		entry->refcount++;
	} else {
		/* new rate limit */
		err = mlx5_set_rate_limit_cmd(dev, rate, burst, entry->index);
		if (err) {
			mlx5_core_err(dev, "Failed configuring rate: %u (%d)\n",
				      rate, err);
			goto out;
		}
		entry->rate = rate;
		entry->burst = burst;
		entry->refcount = 1;
	}
	*index = entry->index;

out:
	mutex_unlock(&table->rl_lock);
	return err;
}
EXPORT_SYMBOL(mlx5_rl_add_rate);

void mlx5_rl_remove_rate(struct mlx5_core_dev *dev, u32 rate, u32 burst)
{
	struct mlx5_rl_table *table = &dev->priv.rl_table;
	struct mlx5_rl_entry *entry = NULL;

	/* 0 is a reserved value for unlimited rate */
	if (rate == 0)
		return;

	mutex_lock(&table->rl_lock);
	entry = find_rl_entry(table, rate, burst);
	if (!entry || !entry->refcount) {
		mlx5_core_warn(dev, "Rate %u is not configured\n", rate);
		goto out;
	}

	entry->refcount--;
	if (!entry->refcount) {
		/* need to remove rate */
		mlx5_set_rate_limit_cmd(dev, 0, 0, entry->index);
		entry->rate = 0;
		entry->burst = 0;
	}

out:
	mutex_unlock(&table->rl_lock);
}
EXPORT_SYMBOL(mlx5_rl_remove_rate);

int mlx5_init_rl_table(struct mlx5_core_dev *dev)
{
	struct mlx5_rl_table *table = &dev->priv.rl_table;
	int i;

	mutex_init(&table->rl_lock);
	if (!MLX5_CAP_GEN(dev, qos) || !MLX5_CAP_QOS(dev, packet_pacing)) {
		table->max_size = 0;
		return 0;
	}

	/* First entry is reserved for unlimited rate */
	table->max_size = MLX5_CAP_QOS(dev, packet_pacing_rate_table_size) - 1;
	table->max_rate = MLX5_CAP_QOS(dev, packet_pacing_max_rate);
	table->min_rate = MLX5_CAP_QOS(dev, packet_pacing_min_rate);

	table->rl_entry = kcalloc(table->max_size, sizeof(struct mlx5_rl_entry),
				  GFP_KERNEL);
	if (!table->rl_entry)
		return -ENOMEM;

	/* The index represents the index in HW rate limit table
	 * Index 0 is reserved for unlimited rate
	 */
	for (i = 0; i < table->max_size; i++)
		table->rl_entry[i].index = i + 1;

	return 0;
}

void mlx5_cleanup_rl_table(struct mlx5_core_dev *dev)
{
	struct mlx5_rl_table *table = &dev->priv.rl_table;
	int i;

	/* Clear all configured rates */
	for (i = 0; i < table->max_size; i++)
		if (table->rl_entry[i].rate)
			mlx5_set_rate_limit_cmd(dev, 0, 0,
						table->rl_entry[i].index);

	kfree(dev->priv.rl_table.rl_entry);
}

#endif
