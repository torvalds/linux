// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2002 Richard Henderson
 * Copyright (C) 2001 Rusty Russell, 2002, 2010 Rusty Russell IBM.
 * Copyright (C) 2023 Luis Chamberlain <mcgrof@kernel.org>
 */

#define INCLUDE_VERMAGIC

#include <linux/export.h>
#include <linux/extable.h>
#include <linux/moduleloader.h>
#include <linux/module_signature.h>
#include <linux/trace_events.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/buildid.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kernel_read_file.h>
#include <linux/kstrtox.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/elf.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/rcupdate.h>
#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/vermagic.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <linux/set_memory.h>
#include <asm/mmu_context.h>
#include <linux/license.h>
#include <asm/sections.h>
#include <linux/tracepoint.h>
#include <linux/ftrace.h>
#include <linux/livepatch.h>
#include <linux/async.h>
#include <linux/percpu.h>
#include <linux/kmemleak.h>
#include <linux/jump_label.h>
#include <linux/pfn.h>
#include <linux/bsearch.h>
#include <linux/dynamic_debug.h>
#include <linux/audit.h>
#include <linux/cfi.h>
#include <linux/codetag.h>
#include <linux/debugfs.h>
#include <linux/execmem.h>
#include <uapi/linux/module.h>
#include "internal.h"

#define CREATE_TRACE_POINTS
#include <trace/events/module.h>

/*
 * Mutex protects:
 * 1) List of modules (also safely readable within RCU read section),
 * 2) module_use links,
 * 3) mod_tree.addr_min/mod_tree.addr_max.
 * (delete and add uses RCU list operations).
 */
DEFINE_MUTEX(module_mutex);
LIST_HEAD(modules);

/* Work queue for freeing init sections in success case */
static void do_free_init(struct work_struct *w);
static DECLARE_WORK(init_free_wq, do_free_init);
static LLIST_HEAD(init_free_list);

struct mod_tree_root mod_tree __cacheline_aligned = {
	.addr_min = -1UL,
};

struct symsearch {
	const struct kernel_symbol *start, *stop;
	const u32 *crcs;
	enum mod_license license;
};

/*
 * Bounds of module memory, for speeding up __module_address.
 * Protected by module_mutex.
 */
static void __mod_update_bounds(enum mod_mem_type type __maybe_unused, void *base,
				unsigned int size, struct mod_tree_root *tree)
{
	unsigned long min = (unsigned long)base;
	unsigned long max = min + size;

#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
	if (mod_mem_type_is_core_data(type)) {
		if (min < tree->data_addr_min)
			tree->data_addr_min = min;
		if (max > tree->data_addr_max)
			tree->data_addr_max = max;
		return;
	}
#endif
	if (min < tree->addr_min)
		tree->addr_min = min;
	if (max > tree->addr_max)
		tree->addr_max = max;
}

static void mod_update_bounds(struct module *mod)
{
	for_each_mod_mem_type(type) {
		struct module_memory *mod_mem = &mod->mem[type];

		if (mod_mem->size)
			__mod_update_bounds(type, mod_mem->base, mod_mem->size, &mod_tree);
	}
}

/* Block module loading/unloading? */
int modules_disabled;
core_param(nomodule, modules_disabled, bint, 0);

/* Waiting for a module to finish initializing? */
static DECLARE_WAIT_QUEUE_HEAD(module_wq);

static BLOCKING_NOTIFIER_HEAD(module_notify_list);

int register_module_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&module_notify_list, nb);
}
EXPORT_SYMBOL(register_module_notifier);

int unregister_module_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&module_notify_list, nb);
}
EXPORT_SYMBOL(unregister_module_notifier);

/*
 * We require a truly strong try_module_get(): 0 means success.
 * Otherwise an error is returned due to ongoing or failed
 * initialization etc.
 */
static inline int strong_try_module_get(struct module *mod)
{
	BUG_ON(mod && mod->state == MODULE_STATE_UNFORMED);
	if (mod && mod->state == MODULE_STATE_COMING)
		return -EBUSY;
	if (try_module_get(mod))
		return 0;
	else
		return -ENOENT;
}

static inline void add_taint_module(struct module *mod, unsigned flag,
				    enum lockdep_ok lockdep_ok)
{
	add_taint(flag, lockdep_ok);
	set_bit(flag, &mod->taints);
}

/*
 * Like strncmp(), except s/-/_/g as per scripts/Makefile.lib:name-fix-token rule.
 */
static int mod_strncmp(const char *str_a, const char *str_b, size_t n)
{
	for (int i = 0; i < n; i++) {
		char a = str_a[i];
		char b = str_b[i];
		int d;

		if (a == '-') a = '_';
		if (b == '-') b = '_';

		d = a - b;
		if (d)
			return d;

		if (!a)
			break;
	}

	return 0;
}

/*
 * A thread that wants to hold a reference to a module only while it
 * is running can call this to safely exit.
 */
void __noreturn __module_put_and_kthread_exit(struct module *mod, long code)
{
	module_put(mod);
	kthread_exit(code);
}
EXPORT_SYMBOL(__module_put_and_kthread_exit);

/* Find a module section: 0 means not found. */
static unsigned int find_sec(const struct load_info *info, const char *name)
{
	unsigned int i;

	for (i = 1; i < info->hdr->e_shnum; i++) {
		Elf_Shdr *shdr = &info->sechdrs[i];
		/* Alloc bit cleared means "ignore it." */
		if ((shdr->sh_flags & SHF_ALLOC)
		    && strcmp(info->secstrings + shdr->sh_name, name) == 0)
			return i;
	}
	return 0;
}

/**
 * find_any_unique_sec() - Find a unique section index by name
 * @info: Load info for the module to scan
 * @name: Name of the section we're looking for
 *
 * Locates a unique section by name. Ignores SHF_ALLOC.
 *
 * Return: Section index if found uniquely, zero if absent, negative count
 *         of total instances if multiple were found.
 */
static int find_any_unique_sec(const struct load_info *info, const char *name)
{
	unsigned int idx;
	unsigned int count = 0;
	int i;

	for (i = 1; i < info->hdr->e_shnum; i++) {
		if (strcmp(info->secstrings + info->sechdrs[i].sh_name,
			   name) == 0) {
			count++;
			idx = i;
		}
	}
	if (count == 1) {
		return idx;
	} else if (count == 0) {
		return 0;
	} else {
		return -count;
	}
}

/* Find a module section, or NULL. */
static void *section_addr(const struct load_info *info, const char *name)
{
	/* Section 0 has sh_addr 0. */
	return (void *)info->sechdrs[find_sec(info, name)].sh_addr;
}

/* Find a module section, or NULL.  Fill in number of "objects" in section. */
static void *section_objs(const struct load_info *info,
			  const char *name,
			  size_t object_size,
			  unsigned int *num)
{
	unsigned int sec = find_sec(info, name);

	/* Section 0 has sh_addr 0 and sh_size 0. */
	*num = info->sechdrs[sec].sh_size / object_size;
	return (void *)info->sechdrs[sec].sh_addr;
}

/* Find a module section: 0 means not found. Ignores SHF_ALLOC flag. */
static unsigned int find_any_sec(const struct load_info *info, const char *name)
{
	unsigned int i;

	for (i = 1; i < info->hdr->e_shnum; i++) {
		Elf_Shdr *shdr = &info->sechdrs[i];
		if (strcmp(info->secstrings + shdr->sh_name, name) == 0)
			return i;
	}
	return 0;
}

/*
 * Find a module section, or NULL. Fill in number of "objects" in section.
 * Ignores SHF_ALLOC flag.
 */
static __maybe_unused void *any_section_objs(const struct load_info *info,
					     const char *name,
					     size_t object_size,
					     unsigned int *num)
{
	unsigned int sec = find_any_sec(info, name);

	/* Section 0 has sh_addr 0 and sh_size 0. */
	*num = info->sechdrs[sec].sh_size / object_size;
	return (void *)info->sechdrs[sec].sh_addr;
}

#ifndef CONFIG_MODVERSIONS
#define symversion(base, idx) NULL
#else
#define symversion(base, idx) ((base != NULL) ? ((base) + (idx)) : NULL)
#endif

static const char *kernel_symbol_name(const struct kernel_symbol *sym)
{
#ifdef CONFIG_HAVE_ARCH_PREL32_RELOCATIONS
	return offset_to_ptr(&sym->name_offset);
#else
	return sym->name;
#endif
}

static const char *kernel_symbol_namespace(const struct kernel_symbol *sym)
{
#ifdef CONFIG_HAVE_ARCH_PREL32_RELOCATIONS
	if (!sym->namespace_offset)
		return NULL;
	return offset_to_ptr(&sym->namespace_offset);
#else
	return sym->namespace;
#endif
}

int cmp_name(const void *name, const void *sym)
{
	return strcmp(name, kernel_symbol_name(sym));
}

static bool find_exported_symbol_in_section(const struct symsearch *syms,
					    struct module *owner,
					    struct find_symbol_arg *fsa)
{
	struct kernel_symbol *sym;

	if (!fsa->gplok && syms->license == GPL_ONLY)
		return false;

	sym = bsearch(fsa->name, syms->start, syms->stop - syms->start,
			sizeof(struct kernel_symbol), cmp_name);
	if (!sym)
		return false;

	fsa->owner = owner;
	fsa->crc = symversion(syms->crcs, sym - syms->start);
	fsa->sym = sym;
	fsa->license = syms->license;

	return true;
}

/*
 * Find an exported symbol and return it, along with, (optional) crc and
 * (optional) module which owns it. Needs RCU or module_mutex.
 */
bool find_symbol(struct find_symbol_arg *fsa)
{
	static const struct symsearch arr[] = {
		{ __start___ksymtab, __stop___ksymtab, __start___kcrctab,
		  NOT_GPL_ONLY },
		{ __start___ksymtab_gpl, __stop___ksymtab_gpl,
		  __start___kcrctab_gpl,
		  GPL_ONLY },
	};
	struct module *mod;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(arr); i++)
		if (find_exported_symbol_in_section(&arr[i], NULL, fsa))
			return true;

	list_for_each_entry_rcu(mod, &modules, list,
				lockdep_is_held(&module_mutex)) {
		struct symsearch arr[] = {
			{ mod->syms, mod->syms + mod->num_syms, mod->crcs,
			  NOT_GPL_ONLY },
			{ mod->gpl_syms, mod->gpl_syms + mod->num_gpl_syms,
			  mod->gpl_crcs,
			  GPL_ONLY },
		};

		if (mod->state == MODULE_STATE_UNFORMED)
			continue;

		for (i = 0; i < ARRAY_SIZE(arr); i++)
			if (find_exported_symbol_in_section(&arr[i], mod, fsa))
				return true;
	}

	pr_debug("Failed to find symbol %s\n", fsa->name);
	return false;
}

/*
 * Search for module by name: must hold module_mutex (or RCU for read-only
 * access).
 */
struct module *find_module_all(const char *name, size_t len,
			       bool even_unformed)
{
	struct module *mod;

	list_for_each_entry_rcu(mod, &modules, list,
				lockdep_is_held(&module_mutex)) {
		if (!even_unformed && mod->state == MODULE_STATE_UNFORMED)
			continue;
		if (strlen(mod->name) == len && !memcmp(mod->name, name, len))
			return mod;
	}
	return NULL;
}

struct module *find_module(const char *name)
{
	return find_module_all(name, strlen(name), false);
}

#ifdef CONFIG_SMP

static inline void __percpu *mod_percpu(struct module *mod)
{
	return mod->percpu;
}

static int percpu_modalloc(struct module *mod, struct load_info *info)
{
	Elf_Shdr *pcpusec = &info->sechdrs[info->index.pcpu];
	unsigned long align = pcpusec->sh_addralign;

	if (!pcpusec->sh_size)
		return 0;

	if (align > PAGE_SIZE) {
		pr_warn("%s: per-cpu alignment %li > %li\n",
			mod->name, align, PAGE_SIZE);
		align = PAGE_SIZE;
	}

	mod->percpu = __alloc_reserved_percpu(pcpusec->sh_size, align);
	if (!mod->percpu) {
		pr_warn("%s: Could not allocate %lu bytes percpu data\n",
			mod->name, (unsigned long)pcpusec->sh_size);
		return -ENOMEM;
	}
	mod->percpu_size = pcpusec->sh_size;
	return 0;
}

static void percpu_modfree(struct module *mod)
{
	free_percpu(mod->percpu);
}

static unsigned int find_pcpusec(struct load_info *info)
{
	return find_sec(info, ".data..percpu");
}

static void percpu_modcopy(struct module *mod,
			   const void *from, unsigned long size)
{
	int cpu;

	for_each_possible_cpu(cpu)
		memcpy(per_cpu_ptr(mod->percpu, cpu), from, size);
}

bool __is_module_percpu_address(unsigned long addr, unsigned long *can_addr)
{
	struct module *mod;
	unsigned int cpu;

	guard(rcu)();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (mod->state == MODULE_STATE_UNFORMED)
			continue;
		if (!mod->percpu_size)
			continue;
		for_each_possible_cpu(cpu) {
			void *start = per_cpu_ptr(mod->percpu, cpu);
			void *va = (void *)addr;

			if (va >= start && va < start + mod->percpu_size) {
				if (can_addr) {
					*can_addr = (unsigned long) (va - start);
					*can_addr += (unsigned long)
						per_cpu_ptr(mod->percpu,
							    get_boot_cpu_id());
				}
				return true;
			}
		}
	}
	return false;
}

/**
 * is_module_percpu_address() - test whether address is from module static percpu
 * @addr: address to test
 *
 * Test whether @addr belongs to module static percpu area.
 *
 * Return: %true if @addr is from module static percpu area
 */
bool is_module_percpu_address(unsigned long addr)
{
	return __is_module_percpu_address(addr, NULL);
}

#else /* ... !CONFIG_SMP */

static inline void __percpu *mod_percpu(struct module *mod)
{
	return NULL;
}
static int percpu_modalloc(struct module *mod, struct load_info *info)
{
	/* UP modules shouldn't have this section: ENOMEM isn't quite right */
	if (info->sechdrs[info->index.pcpu].sh_size != 0)
		return -ENOMEM;
	return 0;
}
static inline void percpu_modfree(struct module *mod)
{
}
static unsigned int find_pcpusec(struct load_info *info)
{
	return 0;
}
static inline void percpu_modcopy(struct module *mod,
				  const void *from, unsigned long size)
{
	/* pcpusec should be 0, and size of that section should be 0. */
	BUG_ON(size != 0);
}
bool is_module_percpu_address(unsigned long addr)
{
	return false;
}

