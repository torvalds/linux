// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * core.c - Kernel Live Patching Core
 *
 * Copyright (C) 2014 Seth Jennings <sjenning@redhat.com>
 * Copyright (C) 2014 SUSE
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/kallsyms.h>
#include <linux/livepatch.h>
#include <linux/elf.h>
#include <linux/moduleloader.h>
#include <linux/completion.h>
#include <linux/memory.h>
#include <linux/rcupdate.h>
#include <asm/cacheflush.h>
#include "core.h"
#include "patch.h"
#include "state.h"
#include "transition.h"

/*
 * klp_mutex is a coarse lock which serializes access to klp data.  All
 * accesses to klp-related variables and structures must have mutex protection,
 * except within the following functions which carefully avoid the need for it:
 *
 * - klp_ftrace_handler()
 * - klp_update_patch_state()
 * - __klp_sched_try_switch()
 */
DEFINE_MUTEX(klp_mutex);

/*
 * Actively used patches: enabled or in transition. Note that replaced
 * or disabled patches are not listed even though the related kernel
 * module still can be loaded.
 */
LIST_HEAD(klp_patches);

static struct kobject *klp_root_kobj;

static bool klp_is_module(struct klp_object *obj)
{
	return obj->name;
}

/* sets obj->mod if object is not vmlinux and module is found */
static void klp_find_object_module(struct klp_object *obj)
{
	struct module *mod;

	if (!klp_is_module(obj))
		return;

	rcu_read_lock_sched();
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

	rcu_read_unlock_sched();
}

static bool klp_initialized(void)
{
	return !!klp_root_kobj;
}

static struct klp_func *klp_find_func(struct klp_object *obj,
				      struct klp_func *old_func)
{
	struct klp_func *func;

	klp_for_each_func(obj, func) {
		if ((strcmp(old_func->old_name, func->old_name) == 0) &&
		    (old_func->old_sympos == func->old_sympos)) {
			return func;
		}
	}

	return NULL;
}

static struct klp_object *klp_find_object(struct klp_patch *patch,
					  struct klp_object *old_obj)
{
	struct klp_object *obj;

	klp_for_each_object(patch, obj) {
		if (klp_is_module(old_obj)) {
			if (klp_is_module(obj) &&
			    strcmp(old_obj->name, obj->name) == 0) {
				return obj;
			}
		} else if (!klp_is_module(obj)) {
			return obj;
		}
	}

	return NULL;
}

struct klp_find_arg {
	const char *name;
	unsigned long addr;
	unsigned long count;
	unsigned long pos;
};

