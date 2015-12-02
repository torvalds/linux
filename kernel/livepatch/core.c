/*
 * core.c - Kernel Live Patching Core
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/ftrace.h>
#include <linux/list.h>
#include <linux/kallsyms.h>
#include <linux/livepatch.h>

/**
 * struct klp_ops - structure for tracking registered ftrace ops structs
 *
 * A single ftrace_ops is shared between all enabled replacement functions
 * (klp_func structs) which have the same old_addr.  This allows the switch
 * between function versions to happen instantaneously by updating the klp_ops
 * struct's func_stack list.  The winner is the klp_func at the top of the
 * func_stack (front of the list).
 *
 * @node:	node for the global klp_ops list
 * @func_stack:	list head for the stack of klp_func's (active func is on top)
 * @fops:	registered ftrace ops struct
 */
struct klp_ops {
	struct list_head node;
	struct list_head func_stack;
	struct ftrace_ops fops;
};

/*
 * The klp_mutex protects the global lists and state transitions of any
 * structure reachable from them.  References to any structure must be obtained
 * under mutex protection (except in klp_ftrace_handler(), which uses RCU to
 * ensure it gets consistent data).
 */
static DEFINE_MUTEX(klp_mutex);

static LIST_HEAD(klp_patches);
static LIST_HEAD(klp_ops);

static struct kobject *klp_root_kobj;

static struct klp_ops *klp_find_ops(unsigned long old_addr)
{
	struct klp_ops *ops;
	struct klp_func *func;

	list_for_each_entry(ops, &klp_ops, node) {
		func = list_first_entry(&ops->func_stack, struct klp_func,
					stack_node);
		if (func->old_addr == old_addr)
			return ops;
	}

	return NULL;
}

static bool klp_is_module(struct klp_object *obj)
{
	return obj->name;
}

static bool klp_is_object_loaded(struct klp_object *obj)
{
	return !obj->name || obj->mod;
}

/* sets obj->mod if object is not vmlinux and module is found */
static void klp_find_object_module(struct klp_object *obj)
{
	struct module *mod;

	if (!klp_is_module(obj))
		return;

	mutex_lock(&module_mutex);
	/*
	 * We do not want to block removal of patched modules and therefore
	 * we do not take a reference here. The patches are removed by
	 * a going module handler instead.
	 */
	mod = find_module(obj->name);
	/*
	 * Do not mess work of the module coming and going notifiers.
	 * Note that the patch might still be needed before the going handler
	 * is called. Module functions can be called even in the GOING state
	 * until mod->exit() finishes. This is especially important for
	 * patches that modify semantic of the functions.
	 */
	if (mod && mod->klp_alive)
		obj->mod = mod;

	mutex_unlock(&module_mutex);
}

/* klp_mutex must be held by caller */
static bool klp_is_patch_registered(struct klp_patch *patch)
{
	struct klp_patch *mypatch;

	list_for_each_entry(mypatch, &klp_patches, list)
		if (mypatch == patch)
			return true;

	return false;
}

static bool klp_initialized(void)
{
	return !!klp_root_kobj;
}

struct klp_find_arg {
	const char *objname;
	const char *name;
	unsigned long addr;
	unsigned long count;
	unsigned long pos;
};

static int klp_find_callback(void *data, const char *name,
			     struct module *mod, unsigned long addr)
{
	struct klp_find_arg *args = data;

	if ((mod && !args->objname) || (!mod && args->objname))
		return 0;

	if (strcmp(args->name, name))
		return 0;

	if (args->objname && strcmp(args->objname, mod->name))
		return 0;

	args->addr = addr;
	args->count++;

	/*
	 * Finish the search when the symbol is found for the desired position
	 * or the position is not defined for a non-unique symbol.
	 */
	if ((args->pos && (args->count == args->pos)) ||
	    (!args->pos && (args->count > 1)))
		return 1;

	return 0;
}

