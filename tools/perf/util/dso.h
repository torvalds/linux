#ifndef __PERF_DSO
#define __PERF_DSO

#include <linux/types.h>
#include <linux/rbtree.h>
#include <stdbool.h>
#include "types.h"
#include "map.h"

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
	u8		 rel;
	u8		 build_id[BUILD_ID_SIZE];
	const char	 *short_name;
	char		 *long_name;
	u16		 long_name_len;
	u16		 short_name_len;
	char		 name[0];
};

static inline void dso__set_loaded(struct dso *dso, enum map_type type)
{
	dso->loaded |= (1 << type);
}

struct dso *dso__new(const char *name);
void dso__delete(struct dso *dso);

void dso__set_short_name(struct dso *dso, const char *name);
void dso__set_long_name(struct dso *dso, char *name);

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
int dso__binary_type_file(struct dso *dso, enum dso_binary_type type,
			  char *root_dir, char *file, size_t size);

int dso__data_fd(struct dso *dso, struct machine *machine);
ssize_t dso__data_read_offset(struct dso *dso, struct machine *machine,
			      u64 offset, u8 *data, ssize_t size);
ssize_t dso__data_read_addr(struct dso *dso, struct map *map,
			    struct machine *machine, u64 addr,
			    u8 *data, ssize_t size);

struct map *dso__new_map(const char *name);
struct dso *dso__kernel_findnew(struct machine *machine, const char *name,
				const char *short_name, int dso_type);

void dsos__add(struct list_head *head, struct dso *dso);
struct dso *dsos__find(struct list_head *head, const char *name,
		       bool cmp_short);
struct dso *__dsos__findnew(struct list_head *head, const char *name);
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
	return dso->data_type == DSO_BINARY_TYPE__VMLINUX ||
	       dso->data_type == DSO_BINARY_TYPE__GUEST_VMLINUX;
}

static inline bool dso__is_kcore(struct dso *dso)
{
	return dso->data_type == DSO_BINARY_TYPE__KCORE ||
	       dso->data_type == DSO_BINARY_TYPE__GUEST_KCORE;
}

#endif /* __PERF_DSO */
