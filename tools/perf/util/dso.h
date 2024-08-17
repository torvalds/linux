/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_DSO
#define __PERF_DSO

#include <linux/refcount.h>
#include <linux/types.h>
#include <linux/rbtree.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdio.h>
#include <linux/bitops.h>
#include "build-id.h"
#include "mutex.h"
#include <internal/rc_check.h>

struct machine;
struct map;
struct perf_env;

#define DSO__NAME_KALLSYMS	"[kernel.kallsyms]"
#define DSO__NAME_KCORE		"[kernel.kcore]"

enum dso_binary_type {
	DSO_BINARY_TYPE__KALLSYMS = 0,
	DSO_BINARY_TYPE__GUEST_KALLSYMS,
	DSO_BINARY_TYPE__VMLINUX,
	DSO_BINARY_TYPE__GUEST_VMLINUX,
	DSO_BINARY_TYPE__JAVA_JIT,
	DSO_BINARY_TYPE__DEBUGLINK,
	DSO_BINARY_TYPE__BUILD_ID_CACHE,
	DSO_BINARY_TYPE__BUILD_ID_CACHE_DEBUGINFO,
	DSO_BINARY_TYPE__FEDORA_DEBUGINFO,
	DSO_BINARY_TYPE__UBUNTU_DEBUGINFO,
	DSO_BINARY_TYPE__MIXEDUP_UBUNTU_DEBUGINFO,
	DSO_BINARY_TYPE__BUILDID_DEBUGINFO,
	DSO_BINARY_TYPE__SYSTEM_PATH_DSO,
	DSO_BINARY_TYPE__GUEST_KMODULE,
	DSO_BINARY_TYPE__GUEST_KMODULE_COMP,
	DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE,
	DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE_COMP,
	DSO_BINARY_TYPE__KCORE,
	DSO_BINARY_TYPE__GUEST_KCORE,
	DSO_BINARY_TYPE__OPENEMBEDDED_DEBUGINFO,
	DSO_BINARY_TYPE__BPF_PROG_INFO,
	DSO_BINARY_TYPE__BPF_IMAGE,
	DSO_BINARY_TYPE__OOL,
	DSO_BINARY_TYPE__NOT_FOUND,
};

enum dso_space_type {
	DSO_SPACE__USER = 0,
	DSO_SPACE__KERNEL,
	DSO_SPACE__KERNEL_GUEST
};

enum dso_swap_type {
	DSO_SWAP__UNSET,
	DSO_SWAP__NO,
	DSO_SWAP__YES,
};

enum dso_data_status {
	DSO_DATA_STATUS_ERROR	= -1,
	DSO_DATA_STATUS_UNKNOWN	= 0,
	DSO_DATA_STATUS_OK	= 1,
};

enum dso_data_status_seen {
	DSO_DATA_STATUS_SEEN_ITRACE,
};

enum dso_type {
	DSO__TYPE_UNKNOWN,
	DSO__TYPE_64BIT,
	DSO__TYPE_32BIT,
	DSO__TYPE_X32BIT,
};

enum dso_load_errno {
	DSO_LOAD_ERRNO__SUCCESS		= 0,

	/*
	 * Choose an arbitrary negative big number not to clash with standard
	 * errno since SUS requires the errno has distinct positive values.
	 * See 'Issue 6' in the link below.
	 *
	 * http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/errno.h.html
	 */
	__DSO_LOAD_ERRNO__START		= -10000,

	DSO_LOAD_ERRNO__INTERNAL_ERROR	= __DSO_LOAD_ERRNO__START,

	/* for symsrc__init() */
	DSO_LOAD_ERRNO__INVALID_ELF,
	DSO_LOAD_ERRNO__CANNOT_READ_BUILDID,
	DSO_LOAD_ERRNO__MISMATCHING_BUILDID,

	/* for decompress_kmodule */
	DSO_LOAD_ERRNO__DECOMPRESSION_FAILURE,

	__DSO_LOAD_ERRNO__END,
};

