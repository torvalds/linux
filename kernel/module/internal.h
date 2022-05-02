/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Module internals
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/elf.h>
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>

#ifndef ARCH_SHF_SMALL
#define ARCH_SHF_SMALL 0
#endif

/* If this is set, the section belongs in the init part of the module */
#define INIT_OFFSET_MASK (1UL << (BITS_PER_LONG - 1))
/* Maximum number of characters written by module_flags() */
#define MODULE_FLAGS_BUF_SIZE (TAINT_FLAGS_COUNT + 4)

#ifndef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
#define	data_layout core_layout
#endif

/*
 * Modules' sections will be aligned on page boundaries
 * to ensure complete separation of code and data, but
 * only when CONFIG_STRICT_MODULE_RWX=y
 */
#ifdef CONFIG_STRICT_MODULE_RWX
# define strict_align(X) PAGE_ALIGN(X)
#else
# define strict_align(X) (X)
#endif

extern struct mutex module_mutex;
extern struct list_head modules;

extern struct module_attribute *modinfo_attrs[];
extern size_t modinfo_attrs_count;

/* Provided by the linker */
extern const struct kernel_symbol __start___ksymtab[];
extern const struct kernel_symbol __stop___ksymtab[];
extern const struct kernel_symbol __start___ksymtab_gpl[];
extern const struct kernel_symbol __stop___ksymtab_gpl[];
extern const s32 __start___kcrctab[];
extern const s32 __start___kcrctab_gpl[];

struct load_info {
	const char *name;
	/* pointer to module in temporary copy, freed at end of load_module() */
	struct module *mod;
	Elf_Ehdr *hdr;
	unsigned long len;
	Elf_Shdr *sechdrs;
	char *secstrings, *strtab;
	unsigned long symoffs, stroffs, init_typeoffs, core_typeoffs;
	struct _ddebug *debug;
	unsigned int num_debug;
	bool sig_ok;
#ifdef CONFIG_KALLSYMS
	unsigned long mod_kallsyms_init_off;
#endif
#ifdef CONFIG_MODULE_DECOMPRESS
	struct page **pages;
	unsigned int max_pages;
	unsigned int used_pages;
#endif
	struct {
		unsigned int sym, str, mod, vers, info, pcpu;
	} index;
};

enum mod_license {
	NOT_GPL_ONLY,
	GPL_ONLY,
};

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

int mod_verify_sig(const void *mod, struct load_info *info);
int try_to_force_load(struct module *mod, const char *reason);
bool find_symbol(struct find_symbol_arg *fsa);
struct module *find_module_all(const char *name, size_t len, bool even_unformed);
int cmp_name(const void *name, const void *sym);
long module_get_offset(struct module *mod, unsigned int *size, Elf_Shdr *sechdr,
		       unsigned int section);
char *module_flags(struct module *mod, char *buf);
size_t module_flags_taint(unsigned long taints, char *buf);

static inline void module_assert_mutex_or_preempt(void)
{
#ifdef CONFIG_LOCKDEP
	if (unlikely(!debug_locks))
		return;

	WARN_ON_ONCE(!rcu_read_lock_sched_held() &&
		     !lockdep_is_held(&module_mutex));
#endif
}

static inline unsigned long kernel_symbol_value(const struct kernel_symbol *sym)
{
#ifdef CONFIG_HAVE_ARCH_PREL32_RELOCATIONS
	return (unsigned long)offset_to_ptr(&sym->value_offset);
#else
	return sym->value;
#endif
}

#ifdef CONFIG_LIVEPATCH
int copy_module_elf(struct module *mod, struct load_info *info);
void free_module_elf(struct module *mod);
#else /* !CONFIG_LIVEPATCH */
static inline int copy_module_elf(struct module *mod, struct load_info *info)
{
	return 0;
}

static inline void free_module_elf(struct module *mod) { }
#endif /* CONFIG_LIVEPATCH */

static inline bool set_livepatch_module(struct module *mod)
{
#ifdef CONFIG_LIVEPATCH
	mod->klp = true;
	return true;
#else
	return false;
#endif
}

#ifdef CONFIG_MODULE_UNLOAD_TAINT_TRACKING
struct mod_unload_taint {
	struct list_head list;
	char name[MODULE_NAME_LEN];
	unsigned long taints;
	u64 count;
};

int try_add_tainted_module(struct module *mod);
void print_unloaded_tainted_modules(void);
#else /* !CONFIG_MODULE_UNLOAD_TAINT_TRACKING */
static inline int try_add_tainted_module(struct module *mod)
{
	return 0;
}

static inline void print_unloaded_tainted_modules(void)
{
}
#endif /* CONFIG_MODULE_UNLOAD_TAINT_TRACKING */