bool __is_module_percpu_address(unsigned long addr, unsigned long *can_addr)
{
	return false;
}

#endif /* CONFIG_SMP */

#define MODINFO_ATTR(field)	\
static void setup_modinfo_##field(struct module *mod, const char *s)  \
{                                                                     \
	mod->field = kstrdup(s, GFP_KERNEL);                          \
}                                                                     \
static ssize_t show_modinfo_##field(const struct module_attribute *mattr, \
			struct module_kobject *mk, char *buffer)      \
{                                                                     \
	return scnprintf(buffer, PAGE_SIZE, "%s\n", mk->mod->field);  \
}                                                                     \
static int modinfo_##field##_exists(struct module *mod)               \
{                                                                     \
	return mod->field != NULL;                                    \
}                                                                     \
static void free_modinfo_##field(struct module *mod)                  \
{                                                                     \
	kfree(mod->field);                                            \
	mod->field = NULL;                                            \
}                                                                     \
static const struct module_attribute modinfo_##field = {              \
	.attr = { .name = __stringify(field), .mode = 0444 },         \
	.show = show_modinfo_##field,                                 \
	.setup = setup_modinfo_##field,                               \
	.test = modinfo_##field##_exists,                             \
	.free = free_modinfo_##field,                                 \
};

MODINFO_ATTR(version);
MODINFO_ATTR(srcversion);

static struct {
	char name[MODULE_NAME_LEN + 1];
	char taints[MODULE_FLAGS_BUF_SIZE];
} last_unloaded_module;

#ifdef CONFIG_MODULE_UNLOAD

EXPORT_TRACEPOINT_SYMBOL(module_get);

/* MODULE_REF_BASE is the base reference count by kmodule loader. */
#define MODULE_REF_BASE	1

/* Init the unload section of the module. */
static int module_unload_init(struct module *mod)
{
	/*
	 * Initialize reference counter to MODULE_REF_BASE.
	 * refcnt == 0 means module is going.
	 */
	atomic_set(&mod->refcnt, MODULE_REF_BASE);

	INIT_LIST_HEAD(&mod->source_list);
	INIT_LIST_HEAD(&mod->target_list);

	/* Hold reference count during initialization. */
	atomic_inc(&mod->refcnt);

	return 0;
}

/* Does a already use b? */
static int already_uses(struct module *a, struct module *b)
{
	struct module_use *use;

	list_for_each_entry(use, &b->source_list, source_list) {
		if (use->source == a)
			return 1;
	}
	pr_debug("%s does not use %s!\n", a->name, b->name);
	return 0;
}

/*
 * Module a uses b
 *  - we add 'a' as a "source", 'b' as a "target" of module use
 *  - the module_use is added to the list of 'b' sources (so
 *    'b' can walk the list to see who sourced them), and of 'a'
 *    targets (so 'a' can see what modules it targets).
 */
static int add_module_usage(struct module *a, struct module *b)
{
	struct module_use *use;

	pr_debug("Allocating new usage for %s.\n", a->name);
	use = kmalloc(sizeof(*use), GFP_ATOMIC);
	if (!use)
		return -ENOMEM;

	use->source = a;
	use->target = b;
	list_add(&use->source_list, &b->source_list);
	list_add(&use->target_list, &a->target_list);
	return 0;
}

/* Module a uses b: caller needs module_mutex() */
static int ref_module(struct module *a, struct module *b)
{
	int err;

	if (b == NULL || already_uses(a, b))
		return 0;

	/* If module isn't available, we fail. */
	err = strong_try_module_get(b);
	if (err)
		return err;

	err = add_module_usage(a, b);
	if (err) {
		module_put(b);
		return err;
	}
	return 0;
}

/* Clear the unload stuff of the module. */
static void module_unload_free(struct module *mod)
{
	struct module_use *use, *tmp;

	mutex_lock(&module_mutex);
	list_for_each_entry_safe(use, tmp, &mod->target_list, target_list) {
		struct module *i = use->target;
		pr_debug("%s unusing %s\n", mod->name, i->name);
		module_put(i);
		list_del(&use->source_list);
		list_del(&use->target_list);
		kfree(use);
	}
	mutex_unlock(&module_mutex);
}

#ifdef CONFIG_MODULE_FORCE_UNLOAD
static inline int try_force_unload(unsigned int flags)
{
	int ret = (flags & O_TRUNC);
	if (ret)
		add_taint(TAINT_FORCED_RMMOD, LOCKDEP_NOW_UNRELIABLE);
	return ret;
}
#else
static inline int try_force_unload(unsigned int flags)
{
	return 0;
}
#endif /* CONFIG_MODULE_FORCE_UNLOAD */

/* Try to release refcount of module, 0 means success. */
static int try_release_module_ref(struct module *mod)
{
	int ret;

	/* Try to decrement refcnt which we set at loading */
	ret = atomic_sub_return(MODULE_REF_BASE, &mod->refcnt);
	BUG_ON(ret < 0);
	if (ret)
		/* Someone can put this right now, recover with checking */
		ret = atomic_add_unless(&mod->refcnt, MODULE_REF_BASE, 0);

	return ret;
}

static int try_stop_module(struct module *mod, int flags, int *forced)
{
	/* If it's not unused, quit unless we're forcing. */
	if (try_release_module_ref(mod) != 0) {
		*forced = try_force_unload(flags);
		if (!(*forced))
			return -EWOULDBLOCK;
	}

	/* Mark it as dying. */
	mod->state = MODULE_STATE_GOING;

	return 0;
}

/**
 * module_refcount() - return the refcount or -1 if unloading
 * @mod:	the module we're checking
 *
 * Return:
 *	-1 if the module is in the process of unloading
 *	otherwise the number of references in the kernel to the module
 */
int module_refcount(struct module *mod)
{
	return atomic_read(&mod->refcnt) - MODULE_REF_BASE;
}
EXPORT_SYMBOL(module_refcount);

/* This exists whether we can unload or not */
static void free_module(struct module *mod);

SYSCALL_DEFINE2(delete_module, const char __user *, name_user,
		unsigned int, flags)
{
	struct module *mod;
	char name[MODULE_NAME_LEN];
	char buf[MODULE_FLAGS_BUF_SIZE];
	int ret, forced = 0;

	if (!capable(CAP_SYS_MODULE) || modules_disabled)
		return -EPERM;

	if (strncpy_from_user(name, name_user, MODULE_NAME_LEN-1) < 0)
		return -EFAULT;
	name[MODULE_NAME_LEN-1] = '\0';

	audit_log_kern_module(name);

	if (mutex_lock_interruptible(&module_mutex) != 0)
		return -EINTR;

	mod = find_module(name);
	if (!mod) {
		ret = -ENOENT;
		goto out;
	}

	if (!list_empty(&mod->source_list)) {
		/* Other modules depend on us: get rid of them first. */
		ret = -EWOULDBLOCK;
		goto out;
	}

	/* Doing init or already dying? */
	if (mod->state != MODULE_STATE_LIVE) {
		/* FIXME: if (force), slam module count damn the torpedoes */
		pr_debug("%s already dying\n", mod->name);
		ret = -EBUSY;
		goto out;
	}

	/* If it has an init func, it must have an exit func to unload */
	if (mod->init && !mod->exit) {
		forced = try_force_unload(flags);
		if (!forced) {
			/* This module can't be removed */
			ret = -EBUSY;
			goto out;
		}
	}

	ret = try_stop_module(mod, flags, &forced);
	if (ret != 0)
		goto out;

	mutex_unlock(&module_mutex);
	/* Final destruction now no one is using it. */
	if (mod->exit != NULL)
		mod->exit();
	blocking_notifier_call_chain(&module_notify_list,
				     MODULE_STATE_GOING, mod);
	klp_module_going(mod);
	ftrace_release_mod(mod);

	async_synchronize_full();

	/* Store the name and taints of the last unloaded module for diagnostic purposes */
	strscpy(last_unloaded_module.name, mod->name);
	strscpy(last_unloaded_module.taints, module_flags(mod, buf, false));

	free_module(mod);
	/* someone could wait for the module in add_unformed_module() */
	wake_up_all(&module_wq);
	return 0;
out:
	mutex_unlock(&module_mutex);
	return ret;
}

void __symbol_put(const char *symbol)
{
	struct find_symbol_arg fsa = {
		.name	= symbol,
		.gplok	= true,
	};

	guard(rcu)();
	BUG_ON(!find_symbol(&fsa));
	module_put(fsa.owner);
}
EXPORT_SYMBOL(__symbol_put);

/* Note this assumes addr is a function, which it currently always is. */
void symbol_put_addr(void *addr)
{
	struct module *modaddr;
	unsigned long a = (unsigned long)dereference_function_descriptor(addr);

	if (core_kernel_text(a))
		return;

	/*
	 * Even though we hold a reference on the module; we still need to
	 * RCU read section in order to safely traverse the data structure.
	 */
	guard(rcu)();
	modaddr = __module_text_address(a);
	BUG_ON(!modaddr);
	module_put(modaddr);
}
EXPORT_SYMBOL_GPL(symbol_put_addr);

static ssize_t show_refcnt(const struct module_attribute *mattr,
			   struct module_kobject *mk, char *buffer)
{
	return sprintf(buffer, "%i\n", module_refcount(mk->mod));
}

static const struct module_attribute modinfo_refcnt =
	__ATTR(refcnt, 0444, show_refcnt, NULL);

void __module_get(struct module *module)
{
	if (module) {
		atomic_inc(&module->refcnt);
		trace_module_get(module, _RET_IP_);
	}
}
EXPORT_SYMBOL(__module_get);

bool try_module_get(struct module *module)
{
	bool ret = true;

	if (module) {
		/* Note: here, we can fail to get a reference */
		if (likely(module_is_live(module) &&
			   atomic_inc_not_zero(&module->refcnt) != 0))
			trace_module_get(module, _RET_IP_);
		else
			ret = false;
	}
	return ret;
}
EXPORT_SYMBOL(try_module_get);

void module_put(struct module *module)
{
	int ret;

	if (module) {
		ret = atomic_dec_if_positive(&module->refcnt);
		WARN_ON(ret < 0);	/* Failed to put refcount */
		trace_module_put(module, _RET_IP_);
	}
}
EXPORT_SYMBOL(module_put);

#else /* !CONFIG_MODULE_UNLOAD */
static inline void module_unload_free(struct module *mod)
{
}

static int ref_module(struct module *a, struct module *b)
{
	return strong_try_module_get(b);
}

static inline int module_unload_init(struct module *mod)
{
	return 0;
}
#endif /* CONFIG_MODULE_UNLOAD */

size_t module_flags_taint(unsigned long taints, char *buf)
{
	size_t l = 0;
	int i;

	for (i = 0; i < TAINT_FLAGS_COUNT; i++) {
		if (taint_flags[i].module && test_bit(i, &taints))
			buf[l++] = taint_flags[i].c_true;
	}

	return l;
}

static ssize_t show_initstate(const struct module_attribute *mattr,
			      struct module_kobject *mk, char *buffer)
{
	const char *state = "unknown";

	switch (mk->mod->state) {
	case MODULE_STATE_LIVE:
		state = "live";
		break;
	case MODULE_STATE_COMING:
		state = "coming";
		break;
	case MODULE_STATE_GOING:
		state = "going";
		break;
	default:
		BUG();
	}
	return sprintf(buffer, "%s\n", state);
}

static const struct module_attribute modinfo_initstate =
	__ATTR(initstate, 0444, show_initstate, NULL);

static ssize_t store_uevent(const struct module_attribute *mattr,
			    struct module_kobject *mk,
			    const char *buffer, size_t count)
{
	int rc;

	rc = kobject_synth_uevent(&mk->kobj, buffer, count);
	return rc ? rc : count;
}

const struct module_attribute module_uevent =
	__ATTR(uevent, 0200, NULL, store_uevent);

static ssize_t show_coresize(const struct module_attribute *mattr,
			     struct module_kobject *mk, char *buffer)
{
	unsigned int size = mk->mod->mem[MOD_TEXT].size;

	if (!IS_ENABLED(CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC)) {
		for_class_mod_mem_type(type, core_data)
			size += mk->mod->mem[type].size;
	}
	return sprintf(buffer, "%u\n", size);
}

static const struct module_attribute modinfo_coresize =
	__ATTR(coresize, 0444, show_coresize, NULL);

#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
static ssize_t show_datasize(const struct module_attribute *mattr,
			     struct module_kobject *mk, char *buffer)
{
	unsigned int size = 0;

	for_class_mod_mem_type(type, core_data)
		size += mk->mod->mem[type].size;
	return sprintf(buffer, "%u\n", size);
}

static const struct module_attribute modinfo_datasize =
	__ATTR(datasize, 0444, show_datasize, NULL);
#endif

static ssize_t show_initsize(const struct module_attribute *mattr,
			     struct module_kobject *mk, char *buffer)
{
	unsigned int size = 0;

	for_class_mod_mem_type(type, init)
		size += mk->mod->mem[type].size;
	return sprintf(buffer, "%u\n", size);
}

static const struct module_attribute modinfo_initsize =
	__ATTR(initsize, 0444, show_initsize, NULL);

static ssize_t show_taint(const struct module_attribute *mattr,
			  struct module_kobject *mk, char *buffer)
{
	size_t l;

	l = module_flags_taint(mk->mod->taints, buffer);
	buffer[l++] = '\n';
	return l;
}

static const struct module_attribute modinfo_taint =
	__ATTR(taint, 0444, show_taint, NULL);

const struct module_attribute *const modinfo_attrs[] = {
	&module_uevent,
	&modinfo_version,
	&modinfo_srcversion,
	&modinfo_initstate,
	&modinfo_coresize,
#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
	&modinfo_datasize,
#endif
	&modinfo_initsize,
	&modinfo_taint,
#ifdef CONFIG_MODULE_UNLOAD
	&modinfo_refcnt,
#endif
	NULL,
};

const size_t modinfo_attrs_count = ARRAY_SIZE(modinfo_attrs);

static const char vermagic[] = VERMAGIC_STRING;

int try_to_force_load(struct module *mod, const char *reason)
{
#ifdef CONFIG_MODULE_FORCE_LOAD
	if (!test_taint(TAINT_FORCED_MODULE))
		pr_warn("%s: %s: kernel tainted.\n", mod->name, reason);
	add_taint_module(mod, TAINT_FORCED_MODULE, LOCKDEP_NOW_UNRELIABLE);
	return 0;
#else
	return -ENOEXEC;
#endif
}

