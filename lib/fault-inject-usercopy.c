// SPDX-License-Identifier: GPL-2.0-only
#include <linux/fault-inject.h>
#include <linux/fault-inject-usercopy.h>

static struct {
	struct fault_attr attr;
} fail_usercopy = {
	.attr = FAULT_ATTR_INITIALIZER,
};

static int __init setup_fail_usercopy(char *str)
{
	return setup_fault_attr(&fail_usercopy.attr, str);
}
__setup("fail_usercopy=", setup_fail_usercopy);

#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS

static int __init fail_usercopy_debugfs(void)
{
	struct dentry *dir;

	dir = fault_create_debugfs_attr("fail_usercopy", NULL,
					&fail_usercopy.attr);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	return 0;
}

late_initcall(fail_usercopy_debugfs);

#endif /* CONFIG_FAULT_INJECTION_DEBUG_FS */

bool should_fail_usercopy(void)
{
	return should_fail(&fail_usercopy.attr, 1);
}
EXPORT_SYMBOL_GPL(should_fail_usercopy);
