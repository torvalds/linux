// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2002 Richard Henderson
 * Copyright (C) 2001 Rusty Russell, 2002, 2010 Rusty Russell IBM.
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
#include <uapi/linux/module.h>
#include "internal.h"

#define CREATE_TRACE_POINTS
#include <trace/events/module.h>

/*
 * Mutex protects:
 * 1) List of modules (also safely readable with preempt_disable),
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

#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
struct mod_tree_root mod_data_tree __cacheline_aligned = {
	.addr_min = -1UL,
};
#endif

#define module_addr_min mod_tree.addr_min
#define module_addr_max mod_tree.addr_max

struct symsearch {
	const struct kernel_symbol *start, *stop;
	const s32 *crcs;
	enum mod_license license;
};

/*
 * Bounds of module text, for speeding up __module_address.
 * Protected by module_mutex.
 */
static void __mod_update_bounds(void *base, unsigned int size, struct mod_tree_root *tree)
{
	unsigned long min = (unsigned long)base;
	unsigned long max = min + size;

	if (min < tree->addr_min)
		tree->addr_min = min;
	if (max > tree->addr_max)
		tree->addr_max = max;
}

static void mod_update_bounds(struct module *mod)
{
	__mod_update_bounds(mod->core_layout.base, mod->core_layout.size, &mod_tree);
	if (mod->init_layout.size)
		__mod_update_bounds(mod->init_layout.base, mod->init_layout.size, &mod_tree);
#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
	__mod_update_bounds(mod->data_layout.base, mod->data_layout.size, &mod_data_tree);
#endif
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
 * (optional) module which owns it.  Needs preempt disabled or module_mutex.
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

	module_assert_mutex_or_preempt();

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
 * Search for module by name: must hold module_mutex (or preempt disabled
 * for read-only access).
 */
struct module *find_module_all(const char *name, size_t len,
			       bool even_unformed)
{
	struct module *mod;

	module_assert_mutex_or_preempt();

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

	preempt_disable();

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
				preempt_enable();
				return true;
			}
		}
	}

	preempt_enable();
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
static ssize_t show_modinfo_##field(struct module_attribute *mattr,   \
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
static struct module_attribute modinfo_##field = {                    \
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
		if (use->source == a) {
			pr_debug("%s uses %s!\n", a->name, b->name);
			return 1;
		}
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
	strscpy(last_unloaded_module.name, mod->name, sizeof(last_unloaded_module.name));
	strscpy(last_unloaded_module.taints, module_flags(mod, buf, false), sizeof(last_unloaded_module.taints));

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

	preempt_disable();
	BUG_ON(!find_symbol(&fsa));
	module_put(fsa.owner);
	preempt_enable();
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
	 * disable preemption in order to safely traverse the data structure.
	 */
	preempt_disable();
	modaddr = __module_text_address(a);
	BUG_ON(!modaddr);
	module_put(modaddr);
	preempt_enable();
}
EXPORT_SYMBOL_GPL(symbol_put_addr);

static ssize_t show_refcnt(struct module_attribute *mattr,
			   struct module_kobject *mk, char *buffer)
{
	return sprintf(buffer, "%i\n", module_refcount(mk->mod));
}

static struct module_attribute modinfo_refcnt =
	__ATTR(refcnt, 0444, show_refcnt, NULL);

void __module_get(struct module *module)
{
	if (module) {
		preempt_disable();
		atomic_inc(&module->refcnt);
		trace_module_get(module, _RET_IP_);
		preempt_enable();
	}
}
EXPORT_SYMBOL(__module_get);

bool try_module_get(struct module *module)
{
	bool ret = true;

	if (module) {
		preempt_disable();
		/* Note: here, we can fail to get a reference */
		if (likely(module_is_live(module) &&
			   atomic_inc_not_zero(&module->refcnt) != 0))
			trace_module_get(module, _RET_IP_);
		else
			ret = false;

		preempt_enable();
	}
	return ret;
}
EXPORT_SYMBOL(try_module_get);