/* Parse tag=value strings from .modinfo section */
char *module_next_tag_pair(char *string, unsigned long *secsize)
{
	/* Skip non-zero chars */
	while (string[0]) {
		string++;
		if ((*secsize)-- <= 1)
			return NULL;
	}

	/* Skip any zero padding. */
	while (!string[0]) {
		string++;
		if ((*secsize)-- <= 1)
			return NULL;
	}
	return string;
}

static char *get_next_modinfo(const struct load_info *info, const char *tag,
			      char *prev)
{
	char *p;
	unsigned int taglen = strlen(tag);
	Elf_Shdr *infosec = &info->sechdrs[info->index.info];
	unsigned long size = infosec->sh_size;

	/*
	 * get_modinfo() calls made before rewrite_section_headers()
	 * must use sh_offset, as sh_addr isn't set!
	 */
	char *modinfo = (char *)info->hdr + infosec->sh_offset;

	if (prev) {
		size -= prev - modinfo;
		modinfo = module_next_tag_pair(prev, &size);
	}

	for (p = modinfo; p; p = module_next_tag_pair(p, &size)) {
		if (strncmp(p, tag, taglen) == 0 && p[taglen] == '=')
			return p + taglen + 1;
	}
	return NULL;
}

static char *get_modinfo(const struct load_info *info, const char *tag)
{
	return get_next_modinfo(info, tag, NULL);
}

/**
 * verify_module_namespace() - does @modname have access to this symbol's @namespace
 * @namespace: export symbol namespace
 * @modname: module name
 *
 * If @namespace is prefixed with "module:" to indicate it is a module namespace
 * then test if @modname matches any of the comma separated patterns.
 *
 * The patterns only support tail-glob.
 */
static bool verify_module_namespace(const char *namespace, const char *modname)
{
	size_t len, modlen = strlen(modname);
	const char *prefix = "module:";
	const char *sep;
	bool glob;

	if (!strstarts(namespace, prefix))
		return false;

	for (namespace += strlen(prefix); *namespace; namespace = sep) {
		sep = strchrnul(namespace, ',');
		len = sep - namespace;

		glob = false;
		if (sep[-1] == '*') {
			len--;
			glob = true;
		}

		if (*sep)
			sep++;

		if (mod_strncmp(namespace, modname, len) == 0 && (glob || len == modlen))
			return true;
	}

	return false;
}

static int verify_namespace_is_imported(const struct load_info *info,
					const struct kernel_symbol *sym,
					struct module *mod)
{
	const char *namespace;
	char *imported_namespace;