static int klp_match_callback(void *data, unsigned long addr)
{
	struct klp_find_arg *args = data;

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

static int klp_find_callback(void *data, const char *name, unsigned long addr)
{
	struct klp_find_arg *args = data;

	if (strcmp(args->name, name))
		return 0;

	return klp_match_callback(data, addr);
}

static int klp_find_object_symbol(const char *objname, const char *name,
				  unsigned long sympos, unsigned long *addr)
{
	struct klp_find_arg args = {
		.name = name,
		.addr = 0,
		.count = 0,
		.pos = sympos,
	};

	if (objname)
		module_kallsyms_on_each_symbol(objname, klp_find_callback, &args);
	else
		kallsyms_on_each_match_symbol(klp_match_callback, name, &args);

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

static int klp_resolve_symbols(Elf_Shdr *sechdrs, const char *strtab,
			       unsigned int symndx, Elf_Shdr *relasec,
			       const char *sec_objname)
{
	int i, cnt, ret;
	char sym_objname[MODULE_NAME_LEN];
	char sym_name[KSYM_NAME_LEN];
	Elf_Rela *relas;
	Elf_Sym *sym;
	unsigned long sympos, addr;
	bool sym_vmlinux;
	bool sec_vmlinux = !strcmp(sec_objname, "vmlinux");

	/*
	 * Since the field widths for sym_objname and sym_name in the sscanf()
	 * call are hard-coded and correspond to MODULE_NAME_LEN and
	 * KSYM_NAME_LEN respectively, we must make sure that MODULE_NAME_LEN
	 * and KSYM_NAME_LEN have the values we expect them to have.
	 *
	 * Because the value of MODULE_NAME_LEN can differ among architectures,
	 * we use the smallest/strictest upper bound possible (56, based on
	 * the current definition of MODULE_NAME_LEN) to prevent overflows.
	 */
	BUILD_BUG_ON(MODULE_NAME_LEN < 56 || KSYM_NAME_LEN != 512);

	relas = (Elf_Rela *) relasec->sh_addr;
	/* For each rela in this klp relocation section */
	for (i = 0; i < relasec->sh_size / sizeof(Elf_Rela); i++) {
		sym = (Elf_Sym *)sechdrs[symndx].sh_addr + ELF_R_SYM(relas[i].r_info);
		if (sym->st_shndx != SHN_LIVEPATCH) {
			pr_err("symbol %s is not marked as a livepatch symbol\n",
			       strtab + sym->st_name);
			return -EINVAL;
		}

		/* Format: .klp.sym.sym_objname.sym_name,sympos */
		cnt = sscanf(strtab + sym->st_name,
			     ".klp.sym.%55[^.].%511[^,],%lu",
			     sym_objname, sym_name, &sympos);
		if (cnt != 3) {
			pr_err("symbol %s has an incorrectly formatted name\n",
			       strtab + sym->st_name);
			return -EINVAL;
		}

		sym_vmlinux = !strcmp(sym_objname, "vmlinux");

		/*
		 * Prevent module-specific KLP rela sections from referencing
		 * vmlinux symbols.  This helps prevent ordering issues with
		 * module special section initializations.  Presumably such
		 * symbols are exported and normal relas can be used instead.
		 */
		if (!sec_vmlinux && sym_vmlinux) {
			pr_err("invalid access to vmlinux symbol '%s' from module-specific livepatch relocation section\n",
			       sym_name);
			return -EINVAL;
		}

		/* klp_find_object_symbol() treats a NULL objname as vmlinux */
		ret = klp_find_object_symbol(sym_vmlinux ? NULL : sym_objname,
					     sym_name, sympos, &addr);
		if (ret)
			return ret;

		sym->st_value = addr;
	}

	return 0;
}

void __weak clear_relocate_add(Elf_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
}

/*
 * At a high-level, there are two types of klp relocation sections: those which
 * reference symbols which live in vmlinux; and those which reference symbols
 * which live in other modules.  This function is called for both types:
 *
 * 1) When a klp module itself loads, the module code calls this function to
 *    write vmlinux-specific klp relocations (.klp.rela.vmlinux.* sections).
 *    These relocations are written to the klp module text to allow the patched
 *    code/data to reference unexported vmlinux symbols.  They're written as
 *    early as possible to ensure that other module init code (.e.g.,
 *    jump_label_apply_nops) can access any unexported vmlinux symbols which
 *    might be referenced by the klp module's special sections.
 *
 * 2) When a to-be-patched module loads -- or is already loaded when a
 *    corresponding klp module loads -- klp code calls this function to write
 *    module-specific klp relocations (.klp.rela.{module}.* sections).  These
 *    are written to the klp module text to allow the patched code/data to
 *    reference symbols which live in the to-be-patched module or one of its
 *    module dependencies.  Exported symbols are supported, in addition to
 *    unexported symbols, in order to enable late module patching, which allows
 *    the to-be-patched module to be loaded and patched sometime *after* the
 *    klp module is loaded.
 */
static int klp_write_section_relocs(struct module *pmod, Elf_Shdr *sechdrs,
				    const char *shstrtab, const char *strtab,
				    unsigned int symndx, unsigned int secndx,
				    const char *objname, bool apply)
{
	int cnt, ret;
	char sec_objname[MODULE_NAME_LEN];
	Elf_Shdr *sec = sechdrs + secndx;

	/*
	 * Format: .klp.rela.sec_objname.section_name
	 * See comment in klp_resolve_symbols() for an explanation
	 * of the selected field width value.
	 */
	cnt = sscanf(shstrtab + sec->sh_name, ".klp.rela.%55[^.]",
		     sec_objname);
	if (cnt != 1) {
		pr_err("section %s has an incorrectly formatted name\n",
		       shstrtab + sec->sh_name);
		return -EINVAL;
	}

	if (strcmp(objname ? objname : "vmlinux", sec_objname))
		return 0;

	if (apply) {
		ret = klp_resolve_symbols(sechdrs, strtab, symndx,
					  sec, sec_objname);
		if (ret)
			return ret;

		return apply_relocate_add(sechdrs, strtab, symndx, secndx, pmod);
	}

	clear_relocate_add(sechdrs, strtab, symndx, secndx, pmod);
	return 0;
}

int klp_apply_section_relocs(struct module *pmod, Elf_Shdr *sechdrs,
			     const char *shstrtab, const char *strtab,
			     unsigned int symndx, unsigned int secndx,
			     const char *objname)
{
	return klp_write_section_relocs(pmod, sechdrs, shstrtab, strtab, symndx,
					secndx, objname, true);
}

/*
 * Sysfs Interface
 *
 * /sys/kernel/livepatch
 * /sys/kernel/livepatch/<patch>
 * /sys/kernel/livepatch/<patch>/enabled
 * /sys/kernel/livepatch/<patch>/transition
 * /sys/kernel/livepatch/<patch>/force
 * /sys/kernel/livepatch/<patch>/replace
 * /sys/kernel/livepatch/<patch>/stack_order
 * /sys/kernel/livepatch/<patch>/<object>
 * /sys/kernel/livepatch/<patch>/<object>/patched
 * /sys/kernel/livepatch/<patch>/<object>/<function,sympos>
 */
static int __klp_disable_patch(struct klp_patch *patch);

static ssize_t enabled_store(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	struct klp_patch *patch;
	int ret;
	bool enabled;

	ret = kstrtobool(buf, &enabled);
	if (ret)
		return ret;

	patch = container_of(kobj, struct klp_patch, kobj);

	mutex_lock(&klp_mutex);

	if (patch->enabled == enabled) {
		/* already in requested state */
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Allow to reverse a pending transition in both ways. It might be
	 * necessary to complete the transition without forcing and breaking
	 * the system integrity.
	 *
	 * Do not allow to re-enable a disabled patch.
	 */
	if (patch == klp_transition_patch)
		klp_reverse_transition();
	else if (!enabled)
		ret = __klp_disable_patch(patch);
	else
		ret = -EINVAL;

out:
	mutex_unlock(&klp_mutex);

	if (ret)
		return ret;
	return count;
}

static ssize_t enabled_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buf)
{
	struct klp_patch *patch;

	patch = container_of(kobj, struct klp_patch, kobj);
	return sysfs_emit(buf, "%d\n", patch->enabled);
}

static ssize_t transition_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	struct klp_patch *patch;

	patch = container_of(kobj, struct klp_patch, kobj);
	return sysfs_emit(buf, "%d\n", patch == klp_transition_patch);
}

static ssize_t force_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{
	struct klp_patch *patch;
	int ret;
	bool val;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	if (!val)
		return count;

	mutex_lock(&klp_mutex);

	patch = container_of(kobj, struct klp_patch, kobj);
	if (patch != klp_transition_patch) {
		mutex_unlock(&klp_mutex);
		return -EINVAL;
	}

	klp_force_transition();

	mutex_unlock(&klp_mutex);

	return count;
}

static ssize_t replace_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buf)
{
	struct klp_patch *patch;

	patch = container_of(kobj, struct klp_patch, kobj);
	return sysfs_emit(buf, "%d\n", patch->replace);
}

