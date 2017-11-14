/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __842_DEBUGFS_H__
#define __842_DEBUGFS_H__

#include <linux/debugfs.h>

static bool sw842_template_counts;
module_param_named(template_counts, sw842_template_counts, bool, 0444);

static atomic_t template_count[OPS_MAX], template_repeat_count,
	template_zeros_count, template_short_data_count, template_end_count;

static struct dentry *sw842_debugfs_root;

static int __init sw842_debugfs_create(void)
{
	umode_t m = S_IRUGO | S_IWUSR;
	int i;

	if (!debugfs_initialized())
		return -ENODEV;

	sw842_debugfs_root = debugfs_create_dir(MODULE_NAME, NULL);
	if (IS_ERR(sw842_debugfs_root))
		return PTR_ERR(sw842_debugfs_root);

	for (i = 0; i < ARRAY_SIZE(template_count); i++) {
		char name[32];

		snprintf(name, 32, "template_%02x", i);
		debugfs_create_atomic_t(name, m, sw842_debugfs_root,
					&template_count[i]);
	}
	debugfs_create_atomic_t("template_repeat", m, sw842_debugfs_root,
				&template_repeat_count);
	debugfs_create_atomic_t("template_zeros", m, sw842_debugfs_root,
				&template_zeros_count);
	debugfs_create_atomic_t("template_short_data", m, sw842_debugfs_root,
				&template_short_data_count);
	debugfs_create_atomic_t("template_end", m, sw842_debugfs_root,
				&template_end_count);

	return 0;
}

static void __exit sw842_debugfs_remove(void)
{
	if (sw842_debugfs_root && !IS_ERR(sw842_debugfs_root))
		debugfs_remove_recursive(sw842_debugfs_root);
}

#endif
