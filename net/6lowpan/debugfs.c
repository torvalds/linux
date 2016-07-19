/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors:
 * (C) 2015 Pengutronix, Alexander Aring <aar@pengutronix.de>
 * Copyright (c)  2015 Nordic Semiconductor. All Rights Reserved.
 */

#include <net/6lowpan.h>

#include "6lowpan_i.h"

#define LOWPAN_DEBUGFS_CTX_PFX_NUM_ARGS	8

static struct dentry *lowpan_debugfs;

static int lowpan_ctx_flag_active_set(void *data, u64 val)
{
	struct lowpan_iphc_ctx *ctx = data;

	if (val != 0 && val != 1)
		return -EINVAL;

	if (val)
		set_bit(LOWPAN_IPHC_CTX_FLAG_ACTIVE, &ctx->flags);
	else
		clear_bit(LOWPAN_IPHC_CTX_FLAG_ACTIVE, &ctx->flags);

	return 0;
}

static int lowpan_ctx_flag_active_get(void *data, u64 *val)
{
	*val = lowpan_iphc_ctx_is_active(data);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(lowpan_ctx_flag_active_fops,
			lowpan_ctx_flag_active_get,
			lowpan_ctx_flag_active_set, "%llu\n");

static int lowpan_ctx_flag_c_set(void *data, u64 val)
{
	struct lowpan_iphc_ctx *ctx = data;

	if (val != 0 && val != 1)
		return -EINVAL;

	if (val)
		set_bit(LOWPAN_IPHC_CTX_FLAG_COMPRESSION, &ctx->flags);
	else
		clear_bit(LOWPAN_IPHC_CTX_FLAG_COMPRESSION, &ctx->flags);

	return 0;
}

static int lowpan_ctx_flag_c_get(void *data, u64 *val)
{
	*val = lowpan_iphc_ctx_is_compression(data);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(lowpan_ctx_flag_c_fops, lowpan_ctx_flag_c_get,
			lowpan_ctx_flag_c_set, "%llu\n");

static int lowpan_ctx_plen_set(void *data, u64 val)
{
	struct lowpan_iphc_ctx *ctx = data;
	struct lowpan_iphc_ctx_table *t =
		container_of(ctx, struct lowpan_iphc_ctx_table, table[ctx->id]);

	if (val > 128)
		return -EINVAL;

	spin_lock_bh(&t->lock);
	ctx->plen = val;
	spin_unlock_bh(&t->lock);

	return 0;
}

static int lowpan_ctx_plen_get(void *data, u64 *val)
{
	struct lowpan_iphc_ctx *ctx = data;
	struct lowpan_iphc_ctx_table *t =
		container_of(ctx, struct lowpan_iphc_ctx_table, table[ctx->id]);

	spin_lock_bh(&t->lock);
	*val = ctx->plen;
	spin_unlock_bh(&t->lock);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(lowpan_ctx_plen_fops, lowpan_ctx_plen_get,
			lowpan_ctx_plen_set, "%llu\n");

static int lowpan_ctx_pfx_show(struct seq_file *file, void *offset)
{
	struct lowpan_iphc_ctx *ctx = file->private;
	struct lowpan_iphc_ctx_table *t =
		container_of(ctx, struct lowpan_iphc_ctx_table, table[ctx->id]);

	spin_lock_bh(&t->lock);
	seq_printf(file, "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
		   be16_to_cpu(ctx->pfx.s6_addr16[0]),
		   be16_to_cpu(ctx->pfx.s6_addr16[1]),
		   be16_to_cpu(ctx->pfx.s6_addr16[2]),
		   be16_to_cpu(ctx->pfx.s6_addr16[3]),
		   be16_to_cpu(ctx->pfx.s6_addr16[4]),
		   be16_to_cpu(ctx->pfx.s6_addr16[5]),
		   be16_to_cpu(ctx->pfx.s6_addr16[6]),
		   be16_to_cpu(ctx->pfx.s6_addr16[7]));
	spin_unlock_bh(&t->lock);

	return 0;
}

static int lowpan_ctx_pfx_open(struct inode *inode, struct file *file)
{
	return single_open(file, lowpan_ctx_pfx_show, inode->i_private);
}

static ssize_t lowpan_ctx_pfx_write(struct file *fp,
				    const char __user *user_buf, size_t count,
				    loff_t *ppos)
{
	char buf[128] = {};
	struct seq_file *file = fp->private_data;
	struct lowpan_iphc_ctx *ctx = file->private;
	struct lowpan_iphc_ctx_table *t =
		container_of(ctx, struct lowpan_iphc_ctx_table, table[ctx->id]);
	int status = count, n, i;
	unsigned int addr[8];

	if (copy_from_user(&buf, user_buf, min_t(size_t, sizeof(buf) - 1,
						 count))) {
		status = -EFAULT;
		goto out;
	}

	n = sscanf(buf, "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x",
		   &addr[0], &addr[1], &addr[2], &addr[3], &addr[4],
		   &addr[5], &addr[6], &addr[7]);
	if (n != LOWPAN_DEBUGFS_CTX_PFX_NUM_ARGS) {
		status = -EINVAL;
		goto out;
	}

	spin_lock_bh(&t->lock);
	for (i = 0; i < 8; i++)
		ctx->pfx.s6_addr16[i] = cpu_to_be16(addr[i] & 0xffff);
	spin_unlock_bh(&t->lock);

out:
	return status;
}

static const struct file_operations lowpan_ctx_pfx_fops = {
	.open		= lowpan_ctx_pfx_open,
	.read		= seq_read,
	.write		= lowpan_ctx_pfx_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int lowpan_dev_debugfs_ctx_init(struct net_device *dev,
				       struct dentry *ctx, u8 id)
{
	struct lowpan_priv *lpriv = lowpan_priv(dev);
	struct dentry *dentry, *root;
	char buf[32];

	WARN_ON_ONCE(id > LOWPAN_IPHC_CTX_TABLE_SIZE);

	sprintf(buf, "%d", id);

	root = debugfs_create_dir(buf, ctx);
	if (!root)
		return -EINVAL;

	dentry = debugfs_create_file("active", 0644, root,
				     &lpriv->ctx.table[id],
				     &lowpan_ctx_flag_active_fops);
	if (!dentry)
		return -EINVAL;

	dentry = debugfs_create_file("compression", 0644, root,
				     &lpriv->ctx.table[id],
				     &lowpan_ctx_flag_c_fops);
	if (!dentry)
		return -EINVAL;

	dentry = debugfs_create_file("prefix", 0644, root,
				     &lpriv->ctx.table[id],
				     &lowpan_ctx_pfx_fops);
	if (!dentry)
		return -EINVAL;

	dentry = debugfs_create_file("prefix_len", 0644, root,
				     &lpriv->ctx.table[id],
				     &lowpan_ctx_plen_fops);
	if (!dentry)
		return -EINVAL;

	return 0;
}

static int lowpan_context_show(struct seq_file *file, void *offset)
{
	struct lowpan_iphc_ctx_table *t = file->private;
	int i;

	seq_printf(file, "%3s|%-43s|%c\n", "cid", "prefix", 'C');
	seq_puts(file, "-------------------------------------------------\n");

	spin_lock_bh(&t->lock);
	for (i = 0; i < LOWPAN_IPHC_CTX_TABLE_SIZE; i++) {
		if (!lowpan_iphc_ctx_is_active(&t->table[i]))
			continue;

		seq_printf(file, "%3d|%39pI6c/%-3d|%d\n", t->table[i].id,
			   &t->table[i].pfx, t->table[i].plen,
			   lowpan_iphc_ctx_is_compression(&t->table[i]));
	}
	spin_unlock_bh(&t->lock);

	return 0;
}

static int lowpan_context_open(struct inode *inode, struct file *file)
{
	return single_open(file, lowpan_context_show, inode->i_private);
}

static const struct file_operations lowpan_context_fops = {
	.open		= lowpan_context_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int lowpan_dev_debugfs_init(struct net_device *dev)
{
	struct lowpan_priv *lpriv = lowpan_priv(dev);
	struct dentry *contexts, *dentry;
	int ret, i;

	/* creating the root */
	lpriv->iface_debugfs = debugfs_create_dir(dev->name, lowpan_debugfs);
	if (!lpriv->iface_debugfs)
		goto fail;

	contexts = debugfs_create_dir("contexts", lpriv->iface_debugfs);
	if (!contexts)
		goto remove_root;

	dentry = debugfs_create_file("show", 0644, contexts,
				     &lowpan_priv(dev)->ctx,
				     &lowpan_context_fops);
	if (!dentry)
		goto remove_root;

	for (i = 0; i < LOWPAN_IPHC_CTX_TABLE_SIZE; i++) {
		ret = lowpan_dev_debugfs_ctx_init(dev, contexts, i);
		if (ret < 0)
			goto remove_root;
	}

	return 0;

remove_root:
	lowpan_dev_debugfs_exit(dev);
fail:
	return -EINVAL;
}

void lowpan_dev_debugfs_exit(struct net_device *dev)
{
	debugfs_remove_recursive(lowpan_priv(dev)->iface_debugfs);
}

int __init lowpan_debugfs_init(void)
{
	lowpan_debugfs = debugfs_create_dir("6lowpan", NULL);
	if (!lowpan_debugfs)
		return -EINVAL;

	return 0;
}

void lowpan_debugfs_exit(void)
{
	debugfs_remove_recursive(lowpan_debugfs);
}
