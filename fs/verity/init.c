// SPDX-License-Identifier: GPL-2.0
/*
 * fs-verity module initialization and logging
 *
 * Copyright 2019 Google LLC
 */

#include "fsverity_private.h"

#include <linux/ratelimit.h>

void fsverity_msg(const struct inode *inode, const char *level,
		  const char *fmt, ...)
{
	static DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);
	struct va_format vaf;
	va_list args;

	if (!__ratelimit(&rs))
		return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	if (inode)
		printk("%sfs-verity (%s, inode %lu): %pV\n",
		       level, inode->i_sb->s_id, inode->i_ino, &vaf);
	else
		printk("%sfs-verity: %pV\n", level, &vaf);
	va_end(args);
}

static int __init fsverity_init(void)
{
	int err;

	fsverity_check_hash_algs();

	err = fsverity_init_info_cache();
	if (err)
		return err;

	err = fsverity_init_workqueue();
	if (err)
		goto err_exit_info_cache;

	err = fsverity_init_signature();
	if (err)
		goto err_exit_workqueue;

	pr_debug("Initialized fs-verity\n");
	return 0;

err_exit_workqueue:
	fsverity_exit_workqueue();
err_exit_info_cache:
	fsverity_exit_info_cache();
	return err;
}
late_initcall(fsverity_init)