static int klp_find_object_symbol(const char *objname, const char *name,
				  unsigned long sympos, unsigned long *addr)
{
	struct klp_find_arg args = {
		.objname = objname,
		.name = name,
		.addr = 0,
		.count = 0,
		.pos = sympos,
	};

	mutex_lock(&module_mutex);
	kallsyms_on_each_symbol(klp_find_callback, &args);
	mutex_unlock(&module_mutex);

	/*
	 * Ensure an address was found. If sympos is 0, ensure symbol is unique;
	 * otherwise ensure the symbol position count matches sympos.
	 */
	if (args.addr == 0)
		pr_err("symbol '%s' not found in symbol table\n", name);
	else if (args.count > 1 && sympos == 0) {
		pr_err("unresolvable ambiguity (%lu matches) on symbol '%s' in object '%s'\n",
		       args.count, name, objname);
	} else if (sympos != args.count && sympos > 0) {
		pr_err("symbol position %lu for symbol '%s' in object '%s' not found\n",
		       sympos, name, objname ? objname : "vmlinux");
	} else {
		*addr = args.addr;
		return 0;
	}

	*addr = 0;
	return -EINVAL;
}

/*
 * external symbols are located outside the parent object (where the parent
 * object is either vmlinux or the kmod being patched).
 */
static int klp_find_external_symbol(struct module *pmod, const char *name,
				    unsigned long *addr)
{
	const struct kernel_symbol *sym;

	/* first, check if it's an exported symbol */
	preempt_disable();
	sym = find_symbol(name, NULL, NULL, true, true);
	if (sym) {
		*addr = sym->value;
		preempt_enable();
		return 0;
	}
	preempt_enable();

	/*
	 * Check if it's in another .o within the patch module. This also
	 * checks that the external symbol is unique.
	 */
	return klp_find_object_symbol(pmod->name, name, 0, addr);
}

static int klp_write_object_relocations(struct module *pmod,
					struct klp_object *obj)
{
	int ret;
	unsigned long val;
	struct klp_reloc *reloc;

	if (WARN_ON(!klp_is_object_loaded(obj)))
		return -EINVAL;

	if (WARN_ON(!obj->relocs))
		return -EINVAL;

	for (reloc = obj->relocs; reloc->name; reloc++) {
		/* discover the address of the referenced symbol */
		if (reloc->external) {
			if (reloc->sympos > 0) {
				pr_err("non-zero sympos for external reloc symbol '%s' is not supported\n",
				       reloc->name);
				return -EINVAL;
			}
			ret = klp_find_external_symbol(pmod, reloc->name, &val);
		} else
			ret = klp_find_object_symbol(obj->name,
						     reloc->name,
						     reloc->sympos,
						     &val);
		if (ret)
			return ret;

		ret = klp_write_module_reloc(pmod, reloc->type, reloc->loc,
					     val + reloc->addend);
		if (ret) {
			pr_err("relocation failed for symbol '%s' at 0x%016lx (%d)\n",
			       reloc->name, val, ret);
			return ret;
		}
	}

	return 0;
}

static void notrace klp_ftrace_handler(unsigned long ip,
				       unsigned long parent_ip,
				       struct ftrace_ops *fops,
				       struct pt_regs *regs)
{
	struct klp_ops *ops;
	struct klp_func *func;

	ops = container_of(fops, struct klp_ops, fops);

	rcu_read_lock();
	func = list_first_or_null_rcu(&ops->func_stack, struct klp_func,
				      stack_node);
	if (WARN_ON_ONCE(!func))
		goto unlock;

	klp_arch_set_pc(regs, (unsigned long)func->new_func);
unlock:
	rcu_read_unlock();
}

static void klp_disable_func(struct klp_func *func)
{
	struct klp_ops *ops;

	if (WARN_ON(func->state != KLP_ENABLED))
		return;
	if (WARN_ON(!func->old_addr))
		return;

	ops = klp_find_ops(func->old_addr);
	if (WARN_ON(!ops))
		return;

	if (list_is_singular(&ops->func_stack)) {
		WARN_ON(unregister_ftrace_function(&ops->fops));
		WARN_ON(ftrace_set_filter_ip(&ops->fops, func->old_addr, 1, 0));

		list_del_rcu(&func->stack_node);
		list_del(&ops->node);
		kfree(ops);
	} else {
		list_del_rcu(&func->stack_node);
	}

	func->state = KLP_DISABLED;
}