static ssize_t stack_order_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct klp_patch *patch, *this_patch;
	int stack_order = 0;

	this_patch = container_of(kobj, struct klp_patch, kobj);

	mutex_lock(&klp_mutex);

	klp_for_each_patch(patch) {
		stack_order++;
		if (patch == this_patch)
			break;
	}

	mutex_unlock(&klp_mutex);

	return sysfs_emit(buf, "%d\n", stack_order);
}

static struct kobj_attribute enabled_kobj_attr = __ATTR_RW(enabled);
static struct kobj_attribute transition_kobj_attr = __ATTR_RO(transition);
static struct kobj_attribute force_kobj_attr = __ATTR_WO(force);
static struct kobj_attribute replace_kobj_attr = __ATTR_RO(replace);
static struct kobj_attribute stack_order_kobj_attr = __ATTR_RO(stack_order);
static struct attribute *klp_patch_attrs[] = {
	&enabled_kobj_attr.attr,
	&transition_kobj_attr.attr,
	&force_kobj_attr.attr,
	&replace_kobj_attr.attr,
	&stack_order_kobj_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(klp_patch);

static ssize_t patched_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buf)
{
	struct klp_object *obj;

	obj = container_of(kobj, struct klp_object, kobj);
	return sysfs_emit(buf, "%d\n", obj->patched);
}

