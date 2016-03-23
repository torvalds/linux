/*
 * livepatch.h - Kernel Live Patching Core
 *
 * Copyright (C) 2014 Seth Jennings <sjenning@redhat.com>
 * Copyright (C) 2014 SUSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _LINUX_LIVEPATCH_H_
#define _LINUX_LIVEPATCH_H_

#include <linux/module.h>
#include <linux/ftrace.h>

#if IS_ENABLED(CONFIG_LIVEPATCH)

#include <asm/livepatch.h>

enum klp_state {
	KLP_DISABLED,
	KLP_ENABLED
};

/**
 * struct klp_func - function structure for live patching
 * @old_name:	name of the function to be patched
 * @new_func:	pointer to the patched function code
 * @old_sympos: a hint indicating which symbol position the old function
 *		can be found (optional)
 * @old_addr:	the address of the function being patched
 * @kobj:	kobject for sysfs resources
 * @state:	tracks function-level patch application state
 * @stack_node:	list node for klp_ops func_stack list
 */
struct klp_func {
	/* external */
	const char *old_name;
	void *new_func;
	/*
	 * The old_sympos field is optional and can be used to resolve
	 * duplicate symbol names in livepatch objects. If this field is zero,
	 * it is expected the symbol is unique, otherwise patching fails. If
	 * this value is greater than zero then that occurrence of the symbol
	 * in kallsyms for the given object is used.
	 */
	unsigned long old_sympos;

	/* internal */
	unsigned long old_addr;
	struct kobject kobj;
	enum klp_state state;
	struct list_head stack_node;
};

/**
 * struct klp_object - kernel object structure for live patching
 * @name:	module name (or NULL for vmlinux)
 * @funcs:	function entries for functions to be patched in the object
 * @kobj:	kobject for sysfs resources
 * @mod:	kernel module associated with the patched object
 * 		(NULL for vmlinux)
 * @state:	tracks object-level patch application state
 */
struct klp_object {
	/* external */
	const char *name;
	struct klp_func *funcs;

	/* internal */
	struct kobject kobj;
	struct module *mod;
	enum klp_state state;
};

/**
 * struct klp_patch - patch structure for live patching
 * @mod:	reference to the live patch module
 * @objs:	object entries for kernel objects to be patched
 * @list:	list node for global list of registered patches
 * @kobj:	kobject for sysfs resources
 * @state:	tracks patch-level application state
 */
struct klp_patch {
	/* external */
	struct module *mod;
	struct klp_object *objs;

	/* internal */
	struct list_head list;
	struct kobject kobj;
	enum klp_state state;
};

#define klp_for_each_object(patch, obj) \
	for (obj = patch->objs; obj->funcs; obj++)

#define klp_for_each_func(obj, func) \
	for (func = obj->funcs; func->old_name; func++)

int klp_register_patch(struct klp_patch *);
int klp_unregister_patch(struct klp_patch *);
int klp_enable_patch(struct klp_patch *);
int klp_disable_patch(struct klp_patch *);

/* Called from the module loader during module coming/going states */
int klp_module_coming(struct module *mod);
void klp_module_going(struct module *mod);

#else /* !CONFIG_LIVEPATCH */

static inline int klp_module_coming(struct module *mod) { return 0; }
static inline void klp_module_going(struct module *mod) { }

#endif /* CONFIG_LIVEPATCH */

#endif /* _LINUX_LIVEPATCH_H_ */
