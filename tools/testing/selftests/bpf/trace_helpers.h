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

int load_kallsyms(void);
int load_kallsyms_refresh(void);

struct ksym *ksym_search(long key);
long ksym_get_addr(const char *name);

/* open kallsyms and find addresses on the fly, faster than load + search. */
int kallsyms_find(const char *sym, unsigned long long *addr);

void read_trace_pipe(void);

ssize_t get_uprobe_offset(const void *addr);
ssize_t get_rel_offset(uintptr_t addr);

int read_build_id(const char *path, char *build_id, size_t size);

#endif
