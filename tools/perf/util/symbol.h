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

extern unsigned int symbol__priv_size;

static inline void *symbol__priv(struct symbol *self)
{
	return ((void *)self) - symbol__priv_size;
}

struct dso {
	struct list_head node;
	struct rb_root	 syms;
	struct symbol    *(*find_symbol)(struct dso *, u64 ip);
	unsigned char	 adjust_symbols;
	unsigned char	 slen_calculated;
	bool		 loaded;
	unsigned char	 origin;
	const char	 *short_name;
	char	 	 *long_name;
	char		 name[0];
};

struct dso *dso__new(const char *name);
void dso__delete(struct dso *self);

struct symbol *dso__find_symbol(struct dso *self, u64 ip);

int dsos__load_kernel(const char *vmlinux, symbol_filter_t filter, int modules);
struct dso *dsos__findnew(const char *name);
int dso__load(struct dso *self, struct map *map, symbol_filter_t filter);
void dsos__fprintf(FILE *fp);

size_t dso__fprintf(struct dso *self, FILE *fp);
char dso__symtab_origin(const struct dso *self);

int filename__read_build_id(const char *filename, void *bf, size_t size);

int load_kernel(symbol_filter_t filter);

void symbol__init(unsigned int priv_size);

extern struct list_head dsos;
extern struct map *kernel_map;
extern struct dso *vdso;
extern const char *vmlinux_name;
extern int   modules;
#endif /* __PERF_SYMBOL */