static struct kobj_attribute patched_kobj_attr = __ATTR_RO(patched);
static struct attribute *klp_object_attrs[] = {
	&patched_kobj_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(klp_object);

static void klp_free_object_dynamic(struct klp_object *obj)
{
	kfree(obj->name);
	kfree(obj);
}

static void klp_init_func_early(struct klp_object *obj,
				struct klp_func *func);
static void klp_init_object_early(struct klp_patch *patch,
				  struct klp_object *obj);

static struct klp_object *klp_alloc_object_dynamic(const char *name,
						   struct klp_patch *patch)
{
	struct klp_object *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return NULL;

	if (name) {
		obj->name = kstrdup(name, GFP_KERNEL);
		if (!obj->name) {
			kfree(obj);
			return NULL;
		}
	}

	klp_init_object_early(patch, obj);
	obj->dynamic = true;

	return obj;
}

static void klp_free_func_nop(struct klp_func *func)
{
	kfree(func->old_name);
	kfree(func);
}

static struct klp_func *klp_alloc_func_nop(struct klp_func *old_func,
					   struct klp_object *obj)
{
	struct klp_func *func;

	func = kzalloc(sizeof(*func), GFP_KERNEL);
	if (!func)
		return NULL;

	if (old_func->old_name) {
		func->old_name = kstrdup(old_func->old_name, GFP_KERNEL);
		if (!func->old_name) {
			kfree(func);
			return NULL;
		}
	}

	klp_init_func_early(obj, func);
	/*
	 * func->new_func is same as func->old_func. These addresses are
	 * set when the object is loaded, see klp_init_object_loaded().
	 */
	func->old_sympos = old_func->old_sympos;
	func->nop = true;

	return func;
}

static int klp_add_object_nops(struct klp_patch *patch,
			       struct klp_object *old_obj)
{
	struct klp_object *obj;
	struct klp_func *func, *old_func;

	obj = klp_find_object(patch, old_obj);

	if (!obj) {
		obj = klp_alloc_object_dynamic(old_obj->name, patch);
		if (!obj)
			return -ENOMEM;
	}

	klp_for_each_func(old_obj, old_func) {
		func = klp_find_func(obj, old_func);
		if (func)
			continue;

		func = klp_alloc_func_nop(old_func, obj);
		if (!func)
			return -ENOMEM;
	}

