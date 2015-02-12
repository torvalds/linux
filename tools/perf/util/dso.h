#ifndef __PERF_DSO
#define __PERF_DSO

#include <linux/types.h>
#include <linux/rbtree.h>
#include <stdbool.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include "map.h"
#include "build-id.h"

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
	DSO_BINARY_TYPE__GUEST_KMODULE_COMP,
	DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE,
	DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE_COMP,
	DSO_BINARY_TYPE__KCORE,
	DSO_BINARY_TYPE__GUEST_KCORE,
	DSO_BINARY_TYPE__OPENEMBEDDED_DEBUGINFO,
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

struct dso_cache {
	struct rb_node	rb_node;
	u64 offset;
	u64 size;
	char data[0];
};

/*
 * DSOs are put into both a list for fast iteration and rbtree for fast
 * long name lookup.
 */
struct dsos {
	struct list_head head;
	struct rb_root	 root;	/* rbtree root sorted by long name */
};

struct dso {
	struct list_head node;
	struct rb_node	 rb_node;	/* rbtree node sorted by long name */
	struct rb_root	 symbols[MAP__NR_TYPES];
	struct rb_root	 symbol_names[MAP__NR_TYPES];
	void		 *a2l;
	char		 *symsrc_filename;
	unsigned int	 a2l_fails;
	enum dso_kernel_type	kernel;
	enum dso_swap_type	needs_swap;
	enum dso_binary_type	symtab_type;
	enum dso_binary_type	binary_type;
	u8		 adjust_symbols:1;
	u8		 has_build_id:1;
	u8		 has_srcline:1;
	u8		 hit:1;
	u8		 annotate_warned:1;
	u8		 short_name_allocated:1;
	u8		 long_name_allocated:1;
	u8		 is_64_bit:1;
	u8		 sorted_by_name;
	u8		 loaded;
	u8		 rel;
	u8		 build_id[BUILD_ID_SIZE];
	const char	 *short_name;
	const char	 *long_name;
	u16		 long_name_len;
	u16		 short_name_len;
	void		*dwfl;			/* DWARF debug info */

	/* dso data file */
	struct {
		struct rb_root	 cache;
		int		 fd;
		int		 status;
		u32		 status_seen;
		size_t		 file_size;
		struct list_head open_entry;
		u64		 debug_frame_offset;
		u64		 eh_frame_hdr_offset;
	} data;

	union { /* Tool specific area */
		void	 *priv;
		u64	 db_id;
	};

	char		 name[0];
};

/* dso__for_each_symbol - iterate over the symbols of given type
 *
 * @dso: the 'struct dso *' in which symbols itereated
 * @pos: the 'struct symbol *' to use as a loop cursor
 * @n: the 'struct rb_node *' to use as a temporary storage
 * @type: the 'enum map_type' type of symbols
 */
#define dso__for_each_symbol(dso, pos, n, type)	\
	symbols__for_each_entry(&(dso)->symbols[(type)], pos, n)

static inline void dso__set_loaded(struct dso *dso, enum map_type type)
{
	dso->loaded |= (1 << type);
}

struct dso *dso__new(const char *name);
void dso__delete(struct dso *dso);

void dso__set_short_name(struct dso *dso, const char *name, bool name_allocated);
void dso__set_long_name(struct dso *dso, const char *name, bool name_allocated);

int dso__name_len(const struct dso *dso);

bool dso__loaded(const struct dso *dso, enum map_type type);

bool dso__sorted_by_name(const struct dso *dso, enum map_type type);
void dso__set_sorted_by_name(struct dso *dso, enum map_type type);
void dso__sort_by_name(struct dso *dso, enum map_type type);

void dso__set_build_id(struct dso *dso, void *build_id);
bool dso__build_id_equal(const struct dso *dso, u8 *build_id);
void dso__read_running_kernel_build_id(struct dso *dso,
				       struct machine *machine);
int dso__kernel_module_get_build_id(struct dso *dso, const char *root_dir);

char dso__symtab_origin(const struct dso *dso);
int dso__read_binary_type_filename(const struct dso *dso, enum dso_binary_type type,
				   char *root_dir, char *filename, size_t size);
bool is_supported_compression(const char *ext);
bool is_kernel_module(const char *pathname);
bool decompress_to_file(const char *ext, const char *filename, int output_fd);
bool dso__needs_decompress(struct dso *dso);

struct kmod_path {
	char *name;
	char *ext;
	bool  comp;
	bool  kmod;
};

int __kmod_path__parse(struct kmod_path *m, const char *path,
		     bool alloc_name, bool alloc_ext);

#define kmod_path__parse(__m, __p)      __kmod_path__parse(__m, __p, false, false)
#define kmod_path__parse_name(__m, __p) __kmod_path__parse(__m, __p, true , false)
#define kmod_path__parse_ext(__m, __p)  __kmod_path__parse(__m, __p, false, true)

/*
 * The dso__data_* external interface provides following functions:
 *   dso__data_fd
 *   dso__data_close
 *   dso__data_size
 *   dso__data_read_offset
 *   dso__data_read_addr
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
 *   int fd = dso__data_fd(dso, machine);
 *   USE 'fd' SOMEHOW
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
int dso__data_fd(struct dso *dso, struct machine *machine);
void dso__data_close(struct dso *dso);

off_t dso__data_size(struct dso *dso, struct machine *machine);
ssize_t dso__data_read_offset(struct dso *dso, struct machine *machine,
			      u64 offset, u8 *data, ssize_t size);
ssize_t dso__data_read_addr(struct dso *dso, struct map *map,
			    struct machine *machine, u64 addr,
			    u8 *data, ssize_t size);
bool dso__data_status_seen(struct dso *dso, enum dso_data_status_seen by);

struct map *dso__new_map(const char *name);
struct dso *dso__kernel_findnew(struct machine *machine, const char *name,
				const char *short_name, int dso_type);

void dsos__add(struct dsos *dsos, struct dso *dso);
struct dso *dsos__addnew(struct dsos *dsos, const char *name);
struct dso *dsos__find(const struct dsos *dsos, const char *name,
		       bool cmp_short);
struct dso *__dsos__findnew(struct dsos *dsos, const char *name);
bool __dsos__read_build_ids(struct list_head *head, bool with_hits);

size_t __dsos__fprintf_buildid(struct list_head *head, FILE *fp,
			       bool (skip)(struct dso *dso, int parm), int parm);
size_t __dsos__fprintf(struct list_head *head, FILE *fp);

size_t dso__fprintf_buildid(struct dso *dso, FILE *fp);
size_t dso__fprintf_symbols_by_name(struct dso *dso,
				    enum map_type type, FILE *fp);
size_t dso__fprintf(struct dso *dso, enum map_type type, FILE *fp);

static inline bool dso__is_vmlinux(struct dso *dso)
{
	return dso->binary_type == DSO_BINARY_TYPE__VMLINUX ||
	       dso->binary_type == DSO_BINARY_TYPE__GUEST_VMLINUX;
}

static inline bool dso__is_kcore(struct dso *dso)
{
	return dso->binary_type == DSO_BINARY_TYPE__KCORE ||
	       dso->binary_type == DSO_BINARY_TYPE__GUEST_KCORE;
}

void dso__free_a2l(struct dso *dso);

enum dso_type dso__type(struct dso *dso, struct machine *machine);

#endif /* __PERF_DSO */