#define DSO__SWAP(dso, type, val)				\
({								\
	type ____r = val;					\
	enum dso_swap_type ___dst = dso__needs_swap(dso);	\
	BUG_ON(___dst == DSO_SWAP__UNSET);			\
	if (___dst == DSO_SWAP__YES) {				\
		switch (sizeof(____r)) {			\
		case 2:						\
			____r = bswap_16(val);			\
			break;					\
		case 4:						\
			____r = bswap_32(val);			\
			break;					\
		case 8:						\
			____r = bswap_64(val);			\
			break;					\
		default:					\
			BUG_ON(1);				\
		}						\
	}							\
	____r;							\
})

#define DSO__DATA_CACHE_SIZE 4096
#define DSO__DATA_CACHE_MASK ~(DSO__DATA_CACHE_SIZE - 1)

/*
 * Data about backing storage DSO, comes from PERF_RECORD_MMAP2 meta events
 */
struct dso_id {
	u32	maj;
	u32	min;
	u64	ino;
	u64	ino_generation;
};

struct dso_cache {
	struct rb_node	rb_node;
	u64 offset;
	u64 size;
	char data[];
};

struct dso_data {
	struct rb_root	 cache;
	struct list_head open_entry;
#ifdef REFCNT_CHECKING
	struct dso	 *dso;
#endif
	int		 fd;
	int		 status;
	u32		 status_seen;
	u64		 file_size;
	u64		 elf_base_addr;
	u64		 debug_frame_offset;
	u64		 eh_frame_hdr_addr;
	u64		 eh_frame_hdr_offset;
};

struct dso_bpf_prog {
	u32		id;
	u32		sub_id;
	struct perf_env	*env;
};

struct auxtrace_cache;

DECLARE_RC_STRUCT(dso) {
	struct mutex	 lock;
	struct dsos	 *dsos;
	struct rb_root_cached symbols;
	struct symbol	 **symbol_names;
	size_t		 symbol_names_len;
	struct rb_root_cached inlined_nodes;
	struct rb_root_cached srclines;
	struct rb_root	 data_types;
	struct rb_root	 global_vars;

	struct {
		u64		addr;
		struct symbol	*symbol;
	} last_find_result;
	struct build_id	 bid;
	u64		 text_offset;
	u64		 text_end;
	const char	 *short_name;
	const char	 *long_name;
	void		 *a2l;
	char		 *symsrc_filename;
#if defined(__powerpc__)
	void		*dwfl;			/* DWARF debug info */
#endif
	struct nsinfo	*nsinfo;
	struct auxtrace_cache *auxtrace_cache;
	union { /* Tool specific area */
		void	 *priv;
		u64	 db_id;
	};
	/* bpf prog information */
	struct dso_bpf_prog bpf_prog;
	/* dso data file */
	struct dso_data	 data;
	struct dso_id	 id;
	unsigned int	 a2l_fails;
	int		 comp;
	refcount_t	 refcnt;
	enum dso_load_errno	load_errno;
	u16		 long_name_len;
	u16		 short_name_len;
	enum dso_binary_type	symtab_type:8;
	enum dso_binary_type	binary_type:8;
	enum dso_space_type	kernel:2;
	enum dso_swap_type	needs_swap:2;
	bool			is_kmod:1;
	u8		 adjust_symbols:1;
	u8		 has_build_id:1;
	u8		 header_build_id:1;
	u8		 has_srcline:1;
	u8		 hit:1;
	u8		 annotate_warned:1;
	u8		 auxtrace_warned:1;
	u8		 short_name_allocated:1;
	u8		 long_name_allocated:1;
	u8		 is_64_bit:1;
	bool		 sorted_by_name;
	bool		 loaded;
	u8		 rel;
	char		 name[];
};

/* dso__for_each_symbol - iterate over the symbols of given type
 *
 * @dso: the 'struct dso *' in which symbols are iterated
 * @pos: the 'struct symbol *' to use as a loop cursor
 * @n: the 'struct rb_node *' to use as a temporary storage
 */
#define dso__for_each_symbol(dso, pos, n)	\
	symbols__for_each_entry(dso__symbols(dso), pos, n)

static inline void *dso__a2l(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->a2l;
}

static inline void dso__set_a2l(struct dso *dso, void *val)
{
	RC_CHK_ACCESS(dso)->a2l = val;
}

