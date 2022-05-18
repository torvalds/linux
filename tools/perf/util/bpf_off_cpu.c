// SPDX-License-Identifier: GPL-2.0
#include "util/bpf_counter.h"
#include "util/debug.h"
#include "util/evsel.h"
#include "util/evlist.h"
#include "util/off_cpu.h"
#include "util/perf-hooks.h"
#include "util/session.h"
#include <bpf/bpf.h>

#include "bpf_skel/off_cpu.skel.h"

#define MAX_STACKS  32
/* we don't need actual timestamp, just want to put the samples at last */
#define OFF_CPU_TIMESTAMP  (~0ull << 32)

static struct off_cpu_bpf *skel;

struct off_cpu_key {
	u32 pid;
	u32 tgid;
	u32 stack_id;
	u32 state;
};

union off_cpu_data {
	struct perf_event_header hdr;
	u64 array[1024 / sizeof(u64)];
};

static int off_cpu_config(struct evlist *evlist)
{
	struct evsel *evsel;
	struct perf_event_attr attr = {
		.type	= PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_BPF_OUTPUT,
		.size	= sizeof(attr), /* to capture ABI version */
	};
	char *evname = strdup(OFFCPU_EVENT);

	if (evname == NULL)
		return -ENOMEM;

	evsel = evsel__new(&attr);
	if (!evsel) {
		free(evname);
		return -ENOMEM;
	}

	evsel->core.attr.freq = 1;
	evsel->core.attr.sample_period = 1;
	/* off-cpu analysis depends on stack trace */
	evsel->core.attr.sample_type = PERF_SAMPLE_CALLCHAIN;

	evlist__add(evlist, evsel);

	free(evsel->name);
	evsel->name = evname;

	return 0;
}

static void off_cpu_start(void *arg __maybe_unused)
{
	skel->bss->enabled = 1;
}

static void off_cpu_finish(void *arg __maybe_unused)
{
	skel->bss->enabled = 0;
	off_cpu_bpf__destroy(skel);
}

int off_cpu_prepare(struct evlist *evlist)
{
	int err;

	if (off_cpu_config(evlist) < 0) {
		pr_err("Failed to config off-cpu BPF event\n");
		return -1;
	}

	set_max_rlimit();

	skel = off_cpu_bpf__open_and_load();
	if (!skel) {
		pr_err("Failed to open off-cpu BPF skeleton\n");
		return -1;
	}

	err = off_cpu_bpf__attach(skel);
	if (err) {
		pr_err("Failed to attach off-cpu BPF skeleton\n");
		goto out;
	}

	if (perf_hooks__set_hook("record_start", off_cpu_start, NULL) ||
	    perf_hooks__set_hook("record_end", off_cpu_finish, NULL)) {
		pr_err("Failed to attach off-cpu skeleton\n");
		goto out;
	}

	return 0;

out:
	off_cpu_bpf__destroy(skel);
	return -1;
}

int off_cpu_write(struct perf_session *session)
{
	int bytes = 0, size;
	int fd, stack;
	u64 sample_type, val, sid = 0;
	struct evsel *evsel;
	struct perf_data_file *file = &session->data->file;
	struct off_cpu_key prev, key;
	union off_cpu_data data = {
		.hdr = {
			.type = PERF_RECORD_SAMPLE,
			.misc = PERF_RECORD_MISC_USER,
		},
	};
	u64 tstamp = OFF_CPU_TIMESTAMP;

	skel->bss->enabled = 0;

	evsel = evlist__find_evsel_by_str(session->evlist, OFFCPU_EVENT);
	if (evsel == NULL) {
		pr_err("%s evsel not found\n", OFFCPU_EVENT);
		return 0;
	}

	sample_type = evsel->core.attr.sample_type;

	if (sample_type & (PERF_SAMPLE_ID | PERF_SAMPLE_IDENTIFIER)) {
		if (evsel->core.id)
			sid = evsel->core.id[0];
	}

	fd = bpf_map__fd(skel->maps.off_cpu);
	stack = bpf_map__fd(skel->maps.stacks);
	memset(&prev, 0, sizeof(prev));

	while (!bpf_map_get_next_key(fd, &prev, &key)) {
		int n = 1;  /* start from perf_event_header */
		int ip_pos = -1;

		bpf_map_lookup_elem(fd, &key, &val);

		if (sample_type & PERF_SAMPLE_IDENTIFIER)
			data.array[n++] = sid;
		if (sample_type & PERF_SAMPLE_IP) {
			ip_pos = n;
			data.array[n++] = 0;  /* will be updated */
		}
		if (sample_type & PERF_SAMPLE_TID)
			data.array[n++] = (u64)key.pid << 32 | key.tgid;
		if (sample_type & PERF_SAMPLE_TIME)
			data.array[n++] = tstamp;
		if (sample_type & PERF_SAMPLE_ID)
			data.array[n++] = sid;
		if (sample_type & PERF_SAMPLE_CPU)
			data.array[n++] = 0;
		if (sample_type & PERF_SAMPLE_PERIOD)
			data.array[n++] = val;
		if (sample_type & PERF_SAMPLE_CALLCHAIN) {
			int len = 0;

			/* data.array[n] is callchain->nr (updated later) */
			data.array[n + 1] = PERF_CONTEXT_USER;
			data.array[n + 2] = 0;

			bpf_map_lookup_elem(stack, &key.stack_id, &data.array[n + 2]);
			while (data.array[n + 2 + len])
				len++;

			/* update length of callchain */
			data.array[n] = len + 1;

			/* update sample ip with the first callchain entry */
			if (ip_pos >= 0)
				data.array[ip_pos] = data.array[n + 2];

			/* calculate sample callchain data array length */
			n += len + 2;
		}
		/* TODO: handle more sample types */

		size = n * sizeof(u64);
		data.hdr.size = size;
		bytes += size;

		if (perf_data_file__write(file, &data, size) < 0) {
			pr_err("failed to write perf data, error: %m\n");
			return bytes;
		}

		prev = key;
		/* increase dummy timestamp to sort later samples */
		tstamp++;
	}
	return bytes;
}