	namespace = kernel_symbol_namespace(sym);
	if (namespace && namespace[0]) {

		if (verify_module_namespace(namespace, mod->name))
			return 0;

		for_each_modinfo_entry(imported_namespace, info, "import_ns") {
			if (strcmp(namespace, imported_namespace) == 0)
				return 0;
		}
#ifdef CONFIG_MODULE_ALLOW_MISSING_NAMESPACE_IMPORTS
		pr_warn(
#else
		pr_err(
#endif
			"%s: module uses symbol (%s) from namespace %s, but does not import it.\n",
			mod->name, kernel_symbol_name(sym), namespace);
#ifndef CONFIG_MODULE_ALLOW_MISSING_NAMESPACE_IMPORTS
		return -EINVAL;
#endif
	}
	return 0;
}

static bool inherit_taint(struct module *mod, struct module *owner, const char *name)
{
	if (!owner || !test_bit(TAINT_PROPRIETARY_MODULE, &owner->taints))
		return true;

	if (mod->using_gplonly_symbols) {
		pr_err("%s: module using GPL-only symbols uses symbols %s from proprietary module %s.\n",
			mod->name, name, owner->name);
		return false;
	}

	if (!test_bit(TAINT_PROPRIETARY_MODULE, &mod->taints)) {
		pr_warn("%s: module uses symbols %s from proprietary module %s, inheriting taint.\n",
			mod->name, name, owner->name);
		set_bit(TAINT_PROPRIETARY_MODULE, &mod->taints);
	}
	return true;
}

/* Resolve a symbol for this module.  I.e. if we find one, record usage. */
static const struct kernel_symbol *resolve_symbol(struct module *mod,
						  const struct load_info *info,
						  const char *name,
						  char ownername[])
{
	struct find_symbol_arg fsa = {
		.name	= name,
		.gplok	= !(mod->taints & (1 << TAINT_PROPRIETARY_MODULE)),
		.warn	= true,
	};
	int err;

	/*
	 * The module_mutex should not be a heavily contended lock;
	 * if we get the occasional sleep here, we'll go an extra iteration
	 * in the wait_event_interruptible(), which is harmless.
	 */
	sched_annotate_sleep();
	mutex_lock(&module_mutex);
	if (!find_symbol(&fsa))
		goto unlock;

	if (fsa.license == GPL_ONLY)
		mod->using_gplonly_symbols = true;

	if (!inherit_taint(mod, fsa.owner, name)) {
		fsa.sym = NULL;
		goto getname;
	}

	if (!check_version(info, name, mod, fsa.crc)) {
		fsa.sym = ERR_PTR(-EINVAL);
		goto getname;
	}

	err = verify_namespace_is_imported(info, fsa.sym, mod);
	if (err) {
		fsa.sym = ERR_PTR(err);
		goto getname;
	}

	err = ref_module(mod, fsa.owner);
	if (err) {
		fsa.sym = ERR_PTR(err);
		goto getname;
	}

getname:
	/* We must make copy under the lock if we failed to get ref. */
	strscpy(ownername, module_name(fsa.owner), MODULE_NAME_LEN);
unlock:
	mutex_unlock(&module_mutex);
	return fsa.sym;
}

static const struct kernel_symbol *
resolve_symbol_wait(struct module *mod,
		    const struct load_info *info,
		    const char *name)
{
	const struct kernel_symbol *ksym;
	char owner[MODULE_NAME_LEN];

	if (wait_event_interruptible_timeout(module_wq,
			!IS_ERR(ksym = resolve_symbol(mod, info, name, owner))
			|| PTR_ERR(ksym) != -EBUSY,
					     30 * HZ) <= 0) {
		pr_warn("%s: gave up waiting for init of module %s.\n",
			mod->name, owner);
	}
	return ksym;
}

void __weak module_arch_cleanup(struct module *mod)
{
}

void __weak module_arch_freeing_init(struct module *mod)
{
}

static int module_memory_alloc(struct module *mod, enum mod_mem_type type)
{
	unsigned int size = PAGE_ALIGN(mod->mem[type].size);
	enum execmem_type execmem_type;
	void *ptr;

	mod->mem[type].size = size;

	if (mod_mem_type_is_data(type))
		execmem_type = EXECMEM_MODULE_DATA;
	else
		execmem_type = EXECMEM_MODULE_TEXT;

	ptr = execmem_alloc(execmem_type, size);
	if (!ptr)
		return -ENOMEM;

	if (execmem_is_rox(execmem_type)) {
		int err = execmem_make_temp_rw(ptr, size);

		if (err) {
			execmem_free(ptr);
			return -ENOMEM;
		}

		mod->mem[type].is_rox = true;
	}

	/*
	 * The pointer to these blocks of memory are stored on the module
	 * structure and we keep that around so long as the module is
	 * around. We only free that memory when we unload the module.
	 * Just mark them as not being a leak then. The .init* ELF
	 * sections *do* get freed after boot so we *could* treat them
	 * slightly differently with kmemleak_ignore() and only grey
	 * them out as they work as typical memory allocations which
	 * *do* eventually get freed, but let's just keep things simple
	 * and avoid *any* false positives.
	 */
	if (!mod->mem[type].is_rox)
		kmemleak_not_leak(ptr);

	memset(ptr, 0, size);
	mod->mem[type].base = ptr;

	return 0;
}

static void module_memory_restore_rox(struct module *mod)
{
	for_class_mod_mem_type(type, text) {
		struct module_memory *mem = &mod->mem[type];

		if (mem->is_rox)
			execmem_restore_rox(mem->base, mem->size);
	}
}

static void module_memory_free(struct module *mod, enum mod_mem_type type)
{
	struct module_memory *mem = &mod->mem[type];

	execmem_free(mem->base);
}

static void free_mod_mem(struct module *mod)
{
	for_each_mod_mem_type(type) {
		struct module_memory *mod_mem = &mod->mem[type];

		if (type == MOD_DATA)
			continue;

		/* Free lock-classes; relies on the preceding sync_rcu(). */
		lockdep_free_key_range(mod_mem->base, mod_mem->size);
		if (mod_mem->size)
			module_memory_free(mod, type);
	}

	/* MOD_DATA hosts mod, so free it at last */
	lockdep_free_key_range(mod->mem[MOD_DATA].base, mod->mem[MOD_DATA].size);
	module_memory_free(mod, MOD_DATA);
}

/* Free a module, remove from lists, etc. */
static void free_module(struct module *mod)
{
	trace_module_free(mod);

	codetag_unload_module(mod);

	mod_sysfs_teardown(mod);

	/*
	 * We leave it in list to prevent duplicate loads, but make sure
	 * that noone uses it while it's being deconstructed.
	 */
	mutex_lock(&module_mutex);
	mod->state = MODULE_STATE_UNFORMED;
	mutex_unlock(&module_mutex);

	/* Arch-specific cleanup. */
	module_arch_cleanup(mod);

	/* Module unload stuff */
	module_unload_free(mod);

	/* Free any allocated parameters. */
	destroy_params(mod->kp, mod->num_kp);

	if (is_livepatch_module(mod))
		free_module_elf(mod);

	/* Now we can delete it from the lists */
	mutex_lock(&module_mutex);
	/* Unlink carefully: kallsyms could be walking list. */
	list_del_rcu(&mod->list);
	mod_tree_remove(mod);
	/* Remove this module from bug list, this uses list_del_rcu */
	module_bug_cleanup(mod);
	/* Wait for RCU synchronizing before releasing mod->list and buglist. */
	synchronize_rcu();
	if (try_add_tainted_module(mod))
		pr_err("%s: adding tainted module to the unloaded tainted modules list failed.\n",
		       mod->name);
	mutex_unlock(&module_mutex);

	/* This may be empty, but that's OK */
	module_arch_freeing_init(mod);
	kfree(mod->args);
	percpu_modfree(mod);

	free_mod_mem(mod);
}

void *__symbol_get(const char *symbol)
{
	struct find_symbol_arg fsa = {
		.name	= symbol,
		.gplok	= true,
		.warn	= true,
	};

	scoped_guard(rcu) {
		if (!find_symbol(&fsa))
			return NULL;
		if (fsa.license != GPL_ONLY) {
			pr_warn("failing symbol_get of non-GPLONLY symbol %s.\n",
				symbol);
			return NULL;
		}
		if (strong_try_module_get(fsa.owner))
			return NULL;
	}
	return (void *)kernel_symbol_value(fsa.sym);
}
EXPORT_SYMBOL_GPL(__symbol_get);

/*
 * Ensure that an exported symbol [global namespace] does not already exist
 * in the kernel or in some other module's exported symbol table.
 *
 * You must hold the module_mutex.
 */
static int verify_exported_symbols(struct module *mod)
{
	unsigned int i;
	const struct kernel_symbol *s;
	struct {
		const struct kernel_symbol *sym;
		unsigned int num;
	} arr[] = {
		{ mod->syms, mod->num_syms },
		{ mod->gpl_syms, mod->num_gpl_syms },
	};

	for (i = 0; i < ARRAY_SIZE(arr); i++) {
		for (s = arr[i].sym; s < arr[i].sym + arr[i].num; s++) {
			struct find_symbol_arg fsa = {
				.name	= kernel_symbol_name(s),
				.gplok	= true,
			};
			if (find_symbol(&fsa)) {
				pr_err("%s: exports duplicate symbol %s"
				       " (owned by %s)\n",
				       mod->name, kernel_symbol_name(s),
				       module_name(fsa.owner));
				return -ENOEXEC;
			}
		}
	}
	return 0;
}

static bool ignore_undef_symbol(Elf_Half emachine, const char *name)
{
	/*
	 * On x86, PIC code and Clang non-PIC code may have call foo@PLT. GNU as
	 * before 2.37 produces an unreferenced _GLOBAL_OFFSET_TABLE_ on x86-64.
	 * i386 has a similar problem but may not deserve a fix.
	 *
	 * If we ever have to ignore many symbols, consider refactoring the code to
	 * only warn if referenced by a relocation.
	 */
	if (emachine == EM_386 || emachine == EM_X86_64)
		return !strcmp(name, "_GLOBAL_OFFSET_TABLE_");
	return false;
}

/* Change all symbols so that st_value encodes the pointer directly. */
static int simplify_symbols(struct module *mod, const struct load_info *info)
{
	Elf_Shdr *symsec = &info->sechdrs[info->index.sym];
	Elf_Sym *sym = (void *)symsec->sh_addr;
	unsigned long secbase;
	unsigned int i;
	int ret = 0;
	const struct kernel_symbol *ksym;

	for (i = 1; i < symsec->sh_size / sizeof(Elf_Sym); i++) {
		const char *name = info->strtab + sym[i].st_name;

		switch (sym[i].st_shndx) {
		case SHN_COMMON:
			/* Ignore common symbols */
			if (!strncmp(name, "__gnu_lto", 9))
				break;

			/*
			 * We compiled with -fno-common.  These are not
			 * supposed to happen.
			 */
			pr_debug("Common symbol: %s\n", name);
			pr_warn("%s: please compile with -fno-common\n",
			       mod->name);
			ret = -ENOEXEC;
			break;

		case SHN_ABS:
			/* Don't need to do anything */
			pr_debug("Absolute symbol: 0x%08lx %s\n",
				 (long)sym[i].st_value, name);
			break;

		case SHN_LIVEPATCH:
			/* Livepatch symbols are resolved by livepatch */
			break;

		case SHN_UNDEF:
			ksym = resolve_symbol_wait(mod, info, name);
			/* Ok if resolved.  */
			if (ksym && !IS_ERR(ksym)) {
				sym[i].st_value = kernel_symbol_value(ksym);
				break;
			}

			/* Ok if weak or ignored.  */
			if (!ksym &&
			    (ELF_ST_BIND(sym[i].st_info) == STB_WEAK ||
			     ignore_undef_symbol(info->hdr->e_machine, name)))
				break;

			ret = PTR_ERR(ksym) ?: -ENOENT;
			pr_warn("%s: Unknown symbol %s (err %d)\n",
				mod->name, name, ret);
			break;

		default:
			/* Divert to percpu allocation if a percpu var. */
			if (sym[i].st_shndx == info->index.pcpu)
				secbase = (unsigned long)mod_percpu(mod);
			else
				secbase = info->sechdrs[sym[i].st_shndx].sh_addr;
			sym[i].st_value += secbase;
			break;
		}
	}

	return ret;
}

static int apply_relocations(struct module *mod, const struct load_info *info)
{
	unsigned int i;
	int err = 0;

	/* Now do relocations. */
	for (i = 1; i < info->hdr->e_shnum; i++) {
		unsigned int infosec = info->sechdrs[i].sh_info;

		/* Not a valid relocation section? */
		if (infosec >= info->hdr->e_shnum)
			continue;

		/*
		 * Don't bother with non-allocated sections.
		 * An exception is the percpu section, which has separate allocations
		 * for individual CPUs. We relocate the percpu section in the initial
		 * ELF template and subsequently copy it to the per-CPU destinations.
		 */
		if (!(info->sechdrs[infosec].sh_flags & SHF_ALLOC) &&
		    (!infosec || infosec != info->index.pcpu))
			continue;

		if (info->sechdrs[i].sh_flags & SHF_RELA_LIVEPATCH)
			err = klp_apply_section_relocs(mod, info->sechdrs,
						       info->secstrings,
						       info->strtab,
						       info->index.sym, i,
						       NULL);
		else if (info->sechdrs[i].sh_type == SHT_REL)
			err = apply_relocate(info->sechdrs, info->strtab,
					     info->index.sym, i, mod);
		else if (info->sechdrs[i].sh_type == SHT_RELA)
			err = apply_relocate_add(info->sechdrs, info->strtab,
						 info->index.sym, i, mod);
		if (err < 0)
			break;
	}
	return err;
}

/* Additional bytes needed by arch in front of individual sections */
unsigned int __weak arch_mod_section_prepend(struct module *mod,
					     unsigned int section)
{
	/* default implementation just returns zero */
	return 0;
}

long module_get_offset_and_type(struct module *mod, enum mod_mem_type type,
				Elf_Shdr *sechdr, unsigned int section)
{
	long offset;
	long mask = ((unsigned long)(type) & SH_ENTSIZE_TYPE_MASK) << SH_ENTSIZE_TYPE_SHIFT;

	mod->mem[type].size += arch_mod_section_prepend(mod, section);
	offset = ALIGN(mod->mem[type].size, sechdr->sh_addralign ?: 1);
	mod->mem[type].size = offset + sechdr->sh_size;

	WARN_ON_ONCE(offset & mask);
	return offset | mask;
}

bool module_init_layout_section(const char *sname)
{
#ifndef CONFIG_MODULE_UNLOAD
	if (module_exit_section(sname))
		return true;
#endif
	return module_init_section(sname);
}

static void __layout_sections(struct module *mod, struct load_info *info, bool is_init)
{
	unsigned int m, i;

	/*
	 * { Mask of required section header flags,
	 *   Mask of excluded section header flags }
	 */
	static const unsigned long masks[][2] = {
		{ SHF_EXECINSTR | SHF_ALLOC, ARCH_SHF_SMALL },
		{ SHF_ALLOC, SHF_WRITE | ARCH_SHF_SMALL },
		{ SHF_RO_AFTER_INIT | SHF_ALLOC, ARCH_SHF_SMALL },
		{ SHF_WRITE | SHF_ALLOC, ARCH_SHF_SMALL },
		{ ARCH_SHF_SMALL | SHF_ALLOC, 0 }
	};
	static const int core_m_to_mem_type[] = {
		MOD_TEXT,
		MOD_RODATA,
		MOD_RO_AFTER_INIT,
		MOD_DATA,
		MOD_DATA,
	};
	static const int init_m_to_mem_type[] = {
		MOD_INIT_TEXT,
		MOD_INIT_RODATA,
		MOD_INVALID,
		MOD_INIT_DATA,
		MOD_INIT_DATA,
	};

	for (m = 0; m < ARRAY_SIZE(masks); ++m) {
		enum mod_mem_type type = is_init ? init_m_to_mem_type[m] : core_m_to_mem_type[m];

		for (i = 0; i < info->hdr->e_shnum; ++i) {
			Elf_Shdr *s = &info->sechdrs[i];
			const char *sname = info->secstrings + s->sh_name;

			if ((s->sh_flags & masks[m][0]) != masks[m][0]
			    || (s->sh_flags & masks[m][1])
			    || s->sh_entsize != ~0UL
			    || is_init != module_init_layout_section(sname))
				continue;

			if (WARN_ON_ONCE(type == MOD_INVALID))
				continue;

			/*
			 * Do not allocate codetag memory as we load it into
			 * preallocated contiguous memory.
			 */
			if (codetag_needs_module_section(mod, sname, s->sh_size)) {
				/*
				 * s->sh_entsize won't be used but populate the
				 * type field to avoid confusion.
				 */
				s->sh_entsize = ((unsigned long)(type) & SH_ENTSIZE_TYPE_MASK)
						<< SH_ENTSIZE_TYPE_SHIFT;
				continue;
			}

			s->sh_entsize = module_get_offset_and_type(mod, type, s, i);
			pr_debug("\t%s\n", sname);
		}
	}
}

/*
 * Lay out the SHF_ALLOC sections in a way not dissimilar to how ld
 * might -- code, read-only data, read-write data, small data.  Tally
 * sizes, and place the offsets into sh_entsize fields: high bit means it
 * belongs in init.
 */
static void layout_sections(struct module *mod, struct load_info *info)
{
	unsigned int i;

	for (i = 0; i < info->hdr->e_shnum; i++)
		info->sechdrs[i].sh_entsize = ~0UL;

	pr_debug("Core section allocation order for %s:\n", mod->name);
	__layout_sections(mod, info, false);

	pr_debug("Init section allocation order for %s:\n", mod->name);
	__layout_sections(mod, info, true);
}

static void module_license_taint_check(struct module *mod, const char *license)
{
	if (!license)
		license = "unspecified";

	if (!license_is_gpl_compatible(license)) {
		if (!test_taint(TAINT_PROPRIETARY_MODULE))
			pr_warn("%s: module license '%s' taints kernel.\n",
				mod->name, license);
		add_taint_module(mod, TAINT_PROPRIETARY_MODULE,
				 LOCKDEP_NOW_UNRELIABLE);
	}
}

static int setup_modinfo(struct module *mod, struct load_info *info)
{
	const struct module_attribute *attr;
	char *imported_namespace;
	int i;

	for (i = 0; (attr = modinfo_attrs[i]); i++) {
		if (attr->setup)
			attr->setup(mod, get_modinfo(info, attr->attr.name));
	}

	for_each_modinfo_entry(imported_namespace, info, "import_ns") {
		/*
		 * 'module:' prefixed namespaces are implicit, disallow
		 * explicit imports.
		 */
		if (strstarts(imported_namespace, "module:")) {
			pr_err("%s: module tries to import module namespace: %s\n",
			       mod->name, imported_namespace);
			return -EPERM;
		}
	}

	return 0;
}

static void free_modinfo(struct module *mod)
{
	const struct module_attribute *attr;
	int i;

	for (i = 0; (attr = modinfo_attrs[i]); i++) {
		if (attr->free)
			attr->free(mod);
	}
}

bool __weak module_init_section(const char *name)
{
	return strstarts(name, ".init");
}

bool __weak module_exit_section(const char *name)
{
	return strstarts(name, ".exit");
}

static int validate_section_offset(const struct load_info *info, Elf_Shdr *shdr)
{
#if defined(CONFIG_64BIT)
	unsigned long long secend;
#else
	unsigned long secend;
#endif

	/*
	 * Check for both overflow and offset/size being
	 * too large.
	 */
	secend = shdr->sh_offset + shdr->sh_size;
	if (secend < shdr->sh_offset || secend > info->len)
		return -ENOEXEC;

	return 0;
}

/**
 * elf_validity_ehdr() - Checks an ELF header for module validity
 * @info: Load info containing the ELF header to check
 *
 * Checks whether an ELF header could belong to a valid module. Checks:
 *
 * * ELF header is within the data the user provided
 * * ELF magic is present
 * * It is relocatable (not final linked, not core file, etc.)
 * * The header's machine type matches what the architecture expects.
 * * Optional arch-specific hook for other properties
 *   - module_elf_check_arch() is currently only used by PPC to check
 *   ELF ABI version, but may be used by others in the future.
 *
 * Return: %0 if valid, %-ENOEXEC on failure.
 */
static int elf_validity_ehdr(const struct load_info *info)
{
	if (info->len < sizeof(*(info->hdr))) {
		pr_err("Invalid ELF header len %lu\n", info->len);
		return -ENOEXEC;
	}
	if (memcmp(info->hdr->e_ident, ELFMAG, SELFMAG) != 0) {
		pr_err("Invalid ELF header magic: != %s\n", ELFMAG);
		return -ENOEXEC;
	}
	if (info->hdr->e_type != ET_REL) {
		pr_err("Invalid ELF header type: %u != %u\n",
		       info->hdr->e_type, ET_REL);
		return -ENOEXEC;
	}
	if (!elf_check_arch(info->hdr)) {
		pr_err("Invalid architecture in ELF header: %u\n",
		       info->hdr->e_machine);
		return -ENOEXEC;
	}
	if (!module_elf_check_arch(info->hdr)) {
		pr_err("Invalid module architecture in ELF header: %u\n",
		       info->hdr->e_machine);
		return -ENOEXEC;
	}
	return 0;
}

/**
 * elf_validity_cache_sechdrs() - Cache section headers if valid
 * @info: Load info to compute section headers from
 *
 * Checks:
 *
 * * ELF header is valid (see elf_validity_ehdr())
 * * Section headers are the size we expect
 * * Section array fits in the user provided data
 * * Section index 0 is NULL
 * * Section contents are inbounds
 *
 * Then updates @info with a &load_info->sechdrs pointer if valid.
 *
 * Return: %0 if valid, negative error code if validation failed.
 */
static int elf_validity_cache_sechdrs(struct load_info *info)
{
	Elf_Shdr *sechdrs;
	Elf_Shdr *shdr;
	int i;
	int err;

	err = elf_validity_ehdr(info);
	if (err < 0)
		return err;

	if (info->hdr->e_shentsize != sizeof(Elf_Shdr)) {
		pr_err("Invalid ELF section header size\n");
		return -ENOEXEC;
	}

	/*
	 * e_shnum is 16 bits, and sizeof(Elf_Shdr) is
	 * known and small. So e_shnum * sizeof(Elf_Shdr)
	 * will not overflow unsigned long on any platform.
	 */
	if (info->hdr->e_shoff >= info->len
	    || (info->hdr->e_shnum * sizeof(Elf_Shdr) >
		info->len - info->hdr->e_shoff)) {
		pr_err("Invalid ELF section header overflow\n");
		return -ENOEXEC;
	}

	sechdrs = (void *)info->hdr + info->hdr->e_shoff;

	/*
	 * The code assumes that section 0 has a length of zero and
	 * an addr of zero, so check for it.
	 */
	if (sechdrs[0].sh_type != SHT_NULL
	    || sechdrs[0].sh_size != 0
	    || sechdrs[0].sh_addr != 0) {
		pr_err("ELF Spec violation: section 0 type(%d)!=SH_NULL or non-zero len or addr\n",
		       sechdrs[0].sh_type);
		return -ENOEXEC;
	}

	/* Validate contents are inbounds */
	for (i = 1; i < info->hdr->e_shnum; i++) {
		shdr = &sechdrs[i];
		switch (shdr->sh_type) {
		case SHT_NULL:
		case SHT_NOBITS:
			/* No contents, offset/size don't mean anything */
			continue;
		default:
			err = validate_section_offset(info, shdr);
			if (err < 0) {
				pr_err("Invalid ELF section in module (section %u type %u)\n",
				       i, shdr->sh_type);
				return err;
			}
		}
	}

	info->sechdrs = sechdrs;

	return 0;
}

/**
 * elf_validity_cache_secstrings() - Caches section names if valid
 * @info: Load info to cache section names from. Must have valid sechdrs.
 *
 * Specifically checks:
 *
 * * Section name table index is inbounds of section headers
 * * Section name table is not empty
 * * Section name table is NUL terminated
 * * All section name offsets are inbounds of the section
 *
 * Then updates @info with a &load_info->secstrings pointer if valid.
 *
 * Return: %0 if valid, negative error code if validation failed.
 */
static int elf_validity_cache_secstrings(struct load_info *info)
{
	Elf_Shdr *strhdr, *shdr;
	char *secstrings;
	int i;

	/*
	 * Verify if the section name table index is valid.
	 */
	if (info->hdr->e_shstrndx == SHN_UNDEF
	    || info->hdr->e_shstrndx >= info->hdr->e_shnum) {
		pr_err("Invalid ELF section name index: %d || e_shstrndx (%d) >= e_shnum (%d)\n",
		       info->hdr->e_shstrndx, info->hdr->e_shstrndx,
		       info->hdr->e_shnum);
		return -ENOEXEC;
	}

	strhdr = &info->sechdrs[info->hdr->e_shstrndx];

	/*
	 * The section name table must be NUL-terminated, as required
	 * by the spec. This makes strcmp and pr_* calls that access
	 * strings in the section safe.
	 */
	secstrings = (void *)info->hdr + strhdr->sh_offset;
	if (strhdr->sh_size == 0) {
		pr_err("empty section name table\n");
		return -ENOEXEC;
	}
	if (secstrings[strhdr->sh_size - 1] != '\0') {
		pr_err("ELF Spec violation: section name table isn't null terminated\n");
		return -ENOEXEC;
	}

	for (i = 0; i < info->hdr->e_shnum; i++) {
		shdr = &info->sechdrs[i];
		/* SHT_NULL means sh_name has an undefined value */
		if (shdr->sh_type == SHT_NULL)
			continue;
		if (shdr->sh_name >= strhdr->sh_size) {
			pr_err("Invalid ELF section name in module (section %u type %u)\n",
			       i, shdr->sh_type);
			return -ENOEXEC;
		}
	}

	info->secstrings = secstrings;
	return 0;
}

/**
 * elf_validity_cache_index_info() - Validate and cache modinfo section
 * @info: Load info to populate the modinfo index on.
 *        Must have &load_info->sechdrs and &load_info->secstrings populated
 *
 * Checks that if there is a .modinfo section, it is unique.
 * Then, it caches its index in &load_info->index.info.
 * Finally, it tries to populate the name to improve error messages.
 *
 * Return: %0 if valid, %-ENOEXEC if multiple modinfo sections were found.
 */
static int elf_validity_cache_index_info(struct load_info *info)
{
	int info_idx;

	info_idx = find_any_unique_sec(info, ".modinfo");

	if (info_idx == 0)
		/* Early return, no .modinfo */
		return 0;

	if (info_idx < 0) {
		pr_err("Only one .modinfo section must exist.\n");
		return -ENOEXEC;
	}

	info->index.info = info_idx;
	/* Try to find a name early so we can log errors with a module name */
	info->name = get_modinfo(info, "name");

	return 0;
}

/**
 * elf_validity_cache_index_mod() - Validates and caches this_module section
 * @info: Load info to cache this_module on.
 *        Must have &load_info->sechdrs and &load_info->secstrings populated
 *
 * The ".gnu.linkonce.this_module" ELF section is special. It is what modpost
 * uses to refer to __this_module and let's use rely on THIS_MODULE to point
 * to &__this_module properly. The kernel's modpost declares it on each
 * modules's *.mod.c file. If the struct module of the kernel changes a full
 * kernel rebuild is required.
 *
 * We have a few expectations for this special section, this function
 * validates all this for us:
 *
 * * The section has contents
 * * The section is unique
 * * We expect the kernel to always have to allocate it: SHF_ALLOC
 * * The section size must match the kernel's run time's struct module
 *   size
 *
 * If all checks pass, the index will be cached in &load_info->index.mod
 *
 * Return: %0 on validation success, %-ENOEXEC on failure
 */
static int elf_validity_cache_index_mod(struct load_info *info)
{
	Elf_Shdr *shdr;
	int mod_idx;

	mod_idx = find_any_unique_sec(info, ".gnu.linkonce.this_module");
	if (mod_idx <= 0) {
		pr_err("module %s: Exactly one .gnu.linkonce.this_module section must exist.\n",
		       info->name ?: "(missing .modinfo section or name field)");
		return -ENOEXEC;
	}

	shdr = &info->sechdrs[mod_idx];

	if (shdr->sh_type == SHT_NOBITS) {
		pr_err("module %s: .gnu.linkonce.this_module section must have a size set\n",
		       info->name ?: "(missing .modinfo section or name field)");
		return -ENOEXEC;
	}

	if (!(shdr->sh_flags & SHF_ALLOC)) {
		pr_err("module %s: .gnu.linkonce.this_module must occupy memory during process execution\n",
		       info->name ?: "(missing .modinfo section or name field)");
		return -ENOEXEC;
	}

	if (shdr->sh_size != sizeof(struct module)) {
		pr_err("module %s: .gnu.linkonce.this_module section size must match the kernel's built struct module size at run time\n",
		       info->name ?: "(missing .modinfo section or name field)");
		return -ENOEXEC;
	}

	info->index.mod = mod_idx;

	return 0;
}

/**
 * elf_validity_cache_index_sym() - Validate and cache symtab index
 * @info: Load info to cache symtab index in.
 *        Must have &load_info->sechdrs and &load_info->secstrings populated.
 *
 * Checks that there is exactly one symbol table, then caches its index in
 * &load_info->index.sym.
 *
 * Return: %0 if valid, %-ENOEXEC on failure.
 */
static int elf_validity_cache_index_sym(struct load_info *info)
{
	unsigned int sym_idx;
	unsigned int num_sym_secs = 0;
	int i;

	for (i = 1; i < info->hdr->e_shnum; i++) {
		if (info->sechdrs[i].sh_type == SHT_SYMTAB) {
			num_sym_secs++;
			sym_idx = i;
		}
	}

	if (num_sym_secs != 1) {
		pr_warn("%s: module has no symbols (stripped?)\n",
			info->name ?: "(missing .modinfo section or name field)");
		return -ENOEXEC;
	}

	info->index.sym = sym_idx;

	return 0;
}

/**
 * elf_validity_cache_index_str() - Validate and cache strtab index
 * @info: Load info to cache strtab index in.
 *        Must have &load_info->sechdrs and &load_info->secstrings populated.
 *        Must have &load_info->index.sym populated.
 *
 * Looks at the symbol table's associated string table, makes sure it is
 * in-bounds, and caches it.
 *
 * Return: %0 if valid, %-ENOEXEC on failure.
 */
static int elf_validity_cache_index_str(struct load_info *info)
{
	unsigned int str_idx = info->sechdrs[info->index.sym].sh_link;

	if (str_idx == SHN_UNDEF || str_idx >= info->hdr->e_shnum) {
		pr_err("Invalid ELF sh_link!=SHN_UNDEF(%d) or (sh_link(%d) >= hdr->e_shnum(%d)\n",
		       str_idx, str_idx, info->hdr->e_shnum);
		return -ENOEXEC;
	}

	info->index.str = str_idx;
	return 0;
}

/**
 * elf_validity_cache_index_versions() - Validate and cache version indices
 * @info:  Load info to cache version indices in.
 *         Must have &load_info->sechdrs and &load_info->secstrings populated.
 * @flags: Load flags, relevant to suppress version loading, see
 *         uapi/linux/module.h
 *
 * If we're ignoring modversions based on @flags, zero all version indices
 * and return validity. Othewrise check:
 *
 * * If "__version_ext_crcs" is present, "__version_ext_names" is present
 * * There is a name present for every crc
 *
 * Then populate:
 *
 * * &load_info->index.vers
 * * &load_info->index.vers_ext_crc
 * * &load_info->index.vers_ext_names
 *
 * if present.
 *
 * Return: %0 if valid, %-ENOEXEC on failure.
 */
static int elf_validity_cache_index_versions(struct load_info *info, int flags)
{
	unsigned int vers_ext_crc;
	unsigned int vers_ext_name;
	size_t crc_count;
	size_t remaining_len;
	size_t name_size;
	char *name;

	/* If modversions were suppressed, pretend we didn't find any */
	if (flags & MODULE_INIT_IGNORE_MODVERSIONS) {
		info->index.vers = 0;
		info->index.vers_ext_crc = 0;
		info->index.vers_ext_name = 0;
		return 0;
	}

	vers_ext_crc = find_sec(info, "__version_ext_crcs");
	vers_ext_name = find_sec(info, "__version_ext_names");

	/* If we have one field, we must have the other */
	if (!!vers_ext_crc != !!vers_ext_name) {
		pr_err("extended version crc+name presence does not match");
		return -ENOEXEC;
	}

	/*
	 * If we have extended version information, we should have the same
	 * number of entries in every section.
	 */
	if (vers_ext_crc) {
		crc_count = info->sechdrs[vers_ext_crc].sh_size / sizeof(u32);
		name = (void *)info->hdr +
			info->sechdrs[vers_ext_name].sh_offset;
		remaining_len = info->sechdrs[vers_ext_name].sh_size;

		while (crc_count--) {
			name_size = strnlen(name, remaining_len) + 1;
			if (name_size > remaining_len) {
				pr_err("more extended version crcs than names");
				return -ENOEXEC;
			}
			remaining_len -= name_size;
			name += name_size;
		}
	}

	info->index.vers = find_sec(info, "__versions");
	info->index.vers_ext_crc = vers_ext_crc;
	info->index.vers_ext_name = vers_ext_name;
	return 0;
}

/**
 * elf_validity_cache_index() - Resolve, validate, cache section indices
 * @info:  Load info to read from and update.
 *         &load_info->sechdrs and &load_info->secstrings must be populated.
 * @flags: Load flags, relevant to suppress version loading, see
 *         uapi/linux/module.h
 *
 * Populates &load_info->index, validating as it goes.
 * See child functions for per-field validation:
 *
 * * elf_validity_cache_index_info()
 * * elf_validity_cache_index_mod()
 * * elf_validity_cache_index_sym()
 * * elf_validity_cache_index_str()
 * * elf_validity_cache_index_versions()
 *
 * If CONFIG_SMP is enabled, load the percpu section by name with no
 * validation.
 *
 * Return: 0 on success, negative error code if an index failed validation.
 */
static int elf_validity_cache_index(struct load_info *info, int flags)
{
	int err;

	err = elf_validity_cache_index_info(info);
	if (err < 0)
		return err;
	err = elf_validity_cache_index_mod(info);
	if (err < 0)
		return err;
	err = elf_validity_cache_index_sym(info);
	if (err < 0)
		return err;
	err = elf_validity_cache_index_str(info);
	if (err < 0)
		return err;
	err = elf_validity_cache_index_versions(info, flags);
	if (err < 0)
		return err;

	info->index.pcpu = find_pcpusec(info);

	return 0;
}

/**
 * elf_validity_cache_strtab() - Validate and cache symbol string table
 * @info: Load info to read from and update.
 *        Must have &load_info->sechdrs and &load_info->secstrings populated.
 *        Must have &load_info->index populated.
 *
 * Checks:
 *
 * * The string table is not empty.
 * * The string table starts and ends with NUL (required by ELF spec).
 * * Every &Elf_Sym->st_name offset in the symbol table is inbounds of the
 *   string table.
 *
 * And caches the pointer as &load_info->strtab in @info.
 *
 * Return: 0 on success, negative error code if a check failed.
 */
static int elf_validity_cache_strtab(struct load_info *info)
{
	Elf_Shdr *str_shdr = &info->sechdrs[info->index.str];
	Elf_Shdr *sym_shdr = &info->sechdrs[info->index.sym];
	char *strtab = (char *)info->hdr + str_shdr->sh_offset;
	Elf_Sym *syms = (void *)info->hdr + sym_shdr->sh_offset;
	int i;

	if (str_shdr->sh_size == 0) {
		pr_err("empty symbol string table\n");
		return -ENOEXEC;
	}
	if (strtab[0] != '\0') {
		pr_err("symbol string table missing leading NUL\n");
		return -ENOEXEC;
	}
	if (strtab[str_shdr->sh_size - 1] != '\0') {
		pr_err("symbol string table isn't NUL terminated\n");
		return -ENOEXEC;
	}

	/*
	 * Now that we know strtab is correctly structured, check symbol
	 * starts are inbounds before they're used later.
	 */
	for (i = 0; i < sym_shdr->sh_size / sizeof(*syms); i++) {
		if (syms[i].st_name >= str_shdr->sh_size) {
			pr_err("symbol name out of bounds in string table");
			return -ENOEXEC;
		}
	}

	info->strtab = strtab;
	return 0;
}

/*
 * Check userspace passed ELF module against our expectations, and cache
 * useful variables for further processing as we go.
 *
 * This does basic validity checks against section offsets and sizes, the
 * section name string table, and the indices used for it (sh_name).
 *
 * As a last step, since we're already checking the ELF sections we cache
 * useful variables which will be used later for our convenience:
 *
 * 	o pointers to section headers
 * 	o cache the modinfo symbol section
 * 	o cache the string symbol section
 * 	o cache the module section
 *
 * As a last step we set info->mod to the temporary copy of the module in
 * info->hdr. The final one will be allocated in move_module(). Any
 * modifications we make to our copy of the module will be carried over
 * to the final minted module.
 */
static int elf_validity_cache_copy(struct load_info *info, int flags)
{
	int err;

	err = elf_validity_cache_sechdrs(info);
	if (err < 0)
		return err;
	err = elf_validity_cache_secstrings(info);
	if (err < 0)
		return err;
	err = elf_validity_cache_index(info, flags);
	if (err < 0)
		return err;
	err = elf_validity_cache_strtab(info);
	if (err < 0)
		return err;

	/* This is temporary: point mod into copy of data. */
	info->mod = (void *)info->hdr + info->sechdrs[info->index.mod].sh_offset;

	/*
	 * If we didn't load the .modinfo 'name' field earlier, fall back to
	 * on-disk struct mod 'name' field.
	 */
	if (!info->name)
		info->name = info->mod->name;

	return 0;
}

#define COPY_CHUNK_SIZE (16*PAGE_SIZE)

static int copy_chunked_from_user(void *dst, const void __user *usrc, unsigned long len)
{
	do {
		unsigned long n = min(len, COPY_CHUNK_SIZE);

		if (copy_from_user(dst, usrc, n) != 0)
			return -EFAULT;
		cond_resched();
		dst += n;
		usrc += n;
		len -= n;
	} while (len);
	return 0;
}

static int check_modinfo_livepatch(struct module *mod, struct load_info *info)
{
	if (!get_modinfo(info, "livepatch"))
		/* Nothing more to do */
		return 0;

	if (set_livepatch_module(mod))
		return 0;

	pr_err("%s: module is marked as livepatch module, but livepatch support is disabled",
	       mod->name);
	return -ENOEXEC;
}

static void check_modinfo_retpoline(struct module *mod, struct load_info *info)
{
	if (retpoline_module_ok(get_modinfo(info, "retpoline")))
		return;

	pr_warn("%s: loading module not compiled with retpoline compiler.\n",
		mod->name);
}

/* Sets info->hdr and info->len. */
static int copy_module_from_user(const void __user *umod, unsigned long len,
				  struct load_info *info)
{
	int err;

