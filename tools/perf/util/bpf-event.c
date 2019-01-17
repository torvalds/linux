// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <bpf/bpf.h>
#include "bpf-event.h"
#include "debug.h"
#include "symbol.h"

int machine__process_bpf_event(struct machine *machine __maybe_unused,
			       union perf_event *event,
			       struct perf_sample *sample __maybe_unused)
{
	if (dump_trace)
		perf_event__fprintf_bpf_event(event, stdout);
	return 0;
}
