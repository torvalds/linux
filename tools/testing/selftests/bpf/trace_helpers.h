/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TRACE_HELPER_H
#define __TRACE_HELPER_H

#include <bpf/libbpf.h>

#define __ALIGN_MASK(x, mask)	(((x)+(mask))&~(mask))
#define ALIGN(x, a)		__ALIGN_MASK(x, (typeof(x))(a)-1)

struct ksym {
	long addr;
	char *name;
};
struct ksyms;

typedef int (*ksym_cmp_t)(const void *p1, const void *p2);
typedef int (*ksym_search_cmp_t)(const void *p1, const struct ksym *p2);

int load_kallsyms(void);
struct ksym *ksym_search(long key);
long ksym_get_addr(const char *name);

struct ksyms *load_kallsyms_local(void);
struct ksym *ksym_search_local(struct ksyms *ksyms, long key);
long ksym_get_addr_local(struct ksyms *ksyms, const char *name);
void free_kallsyms_local(struct ksyms *ksyms);

struct ksyms *load_kallsyms_custom_local(ksym_cmp_t cmp_cb);
struct ksym *search_kallsyms_custom_local(struct ksyms *ksyms, const void *p1,
					  ksym_search_cmp_t cmp_cb);

/* open kallsyms and find addresses on the fly, faster than load + search. */
int kallsyms_find(const char *sym, unsigned long long *addr);

void read_trace_pipe(void);
int read_trace_pipe_iter(void (*cb)(const char *str, void *data),
			 void *data, int iter);

ssize_t get_uprobe_offset(const void *addr);
ssize_t get_rel_offset(uintptr_t addr);

int read_build_id(const char *path, char *build_id, size_t size);

#endif
