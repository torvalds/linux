// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/befs/de.c
 *
 * Copyright (C) 2001 Will Dyson (will_dyson at pobox.com)
 *
 * With help from the ntfs-tng driver by Anton Altparmakov
 *
 * Copyright (C) 1999  Makoto Kato (m_kato@ga2.so-net.ne.jp)
 *
 * de functions
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#ifdef __KERNEL__

#include <stdarg.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>

#endif				/* __KERNEL__ */

#include "befs.h"

void
befs_error(const struct super_block *sb, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_err("(%s): %pV\n", sb->s_id, &vaf);
	va_end(args);
}

void
befs_warning(const struct super_block *sb, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_warn("(%s): %pV\n", sb->s_id, &vaf);
	va_end(args);
}

void
befs_de(const struct super_block *sb, const char *fmt, ...)
{
#ifdef CONFIG_BEFS_DE

	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_de("(%s): %pV\n", sb->s_id, &vaf);
	va_end(args);

#endif				//CONFIG_BEFS_DE
}

void
befs_dump_inode(const struct super_block *sb, befs_inode *inode)
{
#ifdef CONFIG_BEFS_DE

	befs_block_run tmp_run;

	befs_de(sb, "befs_inode information");

	befs_de(sb, "  magic1 %08x", fs32_to_cpu(sb, inode->magic1));

	tmp_run = fsrun_to_cpu(sb, inode->inode_num);
	befs_de(sb, "  inode_num %u, %hu, %hu",
		   tmp_run.allocation_group, tmp_run.start, tmp_run.len);

	befs_de(sb, "  uid %u", fs32_to_cpu(sb, inode->uid));
	befs_de(sb, "  gid %u", fs32_to_cpu(sb, inode->gid));
	befs_de(sb, "  mode %08x", fs32_to_cpu(sb, inode->mode));
	befs_de(sb, "  flags %08x", fs32_to_cpu(sb, inode->flags));
	befs_de(sb, "  create_time %llu",
		   fs64_to_cpu(sb, inode->create_time));
	befs_de(sb, "  last_modified_time %llu",
		   fs64_to_cpu(sb, inode->last_modified_time));

	tmp_run = fsrun_to_cpu(sb, inode->parent);
	befs_de(sb, "  parent [%u, %hu, %hu]",
		   tmp_run.allocation_group, tmp_run.start, tmp_run.len);

	tmp_run = fsrun_to_cpu(sb, inode->attributes);
	befs_de(sb, "  attributes [%u, %hu, %hu]",
		   tmp_run.allocation_group, tmp_run.start, tmp_run.len);

	befs_de(sb, "  type %08x", fs32_to_cpu(sb, inode->type));
	befs_de(sb, "  inode_size %u", fs32_to_cpu(sb, inode->inode_size));

	if (S_ISLNK(fs32_to_cpu(sb, inode->mode))) {
		befs_de(sb, "  Symbolic link [%s]", inode->data.symlink);
	} else {
		int i;

		for (i = 0; i < BEFS_NUM_DIRECT_BLOCKS; i++) {
			tmp_run =
			    fsrun_to_cpu(sb, inode->data.datastream.direct[i]);
			befs_de(sb, "  direct %d [%u, %hu, %hu]", i,
				   tmp_run.allocation_group, tmp_run.start,
				   tmp_run.len);
		}
		befs_de(sb, "  max_direct_range %llu",
			   fs64_to_cpu(sb,
				       inode->data.datastream.
				       max_direct_range));

		tmp_run = fsrun_to_cpu(sb, inode->data.datastream.indirect);
		befs_de(sb, "  indirect [%u, %hu, %hu]",
			   tmp_run.allocation_group,
			   tmp_run.start, tmp_run.len);

		befs_de(sb, "  max_indirect_range %llu",
			   fs64_to_cpu(sb,
				       inode->data.datastream.
				       max_indirect_range));

		tmp_run =
		    fsrun_to_cpu(sb, inode->data.datastream.double_indirect);
		befs_de(sb, "  double indirect [%u, %hu, %hu]",
			   tmp_run.allocation_group, tmp_run.start,
			   tmp_run.len);

		befs_de(sb, "  max_double_indirect_range %llu",
			   fs64_to_cpu(sb,
				       inode->data.datastream.
				       max_double_indirect_range));

		befs_de(sb, "  size %llu",
			   fs64_to_cpu(sb, inode->data.datastream.size));
	}

#endif				//CONFIG_BEFS_DE
}

/*
 * Display super block structure for de.
 */

