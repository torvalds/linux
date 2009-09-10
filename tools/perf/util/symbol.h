#ifndef _PERF_SYMBOL_
#define _PERF_SYMBOL_ 1

#include <linux/types.h>
#include "types.h"
#include <linux/list.h>
#include <linux/rbtree.h>
#include "module.h"

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

#ifndef DMGL_PARAMS
#define DMGL_PARAMS      (1 << 0)       /* Include function args */
#define DMGL_ANSI        (1 << 1)       /* Include const, volatile, etc */
#endif

struct symbol {
	struct rb_node	rb_node;
	u64		start;
	u64		end;
	u64		obj_start;
	u64		hist_sum;
	u64		*hist;
	struct module	*module;
	void		*priv;
	char		name[0];
};

struct dso {
	struct list_head node;
	struct rb_root	 syms;
	struct symbol    *(*find_symbol)(struct dso *, u64 ip);
	unsigned int	 sym_priv_size;
	unsigned char	 adjust_symbols;
	unsigned char	 slen_calculated;
	unsigned char	 origin;
	char		 name[0];
};

const char *sym_hist_filter;

typedef int (*symbol_filter_t)(struct dso *self, struct symbol *sym);

struct dso *dso__new(const char *name, unsigned int sym_priv_size);
void dso__delete(struct dso *self);

static inline void *dso__sym_priv(struct dso *self, struct symbol *sym)
{
	return ((void *)sym) - self->sym_priv_size;
}

struct symbol *dso__find_symbol(struct dso *self, u64 ip);

int dso__load_kernel(struct dso *self, const char *vmlinux,
		     symbol_filter_t filter, int verbose, int modules);
int dso__load_modules(struct dso *self, symbol_filter_t filter, int verbose);
int dso__load(struct dso *self, symbol_filter_t filter, int verbose);

size_t dso__fprintf(struct dso *self, FILE *fp);
char dso__symtab_origin(const struct dso *self);

void symbol__init(void);
#endif /* _PERF_SYMBOL_ */
