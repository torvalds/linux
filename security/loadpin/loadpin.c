// SPDX-License-Identifier: GPL-2.0-only
/*
 * Module and Firmware Pinning Security Module
 *
 * Copyright 2011-2016 Google Inc.
 *
 * Author: Kees Cook <keescook@chromium.org>
 */

#define pr_fmt(fmt) "LoadPin: " fmt

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/lsm_hooks.h>
#include <linux/mount.h>
#include <linux/path.h>
#include <linux/sched.h>	/* current */
#include <linux/string_helpers.h>

static void report_load(const char *origin, struct file *file, char *operation)
{
	char *cmdline, *pathname;

	pathname = kstrdup_quotable_file(file, GFP_KERNEL);
	cmdline = kstrdup_quotable_cmdline(current, GFP_KERNEL);

	pr_notice("%s %s obj=%s%s%s pid=%d cmdline=%s%s%s\n",
		  origin, operation,
		  (pathname && pathname[0] != '<') ? "\"" : "",
		  pathname,
		  (pathname && pathname[0] != '<') ? "\"" : "",
		  task_pid_nr(current),
		  cmdline ? "\"" : "", cmdline, cmdline ? "\"" : "");

	kfree(cmdline);
	kfree(pathname);
}

static int enforce = IS_ENABLED(CONFIG_SECURITY_LOADPIN_ENFORCE);
static char *exclude_read_files[READING_MAX_ID];
static int ignore_read_file_id[READING_MAX_ID] __ro_after_init;
static struct super_block *pinned_root;
static DEFINE_SPINLOCK(pinned_root_spinlock);

#ifdef CONFIG_SYSCTL
static int zero;
static int one = 1;

static struct ctl_path loadpin_sysctl_path[] = {
	{ .procname = "kernel", },
	{ .procname = "loadpin", },
	{ }
};

static struct ctl_table loadpin_sysctl_table[] = {
	{
		.procname       = "enforce",
		.data           = &enforce,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec_minmax,
		.extra1         = &zero,
		.extra2         = &one,
	},
	{ }
};

/*
 * This must be called after early kernel init, since then the rootdev
 * is available.
 */
static void check_pinning_enforcement(struct super_block *mnt_sb)
{
	bool ro = false;

	/*
	 * If load pinning is not enforced via a read-only block
	 * device, allow sysctl to change modes for testing.
	 */
	if (mnt_sb->s_bdev) {
		char bdev[BDEVNAME_SIZE];

		ro = bdev_read_only(mnt_sb->s_bdev);
		bdevname(mnt_sb->s_bdev, bdev);
		pr_info("%s (%u:%u): %s\n", bdev,
			MAJOR(mnt_sb->s_bdev->bd_dev),
			MINOR(mnt_sb->s_bdev->bd_dev),
			ro ? "read-only" : "writable");
	} else
		pr_info("mnt_sb lacks block device, treating as: writable\n");

	if (!ro) {
		if (!register_sysctl_paths(loadpin_sysctl_path,
					   loadpin_sysctl_table))
			pr_notice("sysctl registration failed!\n");
		else
			pr_info("enforcement can be disabled.\n");
	} else
		pr_info("load pinning engaged.\n");
}
#else
static void check_pinning_enforcement(struct super_block *mnt_sb)
{
	pr_info("load pinning engaged.\n");
}
#endif

static void loadpin_sb_free_security(struct super_block *mnt_sb)
{
	/*
	 * When unmounting the filesystem we were using for load
	 * pinning, we acknowledge the superblock release, but make sure
	 * no other modules or firmware can be loaded.
	 */
	if (!IS_ERR_OR_NULL(pinned_root) && mnt_sb == pinned_root) {
		pinned_root = ERR_PTR(-EIO);
		pr_info("umount pinned fs: refusing further loads\n");
	}
}