	return 0;
}

/*
 * Add 'nop' functions which simply return to the caller to run
 * the original function. The 'nop' functions are added to a
 * patch to facilitate a 'replace' mode.
 */
static int klp_add_nops(struct klp_patch *patch)
{
	struct klp_patch *old_patch;
	struct klp_object *old_obj;

	klp_for_each_patch(old_patch) {
		klp_for_each_object(old_patch, old_obj) {
			int err;

			err = klp_add_object_nops(patch, old_obj);
			if (err)
				return err;
		}
	}

	return 0;
}

static void klp_kobj_release_patch(struct kobject *kobj)
{
	struct klp_patch *patch;

	patch = container_of(kobj, struct klp_patch, kobj);
	complete(&patch->finish);
}

static const struct kobj_type klp_ktype_patch = {
	.release = klp_kobj_release_patch,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = klp_patch_groups,
};

static void klp_kobj_release_object(struct kobject *kobj)
{
	struct klp_object *obj;

	obj = container_of(kobj, struct klp_object, kobj);

	if (obj->dynamic)
		klp_free_object_dynamic(obj);
}

static const struct kobj_type klp_ktype_object = {
	.release = klp_kobj_release_object,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = klp_object_groups,
};

static void klp_kobj_release_func(struct kobject *kobj)
{
	struct klp_func *func;

	func = container_of(kobj, struct klp_func, kobj);

	if (func->nop)
		klp_free_func_nop(func);
}

static const struct kobj_type klp_ktype_func = {
	.release = klp_kobj_release_func,
	.sysfs_ops = &kobj_sysfs_ops,
};

static void __klp_free_funcs(struct klp_object *obj, bool nops_only)
{
	struct klp_func *func, *tmp_func;

	klp_for_each_func_safe(obj, func, tmp_func) {
		if (nops_only && !func->nop)
			continue;

		list_del(&func->node);
		kobject_put(&func->kobj);
	}
}

/* Clean up when a patched object is unloaded */
static void klp_free_object_loaded(struct klp_object *obj)
{
	struct klp_func *func;

	obj->mod = NULL;

	klp_for_each_func(obj, func) {
		func->old_func = NULL;

		if (func->nop)
			func->new_func = NULL;
	}
}

static void __klp_free_objects(struct klp_patch *patch, bool nops_only)
{
	struct klp_object *obj, *tmp_obj;

	klp_for_each_object_safe(patch, obj, tmp_obj) {
		__klp_free_funcs(obj, nops_only);

		if (nops_only && !obj->dynamic)
			continue;

		list_del(&obj->node);
		kobject_put(&obj->kobj);
	}
}

static void klp_free_objects(struct klp_patch *patch)
{
	__klp_free_objects(patch, false);
}

static void klp_free_objects_dynamic(struct klp_patch *patch)
{
	__klp_free_objects(patch, true);
}

/*
 * This function implements the free operations that can be called safely
 * under klp_mutex.
 *
 * The operation must be completed by calling klp_free_patch_finish()
 * outside klp_mutex.
 */
static void klp_free_patch_start(struct klp_patch *patch)
{
	if (!list_empty(&patch->list))
		list_del(&patch->list);

	klp_free_objects(patch);
}

/*
 * This function implements the free part that must be called outside
 * klp_mutex.
 *
 * It must be called after klp_free_patch_start(). And it has to be
 * the last function accessing the livepatch structures when the patch
 * gets disabled.
 */
static void klp_free_patch_finish(struct klp_patch *patch)
{
	/*
	 * Avoid deadlock with enabled_store() sysfs callback by
	 * calling this outside klp_mutex. It is safe because
	 * this is called when the patch gets disabled and it
	 * cannot get enabled again.
	 */
	kobject_put(&patch->kobj);
	wait_for_completion(&patch->finish);

	/* Put the module after the last access to struct klp_patch. */
	if (!patch->forced)
		module_put(patch->mod);
}

/*
 * The livepatch might be freed from sysfs interface created by the patch.
 * This work allows to wait until the interface is destroyed in a separate
 * context.
 */
static void klp_free_patch_work_fn(struct work_struct *work)
{
	struct klp_patch *patch =
		container_of(work, struct klp_patch, free_work);

	klp_free_patch_finish(patch);
}

void klp_free_patch_async(struct klp_patch *patch)
{
	klp_free_patch_start(patch);
	schedule_work(&patch->free_work);
}

void klp_free_replaced_patches_async(struct klp_patch *new_patch)
{
	struct klp_patch *old_patch, *tmp_patch;

	klp_for_each_patch_safe(old_patch, tmp_patch) {
		if (old_patch == new_patch)
			return;
		klp_free_patch_async(old_patch);
	}
}

static int klp_init_func(struct klp_object *obj, struct klp_func *func)
{
	if (!func->old_name)
		return -EINVAL;

	/*
	 * NOPs get the address later. The patched module must be loaded,
	 * see klp_init_object_loaded().
	 */
	if (!func->new_func && !func->nop)
		return -EINVAL;

	if (strlen(func->old_name) >= KSYM_NAME_LEN)
		return -EINVAL;

	INIT_LIST_HEAD(&func->stack_node);
	func->patched = false;
	func->transition = false;

	/* The format for the sysfs directory is <function,sympos> where sympos
	 * is the nth occurrence of this symbol in kallsyms for the patched
	 * object. If the user selects 0 for old_sympos, then 1 will be used
	 * since a unique symbol will be the first occurrence.
	 */
	return kobject_add(&func->kobj, &obj->kobj, "%s,%lu",
			   func->old_name,
			   func->old_sympos ? func->old_sympos : 1);
}

static int klp_write_object_relocs(struct klp_patch *patch,
				   struct klp_object *obj,
				   bool apply)
{
	int i, ret;
	struct klp_modinfo *info = patch->mod->klp_info;

	for (i = 1; i < info->hdr.e_shnum; i++) {
		Elf_Shdr *sec = info->sechdrs + i;

		if (!(sec->sh_flags & SHF_RELA_LIVEPATCH))
			continue;

		ret = klp_write_section_relocs(patch->mod, info->sechdrs,
					       info->secstrings,
					       patch->mod->core_kallsyms.strtab,
					       info->symndx, i, obj->name, apply);
		if (ret)
			return ret;
	}

	return 0;
}

static int klp_apply_object_relocs(struct klp_patch *patch,
				   struct klp_object *obj)
{
	return klp_write_object_relocs(patch, obj, true);
}

static void klp_clear_object_relocs(struct klp_patch *patch,
				    struct klp_object *obj)
{
	klp_write_object_relocs(patch, obj, false);
}

/* parts of the initialization that is done only when the object is loaded */
static int klp_init_object_loaded(struct klp_patch *patch,
				  struct klp_object *obj)
{
	struct klp_func *func;
	int ret;

