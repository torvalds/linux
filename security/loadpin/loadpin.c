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
#include <linux/kernel_read_file.h>
#include <linux/lsm_hooks.h>
#include <linux/mount.h>
#include <linux/blkdev.h>
#include <linux/path.h>
#include <linux/sched.h>	/* current */
#include <linux/string_helpers.h>
#include <linux/dm-verity-loadpin.h>
#include <uapi/linux/loadpin.h>

#define VERITY_DIGEST_FILE_HEADER "# LOADPIN_TRUSTED_VERITY_ROOT_DIGESTS"

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
#ifdef CONFIG_SECURITY_LOADPIN_VERITY
static bool deny_reading_verity_digests;
#endif

#ifdef CONFIG_SYSCTL
static struct ctl_table loadpin_sysctl_table[] = {
	{
		.procname       = "enforce",
		.data           = &enforce,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec_minmax,
		.extra1         = SYSCTL_ONE,
		.extra2         = SYSCTL_ONE,
	},
	{ }
};

static void set_sysctl(bool is_writable)
{
	/*
	 * If load pinning is not enforced via a read-only block
	 * device, allow sysctl to change modes for testing.
	 */
	if (is_writable)
		loadpin_sysctl_table[0].extra1 = SYSCTL_ZERO;
	else
		loadpin_sysctl_table[0].extra1 = SYSCTL_ONE;
}
#else
static inline void set_sysctl(bool is_writable) { }
#endif

static void report_writable(struct super_block *mnt_sb, bool writable)
{
	if (mnt_sb->s_bdev) {
		pr_info("%pg (%u:%u): %s\n", mnt_sb->s_bdev,
			MAJOR(mnt_sb->s_bdev->bd_dev),
			MINOR(mnt_sb->s_bdev->bd_dev),
			writable ? "writable" : "read-only");
	} else
		pr_info("mnt_sb lacks block device, treating as: writable\n");

	if (!writable)
		pr_info("load pinning engaged.\n");
}

/*
 * This must be called after early kernel init, since then the rootdev
 * is available.
 */
static bool sb_is_writable(struct super_block *mnt_sb)
{
	bool writable = true;

	if (mnt_sb->s_bdev)
		writable = !bdev_read_only(mnt_sb->s_bdev);

	return writable;
}

static void loadpin_sb_free_security(struct super_block *mnt_sb)
{
	/*
	 * When unmounting the filesystem we were using for load
	 * pinning, we acknowledge the superblock release, but make sure
	 * no other modules or firmware can be loaded when we are in
	 * enforcing mode. Otherwise, allow the root to be reestablished.
	 */
	if (!IS_ERR_OR_NULL(pinned_root) && mnt_sb == pinned_root) {
		if (enforce) {
			pinned_root = ERR_PTR(-EIO);
			pr_info("umount pinned fs: refusing further loads\n");
		} else {
			pinned_root = NULL;
		}
	}
}

static int loadpin_check(struct file *file, enum kernel_read_file_id id)
{
	struct super_block *load_root;
	const char *origin = kernel_read_file_id_str(id);
	bool first_root_pin = false;
	bool load_root_writable;

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
	load_root_writable = sb_is_writable(load_root);

	/* First loaded module/firmware defines the root for all others. */
	spin_lock(&pinned_root_spinlock);
	/*
	 * pinned_root is only NULL at startup or when the pinned root has
	 * been unmounted while we are not in enforcing mode. Otherwise, it
	 * is either a valid reference, or an ERR_PTR.
	 */
	if (!pinned_root) {
		pinned_root = load_root;
		first_root_pin = true;
	}
	spin_unlock(&pinned_root_spinlock);

	if (first_root_pin) {
		report_writable(pinned_root, load_root_writable);
		set_sysctl(load_root_writable);
		report_load(origin, file, "pinned");
	}

	if (IS_ERR_OR_NULL(pinned_root) ||
	    ((load_root != pinned_root) && !dm_verity_loadpin_is_bdev_trusted(load_root->s_bdev))) {
		if (unlikely(!enforce)) {
			report_load(origin, file, "pinning-ignored");
			return 0;
		}

		report_load(origin, file, "denied");
		return -EPERM;
	}

	return 0;
}

static int loadpin_read_file(struct file *file, enum kernel_read_file_id id,
			     bool contents)
{
	/*
	 * LoadPin only cares about the _origin_ of a file, not its
	 * contents, so we can ignore the "are full contents available"
	 * argument here.
	 */
	return loadpin_check(file, id);
}

static int loadpin_load_data(enum kernel_load_data_id id, bool contents)
{
	/*
	 * LoadPin only cares about the _origin_ of a file, not its
	 * contents, so a NULL file is passed, and we can ignore the
	 * state of "contents".
	 */
	return loadpin_check(NULL, (enum kernel_read_file_id) id);
}

