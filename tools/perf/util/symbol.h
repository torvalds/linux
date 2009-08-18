#ifndef _PERF_SYMBOL_
#define _PERF_SYMBOL_ 1

#include <linux/types.h>
#include "../types.h"
#include "list.h"
#include "rbtree.h"

struct symbol {
	struct rb_node	rb_node;
	u64		start;
	u64		end;
	u64		obj_start;
	u64		hist_sum;
	u64		*hist;
	void		*priv;
	char		name[0];
};

struct dso {
	struct list_head node;
	struct rb_root	 syms;
	unsigned int	 sym_priv_size;
	struct symbol    *(*find_symbol)(struct dso *, u64 ip);
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
		     symbol_filter_t filter, int verbose);
int dso__load(struct dso *self, symbol_filter_t filter, int verbose);

size_t dso__fprintf(struct dso *self, FILE *fp);

void symbol__init(void);
#endif /* _PERF_SYMBOL_ */