#ifdef CONFIG_MODULE_DECOMPRESS
int module_decompress(struct load_info *info, const void *buf, size_t size);
void module_decompress_cleanup(struct load_info *info);
#else
static inline int module_decompress(struct load_info *info,
				    const void *buf, size_t size)
{
	return -EOPNOTSUPP;
}

static inline void module_decompress_cleanup(struct load_info *info)
{
}
#endif

struct mod_tree_root {
#ifdef CONFIG_MODULES_TREE_LOOKUP
	struct latch_tree_root root;
#endif
	unsigned long addr_min;
	unsigned long addr_max;
};

extern struct mod_tree_root mod_tree;
extern struct mod_tree_root mod_data_tree;

#ifdef CONFIG_MODULES_TREE_LOOKUP
void mod_tree_insert(struct module *mod);
void mod_tree_remove_init(struct module *mod);
void mod_tree_remove(struct module *mod);
struct module *mod_find(unsigned long addr, struct mod_tree_root *tree);
#else /* !CONFIG_MODULES_TREE_LOOKUP */

static inline void mod_tree_insert(struct module *mod) { }
static inline void mod_tree_remove_init(struct module *mod) { }
static inline void mod_tree_remove(struct module *mod) { }
static inline struct module *mod_find(unsigned long addr, struct mod_tree_root *tree)
{
	struct module *mod;

	list_for_each_entry_rcu(mod, &modules, list,
				lockdep_is_held(&module_mutex)) {
		if (within_module(addr, mod))
			return mod;
	}

	return NULL;
}
#endif /* CONFIG_MODULES_TREE_LOOKUP */

void module_enable_ro(const struct module *mod, bool after_init);
void module_enable_nx(const struct module *mod);
void module_enable_x(const struct module *mod);
int module_enforce_rwx_sections(Elf_Ehdr *hdr, Elf_Shdr *sechdrs,
				char *secstrings, struct module *mod);
bool module_check_misalignment(const struct module *mod);

#ifdef CONFIG_MODULE_SIG
int module_sig_check(struct load_info *info, int flags);
#else /* !CONFIG_MODULE_SIG */
static inline int module_sig_check(struct load_info *info, int flags)
{
	return 0;
}
#endif /* !CONFIG_MODULE_SIG */

#ifdef CONFIG_DEBUG_KMEMLEAK
void kmemleak_load_module(const struct module *mod, const struct load_info *info);
#else /* !CONFIG_DEBUG_KMEMLEAK */
static inline void kmemleak_load_module(const struct module *mod,
					const struct load_info *info) { }
#endif /* CONFIG_DEBUG_KMEMLEAK */

#ifdef CONFIG_KALLSYMS
void init_build_id(struct module *mod, const struct load_info *info);
void layout_symtab(struct module *mod, struct load_info *info);
void add_kallsyms(struct module *mod, const struct load_info *info);
unsigned long find_kallsyms_symbol_value(struct module *mod, const char *name);

static inline bool sect_empty(const Elf_Shdr *sect)
{
	return !(sect->sh_flags & SHF_ALLOC) || sect->sh_size == 0;
}
#else /* !CONFIG_KALLSYMS */
static inline void init_build_id(struct module *mod, const struct load_info *info) { }
static inline void layout_symtab(struct module *mod, struct load_info *info) { }
static inline void add_kallsyms(struct module *mod, const struct load_info *info) { }
#endif /* CONFIG_KALLSYMS */

#ifdef CONFIG_SYSFS
int mod_sysfs_setup(struct module *mod, const struct load_info *info,
		    struct kernel_param *kparam, unsigned int num_params);
void mod_sysfs_teardown(struct module *mod);
void init_param_lock(struct module *mod);
#else /* !CONFIG_SYSFS */
static inline int mod_sysfs_setup(struct module *mod,
			   	  const struct load_info *info,
			   	  struct kernel_param *kparam,
			   	  unsigned int num_params)
{
	return 0;
}

static inline void mod_sysfs_teardown(struct module *mod) { }
static inline void init_param_lock(struct module *mod) { }
#endif /* CONFIG_SYSFS */

#ifdef CONFIG_MODVERSIONS
int check_version(const struct load_info *info,
		  const char *symname, struct module *mod, const s32 *crc);
void module_layout(struct module *mod, struct modversion_info *ver, struct kernel_param *kp,
		   struct kernel_symbol *ks, struct tracepoint * const *tp);
int check_modstruct_version(const struct load_info *info, struct module *mod);
int same_magic(const char *amagic, const char *bmagic, bool has_crcs);
#else /* !CONFIG_MODVERSIONS */
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

static inline int same_magic(const char *amagic, const char *bmagic, bool has_crcs)
{
	return strcmp(amagic, bmagic) == 0;
}
#endif /* CONFIG_MODVERSIONS */
