#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H
/*
 * Dynamic loading of modules into the kernel.
 *
 * Rewritten by Richard Henderson <rth@tamu.edu> Dec 1996
 * Rewritten again by Rusty Russell, 2002
 */
#include <linux/list.h>
#include <linux/stat.h>
#include <linux/compiler.h>
#include <linux/cache.h>
#include <linux/kmod.h>
#include <linux/elf.h>
#include <linux/stringify.h>
#include <linux/kobject.h>
#include <linux/moduleparam.h>
#include <linux/jump_label.h>
#include <linux/export.h>
#include <linux/rbtree_latch.h>

#include <linux/percpu.h>
#include <asm/module.h>

/* In stripped ARM and x86-64 modules, ~ is surprisingly rare. */
#define MODULE_SIG_STRING "~Module signature appended~\n"

/* Not Yet Implemented */
#define MODULE_SUPPORTED_DEVICE(name)

#define MODULE_NAME_LEN MAX_PARAM_PREFIX_LEN

struct modversion_info {
	unsigned long crc;
	char name[MODULE_NAME_LEN];
};

struct module;

struct module_kobject {
	struct kobject kobj;
	struct module *mod;
	struct kobject *drivers_dir;
	struct module_param_attrs *mp;
	struct completion *kobj_completion;
};

struct module_attribute {
	struct attribute attr;
	ssize_t (*show)(struct module_attribute *, struct module_kobject *,
			char *);
	ssize_t (*store)(struct module_attribute *, struct module_kobject *,
			 const char *, size_t count);
	void (*setup)(struct module *, const char *);
	int (*test)(struct module *);
	void (*free)(struct module *);
};

struct module_version_attribute {
	struct module_attribute mattr;
	const char *module_name;
	const char *version;
} __attribute__ ((__aligned__(sizeof(void *))));

extern ssize_t __modver_version_show(struct module_attribute *,
				     struct module_kobject *, char *);

extern struct module_attribute module_uevent;

/* These are either module local, or the kernel's dummy ones. */
extern int init_module(void);
extern void cleanup_module(void);

/* Archs provide a method of finding the correct exception table. */
struct exception_table_entry;

const struct exception_table_entry *
search_extable(const struct exception_table_entry *first,
	       const struct exception_table_entry *last,
	       unsigned long value);
void sort_extable(struct exception_table_entry *start,
		  struct exception_table_entry *finish);
void sort_main_extable(void);
void trim_init_extable(struct module *m);

/* Generic info of form tag = "info" */
#define MODULE_INFO(tag, info) __MODULE_INFO(tag, tag, info)

/* For userspace: you can also call me... */
#define MODULE_ALIAS(_alias) MODULE_INFO(alias, _alias)

/* Soft module dependencies. See man modprobe.d for details.
 * Example: MODULE_SOFTDEP("pre: module-foo module-bar post: module-baz")
 */
#define MODULE_SOFTDEP(_softdep) MODULE_INFO(softdep, _softdep)

/*
 * The following license idents are currently accepted as indicating free
 * software modules
 *
 *	"GPL"				[GNU Public License v2 or later]
 *	"GPL v2"			[GNU Public License v2]
 *	"GPL and additional rights"	[GNU Public License v2 rights and more]
 *	"Dual BSD/GPL"			[GNU Public License v2
 *					 or BSD license choice]
 *	"Dual MIT/GPL"			[GNU Public License v2
 *					 or MIT license choice]
 *	"Dual MPL/GPL"			[GNU Public License v2
 *					 or Mozilla license choice]
 *
 * The following other idents are available
 *
 *	"Proprietary"			[Non free products]
 *
 * There are dual licensed components, but when running with Linux it is the
 * GPL that is relevant so this is a non issue. Similarly LGPL linked with GPL
 * is a GPL combined work.
 *
 * This exists for several reasons
 * 1.	So modinfo can show license info for users wanting to vet their setup
 *	is free
 * 2.	So the community can ignore bug reports including proprietary modules
 * 3.	So vendors can do likewise based on their own policies
 */
#define MODULE_LICENSE(_license) MODULE_INFO(license, _license)

/*
 * Author(s), use "Name <email>" or just "Name", for multiple
 * authors use multiple MODULE_AUTHOR() statements/lines.
 */
#define MODULE_AUTHOR(_author) MODULE_INFO(author, _author)

