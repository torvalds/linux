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
#include "build-id.h"
#include "event.h"
#include "path.h"

#ifdef HAVE_LIBELF_SUPPORT
#include <libelf.h>
#include <gelf.h>
#endif
#include <elf.h>

#include "dso.h"

/*
 * libelf 0.8.x and earlier do not support ELF_C_READ_MMAP;
 * for newer versions we can use mmap to reduce memory usage:
 */
#ifdef HAVE_LIBELF_MMAP_SUPPORT
# define PERF_ELF_C_READ_MMAP ELF_C_READ_MMAP
#else
# define PERF_ELF_C_READ_MMAP ELF_C_READ
#endif

#ifdef HAVE_LIBELF_SUPPORT
Elf_Scn *elf_section_by_name(Elf *elf, GElf_Ehdr *ep,
			     GElf_Shdr *shp, const char *name, size_t *idx);
#endif

#ifndef DMGL_PARAMS
#define DMGL_NO_OPTS     0              /* For readability... */
#define DMGL_PARAMS      (1 << 0)       /* Include function args */
#define DMGL_ANSI        (1 << 1)       /* Include const, volatile, etc */
#endif

#define DSO__NAME_KALLSYMS	"[kernel.kallsyms]"
#define DSO__NAME_KCORE		"[kernel.kcore]"

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
	u8		idle:1;
	u8		ignore:1;
	u8		arch_sym;
	char		name[0];
};

void symbol__delete(struct symbol *sym);
void symbols__delete(struct rb_root *symbols);

/* symbols__for_each_entry - iterate over symbols (rb_root)
 *
 * @symbols: the rb_root of symbols
 * @pos: the 'struct symbol *' to use as a loop cursor
 * @nd: the 'struct rb_node *' to use as a temporary storage
 */
#define symbols__for_each_entry(symbols, pos, nd)			\
	for (nd = rb_first(symbols);					\
	     nd && (pos = rb_entry(nd, struct symbol, rb_node));	\
	     nd = rb_next(nd))

static inline size_t symbol__size(const struct symbol *sym)
{
	return sym->end - sym->start;
}

struct strlist;
struct intlist;

struct symbol_conf {
	unsigned short	priv_size;
	unsigned short	nr_events;
	bool		try_vmlinux_path,
			init_annotation,
			force,
			ignore_vmlinux,
			ignore_vmlinux_buildid,
			show_kernel_path,
			use_modules,
			allow_aliases,
			sort_by_name,
			show_nr_samples,
			show_total_period,
			use_callchain,
			cumulate_callchain,
			show_branchflag_count,
			exclude_other,
			show_cpu_utilization,
			initialized,
			kptr_restrict,
			annotate_asm_raw,
			annotate_src,
			event_group,
			demangle,
			demangle_kernel,
			filter_relative,
			show_hist_headers,
			branch_callstack,
			has_filter,
			show_ref_callgraph,
			hide_unresolved,
			raw_trace,
			report_hierarchy,
			inline_name;
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
			*pid_list_str,
			*tid_list_str,
			*sym_list_str,
			*col_width_list_str,
			*bt_stop_list_str;
       struct strlist	*dso_list,
			*comm_list,
			*sym_list,
			*dso_from_list,
			*dso_to_list,
			*sym_from_list,
			*sym_to_list,
			*bt_stop_list;
	struct intlist	*pid_list,
			*tid_list;
	const char	*symfs;
};

extern struct symbol_conf symbol_conf;

struct symbol_name_rb_node {
	struct rb_node	rb_node;
	struct symbol	sym;
};

static inline int __symbol__join_symfs(char *bf, size_t size, const char *path)
{
	return path__join(bf, size, symbol_conf.symfs, path);
}

#define symbol__join_symfs(bf, path) __symbol__join_symfs(bf, sizeof(bf), path)

extern int vmlinux_path__nr_entries;
extern char **vmlinux_path;

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
};

struct addr_map_symbol {
	struct map    *map;
	struct symbol *sym;
	u64	      addr;
	u64	      al_addr;
	u64	      phys_addr;
};

struct branch_info {
	struct addr_map_symbol from;
	struct addr_map_symbol to;
	struct branch_flags flags;
	char			*srcline_from;
	char			*srcline_to;
};

struct mem_info {
	struct addr_map_symbol iaddr;
	struct addr_map_symbol daddr;
	union perf_mem_data_src data_src;
};

struct addr_location {
	struct machine *machine;
	struct thread *thread;
	struct map    *map;
	struct symbol *sym;
	u64	      addr;
	char	      level;
	u8	      filtered;
	u8	      cpumode;
	s32	      cpu;
	s32	      socket;
};

struct symsrc {
	char *name;
	int fd;
	enum dso_binary_type type;

#ifdef HAVE_LIBELF_SUPPORT
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
	bool is_64_bit;
#endif
};

void symsrc__destroy(struct symsrc *ss);
int symsrc__init(struct symsrc *ss, struct dso *dso, const char *name,
		 enum dso_binary_type type);
bool symsrc__has_symtab(struct symsrc *ss);
bool symsrc__possibly_runtime(struct symsrc *ss);

int dso__load(struct dso *dso, struct map *map);
int dso__load_vmlinux(struct dso *dso, struct map *map,
		      const char *vmlinux, bool vmlinux_allocated);
int dso__load_vmlinux_path(struct dso *dso, struct map *map);
int __dso__load_kallsyms(struct dso *dso, const char *filename, struct map *map,
			 bool no_kcore);