static struct security_hook_list loadpin_hooks[] __ro_after_init = {
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
#ifdef CONFIG_SYSCTL
	if (!register_sysctl("kernel/loadpin", loadpin_sysctl_table))
		pr_notice("sysctl registration failed!\n");
#endif
	security_add_hooks(loadpin_hooks, ARRAY_SIZE(loadpin_hooks), "loadpin");

	return 0;
}

DEFINE_LSM(loadpin) = {
	.name = "loadpin",
	.init = loadpin_init,
};

#ifdef CONFIG_SECURITY_LOADPIN_VERITY

enum loadpin_securityfs_interface_index {
	LOADPIN_DM_VERITY,
};

static int read_trusted_verity_root_digests(unsigned int fd)
{
	struct fd f;
	void *data;
	int rc;
	char *p, *d;

	if (deny_reading_verity_digests)
		return -EPERM;

	/* The list of trusted root digests can only be set up once */
	if (!list_empty(&dm_verity_loadpin_trusted_root_digests))
		return -EPERM;

	f = fdget(fd);
	if (!f.file)
		return -EINVAL;

	data = kzalloc(SZ_4K, GFP_KERNEL);
	if (!data) {
		rc = -ENOMEM;
		goto err;
	}

	rc = kernel_read_file(f.file, 0, (void **)&data, SZ_4K - 1, NULL, READING_POLICY);
	if (rc < 0)
		goto err;

	p = data;
	p[rc] = '\0';
	p = strim(p);

	p = strim(data);
	while ((d = strsep(&p, "\n")) != NULL) {
		int len;
		struct dm_verity_loadpin_trusted_root_digest *trd;

		if (d == data) {
			/* first line, validate header */
			if (strcmp(d, VERITY_DIGEST_FILE_HEADER)) {
				rc = -EPROTO;
				goto err;
			}

			continue;
		}

		len = strlen(d);

		if (len % 2) {
			rc = -EPROTO;
			goto err;
		}

		len /= 2;

		trd = kzalloc(struct_size(trd, data, len), GFP_KERNEL);
		if (!trd) {
			rc = -ENOMEM;
			goto err;
		}
		trd->len = len;

		if (hex2bin(trd->data, d, len)) {
			kfree(trd);
			rc = -EPROTO;
			goto err;
		}

		list_add_tail(&trd->node, &dm_verity_loadpin_trusted_root_digests);
	}

	if (list_empty(&dm_verity_loadpin_trusted_root_digests)) {
		rc = -EPROTO;
		goto err;
	}

	kfree(data);
	fdput(f);

	return 0;

err:
	kfree(data);

	/* any failure in loading/parsing invalidates the entire list */
	{
		struct dm_verity_loadpin_trusted_root_digest *trd, *tmp;

		list_for_each_entry_safe(trd, tmp, &dm_verity_loadpin_trusted_root_digests, node) {
			list_del(&trd->node);
			kfree(trd);
		}
	}

	/* disallow further attempts after reading a corrupt/invalid file */
	deny_reading_verity_digests = true;

	fdput(f);

	return rc;
}

/******************************** securityfs ********************************/

static long dm_verity_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	void __user *uarg = (void __user *)arg;
	unsigned int fd;

	switch (cmd) {
	case LOADPIN_IOC_SET_TRUSTED_VERITY_DIGESTS:
		if (copy_from_user(&fd, uarg, sizeof(fd)))
			return -EFAULT;

		return read_trusted_verity_root_digests(fd);

	default:
		return -EINVAL;
	}
}

static const struct file_operations loadpin_dm_verity_ops = {
	.unlocked_ioctl = dm_verity_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

/**
 * init_loadpin_securityfs - create the securityfs directory for LoadPin
 *
 * We can not put this method normally under the loadpin_init() code path since
 * the security subsystem gets initialized before the vfs caches.
 *
 * Returns 0 if the securityfs directory creation was successful.
 */
static int __init init_loadpin_securityfs(void)
{
	struct dentry *loadpin_dir, *dentry;

	loadpin_dir = securityfs_create_dir("loadpin", NULL);
	if (IS_ERR(loadpin_dir)) {
		pr_err("LoadPin: could not create securityfs dir: %ld\n",
		       PTR_ERR(loadpin_dir));
		return PTR_ERR(loadpin_dir);
	}

	dentry = securityfs_create_file("dm-verity", 0600, loadpin_dir,
					(void *)LOADPIN_DM_VERITY, &loadpin_dm_verity_ops);
	if (IS_ERR(dentry)) {
		pr_err("LoadPin: could not create securityfs entry 'dm-verity': %ld\n",
		       PTR_ERR(dentry));
		return PTR_ERR(dentry);
	}

	return 0;
}

fs_initcall(init_loadpin_securityfs);

#endif /* CONFIG_SECURITY_LOADPIN_VERITY */

/* Should not be mutable after boot, so not listed in sysfs (perm == 0). */
module_param(enforce, int, 0);
MODULE_PARM_DESC(enforce, "Enforce module/firmware pinning");
module_param_array_named(exclude, exclude_read_files, charp, NULL, 0);
MODULE_PARM_DESC(exclude, "Exclude pinning specific read file types");
