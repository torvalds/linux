// SPDX-License-Identifier: GPL-2.0-or-later
/*
   Copyright (C) 2002 Richard Henderson
   Copyright (C) 2001 Rusty Russell, 2002, 2010 Rusty Russell IBM.

*/

#define INCLUDE_VERMAGIC

#include <linux/export.h>
#include <linux/extable.h>
#include <linux/moduleloader.h>
#include <linux/module_signature.h>
#include <linux/trace_events.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/kernel.h>
#include <linux/kernel_read_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/elf.h>
#include <linux/proc_fs.h>
#include <linux/security.h>
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
#include <uapi/linux/module.h>
#include "module-internal.h"

#define CREATE_TRACE_POINTS
#include <trace/events/module.h>

#undef CREATE_TRACE_POINTS
#include <trace/hooks/module.h>
#include <trace/hooks/memory.h>

#ifndef ARCH_SHF_SMALL
#define ARCH_SHF_SMALL 0
#endif

/*
 * Modules' sections will be aligned on page boundaries
 * to ensure complete separation of code and data, but
 * only when CONFIG_ARCH_HAS_STRICT_MODULE_RWX=y
 */
#ifdef CONFIG_ARCH_HAS_STRICT_MODULE_RWX
# define debug_align(X) ALIGN(X, PAGE_SIZE)
#else
# define debug_align(X) (X)
#endif

/* If this is set, the section belongs in the init part of the module */
#define INIT_OFFSET_MASK (1UL << (BITS_PER_LONG-1))

/*
 * Mutex protects:
 * 1) List of modules (also safely readable with preempt_disable),
 * 2) module_use links,
 * 3) module_addr_min/module_addr_max.
 * (delete and add uses RCU list operations). */
DEFINE_MUTEX(module_mutex);
EXPORT_SYMBOL_GPL(module_mutex);
static LIST_HEAD(modules);

/* Work queue for freeing init sections in success case */
static void do_free_init(struct work_struct *w);
static DECLARE_WORK(init_free_wq, do_free_init);
static LLIST_HEAD(init_free_list);

#ifdef CONFIG_MODULES_TREE_LOOKUP

/*
 * Use a latched RB-tree for __module_address(); this allows us to use
 * RCU-sched lookups of the address from any context.
 *
 * This is conditional on PERF_EVENTS || TRACING because those can really hit
 * __module_address() hard by doing a lot of stack unwinding; potentially from
 * NMI context.
 */

static __always_inline unsigned long __mod_tree_val(struct latch_tree_node *n)
{
	struct module_layout *layout = container_of(n, struct module_layout, mtn.node);

	return (unsigned long)layout->base;
}

static __always_inline unsigned long __mod_tree_size(struct latch_tree_node *n)
{
	struct module_layout *layout = container_of(n, struct module_layout, mtn.node);

	return (unsigned long)layout->size;
}

static __always_inline bool
mod_tree_less(struct latch_tree_node *a, struct latch_tree_node *b)
{
	return __mod_tree_val(a) < __mod_tree_val(b);
}

static __always_inline int
mod_tree_comp(void *key, struct latch_tree_node *n)
{
	unsigned long val = (unsigned long)key;
	unsigned long start, end;

	start = __mod_tree_val(n);
	if (val < start)
		return -1;

	end = start + __mod_tree_size(n);
	if (val >= end)
		return 1;

	return 0;
}

static const struct latch_tree_ops mod_tree_ops = {
	.less = mod_tree_less,
	.comp = mod_tree_comp,
};

static struct mod_tree_root {
	struct latch_tree_root root;
	unsigned long addr_min;
	unsigned long addr_max;
} mod_tree __cacheline_aligned = {
	.addr_min = -1UL,
};

#define module_addr_min mod_tree.addr_min
#define module_addr_max mod_tree.addr_max

static noinline void __mod_tree_insert(struct mod_tree_node *node)
{
	latch_tree_insert(&node->node, &mod_tree.root, &mod_tree_ops);
}

static void __mod_tree_remove(struct mod_tree_node *node)
{
	latch_tree_erase(&node->node, &mod_tree.root, &mod_tree_ops);
}

/*
 * These modifications: insert, remove_init and remove; are serialized by the
 * module_mutex.
 */
static void mod_tree_insert(struct module *mod)
{
	mod->core_layout.mtn.mod = mod;
	mod->init_layout.mtn.mod = mod;

	__mod_tree_insert(&mod->core_layout.mtn);
	if (mod->init_layout.size)
		__mod_tree_insert(&mod->init_layout.mtn);
}

static void mod_tree_remove_init(struct module *mod)
{
	if (mod->init_layout.size)
		__mod_tree_remove(&mod->init_layout.mtn);
}

static void mod_tree_remove(struct module *mod)
{
	__mod_tree_remove(&mod->core_layout.mtn);
	mod_tree_remove_init(mod);
}

static struct module *mod_find(unsigned long addr)
{
	struct latch_tree_node *ltn;

	ltn = latch_tree_find((void *)addr, &mod_tree.root, &mod_tree_ops);
	if (!ltn)
		return NULL;

	return container_of(ltn, struct mod_tree_node, node)->mod;
}

#else /* MODULES_TREE_LOOKUP */

static unsigned long module_addr_min = -1UL, module_addr_max = 0;

static void mod_tree_insert(struct module *mod) { }
static void mod_tree_remove_init(struct module *mod) { }
static void mod_tree_remove(struct module *mod) { }

static struct module *mod_find(unsigned long addr)
{
	struct module *mod;

	list_for_each_entry_rcu(mod, &modules, list,
				lockdep_is_held(&module_mutex)) {
		if (within_module(addr, mod))
			return mod;
	}

	return NULL;
}

#endif /* MODULES_TREE_LOOKUP */

/*
 * Bounds of module text, for speeding up __module_address.
 * Protected by module_mutex.
 */
static void __mod_update_bounds(void *base, unsigned int size)
{
	unsigned long min = (unsigned long)base;
	unsigned long max = min + size;

	if (min < module_addr_min)
		module_addr_min = min;
	if (max > module_addr_max)
		module_addr_max = max;
}

static void mod_update_bounds(struct module *mod)
{
	__mod_update_bounds(mod->core_layout.base, mod->core_layout.size);
	if (mod->init_layout.size)
		__mod_update_bounds(mod->init_layout.base, mod->init_layout.size);
}

#ifdef CONFIG_KGDB_KDB
struct list_head *kdb_modules = &modules; /* kdb needs the list of modules */
#endif /* CONFIG_KGDB_KDB */

static void module_assert_mutex(void)
{
	lockdep_assert_held(&module_mutex);
}

static void module_assert_mutex_or_preempt(void)
{
#ifdef CONFIG_LOCKDEP
	if (unlikely(!debug_locks))
		return;

	WARN_ON_ONCE(!rcu_read_lock_sched_held() &&
		!lockdep_is_held(&module_mutex));
#endif
}

static bool sig_enforce = IS_ENABLED(CONFIG_MODULE_SIG_FORCE);
module_param(sig_enforce, bool_enable_only, 0644);

/*
 * Export sig_enforce kernel cmdline parameter to allow other subsystems rely
 * on that instead of directly to CONFIG_MODULE_SIG_FORCE config.
 */
bool is_module_sig_enforced(void)
{
	return sig_enforce;
}
EXPORT_SYMBOL(is_module_sig_enforced);

void set_module_sig_enforced(void)
{
	sig_enforce = true;
}

/* Block module loading/unloading? */
int modules_disabled = 0;
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
 * is running can call this to safely exit.  nfsd and lockd use this.
 */
void __noreturn __module_put_and_exit(struct module *mod, long code)
{
	module_put(mod);
	do_exit(code);
}
EXPORT_SYMBOL(__module_put_and_exit);

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

/* Provided by the linker */
extern const struct kernel_symbol __start___ksymtab[];
extern const struct kernel_symbol __stop___ksymtab[];
extern const struct kernel_symbol __start___ksymtab_gpl[];
extern const struct kernel_symbol __stop___ksymtab_gpl[];
extern const struct kernel_symbol __start___ksymtab_gpl_future[];
extern const struct kernel_symbol __stop___ksymtab_gpl_future[];
extern const s32 __start___kcrctab[];
extern const s32 __start___kcrctab_gpl[];
extern const s32 __start___kcrctab_gpl_future[];
#ifdef CONFIG_UNUSED_SYMBOLS
extern const struct kernel_symbol __start___ksymtab_unused[];
extern const struct kernel_symbol __stop___ksymtab_unused[];
extern const struct kernel_symbol __start___ksymtab_unused_gpl[];
extern const struct kernel_symbol __stop___ksymtab_unused_gpl[];
extern const s32 __start___kcrctab_unused[];
extern const s32 __start___kcrctab_unused_gpl[];
#endif

#ifndef CONFIG_MODVERSIONS
#define symversion(base, idx) NULL
#else
#define symversion(base, idx) ((base != NULL) ? ((base) + (idx)) : NULL)
#endif

static bool each_symbol_in_section(const struct symsearch *arr,
				   unsigned int arrsize,
				   struct module *owner,
				   bool (*fn)(const struct symsearch *syms,
					      struct module *owner,
					      void *data),
				   void *data)
{
	unsigned int j;

	for (j = 0; j < arrsize; j++) {
		if (fn(&arr[j], owner, data))
			return true;
	}

	return false;
}

/* Returns true as soon as fn returns true, otherwise false. */
static bool each_symbol_section(bool (*fn)(const struct symsearch *arr,
				    struct module *owner,
				    void *data),
			 void *data)
{
	struct module *mod;
	static const struct symsearch arr[] = {
		{ __start___ksymtab, __stop___ksymtab, __start___kcrctab,
		  NOT_GPL_ONLY, false },
		{ __start___ksymtab_gpl, __stop___ksymtab_gpl,
		  __start___kcrctab_gpl,
		  GPL_ONLY, false },
		{ __start___ksymtab_gpl_future, __stop___ksymtab_gpl_future,
		  __start___kcrctab_gpl_future,
		  WILL_BE_GPL_ONLY, false },
#ifdef CONFIG_UNUSED_SYMBOLS
		{ __start___ksymtab_unused, __stop___ksymtab_unused,
		  __start___kcrctab_unused,
		  NOT_GPL_ONLY, true },
		{ __start___ksymtab_unused_gpl, __stop___ksymtab_unused_gpl,
		  __start___kcrctab_unused_gpl,
		  GPL_ONLY, true },
#endif
	};

	module_assert_mutex_or_preempt();

	if (each_symbol_in_section(arr, ARRAY_SIZE(arr), NULL, fn, data))
		return true;

	list_for_each_entry_rcu(mod, &modules, list,
				lockdep_is_held(&module_mutex)) {
		struct symsearch arr[] = {
			{ mod->syms, mod->syms + mod->num_syms, mod->crcs,
			  NOT_GPL_ONLY, false },
			{ mod->gpl_syms, mod->gpl_syms + mod->num_gpl_syms,
			  mod->gpl_crcs,
			  GPL_ONLY, false },
			{ mod->gpl_future_syms,
			  mod->gpl_future_syms + mod->num_gpl_future_syms,
			  mod->gpl_future_crcs,
			  WILL_BE_GPL_ONLY, false },
#ifdef CONFIG_UNUSED_SYMBOLS
			{ mod->unused_syms,
			  mod->unused_syms + mod->num_unused_syms,
			  mod->unused_crcs,
			  NOT_GPL_ONLY, true },
			{ mod->unused_gpl_syms,
			  mod->unused_gpl_syms + mod->num_unused_gpl_syms,
			  mod->unused_gpl_crcs,
			  GPL_ONLY, true },
#endif
		};

		if (mod->state == MODULE_STATE_UNFORMED)
			continue;

		if (each_symbol_in_section(arr, ARRAY_SIZE(arr), mod, fn, data))
			return true;
	}
	return false;
}