static int klp_enable_func(struct klp_func *func)
{
	struct klp_ops *ops;
	int ret;

	if (WARN_ON(!func->old_addr))
		return -EINVAL;

	if (WARN_ON(func->state != KLP_DISABLED))
		return -EINVAL;

	ops = klp_find_ops(func->old_addr);
	if (!ops) {
		ops = kzalloc(sizeof(*ops), GFP_KERNEL);
		if (!ops)
			return -ENOMEM;

		ops->fops.func = klp_ftrace_handler;
		ops->fops.flags = FTRACE_OPS_FL_SAVE_REGS |
				  FTRACE_OPS_FL_DYNAMIC |
				  FTRACE_OPS_FL_IPMODIFY;

		list_add(&ops->node, &klp_ops);

		INIT_LIST_HEAD(&ops->func_stack);
		list_add_rcu(&func->stack_node, &ops->func_stack);

		ret = ftrace_set_filter_ip(&ops->fops, func->old_addr, 0, 0);
		if (ret) {
			pr_err("failed to set ftrace filter for function '%s' (%d)\n",
			       func->old_name, ret);
			goto err;
		}

		ret = register_ftrace_function(&ops->fops);
		if (ret) {
			pr_err("failed to register ftrace handler for function '%s' (%d)\n",
			       func->old_name, ret);
			ftrace_set_filter_ip(&ops->fops, func->old_addr, 1, 0);
			goto err;
		}


	} else {
		list_add_rcu(&func->stack_node, &ops->func_stack);
	}

	func->state = KLP_ENABLED;

	return 0;

err:
	list_del_rcu(&func->stack_node);
	list_del(&ops->node);
	kfree(ops);
	return ret;
}

static void klp_disable_object(struct klp_object *obj)
{
	struct klp_func *func;

	klp_for_each_func(obj, func)
		if (func->state == KLP_ENABLED)
			klp_disable_func(func);

	obj->state = KLP_DISABLED;
}

static int klp_enable_object(struct klp_object *obj)
{
	struct klp_func *func;
	int ret;

	if (WARN_ON(obj->state != KLP_DISABLED))
		return -EINVAL;

	if (WARN_ON(!klp_is_object_loaded(obj)))
		return -EINVAL;

	klp_for_each_func(obj, func) {
		ret = klp_enable_func(func);
		if (ret) {
			klp_disable_object(obj);
			return ret;
		}
	}
	obj->state = KLP_ENABLED;

	return 0;
}

static int __klp_disable_patch(struct klp_patch *patch)
{
	struct klp_object *obj;

	/* enforce stacking: only the last enabled patch can be disabled */
	if (!list_is_last(&patch->list, &klp_patches) &&
	    list_next_entry(patch, list)->state == KLP_ENABLED)
		return -EBUSY;

	pr_notice("disabling patch '%s'\n", patch->mod->name);

	klp_for_each_object(patch, obj) {
		if (obj->state == KLP_ENABLED)
			klp_disable_object(obj);
	}

	patch->state = KLP_DISABLED;

	return 0;
}

/**
 * klp_disable_patch() - disables a registered patch
 * @patch:	The registered, enabled patch to be disabled
 *
 * Unregisters the patched functions from ftrace.
 *
 * Return: 0 on success, otherwise error
 */
