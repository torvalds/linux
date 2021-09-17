/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TRACE_HELPER_H
#define __TRACE_HELPER_H

#include <bpf/libbpf.h>

struct ksym {
	long addr;
	char *name;
};

int load_kallsyms(void);
struct ksym *ksym_search(long key);
long ksym_get_addr(const char *name);

/* open kallsyms and find addresses on the fly, faster than load + search. */
int kallsyms_find(const char *sym, unsigned long long *addr);

/* find the address of the next symbol, this can be used to determine the
 * end of a function
 */
int kallsyms_find_next(const char *sym, unsigned long long *addr);

void read_trace_pipe(void);

ssize_t get_uprobe_offset(const void *addr, ssize_t base);
ssize_t get_base_addr(void);
ssize_t get_rel_offset(uintptr_t addr);

#endif