void module_put(struct module *module)
{
	int ret;

	if (module) {
		preempt_disable();
		ret = atomic_dec_if_positive(&module->refcnt);
		WARN_ON(ret < 0);	/* Failed to put refcount */
		trace_module_put(module, _RET_IP_);
		preempt_enable();
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

static ssize_t show_initstate(struct module_attribute *mattr,
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

static struct module_attribute modinfo_initstate =
	__ATTR(initstate, 0444, show_initstate, NULL);

static ssize_t store_uevent(struct module_attribute *mattr,
			    struct module_kobject *mk,
			    const char *buffer, size_t count)
{
	int rc;

	rc = kobject_synth_uevent(&mk->kobj, buffer, count);
	return rc ? rc : count;
}

struct module_attribute module_uevent =
	__ATTR(uevent, 0200, NULL, store_uevent);

static ssize_t show_coresize(struct module_attribute *mattr,
			     struct module_kobject *mk, char *buffer)
{
	return sprintf(buffer, "%u\n", mk->mod->core_layout.size);
}

static struct module_attribute modinfo_coresize =
	__ATTR(coresize, 0444, show_coresize, NULL);

#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
static ssize_t show_datasize(struct module_attribute *mattr,
			     struct module_kobject *mk, char *buffer)
{
	return sprintf(buffer, "%u\n", mk->mod->data_layout.size);
}

static struct module_attribute modinfo_datasize =
	__ATTR(datasize, 0444, show_datasize, NULL);
#endif

static ssize_t show_initsize(struct module_attribute *mattr,
			     struct module_kobject *mk, char *buffer)
{
	return sprintf(buffer, "%u\n", mk->mod->init_layout.size);
}

static struct module_attribute modinfo_initsize =
	__ATTR(initsize, 0444, show_initsize, NULL);

static ssize_t show_taint(struct module_attribute *mattr,
			  struct module_kobject *mk, char *buffer)
{
	size_t l;

	l = module_flags_taint(mk->mod->taints, buffer);
	buffer[l++] = '\n';
	return l;
}

static struct module_attribute modinfo_taint =
	__ATTR(taint, 0444, show_taint, NULL);

struct module_attribute *modinfo_attrs[] = {
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

size_t modinfo_attrs_count = ARRAY_SIZE(modinfo_attrs);

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

static char *get_modinfo(const struct load_info *info, const char *tag);
static char *get_next_modinfo(const struct load_info *info, const char *tag,
			      char *prev);

static int verify_namespace_is_imported(const struct load_info *info,
					const struct kernel_symbol *sym,
					struct module *mod)
{
	const char *namespace;
	char *imported_namespace;

	namespace = kernel_symbol_namespace(sym);
	if (namespace && namespace[0]) {
		imported_namespace = get_modinfo(info, "import_ns");
		while (imported_namespace) {
			if (strcmp(namespace, imported_namespace) == 0)
				return 0;
			imported_namespace = get_next_modinfo(
				info, "import_ns", imported_namespace);
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
	strncpy(ownername, module_name(fsa.owner), MODULE_NAME_LEN);
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

void __weak module_memfree(void *module_region)
{
	/*
	 * This memory may be RO, and freeing RO memory in an interrupt is not
	 * supported by vmalloc.
	 */
	WARN_ON(in_interrupt());
	vfree(module_region);
}

void __weak module_arch_cleanup(struct module *mod)
{
}

void __weak module_arch_freeing_init(struct module *mod)
{
}

/* Free a module, remove from lists, etc. */
static void free_module(struct module *mod)
{
	trace_module_free(mod);

	mod_sysfs_teardown(mod);

	/*
	 * We leave it in list to prevent duplicate loads, but make sure
	 * that noone uses it while it's being deconstructed.
	 */
	mutex_lock(&module_mutex);
	mod->state = MODULE_STATE_UNFORMED;
	mutex_unlock(&module_mutex);

	/* Remove dynamic debug info */
	ddebug_remove_module(mod->name);

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
	/* Wait for RCU-sched synchronizing before releasing mod->list and buglist. */
	synchronize_rcu();
	if (try_add_tainted_module(mod))
		pr_err("%s: adding tainted module to the unloaded tainted modules list failed.\n",
		       mod->name);
	mutex_unlock(&module_mutex);

	/* This may be empty, but that's OK */
	module_arch_freeing_init(mod);
	module_memfree(mod->init_layout.base);
	kfree(mod->args);
	percpu_modfree(mod);

	/* Free lock-classes; relies on the preceding sync_rcu(). */
	lockdep_free_key_range(mod->data_layout.base, mod->data_layout.size);

	/* Finally, free the core (containing the module structure) */
	module_memfree(mod->core_layout.base);
#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
	vfree(mod->data_layout.base);
#endif
}

void *__symbol_get(const char *symbol)
{
	struct find_symbol_arg fsa = {
		.name	= symbol,
		.gplok	= true,
		.warn	= true,
	};

	preempt_disable();
	if (!find_symbol(&fsa))
		goto fail;
	if (fsa.license != GPL_ONLY) {
		pr_warn("failing symbol_get of non-GPLONLY symbol %s.\n",
			symbol);
		goto fail;
	}
	if (strong_try_module_get(fsa.owner))
		goto fail;
	preempt_enable();
	return (void *)kernel_symbol_value(fsa.sym);
fail:
	preempt_enable();
	return NULL;
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
			pr_debug("Absolute symbol: 0x%08lx\n",
			       (long)sym[i].st_value);
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

		/* Don't bother with non-allocated sections */
		if (!(info->sechdrs[infosec].sh_flags & SHF_ALLOC))
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

/* Update size with this section: return offset. */
long module_get_offset(struct module *mod, unsigned int *size,
		       Elf_Shdr *sechdr, unsigned int section)
{
	long ret;

	*size += arch_mod_section_prepend(mod, section);
	ret = ALIGN(*size, sechdr->sh_addralign ?: 1);
	*size = ret + sechdr->sh_size;
	return ret;
}

bool module_init_layout_section(const char *sname)
{
#ifndef CONFIG_MODULE_UNLOAD
	if (module_exit_section(sname))
		return true;
#endif
	return module_init_section(sname);
}

/*
 * Lay out the SHF_ALLOC sections in a way not dissimilar to how ld
 * might -- code, read-only data, read-write data, small data.  Tally
 * sizes, and place the offsets into sh_entsize fields: high bit means it
 * belongs in init.
 */
static void layout_sections(struct module *mod, struct load_info *info)
{
	static unsigned long const masks[][2] = {
		/*
		 * NOTE: all executable code must be the first section
		 * in this array; otherwise modify the text_size
		 * finder in the two loops below
		 */
		{ SHF_EXECINSTR | SHF_ALLOC, ARCH_SHF_SMALL },
		{ SHF_ALLOC, SHF_WRITE | ARCH_SHF_SMALL },
		{ SHF_RO_AFTER_INIT | SHF_ALLOC, ARCH_SHF_SMALL },
		{ SHF_WRITE | SHF_ALLOC, ARCH_SHF_SMALL },
		{ ARCH_SHF_SMALL | SHF_ALLOC, 0 }
	};
	unsigned int m, i;

	for (i = 0; i < info->hdr->e_shnum; i++)
		info->sechdrs[i].sh_entsize = ~0UL;

	pr_debug("Core section allocation order:\n");
	for (m = 0; m < ARRAY_SIZE(masks); ++m) {
		for (i = 0; i < info->hdr->e_shnum; ++i) {
			Elf_Shdr *s = &info->sechdrs[i];
			const char *sname = info->secstrings + s->sh_name;
			unsigned int *sizep;

			if ((s->sh_flags & masks[m][0]) != masks[m][0]
			    || (s->sh_flags & masks[m][1])
			    || s->sh_entsize != ~0UL
			    || module_init_layout_section(sname))
				continue;
			sizep = m ? &mod->data_layout.size : &mod->core_layout.size;
			s->sh_entsize = module_get_offset(mod, sizep, s, i);
			pr_debug("\t%s\n", sname);
		}
		switch (m) {
		case 0: /* executable */
			mod->core_layout.size = strict_align(mod->core_layout.size);
			mod->core_layout.text_size = mod->core_layout.size;
			break;
		case 1: /* RO: text and ro-data */
			mod->data_layout.size = strict_align(mod->data_layout.size);
			mod->data_layout.ro_size = mod->data_layout.size;
			break;
		case 2: /* RO after init */
			mod->data_layout.size = strict_align(mod->data_layout.size);
			mod->data_layout.ro_after_init_size = mod->data_layout.size;
			break;
		case 4: /* whole core */
			mod->data_layout.size = strict_align(mod->data_layout.size);
			break;
		}
	}

	pr_debug("Init section allocation order:\n");
	for (m = 0; m < ARRAY_SIZE(masks); ++m) {
		for (i = 0; i < info->hdr->e_shnum; ++i) {
			Elf_Shdr *s = &info->sechdrs[i];
			const char *sname = info->secstrings + s->sh_name;

			if ((s->sh_flags & masks[m][0]) != masks[m][0]
			    || (s->sh_flags & masks[m][1])
			    || s->sh_entsize != ~0UL
			    || !module_init_layout_section(sname))
				continue;
			s->sh_entsize = (module_get_offset(mod, &mod->init_layout.size, s, i)
					 | INIT_OFFSET_MASK);
			pr_debug("\t%s\n", sname);
		}
		switch (m) {
		case 0: /* executable */
			mod->init_layout.size = strict_align(mod->init_layout.size);
			mod->init_layout.text_size = mod->init_layout.size;
			break;
		case 1: /* RO: text and ro-data */
			mod->init_layout.size = strict_align(mod->init_layout.size);
			mod->init_layout.ro_size = mod->init_layout.size;
			break;
		case 2:
			/*
			 * RO after init doesn't apply to init_layout (only
			 * core_layout), so it just takes the value of ro_size.
			 */
			mod->init_layout.ro_after_init_size = mod->init_layout.ro_size;
			break;
		case 4: /* whole init */
			mod->init_layout.size = strict_align(mod->init_layout.size);
			break;
		}
	}
}

static void set_license(struct module *mod, const char *license)
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

/* Parse tag=value strings from .modinfo section */
static char *next_string(char *string, unsigned long *secsize)
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
		modinfo = next_string(prev, &size);
	}

	for (p = modinfo; p; p = next_string(p, &size)) {
		if (strncmp(p, tag, taglen) == 0 && p[taglen] == '=')
			return p + taglen + 1;
	}
	return NULL;
}

static char *get_modinfo(const struct load_info *info, const char *tag)
{
	return get_next_modinfo(info, tag, NULL);
}

static void setup_modinfo(struct module *mod, struct load_info *info)
{
	struct module_attribute *attr;
	int i;

	for (i = 0; (attr = modinfo_attrs[i]); i++) {
		if (attr->setup)
			attr->setup(mod, get_modinfo(info, attr->attr.name));
	}
}

static void free_modinfo(struct module *mod)
{
	struct module_attribute *attr;
	int i;

	for (i = 0; (attr = modinfo_attrs[i]); i++) {
		if (attr->free)
			attr->free(mod);
	}
}

static void dynamic_debug_setup(struct module *mod, struct _ddebug_info *dyndbg)
{
	if (!dyndbg->num_descs)
		return;
	ddebug_add_module(dyndbg, mod->name);
}

static void dynamic_debug_remove(struct module *mod, struct _ddebug_info *dyndbg)
{
	if (dyndbg->num_descs)
		ddebug_remove_module(mod->name);
}

void * __weak module_alloc(unsigned long size)
{
	return __vmalloc_node_range(size, 1, VMALLOC_START, VMALLOC_END,
			GFP_KERNEL, PAGE_KERNEL_EXEC, VM_FLUSH_RESET_PERMS,
			NUMA_NO_NODE, __builtin_return_address(0));
}

bool __weak module_init_section(const char *name)
{
	return strstarts(name, ".init");
}

bool __weak module_exit_section(const char *name)
{
	return strstarts(name, ".exit");
}

static int validate_section_offset(struct load_info *info, Elf_Shdr *shdr)
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

/*
 * Sanity checks against invalid binaries, wrong arch, weird elf version.
 *
 * Also do basic validity checks against section offsets and sizes, the
 * section name string table, and the indices used for it (sh_name).
 */
static int elf_validity_check(struct load_info *info)
{
	unsigned int i;
	Elf_Shdr *shdr, *strhdr;
	int err;

	if (info->len < sizeof(*(info->hdr))) {
		pr_err("Invalid ELF header len %lu\n", info->len);
		goto no_exec;
	}

	if (memcmp(info->hdr->e_ident, ELFMAG, SELFMAG) != 0) {
		pr_err("Invalid ELF header magic: != %s\n", ELFMAG);
		goto no_exec;
	}
	if (info->hdr->e_type != ET_REL) {
		pr_err("Invalid ELF header type: %u != %u\n",
		       info->hdr->e_type, ET_REL);
		goto no_exec;
	}
	if (!elf_check_arch(info->hdr)) {
		pr_err("Invalid architecture in ELF header: %u\n",
		       info->hdr->e_machine);
		goto no_exec;
	}
	if (info->hdr->e_shentsize != sizeof(Elf_Shdr)) {
		pr_err("Invalid ELF section header size\n");
		goto no_exec;
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
		goto no_exec;
	}

	info->sechdrs = (void *)info->hdr + info->hdr->e_shoff;

	/*
	 * Verify if the section name table index is valid.
	 */
	if (info->hdr->e_shstrndx == SHN_UNDEF
	    || info->hdr->e_shstrndx >= info->hdr->e_shnum) {
		pr_err("Invalid ELF section name index: %d || e_shstrndx (%d) >= e_shnum (%d)\n",
		       info->hdr->e_shstrndx, info->hdr->e_shstrndx,
		       info->hdr->e_shnum);
		goto no_exec;
	}

	strhdr = &info->sechdrs[info->hdr->e_shstrndx];
	err = validate_section_offset(info, strhdr);
	if (err < 0) {
		pr_err("Invalid ELF section hdr(type %u)\n", strhdr->sh_type);
		return err;
	}

	/*
	 * The section name table must be NUL-terminated, as required
	 * by the spec. This makes strcmp and pr_* calls that access
	 * strings in the section safe.
	 */
	info->secstrings = (void *)info->hdr + strhdr->sh_offset;
	if (strhdr->sh_size == 0) {
		pr_err("empty section name table\n");
		goto no_exec;
	}
	if (info->secstrings[strhdr->sh_size - 1] != '\0') {
		pr_err("ELF Spec violation: section name table isn't null terminated\n");
		goto no_exec;
	}

	/*
	 * The code assumes that section 0 has a length of zero and
	 * an addr of zero, so check for it.
	 */
	if (info->sechdrs[0].sh_type != SHT_NULL
	    || info->sechdrs[0].sh_size != 0
	    || info->sechdrs[0].sh_addr != 0) {
		pr_err("ELF Spec violation: section 0 type(%d)!=SH_NULL or non-zero len or addr\n",
		       info->sechdrs[0].sh_type);
		goto no_exec;
	}

	for (i = 1; i < info->hdr->e_shnum; i++) {
		shdr = &info->sechdrs[i];
		switch (shdr->sh_type) {
		case SHT_NULL:
		case SHT_NOBITS:
			continue;
		case SHT_SYMTAB:
			if (shdr->sh_link == SHN_UNDEF
			    || shdr->sh_link >= info->hdr->e_shnum) {
				pr_err("Invalid ELF sh_link!=SHN_UNDEF(%d) or (sh_link(%d) >= hdr->e_shnum(%d)\n",
				       shdr->sh_link, shdr->sh_link,
				       info->hdr->e_shnum);
				goto no_exec;
			}
			fallthrough;
		default:
			err = validate_section_offset(info, shdr);
			if (err < 0) {
				pr_err("Invalid ELF section in module (section %u type %u)\n",
					i, shdr->sh_type);
				return err;
			}

			if (shdr->sh_flags & SHF_ALLOC) {
				if (shdr->sh_name >= strhdr->sh_size) {
					pr_err("Invalid ELF section name in module (section %u type %u)\n",
					       i, shdr->sh_type);
					return -ENOEXEC;
				}
			}
			break;
		}
	}

	return 0;

no_exec:
	return -ENOEXEC;
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

	if (set_livepatch_module(mod)) {
		add_taint_module(mod, TAINT_LIVEPATCH, LOCKDEP_STILL_OK);
		pr_notice_once("%s: tainting kernel with TAINT_LIVEPATCH\n",
				mod->name);
		return 0;
	}

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
	info->sechdrs[info->index.info].sh_flags &= ~(unsigned long)SHF_ALLOC;

	return 0;
}

/*
 * Set up our basic convenience variables (pointers to section headers,
 * search for module section index etc), and do some basic section
 * verification.
 *
 * Set info->mod to the temporary copy of the module in info->hdr. The final one
 * will be allocated in move_module().
 */
static int setup_load_info(struct load_info *info, int flags)
{
	unsigned int i;

	/* Try to find a name early so we can log errors with a module name */
	info->index.info = find_sec(info, ".modinfo");
	if (info->index.info)
		info->name = get_modinfo(info, "name");

	/* Find internal symbols and strings. */
	for (i = 1; i < info->hdr->e_shnum; i++) {
		if (info->sechdrs[i].sh_type == SHT_SYMTAB) {
			info->index.sym = i;
			info->index.str = info->sechdrs[i].sh_link;
			info->strtab = (char *)info->hdr
				+ info->sechdrs[info->index.str].sh_offset;
			break;
		}
	}

	if (info->index.sym == 0) {
		pr_warn("%s: module has no symbols (stripped?)\n",
			info->name ?: "(missing .modinfo section or name field)");
		return -ENOEXEC;
	}

	info->index.mod = find_sec(info, ".gnu.linkonce.this_module");
	if (!info->index.mod) {
		pr_warn("%s: No module found in object\n",
			info->name ?: "(missing .modinfo section or name field)");
		return -ENOEXEC;
	}
	/* This is temporary: point mod into copy of data. */
	info->mod = (void *)info->hdr + info->sechdrs[info->index.mod].sh_offset;

	/*
	 * If we didn't load the .modinfo 'name' field earlier, fall back to
	 * on-disk struct mod 'name' field.
	 */
	if (!info->name)
		info->name = info->mod->name;

	if (flags & MODULE_INIT_IGNORE_MODVERSIONS)
		info->index.vers = 0; /* Pretend no __versions section! */
	else
		info->index.vers = find_sec(info, "__versions");

	info->index.pcpu = find_pcpusec(info);

	return 0;
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

	err = check_modinfo_livepatch(mod, info);
	if (err)
		return err;

	/* Set up license info based on the info section */
	set_license(mod, get_modinfo(info, "license"));

	if (get_modinfo(info, "test")) {
		if (!test_taint(TAINT_TEST))
			pr_warn("%s: loading test module taints kernel.\n",
				mod->name);
		add_taint_module(mod, TAINT_TEST, LOCKDEP_STILL_OK);
	}

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
#endif

	mod->extable = section_objs(info, "__ex_table",
				    sizeof(*mod->extable), &mod->num_exentries);

	if (section_addr(info, "__obsparm"))
		pr_warn("%s: Ignoring obsolete parameters\n", mod->name);

	info->dyndbg.descs = section_objs(info, "__dyndbg",
					sizeof(*info->dyndbg.descs), &info->dyndbg.num_descs);
	info->dyndbg.classes = section_objs(info, "__dyndbg_classes",
					sizeof(*info->dyndbg.classes), &info->dyndbg.num_classes);

	return 0;
}

static int move_module(struct module *mod, struct load_info *info)
{
	int i;
	void *ptr;

	/* Do the allocs. */
	ptr = module_alloc(mod->core_layout.size);
	/*
	 * The pointer to this block is stored in the module structure
	 * which is inside the block. Just mark it as not being a
	 * leak.
	 */
	kmemleak_not_leak(ptr);
	if (!ptr)
		return -ENOMEM;

	memset(ptr, 0, mod->core_layout.size);
	mod->core_layout.base = ptr;

	if (mod->init_layout.size) {
		ptr = module_alloc(mod->init_layout.size);
		/*
		 * The pointer to this block is stored in the module structure
		 * which is inside the block. This block doesn't need to be
		 * scanned as it contains data and code that will be freed
		 * after the module is initialized.
		 */
		kmemleak_ignore(ptr);
		if (!ptr) {
			module_memfree(mod->core_layout.base);
			return -ENOMEM;
		}
		memset(ptr, 0, mod->init_layout.size);
		mod->init_layout.base = ptr;
	} else
		mod->init_layout.base = NULL;

#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
	/* Do the allocs. */
	ptr = vzalloc(mod->data_layout.size);
	/*
	 * The pointer to this block is stored in the module structure
	 * which is inside the block. Just mark it as not being a
	 * leak.
	 */
	kmemleak_not_leak(ptr);
	if (!ptr) {
		module_memfree(mod->core_layout.base);
		module_memfree(mod->init_layout.base);
		return -ENOMEM;
	}

	mod->data_layout.base = ptr;
#endif
	/* Transfer each section which specifies SHF_ALLOC */
	pr_debug("final section addresses:\n");
	for (i = 0; i < info->hdr->e_shnum; i++) {
		void *dest;
		Elf_Shdr *shdr = &info->sechdrs[i];

		if (!(shdr->sh_flags & SHF_ALLOC))
			continue;

		if (shdr->sh_entsize & INIT_OFFSET_MASK)
			dest = mod->init_layout.base
				+ (shdr->sh_entsize & ~INIT_OFFSET_MASK);
		else if (!(shdr->sh_flags & SHF_EXECINSTR))
			dest = mod->data_layout.base + shdr->sh_entsize;
		else
			dest = mod->core_layout.base + shdr->sh_entsize;

		if (shdr->sh_type != SHT_NOBITS)
			memcpy(dest, (void *)shdr->sh_addr, shdr->sh_size);
		/* Update sh_addr to point to copy in image. */
		shdr->sh_addr = (unsigned long)dest;
		pr_debug("\t0x%lx %s\n",
			 (long)shdr->sh_addr, info->secstrings + shdr->sh_name);
	}

	return 0;
}

static int check_module_license_and_versions(struct module *mod)
{
	int prev_taint = test_taint(TAINT_PROPRIETARY_MODULE);

	/*
	 * ndiswrapper is under GPL by itself, but loads proprietary modules.
	 * Don't use add_taint_module(), as it would prevent ndiswrapper from
	 * using GPL-only symbols it needs.
	 */
	if (strcmp(mod->name, "ndiswrapper") == 0)
		add_taint(TAINT_PROPRIETARY_MODULE, LOCKDEP_NOW_UNRELIABLE);

	/* driverloader was caught wrongly pretending to be under GPL */
	if (strcmp(mod->name, "driverloader") == 0)
		add_taint_module(mod, TAINT_PROPRIETARY_MODULE,
				 LOCKDEP_NOW_UNRELIABLE);

	/* lve claims to be GPL but upstream won't provide source */
	if (strcmp(mod->name, "lve") == 0)
		add_taint_module(mod, TAINT_PROPRIETARY_MODULE,
				 LOCKDEP_NOW_UNRELIABLE);

	if (!prev_taint && test_taint(TAINT_PROPRIETARY_MODULE))
		pr_warn("%s: module license taints kernel.\n", mod->name);

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
	if (mod->init_layout.base)
		flush_icache_range((unsigned long)mod->init_layout.base,
				   (unsigned long)mod->init_layout.base
				   + mod->init_layout.size);
	flush_icache_range((unsigned long)mod->core_layout.base,
			   (unsigned long)mod->core_layout.base + mod->core_layout.size);
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
	unsigned int ndx;
	int err;

	err = check_modinfo(info->mod, info, flags);
	if (err)
		return ERR_PTR(err);

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
	 * Mark ro_after_init section with SHF_RO_AFTER_INIT so that
	 * layout_sections() can put it in the right place.
	 * Note: ro_after_init sections also have SHF_{WRITE,ALLOC} set.
	 */
	ndx = find_sec(info, ".data..ro_after_init");
	if (ndx)
		info->sechdrs[ndx].sh_flags |= SHF_RO_AFTER_INIT;
	/*
	 * Mark the __jump_table section as ro_after_init as well: these data
	 * structures are never modified, with the exception of entries that
	 * refer to code in the __init section, which are annotated as such
	 * at module load time.
	 */
	ndx = find_sec(info, "__jump_table");
	if (ndx)
		info->sechdrs[ndx].sh_flags |= SHF_RO_AFTER_INIT;

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
	return mod;
}

/* mod is no longer valid after this! */
static void module_deallocate(struct module *mod, struct load_info *info)
{
	percpu_modfree(mod);
	module_arch_freeing_init(mod);
	module_memfree(mod->init_layout.base);
	module_memfree(mod->core_layout.base);
#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
	vfree(mod->data_layout.base);
#endif
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
	void *module_init;
};

static void do_free_init(struct work_struct *w)
{
	struct llist_node *pos, *n, *list;
	struct mod_initfree *initfree;

	list = llist_del_all(&init_free_list);

	synchronize_rcu();

	llist_for_each_safe(pos, n, list) {
		initfree = container_of(pos, struct mod_initfree, node);
		module_memfree(initfree->module_init);
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

	freeinit = kmalloc(sizeof(*freeinit), GFP_KERNEL);
	if (!freeinit) {
		ret = -ENOMEM;
		goto fail;
	}
	freeinit->module_init = mod->init_layout.base;

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

	ftrace_free_mem(mod, mod->init_layout.base, mod->init_layout.base +
			mod->init_layout.size);
	mutex_lock(&module_mutex);
	/* Drop initial reference. */
	module_put(mod);
	trim_init_extable(mod);
#ifdef CONFIG_KALLSYMS
	/* Switch to core kallsyms now init is done: kallsyms may be walking! */
	rcu_assign_pointer(mod->kallsyms, &mod->core_kallsyms);
#endif
	module_enable_ro(mod, true);
	mod_tree_remove_init(mod);
	module_arch_freeing_init(mod);
	mod->init_layout.base = NULL;
	mod->init_layout.size = 0;
	mod->init_layout.ro_size = 0;
	mod->init_layout.ro_after_init_size = 0;
	mod->init_layout.text_size = 0;
#ifdef CONFIG_DEBUG_INFO_BTF_MODULES
	/* .BTF is not SHF_ALLOC and will get removed, so sanitize pointer */
	mod->btf_data = NULL;
#endif
	/*
	 * We want to free module_init, but be aware that kallsyms may be
	 * walking this with preempt disabled.  In all the failure paths, we
	 * call synchronize_rcu(), but we don't want to slow down the success
	 * path. module_memfree() cannot be called in an interrupt, so do the
	 * work and call synchronize_rcu() in a work queue.
	 *
	 * Note that module_alloc() on most architectures creates W+X page
	 * mappings which won't be cleaned up until do_free_init() runs.  Any
	 * code such as mark_rodata_ro() which depends on those mappings to
	 * be cleaned up needs to sync with the queued work by invoking
	 * flush_module_init_free_work().
	 */
	if (llist_add(&freeinit->node, &init_free_list))
		schedule_work(&init_free_wq);

	mutex_unlock(&module_mutex);
	wake_up_all(&module_wq);

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

/*
 * We try to place it in the list now to make sure it's unique before
 * we dedicate too many resources.  In particular, temporary percpu
 * memory exhaustion.
 */
static int add_unformed_module(struct module *mod)
{
	int err;
	struct module *old;

	mod->state = MODULE_STATE_UNFORMED;

	mutex_lock(&module_mutex);
	old = find_module_all(mod->name, strlen(mod->name), true);
	if (old != NULL) {
		if (old->state == MODULE_STATE_COMING
		    || old->state == MODULE_STATE_UNFORMED) {
			/* Wait in case it fails to load. */
			mutex_unlock(&module_mutex);
			err = wait_event_interruptible(module_wq,
					       finished_loading(mod->name));
			if (err)
				goto out_unlocked;

			/* The module might have gone in the meantime. */
			mutex_lock(&module_mutex);
			old = find_module_all(mod->name, strlen(mod->name),
					      true);
		}

		/*
		 * We are here only when the same module was being loaded. Do
		 * not try to load it again right now. It prevents long delays
		 * caused by serialized module load failures. It might happen
		 * when more devices of the same type trigger load of
		 * a particular module.
		 */
		if (old && old->state == MODULE_STATE_LIVE)
			err = -EEXIST;
		else
			err = -EBUSY;
		goto out;
	}
	mod_update_bounds(mod);
	list_add_rcu(&mod->list, &modules);
	mod_tree_insert(mod);
	err = 0;

out:
	mutex_unlock(&module_mutex);
out_unlocked:
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

	if (module_check_misalignment(mod))
		goto out_misaligned;

	module_enable_ro(mod, false);
	module_enable_nx(mod);
	module_enable_x(mod);

	/*
	 * Mark state as coming so strong_try_module_get() ignores us,
	 * but kallsyms etc. can see us.
	 */
	mod->state = MODULE_STATE_COMING;
	mutex_unlock(&module_mutex);

	return 0;

out_misaligned:
	err = -EINVAL;
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
		if (strtobool(val, &mod->async_probe_requested))
			mod->async_probe_requested = true;
		return 0;
	}

	/* Check for magic 'dyndbg' arg */
	ret = ddebug_dyndbg_module_param_cb(param, val, modname);
	if (ret != 0)
		pr_warn("%s: unknown parameter '%s' ignored\n", modname, param);
	return 0;
}

/*
 * Allocate and load the module: note that size of section 0 is always
 * zero, and we rely on this for optional sections.
 */
static int load_module(struct load_info *info, const char __user *uargs,
		       int flags)
{
	struct module *mod;
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
	 * sections.
	 */
	err = elf_validity_check(info);
	if (err)
		goto free_copy;

	/*
	 * Everything checks out, so set up the section info
	 * in the info structure.
	 */
	err = setup_load_info(info, flags);
	if (err)
		goto free_copy;

	/*
	 * Now that we know we have the correct module name, check
	 * if it's blacklisted.
	 */
	if (blacklisted(info->name)) {
		err = -EPERM;
		pr_err("Module %s is blacklisted\n", info->name);
		goto free_copy;
	}

	err = rewrite_section_headers(info, flags);
	if (err)
		goto free_copy;

	/* Check module struct version now, before we try to use module. */
	if (!check_modstruct_version(info, info->mod)) {
		err = -ENOEXEC;
		goto free_copy;
	}

	/* Figure out module layout, and allocate all the memory. */
	mod = layout_and_allocate(info, flags);
	if (IS_ERR(mod)) {
		err = PTR_ERR(mod);
		goto free_copy;
	}

	audit_log_kern_module(mod->name);

	/* Reserve our place in the list. */
	err = add_unformed_module(mod);
	if (err)
		goto free_module;

#ifdef CONFIG_MODULE_SIG
	mod->sig_ok = info->sig_ok;
	if (!mod->sig_ok) {
		pr_notice_once("%s: module verification failed: signature "
			       "and/or required key missing - tainting "
			       "kernel\n", mod->name);
		add_taint_module(mod, TAINT_UNSIGNED_MODULE, LOCKDEP_STILL_OK);
	}
#endif

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

	err = check_module_license_and_versions(mod);
	if (err)
		goto free_unload;

	/* Set up MODINFO_ATTR fields */
	setup_modinfo(mod, info);

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
	dynamic_debug_setup(mod, &info->dyndbg);

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
	dynamic_debug_remove(mod, &info->dyndbg);
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
	/* Free lock-classes; relies on the preceding sync_rcu() */
	lockdep_free_key_range(mod->data_layout.base, mod->data_layout.size);

	module_deallocate(mod, info);
 free_copy:
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
	if (err)
		return err;

	return load_module(&info, uargs, 0);
}

SYSCALL_DEFINE3(finit_module, int, fd, const char __user *, uargs, int, flags)
{
	struct load_info info = { };
	void *buf = NULL;
	int len;
	int err;

	err = may_init_module();
	if (err)
		return err;

	pr_debug("finit_module: fd=%d, uargs=%p, flags=%i\n", fd, uargs, flags);

	if (flags & ~(MODULE_INIT_IGNORE_MODVERSIONS
		      |MODULE_INIT_IGNORE_VERMAGIC
		      |MODULE_INIT_COMPRESSED_FILE))
		return -EINVAL;

	len = kernel_read_file_from_fd(fd, 0, &buf, INT_MAX, NULL,
				       READING_MODULE);
	if (len < 0)
		return len;

	if (flags & MODULE_INIT_COMPRESSED_FILE) {
		err = module_decompress(&info, buf, len);
		vfree(buf); /* compressed data is no longer needed */
		if (err)
			return err;
	} else {
		info.hdr = buf;
		info.len = len;
	}

	return load_module(&info, uargs, flags);
}

static inline int within(unsigned long addr, void *start, unsigned long size)
{
	return ((void *)addr >= start && (void *)addr < start + size);
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
	const struct exception_table_entry *e = NULL;
	struct module *mod;

	preempt_disable();
	mod = __module_address(addr);
	if (!mod)
		goto out;

	if (!mod->num_exentries)
		goto out;

	e = search_extable(mod->extable,
			   mod->num_exentries,
			   addr);
out:
	preempt_enable();

	/*
	 * Now, if we found one, we are running inside it now, hence
	 * we cannot unload the module, hence no refcnt needed.
	 */
	return e;
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
	bool ret;

	preempt_disable();
	ret = __module_address(addr) != NULL;
	preempt_enable();

	return ret;
}

/**
 * __module_address() - get the module which contains an address.
 * @addr: the address.
 *
 * Must be called with preempt disabled or module mutex held so that
 * module doesn't get freed during this.
 */
struct module *__module_address(unsigned long addr)
{
	struct module *mod;
	struct mod_tree_root *tree;

	if (addr >= mod_tree.addr_min && addr <= mod_tree.addr_max)
		tree = &mod_tree;
#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
	else if (addr >= mod_data_tree.addr_min && addr <= mod_data_tree.addr_max)
		tree = &mod_data_tree;
#endif
	else
		return NULL;

	module_assert_mutex_or_preempt();

	mod = mod_find(addr, tree);
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
	bool ret;

	preempt_disable();
	ret = __module_text_address(addr) != NULL;
	preempt_enable();

	return ret;
}

/**
 * __module_text_address() - get the module whose code contains an address.
 * @addr: the address.
 *
 * Must be called with preempt disabled or module mutex held so that
 * module doesn't get freed during this.
 */
struct module *__module_text_address(unsigned long addr)
{
	struct module *mod = __module_address(addr);
	if (mod) {
		/* Make sure it's within the text section. */
		if (!within(addr, mod->init_layout.base, mod->init_layout.text_size)
		    && !within(addr, mod->core_layout.base, mod->core_layout.text_size))
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
	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (mod->state == MODULE_STATE_UNFORMED)
			continue;
		pr_cont(" %s%s", mod->name, module_flags(mod, buf, true));
	}

	print_unloaded_tainted_modules();
	preempt_enable();
	if (last_unloaded_module.name[0])
		pr_cont(" [last unloaded: %s%s]", last_unloaded_module.name,
			last_unloaded_module.taints);
	pr_cont("\n");
}
