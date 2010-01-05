#ifndef __PERF_SYMBOL
#define __PERF_SYMBOL 1

#include <linux/types.h>
#include <stdbool.h>
#include "types.h"
#include <linux/list.h>
#include <linux/rbtree.h>
#include "event.h"

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

struct symbol {
	struct rb_node	rb_node;
	u64		start;
	u64		end;
	char		name[0];
};

struct strlist;

struct symbol_conf {
	unsigned short	priv_size;
	bool		try_vmlinux_path,
			use_modules,
			sort_by_name,
			show_nr_samples,
			use_callchain,
			exclude_other;
	const char	*vmlinux_name,
			*field_sep;
	char            *dso_list_str,
			*comm_list_str,
			*sym_list_str,
			*col_width_list_str;
       struct strlist	*dso_list,
			*comm_list,
			*sym_list;
};

extern struct symbol_conf symbol_conf;

static inline void *symbol__priv(struct symbol *self)
{
	return ((void *)self) - symbol_conf.priv_size;
}

struct addr_location {
	struct thread *thread;
	struct map    *map;
	struct symbol *sym;
	u64	      addr;
	char	      level;
	bool	      filtered;
};

struct dso {
	struct list_head node;
	struct rb_root	 symbols[MAP__NR_TYPES];
	struct rb_root	 symbol_names[MAP__NR_TYPES];
	u8		 adjust_symbols:1;
	u8		 slen_calculated:1;
	u8		 has_build_id:1;
	u8		 kernel:1;
	unsigned char	 origin;
	u8		 sorted_by_name;
	u8		 loaded;
	u8		 build_id[BUILD_ID_SIZE];
	u16		 long_name_len;
	const char	 *short_name;
	char	 	 *long_name;
	char		 name[0];
};

struct dso *dso__new(const char *name);
void dso__delete(struct dso *self);

bool dso__loaded(const struct dso *self, enum map_type type);
bool dso__sorted_by_name(const struct dso *self, enum map_type type);

void dso__sort_by_name(struct dso *self, enum map_type type);

struct perf_session;

struct dso *dsos__findnew(const char *name);
int dso__load(struct dso *self, struct map *map, struct perf_session *session,
	      symbol_filter_t filter);
void dsos__fprintf(FILE *fp);
size_t dsos__fprintf_buildid(FILE *fp);

size_t dso__fprintf_buildid(struct dso *self, FILE *fp);
size_t dso__fprintf(struct dso *self, enum map_type type, FILE *fp);
char dso__symtab_origin(const struct dso *self);
void dso__set_build_id(struct dso *self, void *build_id);
struct symbol *dso__find_symbol(struct dso *self, enum map_type type, u64 addr);
struct symbol *dso__find_symbol_by_name(struct dso *self, enum map_type type,
					const char *name);

int filename__read_build_id(const char *filename, void *bf, size_t size);
int sysfs__read_build_id(const char *filename, void *bf, size_t size);
bool dsos__read_build_ids(void);
int build_id__sprintf(u8 *self, int len, char *bf);

int symbol__init(void);
int perf_session__create_kernel_maps(struct perf_session *self);

extern struct list_head dsos__user, dsos__kernel;
extern struct dso *vdso;
#endif /* __PERF_SYMBOL */
