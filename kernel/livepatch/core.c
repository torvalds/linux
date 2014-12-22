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

/*
 * The klp_mutex protects the klp_patches list and state transitions of any
 * structure reachable from the patches list.  References to any structure must
 * be obtained under mutex protection.
 */

static DEFINE_MUTEX(klp_mutex);
static LIST_HEAD(klp_patches);

static struct kobject *klp_root_kobj;

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
	if (!klp_is_module(obj))
		return;

	mutex_lock(&module_mutex);
	/*
	 * We don't need to take a reference on the module here because we have
	 * the klp_mutex, which is also taken by the module notifier.  This
	 * prevents any module from unloading until we release the klp_mutex.
	 */
	obj->mod = find_module(obj->name);
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
	return klp_root_kobj;
}

struct klp_find_arg {
	const char *objname;
	const char *name;
	unsigned long addr;
	/*
	 * If count == 0, the symbol was not found. If count == 1, a unique
	 * match was found and addr is set.  If count > 1, there is
	 * unresolvable ambiguity among "count" number of symbols with the same
	 * name in the same object.
	 */
	unsigned long count;
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

	/*
	 * args->addr might be overwritten if another match is found
	 * but klp_find_object_symbol() handles this and only returns the
	 * addr if count == 1.
	 */
	args->addr = addr;
	args->count++;

	return 0;
}

static int klp_find_object_symbol(const char *objname, const char *name,
				  unsigned long *addr)
{
	struct klp_find_arg args = {
		.objname = objname,
		.name = name,
		.addr = 0,
		.count = 0
	};

	kallsyms_on_each_symbol(klp_find_callback, &args);

	if (args.count == 0)
		pr_err("symbol '%s' not found in symbol table\n", name);
	else if (args.count > 1)
		pr_err("unresolvable ambiguity (%lu matches) on symbol '%s' in object '%s'\n",
		       args.count, name, objname);
	else {
		*addr = args.addr;
		return 0;
	}

	*addr = 0;
	return -EINVAL;
}

struct klp_verify_args {
	const char *name;
	const unsigned long addr;
};

static int klp_verify_callback(void *data, const char *name,
			       struct module *mod, unsigned long addr)
{
	struct klp_verify_args *args = data;

	if (!mod &&
	    !strcmp(args->name, name) &&
	    args->addr == addr)
		return 1;

	return 0;
}

static int klp_verify_vmlinux_symbol(const char *name, unsigned long addr)
{
	struct klp_verify_args args = {
		.name = name,
		.addr = addr,
	};

	if (kallsyms_on_each_symbol(klp_verify_callback, &args))
		return 0;

	pr_err("symbol '%s' not found at specified address 0x%016lx, kernel mismatch?",
		name, addr);
	return -EINVAL;
}

static int klp_find_verify_func_addr(struct klp_object *obj,
				     struct klp_func *func)
{
	int ret;

#if defined(CONFIG_RANDOMIZE_BASE)
	/* KASLR is enabled, disregard old_addr from user */
	func->old_addr = 0;
#endif

	if (!func->old_addr || klp_is_module(obj))
		ret = klp_find_object_symbol(obj->name, func->old_name,
					     &func->old_addr);
	else
		ret = klp_verify_vmlinux_symbol(func->old_name,
						func->old_addr);

	return ret;
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
	preempt_enable();
	if (sym) {
		*addr = sym->value;
		return 0;
	}

	/* otherwise check if it's in another .o within the patch module */
	return klp_find_object_symbol(pmod->name, name, addr);
}

static int klp_write_object_relocations(struct module *pmod,
					struct klp_object *obj)
{
	int ret;
	struct klp_reloc *reloc;

	if (WARN_ON(!klp_is_object_loaded(obj)))
		return -EINVAL;

	if (WARN_ON(!obj->relocs))
		return -EINVAL;