	info->len = len;
	if (info->len < sizeof(*(info->hdr)))
		return -ENOEXEC;

	err = security_kernel_load_data(LOADING_MODULE, true);
	if (err)
		return err;

	/* Suck in entire file: we'll want most of it. */
	info->hdr = __vmalloc(info->len, GFP_KERNEL | __GFP_NOWARN);
	if (!info->hdr)
		return -ENOMEM;

	if (copy_chunked_from_user(info->hdr, umod, info->len) != 0) {
		err = -EFAULT;
		goto out;
	}

	err = security_kernel_post_load_data((char *)info->hdr, info->len,
					     LOADING_MODULE, "init_module");
out:
	if (err)
		vfree(info->hdr);

	return err;
}

static void free_copy(struct load_info *info, int flags)
{
	if (flags & MODULE_INIT_COMPRESSED_FILE)
		module_decompress_cleanup(info);
	else
		vfree(info->hdr);
}

static int rewrite_section_headers(struct load_info *info, int flags)
{
	unsigned int i;

	/* This should always be true, but let's be sure. */
	info->sechdrs[0].sh_addr = 0;

	for (i = 1; i < info->hdr->e_shnum; i++) {
		Elf_Shdr *shdr = &info->sechdrs[i];

		/*
		 * Mark all sections sh_addr with their address in the
		 * temporary image.
		 */
		shdr->sh_addr = (size_t)info->hdr + shdr->sh_offset;

	}

	/* Track but don't keep modinfo and version sections. */
	info->sechdrs[info->index.vers].sh_flags &= ~(unsigned long)SHF_ALLOC;
	info->sechdrs[info->index.vers_ext_crc].sh_flags &=
		~(unsigned long)SHF_ALLOC;
	info->sechdrs[info->index.vers_ext_name].sh_flags &=
		~(unsigned long)SHF_ALLOC;
	info->sechdrs[info->index.info].sh_flags &= ~(unsigned long)SHF_ALLOC;

	return 0;
}

static const char *const module_license_offenders[] = {
	/* driverloader was caught wrongly pretending to be under GPL */
	"driverloader",

	/* lve claims to be GPL but upstream won't provide source */
	"lve",
};

/*
 * These calls taint the kernel depending certain module circumstances */
static void module_augment_kernel_taints(struct module *mod, struct load_info *info)
{
	int prev_taint = test_taint(TAINT_PROPRIETARY_MODULE);
	size_t i;

	if (!get_modinfo(info, "intree")) {
		if (!test_taint(TAINT_OOT_MODULE))
			pr_warn("%s: loading out-of-tree module taints kernel.\n",
				mod->name);
		add_taint_module(mod, TAINT_OOT_MODULE, LOCKDEP_STILL_OK);
	}

	check_modinfo_retpoline(mod, info);

	if (get_modinfo(info, "staging")) {
		add_taint_module(mod, TAINT_CRAP, LOCKDEP_STILL_OK);
		pr_warn("%s: module is from the staging directory, the quality "
			"is unknown, you have been warned.\n", mod->name);
	}

	if (is_livepatch_module(mod)) {
		add_taint_module(mod, TAINT_LIVEPATCH, LOCKDEP_STILL_OK);
		pr_notice_once("%s: tainting kernel with TAINT_LIVEPATCH\n",
				mod->name);
	}

	module_license_taint_check(mod, get_modinfo(info, "license"));

	if (get_modinfo(info, "test")) {
		if (!test_taint(TAINT_TEST))
			pr_warn("%s: loading test module taints kernel.\n",
				mod->name);
		add_taint_module(mod, TAINT_TEST, LOCKDEP_STILL_OK);
	}
#ifdef CONFIG_MODULE_SIG
	mod->sig_ok = info->sig_ok;
	if (!mod->sig_ok) {
		pr_notice_once("%s: module verification failed: signature "
			       "and/or required key missing - tainting "
			       "kernel\n", mod->name);
		add_taint_module(mod, TAINT_UNSIGNED_MODULE, LOCKDEP_STILL_OK);
	}
#endif

	/*
	 * ndiswrapper is under GPL by itself, but loads proprietary modules.
	 * Don't use add_taint_module(), as it would prevent ndiswrapper from
	 * using GPL-only symbols it needs.
	 */
	if (strcmp(mod->name, "ndiswrapper") == 0)
		add_taint(TAINT_PROPRIETARY_MODULE, LOCKDEP_NOW_UNRELIABLE);

	for (i = 0; i < ARRAY_SIZE(module_license_offenders); ++i) {
		if (strcmp(mod->name, module_license_offenders[i]) == 0)
			add_taint_module(mod, TAINT_PROPRIETARY_MODULE,
					 LOCKDEP_NOW_UNRELIABLE);
	}

	if (!prev_taint && test_taint(TAINT_PROPRIETARY_MODULE))
		pr_warn("%s: module license taints kernel.\n", mod->name);

}

static int check_modinfo(struct module *mod, struct load_info *info, int flags)
{
	const char *modmagic = get_modinfo(info, "vermagic");
	int err;

	if (flags & MODULE_INIT_IGNORE_VERMAGIC)
		modmagic = NULL;

	/* This is allowed: modprobe --force will invalidate it. */
	if (!modmagic) {
		err = try_to_force_load(mod, "bad vermagic");
		if (err)
			return err;
	} else if (!same_magic(modmagic, vermagic, info->index.vers)) {
		pr_err("%s: version magic '%s' should be '%s'\n",
		       info->name, modmagic, vermagic);
		return -ENOEXEC;
	}

	err = check_modinfo_livepatch(mod, info);
	if (err)
		return err;

	return 0;
}

static int find_module_sections(struct module *mod, struct load_info *info)
{
	mod->kp = section_objs(info, "__param",
			       sizeof(*mod->kp), &mod->num_kp);
	mod->syms = section_objs(info, "__ksymtab",
				 sizeof(*mod->syms), &mod->num_syms);
	mod->crcs = section_addr(info, "__kcrctab");
	mod->gpl_syms = section_objs(info, "__ksymtab_gpl",
				     sizeof(*mod->gpl_syms),
				     &mod->num_gpl_syms);
	mod->gpl_crcs = section_addr(info, "__kcrctab_gpl");

#ifdef CONFIG_CONSTRUCTORS
	mod->ctors = section_objs(info, ".ctors",
				  sizeof(*mod->ctors), &mod->num_ctors);
	if (!mod->ctors)
		mod->ctors = section_objs(info, ".init_array",
				sizeof(*mod->ctors), &mod->num_ctors);
	else if (find_sec(info, ".init_array")) {
		/*
		 * This shouldn't happen with same compiler and binutils
		 * building all parts of the module.
		 */
		pr_warn("%s: has both .ctors and .init_array.\n",
		       mod->name);
		return -EINVAL;
	}
#endif

	mod->noinstr_text_start = section_objs(info, ".noinstr.text", 1,
						&mod->noinstr_text_size);

#ifdef CONFIG_TRACEPOINTS
	mod->tracepoints_ptrs = section_objs(info, "__tracepoints_ptrs",
					     sizeof(*mod->tracepoints_ptrs),
					     &mod->num_tracepoints);
#endif
#ifdef CONFIG_TREE_SRCU
	mod->srcu_struct_ptrs = section_objs(info, "___srcu_struct_ptrs",
					     sizeof(*mod->srcu_struct_ptrs),
					     &mod->num_srcu_structs);
#endif
#ifdef CONFIG_BPF_EVENTS
	mod->bpf_raw_events = section_objs(info, "__bpf_raw_tp_map",
					   sizeof(*mod->bpf_raw_events),
					   &mod->num_bpf_raw_events);
#endif
#ifdef CONFIG_DEBUG_INFO_BTF_MODULES
	mod->btf_data = any_section_objs(info, ".BTF", 1, &mod->btf_data_size);
	mod->btf_base_data = any_section_objs(info, ".BTF.base", 1,
					      &mod->btf_base_data_size);
#endif
#ifdef CONFIG_JUMP_LABEL
	mod->jump_entries = section_objs(info, "__jump_table",
					sizeof(*mod->jump_entries),
					&mod->num_jump_entries);
#endif
#ifdef CONFIG_EVENT_TRACING
	mod->trace_events = section_objs(info, "_ftrace_events",
					 sizeof(*mod->trace_events),
					 &mod->num_trace_events);
	mod->trace_evals = section_objs(info, "_ftrace_eval_map",
					sizeof(*mod->trace_evals),
					&mod->num_trace_evals);
#endif
#ifdef CONFIG_TRACING
	mod->trace_bprintk_fmt_start = section_objs(info, "__trace_printk_fmt",
					 sizeof(*mod->trace_bprintk_fmt_start),
					 &mod->num_trace_bprintk_fmt);
#endif
#ifdef CONFIG_FTRACE_MCOUNT_RECORD
	/* sechdrs[0].sh_size is always zero */
	mod->ftrace_callsites = section_objs(info, FTRACE_CALLSITE_SECTION,
					     sizeof(*mod->ftrace_callsites),
					     &mod->num_ftrace_callsites);
#endif
#ifdef CONFIG_FUNCTION_ERROR_INJECTION
	mod->ei_funcs = section_objs(info, "_error_injection_whitelist",
					    sizeof(*mod->ei_funcs),
					    &mod->num_ei_funcs);
#endif
#ifdef CONFIG_KPROBES
	mod->kprobes_text_start = section_objs(info, ".kprobes.text", 1,
						&mod->kprobes_text_size);
	mod->kprobe_blacklist = section_objs(info, "_kprobe_blacklist",
						sizeof(unsigned long),
						&mod->num_kprobe_blacklist);
#endif
#ifdef CONFIG_PRINTK_INDEX
	mod->printk_index_start = section_objs(info, ".printk_index",
					       sizeof(*mod->printk_index_start),
					       &mod->printk_index_size);
#endif
#ifdef CONFIG_HAVE_STATIC_CALL_INLINE
	mod->static_call_sites = section_objs(info, ".static_call_sites",
					      sizeof(*mod->static_call_sites),
					      &mod->num_static_call_sites);
#endif
#if IS_ENABLED(CONFIG_KUNIT)
	mod->kunit_suites = section_objs(info, ".kunit_test_suites",
					      sizeof(*mod->kunit_suites),
					      &mod->num_kunit_suites);
	mod->kunit_init_suites = section_objs(info, ".kunit_init_test_suites",
					      sizeof(*mod->kunit_init_suites),
					      &mod->num_kunit_init_suites);
#endif

	mod->extable = section_objs(info, "__ex_table",
				    sizeof(*mod->extable), &mod->num_exentries);

	if (section_addr(info, "__obsparm"))
		pr_warn("%s: Ignoring obsolete parameters\n", mod->name);

#ifdef CONFIG_DYNAMIC_DEBUG_CORE
	mod->dyndbg_info.descs = section_objs(info, "__dyndbg",
					      sizeof(*mod->dyndbg_info.descs),
					      &mod->dyndbg_info.num_descs);
	mod->dyndbg_info.classes = section_objs(info, "__dyndbg_classes",
						sizeof(*mod->dyndbg_info.classes),
						&mod->dyndbg_info.num_classes);
#endif

	return 0;
}

static int move_module(struct module *mod, struct load_info *info)
{
	int i, ret;
	enum mod_mem_type t = MOD_MEM_NUM_TYPES;
	bool codetag_section_found = false;

	for_each_mod_mem_type(type) {
		if (!mod->mem[type].size) {
			mod->mem[type].base = NULL;
			continue;
		}

		ret = module_memory_alloc(mod, type);
		if (ret) {
			t = type;
			goto out_err;
		}
	}

	/* Transfer each section which specifies SHF_ALLOC */
	pr_debug("Final section addresses for %s:\n", mod->name);
	for (i = 0; i < info->hdr->e_shnum; i++) {
		void *dest;
		Elf_Shdr *shdr = &info->sechdrs[i];
		const char *sname;

		if (!(shdr->sh_flags & SHF_ALLOC))
			continue;

		sname = info->secstrings + shdr->sh_name;
		/*
		 * Load codetag sections separately as they might still be used
		 * after module unload.
		 */
		if (codetag_needs_module_section(mod, sname, shdr->sh_size)) {
			dest = codetag_alloc_module_section(mod, sname, shdr->sh_size,
					arch_mod_section_prepend(mod, i), shdr->sh_addralign);
			if (WARN_ON(!dest)) {
				ret = -EINVAL;
				goto out_err;
			}
			if (IS_ERR(dest)) {
				ret = PTR_ERR(dest);
				goto out_err;
			}
			codetag_section_found = true;
		} else {
			enum mod_mem_type type = shdr->sh_entsize >> SH_ENTSIZE_TYPE_SHIFT;
			unsigned long offset = shdr->sh_entsize & SH_ENTSIZE_OFFSET_MASK;

			dest = mod->mem[type].base + offset;
		}

		if (shdr->sh_type != SHT_NOBITS) {
			/*
			 * Our ELF checker already validated this, but let's
			 * be pedantic and make the goal clearer. We actually
			 * end up copying over all modifications made to the
			 * userspace copy of the entire struct module.
			 */
			if (i == info->index.mod &&
			   (WARN_ON_ONCE(shdr->sh_size != sizeof(struct module)))) {
				ret = -ENOEXEC;
				goto out_err;
			}
			memcpy(dest, (void *)shdr->sh_addr, shdr->sh_size);
		}
		/*
		 * Update the userspace copy's ELF section address to point to
		 * our newly allocated memory as a pure convenience so that
		 * users of info can keep taking advantage and using the newly
		 * minted official memory area.
		 */
		shdr->sh_addr = (unsigned long)dest;
		pr_debug("\t0x%lx 0x%.8lx %s\n", (long)shdr->sh_addr,
			 (long)shdr->sh_size, info->secstrings + shdr->sh_name);
	}

	return 0;
out_err:
	module_memory_restore_rox(mod);
	while (t--)
		module_memory_free(mod, t);
	if (codetag_section_found)
		codetag_free_module_sections(mod);

	return ret;
}

static int check_export_symbol_versions(struct module *mod)
{
#ifdef CONFIG_MODVERSIONS
	if ((mod->num_syms && !mod->crcs) ||
	    (mod->num_gpl_syms && !mod->gpl_crcs)) {
		return try_to_force_load(mod,
					 "no versions for exported symbols");
	}
#endif
	return 0;
}

static void flush_module_icache(const struct module *mod)
{
	/*
	 * Flush the instruction cache, since we've played with text.
	 * Do it before processing of module parameters, so the module
	 * can provide parameter accessor functions of its own.
	 */
	for_each_mod_mem_type(type) {
		const struct module_memory *mod_mem = &mod->mem[type];

		if (mod_mem->size) {
			flush_icache_range((unsigned long)mod_mem->base,
					   (unsigned long)mod_mem->base + mod_mem->size);
		}
	}
}

bool __weak module_elf_check_arch(Elf_Ehdr *hdr)
{
	return true;
}

int __weak module_frob_arch_sections(Elf_Ehdr *hdr,
				     Elf_Shdr *sechdrs,
				     char *secstrings,
				     struct module *mod)
{
	return 0;
}

/* module_blacklist is a comma-separated list of module names */
static char *module_blacklist;
static bool blacklisted(const char *module_name)
{
	const char *p;
	size_t len;

	if (!module_blacklist)
		return false;

	for (p = module_blacklist; *p; p += len) {
		len = strcspn(p, ",");
		if (strlen(module_name) == len && !memcmp(module_name, p, len))
			return true;
		if (p[len] == ',')
			len++;
	}
	return false;
}
core_param(module_blacklist, module_blacklist, charp, 0400);

static struct module *layout_and_allocate(struct load_info *info, int flags)
{
	struct module *mod;
	int err;

