#ifndef __PERF_SYMBOL
#define __PERF_SYMBOL 1

#include <linux/types.h>
#include <stdbool.h>
#include <stdint.h>
#include "map.h"
#include "../perf.h"
#include <linux/list.h>
#include <linux/rbtree.h>
#include <stdio.h>
#include <byteswap.h>

#ifdef HAVE_CPLUS_DEMANGLE
extern char *cplus_demangle(const char *, int);

static inline char *bfd_demangle(void __used *v, const char *c, int i)
{
	return cplus_demangle(c, i);
}
#else
#ifdef NO_DEMANGLE
static inline char *bfd_demangle(void __used *v, const char __used *c,
				 int __used i)
{
	return NULL;
}
#else
#include <bfd.h>
#endif
#endif

int hex2u64(const char *ptr, u64 *val);
char *strxfrchar(char *s, char from, char to);

/*
 * libelf 0.8.x and earlier do not support ELF_C_READ_MMAP;
 * for newer versions we can use mmap to reduce memory usage:
 */
#ifdef LIBELF_NO_MMAP
# define PERF_ELF_C_READ_MMAP ELF_C_READ
#else
# define PERF_ELF_C_READ_MMAP ELF_C_READ_MMAP
#endif

#ifndef DMGL_PARAMS
#define DMGL_PARAMS      (1 << 0)       /* Include function args */
#define DMGL_ANSI        (1 << 1)       /* Include const, volatile, etc */
#endif

#define BUILD_ID_SIZE 20

/** struct symbol - symtab entry
 *
 * @ignore - resolvable but tools ignore it (e.g. idle routines)
 */
struct symbol {
	struct rb_node	rb_node;
	u64		start;
	u64		end;
	u16		namelen;
	u8		binding;
	bool		ignore;
	char		name[0];
};

void symbol__delete(struct symbol *sym);

static inline size_t symbol__size(const struct symbol *sym)
{
	return sym->end - sym->start + 1;
}

struct strlist;

struct symbol_conf {
	unsigned short	priv_size;
	unsigned short	nr_events;
	bool		try_vmlinux_path,
			show_kernel_path,
			use_modules,
			sort_by_name,
			show_nr_samples,
			show_total_period,
			use_callchain,
			exclude_other,
			show_cpu_utilization,
			initialized,
			kptr_restrict,
			annotate_asm_raw,
			annotate_src;
	const char	*vmlinux_name,
			*kallsyms_name,
			*source_prefix,
			*field_sep;
	const char	*default_guest_vmlinux_name,
			*default_guest_kallsyms,
			*default_guest_modules;
	const char	*guestmount;
	const char	*dso_list_str,
			*comm_list_str,
			*sym_list_str,
			*col_width_list_str;
       struct strlist	*dso_list,
			*comm_list,
			*sym_list,
			*dso_from_list,
			*dso_to_list,
			*sym_from_list,
			*sym_to_list;
	const char	*symfs;
};

extern struct symbol_conf symbol_conf;

static inline void *symbol__priv(struct symbol *sym)
{
	return ((void *)sym) - symbol_conf.priv_size;
}

struct ref_reloc_sym {
	const char	*name;
	u64		addr;
	u64		unrelocated_addr;
};

struct map_symbol {
	struct map    *map;
	struct symbol *sym;
	bool	      unfolded;
	bool	      has_children;
};

struct addr_map_symbol {
	struct map    *map;
	struct symbol *sym;
	u64	      addr;
	u64	      al_addr;
};

struct branch_info {
	struct addr_map_symbol from;
	struct addr_map_symbol to;
	struct branch_flags flags;
};

struct addr_location {
	struct thread *thread;
	struct map    *map;
	struct symbol *sym;
	u64	      addr;
	char	      level;
	bool	      filtered;
	u8	      cpumode;
	s32	      cpu;
};

enum dso_kernel_type {
	DSO_TYPE_USER = 0,
	DSO_TYPE_KERNEL,
	DSO_TYPE_GUEST_KERNEL
};

enum dso_swap_type {
	DSO_SWAP__UNSET,
	DSO_SWAP__NO,
	DSO_SWAP__YES,
};

struct dso {
	struct list_head node;
	struct rb_root	 symbols[MAP__NR_TYPES];
	struct rb_root	 symbol_names[MAP__NR_TYPES];
	enum dso_kernel_type	kernel;
	enum dso_swap_type	needs_swap;
	u8		 adjust_symbols:1;
	u8		 has_build_id:1;
	u8		 hit:1;
	u8		 annotate_warned:1;
	u8		 sname_alloc:1;
	u8		 lname_alloc:1;
	unsigned char	 symtab_type;
	u8		 sorted_by_name;
	u8		 loaded;
	u8		 build_id[BUILD_ID_SIZE];
	const char	 *short_name;
	char	 	 *long_name;
	u16		 long_name_len;
	u16		 short_name_len;
	char		 name[0];
};

