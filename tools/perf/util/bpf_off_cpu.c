// SPDX-License-Identifier: GPL-2.0
#include "util/bpf_counter.h"
#include "util/debug.h"
#include "util/evsel.h"
#include "util/evlist.h"
#include "util/off_cpu.h"
#include "util/perf-hooks.h"
#include "util/record.h"
#include "util/session.h"
#include "util/target.h"
#include "util/cpumap.h"
#include "util/thread_map.h"
#include "util/cgroup.h"
#include "util/strlist.h"
#include <bpf/bpf.h>

#include "bpf_skel/off_cpu.skel.h"

#define MAX_STACKS  32
#define MAX_PROC  4096
/* we don't need actual timestamp, just want to put the samples at last */
#define OFF_CPU_TIMESTAMP  (~0ull << 32)

static struct off_cpu_bpf *skel;

struct off_cpu_key {
	u32 pid;
	u32 tgid;
	u32 stack_id;
	u32 state;
	u64 cgroup_id;
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

static void off_cpu_start(void *arg)
{
	struct evlist *evlist = arg;

	/* update task filter for the given workload */
	if (!skel->bss->has_cpu && !skel->bss->has_task &&
	    perf_thread_map__pid(evlist->core.threads, 0) != -1) {
		int fd;
		u32 pid;
		u8 val = 1;

		skel->bss->has_task = 1;
		skel->bss->uses_tgid = 1;
		fd = bpf_map__fd(skel->maps.task_filter);
		pid = perf_thread_map__pid(evlist->core.threads, 0);
		bpf_map_update_elem(fd, &pid, &val, BPF_ANY);
	}

	skel->bss->enabled = 1;
}

static void off_cpu_finish(void *arg __maybe_unused)
{
	skel->bss->enabled = 0;
	off_cpu_bpf__destroy(skel);
}

/* v5.18 kernel added prev_state arg, so it needs to check the signature */
static void check_sched_switch_args(void)
{
	const struct btf *btf = btf__load_vmlinux_btf();
	const struct btf_type *t1, *t2, *t3;
	u32 type_id;

	type_id = btf__find_by_name_kind(btf, "btf_trace_sched_switch",
					 BTF_KIND_TYPEDEF);
	if ((s32)type_id < 0)
		return;

	t1 = btf__type_by_id(btf, type_id);
	if (t1 == NULL)
		return;

	t2 = btf__type_by_id(btf, t1->type);
	if (t2 == NULL || !btf_is_ptr(t2))
		return;

	t3 = btf__type_by_id(btf, t2->type);
	/* btf_trace func proto has one more argument for the context */
	if (t3 && btf_is_func_proto(t3) && btf_vlen(t3) == 5) {
		/* new format: pass prev_state as 4th arg */
		skel->rodata->has_prev_state = true;
	}
}

int off_cpu_prepare(struct evlist *evlist, struct target *target,
		    struct record_opts *opts)
{
	int err, fd, i;
	int ncpus = 1, ntasks = 1, ncgrps = 1;
	struct strlist *pid_slist = NULL;
	struct str_node *pos;

	if (off_cpu_config(evlist) < 0) {
		pr_err("Failed to config off-cpu BPF event\n");
		return -1;
	}

	skel = off_cpu_bpf__open();
	if (!skel) {
		pr_err("Failed to open off-cpu BPF skeleton\n");
		return -1;
	}

	/* don't need to set cpu filter for system-wide mode */
	if (target->cpu_list) {
		ncpus = perf_cpu_map__nr(evlist->core.user_requested_cpus);
		bpf_map__set_max_entries(skel->maps.cpu_filter, ncpus);
	}