	if (klp_is_module(obj)) {
		/*
		 * Only write module-specific relocations here
		 * (.klp.rela.{module}.*).  vmlinux-specific relocations were
		 * written earlier during the initialization of the klp module
		 * itself.
		 */
		ret = klp_apply_object_relocs(patch, obj);
		if (ret)
			return ret;
	}

	klp_for_each_func(obj, func) {
		ret = klp_find_object_symbol(obj->name, func->old_name,
					     func->old_sympos,
					     (unsigned long *)&func->old_func);
		if (ret)
			return ret;

		ret = kallsyms_lookup_size_offset((unsigned long)func->old_func,
						  &func->old_size, NULL);
		if (!ret) {
			pr_err("kallsyms size lookup failed for '%s'\n",
			       func->old_name);
			return -ENOENT;
		}

		if (func->nop)
			func->new_func = func->old_func;

		ret = kallsyms_lookup_size_offset((unsigned long)func->new_func,
						  &func->new_size, NULL);
		if (!ret) {
			pr_err("kallsyms size lookup failed for '%s' replacement\n",
			       func->old_name);
			return -ENOENT;
		}
	}

	return 0;
}

static int klp_init_object(struct klp_patch *patch, struct klp_object *obj)
{
	struct klp_func *func;
	int ret;
	const char *name;

	if (klp_is_module(obj) && strlen(obj->name) >= MODULE_NAME_LEN)
		return -EINVAL;

	obj->patched = false;
	obj->mod = NULL;

	klp_find_object_module(obj);

	name = klp_is_module(obj) ? obj->name : "vmlinux";
	ret = kobject_add(&obj->kobj, &patch->kobj, "%s", name);
	if (ret)
		return ret;

	klp_for_each_func(obj, func) {
		ret = klp_init_func(obj, func);
		if (ret)
			return ret;
	}

	if (klp_is_object_loaded(obj))
		ret = klp_init_object_loaded(patch, obj);

	return ret;
}

static void klp_init_func_early(struct klp_object *obj,
				struct klp_func *func)
{
	kobject_init(&func->kobj, &klp_ktype_func);
	list_add_tail(&func->node, &obj->func_list);
}

static void klp_init_object_early(struct klp_patch *patch,
				  struct klp_object *obj)
{
	INIT_LIST_HEAD(&obj->func_list);
	kobject_init(&obj->kobj, &klp_ktype_object);
	list_add_tail(&obj->node, &patch->obj_list);
}

static void klp_init_patch_early(struct klp_patch *patch)
{
	struct klp_object *obj;
	struct klp_func *func;

	INIT_LIST_HEAD(&patch->list);
	INIT_LIST_HEAD(&patch->obj_list);
	kobject_init(&patch->kobj, &klp_ktype_patch);
	patch->enabled = false;
	patch->forced = false;
	INIT_WORK(&patch->free_work, klp_free_patch_work_fn);
	init_completion(&patch->finish);

	klp_for_each_object_static(patch, obj) {
		klp_init_object_early(patch, obj);

		klp_for_each_func_static(obj, func) {
			klp_init_func_early(obj, func);
		}
	}
}

static int klp_init_patch(struct klp_patch *patch)
{
	struct klp_object *obj;
	int ret;

	ret = kobject_add(&patch->kobj, klp_root_kobj, "%s", patch->mod->name);
	if (ret)
		return ret;

	if (patch->replace) {
		ret = klp_add_nops(patch);
		if (ret)
			return ret;
	}

	klp_for_each_object(patch, obj) {
		ret = klp_init_object(patch, obj);
		if (ret)
			return ret;
	}

	list_add_tail(&patch->list, &klp_patches);

	return 0;
}

static int __klp_disable_patch(struct klp_patch *patch)
{
	struct klp_object *obj;

	if (WARN_ON(!patch->enabled))
		return -EINVAL;

	if (klp_transition_patch)
		return -EBUSY;

	klp_init_transition(patch, KLP_TRANSITION_UNPATCHED);

	klp_for_each_object(patch, obj)
		if (obj->patched)
			klp_pre_unpatch_callback(obj);

	/*
	 * Enforce the order of the func->transition writes in
	 * klp_init_transition() and the TIF_PATCH_PENDING writes in
	 * klp_start_transition().  In the rare case where klp_ftrace_handler()
	 * is called shortly after klp_update_patch_state() switches the task,
	 * this ensures the handler sees that func->transition is set.
	 */
	smp_wmb();

	klp_start_transition();
	patch->enabled = false;
	klp_try_complete_transition();

	return 0;
}

static int __klp_enable_patch(struct klp_patch *patch)
{
	struct klp_object *obj;
	int ret;

	if (klp_transition_patch)
		return -EBUSY;

	if (WARN_ON(patch->enabled))
		return -EINVAL;

	pr_notice("enabling patch '%s'\n", patch->mod->name);

	klp_init_transition(patch, KLP_TRANSITION_PATCHED);

	/*
	 * Enforce the order of the func->transition writes in
	 * klp_init_transition() and the ops->func_stack writes in
	 * klp_patch_object(), so that klp_ftrace_handler() will see the
	 * func->transition updates before the handler is registered and the
	 * new funcs become visible to the handler.
	 */
	smp_wmb();

	klp_for_each_object(patch, obj) {
		if (!klp_is_object_loaded(obj))
			continue;

		ret = klp_pre_patch_callback(obj);
		if (ret) {
			pr_warn("pre-patch callback failed for object '%s'\n",
				klp_is_module(obj) ? obj->name : "vmlinux");
			goto err;
		}

		ret = klp_patch_object(obj);
		if (ret) {
			pr_warn("failed to patch object '%s'\n",
				klp_is_module(obj) ? obj->name : "vmlinux");
			goto err;
		}
	}

	klp_start_transition();
	patch->enabled = true;
	klp_try_complete_transition();

	return 0;
err:
	pr_warn("failed to enable patch '%s'\n", patch->mod->name);

	klp_cancel_transition();
	return ret;
}

/**
 * klp_enable_patch() - enable the livepatch
 * @patch:	patch to be enabled
 *
 * Initializes the data structure associated with the patch, creates the sysfs
 * interface, performs the needed symbol lookups and code relocations,
 * registers the patched functions with ftrace.
 *
 * This function is supposed to be called from the livepatch module_init()
 * callback.
 *
 * Return: 0 on success, otherwise error
 */
int klp_enable_patch(struct klp_patch *patch)
{
	int ret;
	struct klp_object *obj;

	if (!patch || !patch->mod || !patch->objs)
		return -EINVAL;

	klp_for_each_object_static(patch, obj) {
		if (!obj->funcs)
			return -EINVAL;
	}


	if (!is_livepatch_module(patch->mod)) {
		pr_err("module %s is not marked as a livepatch module\n",
		       patch->mod->name);
		return -EINVAL;
	}

	if (!klp_initialized())
		return -ENODEV;

	if (!klp_have_reliable_stack()) {
		pr_warn("This architecture doesn't have support for the livepatch consistency model.\n");
		pr_warn("The livepatch transition may never complete.\n");
	}

	mutex_lock(&klp_mutex);

	if (!klp_is_patch_compatible(patch)) {
		pr_err("Livepatch patch (%s) is not compatible with the already installed livepatches.\n",
			patch->mod->name);
		mutex_unlock(&klp_mutex);
		return -EINVAL;
	}

	if (!try_module_get(patch->mod)) {
		mutex_unlock(&klp_mutex);
		return -ENODEV;
	}

	klp_init_patch_early(patch);

	ret = klp_init_patch(patch);
	if (ret)
		goto err;

	ret = __klp_enable_patch(patch);
	if (ret)
		goto err;

	mutex_unlock(&klp_mutex);

	return 0;

err:
	klp_free_patch_start(patch);

	mutex_unlock(&klp_mutex);

	klp_free_patch_finish(patch);

	return ret;
}
EXPORT_SYMBOL_GPL(klp_enable_patch);

/*
 * This function unpatches objects from the replaced livepatches.
 *
 * We could be pretty aggressive here. It is called in the situation where
 * these structures are no longer accessed from the ftrace handler.
 * All functions are redirected by the klp_transition_patch. They
 * use either a new code or they are in the original code because
 * of the special nop function patches.
 *
 * The only exception is when the transition was forced. In this case,
 * klp_ftrace_handler() might still see the replaced patch on the stack.
 * Fortunately, it is carefully designed to work with removed functions
 * thanks to RCU. We only have to keep the patches on the system. Also
 * this is handled transparently by patch->module_put.
 */
void klp_unpatch_replaced_patches(struct klp_patch *new_patch)
{
	struct klp_patch *old_patch;

	klp_for_each_patch(old_patch) {
		if (old_patch == new_patch)
			return;

		old_patch->enabled = false;
		klp_unpatch_objects(old_patch);
	}
}

/*
 * This function removes the dynamically allocated 'nop' functions.
 *
 * We could be pretty aggressive. NOPs do not change the existing
 * behavior except for adding unnecessary delay by the ftrace handler.
 *
 * It is safe even when the transition was forced. The ftrace handler
 * will see a valid ops->func_stack entry thanks to RCU.
 *
 * We could even free the NOPs structures. They must be the last entry
 * in ops->func_stack. Therefore unregister_ftrace_function() is called.
 * It does the same as klp_synchronize_transition() to make sure that
 * nobody is inside the ftrace handler once the operation finishes.
 *
 * IMPORTANT: It must be called right after removing the replaced patches!
 */
void klp_discard_nops(struct klp_patch *new_patch)
{
	klp_unpatch_objects_dynamic(klp_transition_patch);
	klp_free_objects_dynamic(klp_transition_patch);
}

/*
 * Remove parts of patches that touch a given kernel module. The list of
 * patches processed might be limited. When limit is NULL, all patches
 * will be handled.
 */
static void klp_cleanup_module_patches_limited(struct module *mod,
					       struct klp_patch *limit)
{
	struct klp_patch *patch;
	struct klp_object *obj;

