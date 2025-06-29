/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Module internals
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * Copyright (C) 2023 Luis Chamberlain <mcgrof@kernel.org>
 */

#include <linux/elf.h>
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/mm.h>

#ifndef ARCH_SHF_SMALL
#define ARCH_SHF_SMALL 0
#endif

/*
 * Use highest 4 bits of sh_entsize to store the mod_mem_type of this
 * section. This leaves 28 bits for offset on 32-bit systems, which is
 * about 256 MiB (WARN_ON_ONCE if we exceed that).
 */

#define SH_ENTSIZE_TYPE_BITS	4
#define SH_ENTSIZE_TYPE_SHIFT	(BITS_PER_LONG - SH_ENTSIZE_TYPE_BITS)
#define SH_ENTSIZE_TYPE_MASK	((1UL << SH_ENTSIZE_TYPE_BITS) - 1)
#define SH_ENTSIZE_OFFSET_MASK	((1UL << (BITS_PER_LONG - SH_ENTSIZE_TYPE_BITS)) - 1)

/* Maximum number of characters written by module_flags() */
#define MODULE_FLAGS_BUF_SIZE (TAINT_FLAGS_COUNT + 4)

struct kernel_symbol {
#ifdef CONFIG_HAVE_ARCH_PREL32_RELOCATIONS
	int value_offset;
	int name_offset;
	int namespace_offset;
#else
	unsigned long value;
	const char *name;
	const char *namespace;
#endif
};

extern struct mutex module_mutex;
extern struct list_head modules;

extern const struct module_attribute *const modinfo_attrs[];
extern const size_t modinfo_attrs_count;

/* Provided by the linker */
extern const struct kernel_symbol __start___ksymtab[];
extern const struct kernel_symbol __stop___ksymtab[];
extern const struct kernel_symbol __start___ksymtab_gpl[];
extern const struct kernel_symbol __stop___ksymtab_gpl[];
extern const u32 __start___kcrctab[];
extern const u32 __start___kcrctab_gpl[];