static inline unsigned int dso__a2l_fails(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->a2l_fails;
}

static inline void dso__set_a2l_fails(struct dso *dso, unsigned int val)
{
	RC_CHK_ACCESS(dso)->a2l_fails = val;
}

static inline bool dso__adjust_symbols(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->adjust_symbols;
}

static inline void dso__set_adjust_symbols(struct dso *dso, bool val)
{
	RC_CHK_ACCESS(dso)->adjust_symbols = val;
}

static inline bool dso__annotate_warned(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->annotate_warned;
}

static inline void dso__set_annotate_warned(struct dso *dso)
{
	RC_CHK_ACCESS(dso)->annotate_warned = 1;
}

static inline bool dso__auxtrace_warned(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->auxtrace_warned;
}

static inline void dso__set_auxtrace_warned(struct dso *dso)
{
	RC_CHK_ACCESS(dso)->auxtrace_warned = 1;
}

static inline struct auxtrace_cache *dso__auxtrace_cache(struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->auxtrace_cache;
}

static inline void dso__set_auxtrace_cache(struct dso *dso, struct auxtrace_cache *cache)
{
	RC_CHK_ACCESS(dso)->auxtrace_cache = cache;
}

static inline struct build_id *dso__bid(struct dso *dso)
{
	return &RC_CHK_ACCESS(dso)->bid;
}

static inline const struct build_id *dso__bid_const(const struct dso *dso)
{
	return &RC_CHK_ACCESS(dso)->bid;
}

static inline struct dso_bpf_prog *dso__bpf_prog(struct dso *dso)
{
	return &RC_CHK_ACCESS(dso)->bpf_prog;
}

static inline bool dso__has_build_id(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->has_build_id;
}

static inline void dso__set_has_build_id(struct dso *dso)
{
	RC_CHK_ACCESS(dso)->has_build_id = true;
}

static inline bool dso__has_srcline(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->has_srcline;
}

static inline void dso__set_has_srcline(struct dso *dso, bool val)
{
	RC_CHK_ACCESS(dso)->has_srcline = val;
}

static inline int dso__comp(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->comp;
}

static inline void dso__set_comp(struct dso *dso, int comp)
{
	RC_CHK_ACCESS(dso)->comp = comp;
}

static inline struct dso_data *dso__data(struct dso *dso)
{
	return &RC_CHK_ACCESS(dso)->data;
}

static inline u64 dso__db_id(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->db_id;
}

static inline void dso__set_db_id(struct dso *dso, u64 db_id)
{
	RC_CHK_ACCESS(dso)->db_id = db_id;
}

static inline struct dsos *dso__dsos(struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->dsos;
}

static inline void dso__set_dsos(struct dso *dso, struct dsos *dsos)
{
	RC_CHK_ACCESS(dso)->dsos = dsos;
}

static inline bool dso__header_build_id(struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->header_build_id;
}

static inline void dso__set_header_build_id(struct dso *dso, bool val)
{
	RC_CHK_ACCESS(dso)->header_build_id = val;
}

static inline bool dso__hit(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->hit;
}

static inline void dso__set_hit(struct dso *dso)
{
	RC_CHK_ACCESS(dso)->hit = 1;
}

static inline struct dso_id *dso__id(struct dso *dso)
{
	return &RC_CHK_ACCESS(dso)->id;
}

static inline const struct dso_id *dso__id_const(const struct dso *dso)
{
	return &RC_CHK_ACCESS(dso)->id;
}

static inline struct rb_root_cached *dso__inlined_nodes(struct dso *dso)
{
	return &RC_CHK_ACCESS(dso)->inlined_nodes;
}

static inline bool dso__is_64_bit(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->is_64_bit;
}

static inline void dso__set_is_64_bit(struct dso *dso, bool is)
{
	RC_CHK_ACCESS(dso)->is_64_bit = is;
}

static inline bool dso__is_kmod(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->is_kmod;
}

static inline void dso__set_is_kmod(struct dso *dso)
{
	RC_CHK_ACCESS(dso)->is_kmod = 1;
}

static inline enum dso_space_type dso__kernel(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->kernel;
}