/* What your module does. */
#define MODULE_DESCRIPTION(_description) MODULE_INFO(description, _description)

#ifdef MODULE
/* Creates an alias so file2alias.c can find device table. */
#define MODULE_DEVICE_TABLE(type, name)					\
extern const typeof(name) __mod_##type##__##name##_device_table		\
  __attribute__ ((unused, alias(__stringify(name))))
#else  /* !MODULE */
#define MODULE_DEVICE_TABLE(type, name)
#endif

/* Version of form [<epoch>:]<version>[-<extra-version>].
 * Or for CVS/RCS ID version, everything but the number is stripped.
 * <epoch>: A (small) unsigned integer which allows you to start versions
 * anew. If not mentioned, it's zero.  eg. "2:1.0" is after
 * "1:2.0".

 * <version>: The <version> may contain only alphanumerics and the
 * character `.'.  Ordered by numeric sort for numeric parts,
 * ascii sort for ascii parts (as per RPM or DEB algorithm).

 * <extraversion>: Like <version>, but inserted for local
 * customizations, eg "rh3" or "rusty1".

 * Using this automatically adds a checksum of the .c files and the
 * local headers in "srcversion".
 */

#if defined(MODULE) || !defined(CONFIG_SYSFS)
#define MODULE_VERSION(_version) MODULE_INFO(version, _version)
#else
#define MODULE_VERSION(_version)					\
	static struct module_version_attribute ___modver_attr = {	\
		.mattr	= {						\
			.attr	= {					\
				.name	= "version",			\
				.mode	= S_IRUGO,			\
			},						\
			.show	= __modver_version_show,		\
		},							\
		.module_name	= KBUILD_MODNAME,			\
		.version	= _version,				\
	};								\
	static const struct module_version_attribute			\
	__used __attribute__ ((__section__ ("__modver")))		\
	* __moduleparam_const __modver_attr = &___modver_attr
#endif

/* Optional firmware file (or files) needed by the module
 * format is simply firmware file name.  Multiple firmware
 * files require multiple MODULE_FIRMWARE() specifiers */
#define MODULE_FIRMWARE(_firmware) MODULE_INFO(firmware, _firmware)

/* Given an address, look for it in the exception tables */
const struct exception_table_entry *search_exception_tables(unsigned long add);

struct notifier_block;

#ifdef CONFIG_MODULES

extern int modules_disabled; /* for sysctl */
/* Get/put a kernel symbol (calls must be symmetric) */
void *__symbol_get(const char *symbol);
void *__symbol_get_gpl(const char *symbol);
#define symbol_get(x) ((typeof(&x))(__symbol_get(VMLINUX_SYMBOL_STR(x))))

/* modules using other modules: kdb wants to see this. */
struct module_use {
	struct list_head source_list;
	struct list_head target_list;
	struct module *source, *target;
};

enum module_state {
	MODULE_STATE_LIVE,	/* Normal state. */
	MODULE_STATE_COMING,	/* Full formed, running module_init. */
	MODULE_STATE_GOING,	/* Going away. */
	MODULE_STATE_UNFORMED,	/* Still setting it up. */
};

struct module;

struct mod_tree_node {
	struct module *mod;
	struct latch_tree_node node;
};

struct module {
	enum module_state state;

	/* Member of list of modules */
	struct list_head list;

	/* Unique handle for this module */
	char name[MODULE_NAME_LEN];

	/* Sysfs stuff. */
	struct module_kobject mkobj;
	struct module_attribute *modinfo_attrs;
	const char *version;
	const char *srcversion;
	struct kobject *holders_dir;

	/* Exported symbols */
	const struct kernel_symbol *syms;
	const unsigned long *crcs;
	unsigned int num_syms;

	/* Kernel parameters. */
#ifdef CONFIG_SYSFS
	struct mutex param_lock;
#endif
	struct kernel_param *kp;
	unsigned int num_kp;

	/* GPL-only exported symbols. */
	unsigned int num_gpl_syms;
	const struct kernel_symbol *gpl_syms;
	const unsigned long *gpl_crcs;

#ifdef CONFIG_UNUSED_SYMBOLS
	/* unused exported symbols. */
	const struct kernel_symbol *unused_syms;
	const unsigned long *unused_crcs;
	unsigned int num_unused_syms;

