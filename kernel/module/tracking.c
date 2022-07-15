// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Module taint unload tracking support
 *
 * Copyright (C) 2022 Aaron Tomlin
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include "internal.h"

static LIST_HEAD(unloaded_tainted_modules);

int try_add_tainted_module(struct module *mod)
{
	struct mod_unload_taint *mod_taint;

	module_assert_mutex_or_preempt();

	list_for_each_entry_rcu(mod_taint, &unloaded_tainted_modules, list,
				lockdep_is_held(&module_mutex)) {
		if (!strcmp(mod_taint->name, mod->name) &&
		    mod_taint->taints & mod->taints) {
			mod_taint->count++;
			goto out;
		}
	}

	mod_taint = kmalloc(sizeof(*mod_taint), GFP_KERNEL);
	if (unlikely(!mod_taint))
		return -ENOMEM;
	strscpy(mod_taint->name, mod->name, MODULE_NAME_LEN);
	mod_taint->taints = mod->taints;
	list_add_rcu(&mod_taint->list, &unloaded_tainted_modules);
	mod_taint->count = 1;
out:
	return 0;
}

void print_unloaded_tainted_modules(void)
{
	struct mod_unload_taint *mod_taint;
	char buf[MODULE_FLAGS_BUF_SIZE];

	if (!list_empty(&unloaded_tainted_modules)) {
		printk(KERN_DEFAULT "Unloaded tainted modules:");
		list_for_each_entry_rcu(mod_taint, &unloaded_tainted_modules,
					list) {
			size_t l;

			l = module_flags_taint(mod_taint->taints, buf);
			buf[l++] = '\0';
			pr_cont(" %s(%s):%llu", mod_taint->name, buf,
				mod_taint->count);
		}
	}
}
