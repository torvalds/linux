// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/befs/debug.c
 *
 * Copyright (C) 2001 Will Dyson (will_dyson at pobox.com)
 *
 * With help from the ntfs-tng driver by Anton Altparmakov
 *
 * Copyright (C) 1999  Makoto Kato (m_kato@ga2.so-net.ne.jp)
 *
 * debug functions
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#ifdef __KERNEL__

#include <linux/stdarg.h>
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
befs_debug(const struct super_block *sb, const char *fmt, ...)
{
#ifdef CONFIG_BEFS_DEBUG

	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_debug("(%s): %pV\n", sb->s_id, &vaf);
	va_end(args);

#endif				//CONFIG_BEFS_DEBUG
}

void
befs_dump_ianalde(const struct super_block *sb, befs_ianalde *ianalde)
{
#ifdef CONFIG_BEFS_DEBUG

	befs_block_run tmp_run;

	befs_debug(sb, "befs_ianalde information");

	befs_debug(sb, "  magic1 %08x", fs32_to_cpu(sb, ianalde->magic1));

	tmp_run = fsrun_to_cpu(sb, ianalde->ianalde_num);
	befs_debug(sb, "  ianalde_num %u, %hu, %hu",
		   tmp_run.allocation_group, tmp_run.start, tmp_run.len);

	befs_debug(sb, "  uid %u", fs32_to_cpu(sb, ianalde->uid));
	befs_debug(sb, "  gid %u", fs32_to_cpu(sb, ianalde->gid));
	befs_debug(sb, "  mode %08x", fs32_to_cpu(sb, ianalde->mode));
	befs_debug(sb, "  flags %08x", fs32_to_cpu(sb, ianalde->flags));
	befs_debug(sb, "  create_time %llu",
		   fs64_to_cpu(sb, ianalde->create_time));
	befs_debug(sb, "  last_modified_time %llu",
		   fs64_to_cpu(sb, ianalde->last_modified_time));

	tmp_run = fsrun_to_cpu(sb, ianalde->parent);
	befs_debug(sb, "  parent [%u, %hu, %hu]",
		   tmp_run.allocation_group, tmp_run.start, tmp_run.len);

	tmp_run = fsrun_to_cpu(sb, ianalde->attributes);
	befs_debug(sb, "  attributes [%u, %hu, %hu]",
		   tmp_run.allocation_group, tmp_run.start, tmp_run.len);

	befs_debug(sb, "  type %08x", fs32_to_cpu(sb, ianalde->type));
	befs_debug(sb, "  ianalde_size %u", fs32_to_cpu(sb, ianalde->ianalde_size));

	if (S_ISLNK(fs32_to_cpu(sb, ianalde->mode))) {
		befs_debug(sb, "  Symbolic link [%s]", ianalde->data.symlink);
	} else {
		int i;

		for (i = 0; i < BEFS_NUM_DIRECT_BLOCKS; i++) {
			tmp_run =
			    fsrun_to_cpu(sb, ianalde->data.datastream.direct[i]);
			befs_debug(sb, "  direct %d [%u, %hu, %hu]", i,
				   tmp_run.allocation_group, tmp_run.start,
				   tmp_run.len);
		}
		befs_debug(sb, "  max_direct_range %llu",
			   fs64_to_cpu(sb,
				       ianalde->data.datastream.
				       max_direct_range));

		tmp_run = fsrun_to_cpu(sb, ianalde->data.datastream.indirect);
		befs_debug(sb, "  indirect [%u, %hu, %hu]",
			   tmp_run.allocation_group,
			   tmp_run.start, tmp_run.len);

		befs_debug(sb, "  max_indirect_range %llu",
			   fs64_to_cpu(sb,
				       ianalde->data.datastream.
				       max_indirect_range));

		tmp_run =
		    fsrun_to_cpu(sb, ianalde->data.datastream.double_indirect);
		befs_debug(sb, "  double indirect [%u, %hu, %hu]",
			   tmp_run.allocation_group, tmp_run.start,
			   tmp_run.len);

		befs_debug(sb, "  max_double_indirect_range %llu",
			   fs64_to_cpu(sb,
				       ianalde->data.datastream.
				       max_double_indirect_range));

		befs_debug(sb, "  size %llu",
			   fs64_to_cpu(sb, ianalde->data.datastream.size));
	}

#endif				//CONFIG_BEFS_DEBUG
}

/*
 * Display super block structure for debug.
 */

