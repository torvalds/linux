/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_BPF_EVENT_H
#define __PERF_BPF_EVENT_H

#include <linux/compiler.h>

struct machine;
union perf_event;
struct perf_sample;

#ifdef HAVE_LIBBPF_SUPPORT
int machine__process_bpf_event(struct machine *machine, union perf_event *event,
			       struct perf_sample *sample);
#else
static inline int machine__process_bpf_event(struct machine *machine __maybe_unused,
					     union perf_event *event __maybe_unused,
					     struct perf_sample *sample __maybe_unused)
{
	return 0;
}
#endif // HAVE_LIBBPF_SUPPORT
#endif