static int loadpin_read_file(struct file *file, enum kernel_read_file_id id)
{
	struct super_block *load_root;
	const char *origin = kernel_read_file_id_str(id);

	/* If the file id is excluded, ignore the pinning. */
	if ((unsigned int)id < ARRAY_SIZE(ignore_read_file_id) &&
	    ignore_read_file_id[id]) {
		report_load(origin, file, "pinning-excluded");
		return 0;
	}

	/* This handles the older init_module API that has a NULL file. */
	if (!file) {
		if (!enforce) {
			report_load(origin, NULL, "old-api-pinning-ignored");
			return 0;
		}

		report_load(origin, NULL, "old-api-denied");
		return -EPERM;
	}

	load_root = file->f_path.mnt->mnt_sb;

	/* First loaded module/firmware defines the root for all others. */
	spin_lock(&pinned_root_spinlock);
	/*
	 * pinned_root is only NULL at startup. Otherwise, it is either
	 * a valid reference, or an ERR_PTR.
	 */
	if (!pinned_root) {
		pinned_root = load_root;
		/*
		 * Unlock now since it's only pinned_root we care about.
		 * In the worst case, we will (correctly) report pinning
		 * failures before we have announced that pinning is
		 * enforcing. This would be purely cosmetic.
		 */
		spin_unlock(&pinned_root_spinlock);
		check_pinning_enforcement(pinned_root);
		report_load(origin, file, "pinned");
	} else {
		spin_unlock(&pinned_root_spinlock);
	}

	if (IS_ERR_OR_NULL(pinned_root) || load_root != pinned_root) {
		if (unlikely(!enforce)) {
			report_load(origin, file, "pinning-ignored");
			return 0;
		}

		report_load(origin, file, "denied");
		return -EPERM;
	}

	return 0;
}

static int loadpin_load_data(enum kernel_load_data_id id)
{
	return loadpin_read_file(NULL, (enum kernel_read_file_id) id);
}

static struct security_hook_list loadpin_hooks[] __lsm_ro_after_init = {
	LSM_HOOK_INIT(sb_free_security, loadpin_sb_free_security),
	LSM_HOOK_INIT(kernel_read_file, loadpin_read_file),
	LSM_HOOK_INIT(kernel_load_data, loadpin_load_data),
};

static void __init parse_exclude(void)
{
	int i, j;
	char *cur;

	/*
	 * Make sure all the arrays stay within expected sizes. This
	 * is slightly weird because kernel_read_file_str[] includes
	 * READING_MAX_ID, which isn't actually meaningful here.
	 */
	BUILD_BUG_ON(ARRAY_SIZE(exclude_read_files) !=
		     ARRAY_SIZE(ignore_read_file_id));
	BUILD_BUG_ON(ARRAY_SIZE(kernel_read_file_str) <
		     ARRAY_SIZE(ignore_read_file_id));

	for (i = 0; i < ARRAY_SIZE(exclude_read_files); i++) {
		cur = exclude_read_files[i];
		if (!cur)
			break;
		if (*cur == '\0')
			continue;

		for (j = 0; j < ARRAY_SIZE(ignore_read_file_id); j++) {
			if (strcmp(cur, kernel_read_file_str[j]) == 0) {
				pr_info("excluding: %s\n",
					kernel_read_file_str[j]);
				ignore_read_file_id[j] = 1;
				/*
				 * Can not break, because one read_file_str
				 * may map to more than on read_file_id.
				 */
			}
		}
	}
}

static int __init loadpin_init(void)
{
	pr_info("ready to pin (currently %senforcing)\n",
		enforce ? "" : "not ");
	parse_exclude();
	security_add_hooks(loadpin_hooks, ARRAY_SIZE(loadpin_hooks), "loadpin");
	return 0;
}

DEFINE_LSM(loadpin) = {
	.name = "loadpin",
	.init = loadpin_init,
};

/* Should not be mutable after boot, so not listed in sysfs (perm == 0). */
module_param(enforce, int, 0);
MODULE_PARM_DESC(enforce, "Enforce module/firmware pinning");
module_param_array_named(exclude, exclude_read_files, charp, NULL, 0);
MODULE_PARM_DESC(exclude, "Exclude pinning specific read file types");