struct find_symbol_arg {
	/* Input */
	const char *name;
	bool gplok;
	bool warn;

	/* Output */
	struct module *owner;
	const s32 *crc;
	const struct kernel_symbol *sym;
	enum mod_license license;
};

static bool check_exported_symbol(const struct symsearch *syms,
				  struct module *owner,
				  unsigned int symnum, void *data)
{
	struct find_symbol_arg *fsa = data;

	if (!fsa->gplok) {
		if (syms->license == GPL_ONLY)
			return false;
		if (syms->license == WILL_BE_GPL_ONLY && fsa->warn) {
			pr_warn("Symbol %s is being used by a non-GPL module, "
				"which will not be allowed in the future\n",
				fsa->name);
		}
	}

#ifdef CONFIG_UNUSED_SYMBOLS
	if (syms->unused && fsa->warn) {
		pr_warn("Symbol %s is marked as UNUSED, however this module is "
			"using it.\n", fsa->name);
		pr_warn("This symbol will go away in the future.\n");
		pr_warn("Please evaluate if this is the right api to use and "
			"if it really is, submit a report to the linux kernel "
			"mailing list together with submitting your code for "
			"inclusion.\n");
	}
#endif

	fsa->owner = owner;
	fsa->crc = symversion(syms->crcs, symnum);
	fsa->sym = &syms->start[symnum];
	fsa->license = syms->license;
	return true;
}

static unsigned long kernel_symbol_value(const struct kernel_symbol *sym)
{
#ifdef CONFIG_HAVE_ARCH_PREL32_RELOCATIONS
	return (unsigned long)offset_to_ptr(&sym->value_offset);
#else
	return sym->value;
#endif
}

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

static int cmp_name(const void *name, const void *sym)
{
	return strcmp(name, kernel_symbol_name(sym));
}

static bool find_exported_symbol_in_section(const struct symsearch *syms,
					    struct module *owner,
					    void *data)
{
	struct find_symbol_arg *fsa = data;
	struct kernel_symbol *sym;

	sym = bsearch(fsa->name, syms->start, syms->stop - syms->start,
			sizeof(struct kernel_symbol), cmp_name);

	if (sym != NULL && check_exported_symbol(syms, owner,
						 sym - syms->start, data))
		return true;

	return false;
}

/* Find an exported symbol and return it, along with, (optional) crc and
 * (optional) module which owns it.  Needs preempt disabled or module_mutex. */
static const struct kernel_symbol *find_symbol(const char *name,
					struct module **owner,
					const s32 **crc,
					enum mod_license *license,
					bool gplok,
					bool warn)
{
	struct find_symbol_arg fsa;

	fsa.name = name;
	fsa.gplok = gplok;
	fsa.warn = warn;

	if (each_symbol_section(find_exported_symbol_in_section, &fsa)) {
		if (owner)
			*owner = fsa.owner;
		if (crc)
			*crc = fsa.crc;
		if (license)
			*license = fsa.license;
		return fsa.sym;
	}

	pr_debug("Failed to find symbol %s\n", name);
	return NULL;
}

/*
 * Search for module by name: must hold module_mutex (or preempt disabled
 * for read-only access).
 */
static struct module *find_module_all(const char *name, size_t len,
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
	module_assert_mutex();
	return find_module_all(name, strlen(name), false);
}
EXPORT_SYMBOL_GPL(find_module);

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
 * is_module_percpu_address - test whether address is from module static percpu
 * @addr: address to test
 *
 * Test whether @addr belongs to module static percpu area.
 *
 * RETURNS:
 * %true if @addr is from module static percpu area
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
MODINFO_ATTR(scmversion);

static char last_unloaded_module[MODULE_NAME_LEN+1];

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
 * module_refcount - return the refcount or -1 if unloading
 *
 * @mod:	the module we're checking
 *
 * Returns:
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

	/* Stop the machine so refcounts can't move and disable module. */
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

	/* Store the name of the last unloaded module for diagnostic purposes */
	strlcpy(last_unloaded_module, mod->name, sizeof(last_unloaded_module));

	free_module(mod);
	/* someone could wait for the module in add_unformed_module() */
	wake_up_all(&module_wq);
	return 0;
out:
	mutex_unlock(&module_mutex);
	return ret;
}

static inline void print_unload_info(struct seq_file *m, struct module *mod)
{
	struct module_use *use;
	int printed_something = 0;

	seq_printf(m, " %i ", module_refcount(mod));

	/*
	 * Always include a trailing , so userspace can differentiate
	 * between this and the old multi-field proc format.
	 */
	list_for_each_entry(use, &mod->source_list, source_list) {
		printed_something = 1;
		seq_printf(m, "%s,", use->source->name);
	}

	if (mod->init != NULL && mod->exit == NULL) {
		printed_something = 1;
		seq_puts(m, "[permanent],");
	}

	if (!printed_something)
		seq_puts(m, "-");
}