	for (reloc = obj->relocs; reloc->name; reloc++) {
		if (!klp_is_module(obj)) {
			ret = klp_verify_vmlinux_symbol(reloc->name,
							reloc->val);
			if (ret)
				return ret;
		} else {
			/* module, reloc->val needs to be discovered */
			if (reloc->external)
				ret = klp_find_external_symbol(pmod,
							       reloc->name,
							       &reloc->val);
			else
				ret = klp_find_object_symbol(obj->mod->name,
							     reloc->name,
							     &reloc->val);
			if (ret)
				return ret;
		}
		ret = klp_write_module_reloc(pmod, reloc->type, reloc->loc,
					     reloc->val + reloc->addend);
		if (ret) {
			pr_err("relocation failed for symbol '%s' at 0x%016lx (%d)\n",
			       reloc->name, reloc->val, ret);
			return ret;
		}
	}

	return 0;
}

static void notrace klp_ftrace_handler(unsigned long ip,
				       unsigned long parent_ip,
				       struct ftrace_ops *ops,
				       struct pt_regs *regs)
{
	struct klp_func *func = ops->private;

	klp_arch_set_pc(regs, (unsigned long)func->new_func);
}

static int klp_disable_func(struct klp_func *func)
{
	int ret;

	if (WARN_ON(func->state != KLP_ENABLED))
		return -EINVAL;

	if (WARN_ON(!func->old_addr))
		return -EINVAL;

	ret = unregister_ftrace_function(func->fops);
	if (ret) {
		pr_err("failed to unregister ftrace handler for function '%s' (%d)\n",
		       func->old_name, ret);
		return ret;
	}

	ret = ftrace_set_filter_ip(func->fops, func->old_addr, 1, 0);
	if (ret)
		pr_warn("function unregister succeeded but failed to clear the filter\n");

	func->state = KLP_DISABLED;

	return 0;
}

static int klp_enable_func(struct klp_func *func)
{
	int ret;

	if (WARN_ON(!func->old_addr))
		return -EINVAL;

	if (WARN_ON(func->state != KLP_DISABLED))
		return -EINVAL;

	ret = ftrace_set_filter_ip(func->fops, func->old_addr, 0, 0);
	if (ret) {
		pr_err("failed to set ftrace filter for function '%s' (%d)\n",
		       func->old_name, ret);
		return ret;
	}

	ret = register_ftrace_function(func->fops);
	if (ret) {
		pr_err("failed to register ftrace handler for function '%s' (%d)\n",
		       func->old_name, ret);
		ftrace_set_filter_ip(func->fops, func->old_addr, 1, 0);
	} else {
		func->state = KLP_ENABLED;
	}

	return ret;
}

static int klp_disable_object(struct klp_object *obj)
{
	struct klp_func *func;
	int ret;

	for (func = obj->funcs; func->old_name; func++) {
		if (func->state != KLP_ENABLED)
			continue;

		ret = klp_disable_func(func);
		if (ret)
			return ret;
	}

	obj->state = KLP_DISABLED;

	return 0;
}

static int klp_enable_object(struct klp_object *obj)
{
	struct klp_func *func;
	int ret;

	if (WARN_ON(obj->state != KLP_DISABLED))
		return -EINVAL;

	if (WARN_ON(!klp_is_object_loaded(obj)))
		return -EINVAL;

	for (func = obj->funcs; func->old_name; func++) {
		ret = klp_enable_func(func);
		if (ret)
			goto unregister;
	}
	obj->state = KLP_ENABLED;

	return 0;

unregister:
	WARN_ON(klp_disable_object(obj));
	return ret;
}