	/* Allow arches to frob section contents and sizes.  */
	err = module_frob_arch_sections(info->hdr, info->sechdrs,
					info->secstrings, info->mod);
	if (err < 0)
		return ERR_PTR(err);

	err = module_enforce_rwx_sections(info->hdr, info->sechdrs,
					  info->secstrings, info->mod);
	if (err < 0)
		return ERR_PTR(err);

	/* We will do a special allocation for per-cpu sections later. */
	info->sechdrs[info->index.pcpu].sh_flags &= ~(unsigned long)SHF_ALLOC;

	/*
	 * Mark relevant sections as SHF_RO_AFTER_INIT so layout_sections() can
	 * put them in the right place.
	 * Note: ro_after_init sections also have SHF_{WRITE,ALLOC} set.
	 */
	module_mark_ro_after_init(info->hdr, info->sechdrs, info->secstrings);

	/*
	 * Determine total sizes, and put offsets in sh_entsize.  For now
	 * this is done generically; there doesn't appear to be any
	 * special cases for the architectures.
	 */
	layout_sections(info->mod, info);
	layout_symtab(info->mod, info);

	/* Allocate and move to the final place */
	err = move_module(info->mod, info);
	if (err)
		return ERR_PTR(err);

	/* Module has been copied to its final place now: return it. */
	mod = (void *)info->sechdrs[info->index.mod].sh_addr;
	kmemleak_load_module(mod, info);
	codetag_module_replaced(info->mod, mod);