void __symbol_put(const char *symbol)
{
	struct module *owner;

	preempt_disable();
	if (!find_symbol(symbol, &owner, NULL, NULL, true, false))
		BUG();
	module_put(owner);
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
static inline void print_unload_info(struct seq_file *m, struct module *mod)
{
	/* We don't know the usage count, or what modules are using. */
	seq_puts(m, " - -");
}

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

static size_t module_flags_taint(struct module *mod, char *buf)
{
	size_t l = 0;
	int i;

	for (i = 0; i < TAINT_FLAGS_COUNT; i++) {
		if (taint_flags[i].module && test_bit(i, &mod->taints))
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

	l = module_flags_taint(mk->mod, buffer);
	buffer[l++] = '\n';
	return l;
}

static struct module_attribute modinfo_taint =
	__ATTR(taint, 0444, show_taint, NULL);

static struct module_attribute *modinfo_attrs[] = {
	&module_uevent,
	&modinfo_version,
	&modinfo_srcversion,
	&modinfo_scmversion,
	&modinfo_initstate,
	&modinfo_coresize,
	&modinfo_initsize,
	&modinfo_taint,
#ifdef CONFIG_MODULE_UNLOAD
	&modinfo_refcnt,
#endif
	NULL,
};

static const char vermagic[] = VERMAGIC_STRING;

static int try_to_force_load(struct module *mod, const char *reason)
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

#ifdef CONFIG_MODVERSIONS

static u32 resolve_rel_crc(const s32 *crc)
{
	return *(u32 *)((void *)crc + *crc);
}

static int check_version(const struct load_info *info,
			 const char *symname,
			 struct module *mod,
			 const s32 *crc)
{
	Elf_Shdr *sechdrs = info->sechdrs;
	unsigned int versindex = info->index.vers;
	unsigned int i, num_versions;
	struct modversion_info *versions;

	/* Exporting module didn't supply crcs?  OK, we're already tainted. */
	if (!crc)
		return 1;

	/* No versions at all?  modprobe --force does this. */
	if (versindex == 0)
		return try_to_force_load(mod, symname) == 0;

	versions = (void *) sechdrs[versindex].sh_addr;
	num_versions = sechdrs[versindex].sh_size
		/ sizeof(struct modversion_info);

	for (i = 0; i < num_versions; i++) {
		u32 crcval;

		if (strcmp(versions[i].name, symname) != 0)
			continue;

		if (IS_ENABLED(CONFIG_MODULE_REL_CRCS))
			crcval = resolve_rel_crc(crc);
		else
			crcval = *crc;
		if (versions[i].crc == crcval)
			return 1;
		pr_debug("Found checksum %X vs module %lX\n",
			 crcval, versions[i].crc);
		goto bad_version;
	}

	/* Broken toolchain. Warn once, then let it go.. */
	pr_warn_once("%s: no symbol version for %s\n", info->name, symname);
	return 1;

bad_version:
	pr_warn("%s: disagrees about version of symbol %s\n",
	       info->name, symname);
	return 0;
}

static inline int check_modstruct_version(const struct load_info *info,
					  struct module *mod)
{
	const s32 *crc;

	/*
	 * Since this should be found in kernel (which can't be removed), no
	 * locking is necessary -- use preempt_disable() to placate lockdep.
	 */
	preempt_disable();
	if (!find_symbol("module_layout", NULL, &crc, NULL, true, false)) {
		preempt_enable();
		BUG();
	}
	preempt_enable();
	return check_version(info, "module_layout", mod, crc);
}

/* First part is kernel version, which we ignore if module has crcs. */
static inline int same_magic(const char *amagic, const char *bmagic,
			     bool has_crcs)
{
	if (has_crcs) {
		amagic += strcspn(amagic, " ");
		bmagic += strcspn(bmagic, " ");
	}
	return strcmp(amagic, bmagic) == 0;
}
#else
static inline int check_version(const struct load_info *info,
				const char *symname,
				struct module *mod,
				const s32 *crc)
{
	return 1;
}

static inline int check_modstruct_version(const struct load_info *info,
					  struct module *mod)
{
	return 1;
}

static inline int same_magic(const char *amagic, const char *bmagic,
			     bool has_crcs)
{
	return strcmp(amagic, bmagic) == 0;
}
#endif /* CONFIG_MODVERSIONS */

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

static bool inherit_taint(struct module *mod, struct module *owner)
{
	if (!owner || !test_bit(TAINT_PROPRIETARY_MODULE, &owner->taints))
		return true;

	if (mod->using_gplonly_symbols) {
		pr_err("%s: module using GPL-only symbols uses symbols from proprietary module %s.\n",
			mod->name, owner->name);
		return false;
	}

	if (!test_bit(TAINT_PROPRIETARY_MODULE, &mod->taints)) {
		pr_warn("%s: module uses symbols from proprietary module %s, inheriting taint.\n",
			mod->name, owner->name);
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
	struct module *owner;
	const struct kernel_symbol *sym;
	const s32 *crc;
	enum mod_license license;
	int err;

	/*
	 * The module_mutex should not be a heavily contended lock;
	 * if we get the occasional sleep here, we'll go an extra iteration
	 * in the wait_event_interruptible(), which is harmless.
	 */
	sched_annotate_sleep();
	mutex_lock(&module_mutex);
	sym = find_symbol(name, &owner, &crc, &license,
			  !(mod->taints & (1 << TAINT_PROPRIETARY_MODULE)), true);
	if (!sym)
		goto unlock;

	if (license == GPL_ONLY)
		mod->using_gplonly_symbols = true;

	if (!inherit_taint(mod, owner)) {
		sym = NULL;
		goto getname;
	}

	if (!check_version(info, name, mod, crc)) {
		sym = ERR_PTR(-EINVAL);
		goto getname;
	}

	err = verify_namespace_is_imported(info, sym, mod);
	if (err) {
		sym = ERR_PTR(err);
		goto getname;
	}

	err = ref_module(mod, owner);
	if (err) {
		sym = ERR_PTR(err);
		goto getname;
	}

getname:
	/* We must make copy under the lock if we failed to get ref. */
	strncpy(ownername, module_name(owner), MODULE_NAME_LEN);
unlock:
	mutex_unlock(&module_mutex);
	return sym;
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

/*
 * /sys/module/foo/sections stuff
 * J. Corbet <corbet@lwn.net>
 */
#ifdef CONFIG_SYSFS

#ifdef CONFIG_KALLSYMS
static inline bool sect_empty(const Elf_Shdr *sect)
{
	return !(sect->sh_flags & SHF_ALLOC) || sect->sh_size == 0;
}

struct module_sect_attr {
	struct bin_attribute battr;
	unsigned long address;
};

struct module_sect_attrs {
	struct attribute_group grp;
	unsigned int nsections;
	struct module_sect_attr attrs[];
};

#define MODULE_SECT_READ_SIZE (3 /* "0x", "\n" */ + (BITS_PER_LONG / 4))
static ssize_t module_sect_read(struct file *file, struct kobject *kobj,
				struct bin_attribute *battr,
				char *buf, loff_t pos, size_t count)
{
	struct module_sect_attr *sattr =
		container_of(battr, struct module_sect_attr, battr);
	char bounce[MODULE_SECT_READ_SIZE + 1];
	size_t wrote;

	if (pos != 0)
		return -EINVAL;

	/*
	 * Since we're a binary read handler, we must account for the
	 * trailing NUL byte that sprintf will write: if "buf" is
	 * too small to hold the NUL, or the NUL is exactly the last
	 * byte, the read will look like it got truncated by one byte.
	 * Since there is no way to ask sprintf nicely to not write
	 * the NUL, we have to use a bounce buffer.
	 */
	wrote = scnprintf(bounce, sizeof(bounce), "0x%px\n",
			 kallsyms_show_value(file->f_cred)
				? (void *)sattr->address : NULL);
	count = min(count, wrote);
	memcpy(buf, bounce, count);

	return count;
}

static void free_sect_attrs(struct module_sect_attrs *sect_attrs)
{
	unsigned int section;

	for (section = 0; section < sect_attrs->nsections; section++)
		kfree(sect_attrs->attrs[section].battr.attr.name);
	kfree(sect_attrs);
}

static void add_sect_attrs(struct module *mod, const struct load_info *info)
{
	unsigned int nloaded = 0, i, size[2];
	struct module_sect_attrs *sect_attrs;
	struct module_sect_attr *sattr;
	struct bin_attribute **gattr;

	/* Count loaded sections and allocate structures */
	for (i = 0; i < info->hdr->e_shnum; i++)
		if (!sect_empty(&info->sechdrs[i]))
			nloaded++;
	size[0] = ALIGN(struct_size(sect_attrs, attrs, nloaded),
			sizeof(sect_attrs->grp.bin_attrs[0]));
	size[1] = (nloaded + 1) * sizeof(sect_attrs->grp.bin_attrs[0]);
	sect_attrs = kzalloc(size[0] + size[1], GFP_KERNEL);
	if (sect_attrs == NULL)
		return;

	/* Setup section attributes. */
	sect_attrs->grp.name = "sections";
	sect_attrs->grp.bin_attrs = (void *)sect_attrs + size[0];

	sect_attrs->nsections = 0;
	sattr = &sect_attrs->attrs[0];
	gattr = &sect_attrs->grp.bin_attrs[0];
	for (i = 0; i < info->hdr->e_shnum; i++) {
		Elf_Shdr *sec = &info->sechdrs[i];
		if (sect_empty(sec))
			continue;
		sysfs_bin_attr_init(&sattr->battr);
		sattr->address = sec->sh_addr;
		sattr->battr.attr.name =
			kstrdup(info->secstrings + sec->sh_name, GFP_KERNEL);
		if (sattr->battr.attr.name == NULL)
			goto out;
		sect_attrs->nsections++;
		sattr->battr.read = module_sect_read;
		sattr->battr.size = MODULE_SECT_READ_SIZE;
		sattr->battr.attr.mode = 0400;
		*(gattr++) = &(sattr++)->battr;
	}
	*gattr = NULL;

	if (sysfs_create_group(&mod->mkobj.kobj, &sect_attrs->grp))
		goto out;

	mod->sect_attrs = sect_attrs;
	return;
  out:
	free_sect_attrs(sect_attrs);
}

static void remove_sect_attrs(struct module *mod)
{
	if (mod->sect_attrs) {
		sysfs_remove_group(&mod->mkobj.kobj,
				   &mod->sect_attrs->grp);
		/* We are positive that no one is using any sect attrs
		 * at this point.  Deallocate immediately. */
		free_sect_attrs(mod->sect_attrs);
		mod->sect_attrs = NULL;
	}
}

/*
 * /sys/module/foo/notes/.section.name gives contents of SHT_NOTE sections.
 */

struct module_notes_attrs {
	struct kobject *dir;
	unsigned int notes;
	struct bin_attribute attrs[];
};

static ssize_t module_notes_read(struct file *filp, struct kobject *kobj,
				 struct bin_attribute *bin_attr,
				 char *buf, loff_t pos, size_t count)
{
	/*
	 * The caller checked the pos and count against our size.
	 */
	memcpy(buf, bin_attr->private + pos, count);
	return count;
}

static void free_notes_attrs(struct module_notes_attrs *notes_attrs,
			     unsigned int i)
{
	if (notes_attrs->dir) {
		while (i-- > 0)
			sysfs_remove_bin_file(notes_attrs->dir,
					      &notes_attrs->attrs[i]);
		kobject_put(notes_attrs->dir);
	}
	kfree(notes_attrs);
}

static void add_notes_attrs(struct module *mod, const struct load_info *info)
{
	unsigned int notes, loaded, i;
	struct module_notes_attrs *notes_attrs;
	struct bin_attribute *nattr;

	/* failed to create section attributes, so can't create notes */
	if (!mod->sect_attrs)
		return;

	/* Count notes sections and allocate structures.  */
	notes = 0;
	for (i = 0; i < info->hdr->e_shnum; i++)
		if (!sect_empty(&info->sechdrs[i]) &&
		    (info->sechdrs[i].sh_type == SHT_NOTE))
			++notes;

	if (notes == 0)
		return;

	notes_attrs = kzalloc(struct_size(notes_attrs, attrs, notes),
			      GFP_KERNEL);
	if (notes_attrs == NULL)
		return;

	notes_attrs->notes = notes;
	nattr = &notes_attrs->attrs[0];
	for (loaded = i = 0; i < info->hdr->e_shnum; ++i) {
		if (sect_empty(&info->sechdrs[i]))
			continue;
		if (info->sechdrs[i].sh_type == SHT_NOTE) {
			sysfs_bin_attr_init(nattr);
			nattr->attr.name = mod->sect_attrs->attrs[loaded].battr.attr.name;
			nattr->attr.mode = S_IRUGO;
			nattr->size = info->sechdrs[i].sh_size;
			nattr->private = (void *) info->sechdrs[i].sh_addr;
			nattr->read = module_notes_read;
			++nattr;
		}
		++loaded;
	}

	notes_attrs->dir = kobject_create_and_add("notes", &mod->mkobj.kobj);
	if (!notes_attrs->dir)
		goto out;

	for (i = 0; i < notes; ++i)
		if (sysfs_create_bin_file(notes_attrs->dir,
					  &notes_attrs->attrs[i]))
			goto out;

	mod->notes_attrs = notes_attrs;
	return;

  out:
	free_notes_attrs(notes_attrs, i);
}

static void remove_notes_attrs(struct module *mod)
{
	if (mod->notes_attrs)
		free_notes_attrs(mod->notes_attrs, mod->notes_attrs->notes);
}

#else

static inline void add_sect_attrs(struct module *mod,
				  const struct load_info *info)
{
}

static inline void remove_sect_attrs(struct module *mod)
{
}

static inline void add_notes_attrs(struct module *mod,
				   const struct load_info *info)
{
}

static inline void remove_notes_attrs(struct module *mod)
{
}
#endif /* CONFIG_KALLSYMS */

static void del_usage_links(struct module *mod)
{
#ifdef CONFIG_MODULE_UNLOAD
	struct module_use *use;

	mutex_lock(&module_mutex);
	list_for_each_entry(use, &mod->target_list, target_list)
		sysfs_remove_link(use->target->holders_dir, mod->name);
	mutex_unlock(&module_mutex);
#endif
}

static int add_usage_links(struct module *mod)
{
	int ret = 0;
#ifdef CONFIG_MODULE_UNLOAD
	struct module_use *use;

	mutex_lock(&module_mutex);
	list_for_each_entry(use, &mod->target_list, target_list) {
		ret = sysfs_create_link(use->target->holders_dir,
					&mod->mkobj.kobj, mod->name);
		if (ret)
			break;
	}
	mutex_unlock(&module_mutex);
	if (ret)
		del_usage_links(mod);
#endif
	return ret;
}

static void module_remove_modinfo_attrs(struct module *mod, int end);

static int module_add_modinfo_attrs(struct module *mod)
{
	struct module_attribute *attr;
	struct module_attribute *temp_attr;
	int error = 0;
	int i;

	mod->modinfo_attrs = kzalloc((sizeof(struct module_attribute) *
					(ARRAY_SIZE(modinfo_attrs) + 1)),
					GFP_KERNEL);
	if (!mod->modinfo_attrs)
		return -ENOMEM;

	temp_attr = mod->modinfo_attrs;
	for (i = 0; (attr = modinfo_attrs[i]); i++) {
		if (!attr->test || attr->test(mod)) {
			memcpy(temp_attr, attr, sizeof(*temp_attr));
			sysfs_attr_init(&temp_attr->attr);
			error = sysfs_create_file(&mod->mkobj.kobj,
					&temp_attr->attr);
			if (error)
				goto error_out;
			++temp_attr;
		}
	}

	return 0;

error_out:
	if (i > 0)
		module_remove_modinfo_attrs(mod, --i);
	else
		kfree(mod->modinfo_attrs);
	return error;
}

static void module_remove_modinfo_attrs(struct module *mod, int end)
{
	struct module_attribute *attr;
	int i;

	for (i = 0; (attr = &mod->modinfo_attrs[i]); i++) {
		if (end >= 0 && i > end)
			break;
		/* pick a field to test for end of list */
		if (!attr->attr.name)
			break;
		sysfs_remove_file(&mod->mkobj.kobj, &attr->attr);
		if (attr->free)
			attr->free(mod);
	}
	kfree(mod->modinfo_attrs);
}

static void mod_kobject_put(struct module *mod)
{
	DECLARE_COMPLETION_ONSTACK(c);
	mod->mkobj.kobj_completion = &c;
	kobject_put(&mod->mkobj.kobj);
	wait_for_completion(&c);
}

static int mod_sysfs_init(struct module *mod)
{
	int err;
	struct kobject *kobj;

	if (!module_sysfs_initialized) {
		pr_err("%s: module sysfs not initialized\n", mod->name);
		err = -EINVAL;
		goto out;
	}

	kobj = kset_find_obj(module_kset, mod->name);
	if (kobj) {
		pr_err("%s: module is already loaded\n", mod->name);
		kobject_put(kobj);
		err = -EINVAL;
		goto out;
	}

	mod->mkobj.mod = mod;

	memset(&mod->mkobj.kobj, 0, sizeof(mod->mkobj.kobj));
	mod->mkobj.kobj.kset = module_kset;
	err = kobject_init_and_add(&mod->mkobj.kobj, &module_ktype, NULL,
				   "%s", mod->name);
	if (err)
		mod_kobject_put(mod);

out:
	return err;
}

static int mod_sysfs_setup(struct module *mod,
			   const struct load_info *info,
			   struct kernel_param *kparam,
			   unsigned int num_params)
{
	int err;

	err = mod_sysfs_init(mod);
	if (err)
		goto out;

	mod->holders_dir = kobject_create_and_add("holders", &mod->mkobj.kobj);
	if (!mod->holders_dir) {
		err = -ENOMEM;
		goto out_unreg;
	}

	err = module_param_sysfs_setup(mod, kparam, num_params);
	if (err)
		goto out_unreg_holders;

	err = module_add_modinfo_attrs(mod);
	if (err)
		goto out_unreg_param;

	err = add_usage_links(mod);
	if (err)
		goto out_unreg_modinfo_attrs;

	add_sect_attrs(mod, info);
	add_notes_attrs(mod, info);

	return 0;

out_unreg_modinfo_attrs:
	module_remove_modinfo_attrs(mod, -1);
out_unreg_param:
	module_param_sysfs_remove(mod);
out_unreg_holders:
	kobject_put(mod->holders_dir);
out_unreg:
	mod_kobject_put(mod);
out:
	return err;
}

static void mod_sysfs_fini(struct module *mod)
{
	remove_notes_attrs(mod);
	remove_sect_attrs(mod);
	mod_kobject_put(mod);
}

static void init_param_lock(struct module *mod)
{
	mutex_init(&mod->param_lock);
}
#else /* !CONFIG_SYSFS */

static int mod_sysfs_setup(struct module *mod,
			   const struct load_info *info,
			   struct kernel_param *kparam,
			   unsigned int num_params)
{
	return 0;
}

static void mod_sysfs_fini(struct module *mod)
{
}

static void module_remove_modinfo_attrs(struct module *mod, int end)
{
}

static void del_usage_links(struct module *mod)
{
}

static void init_param_lock(struct module *mod)
{
}
#endif /* CONFIG_SYSFS */

static void mod_sysfs_teardown(struct module *mod)
{
	del_usage_links(mod);
	module_remove_modinfo_attrs(mod, -1);
	module_param_sysfs_remove(mod);
	kobject_put(mod->mkobj.drivers_dir);
	kobject_put(mod->holders_dir);
	mod_sysfs_fini(mod);
}

/*
 * LKM RO/NX protection: protect module's text/ro-data
 * from modification and any data from execution.
 *
 * General layout of module is:
 *          [text] [read-only-data] [ro-after-init] [writable data]
 * text_size -----^                ^               ^               ^
 * ro_size ------------------------|               |               |
 * ro_after_init_size -----------------------------|               |
 * size -----------------------------------------------------------|
 *
 * These values are always page-aligned (as is base)
 */

/*
 * Since some arches are moving towards PAGE_KERNEL module allocations instead
 * of PAGE_KERNEL_EXEC, keep frob_text() and module_enable_x() outside of the
 * CONFIG_STRICT_MODULE_RWX block below because they are needed regardless of
 * whether we are strict.
 */
#ifdef CONFIG_ARCH_HAS_STRICT_MODULE_RWX
static void frob_text(const struct module_layout *layout,
		      int (*set_memory)(unsigned long start, int num_pages))
{
	BUG_ON((unsigned long)layout->base & (PAGE_SIZE-1));
	BUG_ON((unsigned long)layout->text_size & (PAGE_SIZE-1));
	set_memory((unsigned long)layout->base,
		   layout->text_size >> PAGE_SHIFT);
}

static void module_enable_x(const struct module *mod)
{
	frob_text(&mod->core_layout, set_memory_x);
	frob_text(&mod->init_layout, set_memory_x);
}
#else /* !CONFIG_ARCH_HAS_STRICT_MODULE_RWX */
static void module_enable_x(const struct module *mod) { }
#endif /* CONFIG_ARCH_HAS_STRICT_MODULE_RWX */

#ifdef CONFIG_STRICT_MODULE_RWX
static void frob_rodata(const struct module_layout *layout,
			int (*set_memory)(unsigned long start, int num_pages))
{
	BUG_ON((unsigned long)layout->base & (PAGE_SIZE-1));
	BUG_ON((unsigned long)layout->text_size & (PAGE_SIZE-1));
	BUG_ON((unsigned long)layout->ro_size & (PAGE_SIZE-1));
	set_memory((unsigned long)layout->base + layout->text_size,
		   (layout->ro_size - layout->text_size) >> PAGE_SHIFT);
}

static void frob_ro_after_init(const struct module_layout *layout,
				int (*set_memory)(unsigned long start, int num_pages))
{
	BUG_ON((unsigned long)layout->base & (PAGE_SIZE-1));
	BUG_ON((unsigned long)layout->ro_size & (PAGE_SIZE-1));
	BUG_ON((unsigned long)layout->ro_after_init_size & (PAGE_SIZE-1));
	set_memory((unsigned long)layout->base + layout->ro_size,
		   (layout->ro_after_init_size - layout->ro_size) >> PAGE_SHIFT);
}

static void frob_writable_data(const struct module_layout *layout,
			       int (*set_memory)(unsigned long start, int num_pages))
{
	BUG_ON((unsigned long)layout->base & (PAGE_SIZE-1));
	BUG_ON((unsigned long)layout->ro_after_init_size & (PAGE_SIZE-1));
	BUG_ON((unsigned long)layout->size & (PAGE_SIZE-1));
	set_memory((unsigned long)layout->base + layout->ro_after_init_size,
		   (layout->size - layout->ro_after_init_size) >> PAGE_SHIFT);
}

static void module_enable_ro(const struct module *mod, bool after_init)
{
	if (!rodata_enabled)
		return;

	set_vm_flush_reset_perms(mod->core_layout.base);
	set_vm_flush_reset_perms(mod->init_layout.base);
	frob_text(&mod->core_layout, set_memory_ro);

	frob_rodata(&mod->core_layout, set_memory_ro);
	frob_text(&mod->init_layout, set_memory_ro);
	frob_rodata(&mod->init_layout, set_memory_ro);

	if (after_init)
		frob_ro_after_init(&mod->core_layout, set_memory_ro);
}

static void module_enable_nx(const struct module *mod)
{
	frob_rodata(&mod->core_layout, set_memory_nx);
	frob_ro_after_init(&mod->core_layout, set_memory_nx);
	frob_writable_data(&mod->core_layout, set_memory_nx);
	frob_rodata(&mod->init_layout, set_memory_nx);
	frob_writable_data(&mod->init_layout, set_memory_nx);
}

static int module_enforce_rwx_sections(Elf_Ehdr *hdr, Elf_Shdr *sechdrs,
				       char *secstrings, struct module *mod)
{
	const unsigned long shf_wx = SHF_WRITE|SHF_EXECINSTR;
	int i;

	for (i = 0; i < hdr->e_shnum; i++) {
		if ((sechdrs[i].sh_flags & shf_wx) == shf_wx) {
			pr_err("%s: section %s (index %d) has invalid WRITE|EXEC flags\n",
				mod->name, secstrings + sechdrs[i].sh_name, i);
			return -ENOEXEC;
		}
	}

	return 0;
}

#else /* !CONFIG_STRICT_MODULE_RWX */
static void module_enable_nx(const struct module *mod) { }
static void module_enable_ro(const struct module *mod, bool after_init) {}
static int module_enforce_rwx_sections(Elf_Ehdr *hdr, Elf_Shdr *sechdrs,
				       char *secstrings, struct module *mod)
{
	return 0;
}
#endif /*  CONFIG_STRICT_MODULE_RWX */

#ifdef CONFIG_LIVEPATCH
/*
 * Persist Elf information about a module. Copy the Elf header,
 * section header table, section string table, and symtab section
 * index from info to mod->klp_info.
 */
static int copy_module_elf(struct module *mod, struct load_info *info)
{
	unsigned int size, symndx;
	int ret;

	size = sizeof(*mod->klp_info);
	mod->klp_info = kmalloc(size, GFP_KERNEL);
	if (mod->klp_info == NULL)
		return -ENOMEM;

	/* Elf header */
	size = sizeof(mod->klp_info->hdr);
	memcpy(&mod->klp_info->hdr, info->hdr, size);

	/* Elf section header table */
	size = sizeof(*info->sechdrs) * info->hdr->e_shnum;
	mod->klp_info->sechdrs = kmemdup(info->sechdrs, size, GFP_KERNEL);
	if (mod->klp_info->sechdrs == NULL) {
		ret = -ENOMEM;
		goto free_info;
	}

	/* Elf section name string table */
	size = info->sechdrs[info->hdr->e_shstrndx].sh_size;
	mod->klp_info->secstrings = kmemdup(info->secstrings, size, GFP_KERNEL);
	if (mod->klp_info->secstrings == NULL) {
		ret = -ENOMEM;
		goto free_sechdrs;
	}

	/* Elf symbol section index */
	symndx = info->index.sym;
	mod->klp_info->symndx = symndx;

	/*
	 * For livepatch modules, core_kallsyms.symtab is a complete
	 * copy of the original symbol table. Adjust sh_addr to point
	 * to core_kallsyms.symtab since the copy of the symtab in module
	 * init memory is freed at the end of do_init_module().
	 */
	mod->klp_info->sechdrs[symndx].sh_addr = \
		(unsigned long) mod->core_kallsyms.symtab;

	return 0;

free_sechdrs:
	kfree(mod->klp_info->sechdrs);
free_info:
	kfree(mod->klp_info);
	return ret;
}

static void free_module_elf(struct module *mod)
{
	kfree(mod->klp_info->sechdrs);
	kfree(mod->klp_info->secstrings);
	kfree(mod->klp_info);
}
#else /* !CONFIG_LIVEPATCH */
static int copy_module_elf(struct module *mod, struct load_info *info)
{
	return 0;
}

static void free_module_elf(struct module *mod)
{
}
#endif /* CONFIG_LIVEPATCH */

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

static void cfi_cleanup(struct module *mod);

/* Free a module, remove from lists, etc. */
static void free_module(struct module *mod)
{
	trace_module_free(mod);

	mod_sysfs_teardown(mod);

	/* We leave it in list to prevent duplicate loads, but make sure
	 * that noone uses it while it's being deconstructed. */
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
	mutex_unlock(&module_mutex);

	/* Clean up CFI for the module. */
	cfi_cleanup(mod);

	/* This may be empty, but that's OK */
	module_arch_freeing_init(mod);
	trace_android_vh_set_memory_rw((unsigned long)mod->init_layout.base,
		(mod->init_layout.size)>>PAGE_SHIFT);
	trace_android_vh_set_memory_nx((unsigned long)mod->init_layout.base,
		(mod->init_layout.size)>>PAGE_SHIFT);
	module_memfree(mod->init_layout.base);
	kfree(mod->args);
	percpu_modfree(mod);

	/* Free lock-classes; relies on the preceding sync_rcu(). */
	lockdep_free_key_range(mod->core_layout.base, mod->core_layout.size);

	/* Finally, free the core (containing the module structure) */
	trace_android_vh_set_memory_rw((unsigned long)mod->core_layout.base,
		(mod->core_layout.size)>>PAGE_SHIFT);
	trace_android_vh_set_memory_nx((unsigned long)mod->core_layout.base,
		(mod->core_layout.size)>>PAGE_SHIFT);
	module_memfree(mod->core_layout.base);
}

void *__symbol_get(const char *symbol)
{
	struct module *owner;
	const struct kernel_symbol *sym;

	preempt_disable();
	sym = find_symbol(symbol, &owner, NULL, NULL, true, true);
	if (sym && strong_try_module_get(owner))
		sym = NULL;
	preempt_enable();

	return sym ? (void *)kernel_symbol_value(sym) : NULL;
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
	struct module *owner;
	const struct kernel_symbol *s;
	struct {
		const struct kernel_symbol *sym;
		unsigned int num;
	} arr[] = {
		{ mod->syms, mod->num_syms },
		{ mod->gpl_syms, mod->num_gpl_syms },
		{ mod->gpl_future_syms, mod->num_gpl_future_syms },
#ifdef CONFIG_UNUSED_SYMBOLS
		{ mod->unused_syms, mod->num_unused_syms },
		{ mod->unused_gpl_syms, mod->num_unused_gpl_syms },
#endif
	};

	for (i = 0; i < ARRAY_SIZE(arr); i++) {
		for (s = arr[i].sym; s < arr[i].sym + arr[i].num; s++) {
			if (find_symbol(kernel_symbol_name(s), &owner, NULL,
					NULL, true, false)) {
				pr_err("%s: exports duplicate symbol %s"
				       " (owned by %s)\n",
				       mod->name, kernel_symbol_name(s),
				       module_name(owner));
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

			/* We compiled with -fno-common.  These are not
			   supposed to happen.  */
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
static long get_offset(struct module *mod, unsigned int *size,
		       Elf_Shdr *sechdr, unsigned int section)
{
	long ret;

	*size += arch_mod_section_prepend(mod, section);
	ret = ALIGN(*size, sechdr->sh_addralign ?: 1);
	*size = ret + sechdr->sh_size;
	return ret;
}

/* Lay out the SHF_ALLOC sections in a way not dissimilar to how ld
   might -- code, read-only data, read-write data, small data.  Tally
   sizes, and place the offsets into sh_entsize fields: high bit means it
   belongs in init. */
static void layout_sections(struct module *mod, struct load_info *info)
{
	static unsigned long const masks[][2] = {
		/* NOTE: all executable code must be the first section
		 * in this array; otherwise modify the text_size
		 * finder in the two loops below */
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

			if ((s->sh_flags & masks[m][0]) != masks[m][0]
			    || (s->sh_flags & masks[m][1])
			    || s->sh_entsize != ~0UL
			    || module_init_section(sname))
				continue;
			s->sh_entsize = get_offset(mod, &mod->core_layout.size, s, i);
			pr_debug("\t%s\n", sname);
		}
		switch (m) {
		case 0: /* executable */
			mod->core_layout.size = debug_align(mod->core_layout.size);
			mod->core_layout.text_size = mod->core_layout.size;
			break;
		case 1: /* RO: text and ro-data */
			mod->core_layout.size = debug_align(mod->core_layout.size);
			mod->core_layout.ro_size = mod->core_layout.size;
			break;
		case 2: /* RO after init */
			mod->core_layout.size = debug_align(mod->core_layout.size);
			mod->core_layout.ro_after_init_size = mod->core_layout.size;
			break;
		case 4: /* whole core */
			mod->core_layout.size = debug_align(mod->core_layout.size);
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
			    || !module_init_section(sname))
				continue;
			s->sh_entsize = (get_offset(mod, &mod->init_layout.size, s, i)
					 | INIT_OFFSET_MASK);
			pr_debug("\t%s\n", sname);
		}
		switch (m) {
		case 0: /* executable */
			mod->init_layout.size = debug_align(mod->init_layout.size);
			mod->init_layout.text_size = mod->init_layout.size;
			break;
		case 1: /* RO: text and ro-data */
			mod->init_layout.size = debug_align(mod->init_layout.size);
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
			mod->init_layout.size = debug_align(mod->init_layout.size);
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

#ifdef CONFIG_KALLSYMS

/* Lookup exported symbol in given range of kernel_symbols */
static const struct kernel_symbol *lookup_exported_symbol(const char *name,
							  const struct kernel_symbol *start,
							  const struct kernel_symbol *stop)
{
	return bsearch(name, start, stop - start,
			sizeof(struct kernel_symbol), cmp_name);
}

static int is_exported(const char *name, unsigned long value,
		       const struct module *mod)
{
	const struct kernel_symbol *ks;
	if (!mod)
		ks = lookup_exported_symbol(name, __start___ksymtab, __stop___ksymtab);
	else
		ks = lookup_exported_symbol(name, mod->syms, mod->syms + mod->num_syms);

	return ks != NULL && kernel_symbol_value(ks) == value;
}

/* As per nm */
static char elf_type(const Elf_Sym *sym, const struct load_info *info)
{
	const Elf_Shdr *sechdrs = info->sechdrs;

	if (ELF_ST_BIND(sym->st_info) == STB_WEAK) {
		if (ELF_ST_TYPE(sym->st_info) == STT_OBJECT)
			return 'v';
		else
			return 'w';
	}
	if (sym->st_shndx == SHN_UNDEF)
		return 'U';
	if (sym->st_shndx == SHN_ABS || sym->st_shndx == info->index.pcpu)
		return 'a';
	if (sym->st_shndx >= SHN_LORESERVE)
		return '?';
	if (sechdrs[sym->st_shndx].sh_flags & SHF_EXECINSTR)
		return 't';
	if (sechdrs[sym->st_shndx].sh_flags & SHF_ALLOC
	    && sechdrs[sym->st_shndx].sh_type != SHT_NOBITS) {
		if (!(sechdrs[sym->st_shndx].sh_flags & SHF_WRITE))
			return 'r';
		else if (sechdrs[sym->st_shndx].sh_flags & ARCH_SHF_SMALL)
			return 'g';
		else
			return 'd';
	}
	if (sechdrs[sym->st_shndx].sh_type == SHT_NOBITS) {
		if (sechdrs[sym->st_shndx].sh_flags & ARCH_SHF_SMALL)
			return 's';
		else
			return 'b';
	}
	if (strstarts(info->secstrings + sechdrs[sym->st_shndx].sh_name,
		      ".debug")) {
		return 'n';
	}
	return '?';
}

static bool is_core_symbol(const Elf_Sym *src, const Elf_Shdr *sechdrs,
			unsigned int shnum, unsigned int pcpundx)
{
	const Elf_Shdr *sec;

	if (src->st_shndx == SHN_UNDEF
	    || src->st_shndx >= shnum
	    || !src->st_name)
		return false;

#ifdef CONFIG_KALLSYMS_ALL
	if (src->st_shndx == pcpundx)
		return true;
#endif

	sec = sechdrs + src->st_shndx;
	if (!(sec->sh_flags & SHF_ALLOC)
#ifndef CONFIG_KALLSYMS_ALL
	    || !(sec->sh_flags & SHF_EXECINSTR)
#endif
	    || (sec->sh_entsize & INIT_OFFSET_MASK))
		return false;

	return true;
}

/*
 * We only allocate and copy the strings needed by the parts of symtab
 * we keep.  This is simple, but has the effect of making multiple
 * copies of duplicates.  We could be more sophisticated, see
 * linux-kernel thread starting with
 * <73defb5e4bca04a6431392cc341112b1@localhost>.
 */
static void layout_symtab(struct module *mod, struct load_info *info)
{
	Elf_Shdr *symsect = info->sechdrs + info->index.sym;
	Elf_Shdr *strsect = info->sechdrs + info->index.str;
	const Elf_Sym *src;
	unsigned int i, nsrc, ndst, strtab_size = 0;

	/* Put symbol section at end of init part of module. */
	symsect->sh_flags |= SHF_ALLOC;
	symsect->sh_entsize = get_offset(mod, &mod->init_layout.size, symsect,
					 info->index.sym) | INIT_OFFSET_MASK;
	pr_debug("\t%s\n", info->secstrings + symsect->sh_name);

	src = (void *)info->hdr + symsect->sh_offset;
	nsrc = symsect->sh_size / sizeof(*src);

	/* Compute total space required for the core symbols' strtab. */
	for (ndst = i = 0; i < nsrc; i++) {
		if (i == 0 || is_livepatch_module(mod) ||
		    is_core_symbol(src+i, info->sechdrs, info->hdr->e_shnum,
				   info->index.pcpu)) {
			strtab_size += strlen(&info->strtab[src[i].st_name])+1;
			ndst++;
		}
	}

	/* Append room for core symbols at end of core part. */
	info->symoffs = ALIGN(mod->core_layout.size, symsect->sh_addralign ?: 1);
	info->stroffs = mod->core_layout.size = info->symoffs + ndst * sizeof(Elf_Sym);
	mod->core_layout.size += strtab_size;
	info->core_typeoffs = mod->core_layout.size;
	mod->core_layout.size += ndst * sizeof(char);
	mod->core_layout.size = debug_align(mod->core_layout.size);

	/* Put string table section at end of init part of module. */
	strsect->sh_flags |= SHF_ALLOC;
	strsect->sh_entsize = get_offset(mod, &mod->init_layout.size, strsect,
					 info->index.str) | INIT_OFFSET_MASK;
	pr_debug("\t%s\n", info->secstrings + strsect->sh_name);

	/* We'll tack temporary mod_kallsyms on the end. */
	mod->init_layout.size = ALIGN(mod->init_layout.size,
				      __alignof__(struct mod_kallsyms));
	info->mod_kallsyms_init_off = mod->init_layout.size;
	mod->init_layout.size += sizeof(struct mod_kallsyms);
	info->init_typeoffs = mod->init_layout.size;
	mod->init_layout.size += nsrc * sizeof(char);
	mod->init_layout.size = debug_align(mod->init_layout.size);
}

/*
 * We use the full symtab and strtab which layout_symtab arranged to
 * be appended to the init section.  Later we switch to the cut-down
 * core-only ones.
 */
static void add_kallsyms(struct module *mod, const struct load_info *info)
{
	unsigned int i, ndst;
	const Elf_Sym *src;
	Elf_Sym *dst;
	char *s;
	Elf_Shdr *symsec = &info->sechdrs[info->index.sym];

	/* Set up to point into init section. */
	mod->kallsyms = mod->init_layout.base + info->mod_kallsyms_init_off;

	mod->kallsyms->symtab = (void *)symsec->sh_addr;
	mod->kallsyms->num_symtab = symsec->sh_size / sizeof(Elf_Sym);
	/* Make sure we get permanent strtab: don't use info->strtab. */
	mod->kallsyms->strtab = (void *)info->sechdrs[info->index.str].sh_addr;
	mod->kallsyms->typetab = mod->init_layout.base + info->init_typeoffs;

	/*
	 * Now populate the cut down core kallsyms for after init
	 * and set types up while we still have access to sections.
	 */
	mod->core_kallsyms.symtab = dst = mod->core_layout.base + info->symoffs;
	mod->core_kallsyms.strtab = s = mod->core_layout.base + info->stroffs;
	mod->core_kallsyms.typetab = mod->core_layout.base + info->core_typeoffs;
	src = mod->kallsyms->symtab;
	for (ndst = i = 0; i < mod->kallsyms->num_symtab; i++) {
		mod->kallsyms->typetab[i] = elf_type(src + i, info);
		if (i == 0 || is_livepatch_module(mod) ||
		    is_core_symbol(src+i, info->sechdrs, info->hdr->e_shnum,
				   info->index.pcpu)) {
			mod->core_kallsyms.typetab[ndst] =
			    mod->kallsyms->typetab[i];
			dst[ndst] = src[i];
			dst[ndst++].st_name = s - mod->core_kallsyms.strtab;
			s += strlcpy(s, &mod->kallsyms->strtab[src[i].st_name],
				     KSYM_NAME_LEN) + 1;
		}
	}
	mod->core_kallsyms.num_symtab = ndst;
}
#else
static inline void layout_symtab(struct module *mod, struct load_info *info)
{
}

static void add_kallsyms(struct module *mod, const struct load_info *info)
{
}
#endif /* CONFIG_KALLSYMS */

static void dynamic_debug_setup(struct module *mod, struct _ddebug *debug, unsigned int num)
{
	if (!debug)
		return;
	ddebug_add_module(debug, num, mod->name);
}

static void dynamic_debug_remove(struct module *mod, struct _ddebug *debug)
{
	if (debug)
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

#ifdef CONFIG_DEBUG_KMEMLEAK
static void kmemleak_load_module(const struct module *mod,
				 const struct load_info *info)
{
	unsigned int i;

	/* only scan the sections containing data */
	kmemleak_scan_area(mod, sizeof(struct module), GFP_KERNEL);

	for (i = 1; i < info->hdr->e_shnum; i++) {
		/* Scan all writable sections that's not executable */
		if (!(info->sechdrs[i].sh_flags & SHF_ALLOC) ||
		    !(info->sechdrs[i].sh_flags & SHF_WRITE) ||
		    (info->sechdrs[i].sh_flags & SHF_EXECINSTR))
			continue;

		kmemleak_scan_area((void *)info->sechdrs[i].sh_addr,
				   info->sechdrs[i].sh_size, GFP_KERNEL);
	}
}
#else
static inline void kmemleak_load_module(const struct module *mod,
					const struct load_info *info)
{
}
#endif

#ifdef CONFIG_MODULE_SIG
static int module_sig_check(struct load_info *info, int flags)
{
	int err = -ENODATA;
	const unsigned long markerlen = sizeof(MODULE_SIG_STRING) - 1;
	const char *reason;
	const void *mod = info->hdr;

	/*
	 * Require flags == 0, as a module with version information
	 * removed is no longer the module that was signed
	 */
	if (flags == 0 &&
	    info->len > markerlen &&
	    memcmp(mod + info->len - markerlen, MODULE_SIG_STRING, markerlen) == 0) {
		/* We truncate the module to discard the signature */
		info->len -= markerlen;
		err = mod_verify_sig(mod, info);
	}

	switch (err) {
	case 0:
		info->sig_ok = true;
		return 0;

		/* We don't permit modules to be loaded into trusted kernels
		 * without a valid signature on them, but if we're not
		 * enforcing, certain errors are non-fatal.
		 */
	case -ENODATA:
		reason = "unsigned module";
		break;
	case -ENOPKG:
		reason = "module with unsupported crypto";
		break;
	case -ENOKEY:
		reason = "module with unavailable key";
		break;

		/* All other errors are fatal, including nomem, unparseable
		 * signatures and signature check failures - even if signatures
		 * aren't required.
		 */
	default:
		return err;
	}

	if (is_module_sig_enforced()) {
		pr_notice("Loading of %s is rejected\n", reason);
		return -EKEYREJECTED;
	}

	return security_locked_down(LOCKDOWN_MODULE_SIGNATURE);
}
#else /* !CONFIG_MODULE_SIG */
static int module_sig_check(struct load_info *info, int flags)
{
	return 0;
}
#endif /* !CONFIG_MODULE_SIG */

static int validate_section_offset(struct load_info *info, Elf_Shdr *shdr)
{
	unsigned long secend;

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

	if (info->len < sizeof(*(info->hdr)))
		return -ENOEXEC;

	if (memcmp(info->hdr->e_ident, ELFMAG, SELFMAG) != 0
	    || info->hdr->e_type != ET_REL
	    || !elf_check_arch(info->hdr)
	    || info->hdr->e_shentsize != sizeof(Elf_Shdr))
		return -ENOEXEC;

	/*
	 * e_shnum is 16 bits, and sizeof(Elf_Shdr) is
	 * known and small. So e_shnum * sizeof(Elf_Shdr)
	 * will not overflow unsigned long on any platform.
	 */
	if (info->hdr->e_shoff >= info->len
	    || (info->hdr->e_shnum * sizeof(Elf_Shdr) >
		info->len - info->hdr->e_shoff))
		return -ENOEXEC;

	info->sechdrs = (void *)info->hdr + info->hdr->e_shoff;

	/*
	 * Verify if the section name table index is valid.
	 */
	if (info->hdr->e_shstrndx == SHN_UNDEF
	    || info->hdr->e_shstrndx >= info->hdr->e_shnum)
		return -ENOEXEC;

	strhdr = &info->sechdrs[info->hdr->e_shstrndx];
	err = validate_section_offset(info, strhdr);
	if (err < 0)
		return err;

	/*
	 * The section name table must be NUL-terminated, as required
	 * by the spec. This makes strcmp and pr_* calls that access
	 * strings in the section safe.
	 */
	info->secstrings = (void *)info->hdr + strhdr->sh_offset;
	if (info->secstrings[strhdr->sh_size - 1] != '\0')
		return -ENOEXEC;

	/*
	 * The code assumes that section 0 has a length of zero and
	 * an addr of zero, so check for it.
	 */
	if (info->sechdrs[0].sh_type != SHT_NULL
	    || info->sechdrs[0].sh_size != 0
	    || info->sechdrs[0].sh_addr != 0)
		return -ENOEXEC;

	for (i = 1; i < info->hdr->e_shnum; i++) {
		shdr = &info->sechdrs[i];
		switch (shdr->sh_type) {
		case SHT_NULL:
		case SHT_NOBITS:
			continue;
		case SHT_SYMTAB:
			if (shdr->sh_link == SHN_UNDEF
			    || shdr->sh_link >= info->hdr->e_shnum)
				return -ENOEXEC;
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

#ifdef CONFIG_LIVEPATCH
static int check_modinfo_livepatch(struct module *mod, struct load_info *info)
{
	if (get_modinfo(info, "livepatch")) {
		mod->klp = true;
		add_taint_module(mod, TAINT_LIVEPATCH, LOCKDEP_STILL_OK);
		pr_notice_once("%s: tainting kernel with TAINT_LIVEPATCH\n",
			       mod->name);
	}

	return 0;
}
#else /* !CONFIG_LIVEPATCH */
static int check_modinfo_livepatch(struct module *mod, struct load_info *info)
{
	if (get_modinfo(info, "livepatch")) {
		pr_err("%s: module is marked as livepatch module, but livepatch support is disabled",
		       mod->name);
		return -ENOEXEC;
	}

	return 0;
}
#endif /* CONFIG_LIVEPATCH */

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

static void free_copy(struct load_info *info)
{
	vfree(info->hdr);
}

static int rewrite_section_headers(struct load_info *info, int flags)
{
	unsigned int i;

	/* This should always be true, but let's be sure. */
	info->sechdrs[0].sh_addr = 0;

	for (i = 1; i < info->hdr->e_shnum; i++) {
		Elf_Shdr *shdr = &info->sechdrs[i];

		/* Mark all sections sh_addr with their address in the
		   temporary image. */
		shdr->sh_addr = (size_t)info->hdr + shdr->sh_offset;

#ifndef CONFIG_MODULE_UNLOAD
		/* Don't load .exit sections */
		if (module_exit_section(info->secstrings+shdr->sh_name))
			shdr->sh_flags &= ~(unsigned long)SHF_ALLOC;
#endif
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
	mod->gpl_future_syms = section_objs(info,
					    "__ksymtab_gpl_future",
					    sizeof(*mod->gpl_future_syms),
					    &mod->num_gpl_future_syms);
	mod->gpl_future_crcs = section_addr(info, "__kcrctab_gpl_future");

#ifdef CONFIG_UNUSED_SYMBOLS
	mod->unused_syms = section_objs(info, "__ksymtab_unused",
					sizeof(*mod->unused_syms),
					&mod->num_unused_syms);
	mod->unused_crcs = section_addr(info, "__kcrctab_unused");
	mod->unused_gpl_syms = section_objs(info, "__ksymtab_unused_gpl",
					    sizeof(*mod->unused_gpl_syms),
					    &mod->num_unused_gpl_syms);
	mod->unused_gpl_crcs = section_addr(info, "__kcrctab_unused_gpl");
#endif
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
#ifdef CONFIG_HAVE_STATIC_CALL_INLINE
	mod->static_call_sites = section_objs(info, ".static_call_sites",
					      sizeof(*mod->static_call_sites),
					      &mod->num_static_call_sites);
#endif
	mod->extable = section_objs(info, "__ex_table",
				    sizeof(*mod->extable), &mod->num_exentries);

	if (section_addr(info, "__obsparm"))
		pr_warn("%s: Ignoring obsolete parameters\n", mod->name);

	info->debug = section_objs(info, "__dyndbg",
				   sizeof(*info->debug), &info->num_debug);

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
	if ((mod->num_syms && !mod->crcs)
	    || (mod->num_gpl_syms && !mod->gpl_crcs)
	    || (mod->num_gpl_future_syms && !mod->gpl_future_crcs)
#ifdef CONFIG_UNUSED_SYMBOLS
	    || (mod->num_unused_syms && !mod->unused_crcs)
	    || (mod->num_unused_gpl_syms && !mod->unused_gpl_crcs)
#endif
		) {
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

	/* Determine total sizes, and put offsets in sh_entsize.  For now
	   this is done generically; there doesn't appear to be any
	   special cases for the architectures. */
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
	trace_android_vh_set_memory_rw((unsigned long)mod->init_layout.base,
		(mod->init_layout.size)>>PAGE_SHIFT);
	trace_android_vh_set_memory_nx((unsigned long)mod->init_layout.base,
		(mod->init_layout.size)>>PAGE_SHIFT);
	module_memfree(mod->init_layout.base);
	trace_android_vh_set_memory_rw((unsigned long)mod->core_layout.base,
		(mod->core_layout.size)>>PAGE_SHIFT);
	trace_android_vh_set_memory_nx((unsigned long)mod->core_layout.base,
		(mod->core_layout.size)>>PAGE_SHIFT);
	module_memfree(mod->core_layout.base);
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
	ret = !mod || mod->state == MODULE_STATE_LIVE;
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

	/*
	 * We want to find out whether @mod uses async during init.  Clear
	 * PF_USED_ASYNC.  async_schedule*() will set it.
	 */
	current->flags &= ~PF_USED_ASYNC;

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
	 * is done.  This has potential to deadlock.  For example, a newly
	 * detected block device can trigger request_module() of the
	 * default iosched from async probing task.  Once userland helper
	 * reaches here, async_synchronize_full() will wait on the async
	 * task waiting on request_module() and deadlock.
	 *
	 * This deadlock is avoided by perfomring async_synchronize_full()
	 * iff module init queued any async jobs.  This isn't a full
	 * solution as it will deadlock the same if module loading from
	 * async jobs nests more than once; however, due to the various
	 * constraints, this hack seems to be the best option for now.
	 * Please refer to the following thread for details.
	 *
	 * http://thread.gmane.org/gmane.linux.kernel/1420814
	 */
	if (!mod->async_probe_requested && (current->flags & PF_USED_ASYNC))
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
	trace_android_vh_set_module_permit_after_init(mod);
	mod_tree_remove_init(mod);
	module_arch_freeing_init(mod);
	trace_android_vh_set_memory_rw((unsigned long)mod->init_layout.base,
		(mod->init_layout.size)>>PAGE_SHIFT);
	trace_android_vh_set_memory_nx((unsigned long)mod->init_layout.base,
		(mod->init_layout.size)>>PAGE_SHIFT);
	mod->init_layout.base = NULL;
	mod->init_layout.size = 0;
	mod->init_layout.ro_size = 0;
	mod->init_layout.ro_after_init_size = 0;
	mod->init_layout.text_size = 0;
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
	 * be cleaned up needs to sync with the queued work - ie
	 * rcu_barrier()
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

again:
	mutex_lock(&module_mutex);
	old = find_module_all(mod->name, strlen(mod->name), true);
	if (old != NULL) {
		if (old->state != MODULE_STATE_LIVE) {
			/* Wait in case it fails to load. */
			mutex_unlock(&module_mutex);
			err = wait_event_interruptible(module_wq,
					       finished_loading(mod->name));
			if (err)
				goto out_unlocked;
			goto again;
		}
		err = -EEXIST;
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

	/* This relies on module_mutex for list integrity. */
	module_bug_finalize(info->hdr, info->sechdrs, mod);

	module_enable_ro(mod, false);
	module_enable_nx(mod);
	module_enable_x(mod);
	trace_android_vh_set_module_permit_before_init(mod);

	/* Mark state as coming so strong_try_module_get() ignores us,
	 * but kallsyms etc. can see us. */
	mod->state = MODULE_STATE_COMING;
	mutex_unlock(&module_mutex);

	return 0;

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
		mod->async_probe_requested = true;
		return 0;
	}

	/* Check for magic 'dyndbg' arg */
	ret = ddebug_dyndbg_module_param_cb(param, val, modname);
	if (ret != 0)
		pr_warn("%s: unknown parameter '%s' ignored\n", modname, param);
	return 0;
}

static void cfi_init(struct module *mod);

/* Allocate and load the module: note that size of section 0 is always
   zero, and we rely on this for optional sections. */
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
	if (err) {
		pr_err("Module has invalid ELF structures\n");
		goto free_copy;
	}

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

	/* Now we've got everything in the final locations, we can
	 * find optional sections. */
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

	/* Setup CFI for the module. */
	cfi_init(mod);

	/* Now copy in args */
	mod->args = strndup_user(uargs, ~0UL >> 1);
	if (IS_ERR(mod->args)) {
		err = PTR_ERR(mod->args);
		goto free_arch_cleanup;
	}

	dynamic_debug_setup(mod, info->debug, info->num_debug);

	/* Ftrace init must be called in the MODULE_STATE_UNFORMED state */
	ftrace_module_init(mod);

	/* Finally it's fully formed, ready to start executing. */
	err = complete_formation(mod, info);
	if (err)
		goto ddebug_cleanup;

	err = prepare_coming_module(mod);
	if (err)
		goto bug_cleanup;

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
	free_copy(info);

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
	dynamic_debug_remove(mod, info->debug);
	synchronize_rcu();
	kfree(mod->args);
 free_arch_cleanup:
	cfi_cleanup(mod);
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
	lockdep_free_key_range(mod->core_layout.base, mod->core_layout.size);

	module_deallocate(mod, info);
 free_copy:
	free_copy(info);
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
	void *hdr = NULL;
	int err;

	err = may_init_module();
	if (err)
		return err;

	pr_debug("finit_module: fd=%d, uargs=%p, flags=%i\n", fd, uargs, flags);

	if (flags & ~(MODULE_INIT_IGNORE_MODVERSIONS
		      |MODULE_INIT_IGNORE_VERMAGIC))
		return -EINVAL;

	err = kernel_read_file_from_fd(fd, 0, &hdr, INT_MAX, NULL,
				       READING_MODULE);
	if (err < 0)
		return err;
	info.hdr = hdr;
	info.len = err;

	return load_module(&info, uargs, flags);
}

static inline int within(unsigned long addr, void *start, unsigned long size)
{
	return ((void *)addr >= start && (void *)addr < start + size);
}

#ifdef CONFIG_KALLSYMS
/*
 * This ignores the intensely annoying "mapping symbols" found
 * in ARM ELF files: $a, $t and $d.
 */
static inline int is_arm_mapping_symbol(const char *str)
{
	if (str[0] == '.' && str[1] == 'L')
		return true;
	return str[0] == '$' && strchr("axtd", str[1])
	       && (str[2] == '\0' || str[2] == '.');
}

static inline int is_cfi_typeid_symbol(const char *str)
{
	return !strncmp(str, "__typeid__", 10);
}

static const char *kallsyms_symbol_name(struct mod_kallsyms *kallsyms, unsigned int symnum)
{
	return kallsyms->strtab + kallsyms->symtab[symnum].st_name;
}

/*
 * Given a module and address, find the corresponding symbol and return its name
 * while providing its size and offset if needed.
 */
static const char *find_kallsyms_symbol(struct module *mod,
					unsigned long addr,
					unsigned long *size,
					unsigned long *offset)
{
	unsigned int i, best = 0;
	unsigned long nextval, bestval;
	struct mod_kallsyms *kallsyms = rcu_dereference_sched(mod->kallsyms);

	/* At worse, next value is at end of module */
	if (within_module_init(addr, mod))
		nextval = (unsigned long)mod->init_layout.base+mod->init_layout.text_size;
	else
		nextval = (unsigned long)mod->core_layout.base+mod->core_layout.text_size;

	bestval = kallsyms_symbol_value(&kallsyms->symtab[best]);

	/* Scan for closest preceding symbol, and next symbol. (ELF
	   starts real symbols at 1). */
	for (i = 1; i < kallsyms->num_symtab; i++) {
		const Elf_Sym *sym = &kallsyms->symtab[i];
		unsigned long thisval = kallsyms_symbol_value(sym);

		if (sym->st_shndx == SHN_UNDEF)
			continue;

		/* We ignore unnamed symbols: they're uninformative
		 * and inserted at a whim. */
		if (*kallsyms_symbol_name(kallsyms, i) == '\0'
		    || is_arm_mapping_symbol(kallsyms_symbol_name(kallsyms, i))
		    || is_cfi_typeid_symbol(kallsyms_symbol_name(kallsyms, i)))
			continue;

		if (thisval <= addr && thisval > bestval) {
			best = i;
			bestval = thisval;
		}
		if (thisval > addr && thisval < nextval)
			nextval = thisval;
	}

	if (!best)
		return NULL;

	if (size)
		*size = nextval - bestval;
	if (offset)
		*offset = addr - bestval;

	return kallsyms_symbol_name(kallsyms, best);
}

void * __weak dereference_module_function_descriptor(struct module *mod,
						     void *ptr)
{
	return ptr;
}

/* For kallsyms to ask for address resolution.  NULL means not found.  Careful
 * not to lock to avoid deadlock on oopses, simply disable preemption. */
const char *module_address_lookup(unsigned long addr,
			    unsigned long *size,
			    unsigned long *offset,
			    char **modname,
			    char *namebuf)
{
	const char *ret = NULL;
	struct module *mod;

	preempt_disable();
	mod = __module_address(addr);
	if (mod) {
		if (modname)
			*modname = mod->name;

		ret = find_kallsyms_symbol(mod, addr, size, offset);
	}
	/* Make a copy in here where it's safe */
	if (ret) {
		strncpy(namebuf, ret, KSYM_NAME_LEN - 1);
		ret = namebuf;
	}
	preempt_enable();

	return ret;
}

int lookup_module_symbol_name(unsigned long addr, char *symname)
{
	struct module *mod;

	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (mod->state == MODULE_STATE_UNFORMED)
			continue;
		if (within_module(addr, mod)) {
			const char *sym;

			sym = find_kallsyms_symbol(mod, addr, NULL, NULL);
			if (!sym)
				goto out;

			strlcpy(symname, sym, KSYM_NAME_LEN);
			preempt_enable();
			return 0;
		}
	}
out:
	preempt_enable();
	return -ERANGE;
}

int lookup_module_symbol_attrs(unsigned long addr, unsigned long *size,
			unsigned long *offset, char *modname, char *name)
{
	struct module *mod;

	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (mod->state == MODULE_STATE_UNFORMED)
			continue;
		if (within_module(addr, mod)) {
			const char *sym;

			sym = find_kallsyms_symbol(mod, addr, size, offset);
			if (!sym)
				goto out;
			if (modname)
				strlcpy(modname, mod->name, MODULE_NAME_LEN);
			if (name)
				strlcpy(name, sym, KSYM_NAME_LEN);
			preempt_enable();
			return 0;
		}
	}
out:
	preempt_enable();
	return -ERANGE;
}

int module_get_kallsym(unsigned int symnum, unsigned long *value, char *type,
			char *name, char *module_name, int *exported)
{
	struct module *mod;

	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		struct mod_kallsyms *kallsyms;

		if (mod->state == MODULE_STATE_UNFORMED)
			continue;
		kallsyms = rcu_dereference_sched(mod->kallsyms);
		if (symnum < kallsyms->num_symtab) {
			const Elf_Sym *sym = &kallsyms->symtab[symnum];

			*value = kallsyms_symbol_value(sym);
			*type = kallsyms->typetab[symnum];
			strlcpy(name, kallsyms_symbol_name(kallsyms, symnum), KSYM_NAME_LEN);
			strlcpy(module_name, mod->name, MODULE_NAME_LEN);
			*exported = is_exported(name, *value, mod);
			preempt_enable();
			return 0;
		}
		symnum -= kallsyms->num_symtab;
	}
	preempt_enable();
	return -ERANGE;
}

/* Given a module and name of symbol, find and return the symbol's value */
static unsigned long find_kallsyms_symbol_value(struct module *mod, const char *name)
{
	unsigned int i;
	struct mod_kallsyms *kallsyms = rcu_dereference_sched(mod->kallsyms);

	for (i = 0; i < kallsyms->num_symtab; i++) {
		const Elf_Sym *sym = &kallsyms->symtab[i];

		if (strcmp(name, kallsyms_symbol_name(kallsyms, i)) == 0 &&
		    sym->st_shndx != SHN_UNDEF)
			return kallsyms_symbol_value(sym);
	}
	return 0;
}

/* Look for this name: can be of form module:name. */
unsigned long module_kallsyms_lookup_name(const char *name)
{
	struct module *mod;
	char *colon;
	unsigned long ret = 0;

	/* Don't lock: we're in enough trouble already. */
	preempt_disable();
	if ((colon = strnchr(name, MODULE_NAME_LEN, ':')) != NULL) {
		if ((mod = find_module_all(name, colon - name, false)) != NULL)
			ret = find_kallsyms_symbol_value(mod, colon+1);
	} else {
		list_for_each_entry_rcu(mod, &modules, list) {
			if (mod->state == MODULE_STATE_UNFORMED)
				continue;
			if ((ret = find_kallsyms_symbol_value(mod, name)) != 0)
				break;
		}
	}
	preempt_enable();
	return ret;
}

int module_kallsyms_on_each_symbol(int (*fn)(void *, const char *,
					     struct module *, unsigned long),
				   void *data)
{
	struct module *mod;
	unsigned int i;
	int ret;

	module_assert_mutex();

	list_for_each_entry(mod, &modules, list) {
		/* We hold module_mutex: no need for rcu_dereference_sched */
		struct mod_kallsyms *kallsyms = mod->kallsyms;

		if (mod->state == MODULE_STATE_UNFORMED)
			continue;
		for (i = 0; i < kallsyms->num_symtab; i++) {
			const Elf_Sym *sym = &kallsyms->symtab[i];

			if (sym->st_shndx == SHN_UNDEF)
				continue;

			ret = fn(data, kallsyms_symbol_name(kallsyms, i),
				 mod, kallsyms_symbol_value(sym));
			if (ret != 0)
				return ret;
		}
	}
	return 0;
}
#endif /* CONFIG_KALLSYMS */

static void cfi_init(struct module *mod)
{
#ifdef CONFIG_CFI_CLANG
	initcall_t *init;
	exitcall_t *exit;

	rcu_read_lock_sched();
	mod->cfi_check = (cfi_check_fn)
		find_kallsyms_symbol_value(mod, "__cfi_check");
	init = (initcall_t *)
		find_kallsyms_symbol_value(mod, "__cfi_jt_init_module");
	exit = (exitcall_t *)
		find_kallsyms_symbol_value(mod, "__cfi_jt_cleanup_module");
	rcu_read_unlock_sched();

	/* Fix init/exit functions to point to the CFI jump table */
	if (init) mod->init = *init;
	if (exit) mod->exit = *exit;

	cfi_module_add(mod, module_addr_min);
#endif
}

static void cfi_cleanup(struct module *mod)
{
#ifdef CONFIG_CFI_CLANG
	cfi_module_remove(mod, module_addr_min);
#endif
}

/* Maximum number of characters written by module_flags() */
#define MODULE_FLAGS_BUF_SIZE (TAINT_FLAGS_COUNT + 4)

/* Keep in sync with MODULE_FLAGS_BUF_SIZE !!! */
static char *module_flags(struct module *mod, char *buf)
{
	int bx = 0;

	BUG_ON(mod->state == MODULE_STATE_UNFORMED);
	if (mod->taints ||
	    mod->state == MODULE_STATE_GOING ||
	    mod->state == MODULE_STATE_COMING) {
		buf[bx++] = '(';
		bx += module_flags_taint(mod, buf + bx);
		/* Show a - for module-is-being-unloaded */
		if (mod->state == MODULE_STATE_GOING)
			buf[bx++] = '-';
		/* Show a + for module-is-being-loaded */
		if (mod->state == MODULE_STATE_COMING)
			buf[bx++] = '+';
		buf[bx++] = ')';
	}
	buf[bx] = '\0';

	return buf;
}

#ifdef CONFIG_PROC_FS
/* Called by the /proc file system to return a list of modules. */
static void *m_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&module_mutex);
	return seq_list_start(&modules, *pos);
}

static void *m_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next(p, &modules, pos);
}

static void m_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&module_mutex);
}

static int m_show(struct seq_file *m, void *p)
{
	struct module *mod = list_entry(p, struct module, list);
	char buf[MODULE_FLAGS_BUF_SIZE];
	void *value;

	/* We always ignore unformed modules. */
	if (mod->state == MODULE_STATE_UNFORMED)
		return 0;

	seq_printf(m, "%s %u",
		   mod->name, mod->init_layout.size + mod->core_layout.size);
	print_unload_info(m, mod);

	/* Informative for users. */
	seq_printf(m, " %s",
		   mod->state == MODULE_STATE_GOING ? "Unloading" :
		   mod->state == MODULE_STATE_COMING ? "Loading" :
		   "Live");
	/* Used by oprofile and other similar tools. */
	value = m->private ? NULL : mod->core_layout.base;
	seq_printf(m, " 0x%px", value);

	/* Taints info */
	if (mod->taints)
		seq_printf(m, " %s", module_flags(mod, buf));

	seq_puts(m, "\n");
	return 0;
}

/* Format: modulename size refcount deps address

   Where refcount is a number or -, and deps is a comma-separated list
   of depends or -.
*/
static const struct seq_operations modules_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= m_show
};

/*
 * This also sets the "private" pointer to non-NULL if the
 * kernel pointers should be hidden (so you can just test
 * "m->private" to see if you should keep the values private).
 *
 * We use the same logic as for /proc/kallsyms.
 */
static int modules_open(struct inode *inode, struct file *file)
{
	int err = seq_open(file, &modules_op);

	if (!err) {
		struct seq_file *m = file->private_data;
		m->private = kallsyms_show_value(file->f_cred) ? NULL : (void *)8ul;
	}

	return err;
}

static const struct proc_ops modules_proc_ops = {
	.proc_flags	= PROC_ENTRY_PERMANENT,
	.proc_open	= modules_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
};

static int __init proc_modules_init(void)
{
	proc_create("modules", 0, NULL, &modules_proc_ops);
	return 0;
}
module_init(proc_modules_init);
#endif

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

/*
 * is_module_address - is this address inside a module?
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

/*
 * __module_address - get the module which contains an address.
 * @addr: the address.
 *
 * Must be called with preempt disabled or module mutex held so that
 * module doesn't get freed during this.
 */
struct module *__module_address(unsigned long addr)
{
	struct module *mod;

	if (addr < module_addr_min || addr > module_addr_max)
		return NULL;

	module_assert_mutex_or_preempt();

	mod = mod_find(addr);
	if (mod) {
		BUG_ON(!within_module(addr, mod));
		if (mod->state == MODULE_STATE_UNFORMED)
			mod = NULL;
	}
	return mod;
}

/*
 * is_module_text_address - is this address inside module code?
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

/*
 * __module_text_address - get the module whose code contains an address.
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
		pr_cont(" %s%s", mod->name, module_flags(mod, buf));
	}
	preempt_enable();
	if (last_unloaded_module[0])
		pr_cont(" [last unloaded: %s]", last_unloaded_module);
	pr_cont("\n");
}

#ifdef CONFIG_MODVERSIONS
/* Generate the signature for all relevant module structures here.
 * If these change, we don't want to try to parse the module. */
void module_layout(struct module *mod,
		   struct modversion_info *ver,
		   struct kernel_param *kp,
		   struct kernel_symbol *ks,
		   struct tracepoint * const *tp)
{
}
EXPORT_SYMBOL(module_layout);
#endif