static int __klp_disable_patch(struct klp_patch *patch)
{
	struct klp_object *obj;
	int ret;

	pr_notice("disabling patch '%s'\n", patch->mod->name);

	for (obj = patch->objs; obj->funcs; obj++) {
		if (obj->state != KLP_ENABLED)
			continue;

		ret = klp_disable_object(obj);
		if (ret)
			return ret;
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

	pr_notice_once("tainting kernel with TAINT_LIVEPATCH\n");
	add_taint(TAINT_LIVEPATCH, LOCKDEP_STILL_OK);

	pr_notice("enabling patch '%s'\n", patch->mod->name);

	for (obj = patch->objs; obj->funcs; obj++) {
		klp_find_object_module(obj);

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
 * /sys/kernel/livepatch/<patch>/<object>/<func>
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

static void klp_kobj_release_func(struct kobject *kobj)
{
	struct klp_func *func;

	func = container_of(kobj, struct klp_func, kobj);
	kfree(func->fops);
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

	for (func = obj->funcs; func->old_name; func++)
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
		kobject_put(obj->kobj);
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
	struct ftrace_ops *ops;
	int ret;

	ops = kzalloc(sizeof(*ops), GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	ops->private = func;
	ops->func = klp_ftrace_handler;
	ops->flags = FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_DYNAMIC |
		     FTRACE_OPS_FL_IPMODIFY;
	func->fops = ops;
	func->state = KLP_DISABLED;

	ret = kobject_init_and_add(&func->kobj, &klp_ktype_func,
				   obj->kobj, func->old_name);
	if (ret) {
		kfree(func->fops);
		return ret;
	}

	return 0;
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

	for (func = obj->funcs; func->old_name; func++) {
		ret = klp_find_verify_func_addr(obj, func);
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

	klp_find_object_module(obj);

	name = klp_is_module(obj) ? obj->name : "vmlinux";
	obj->kobj = kobject_create_and_add(name, &patch->kobj);
	if (!obj->kobj)
		return -ENOMEM;

	for (func = obj->funcs; func->old_name; func++) {
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
	kobject_put(obj->kobj);
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
				   klp_root_kobj, patch->mod->name);
	if (ret)
		goto unlock;

	for (obj = patch->objs; obj->funcs; obj++) {
		ret = klp_init_object(patch, obj);
		if (ret)
			goto free;
	}

	list_add(&patch->list, &klp_patches);

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

static void klp_module_notify_coming(struct klp_patch *patch,
				     struct klp_object *obj)
{
	struct module *pmod = patch->mod;
	struct module *mod = obj->mod;
	int ret;

	ret = klp_init_object_loaded(patch, obj);
	if (ret)
		goto err;

	if (patch->state == KLP_DISABLED)
		return;

	pr_notice("applying patch '%s' to loading module '%s'\n",
		  pmod->name, mod->name);

	ret = klp_enable_object(obj);
	if (!ret)
		return;

err:
	pr_warn("failed to apply patch '%s' to module '%s' (%d)\n",
		pmod->name, mod->name, ret);
}

static void klp_module_notify_going(struct klp_patch *patch,
				    struct klp_object *obj)
{
	struct module *pmod = patch->mod;
	struct module *mod = obj->mod;
	int ret;

	if (patch->state == KLP_DISABLED)
		goto disabled;

	pr_notice("reverting patch '%s' on unloading module '%s'\n",
		  pmod->name, mod->name);

	ret = klp_disable_object(obj);
	if (ret)
		pr_warn("failed to revert patch '%s' on module '%s' (%d)\n",
			pmod->name, mod->name, ret);

disabled:
	klp_free_object_loaded(obj);
}

static int klp_module_notify(struct notifier_block *nb, unsigned long action,
			     void *data)
{
	struct module *mod = data;
	struct klp_patch *patch;
	struct klp_object *obj;

	if (action != MODULE_STATE_COMING && action != MODULE_STATE_GOING)
		return 0;

	mutex_lock(&klp_mutex);

	list_for_each_entry(patch, &klp_patches, list) {
		for (obj = patch->objs; obj->funcs; obj++) {
			if (!klp_is_module(obj) || strcmp(obj->name, mod->name))
				continue;

			if (action == MODULE_STATE_COMING) {
				obj->mod = mod;
				klp_module_notify_coming(patch, obj);
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

static int klp_init(void)
{
	int ret;

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