	/* GPL-only, unused exported symbols. */
	unsigned int num_unused_gpl_syms;
	const struct kernel_symbol *unused_gpl_syms;
	const unsigned long *unused_gpl_crcs;
#endif

#ifdef CONFIG_MODULE_SIG
	/* Signature was verified. */
	bool sig_ok;
#endif

	bool async_probe_requested;

	/* symbols that will be GPL-only in the near future. */
	const struct kernel_symbol *gpl_future_syms;
	const unsigned long *gpl_future_crcs;
	unsigned int num_gpl_future_syms;

	/* Exception table */
	unsigned int num_exentries;
	struct exception_table_entry *extable;

	/* Startup function. */
	int (*init)(void);

	/*
	 * If this is non-NULL, vfree() after init() returns.
	 *
	 * Cacheline align here, such that:
	 *   module_init, module_core, init_size, core_size,
	 *   init_text_size, core_text_size and mtn_core::{mod,node[0]}
	 * are on the same cacheline.
	 */
	void *module_init	____cacheline_aligned;

	/* Here is the actual code + data, vfree'd on unload. */
	void *module_core;

	/* Here are the sizes of the init and core sections */
	unsigned int init_size, core_size;

	/* The size of the executable code in each section.  */
	unsigned int init_text_size, core_text_size;

#ifdef CONFIG_MODULES_TREE_LOOKUP
	/*
	 * We want mtn_core::{mod,node[0]} to be in the same cacheline as the
	 * above entries such that a regular lookup will only touch one
	 * cacheline.
	 */
	struct mod_tree_node	mtn_core;
	struct mod_tree_node	mtn_init;
#endif

	/* Size of RO sections of the module (text+rodata) */
	unsigned int init_ro_size, core_ro_size;

	/* Arch-specific module values */
	struct mod_arch_specific arch;

	unsigned int taints;	/* same bits as kernel:tainted */

#ifdef CONFIG_GENERIC_BUG
	/* Support for BUG */
	unsigned num_bugs;
	struct list_head bug_list;
	struct bug_entry *bug_table;
#endif

#ifdef CONFIG_KALLSYMS
	/*
	 * We keep the symbol and string tables for kallsyms.
	 * The core_* fields below are temporary, loader-only (they
	 * could really be discarded after module init).
	 */
	Elf_Sym *symtab, *core_symtab;
	unsigned int num_symtab, core_num_syms;
	char *strtab, *core_strtab;

	/* Section attributes */
	struct module_sect_attrs *sect_attrs;

	/* Notes attributes */
	struct module_notes_attrs *notes_attrs;
#endif

	/* The command line arguments (may be mangled).  People like
	   keeping pointers to this stuff */
	char *args;

#ifdef CONFIG_SMP
	/* Per-cpu data. */
	void __percpu *percpu;
	unsigned int percpu_size;
#endif

#ifdef CONFIG_TRACEPOINTS
	unsigned int num_tracepoints;
	struct tracepoint * const *tracepoints_ptrs;
#endif
#ifdef HAVE_JUMP_LABEL
	struct jump_entry *jump_entries;
	unsigned int num_jump_entries;
#endif
#ifdef CONFIG_TRACING
	unsigned int num_trace_bprintk_fmt;
	const char **trace_bprintk_fmt_start;
#endif
#ifdef CONFIG_EVENT_TRACING
	struct trace_event_call **trace_events;
	unsigned int num_trace_events;
	struct trace_enum_map **trace_enums;
	unsigned int num_trace_enums;
#endif
#ifdef CONFIG_FTRACE_MCOUNT_RECORD
	unsigned int num_ftrace_callsites;
	unsigned long *ftrace_callsites;
#endif

#ifdef CONFIG_LIVEPATCH
	bool klp_alive;
#endif

#ifdef CONFIG_MODULE_UNLOAD
	/* What modules depend on me? */
	struct list_head source_list;
	/* What modules do I depend on? */
	struct list_head target_list;

	/* Destruction function. */
	void (*exit)(void);

	atomic_t refcnt;
#endif

#ifdef CONFIG_CONSTRUCTORS
	/* Constructor functions. */
	ctor_fn_t *ctors;
	unsigned int num_ctors;
#endif
} ____cacheline_aligned;
#ifndef MODULE_ARCH_INIT
#define MODULE_ARCH_INIT {}
#endif

extern struct mutex module_mutex;

/* FIXME: It'd be nice to isolate modules during init, too, so they
   aren't used before they (may) fail.  But presently too much code
   (IDE & SCSI) require entry into the module during init.*/
