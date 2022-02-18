#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>

#include <linux/err.h>

#include "util/ftrace.h"
#include "util/cpumap.h"
#include "util/thread_map.h"
#include "util/debug.h"
#include "util/evlist.h"
#include "util/bpf_counter.h"

#include "util/bpf_skel/func_latency.skel.h"

static struct func_latency_bpf *skel;

int perf_ftrace__latency_prepare_bpf(struct perf_ftrace *ftrace)
{
	int fd, err;
	int i, ncpus = 1, ntasks = 1;
	struct filter_entry *func;

	if (!list_is_singular(&ftrace->filters)) {
		pr_err("ERROR: %s target function(s).\n",
		       list_empty(&ftrace->filters) ? "No" : "Too many");
		return -1;
	}

	func = list_first_entry(&ftrace->filters, struct filter_entry, list);

	skel = func_latency_bpf__open();
	if (!skel) {
		pr_err("Failed to open func latency skeleton\n");
		return -1;
	}

	/* don't need to set cpu filter for system-wide mode */
	if (ftrace->target.cpu_list) {
		ncpus = perf_cpu_map__nr(ftrace->evlist->core.cpus);
		bpf_map__set_max_entries(skel->maps.cpu_filter, ncpus);
	}

	if (target__has_task(&ftrace->target) || target__none(&ftrace->target)) {
		ntasks = perf_thread_map__nr(ftrace->evlist->core.threads);
		bpf_map__set_max_entries(skel->maps.task_filter, ntasks);
	}

	set_max_rlimit();

	err = func_latency_bpf__load(skel);
	if (err) {
		pr_err("Failed to load func latency skeleton\n");
		goto out;
	}

	if (ftrace->target.cpu_list) {
		u32 cpu;
		u8 val = 1;

		skel->bss->has_cpu = 1;
		fd = bpf_map__fd(skel->maps.cpu_filter);

		for (i = 0; i < ncpus; i++) {
			cpu = perf_cpu_map__cpu(ftrace->evlist->core.cpus, i).cpu;
			bpf_map_update_elem(fd, &cpu, &val, BPF_ANY);
		}
	}

	if (target__has_task(&ftrace->target) || target__none(&ftrace->target)) {
		u32 pid;
		u8 val = 1;

		skel->bss->has_task = 1;
		fd = bpf_map__fd(skel->maps.task_filter);

		for (i = 0; i < ntasks; i++) {
			pid = perf_thread_map__pid(ftrace->evlist->core.threads, i);
			bpf_map_update_elem(fd, &pid, &val, BPF_ANY);
		}
	}

	skel->links.func_begin = bpf_program__attach_kprobe(skel->progs.func_begin,
							    false, func->name);
	if (IS_ERR(skel->links.func_begin)) {
		pr_err("Failed to attach fentry program\n");
		err = PTR_ERR(skel->links.func_begin);
		goto out;
	}

	skel->links.func_end = bpf_program__attach_kprobe(skel->progs.func_end,
							  true, func->name);
	if (IS_ERR(skel->links.func_end)) {
		pr_err("Failed to attach fexit program\n");
		err = PTR_ERR(skel->links.func_end);
		goto out;
	}

	/* XXX: we don't actually use this fd - just for poll() */
	return open("/dev/null", O_RDONLY);

out:
	return err;
}

int perf_ftrace__latency_start_bpf(struct perf_ftrace *ftrace __maybe_unused)
{
	skel->bss->enabled = 1;
	return 0;
}

int perf_ftrace__latency_stop_bpf(struct perf_ftrace *ftrace __maybe_unused)
{
	skel->bss->enabled = 0;
	return 0;
}

int perf_ftrace__latency_read_bpf(struct perf_ftrace *ftrace __maybe_unused,
				  int buckets[])
{
	int i, fd, err;
	u32 idx;
	u64 *hist;
	int ncpus = cpu__max_cpu().cpu;

	fd = bpf_map__fd(skel->maps.latency);

	hist = calloc(ncpus, sizeof(*hist));
	if (hist == NULL)
		return -ENOMEM;

	for (idx = 0; idx < NUM_BUCKET; idx++) {
		err = bpf_map_lookup_elem(fd, &idx, hist);
		if (err) {
			buckets[idx] = 0;
			continue;
		}

		for (i = 0; i < ncpus; i++)
			buckets[idx] += hist[i];
	}

	free(hist);
	return 0;
}

int perf_ftrace__latency_cleanup_bpf(struct perf_ftrace *ftrace __maybe_unused)
{
	func_latency_bpf__destroy(skel);
	return 0;
}
