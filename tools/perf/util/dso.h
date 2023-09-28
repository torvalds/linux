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

struct auxtrace_cache;

struct dso {
	struct mutex	 lock;
	struct list_head node;
	struct rb_node	 rb_node;	/* rbtree node sorted by long name */
	struct rb_root	 *root;		/* root of rbtree that rb_node is in */
	struct rb_root_cached symbols;
	struct symbol	 **symbol_names;
	size_t		 symbol_names_len;
	struct rb_root_cached inlined_nodes;
	struct rb_root_cached srclines;
	struct {
		u64		addr;
		struct symbol	*symbol;
	} last_find_result;
	void		 *a2l;
	char		 *symsrc_filename;
	unsigned int	 a2l_fails;
	enum dso_space_type	kernel;
	bool			is_kmod;
	enum dso_swap_type	needs_swap;
	enum dso_binary_type	symtab_type;
	enum dso_binary_type	binary_type;
	enum dso_load_errno	load_errno;
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
	struct build_id	 bid;
	u64		 text_offset;
	u64		 text_end;
	const char	 *short_name;
	const char	 *long_name;
	u16		 long_name_len;
	u16		 short_name_len;
	void		*dwfl;			/* DWARF debug info */
	struct auxtrace_cache *auxtrace_cache;
	int		 comp;

	/* dso data file */
	struct {
		struct rb_root	 cache;
		int		 fd;
		int		 status;
		u32		 status_seen;
		u64		 file_size;
		struct list_head open_entry;
		u64		 elf_base_addr;
		u64		 debug_frame_offset;
		u64		 eh_frame_hdr_addr;
		u64		 eh_frame_hdr_offset;
	} data;
	/* bpf prog information */
	struct {
		u32		id;
		u32		sub_id;
		struct perf_env	*env;
	} bpf_prog;

	union { /* Tool specific area */
		void	 *priv;
		u64	 db_id;
	};
	struct nsinfo	*nsinfo;
	struct dso_id	 id;
	refcount_t	 refcnt;
	char		 name[];
};

/* dso__for_each_symbol - iterate over the symbols of given type
 *
 * @dso: the 'struct dso *' in which symbols are iterated
 * @pos: the 'struct symbol *' to use as a loop cursor
 * @n: the 'struct rb_node *' to use as a temporary storage
 */
#define dso__for_each_symbol(dso, pos, n)	\
	symbols__for_each_entry(&(dso)->symbols, pos, n)

#define dsos__for_each_with_build_id(pos, head)	\
	list_for_each_entry(pos, head, node)	\
		if (!pos->has_build_id)		\
			continue;		\
		else

static inline void dso__set_loaded(struct dso *dso)
{
	dso->loaded = true;
}

struct dso *dso__new_id(const char *name, struct dso_id *id);
struct dso *dso__new(const char *name);
void dso__delete(struct dso *dso);

int dso__cmp_id(struct dso *a, struct dso *b);
void dso__set_short_name(struct dso *dso, const char *name, bool name_allocated);
void dso__set_long_name(struct dso *dso, const char *name, bool name_allocated);

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
	return !RB_EMPTY_ROOT(&dso->symbols.rb_root);
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

static inline bool dso__is_vmlinux(const struct dso *dso)
{
	return dso->binary_type == DSO_BINARY_TYPE__VMLINUX ||
	       dso->binary_type == DSO_BINARY_TYPE__GUEST_VMLINUX;
}

static inline bool dso__is_kcore(const struct dso *dso)
{
	return dso->binary_type == DSO_BINARY_TYPE__KCORE ||
	       dso->binary_type == DSO_BINARY_TYPE__GUEST_KCORE;
}

static inline bool dso__is_kallsyms(const struct dso *dso)
{
	return dso->kernel && dso->long_name[0] != '/';
}

bool dso__is_object_file(const struct dso *dso);

void dso__free_a2l(struct dso *dso);

enum dso_type dso__type(struct dso *dso, struct machine *machine);

int dso__strerror_load(struct dso *dso, char *buf, size_t buflen);

void reset_fd_limit(void);

#endif /* __PERF_DSO */
