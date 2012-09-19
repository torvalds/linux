#include <linux/module.h>

#include "notifier-error-inject.h"

static int debugfs_errno_set(void *data, u64 val)
{
	*(int *)data = clamp_t(int, val, -MAX_ERRNO, 0);
	return 0;
}

static int debugfs_errno_get(void *data, u64 *val)
{
	*val = *(int *)data;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_errno, debugfs_errno_get, debugfs_errno_set,
			"%lld\n");

static struct dentry *debugfs_create_errno(const char *name, mode_t mode,
				struct dentry *parent, int *value)
{
	return debugfs_create_file(name, mode, parent, value, &fops_errno);
}

static int notifier_err_inject_callback(struct notifier_block *nb,
				unsigned long val, void *p)
{
	int err = 0;
	struct notifier_err_inject *err_inject =
		container_of(nb, struct notifier_err_inject, nb);
	struct notifier_err_inject_action *action;

	for (action = err_inject->actions; action->name; action++) {
		if (action->val == val) {
			err = action->error;
			break;
		}
	}
	if (err)
		pr_info("Injecting error (%d) to %s\n", err, action->name);

	return notifier_from_errno(err);
}

struct dentry *notifier_err_inject_dir;
EXPORT_SYMBOL_GPL(notifier_err_inject_dir);

struct dentry *notifier_err_inject_init(const char *name, struct dentry *parent,
			struct notifier_err_inject *err_inject, int priority)
{
	struct notifier_err_inject_action *action;
	mode_t mode = S_IFREG | S_IRUSR | S_IWUSR;
	struct dentry *dir;
	struct dentry *actions_dir;

	err_inject->nb.notifier_call = notifier_err_inject_callback;
	err_inject->nb.priority = priority;

	dir = debugfs_create_dir(name, parent);
	if (!dir)
		return ERR_PTR(-ENOMEM);

	actions_dir = debugfs_create_dir("actions", dir);
	if (!actions_dir)
		goto fail;

	for (action = err_inject->actions; action->name; action++) {
		struct dentry *action_dir;

		action_dir = debugfs_create_dir(action->name, actions_dir);
		if (!action_dir)
			goto fail;

		/*
		 * Create debugfs r/w file containing action->error. If
		 * notifier call chain is called with action->val, it will
		 * fail with the error code
		 */
		if (!debugfs_create_errno("error", mode, action_dir,
					&action->error))
			goto fail;
	}
	return dir;
fail:
	debugfs_remove_recursive(dir);
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL_GPL(notifier_err_inject_init);

static int __init err_inject_init(void)
{
	notifier_err_inject_dir =
		debugfs_create_dir("notifier-error-inject", NULL);

	if (!notifier_err_inject_dir)
		return -ENOMEM;

	return 0;
}

static void __exit err_inject_exit(void)
{
	debugfs_remove_recursive(notifier_err_inject_dir);
}

module_init(err_inject_init);
module_exit(err_inject_exit);

MODULE_DESCRIPTION("Notifier error injection module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Akinobu Mita <akinobu.mita@gmail.com>");