int dso__load_kallsyms(struct dso *dso, const char *filename, struct map *map);

void dso__insert_symbol(struct dso *dso, enum map_type type,
			struct symbol *sym);

struct symbol *dso__find_symbol(struct dso *dso, enum map_type type,
				u64 addr);
struct symbol *dso__find_symbol_by_name(struct dso *dso, enum map_type type,
					const char *name);
struct symbol *symbol__next_by_name(struct symbol *sym);

struct symbol *dso__first_symbol(struct dso *dso, enum map_type type);
struct symbol *dso__last_symbol(struct dso *dso, enum map_type type);
struct symbol *dso__next_symbol(struct symbol *sym);

enum dso_type dso__type_fd(int fd);

int filename__read_build_id(const char *filename, void *bf, size_t size);
int sysfs__read_build_id(const char *filename, void *bf, size_t size);
int modules__parse(const char *filename, void *arg,
		   int (*process_module)(void *arg, const char *name,
					 u64 start, u64 size));
int filename__read_debuglink(const char *filename, char *debuglink,
			     size_t size);

struct perf_env;
int symbol__init(struct perf_env *env);
void symbol__exit(void);
void symbol__elf_init(void);
int symbol__annotation_init(void);

struct symbol *symbol__new(u64 start, u64 len, u8 binding, const char *name);
size_t __symbol__fprintf_symname_offs(const struct symbol *sym,
				      const struct addr_location *al,
				      bool unknown_as_addr,
				      bool print_offsets, FILE *fp);
size_t symbol__fprintf_symname_offs(const struct symbol *sym,
				    const struct addr_location *al, FILE *fp);
size_t __symbol__fprintf_symname(const struct symbol *sym,
				 const struct addr_location *al,
				 bool unknown_as_addr, FILE *fp);
size_t symbol__fprintf_symname(const struct symbol *sym, FILE *fp);
size_t symbol__fprintf(struct symbol *sym, FILE *fp);
bool symbol_type__is_a(char symbol_type, enum map_type map_type);
bool symbol__restricted_filename(const char *filename,
				 const char *restricted_filename);
int symbol__config_symfs(const struct option *opt __maybe_unused,
			 const char *dir, int unset __maybe_unused);

int dso__load_sym(struct dso *dso, struct map *map, struct symsrc *syms_ss,
		  struct symsrc *runtime_ss, int kmodule);
int dso__synthesize_plt_symbols(struct dso *dso, struct symsrc *ss,
				struct map *map);

char *dso__demangle_sym(struct dso *dso, int kmodule, const char *elf_name);

void __symbols__insert(struct rb_root *symbols, struct symbol *sym, bool kernel);
void symbols__insert(struct rb_root *symbols, struct symbol *sym);
void symbols__fixup_duplicate(struct rb_root *symbols);
void symbols__fixup_end(struct rb_root *symbols);
void __map_groups__fixup_end(struct map_groups *mg, enum map_type type);

typedef int (*mapfn_t)(u64 start, u64 len, u64 pgoff, void *data);
int file__read_maps(int fd, bool exe, mapfn_t mapfn, void *data,
		    bool *is_64_bit);

#define PERF_KCORE_EXTRACT "/tmp/perf-kcore-XXXXXX"

struct kcore_extract {
	char *kcore_filename;
	u64 addr;
	u64 offs;
	u64 len;
	char extract_filename[sizeof(PERF_KCORE_EXTRACT)];
	int fd;
};

int kcore_extract__create(struct kcore_extract *kce);
void kcore_extract__delete(struct kcore_extract *kce);

int kcore_copy(const char *from_dir, const char *to_dir);
int compare_proc_modules(const char *from, const char *to);

int setup_list(struct strlist **list, const char *list_str,
	       const char *list_name);
int setup_intlist(struct intlist **list, const char *list_str,
		  const char *list_name);

#ifdef HAVE_LIBELF_SUPPORT
bool elf__needs_adjust_symbols(GElf_Ehdr ehdr);
void arch__sym_update(struct symbol *s, GElf_Sym *sym);
#endif

#define SYMBOL_A 0
#define SYMBOL_B 1

int arch__compare_symbol_names(const char *namea, const char *nameb);
int arch__compare_symbol_names_n(const char *namea, const char *nameb,
				 unsigned int n);
int arch__choose_best_symbol(struct symbol *syma, struct symbol *symb);

enum symbol_tag_include {
	SYMBOL_TAG_INCLUDE__NONE = 0,
	SYMBOL_TAG_INCLUDE__DEFAULT_ONLY
};

int symbol__match_symbol_name(const char *namea, const char *nameb,
			      enum symbol_tag_include includes);

/* structure containing an SDT note's info */
struct sdt_note {
	char *name;			/* name of the note*/
	char *provider;			/* provider name */
	char *args;
	bool bit32;			/* whether the location is 32 bits? */
	union {				/* location, base and semaphore addrs */
		Elf64_Addr a64[3];
		Elf32_Addr a32[3];
	} addr;
	struct list_head note_list;	/* SDT notes' list */
};

int get_sdt_note_list(struct list_head *head, const char *target);
int cleanup_sdt_note_list(struct list_head *sdt_notes);
int sdt_notes__get_count(struct list_head *start);

#define SDT_BASE_SCN ".stapsdt.base"
#define SDT_NOTE_SCN  ".note.stapsdt"
#define SDT_NOTE_TYPE 3
#define SDT_NOTE_NAME "stapsdt"
#define NR_ADDR 3

#endif /* __PERF_SYMBOL */
