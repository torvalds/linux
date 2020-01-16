/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/yestifier.h>

struct yestifier_err_inject_action {
	unsigned long val;
	int error;
	const char *name;
};

#define NOTIFIER_ERR_INJECT_ACTION(action)	\
	.name = #action, .val = (action),

struct yestifier_err_inject {
	struct yestifier_block nb;
	struct yestifier_err_inject_action actions[];
	/* The last slot must be terminated with zero sentinel */
};

extern struct dentry *yestifier_err_inject_dir;

extern struct dentry *yestifier_err_inject_init(const char *name,
		struct dentry *parent, struct yestifier_err_inject *err_inject,
		int priority);
