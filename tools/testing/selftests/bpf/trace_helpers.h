/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TRACE_HELPER_H
#define __TRACE_HELPER_H

struct ksym {
	long addr;
	char *name;
};

int load_kallsyms(void);
struct ksym *ksym_search(long key);

typedef int (*perf_event_print_fn)(void *data, int size);

/* return code for perf_event_print_fn */
#define PERF_EVENT_DONE		0
#define PERF_EVENT_ERROR	-1
#define PERF_EVENT_CONT		-2

int perf_event_mmap(int fd);
/* return PERF_EVENT_DONE or PERF_EVENT_ERROR */
int perf_event_poller(int fd, perf_event_print_fn output_fn);
#endif
