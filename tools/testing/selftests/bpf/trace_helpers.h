/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TRACE_HELPER_H
#define __TRACE_HELPER_H

#include <libbpf.h>

struct ksym {
	long addr;
	char *name;
};

int load_kallsyms(void);
struct ksym *ksym_search(long key);
long ksym_get_addr(const char *name);

typedef enum bpf_perf_event_ret (*perf_event_print_fn)(void *data, int size);

int perf_event_mmap(int fd);
/* return LIBBPF_PERF_EVENT_DONE or LIBBPF_PERF_EVENT_ERROR */
int perf_event_poller(int fd, perf_event_print_fn output_fn);
#endif
