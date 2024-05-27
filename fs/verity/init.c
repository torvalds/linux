// SPDX-License-Identifier: GPL-2.0
/*
 * fs-verity module initialization and logging
 *
 * Copyright 2019 Google LLC
 */

#include "fsverity_private.h"

#include <linux/ratelimit.h>

#ifdef CONFIG_SYSCTL
static struct ctl_table fsverity_sysctl_table[] = {
#ifdef CONFIG_FS_VERITY_BUILTIN_SIGNATURES
	{
		.procname       = "require_signatures",
		.data           = &fsverity_require_signatures,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec_minmax,
		.extra1         = SYSCTL_ZERO,
		.extra2         = SYSCTL_ONE,
	},
#endif
};

static void __init fsverity_init_sysctl(void)
{
	register_sysctl_init("fs/verity", fsverity_sysctl_table);
}
#else /* CONFIG_SYSCTL */
static inline void fsverity_init_sysctl(void)
{
}
#endif /* !CONFIG_SYSCTL */

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
	fsverity_check_hash_algs();
	fsverity_init_info_cache();
	fsverity_init_workqueue();
	fsverity_init_sysctl();
	fsverity_init_signature();
	fsverity_init_bpf();
	return 0;
}
late_initcall(fsverity_init)