	if (target->pid) {
		pid_slist = strlist__new(target->pid, NULL);
		if (!pid_slist) {
			pr_err("Failed to create a strlist for pid\n");
			return -1;
		}

		ntasks = 0;
		strlist__for_each_entry(pos, pid_slist) {
			char *end_ptr;
			int pid = strtol(pos->s, &end_ptr, 10);

			if (pid == INT_MIN || pid == INT_MAX ||
			    (*end_ptr != '\0' && *end_ptr != ','))
				continue;

			ntasks++;
		}

		if (ntasks < MAX_PROC)
			ntasks = MAX_PROC;

		bpf_map__set_max_entries(skel->maps.task_filter, ntasks);
	} else if (target__has_task(target)) {
		ntasks = perf_thread_map__nr(evlist->core.threads);
		bpf_map__set_max_entries(skel->maps.task_filter, ntasks);
	} else if (target__none(target)) {
		bpf_map__set_max_entries(skel->maps.task_filter, MAX_PROC);
	}

	if (evlist__first(evlist)->cgrp) {
		ncgrps = evlist->core.nr_entries - 1; /* excluding a dummy */
		bpf_map__set_max_entries(skel->maps.cgroup_filter, ncgrps);

		if (!cgroup_is_v2("perf_event"))
			skel->rodata->uses_cgroup_v1 = true;
	}

	if (opts->record_cgroup) {
		skel->rodata->needs_cgroup = true;

		if (!cgroup_is_v2("perf_event"))
			skel->rodata->uses_cgroup_v1 = true;
	}

	set_max_rlimit();
	check_sched_switch_args();

	err = off_cpu_bpf__load(skel);
	if (err) {
		pr_err("Failed to load off-cpu skeleton\n");
		goto out;
	}

	if (target->cpu_list) {
		u32 cpu;
		u8 val = 1;

		skel->bss->has_cpu = 1;
		fd = bpf_map__fd(skel->maps.cpu_filter);

		for (i = 0; i < ncpus; i++) {
			cpu = perf_cpu_map__cpu(evlist->core.user_requested_cpus, i).cpu;
			bpf_map_update_elem(fd, &cpu, &val, BPF_ANY);
		}
	}

	if (target->pid) {
		u8 val = 1;

		skel->bss->has_task = 1;
		skel->bss->uses_tgid = 1;
		fd = bpf_map__fd(skel->maps.task_filter);

		strlist__for_each_entry(pos, pid_slist) {
			char *end_ptr;
			u32 tgid;
			int pid = strtol(pos->s, &end_ptr, 10);

			if (pid == INT_MIN || pid == INT_MAX ||
			    (*end_ptr != '\0' && *end_ptr != ','))
				continue;

			tgid = pid;
			bpf_map_update_elem(fd, &tgid, &val, BPF_ANY);
		}
	} else if (target__has_task(target)) {
		u32 pid;
		u8 val = 1;

		skel->bss->has_task = 1;
		fd = bpf_map__fd(skel->maps.task_filter);

		for (i = 0; i < ntasks; i++) {
			pid = perf_thread_map__pid(evlist->core.threads, i);
			bpf_map_update_elem(fd, &pid, &val, BPF_ANY);
		}
	}

	if (evlist__first(evlist)->cgrp) {
		struct evsel *evsel;
		u8 val = 1;

		skel->bss->has_cgroup = 1;
		fd = bpf_map__fd(skel->maps.cgroup_filter);

		evlist__for_each_entry(evlist, evsel) {
			struct cgroup *cgrp = evsel->cgrp;

			if (cgrp == NULL)
				continue;

			if (!cgrp->id && read_cgroup_id(cgrp) < 0) {
				pr_err("Failed to read cgroup id of %s\n",
				       cgrp->name);
				goto out;
			}

			bpf_map_update_elem(fd, &cgrp->id, &val, BPF_ANY);
		}
	}

	err = off_cpu_bpf__attach(skel);
	if (err) {
		pr_err("Failed to attach off-cpu BPF skeleton\n");
		goto out;
	}

	if (perf_hooks__set_hook("record_start", off_cpu_start, evlist) ||
	    perf_hooks__set_hook("record_end", off_cpu_finish, evlist)) {
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

	if (sample_type & ~OFFCPU_SAMPLE_TYPES) {
		pr_err("not supported sample type: %llx\n",
		       (unsigned long long)sample_type);
		return -1;
	}

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
		if (sample_type & PERF_SAMPLE_CGROUP)
			data.array[n++] = key.cgroup_id;

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