struct load_info {
	const char *name;
	/* pointer to module in temporary copy, freed at end of load_module() */
	struct module *mod;
	Elf_Ehdr *hdr;
	unsigned long len;
	Elf_Shdr *sechdrs;
	char *secstrings, *strtab;
	unsigned long symoffs, stroffs, init_typeoffs, core_typeoffs;
	bool sig_ok;
#ifdef CONFIG_KALLSYMS
	unsigned long mod_kallsyms_init_off;
#endif
#ifdef CONFIG_MODULE_DECOMPRESS
#ifdef CONFIG_MODULE_STATS
	unsigned long compressed_len;
#endif
	struct page **pages;
	unsigned int max_pages;
	unsigned int used_pages;
#endif
	struct {
		unsigned int sym;
		unsigned int str;
		unsigned int mod;
		unsigned int vers;
		unsigned int info;
		unsigned int pcpu;
		unsigned int vers_ext_crc;
		unsigned int vers_ext_name;
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
	const u32 *crc;
	const struct kernel_symbol *sym;
	enum mod_license license;
};

int mod_verify_sig(const void *mod, struct load_info *info);
int try_to_force_load(struct module *mod, const char *reason);
bool find_symbol(struct find_symbol_arg *fsa);
struct module *find_module_all(const char *name, size_t len, bool even_unformed);
int cmp_name(const void *name, const void *sym);
long module_get_offset_and_type(struct module *mod, enum mod_mem_type type,
				Elf_Shdr *sechdr, unsigned int section);
char *module_flags(struct module *mod, char *buf, bool show_state);
size_t module_flags_taint(unsigned long taints, char *buf);

char *module_next_tag_pair(char *string, unsigned long *secsize);

#define for_each_modinfo_entry(entry, info, name) \
	for (entry = get_modinfo(info, name); entry; entry = get_next_modinfo(info, name, entry))

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

/**
 * enum fail_dup_mod_reason - state at which a duplicate module was detected
 *
 * @FAIL_DUP_MOD_BECOMING: the module is read properly, passes all checks but
 * 	we've determined that another module with the same name is already loaded
 * 	or being processed on our &modules list. This happens on early_mod_check()
 * 	right before layout_and_allocate(). The kernel would have already
 * 	vmalloc()'d space for the entire module through finit_module(). If
 * 	decompression was used two vmap() spaces were used. These failures can
 * 	happen when userspace has not seen the module present on the kernel and
 * 	tries to load the module multiple times at same time.
 * @FAIL_DUP_MOD_LOAD: the module has been read properly, passes all validation
 *	checks and the kernel determines that the module was unique and because
 *	of this allocated yet another private kernel copy of the module space in
 *	layout_and_allocate() but after this determined in add_unformed_module()
 *	that another module with the same name is already loaded or being processed.
 *	These failures should be mitigated as much as possible and are indicative
 *	of really fast races in loading modules. Without module decompression
 *	they waste twice as much vmap space. With module decompression three
 *	times the module's size vmap space is wasted.
 */
enum fail_dup_mod_reason {
	FAIL_DUP_MOD_BECOMING = 0,
	FAIL_DUP_MOD_LOAD,
};

#ifdef CONFIG_MODULE_DEBUGFS
extern struct dentry *mod_debugfs_root;
#endif

#ifdef CONFIG_MODULE_STATS

#define mod_stat_add_long(count, var) atomic_long_add(count, var)
#define mod_stat_inc(name) atomic_inc(name)

extern atomic_long_t total_mod_size;
extern atomic_long_t total_text_size;
extern atomic_long_t invalid_kread_bytes;
extern atomic_long_t invalid_decompress_bytes;

extern atomic_t modcount;
extern atomic_t failed_kreads;
extern atomic_t failed_decompress;
struct mod_fail_load {
	struct list_head list;
	char name[MODULE_NAME_LEN];
	atomic_long_t count;
	unsigned long dup_fail_mask;
};

int try_add_failed_module(const char *name, enum fail_dup_mod_reason reason);
void mod_stat_bump_invalid(struct load_info *info, int flags);
void mod_stat_bump_becoming(struct load_info *info, int flags);

#else

#define mod_stat_add_long(name, var)
#define mod_stat_inc(name)

static inline int try_add_failed_module(const char *name,
					enum fail_dup_mod_reason reason)
{
	return 0;
}

static inline void mod_stat_bump_invalid(struct load_info *info, int flags)
{
}

static inline void mod_stat_bump_becoming(struct load_info *info, int flags)
{
}

#endif /* CONFIG_MODULE_STATS */

#ifdef CONFIG_MODULE_DEBUG_AUTOLOAD_DUPS
bool kmod_dup_request_exists_wait(char *module_name, bool wait, int *dup_ret);
void kmod_dup_request_announce(char *module_name, int ret);
#else
static inline bool kmod_dup_request_exists_wait(char *module_name, bool wait, int *dup_ret)
{
	return false;
}

static inline void kmod_dup_request_announce(char *module_name, int ret)
{
}
#endif

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
#ifdef CONFIG_ARCH_WANTS_MODULES_DATA_IN_VMALLOC
	unsigned long data_addr_min;
	unsigned long data_addr_max;
#endif
};

extern struct mod_tree_root mod_tree;

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

int module_enable_rodata_ro(const struct module *mod);
int module_enable_rodata_ro_after_init(const struct module *mod);
int module_enable_data_nx(const struct module *mod);
int module_enable_text_rox(const struct module *mod);
int module_enforce_rwx_sections(const Elf_Ehdr *hdr, const Elf_Shdr *sechdrs,
				const char *secstrings,
				const struct module *mod);
void module_mark_ro_after_init(const Elf_Ehdr *hdr, Elf_Shdr *sechdrs,
			       const char *secstrings);

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
		  const char *symname, struct module *mod, const u32 *crc);
void module_layout(struct module *mod, struct modversion_info *ver, struct kernel_param *kp,
		   struct kernel_symbol *ks, struct tracepoint * const *tp);
int check_modstruct_version(const struct load_info *info, struct module *mod);
int same_magic(const char *amagic, const char *bmagic, bool has_crcs);
struct modversion_info_ext {
	size_t remaining;
	const u32 *crc;
	const char *name;
};
void modversion_ext_start(const struct load_info *info, struct modversion_info_ext *ver);
void modversion_ext_advance(struct modversion_info_ext *ver);
#define for_each_modversion_info_ext(ver, info) \
	for (modversion_ext_start(info, &ver); ver.remaining > 0; modversion_ext_advance(&ver))
#else /* !CONFIG_MODVERSIONS */
static inline int check_version(const struct load_info *info,
				const char *symname,
				struct module *mod,
				const u32 *crc)
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