static inline int module_is_live(struct module *mod)
{
	return mod->state != MODULE_STATE_GOING;
}

struct module *__module_text_address(unsigned long addr);
struct module *__module_address(unsigned long addr);
bool is_module_address(unsigned long addr);
bool is_module_percpu_address(unsigned long addr);
bool is_module_text_address(unsigned long addr);

static inline bool within_module_core(unsigned long addr,
				      const struct module *mod)
{
	return (unsigned long)mod->module_core <= addr &&
	       addr < (unsigned long)mod->module_core + mod->core_size;
}

static inline bool within_module_init(unsigned long addr,
				      const struct module *mod)
{
	return (unsigned long)mod->module_init <= addr &&
	       addr < (unsigned long)mod->module_init + mod->init_size;
}

static inline bool within_module(unsigned long addr, const struct module *mod)
{
	return within_module_init(addr, mod) || within_module_core(addr, mod);
}

/* Search for module by name: must hold module_mutex. */
struct module *find_module(const char *name);

struct symsearch {
	const struct kernel_symbol *start, *stop;
	const unsigned long *crcs;
	enum {
		NOT_GPL_ONLY,
		GPL_ONLY,
		WILL_BE_GPL_ONLY,
	} licence;
	bool unused;
};

/*
 * Search for an exported symbol by name.
 *
 * Must be called with module_mutex held or preemption disabled.
 */
const struct kernel_symbol *find_symbol(const char *name,
					struct module **owner,
					const unsigned long **crc,
					bool gplok,
					bool warn);

/*
 * Walk the exported symbol table
 *
 * Must be called with module_mutex held or preemption disabled.
 */
bool each_symbol_section(bool (*fn)(const struct symsearch *arr,
				    struct module *owner,
				    void *data), void *data);

/* Returns 0 and fills in value, defined and namebuf, or -ERANGE if
   symnum out of range. */
int module_get_kallsym(unsigned int symnum, unsigned long *value, char *type,
			char *name, char *module_name, int *exported);

/* Look for this name: can be of form module:name. */
unsigned long module_kallsyms_lookup_name(const char *name);

int module_kallsyms_on_each_symbol(int (*fn)(void *, const char *,
					     struct module *, unsigned long),
				   void *data);

extern void __module_put_and_exit(struct module *mod, long code)
	__attribute__((noreturn));
#define module_put_and_exit(code) __module_put_and_exit(THIS_MODULE, code)

#ifdef CONFIG_MODULE_UNLOAD
int module_refcount(struct module *mod);
void __symbol_put(const char *symbol);
#define symbol_put(x) __symbol_put(VMLINUX_SYMBOL_STR(x))
void symbol_put_addr(void *addr);

/* Sometimes we know we already have a refcount, and it's easier not
   to handle the error case (which only happens with rmmod --wait). */
extern void __module_get(struct module *module);

/* This is the Right Way to get a module: if it fails, it's being removed,
 * so pretend it's not there. */
extern bool try_module_get(struct module *module);

extern void module_put(struct module *module);

#else /*!CONFIG_MODULE_UNLOAD*/
static inline int try_module_get(struct module *module)
{
	return !module || module_is_live(module);
}
static inline void module_put(struct module *module)
{
}
static inline void __module_get(struct module *module)
{
}
#define symbol_put(x) do { } while (0)
#define symbol_put_addr(p) do { } while (0)

#endif /* CONFIG_MODULE_UNLOAD */
int ref_module(struct module *a, struct module *b);

/* This is a #define so the string doesn't get put in every .o file */
#define module_name(mod)			\
({						\
	struct module *__mod = (mod);		\
	__mod ? __mod->name : "kernel";		\
})

/* For kallsyms to ask for address resolution.  namebuf should be at
 * least KSYM_NAME_LEN long: a pointer to namebuf is returned if
 * found, otherwise NULL. */
const char *module_address_lookup(unsigned long addr,
			    unsigned long *symbolsize,
			    unsigned long *offset,
			    char **modname,
			    char *namebuf);
int lookup_module_symbol_name(unsigned long addr, char *symname);
int lookup_module_symbol_attrs(unsigned long addr, unsigned long *size, unsigned long *offset, char *modname, char *name);

/* For extable.c to search modules' exception tables. */
const struct exception_table_entry *search_module_extables(unsigned long addr);

