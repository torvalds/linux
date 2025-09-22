// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/fs.h>
#include <drm/drm_print.h>

#include "gt/intel_gt_debugfs.h"
#include "intel_guc.h"
#include "intel_guc_log.h"
#include "intel_guc_log_debugfs.h"
#include "intel_uc.h"

static u32 obj_to_guc_log_dump_size(struct drm_i915_gem_object *obj)
{
	u32 size;

	if (!obj)
		return PAGE_SIZE;

	/* "0x%08x 0x%08x 0x%08x 0x%08x\n" => 16 bytes -> 44 chars => x2.75 */
	size = ((obj->base.size * 11) + 3) / 4;

	/* Add padding for final blank line, any extra header info, etc. */
	size = PAGE_ALIGN(size + PAGE_SIZE);

	return size;
}

static u32 guc_log_dump_size(struct intel_guc_log *log)
{
	struct intel_guc *guc = log_to_guc(log);

	if (!intel_guc_is_supported(guc))
		return PAGE_SIZE;

	if (!log->vma)
		return PAGE_SIZE;

	return obj_to_guc_log_dump_size(log->vma->obj);
}

#ifdef notyet

static int guc_log_dump_show(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);
	int ret;

	ret = intel_guc_log_dump(m->private, &p, false);

	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM) && seq_has_overflowed(m))
		pr_warn_once("preallocated size:%zx for %s exceeded\n",
			     m->size, __func__);

	return ret;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE_WITH_SIZE(guc_log_dump, guc_log_dump_size);

static u32 guc_load_err_dump_size(struct intel_guc_log *log)
{
	struct intel_guc *guc = log_to_guc(log);
	struct intel_uc *uc = container_of(guc, struct intel_uc, guc);

	if (!intel_guc_is_supported(guc))
		return PAGE_SIZE;

	return obj_to_guc_log_dump_size(uc->load_err_log);
}

static int guc_load_err_log_dump_show(struct seq_file *m, void *data)
{
	struct drm_printer p = drm_seq_file_printer(m);

	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM) && seq_has_overflowed(m))
		pr_warn_once("preallocated size:%zx for %s exceeded\n",
			     m->size, __func__);

	return intel_guc_log_dump(m->private, &p, true);
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE_WITH_SIZE(guc_load_err_log_dump, guc_load_err_dump_size);

static int guc_log_level_get(void *data, u64 *val)
{
	struct intel_guc_log *log = data;

	if (!log->vma)
		return -ENODEV;

	*val = intel_guc_log_get_level(log);

	return 0;
}

static int guc_log_level_set(void *data, u64 val)
{
	struct intel_guc_log *log = data;

	if (!log->vma)
		return -ENODEV;

	return intel_guc_log_set_level(log, val);
}

DEFINE_SIMPLE_ATTRIBUTE(guc_log_level_fops,
			guc_log_level_get, guc_log_level_set,
			"%lld\n");

static int guc_log_relay_open(struct inode *inode, struct file *file)
{
	struct intel_guc_log *log = inode->i_private;

	if (!intel_guc_is_ready(log_to_guc(log)))
		return -ENODEV;

	file->private_data = log;

	return intel_guc_log_relay_open(log);
}

static ssize_t
guc_log_relay_write(struct file *filp,
		    const char __user *ubuf,
		    size_t cnt,
		    loff_t *ppos)
{
	struct intel_guc_log *log = filp->private_data;
	int val;
	int ret;

	ret = kstrtoint_from_user(ubuf, cnt, 0, &val);
	if (ret < 0)
		return ret;

	/*
	 * Enable and start the guc log relay on value of 1.
	 * Flush log relay for any other value.
	 */
	if (val == 1)
		ret = intel_guc_log_relay_start(log);
	else
		intel_guc_log_relay_flush(log);

	return ret ?: cnt;
}

static int guc_log_relay_release(struct inode *inode, struct file *file)
{
	struct intel_guc_log *log = inode->i_private;

	intel_guc_log_relay_close(log);
	return 0;
}

static const struct file_operations guc_log_relay_fops = {
	.owner = THIS_MODULE,
	.open = guc_log_relay_open,
	.write = guc_log_relay_write,
	.release = guc_log_relay_release,
};

#endif /* notyet */

void intel_guc_log_debugfs_register(struct intel_guc_log *log,
				    struct dentry *root)
{
	STUB();
#ifdef notyet
	static const struct intel_gt_debugfs_file files[] = {
		{ "guc_log_dump", &guc_log_dump_fops, NULL },
		{ "guc_load_err_log_dump", &guc_load_err_log_dump_fops, NULL },
		{ "guc_log_level", &guc_log_level_fops, NULL },
		{ "guc_log_relay", &guc_log_relay_fops, NULL },
	};

	if (!intel_guc_is_supported(log_to_guc(log)))
		return;

	intel_gt_debugfs_register_files(root, files, ARRAY_SIZE(files), log);
#endif
}