static inline void dso__set_kernel(struct dso *dso, enum dso_space_type kernel)
{
	RC_CHK_ACCESS(dso)->kernel = kernel;
}

static inline u64 dso__last_find_result_addr(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->last_find_result.addr;
}

static inline void dso__set_last_find_result_addr(struct dso *dso, u64 addr)
{
	RC_CHK_ACCESS(dso)->last_find_result.addr = addr;
}

static inline struct symbol *dso__last_find_result_symbol(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->last_find_result.symbol;
}

static inline void dso__set_last_find_result_symbol(struct dso *dso, struct symbol *symbol)
{
	RC_CHK_ACCESS(dso)->last_find_result.symbol = symbol;
}

static inline enum dso_load_errno *dso__load_errno(struct dso *dso)
{
	return &RC_CHK_ACCESS(dso)->load_errno;
}

static inline void dso__set_loaded(struct dso *dso)
{
	RC_CHK_ACCESS(dso)->loaded = true;
}

static inline struct mutex *dso__lock(struct dso *dso)
{
	return &RC_CHK_ACCESS(dso)->lock;
}

static inline const char *dso__long_name(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->long_name;
}

static inline bool dso__long_name_allocated(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->long_name_allocated;
}

static inline void dso__set_long_name_allocated(struct dso *dso, bool allocated)
{
	RC_CHK_ACCESS(dso)->long_name_allocated = allocated;
}

static inline u16 dso__long_name_len(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->long_name_len;
}

static inline const char *dso__name(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->name;
}

static inline enum dso_swap_type dso__needs_swap(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->needs_swap;
}

static inline void dso__set_needs_swap(struct dso *dso, enum dso_swap_type type)
{
	RC_CHK_ACCESS(dso)->needs_swap = type;
}

static inline struct nsinfo *dso__nsinfo(struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->nsinfo;
}

static inline const struct nsinfo *dso__nsinfo_const(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->nsinfo;
}

static inline struct nsinfo **dso__nsinfo_ptr(struct dso *dso)
{
	return &RC_CHK_ACCESS(dso)->nsinfo;
}

void dso__set_nsinfo(struct dso *dso, struct nsinfo *nsi);

static inline u8 dso__rel(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->rel;
}

static inline void dso__set_rel(struct dso *dso, u8 rel)
{
	RC_CHK_ACCESS(dso)->rel = rel;
}

static inline const char *dso__short_name(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->short_name;
}

static inline bool dso__short_name_allocated(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->short_name_allocated;
}

static inline void dso__set_short_name_allocated(struct dso *dso, bool allocated)
{
	RC_CHK_ACCESS(dso)->short_name_allocated = allocated;
}

static inline u16 dso__short_name_len(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->short_name_len;
}

static inline struct rb_root_cached *dso__srclines(struct dso *dso)
{
	return &RC_CHK_ACCESS(dso)->srclines;
}

static inline struct rb_root *dso__data_types(struct dso *dso)
{
	return &RC_CHK_ACCESS(dso)->data_types;
}

static inline struct rb_root *dso__global_vars(struct dso *dso)
{
	return &RC_CHK_ACCESS(dso)->global_vars;
}

static inline struct rb_root_cached *dso__symbols(struct dso *dso)
{
	return &RC_CHK_ACCESS(dso)->symbols;
}

static inline struct symbol **dso__symbol_names(struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->symbol_names;
}

static inline void dso__set_symbol_names(struct dso *dso, struct symbol **names)
{
	RC_CHK_ACCESS(dso)->symbol_names = names;
}

static inline size_t dso__symbol_names_len(struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->symbol_names_len;
}

static inline void dso__set_symbol_names_len(struct dso *dso, size_t len)
{
	RC_CHK_ACCESS(dso)->symbol_names_len = len;
}

static inline const char *dso__symsrc_filename(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->symsrc_filename;
}

static inline void dso__set_symsrc_filename(struct dso *dso, char *val)
{
	RC_CHK_ACCESS(dso)->symsrc_filename = val;
}

static inline void dso__free_symsrc_filename(struct dso *dso)
{
	zfree(&RC_CHK_ACCESS(dso)->symsrc_filename);
}