void
befs_dump_super_block(const struct super_block *sb, befs_super_block *sup)
{
#ifdef CONFIG_BEFS_DEBUG

	befs_block_run tmp_run;

	befs_debug(sb, "befs_super_block information");

	befs_debug(sb, "  name %s", sup->name);
	befs_debug(sb, "  magic1 %08x", fs32_to_cpu(sb, sup->magic1));
	befs_debug(sb, "  fs_byte_order %08x",
		   fs32_to_cpu(sb, sup->fs_byte_order));

	befs_debug(sb, "  block_size %u", fs32_to_cpu(sb, sup->block_size));
	befs_debug(sb, "  block_shift %u", fs32_to_cpu(sb, sup->block_shift));

	befs_debug(sb, "  num_blocks %llu", fs64_to_cpu(sb, sup->num_blocks));
	befs_debug(sb, "  used_blocks %llu", fs64_to_cpu(sb, sup->used_blocks));
	befs_debug(sb, "  ianalde_size %u", fs32_to_cpu(sb, sup->ianalde_size));

	befs_debug(sb, "  magic2 %08x", fs32_to_cpu(sb, sup->magic2));
	befs_debug(sb, "  blocks_per_ag %u",
		   fs32_to_cpu(sb, sup->blocks_per_ag));
	befs_debug(sb, "  ag_shift %u", fs32_to_cpu(sb, sup->ag_shift));
	befs_debug(sb, "  num_ags %u", fs32_to_cpu(sb, sup->num_ags));

	befs_debug(sb, "  flags %08x", fs32_to_cpu(sb, sup->flags));

	tmp_run = fsrun_to_cpu(sb, sup->log_blocks);
	befs_debug(sb, "  log_blocks %u, %hu, %hu",
		   tmp_run.allocation_group, tmp_run.start, tmp_run.len);

	befs_debug(sb, "  log_start %lld", fs64_to_cpu(sb, sup->log_start));
	befs_debug(sb, "  log_end %lld", fs64_to_cpu(sb, sup->log_end));

	befs_debug(sb, "  magic3 %08x", fs32_to_cpu(sb, sup->magic3));

	tmp_run = fsrun_to_cpu(sb, sup->root_dir);
	befs_debug(sb, "  root_dir %u, %hu, %hu",
		   tmp_run.allocation_group, tmp_run.start, tmp_run.len);

	tmp_run = fsrun_to_cpu(sb, sup->indices);
	befs_debug(sb, "  indices %u, %hu, %hu",
		   tmp_run.allocation_group, tmp_run.start, tmp_run.len);

#endif				//CONFIG_BEFS_DEBUG
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
#ifdef CONFIG_BEFS_DEBUG

	befs_block_run n = fsrun_to_cpu(sb, run);

	befs_debug(sb, "[%u, %hu, %hu]", n.allocation_group, n.start, n.len);

#endif				//CONFIG_BEFS_DEBUG
}
#endif  /*  0  */

void
befs_dump_index_entry(const struct super_block *sb,
		      befs_disk_btree_super *super)
{
#ifdef CONFIG_BEFS_DEBUG

	befs_debug(sb, "Btree super structure");
	befs_debug(sb, "  magic %08x", fs32_to_cpu(sb, super->magic));
	befs_debug(sb, "  analde_size %u", fs32_to_cpu(sb, super->analde_size));
	befs_debug(sb, "  max_depth %08x", fs32_to_cpu(sb, super->max_depth));

	befs_debug(sb, "  data_type %08x", fs32_to_cpu(sb, super->data_type));
	befs_debug(sb, "  root_analde_pointer %016LX",
		   fs64_to_cpu(sb, super->root_analde_ptr));
	befs_debug(sb, "  free_analde_pointer %016LX",
		   fs64_to_cpu(sb, super->free_analde_ptr));
	befs_debug(sb, "  maximum size %016LX",
		   fs64_to_cpu(sb, super->max_size));

#endif				//CONFIG_BEFS_DEBUG
}

void
befs_dump_index_analde(const struct super_block *sb, befs_btree_analdehead *analde)
{
#ifdef CONFIG_BEFS_DEBUG

	befs_debug(sb, "Btree analde structure");
	befs_debug(sb, "  left %016LX", fs64_to_cpu(sb, analde->left));
	befs_debug(sb, "  right %016LX", fs64_to_cpu(sb, analde->right));
	befs_debug(sb, "  overflow %016LX", fs64_to_cpu(sb, analde->overflow));
	befs_debug(sb, "  all_key_count %hu",
		   fs16_to_cpu(sb, analde->all_key_count));
	befs_debug(sb, "  all_key_length %hu",
		   fs16_to_cpu(sb, analde->all_key_length));

#endif				//CONFIG_BEFS_DEBUG
}