void
befs_dump_super_block(const struct super_block *sb, befs_super_block *sup)
{
#ifdef CONFIG_BEFS_DE

	befs_block_run tmp_run;

	befs_de(sb, "befs_super_block information");

	befs_de(sb, "  name %s", sup->name);
	befs_de(sb, "  magic1 %08x", fs32_to_cpu(sb, sup->magic1));
	befs_de(sb, "  fs_byte_order %08x",
		   fs32_to_cpu(sb, sup->fs_byte_order));

	befs_de(sb, "  block_size %u", fs32_to_cpu(sb, sup->block_size));
	befs_de(sb, "  block_shift %u", fs32_to_cpu(sb, sup->block_shift));

	befs_de(sb, "  num_blocks %llu", fs64_to_cpu(sb, sup->num_blocks));
	befs_de(sb, "  used_blocks %llu", fs64_to_cpu(sb, sup->used_blocks));
	befs_de(sb, "  inode_size %u", fs32_to_cpu(sb, sup->inode_size));

	befs_de(sb, "  magic2 %08x", fs32_to_cpu(sb, sup->magic2));
	befs_de(sb, "  blocks_per_ag %u",
		   fs32_to_cpu(sb, sup->blocks_per_ag));
	befs_de(sb, "  ag_shift %u", fs32_to_cpu(sb, sup->ag_shift));
	befs_de(sb, "  num_ags %u", fs32_to_cpu(sb, sup->num_ags));

	befs_de(sb, "  flags %08x", fs32_to_cpu(sb, sup->flags));

	tmp_run = fsrun_to_cpu(sb, sup->log_blocks);
	befs_de(sb, "  log_blocks %u, %hu, %hu",
		   tmp_run.allocation_group, tmp_run.start, tmp_run.len);

	befs_de(sb, "  log_start %lld", fs64_to_cpu(sb, sup->log_start));
	befs_de(sb, "  log_end %lld", fs64_to_cpu(sb, sup->log_end));

	befs_de(sb, "  magic3 %08x", fs32_to_cpu(sb, sup->magic3));

	tmp_run = fsrun_to_cpu(sb, sup->root_dir);
	befs_de(sb, "  root_dir %u, %hu, %hu",
		   tmp_run.allocation_group, tmp_run.start, tmp_run.len);

	tmp_run = fsrun_to_cpu(sb, sup->indices);
	befs_de(sb, "  indices %u, %hu, %hu",
		   tmp_run.allocation_group, tmp_run.start, tmp_run.len);

#endif				//CONFIG_BEFS_DE
}

#if 0
/* unused */
void
befs_dump_small_data(const struct super_block *sb, befs_small_data *sd)
{
}

/* unused */
void
befs_dump_run(const struct super_block *sb, befs_disk_block_run run)
{
#ifdef CONFIG_BEFS_DE

	befs_block_run n = fsrun_to_cpu(sb, run);

	befs_de(sb, "[%u, %hu, %hu]", n.allocation_group, n.start, n.len);

#endif				//CONFIG_BEFS_DE
}
#endif  /*  0  */

void
befs_dump_index_entry(const struct super_block *sb,
		      befs_disk_btree_super *super)
{
#ifdef CONFIG_BEFS_DE

	befs_de(sb, "Btree super structure");
	befs_de(sb, "  magic %08x", fs32_to_cpu(sb, super->magic));
	befs_de(sb, "  node_size %u", fs32_to_cpu(sb, super->node_size));
	befs_de(sb, "  max_depth %08x", fs32_to_cpu(sb, super->max_depth));

	befs_de(sb, "  data_type %08x", fs32_to_cpu(sb, super->data_type));
	befs_de(sb, "  root_node_pointer %016LX",
		   fs64_to_cpu(sb, super->root_node_ptr));
	befs_de(sb, "  free_node_pointer %016LX",
		   fs64_to_cpu(sb, super->free_node_ptr));
	befs_de(sb, "  maximum size %016LX",
		   fs64_to_cpu(sb, super->max_size));

#endif				//CONFIG_BEFS_DE
}

void
befs_dump_index_node(const struct super_block *sb, befs_btree_nodehead *node)
{
#ifdef CONFIG_BEFS_DE

	befs_de(sb, "Btree node structure");
	befs_de(sb, "  left %016LX", fs64_to_cpu(sb, node->left));
	befs_de(sb, "  right %016LX", fs64_to_cpu(sb, node->right));
	befs_de(sb, "  overflow %016LX", fs64_to_cpu(sb, node->overflow));
	befs_de(sb, "  all_key_count %hu",
		   fs16_to_cpu(sb, node->all_key_count));
	befs_de(sb, "  all_key_length %hu",
		   fs16_to_cpu(sb, node->all_key_length));

#endif				//CONFIG_BEFS_DE
}
