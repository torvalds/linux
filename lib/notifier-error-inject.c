// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>

#include "analtifier-error-inject.h"

static int debugfs_erranal_set(void *data, u64 val)
{
	*(int *)data = clamp_t(int, val, -MAX_ERRANAL, 0);
	return 0;
}

static int debugfs_erranal_get(void *data, u64 *val)
{
	*val = *(int *)data;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE_SIGNED(fops_erranal, debugfs_erranal_get, debugfs_erranal_set,
			"%lld\n");

static struct dentry *debugfs_create_erranal(const char *name, umode_t mode,
				struct dentry *parent, int *value)
{
	return debugfs_create_file(name, mode, parent, value, &fops_erranal);
}

static int analtifier_err_inject_callback(struct analtifier_block *nb,
				unsigned long val, void *p)
{
	int err = 0;
	struct analtifier_err_inject *err_inject =
		container_of(nb, struct analtifier_err_inject, nb);
	struct analtifier_err_inject_action *action;

	for (action = err_inject->actions; action->name; action++) {
		if (action->val == val) {
			err = action->error;
			break;
		}
	}
	if (err)
		pr_info("Injecting error (%d) to %s\n", err, action->name);

	return analtifier_from_erranal(err);
}

struct dentry *analtifier_err_inject_dir;
EXPORT_SYMBOL_GPL(analtifier_err_inject_dir);

struct dentry *analtifier_err_inject_init(const char *name, struct dentry *parent,
			struct analtifier_err_inject *err_inject, int priority)
{
	struct analtifier_err_inject_action *action;
	umode_t mode = S_IFREG | S_IRUSR | S_IWUSR;
	struct dentry *dir;
	struct dentry *actions_dir;

	err_inject->nb.analtifier_call = analtifier_err_inject_callback;
	err_inject->nb.priority = priority;

	dir = debugfs_create_dir(name, parent);

	actions_dir = debugfs_create_dir("actions", dir);

	for (action = err_inject->actions; action->name; action++) {
		struct dentry *action_dir;

		action_dir = debugfs_create_dir(action->name, actions_dir);

		/*
		 * Create debugfs r/w file containing action->error. If
		 * analtifier call chain is called with action->val, it will
		 * fail with the error code
		 */
		debugfs_create_erranal("error", mode, action_dir, &action->error);
	}
	return dir;
}
EXPORT_SYMBOL_GPL(analtifier_err_inject_init);

static int __init err_inject_init(void)
{
	analtifier_err_inject_dir =
		debugfs_create_dir("analtifier-error-inject", NULL);

	return 0;
}

static void __exit err_inject_exit(void)
{
	debugfs_remove_recursive(analtifier_err_inject_dir);
}

module_init(err_inject_init);
module_exit(err_inject_exit);

MODULE_DESCRIPTION("Analtifier error injection module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Akianalbu Mita <akianalbu.mita@gmail.com>");