int register_module_notifier(struct notifier_block *nb);
int unregister_module_notifier(struct notifier_block *nb);

extern void print_modules(void);

static inline bool module_requested_async_probing(struct module *module)
{
	return module && module->async_probe_requested;
}

#else /* !CONFIG_MODULES... */

/* Given an address, look for it in the exception tables. */
static inline const struct exception_table_entry *
search_module_extables(unsigned long addr)
{
	return NULL;
}

static inline struct module *__module_address(unsigned long addr)
{
	return NULL;
}

static inline struct module *__module_text_address(unsigned long addr)
{
	return NULL;
}

static inline bool is_module_address(unsigned long addr)
{
	return false;
}

static inline bool is_module_percpu_address(unsigned long addr)
{
	return false;
}

static inline bool is_module_text_address(unsigned long addr)
{
	return false;
}

/* Get/put a kernel symbol (calls should be symmetric) */
#define symbol_get(x) ({ extern typeof(x) x __attribute__((weak)); &(x); })
#define symbol_put(x) do { } while (0)
#define symbol_put_addr(x) do { } while (0)

static inline void __module_get(struct module *module)
{
}

static inline int try_module_get(struct module *module)
{
	return 1;
}

static inline void module_put(struct module *module)
{
}

#define module_name(mod) "kernel"

/* For kallsyms to ask for address resolution.  NULL means not found. */
static inline const char *module_address_lookup(unsigned long addr,
					  unsigned long *symbolsize,
					  unsigned long *offset,
					  char **modname,
					  char *namebuf)
{
	return NULL;
}

static inline int lookup_module_symbol_name(unsigned long addr, char *symname)
{
	return -ERANGE;
}

static inline int lookup_module_symbol_attrs(unsigned long addr, unsigned long *size, unsigned long *offset, char *modname, char *name)
{
	return -ERANGE;
}

static inline int module_get_kallsym(unsigned int symnum, unsigned long *value,
					char *type, char *name,
					char *module_name, int *exported)
{
	return -ERANGE;
}

static inline unsigned long module_kallsyms_lookup_name(const char *name)
{
	return 0;
}

static inline int module_kallsyms_on_each_symbol(int (*fn)(void *, const char *,
							   struct module *,
							   unsigned long),
						 void *data)
{
	return 0;
}

static inline int register_module_notifier(struct notifier_block *nb)
{
	/* no events will happen anyway, so this can always succeed */
	return 0;
}

static inline int unregister_module_notifier(struct notifier_block *nb)
{
	return 0;
}

#define module_put_and_exit(code) do_exit(code)

static inline void print_modules(void)
{
}

static inline bool module_requested_async_probing(struct module *module)
{
	return false;
}

#endif /* CONFIG_MODULES */

#ifdef CONFIG_SYSFS
extern struct kset *module_kset;
extern struct kobj_type module_ktype;
extern int module_sysfs_initialized;
#endif /* CONFIG_SYSFS */

#define symbol_request(x) try_then_request_module(symbol_get(x), "symbol:" #x)

/* BELOW HERE ALL THESE ARE OBSOLETE AND WILL VANISH */

#define __MODULE_STRING(x) __stringify(x)

#ifdef CONFIG_DEBUG_SET_MODULE_RONX
extern void set_all_modules_text_rw(void);
extern void set_all_modules_text_ro(void);
#else
static inline void set_all_modules_text_rw(void) { }
static inline void set_all_modules_text_ro(void) { }
#endif

#ifdef CONFIG_GENERIC_BUG
void module_bug_finalize(const Elf_Ehdr *, const Elf_Shdr *,
			 struct module *);
void module_bug_cleanup(struct module *);

#else	/* !CONFIG_GENERIC_BUG */

static inline void module_bug_finalize(const Elf_Ehdr *hdr,
					const Elf_Shdr *sechdrs,
					struct module *mod)
{
}
static inline void module_bug_cleanup(struct module *mod) {}
#endif	/* CONFIG_GENERIC_BUG */

#ifdef CONFIG_MODULE_SIG
static inline bool module_sig_ok(struct module *module)
{
	return module->sig_ok;
}
#else	/* !CONFIG_MODULE_SIG */
static inline bool module_sig_ok(struct module *module)
{
	return true;
}
#endif	/* CONFIG_MODULE_SIG */

#endif /* _LINUX_MODULE_H */
