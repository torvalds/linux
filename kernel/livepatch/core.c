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
#include <linux/elf.h>
#include <linux/moduleloader.h>
#include <asm/cacheflush.h>

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
	 * klp_module_going() instead.
	 */
	mod = find_module(obj->name);
	/*
	 * Do not mess work of klp_module_coming() and klp_module_going().
	 * Note that the patch might still be needed before klp_module_going()
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
		pr_err("unresolvable ambiguity for symbol '%s' in object '%s'\n",
		       name, objname);
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

static int klp_resolve_symbols(Elf_Shdr *relasec, struct module *pmod)
{
	int i, cnt, vmlinux, ret;
	char objname[MODULE_NAME_LEN];
	char symname[KSYM_NAME_LEN];
	char *strtab = pmod->core_kallsyms.strtab;
	Elf_Rela *relas;
	Elf_Sym *sym;
	unsigned long sympos, addr;

	/*
	 * Since the field widths for objname and symname in the sscanf()
	 * call are hard-coded and correspond to MODULE_NAME_LEN and
	 * KSYM_NAME_LEN respectively, we must make sure that MODULE_NAME_LEN
	 * and KSYM_NAME_LEN have the values we expect them to have.
	 *
	 * Because the value of MODULE_NAME_LEN can differ among architectures,
	 * we use the smallest/strictest upper bound possible (56, based on
	 * the current definition of MODULE_NAME_LEN) to prevent overflows.
	 */
	BUILD_BUG_ON(MODULE_NAME_LEN < 56 || KSYM_NAME_LEN != 128);

	relas = (Elf_Rela *) relasec->sh_addr;
	/* For each rela in this klp relocation section */
	for (i = 0; i < relasec->sh_size / sizeof(Elf_Rela); i++) {
		sym = pmod->core_kallsyms.symtab + ELF_R_SYM(relas[i].r_info);
		if (sym->st_shndx != SHN_LIVEPATCH) {
			pr_err("symbol %s is not marked as a livepatch symbol",
			       strtab + sym->st_name);
			return -EINVAL;
		}

		/* Format: .klp.sym.objname.symname,sympos */
		cnt = sscanf(strtab + sym->st_name,
			     ".klp.sym.%55[^.].%127[^,],%lu",
			     objname, symname, &sympos);
		if (cnt != 3) {
			pr_err("symbol %s has an incorrectly formatted name",
			       strtab + sym->st_name);
			return -EINVAL;
		}

		/* klp_find_object_symbol() treats a NULL objname as vmlinux */
		vmlinux = !strcmp(objname, "vmlinux");
		ret = klp_find_object_symbol(vmlinux ? NULL : objname,
					     symname, sympos, &addr);
		if (ret)
			return ret;

		sym->st_value = addr;
	}

	return 0;
}

static int klp_write_object_relocations(struct module *pmod,
					struct klp_object *obj)
{
	int i, cnt, ret = 0;
	const char *objname, *secname;
	char sec_objname[MODULE_NAME_LEN];
	Elf_Shdr *sec;

	if (WARN_ON(!klp_is_object_loaded(obj)))
		return -EINVAL;

	objname = klp_is_module(obj) ? obj->name : "vmlinux";

	/* For each klp relocation section */
	for (i = 1; i < pmod->klp_info->hdr.e_shnum; i++) {
		sec = pmod->klp_info->sechdrs + i;
		secname = pmod->klp_info->secstrings + sec->sh_name;
		if (!(sec->sh_flags & SHF_RELA_LIVEPATCH))
			continue;

		/*
		 * Format: .klp.rela.sec_objname.section_name
		 * See comment in klp_resolve_symbols() for an explanation
		 * of the selected field width value.
		 */
		cnt = sscanf(secname, ".klp.rela.%55[^.]", sec_objname);
		if (cnt != 1) {
			pr_err("section %s has an incorrectly formatted name",
			       secname);
			ret = -EINVAL;
			break;
		}

		if (strcmp(objname, sec_objname))
			continue;

		ret = klp_resolve_symbols(sec, pmod);
		if (ret)
			break;

		ret = apply_relocate_add(pmod->klp_info->sechdrs,
					 pmod->core_kallsyms.strtab,
					 pmod->klp_info->symndx, i, pmod);
		if (ret)
			break;
	}

	return ret;
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

/*
 * Convert a function address into the appropriate ftrace location.
 *
 * Usually this is just the address of the function, but on some architectures
 * it's more complicated so allow them to provide a custom behaviour.
 */
#ifndef klp_get_ftrace_location
static unsigned long klp_get_ftrace_location(unsigned long faddr)
{
	return faddr;
}
#endif

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
		unsigned long ftrace_loc;

		ftrace_loc = klp_get_ftrace_location(func->old_addr);
		if (WARN_ON(!ftrace_loc))
			return;

		WARN_ON(unregister_ftrace_function(&ops->fops));
		WARN_ON(ftrace_set_filter_ip(&ops->fops, ftrace_loc, 1, 0));

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
		unsigned long ftrace_loc;