static inline enum dso_binary_type dso__symtab_type(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->symtab_type;
}

static inline void dso__set_symtab_type(struct dso *dso, enum dso_binary_type bt)
{
	RC_CHK_ACCESS(dso)->symtab_type = bt;
}

static inline u64 dso__text_end(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->text_end;
}

static inline void dso__set_text_end(struct dso *dso, u64 val)
{
	RC_CHK_ACCESS(dso)->text_end = val;
}

static inline u64 dso__text_offset(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->text_offset;
}

static inline void dso__set_text_offset(struct dso *dso, u64 val)
{
	RC_CHK_ACCESS(dso)->text_offset = val;
}

int dso_id__cmp(const struct dso_id *a, const struct dso_id *b);
bool dso_id__empty(const struct dso_id *id);

struct dso *dso__new_id(const char *name, const struct dso_id *id);
struct dso *dso__new(const char *name);
void dso__delete(struct dso *dso);

int dso__cmp_id(struct dso *a, struct dso *b);
void dso__set_short_name(struct dso *dso, const char *name, bool name_allocated);
void dso__set_long_name(struct dso *dso, const char *name, bool name_allocated);
void __dso__inject_id(struct dso *dso, const struct dso_id *id);

int dso__name_len(const struct dso *dso);

struct dso *dso__get(struct dso *dso);
void dso__put(struct dso *dso);

static inline void __dso__zput(struct dso **dso)
{
	dso__put(*dso);
	*dso = NULL;
}

#define dso__zput(dso) __dso__zput(&dso)

bool dso__loaded(const struct dso *dso);

static inline bool dso__has_symbols(const struct dso *dso)
{
	return !RB_EMPTY_ROOT(&RC_CHK_ACCESS(dso)->symbols.rb_root);
}

char *dso__filename_with_chroot(const struct dso *dso, const char *filename);

bool dso__sorted_by_name(const struct dso *dso);
void dso__set_sorted_by_name(struct dso *dso);
void dso__sort_by_name(struct dso *dso);

void dso__set_build_id(struct dso *dso, struct build_id *bid);
bool dso__build_id_equal(const struct dso *dso, struct build_id *bid);
void dso__read_running_kernel_build_id(struct dso *dso,
				       struct machine *machine);
int dso__kernel_module_get_build_id(struct dso *dso, const char *root_dir);

char dso__symtab_origin(const struct dso *dso);
int dso__read_binary_type_filename(const struct dso *dso, enum dso_binary_type type,
				   char *root_dir, char *filename, size_t size);
bool is_kernel_module(const char *pathname, int cpumode);
bool dso__needs_decompress(struct dso *dso);
int dso__decompress_kmodule_fd(struct dso *dso, const char *name);
int dso__decompress_kmodule_path(struct dso *dso, const char *name,
				 char *pathname, size_t len);
int filename__decompress(const char *name, char *pathname,
			 size_t len, int comp, int *err);

#define KMOD_DECOMP_NAME  "/tmp/perf-kmod-XXXXXX"
#define KMOD_DECOMP_LEN   sizeof(KMOD_DECOMP_NAME)

struct kmod_path {
	char *name;
	int   comp;
	bool  kmod;
};

int __kmod_path__parse(struct kmod_path *m, const char *path,
		     bool alloc_name);

#define kmod_path__parse(__m, __p)      __kmod_path__parse(__m, __p, false)
#define kmod_path__parse_name(__m, __p) __kmod_path__parse(__m, __p, true)

void dso__set_module_info(struct dso *dso, struct kmod_path *m,
			  struct machine *machine);