	return mod;
}

/* mod is no longer valid after this! */
static void module_deallocate(struct module *mod, struct load_info *info)
{
	percpu_modfree(mod);
	module_arch_freeing_init(mod);
	codetag_free_module_sections(mod);

	free_mod_mem(mod);
}

int __weak module_finalize(const Elf_Ehdr *hdr,
			   const Elf_Shdr *sechdrs,
			   struct module *me)
{
	return 0;
}

static int post_relocation(struct module *mod, const struct load_info *info)
{
	/* Sort exception table now relocations are done. */
	sort_extable(mod->extable, mod->extable + mod->num_exentries);

	/* Copy relocated percpu area over. */
	percpu_modcopy(mod, (void *)info->sechdrs[info->index.pcpu].sh_addr,
		       info->sechdrs[info->index.pcpu].sh_size);

	/* Setup kallsyms-specific fields. */
	add_kallsyms(mod, info);

	/* Arch-specific module finalizing. */
	return module_finalize(info->hdr, info->sechdrs, mod);
}

/* Call module constructors. */
static void do_mod_ctors(struct module *mod)
{
#ifdef CONFIG_CONSTRUCTORS
	unsigned long i;

	for (i = 0; i < mod->num_ctors; i++)
		mod->ctors[i]();
#endif
}

/* For freeing module_init on success, in case kallsyms traversing */
struct mod_initfree {
	struct llist_node node;
	void *init_text;
	void *init_data;
	void *init_rodata;
};

static void do_free_init(struct work_struct *w)
{
	struct llist_node *pos, *n, *list;
	struct mod_initfree *initfree;

	list = llist_del_all(&init_free_list);

	synchronize_rcu();

	llist_for_each_safe(pos, n, list) {
		initfree = container_of(pos, struct mod_initfree, node);
		execmem_free(initfree->init_text);
		execmem_free(initfree->init_data);
		execmem_free(initfree->init_rodata);
		kfree(initfree);
	}
}

void flush_module_init_free_work(void)
{
	flush_work(&init_free_wq);
}

#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX "module."
/* Default value for module->async_probe_requested */
static bool async_probe;
module_param(async_probe, bool, 0644);

/*
 * This is where the real work happens.
 *
 * Keep it uninlined to provide a reliable breakpoint target, e.g. for the gdb
 * helper command 'lx-symbols'.
 */
static noinline int do_init_module(struct module *mod)
{
	int ret = 0;
	struct mod_initfree *freeinit;
#if defined(CONFIG_MODULE_STATS)
	unsigned int text_size = 0, total_size = 0;

	for_each_mod_mem_type(type) {
		const struct module_memory *mod_mem = &mod->mem[type];
		if (mod_mem->size) {
			total_size += mod_mem->size;
			if (type == MOD_TEXT || type == MOD_INIT_TEXT)
				text_size += mod_mem->size;
		}
	}
#endif

	freeinit = kmalloc(sizeof(*freeinit), GFP_KERNEL);
	if (!freeinit) {
		ret = -ENOMEM;
		goto fail;
	}
	freeinit->init_text = mod->mem[MOD_INIT_TEXT].base;
	freeinit->init_data = mod->mem[MOD_INIT_DATA].base;
	freeinit->init_rodata = mod->mem[MOD_INIT_RODATA].base;

	do_mod_ctors(mod);
	/* Start the module */
	if (mod->init != NULL)
		ret = do_one_initcall(mod->init);
	if (ret < 0) {
		goto fail_free_freeinit;
	}
	if (ret > 0) {
		pr_warn("%s: '%s'->init suspiciously returned %d, it should "
			"follow 0/-E convention\n"
			"%s: loading module anyway...\n",
			__func__, mod->name, ret, __func__);
		dump_stack();
	}

	/* Now it's a first class citizen! */
	mod->state = MODULE_STATE_LIVE;
	blocking_notifier_call_chain(&module_notify_list,
				     MODULE_STATE_LIVE, mod);

	/* Delay uevent until module has finished its init routine */
	kobject_uevent(&mod->mkobj.kobj, KOBJ_ADD);

	/*
	 * We need to finish all async code before the module init sequence
	 * is done. This has potential to deadlock if synchronous module
	 * loading is requested from async (which is not allowed!).
	 *
	 * See commit 0fdff3ec6d87 ("async, kmod: warn on synchronous
	 * request_module() from async workers") for more details.
	 */
	if (!mod->async_probe_requested)
		async_synchronize_full();

	ftrace_free_mem(mod, mod->mem[MOD_INIT_TEXT].base,
			mod->mem[MOD_INIT_TEXT].base + mod->mem[MOD_INIT_TEXT].size);
	mutex_lock(&module_mutex);
	/* Drop initial reference. */
	module_put(mod);
	trim_init_extable(mod);
#ifdef CONFIG_KALLSYMS
	/* Switch to core kallsyms now init is done: kallsyms may be walking! */
	rcu_assign_pointer(mod->kallsyms, &mod->core_kallsyms);
#endif
	ret = module_enable_rodata_ro_after_init(mod);
	if (ret)
		pr_warn("%s: module_enable_rodata_ro_after_init() returned %d, "
			"ro_after_init data might still be writable\n",
			mod->name, ret);

	mod_tree_remove_init(mod);
	module_arch_freeing_init(mod);
	for_class_mod_mem_type(type, init) {
		mod->mem[type].base = NULL;
		mod->mem[type].size = 0;
	}

#ifdef CONFIG_DEBUG_INFO_BTF_MODULES
	/* .BTF is not SHF_ALLOC and will get removed, so sanitize pointers */
	mod->btf_data = NULL;
	mod->btf_base_data = NULL;
#endif
	/*
	 * We want to free module_init, but be aware that kallsyms may be
	 * walking this within an RCU read section. In all the failure paths, we
	 * call synchronize_rcu(), but we don't want to slow down the success
	 * path. execmem_free() cannot be called in an interrupt, so do the
	 * work and call synchronize_rcu() in a work queue.
	 *
	 * Note that execmem_alloc() on most architectures creates W+X page
	 * mappings which won't be cleaned up until do_free_init() runs.  Any
	 * code such as mark_rodata_ro() which depends on those mappings to
	 * be cleaned up needs to sync with the queued work by invoking
	 * flush_module_init_free_work().
	 */
	if (llist_add(&freeinit->node, &init_free_list))
		schedule_work(&init_free_wq);

	mutex_unlock(&module_mutex);
	wake_up_all(&module_wq);

	mod_stat_add_long(text_size, &total_text_size);
	mod_stat_add_long(total_size, &total_mod_size);

	mod_stat_inc(&modcount);

	return 0;

fail_free_freeinit:
	kfree(freeinit);
fail:
	/* Try to protect us from buggy refcounters. */
	mod->state = MODULE_STATE_GOING;
	synchronize_rcu();
	module_put(mod);
	blocking_notifier_call_chain(&module_notify_list,
				     MODULE_STATE_GOING, mod);
	klp_module_going(mod);
	ftrace_release_mod(mod);
	free_module(mod);
	wake_up_all(&module_wq);

	return ret;
}

static int may_init_module(void)
{
	if (!capable(CAP_SYS_MODULE) || modules_disabled)
		return -EPERM;

	return 0;
}

/* Is this module of this name done loading?  No locks held. */
static bool finished_loading(const char *name)
{
	struct module *mod;
	bool ret;

	/*
	 * The module_mutex should not be a heavily contended lock;
	 * if we get the occasional sleep here, we'll go an extra iteration
	 * in the wait_event_interruptible(), which is harmless.
	 */
	sched_annotate_sleep();
	mutex_lock(&module_mutex);
	mod = find_module_all(name, strlen(name), true);
	ret = !mod || mod->state == MODULE_STATE_LIVE
		|| mod->state == MODULE_STATE_GOING;
	mutex_unlock(&module_mutex);

	return ret;
}

/* Must be called with module_mutex held */
static int module_patient_check_exists(const char *name,
				       enum fail_dup_mod_reason reason)
{
	struct module *old;
	int err = 0;

	old = find_module_all(name, strlen(name), true);
	if (old == NULL)
		return 0;

	if (old->state == MODULE_STATE_COMING ||
	    old->state == MODULE_STATE_UNFORMED) {
		/* Wait in case it fails to load. */
		mutex_unlock(&module_mutex);
		err = wait_event_interruptible(module_wq,
				       finished_loading(name));
		mutex_lock(&module_mutex);
		if (err)
			return err;

		/* The module might have gone in the meantime. */
		old = find_module_all(name, strlen(name), true);
	}

	if (try_add_failed_module(name, reason))
		pr_warn("Could not add fail-tracking for module: %s\n", name);