		ftrace_loc = klp_get_ftrace_location(func->old_addr);
		if (!ftrace_loc) {
			pr_err("failed to find location for function '%s'\n",
				func->old_name);
			return -EINVAL;
		}

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

		ret = ftrace_set_filter_ip(&ops->fops, ftrace_loc, 0, 0);
		if (ret) {
			pr_err("failed to set ftrace filter for function '%s' (%d)\n",
			       func->old_name, ret);
			goto err;
		}

		ret = register_ftrace_function(&ops->fops);
		if (ret) {
			pr_err("failed to register ftrace handler for function '%s' (%d)\n",
			       func->old_name, ret);
			ftrace_set_filter_ip(&ops->fops, ftrace_loc, 1, 0);
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
	if (!func->old_name || !func->new_func)
		return -EINVAL;

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

/* Arches may override this to finish any remaining arch-specific tasks */
void __weak arch_klp_init_object_loaded(struct klp_patch *patch,
					struct klp_object *obj)
{
}

/* parts of the initialization that is done only when the object is loaded */
static int klp_init_object_loaded(struct klp_patch *patch,
				  struct klp_object *obj)
{
	struct klp_func *func;
	int ret;

	module_disable_ro(patch->mod);
	ret = klp_write_object_relocations(patch->mod, obj);
	if (ret) {
		module_enable_ro(patch->mod, true);
		return ret;
	}

	arch_klp_init_object_loaded(patch, obj);
	module_enable_ro(patch->mod, true);

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

	if (!patch || !patch->mod)
		return -EINVAL;

	if (!is_livepatch_module(patch->mod)) {
		pr_err("module %s is not marked as a livepatch module",
		       patch->mod->name);
		return -EINVAL;
	}

	if (!klp_initialized())
		return -ENODEV;

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

int klp_module_coming(struct module *mod)
{
	int ret;
	struct klp_patch *patch;
	struct klp_object *obj;

	if (WARN_ON(mod->state != MODULE_STATE_COMING))
		return -EINVAL;

	mutex_lock(&klp_mutex);
	/*
	 * Each module has to know that klp_module_coming()
	 * has been called. We never know what module will
	 * get patched by a new patch.
	 */
	mod->klp_alive = true;

	list_for_each_entry(patch, &klp_patches, list) {
		klp_for_each_object(patch, obj) {
			if (!klp_is_module(obj) || strcmp(obj->name, mod->name))
				continue;

			obj->mod = mod;

			ret = klp_init_object_loaded(patch, obj);
			if (ret) {
				pr_warn("failed to initialize patch '%s' for module '%s' (%d)\n",
					patch->mod->name, obj->mod->name, ret);
				goto err;
			}

			if (patch->state == KLP_DISABLED)
				break;

			pr_notice("applying patch '%s' to loading module '%s'\n",
				  patch->mod->name, obj->mod->name);

			ret = klp_enable_object(obj);
			if (ret) {
				pr_warn("failed to apply patch '%s' to module '%s' (%d)\n",
					patch->mod->name, obj->mod->name, ret);
				goto err;
			}

			break;
		}
	}

	mutex_unlock(&klp_mutex);

	return 0;

err:
	/*
	 * If a patch is unsuccessfully applied, return
	 * error to the module loader.
	 */
	pr_warn("patch '%s' failed for module '%s', refusing to load module '%s'\n",
		patch->mod->name, obj->mod->name, obj->mod->name);
	mod->klp_alive = false;
	klp_free_object_loaded(obj);
	mutex_unlock(&klp_mutex);

	return ret;
}

void klp_module_going(struct module *mod)
{
	struct klp_patch *patch;
	struct klp_object *obj;

	if (WARN_ON(mod->state != MODULE_STATE_GOING &&
		    mod->state != MODULE_STATE_COMING))
		return;

	mutex_lock(&klp_mutex);
	/*
	 * Each module has to know that klp_module_going()
	 * has been called. We never know what module will
	 * get patched by a new patch.
	 */
	mod->klp_alive = false;

	list_for_each_entry(patch, &klp_patches, list) {
		klp_for_each_object(patch, obj) {
			if (!klp_is_module(obj) || strcmp(obj->name, mod->name))
				continue;

			if (patch->state != KLP_DISABLED) {
				pr_notice("reverting patch '%s' on unloading module '%s'\n",
					  patch->mod->name, obj->mod->name);
				klp_disable_object(obj);
			}

			klp_free_object_loaded(obj);
			break;
		}
	}

	mutex_unlock(&klp_mutex);
}

static int __init klp_init(void)
{
	int ret;

	ret = klp_check_compiler_support();
	if (ret) {
		pr_info("Your compiler is too old; turning off.\n");
		return -EINVAL;
	}

	klp_root_kobj = kobject_create_and_add("livepatch", kernel_kobj);
	if (!klp_root_kobj)
		return -ENOMEM;

	return 0;
}

module_init(klp_init);