/*
 * The dso__data_* external interface provides following functions:
 *   dso__data_get_fd
 *   dso__data_put_fd
 *   dso__data_close
 *   dso__data_size
 *   dso__data_read_offset
 *   dso__data_read_addr
 *   dso__data_write_cache_offs
 *   dso__data_write_cache_addr
 *
 * Please refer to the dso.c object code for each function and
 * arguments documentation. Following text tries to explain the
 * dso file descriptor caching.
 *
 * The dso__data* interface allows caching of opened file descriptors
 * to speed up the dso data accesses. The idea is to leave the file
 * descriptor opened ideally for the whole life of the dso object.
 *
 * The current usage of the dso__data_* interface is as follows:
 *
 * Get DSO's fd:
 *   int fd = dso__data_get_fd(dso, machine);
 *   if (fd >= 0) {
 *       USE 'fd' SOMEHOW
 *       dso__data_put_fd(dso);
 *   }
 *
 * Read DSO's data:
 *   n = dso__data_read_offset(dso_0, &machine, 0, buf, BUFSIZE);
 *   n = dso__data_read_addr(dso_0, &machine, 0, buf, BUFSIZE);
 *
 * Eventually close DSO's fd:
 *   dso__data_close(dso);
 *
 * It is not necessary to close the DSO object data file. Each time new
 * DSO data file is opened, the limit (RLIMIT_NOFILE/2) is checked. Once
 * it is crossed, the oldest opened DSO object is closed.
 *
 * The dso__delete function calls close_dso function to ensure the
 * data file descriptor gets closed/unmapped before the dso object
 * is freed.
 *
 * TODO
*/
int dso__data_get_fd(struct dso *dso, struct machine *machine);
void dso__data_put_fd(struct dso *dso);
void dso__data_close(struct dso *dso);

int dso__data_file_size(struct dso *dso, struct machine *machine);
off_t dso__data_size(struct dso *dso, struct machine *machine);
ssize_t dso__data_read_offset(struct dso *dso, struct machine *machine,
			      u64 offset, u8 *data, ssize_t size);
ssize_t dso__data_read_addr(struct dso *dso, struct map *map,
			    struct machine *machine, u64 addr,
			    u8 *data, ssize_t size);
bool dso__data_status_seen(struct dso *dso, enum dso_data_status_seen by);
ssize_t dso__data_write_cache_offs(struct dso *dso, struct machine *machine,
				   u64 offset, const u8 *data, ssize_t size);
ssize_t dso__data_write_cache_addr(struct dso *dso, struct map *map,
				   struct machine *machine, u64 addr,
				   const u8 *data, ssize_t size);

struct map *dso__new_map(const char *name);
struct dso *machine__findnew_kernel(struct machine *machine, const char *name,
				    const char *short_name, int dso_type);

void dso__reset_find_symbol_cache(struct dso *dso);

size_t dso__fprintf_symbols_by_name(struct dso *dso, FILE *fp);
size_t dso__fprintf(struct dso *dso, FILE *fp);

static inline enum dso_binary_type dso__binary_type(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->binary_type;
}

static inline void dso__set_binary_type(struct dso *dso, enum dso_binary_type bt)
{
	RC_CHK_ACCESS(dso)->binary_type = bt;
}

static inline bool dso__is_vmlinux(const struct dso *dso)
{
	enum dso_binary_type bt = dso__binary_type(dso);

	return bt == DSO_BINARY_TYPE__VMLINUX || bt == DSO_BINARY_TYPE__GUEST_VMLINUX;
}

static inline bool dso__is_kcore(const struct dso *dso)
{
	enum dso_binary_type bt = dso__binary_type(dso);

	return bt == DSO_BINARY_TYPE__KCORE || bt == DSO_BINARY_TYPE__GUEST_KCORE;
}

static inline bool dso__is_kallsyms(const struct dso *dso)
{
	return RC_CHK_ACCESS(dso)->kernel && RC_CHK_ACCESS(dso)->long_name[0] != '/';
}

bool dso__is_object_file(const struct dso *dso);

void dso__free_a2l(struct dso *dso);

enum dso_type dso__type(struct dso *dso, struct machine *machine);

int dso__strerror_load(struct dso *dso, char *buf, size_t buflen);

void reset_fd_limit(void);

u64 dso__find_global_type(struct dso *dso, u64 addr);
u64 dso__findnew_global_type(struct dso *dso, u64 addr, u64 offset);

/* Check if dso name is of format "/tmp/perf-%d.map" */
bool perf_pid_map_tid(const char *dso_name, int *tid);
bool is_perf_pid_map_name(const char *dso_name);

#endif /* __PERF_DSO */