	/*
	 * We are here only when the same module was being loaded. Do
	 * not try to load it again right now. It prevents long delays
	 * caused by serialized module load failures. It might happen
	 * when more devices of the same type trigger load of
	 * a particular module.
	 */
	if (old && old->state == MODULE_STATE_LIVE)
		return -EEXIST;
	return -EBUSY;
}

/*
 * We try to place it in the list now to make sure it's unique before
 * we dedicate too many resources.  In particular, temporary percpu
 * memory exhaustion.
 */
static int add_unformed_module(struct module *mod)
{
	int err;

	mod->state = MODULE_STATE_UNFORMED;

	mutex_lock(&module_mutex);
	err = module_patient_check_exists(mod->name, FAIL_DUP_MOD_LOAD);
	if (err)
		goto out;

	mod_update_bounds(mod);
	list_add_rcu(&mod->list, &modules);
	mod_tree_insert(mod);
	err = 0;

out:
	mutex_unlock(&module_mutex);
	return err;
}

static int complete_formation(struct module *mod, struct load_info *info)
{
	int err;

	mutex_lock(&module_mutex);

	/* Find duplicate symbols (must be called under lock). */
	err = verify_exported_symbols(mod);
	if (err < 0)
		goto out;

	/* These rely on module_mutex for list integrity. */
	module_bug_finalize(info->hdr, info->sechdrs, mod);
	module_cfi_finalize(info->hdr, info->sechdrs, mod);

	err = module_enable_rodata_ro(mod);
	if (err)
		goto out_strict_rwx;
	err = module_enable_data_nx(mod);
	if (err)
		goto out_strict_rwx;
	err = module_enable_text_rox(mod);
	if (err)
		goto out_strict_rwx;

	/*
	 * Mark state as coming so strong_try_module_get() ignores us,
	 * but kallsyms etc. can see us.
	 */
	mod->state = MODULE_STATE_COMING;
	mutex_unlock(&module_mutex);

	return 0;

out_strict_rwx:
	module_bug_cleanup(mod);
out:
	mutex_unlock(&module_mutex);
	return err;
}

static int prepare_coming_module(struct module *mod)
{
	int err;

	ftrace_module_enable(mod);
	err = klp_module_coming(mod);
	if (err)
		return err;

	err = blocking_notifier_call_chain_robust(&module_notify_list,
			MODULE_STATE_COMING, MODULE_STATE_GOING, mod);
	err = notifier_to_errno(err);
	if (err)
		klp_module_going(mod);

	return err;
}

static int unknown_module_param_cb(char *param, char *val, const char *modname,
				   void *arg)
{
	struct module *mod = arg;
	int ret;

	if (strcmp(param, "async_probe") == 0) {
		if (kstrtobool(val, &mod->async_probe_requested))
			mod->async_probe_requested = true;
		return 0;
	}

	/* Check for magic 'dyndbg' arg */
	ret = ddebug_dyndbg_module_param_cb(param, val, modname);
	if (ret != 0)
		pr_warn("%s: unknown parameter '%s' ignored\n", modname, param);
	return 0;
}

/* Module within temporary copy, this doesn't do any allocation  */
static int early_mod_check(struct load_info *info, int flags)
{
	int err;

	/*
	 * Now that we know we have the correct module name, check
	 * if it's blacklisted.
	 */
	if (blacklisted(info->name)) {
		pr_err("Module %s is blacklisted\n", info->name);
		return -EPERM;
	}

	err = rewrite_section_headers(info, flags);
	if (err)
		return err;

	/* Check module struct version now, before we try to use module. */
	if (!check_modstruct_version(info, info->mod))
		return -ENOEXEC;

	err = check_modinfo(info->mod, info, flags);
	if (err)
		return err;

	mutex_lock(&module_mutex);
	err = module_patient_check_exists(info->mod->name, FAIL_DUP_MOD_BECOMING);
	mutex_unlock(&module_mutex);

	return err;
}

/*
 * Allocate and load the module: note that size of section 0 is always
 * zero, and we rely on this for optional sections.
 */
static int load_module(struct load_info *info, const char __user *uargs,
		       int flags)
{
	struct module *mod;
	bool module_allocated = false;
	long err = 0;
	char *after_dashes;

	/*
	 * Do the signature check (if any) first. All that
	 * the signature check needs is info->len, it does
	 * not need any of the section info. That can be
	 * set up later. This will minimize the chances
	 * of a corrupt module causing problems before
	 * we even get to the signature check.
	 *
	 * The check will also adjust info->len by stripping
	 * off the sig length at the end of the module, making
	 * checks against info->len more correct.
	 */
	err = module_sig_check(info, flags);
	if (err)
		goto free_copy;

	/*
	 * Do basic sanity checks against the ELF header and
	 * sections. Cache useful sections and set the
	 * info->mod to the userspace passed struct module.
	 */
	err = elf_validity_cache_copy(info, flags);
	if (err)
		goto free_copy;

	err = early_mod_check(info, flags);
	if (err)
		goto free_copy;

	/* Figure out module layout, and allocate all the memory. */
	mod = layout_and_allocate(info, flags);
	if (IS_ERR(mod)) {
		err = PTR_ERR(mod);
		goto free_copy;
	}

	module_allocated = true;

	audit_log_kern_module(info->name);

	/* Reserve our place in the list. */
	err = add_unformed_module(mod);
	if (err)
		goto free_module;

	/*
	 * We are tainting your kernel if your module gets into
	 * the modules linked list somehow.
	 */
	module_augment_kernel_taints(mod, info);

	/* To avoid stressing percpu allocator, do this once we're unique. */
	err = percpu_modalloc(mod, info);
	if (err)
		goto unlink_mod;

	/* Now module is in final location, initialize linked lists, etc. */
	err = module_unload_init(mod);
	if (err)
		goto unlink_mod;

	init_param_lock(mod);

	/*
	 * Now we've got everything in the final locations, we can
	 * find optional sections.
	 */
	err = find_module_sections(mod, info);
	if (err)
		goto free_unload;

	err = check_export_symbol_versions(mod);
	if (err)
		goto free_unload;

	/* Set up MODINFO_ATTR fields */
	err = setup_modinfo(mod, info);
	if (err)
		goto free_modinfo;

	/* Fix up syms, so that st_value is a pointer to location. */
	err = simplify_symbols(mod, info);
	if (err < 0)
		goto free_modinfo;

	err = apply_relocations(mod, info);
	if (err < 0)
		goto free_modinfo;

	err = post_relocation(mod, info);
	if (err < 0)
		goto free_modinfo;

	flush_module_icache(mod);

	/* Now copy in args */
	mod->args = strndup_user(uargs, ~0UL >> 1);
	if (IS_ERR(mod->args)) {
		err = PTR_ERR(mod->args);
		goto free_arch_cleanup;
	}

	init_build_id(mod, info);

	/* Ftrace init must be called in the MODULE_STATE_UNFORMED state */
	ftrace_module_init(mod);

	/* Finally it's fully formed, ready to start executing. */
	err = complete_formation(mod, info);
	if (err)
		goto ddebug_cleanup;

	err = prepare_coming_module(mod);
	if (err)
		goto bug_cleanup;

	mod->async_probe_requested = async_probe;

	/* Module is ready to execute: parsing args may do that. */
	after_dashes = parse_args(mod->name, mod->args, mod->kp, mod->num_kp,
				  -32768, 32767, mod,
				  unknown_module_param_cb);
	if (IS_ERR(after_dashes)) {
		err = PTR_ERR(after_dashes);
		goto coming_cleanup;
	} else if (after_dashes) {
		pr_warn("%s: parameters '%s' after `--' ignored\n",
		       mod->name, after_dashes);
	}

	/* Link in to sysfs. */
	err = mod_sysfs_setup(mod, info, mod->kp, mod->num_kp);
	if (err < 0)
		goto coming_cleanup;

	if (is_livepatch_module(mod)) {
		err = copy_module_elf(mod, info);
		if (err < 0)
			goto sysfs_cleanup;
	}

	if (codetag_load_module(mod))
		goto sysfs_cleanup;

	/* Get rid of temporary copy. */
	free_copy(info, flags);

	/* Done! */
	trace_module_load(mod);

	return do_init_module(mod);

 sysfs_cleanup:
	mod_sysfs_teardown(mod);
 coming_cleanup:
	mod->state = MODULE_STATE_GOING;
	destroy_params(mod->kp, mod->num_kp);
	blocking_notifier_call_chain(&module_notify_list,
				     MODULE_STATE_GOING, mod);
	klp_module_going(mod);
 bug_cleanup:
	mod->state = MODULE_STATE_GOING;
	/* module_bug_cleanup needs module_mutex protection */
	mutex_lock(&module_mutex);
	module_bug_cleanup(mod);
	mutex_unlock(&module_mutex);

 ddebug_cleanup:
	ftrace_release_mod(mod);
	synchronize_rcu();
	kfree(mod->args);
 free_arch_cleanup:
	module_arch_cleanup(mod);
 free_modinfo:
	free_modinfo(mod);
 free_unload:
	module_unload_free(mod);
 unlink_mod:
	mutex_lock(&module_mutex);
	/* Unlink carefully: kallsyms could be walking list. */
	list_del_rcu(&mod->list);
	mod_tree_remove(mod);
	wake_up_all(&module_wq);
	/* Wait for RCU-sched synchronizing before releasing mod->list. */
	synchronize_rcu();
	mutex_unlock(&module_mutex);
 free_module:
	mod_stat_bump_invalid(info, flags);
	/* Free lock-classes; relies on the preceding sync_rcu() */
	for_class_mod_mem_type(type, core_data) {
		lockdep_free_key_range(mod->mem[type].base,
				       mod->mem[type].size);
	}

	module_memory_restore_rox(mod);
	module_deallocate(mod, info);
 free_copy:
	/*
	 * The info->len is always set. We distinguish between
	 * failures once the proper module was allocated and
	 * before that.
	 */
	if (!module_allocated) {
		audit_log_kern_module(info->name ? info->name : "?");
		mod_stat_bump_becoming(info, flags);
	}
	free_copy(info, flags);
	return err;
}

SYSCALL_DEFINE3(init_module, void __user *, umod,
		unsigned long, len, const char __user *, uargs)
{
	int err;
	struct load_info info = { };

	err = may_init_module();
	if (err)
		return err;

	pr_debug("init_module: umod=%p, len=%lu, uargs=%p\n",
	       umod, len, uargs);

	err = copy_module_from_user(umod, len, &info);
	if (err) {
		mod_stat_inc(&failed_kreads);
		mod_stat_add_long(len, &invalid_kread_bytes);
		return err;
	}

	return load_module(&info, uargs, 0);
}

struct idempotent {
	const void *cookie;
	struct hlist_node entry;
	struct completion complete;
	int ret;
};

#define IDEM_HASH_BITS 8
static struct hlist_head idem_hash[1 << IDEM_HASH_BITS];
static DEFINE_SPINLOCK(idem_lock);

static bool idempotent(struct idempotent *u, const void *cookie)
{
	int hash = hash_ptr(cookie, IDEM_HASH_BITS);
	struct hlist_head *head = idem_hash + hash;
	struct idempotent *existing;
	bool first;

	u->ret = -EINTR;
	u->cookie = cookie;
	init_completion(&u->complete);

	spin_lock(&idem_lock);
	first = true;
	hlist_for_each_entry(existing, head, entry) {
		if (existing->cookie != cookie)
			continue;
		first = false;
		break;
	}
	hlist_add_head(&u->entry, idem_hash + hash);
	spin_unlock(&idem_lock);

	return !first;
}

/*
 * We were the first one with 'cookie' on the list, and we ended
 * up completing the operation. We now need to walk the list,
 * remove everybody - which includes ourselves - fill in the return
 * value, and then complete the operation.
 */
static int idempotent_complete(struct idempotent *u, int ret)
{
	const void *cookie = u->cookie;
	int hash = hash_ptr(cookie, IDEM_HASH_BITS);
	struct hlist_head *head = idem_hash + hash;
	struct hlist_node *next;
	struct idempotent *pos;

	spin_lock(&idem_lock);
	hlist_for_each_entry_safe(pos, next, head, entry) {
		if (pos->cookie != cookie)
			continue;
		hlist_del_init(&pos->entry);
		pos->ret = ret;
		complete(&pos->complete);
	}
	spin_unlock(&idem_lock);
	return ret;
}

/*
 * Wait for the idempotent worker.
 *
 * If we get interrupted, we need to remove ourselves from the
 * the idempotent list, and the completion may still come in.
 *
 * The 'idem_lock' protects against the race, and 'idem.ret' was
 * initialized to -EINTR and is thus always the right return
 * value even if the idempotent work then completes between
 * the wait_for_completion and the cleanup.
 */
static int idempotent_wait_for_completion(struct idempotent *u)
{
	if (wait_for_completion_interruptible(&u->complete)) {
		spin_lock(&idem_lock);
		if (!hlist_unhashed(&u->entry))
			hlist_del(&u->entry);
		spin_unlock(&idem_lock);
	}
	return u->ret;
}

static int init_module_from_file(struct file *f, const char __user * uargs, int flags)
{
	struct load_info info = { };
	void *buf = NULL;
	int len;

	len = kernel_read_file(f, 0, &buf, INT_MAX, NULL, READING_MODULE);
	if (len < 0) {
		mod_stat_inc(&failed_kreads);
		return len;
	}

	if (flags & MODULE_INIT_COMPRESSED_FILE) {
		int err = module_decompress(&info, buf, len);
		vfree(buf); /* compressed data is no longer needed */
		if (err) {
			mod_stat_inc(&failed_decompress);
			mod_stat_add_long(len, &invalid_decompress_bytes);
			return err;
		}
	} else {
		info.hdr = buf;
		info.len = len;
	}

	return load_module(&info, uargs, flags);
}

static int idempotent_init_module(struct file *f, const char __user * uargs, int flags)
{
	struct idempotent idem;

	if (!(f->f_mode & FMODE_READ))
		return -EBADF;

	/* Are we the winners of the race and get to do this? */
	if (!idempotent(&idem, file_inode(f))) {
		int ret = init_module_from_file(f, uargs, flags);
		return idempotent_complete(&idem, ret);
	}

	/*
	 * Somebody else won the race and is loading the module.
	 */
	return idempotent_wait_for_completion(&idem);
}

SYSCALL_DEFINE3(finit_module, int, fd, const char __user *, uargs, int, flags)
{
	int err = may_init_module();
	if (err)
		return err;

	pr_debug("finit_module: fd=%d, uargs=%p, flags=%i\n", fd, uargs, flags);

	if (flags & ~(MODULE_INIT_IGNORE_MODVERSIONS
		      |MODULE_INIT_IGNORE_VERMAGIC
		      |MODULE_INIT_COMPRESSED_FILE))
		return -EINVAL;

	CLASS(fd, f)(fd);
	if (fd_empty(f))
		return -EBADF;
	return idempotent_init_module(fd_file(f), uargs, flags);
}

/* Keep in sync with MODULE_FLAGS_BUF_SIZE !!! */
char *module_flags(struct module *mod, char *buf, bool show_state)
{
	int bx = 0;

	BUG_ON(mod->state == MODULE_STATE_UNFORMED);
	if (!mod->taints && !show_state)
		goto out;
	if (mod->taints ||
	    mod->state == MODULE_STATE_GOING ||
	    mod->state == MODULE_STATE_COMING) {
		buf[bx++] = '(';
		bx += module_flags_taint(mod->taints, buf + bx);
		/* Show a - for module-is-being-unloaded */
		if (mod->state == MODULE_STATE_GOING && show_state)
			buf[bx++] = '-';
		/* Show a + for module-is-being-loaded */
		if (mod->state == MODULE_STATE_COMING && show_state)
			buf[bx++] = '+';
		buf[bx++] = ')';
	}
out:
	buf[bx] = '\0';

	return buf;
}

/* Given an address, look for it in the module exception tables. */
const struct exception_table_entry *search_module_extables(unsigned long addr)
{
	struct module *mod;

	guard(rcu)();
	mod = __module_address(addr);
	if (!mod)
		return NULL;

	if (!mod->num_exentries)
		return NULL;
	/*
	 * The address passed here belongs to a module that is currently
	 * invoked (we are running inside it). Therefore its module::refcnt
	 * needs already be >0 to ensure that it is not removed at this stage.
	 * All other user need to invoke this function within a RCU read
	 * section.
	 */
	return search_extable(mod->extable, mod->num_exentries, addr);
}

/**
 * is_module_address() - is this address inside a module?
 * @addr: the address to check.
 *
 * See is_module_text_address() if you simply want to see if the address
 * is code (not data).
 */
bool is_module_address(unsigned long addr)
{
	guard(rcu)();
	return __module_address(addr) != NULL;
}

/**
 * __module_address() - get the module which contains an address.
 * @addr: the address.
 *
 * Must be called within RCU read section or module mutex held so that
 * module doesn't get freed during this.
 */
struct module *__module_address(unsigned long addr)
{
	struct module *mod;

	if (addr >= mod_tree.addr_min && addr <= mod_tree.addr_max)
		goto lookup;

#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
	if (addr >= mod_tree.data_addr_min && addr <= mod_tree.data_addr_max)
		goto lookup;
#endif

	return NULL;

lookup:
	mod = mod_find(addr, &mod_tree);
	if (mod) {
		BUG_ON(!within_module(addr, mod));
		if (mod->state == MODULE_STATE_UNFORMED)
			mod = NULL;
	}
	return mod;
}

/**
 * is_module_text_address() - is this address inside module code?
 * @addr: the address to check.
 *
 * See is_module_address() if you simply want to see if the address is
 * anywhere in a module.  See kernel_text_address() for testing if an
 * address corresponds to kernel or module code.
 */
bool is_module_text_address(unsigned long addr)
{
	guard(rcu)();
	return __module_text_address(addr) != NULL;
}

void module_for_each_mod(int(*func)(struct module *mod, void *data), void *data)
{
	struct module *mod;

	guard(rcu)();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (mod->state == MODULE_STATE_UNFORMED)
			continue;
		if (func(mod, data))
			break;
	}
}

/**
 * __module_text_address() - get the module whose code contains an address.
 * @addr: the address.
 *
 * Must be called within RCU read section or module mutex held so that
 * module doesn't get freed during this.
 */
struct module *__module_text_address(unsigned long addr)
{
	struct module *mod = __module_address(addr);
	if (mod) {
		/* Make sure it's within the text section. */
		if (!within_module_mem_type(addr, mod, MOD_TEXT) &&
		    !within_module_mem_type(addr, mod, MOD_INIT_TEXT))
			mod = NULL;
	}
	return mod;
}

/* Don't grab lock, we're oopsing. */
void print_modules(void)
{
	struct module *mod;
	char buf[MODULE_FLAGS_BUF_SIZE];

	printk(KERN_DEFAULT "Modules linked in:");
	/* Most callers should already have preempt disabled, but make sure */
	guard(rcu)();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (mod->state == MODULE_STATE_UNFORMED)
			continue;
		pr_cont(" %s%s", mod->name, module_flags(mod, buf, true));
	}

	print_unloaded_tainted_modules();
	if (last_unloaded_module.name[0])
		pr_cont(" [last unloaded: %s%s]", last_unloaded_module.name,
			last_unloaded_module.taints);
	pr_cont("\n");
}

#ifdef CONFIG_MODULE_DEBUGFS
struct dentry *mod_debugfs_root;

static int module_debugfs_init(void)
{
	mod_debugfs_root = debugfs_create_dir("modules", NULL);
	return 0;
}
module_init(module_debugfs_init);
#endif