#define DSO__SWAP(dso, type, val)			\
({							\
	type ____r = val;				\
	BUG_ON(dso->needs_swap == DSO_SWAP__UNSET);	\
	if (dso->needs_swap == DSO_SWAP__YES) {		\
		switch (sizeof(____r)) {		\
		case 2:					\
			____r = bswap_16(val);		\
			break;				\
		case 4:					\
			____r = bswap_32(val);		\
			break;				\
		case 8:					\
			____r = bswap_64(val);		\
			break;				\
		default:				\
			BUG_ON(1);			\
		}					\
	}						\
	____r;						\
})

struct dso *dso__new(const char *name);
void dso__delete(struct dso *dso);

int dso__name_len(const struct dso *dso);

bool dso__loaded(const struct dso *dso, enum map_type type);
bool dso__sorted_by_name(const struct dso *dso, enum map_type type);

static inline void dso__set_loaded(struct dso *dso, enum map_type type)
{
	dso->loaded |= (1 << type);
}

void dso__sort_by_name(struct dso *dso, enum map_type type);

struct dso *__dsos__findnew(struct list_head *head, const char *name);

int dso__load(struct dso *dso, struct map *map, symbol_filter_t filter);
int dso__load_vmlinux(struct dso *dso, struct map *map,
		      const char *vmlinux, symbol_filter_t filter);
int dso__load_vmlinux_path(struct dso *dso, struct map *map,
			   symbol_filter_t filter);
int dso__load_kallsyms(struct dso *dso, const char *filename, struct map *map,
		       symbol_filter_t filter);
int machine__load_kallsyms(struct machine *machine, const char *filename,
			   enum map_type type, symbol_filter_t filter);
int machine__load_vmlinux_path(struct machine *machine, enum map_type type,
			       symbol_filter_t filter);

size_t __dsos__fprintf(struct list_head *head, FILE *fp);

size_t machine__fprintf_dsos_buildid(struct machine *machine,
				     FILE *fp, bool with_hits);
size_t machines__fprintf_dsos(struct rb_root *machines, FILE *fp);
size_t machines__fprintf_dsos_buildid(struct rb_root *machines,
				      FILE *fp, bool with_hits);
size_t dso__fprintf_buildid(struct dso *dso, FILE *fp);
size_t dso__fprintf_symbols_by_name(struct dso *dso,
				    enum map_type type, FILE *fp);
size_t dso__fprintf(struct dso *dso, enum map_type type, FILE *fp);

enum symtab_type {
	SYMTAB__KALLSYMS = 0,
	SYMTAB__GUEST_KALLSYMS,
	SYMTAB__JAVA_JIT,
	SYMTAB__BUILD_ID_CACHE,
	SYMTAB__FEDORA_DEBUGINFO,
	SYMTAB__UBUNTU_DEBUGINFO,
	SYMTAB__BUILDID_DEBUGINFO,
	SYMTAB__SYSTEM_PATH_DSO,
	SYMTAB__GUEST_KMODULE,
	SYMTAB__SYSTEM_PATH_KMODULE,
	SYMTAB__NOT_FOUND,
};

char dso__symtab_origin(const struct dso *dso);
void dso__set_long_name(struct dso *dso, char *name);
void dso__set_build_id(struct dso *dso, void *build_id);
void dso__read_running_kernel_build_id(struct dso *dso,
				       struct machine *machine);
struct map *dso__new_map(const char *name);
struct symbol *dso__find_symbol(struct dso *dso, enum map_type type,
				u64 addr);
struct symbol *dso__find_symbol_by_name(struct dso *dso, enum map_type type,
					const char *name);

int filename__read_build_id(const char *filename, void *bf, size_t size);
int sysfs__read_build_id(const char *filename, void *bf, size_t size);
bool __dsos__read_build_ids(struct list_head *head, bool with_hits);
int build_id__sprintf(const u8 *build_id, int len, char *bf);
int kallsyms__parse(const char *filename, void *arg,
		    int (*process_symbol)(void *arg, const char *name,
					  char type, u64 start, u64 end));

void machine__destroy_kernel_maps(struct machine *machine);
int __machine__create_kernel_maps(struct machine *machine, struct dso *kernel);
int machine__create_kernel_maps(struct machine *machine);

int machines__create_kernel_maps(struct rb_root *machines, pid_t pid);
int machines__create_guest_kernel_maps(struct rb_root *machines);
void machines__destroy_guest_kernel_maps(struct rb_root *machines);

int symbol__init(void);
void symbol__exit(void);
size_t symbol__fprintf_symname_offs(const struct symbol *sym,
				    const struct addr_location *al, FILE *fp);
size_t symbol__fprintf_symname(const struct symbol *sym, FILE *fp);
bool symbol_type__is_a(char symbol_type, enum map_type map_type);

size_t machine__fprintf_vmlinux_path(struct machine *machine, FILE *fp);

#endif /* __PERF_SYMBOL */
