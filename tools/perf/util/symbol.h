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
#include <libgen.h>

#ifndef NO_LIBELF_SUPPORT
#include <libelf.h>
#include <gelf.h>
#include <elf.h>
#endif

#ifdef HAVE_CPLUS_DEMANGLE
extern char *cplus_demangle(const char *, int);

static inline char *bfd_demangle(void __maybe_unused *v, const char *c, int i)
{
	return cplus_demangle(c, i);
}
#else
#ifdef NO_DEMANGLE
static inline char *bfd_demangle(void __maybe_unused *v,
				 const char __maybe_unused *c,
				 int __maybe_unused i)
{
	return NULL;
}
#else
#define PACKAGE 'perf'
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

enum dso_binary_type {
	DSO_BINARY_TYPE__KALLSYMS = 0,
	DSO_BINARY_TYPE__GUEST_KALLSYMS,
	DSO_BINARY_TYPE__VMLINUX,
	DSO_BINARY_TYPE__GUEST_VMLINUX,
	DSO_BINARY_TYPE__JAVA_JIT,
	DSO_BINARY_TYPE__DEBUGLINK,
	DSO_BINARY_TYPE__BUILD_ID_CACHE,
	DSO_BINARY_TYPE__FEDORA_DEBUGINFO,
	DSO_BINARY_TYPE__UBUNTU_DEBUGINFO,
	DSO_BINARY_TYPE__BUILDID_DEBUGINFO,
	DSO_BINARY_TYPE__SYSTEM_PATH_DSO,
	DSO_BINARY_TYPE__GUEST_KMODULE,
	DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE,
	DSO_BINARY_TYPE__NOT_FOUND,
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

#define DSO__DATA_CACHE_SIZE 4096
#define DSO__DATA_CACHE_MASK ~(DSO__DATA_CACHE_SIZE - 1)

struct dso_cache {
	struct rb_node	rb_node;
	u64 offset;
	u64 size;
	char data[0];
};

struct dso {
	struct list_head node;
	struct rb_root	 symbols[MAP__NR_TYPES];
	struct rb_root	 symbol_names[MAP__NR_TYPES];
	struct rb_root	 cache;
	enum dso_kernel_type	kernel;
	enum dso_swap_type	needs_swap;
	enum dso_binary_type	symtab_type;
	enum dso_binary_type	data_type;
	u8		 adjust_symbols:1;
	u8		 has_build_id:1;
	u8		 hit:1;
	u8		 annotate_warned:1;
	u8		 sname_alloc:1;
	u8		 lname_alloc:1;
	u8		 sorted_by_name;
	u8		 loaded;
	u8		 build_id[BUILD_ID_SIZE];
	const char	 *short_name;
	char	 	 *long_name;
	u16		 long_name_len;
	u16		 short_name_len;
	char		 name[0];
};

struct symsrc {
	char *name;
	int fd;
	enum dso_binary_type type;

#ifndef NO_LIBELF_SUPPORT
	Elf *elf;
	GElf_Ehdr ehdr;

	Elf_Scn *opdsec;
	size_t opdidx;
	GElf_Shdr opdshdr;

	Elf_Scn *symtab;
	GElf_Shdr symshdr;

	Elf_Scn *dynsym;
	size_t dynsym_idx;
	GElf_Shdr dynshdr;

	bool adjust_symbols;
#endif
};

void symsrc__destroy(struct symsrc *ss);
int symsrc__init(struct symsrc *ss, struct dso *dso, const char *name,
		 enum dso_binary_type type);
bool symsrc__has_symtab(struct symsrc *ss);
bool symsrc__possibly_runtime(struct symsrc *ss);

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

void dsos__add(struct list_head *head, struct dso *dso);
struct dso *dsos__find(struct list_head *head, const char *name);
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

char dso__symtab_origin(const struct dso *dso);
void dso__set_long_name(struct dso *dso, char *name);
void dso__set_build_id(struct dso *dso, void *build_id);
bool dso__build_id_equal(const struct dso *dso, u8 *build_id);
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
					  char type, u64 start));
int filename__read_debuglink(const char *filename, char *debuglink,
			     size_t size);

void machine__destroy_kernel_maps(struct machine *machine);
int __machine__create_kernel_maps(struct machine *machine, struct dso *kernel);
int machine__create_kernel_maps(struct machine *machine);

int machines__create_kernel_maps(struct rb_root *machines, pid_t pid);
int machines__create_guest_kernel_maps(struct rb_root *machines);
void machines__destroy_guest_kernel_maps(struct rb_root *machines);

int symbol__init(void);
void symbol__exit(void);
void symbol__elf_init(void);
struct symbol *symbol__new(u64 start, u64 len, u8 binding, const char *name);
size_t symbol__fprintf_symname_offs(const struct symbol *sym,
				    const struct addr_location *al, FILE *fp);
size_t symbol__fprintf_symname(const struct symbol *sym, FILE *fp);
bool symbol_type__is_a(char symbol_type, enum map_type map_type);

size_t machine__fprintf_vmlinux_path(struct machine *machine, FILE *fp);

int dso__binary_type_file(struct dso *dso, enum dso_binary_type type,
			  char *root_dir, char *file, size_t size);

int dso__data_fd(struct dso *dso, struct machine *machine);
ssize_t dso__data_read_offset(struct dso *dso, struct machine *machine,
			      u64 offset, u8 *data, ssize_t size);
ssize_t dso__data_read_addr(struct dso *dso, struct map *map,
			    struct machine *machine, u64 addr,
			    u8 *data, ssize_t size);
int dso__test_data(void);
int dso__load_sym(struct dso *dso, struct map *map, struct symsrc *syms_ss,
		  struct symsrc *runtime_ss, symbol_filter_t filter,
		  int kmodule);
int dso__synthesize_plt_symbols(struct dso *dso, struct symsrc *ss,
				struct map *map, symbol_filter_t filter);

void symbols__insert(struct rb_root *symbols, struct symbol *sym);
void symbols__fixup_duplicate(struct rb_root *symbols);
void symbols__fixup_end(struct rb_root *symbols);
void __map_groups__fixup_end(struct map_groups *mg, enum map_type type);

#endif /* __PERF_SYMBOL */
