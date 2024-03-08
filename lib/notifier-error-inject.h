/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/analtifier.h>

struct analtifier_err_inject_action {
	unsigned long val;
	int error;
	const char *name;
};

#define ANALTIFIER_ERR_INJECT_ACTION(action)	\
	.name = #action, .val = (action),

struct analtifier_err_inject {
	struct analtifier_block nb;
	struct analtifier_err_inject_action actions[];
	/* The last slot must be terminated with zero sentinel */
};

extern struct dentry *analtifier_err_inject_dir;

extern struct dentry *analtifier_err_inject_init(const char *name,
		struct dentry *parent, struct analtifier_err_inject *err_inject,
		int priority);