int klp_disable_patch(struct klp_patch *patch)
{
	int ret;

	mutex_lock(&klp_mutex);

	if (!klp_is_patch_registered(patch)) {
		ret = -EINVAL;
		goto err;
	}

	if (patch->state == KLP_DISABLED) {
		ret = -EINVAL;
		goto err;
	}

	ret = __klp_disable_patch(patch);

err:
	mutex_unlock(&klp_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(klp_disable_patch);

static int __klp_enable_patch(struct klp_patch *patch)
{
	struct klp_object *obj;
	int ret;

	if (WARN_ON(patch->state != KLP_DISABLED))
		return -EINVAL;

	/* enforce stacking: only the first disabled patch can be enabled */
	if (patch->list.prev != &klp_patches &&
	    list_prev_entry(patch, list)->state == KLP_DISABLED)
		return -EBUSY;

	pr_notice_once("tainting kernel with TAINT_LIVEPATCH\n");
	add_taint(TAINT_LIVEPATCH, LOCKDEP_STILL_OK);

	pr_notice("enabling patch '%s'\n", patch->mod->name);

	klp_for_each_object(patch, obj) {
		if (!klp_is_object_loaded(obj))
			continue;

		ret = klp_enable_object(obj);
		if (ret)
			goto unregister;
	}

	patch->state = KLP_ENABLED;

	return 0;

unregister:
	WARN_ON(__klp_disable_patch(patch));
	return ret;
}

/**
 * klp_enable_patch() - enables a registered patch
 * @patch:	The registered, disabled patch to be enabled
 *
 * Performs the needed symbol lookups and code relocations,
 * then registers the patched functions with ftrace.
 *
 * Return: 0 on success, otherwise error
 */
int klp_enable_patch(struct klp_patch *patch)
{
	int ret;

	mutex_lock(&klp_mutex);

	if (!klp_is_patch_registered(patch)) {
		ret = -EINVAL;
		goto err;
	}

	ret = __klp_enable_patch(patch);

err:
	mutex_unlock(&klp_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(klp_enable_patch);

/*
 * Sysfs Interface
 *
 * /sys/kernel/livepatch
 * /sys/kernel/livepatch/<patch>
 * /sys/kernel/livepatch/<patch>/enabled
 * /sys/kernel/livepatch/<patch>/<object>
 * /sys/kernel/livepatch/<patch>/<object>/<function,sympos>
 */

static ssize_t enabled_store(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	struct klp_patch *patch;
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return -EINVAL;

	if (val != KLP_DISABLED && val != KLP_ENABLED)
		return -EINVAL;

	patch = container_of(kobj, struct klp_patch, kobj);

	mutex_lock(&klp_mutex);

	if (val == patch->state) {
		/* already in requested state */
		ret = -EINVAL;
		goto err;
	}

	if (val == KLP_ENABLED) {
		ret = __klp_enable_patch(patch);
		if (ret)
			goto err;
	} else {
		ret = __klp_disable_patch(patch);
		if (ret)
			goto err;
	}

	mutex_unlock(&klp_mutex);

	return count;

err:
	mutex_unlock(&klp_mutex);
	return ret;
}

static ssize_t enabled_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buf)
{
	struct klp_patch *patch;

	patch = container_of(kobj, struct klp_patch, kobj);
	return snprintf(buf, PAGE_SIZE-1, "%d\n", patch->state);
}

static struct kobj_attribute enabled_kobj_attr = __ATTR_RW(enabled);
static struct attribute *klp_patch_attrs[] = {
	&enabled_kobj_attr.attr,
	NULL
};

static void klp_kobj_release_patch(struct kobject *kobj)
{
	/*
	 * Once we have a consistency model we'll need to module_put() the
	 * patch module here.  See klp_register_patch() for more details.
	 */
}

static struct kobj_type klp_ktype_patch = {
	.release = klp_kobj_release_patch,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_attrs = klp_patch_attrs,
};

static void klp_kobj_release_object(struct kobject *kobj)
{
}

static struct kobj_type klp_ktype_object = {
	.release = klp_kobj_release_object,
	.sysfs_ops = &kobj_sysfs_ops,
};

static void klp_kobj_release_func(struct kobject *kobj)
{
}

static struct kobj_type klp_ktype_func = {
	.release = klp_kobj_release_func,
	.sysfs_ops = &kobj_sysfs_ops,
};

/*
 * Free all functions' kobjects in the array up to some limit. When limit is
 * NULL, all kobjects are freed.
 */
static void klp_free_funcs_limited(struct klp_object *obj,
				   struct klp_func *limit)
{
	struct klp_func *func;

	for (func = obj->funcs; func->old_name && func != limit; func++)
		kobject_put(&func->kobj);
}

/* Clean up when a patched object is unloaded */
static void klp_free_object_loaded(struct klp_object *obj)
{
	struct klp_func *func;

	obj->mod = NULL;

	klp_for_each_func(obj, func)
		func->old_addr = 0;
}

/*
 * Free all objects' kobjects in the array up to some limit. When limit is
 * NULL, all kobjects are freed.
 */
static void klp_free_objects_limited(struct klp_patch *patch,
				     struct klp_object *limit)
{
	struct klp_object *obj;

	for (obj = patch->objs; obj->funcs && obj != limit; obj++) {
		klp_free_funcs_limited(obj, NULL);
		kobject_put(&obj->kobj);
	}
}

static void klp_free_patch(struct klp_patch *patch)
{
	klp_free_objects_limited(patch, NULL);
	if (!list_empty(&patch->list))
		list_del(&patch->list);
	kobject_put(&patch->kobj);
}

static int klp_init_func(struct klp_object *obj, struct klp_func *func)
{
	INIT_LIST_HEAD(&func->stack_node);
	func->state = KLP_DISABLED;

	/* The format for the sysfs directory is <function,sympos> where sympos
	 * is the nth occurrence of this symbol in kallsyms for the patched
	 * object. If the user selects 0 for old_sympos, then 1 will be used
	 * since a unique symbol will be the first occurrence.
	 */
	return kobject_init_and_add(&func->kobj, &klp_ktype_func,
				    &obj->kobj, "%s,%lu", func->old_name,
				    func->old_sympos ? func->old_sympos : 1);
}

/* parts of the initialization that is done only when the object is loaded */
static int klp_init_object_loaded(struct klp_patch *patch,
				  struct klp_object *obj)
{
	struct klp_func *func;
	int ret;

	if (obj->relocs) {
		ret = klp_write_object_relocations(patch->mod, obj);
		if (ret)
			return ret;
	}

	klp_for_each_func(obj, func) {
		ret = klp_find_object_symbol(obj->name, func->old_name,
					     func->old_sympos,
					     &func->old_addr);
		if (ret)
			return ret;
	}

	return 0;
}

static int klp_init_object(struct klp_patch *patch, struct klp_object *obj)
{
	struct klp_func *func;
	int ret;
	const char *name;

	if (!obj->funcs)
		return -EINVAL;

	obj->state = KLP_DISABLED;
	obj->mod = NULL;

	klp_find_object_module(obj);

	name = klp_is_module(obj) ? obj->name : "vmlinux";
	ret = kobject_init_and_add(&obj->kobj, &klp_ktype_object,
				   &patch->kobj, "%s", name);
	if (ret)
		return ret;

	klp_for_each_func(obj, func) {
		ret = klp_init_func(obj, func);
		if (ret)
			goto free;
	}

	if (klp_is_object_loaded(obj)) {
		ret = klp_init_object_loaded(patch, obj);
		if (ret)
			goto free;
	}

	return 0;

free:
	klp_free_funcs_limited(obj, func);
	kobject_put(&obj->kobj);
	return ret;
}

static int klp_init_patch(struct klp_patch *patch)
{
	struct klp_object *obj;
	int ret;

	if (!patch->objs)
		return -EINVAL;

	mutex_lock(&klp_mutex);

	patch->state = KLP_DISABLED;

	ret = kobject_init_and_add(&patch->kobj, &klp_ktype_patch,
				   klp_root_kobj, "%s", patch->mod->name);
	if (ret)
		goto unlock;

	klp_for_each_object(patch, obj) {
		ret = klp_init_object(patch, obj);
		if (ret)
			goto free;
	}

	list_add_tail(&patch->list, &klp_patches);

	mutex_unlock(&klp_mutex);

	return 0;

free:
	klp_free_objects_limited(patch, obj);
	kobject_put(&patch->kobj);
unlock:
	mutex_unlock(&klp_mutex);
	return ret;
}

/**
 * klp_unregister_patch() - unregisters a patch
 * @patch:	Disabled patch to be unregistered
 *
 * Frees the data structures and removes the sysfs interface.
 *
 * Return: 0 on success, otherwise error
 */
int klp_unregister_patch(struct klp_patch *patch)
{
	int ret = 0;

	mutex_lock(&klp_mutex);

	if (!klp_is_patch_registered(patch)) {
		ret = -EINVAL;
		goto out;
	}

	if (patch->state == KLP_ENABLED) {
		ret = -EBUSY;
		goto out;
	}

	klp_free_patch(patch);

out:
	mutex_unlock(&klp_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(klp_unregister_patch);

/**
 * klp_register_patch() - registers a patch
 * @patch:	Patch to be registered
 *
 * Initializes the data structure associated with the patch and
 * creates the sysfs interface.
 *
 * Return: 0 on success, otherwise error
 */
int klp_register_patch(struct klp_patch *patch)
{
	int ret;

	if (!klp_initialized())
		return -ENODEV;

	if (!patch || !patch->mod)
		return -EINVAL;

	/*
	 * A reference is taken on the patch module to prevent it from being
	 * unloaded.  Right now, we don't allow patch modules to unload since
	 * there is currently no method to determine if a thread is still
	 * running in the patched code contained in the patch module once
	 * the ftrace registration is successful.
	 */
	if (!try_module_get(patch->mod))
		return -ENODEV;

	ret = klp_init_patch(patch);
	if (ret)
		module_put(patch->mod);

	return ret;
}
EXPORT_SYMBOL_GPL(klp_register_patch);

static int klp_module_notify_coming(struct klp_patch *patch,
				     struct klp_object *obj)
{
	struct module *pmod = patch->mod;
	struct module *mod = obj->mod;
	int ret;

	ret = klp_init_object_loaded(patch, obj);
	if (ret) {
		pr_warn("failed to initialize patch '%s' for module '%s' (%d)\n",
			pmod->name, mod->name, ret);
		return ret;
	}

	if (patch->state == KLP_DISABLED)
		return 0;

	pr_notice("applying patch '%s' to loading module '%s'\n",
		  pmod->name, mod->name);

	ret = klp_enable_object(obj);
	if (ret)
		pr_warn("failed to apply patch '%s' to module '%s' (%d)\n",
			pmod->name, mod->name, ret);
	return ret;
}

static void klp_module_notify_going(struct klp_patch *patch,
				    struct klp_object *obj)
{
	struct module *pmod = patch->mod;
	struct module *mod = obj->mod;

	if (patch->state == KLP_DISABLED)
		goto disabled;

	pr_notice("reverting patch '%s' on unloading module '%s'\n",
		  pmod->name, mod->name);

	klp_disable_object(obj);

disabled:
	klp_free_object_loaded(obj);
}

static int klp_module_notify(struct notifier_block *nb, unsigned long action,
			     void *data)
{
	int ret;
	struct module *mod = data;
	struct klp_patch *patch;
	struct klp_object *obj;

	if (action != MODULE_STATE_COMING && action != MODULE_STATE_GOING)
		return 0;

	mutex_lock(&klp_mutex);

	/*
	 * Each module has to know that the notifier has been called.
	 * We never know what module will get patched by a new patch.
	 */
	if (action == MODULE_STATE_COMING)
		mod->klp_alive = true;
	else /* MODULE_STATE_GOING */
		mod->klp_alive = false;

	list_for_each_entry(patch, &klp_patches, list) {
		klp_for_each_object(patch, obj) {
			if (!klp_is_module(obj) || strcmp(obj->name, mod->name))
				continue;

			if (action == MODULE_STATE_COMING) {
				obj->mod = mod;
				ret = klp_module_notify_coming(patch, obj);
				if (ret) {
					obj->mod = NULL;
					pr_warn("patch '%s' is in an inconsistent state!\n",
						patch->mod->name);
				}
			} else /* MODULE_STATE_GOING */
				klp_module_notify_going(patch, obj);

			break;
		}
	}

	mutex_unlock(&klp_mutex);

	return 0;
}

static struct notifier_block klp_module_nb = {
	.notifier_call = klp_module_notify,
	.priority = INT_MIN+1, /* called late but before ftrace notifier */
};

static int __init klp_init(void)
{
	int ret;

	ret = klp_check_compiler_support();
	if (ret) {
		pr_info("Your compiler is too old; turning off.\n");
		return -EINVAL;
	}

	ret = register_module_notifier(&klp_module_nb);
	if (ret)
		return ret;

	klp_root_kobj = kobject_create_and_add("livepatch", kernel_kobj);
	if (!klp_root_kobj) {
		ret = -ENOMEM;
		goto unregister;
	}

	return 0;

unregister:
	unregister_module_notifier(&klp_module_nb);
	return ret;
}

module_init(klp_init);