	klp_for_each_patch(patch) {
		if (patch == limit)
			break;

		klp_for_each_object(patch, obj) {
			if (!klp_is_module(obj) || strcmp(obj->name, mod->name))
				continue;

			if (patch != klp_transition_patch)
				klp_pre_unpatch_callback(obj);

			pr_notice("reverting patch '%s' on unloading module '%s'\n",
				  patch->mod->name, obj->mod->name);
			klp_unpatch_object(obj);

			klp_post_unpatch_callback(obj);
			klp_clear_object_relocs(patch, obj);
			klp_free_object_loaded(obj);
			break;
		}
	}
}

int klp_module_coming(struct module *mod)
{
	int ret;
	struct klp_patch *patch;
	struct klp_object *obj;

	if (WARN_ON(mod->state != MODULE_STATE_COMING))
		return -EINVAL;

	if (!strcmp(mod->name, "vmlinux")) {
		pr_err("vmlinux.ko: invalid module name\n");
		return -EINVAL;
	}

	mutex_lock(&klp_mutex);
	/*
	 * Each module has to know that klp_module_coming()
	 * has been called. We never know what module will
	 * get patched by a new patch.
	 */
	mod->klp_alive = true;

	klp_for_each_patch(patch) {
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

			pr_notice("applying patch '%s' to loading module '%s'\n",
				  patch->mod->name, obj->mod->name);

			ret = klp_pre_patch_callback(obj);
			if (ret) {
				pr_warn("pre-patch callback failed for object '%s'\n",
					obj->name);
				goto err;
			}

			ret = klp_patch_object(obj);
			if (ret) {
				pr_warn("failed to apply patch '%s' to module '%s' (%d)\n",
					patch->mod->name, obj->mod->name, ret);

				klp_post_unpatch_callback(obj);
				goto err;
			}

			if (patch != klp_transition_patch)
				klp_post_patch_callback(obj);

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
	obj->mod = NULL;
	klp_cleanup_module_patches_limited(mod, patch);
	mutex_unlock(&klp_mutex);

	return ret;
}

void klp_module_going(struct module *mod)
{
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

	klp_cleanup_module_patches_limited(mod, NULL);

	mutex_unlock(&klp_mutex);
}

static int __init klp_init(void)
{
	klp_root_kobj = kobject_create_and_add("livepatch", kernel_kobj);
	if (!klp_root_kobj)
		return -ENOMEM;

	return 0;
}

module_init(klp_init);
