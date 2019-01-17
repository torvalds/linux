/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_BPF_EVENT_H
#define __PERF_BPF_EVENT_H

#include <linux/compiler.h>
#include "event.h"

struct machine;
union perf_event;
struct perf_sample;
struct perf_tool;
struct record_opts;

#ifdef HAVE_LIBBPF_SUPPORT
int machine__process_bpf_event(struct machine *machine, union perf_event *event,
			       struct perf_sample *sample);

int perf_event__synthesize_bpf_events(struct perf_tool *tool,
				      perf_event__handler_t process,
				      struct machine *machine,
				      struct record_opts *opts);
#else
static inline int machine__process_bpf_event(struct machine *machine __maybe_unused,
					     union perf_event *event __maybe_unused,
					     struct perf_sample *sample __maybe_unused)
{
	return 0;
}

static inline int perf_event__synthesize_bpf_events(struct perf_tool *tool __maybe_unused,
						    perf_event__handler_t process __maybe_unused,
						    struct machine *machine __maybe_unused,
						    struct record_opts *opts __maybe_unused)
{
	return 0;
}
#endif // HAVE_LIBBPF_SUPPORT
#endif
