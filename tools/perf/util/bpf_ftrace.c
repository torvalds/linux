#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>

#include <bpf/bpf.h>
#include <linux/err.h>

#include "util/ftrace.h"
#include "util/cpumap.h"
#include "util/thread_map.h"
#include "util/debug.h"
#include "util/evlist.h"
#include "util/bpf_counter.h"
#include "util/stat.h"

#include "util/bpf_skel/func_latency.skel.h"

static struct func_latency_bpf *skel;

int perf_ftrace__latency_prepare_bpf(struct perf_ftrace *ftrace)
{
	int fd, err;
	int i, ncpus = 1, ntasks = 1;
	struct filter_entry *func = NULL;

	if (!list_empty(&ftrace->filters)) {
		if (!list_is_singular(&ftrace->filters)) {
			pr_err("ERROR: Too many target functions.\n");
			return -1;
		}
		func = list_first_entry(&ftrace->filters, struct filter_entry, list);
	} else {
		int count = 0;
		struct list_head *pos;

		list_for_each(pos, &ftrace->event_pair)
			count++;

		if (count != 2) {
			pr_err("ERROR: Needs two target events.\n");
			return -1;
		}
	}

	skel = func_latency_bpf__open();
	if (!skel) {
		pr_err("Failed to open func latency skeleton\n");
		return -1;
	}

	skel->rodata->bucket_range = ftrace->bucket_range;
	skel->rodata->min_latency = ftrace->min_latency;
	skel->rodata->bucket_num = ftrace->bucket_num;
	if (ftrace->bucket_range && ftrace->bucket_num) {
		bpf_map__set_max_entries(skel->maps.latency, ftrace->bucket_num);
	}

	/* don't need to set cpu filter for system-wide mode */
	if (ftrace->target.cpu_list) {
		ncpus = perf_cpu_map__nr(ftrace->evlist->core.user_requested_cpus);
		bpf_map__set_max_entries(skel->maps.cpu_filter, ncpus);
		skel->rodata->has_cpu = 1;
	}

	if (target__has_task(&ftrace->target) || target__none(&ftrace->target)) {
		ntasks = perf_thread_map__nr(ftrace->evlist->core.threads);
		bpf_map__set_max_entries(skel->maps.task_filter, ntasks);
		skel->rodata->has_task = 1;
	}

	skel->rodata->use_nsec = ftrace->use_nsec;

	set_max_rlimit();

	err = func_latency_bpf__load(skel);
	if (err) {
		pr_err("Failed to load func latency skeleton\n");
		goto out;
	}

	if (ftrace->target.cpu_list) {
		u32 cpu;
		u8 val = 1;

		fd = bpf_map__fd(skel->maps.cpu_filter);

		for (i = 0; i < ncpus; i++) {
			cpu = perf_cpu_map__cpu(ftrace->evlist->core.user_requested_cpus, i).cpu;
			bpf_map_update_elem(fd, &cpu, &val, BPF_ANY);
		}
	}

	if (target__has_task(&ftrace->target) || target__none(&ftrace->target)) {
		u32 pid;
		u8 val = 1;

		fd = bpf_map__fd(skel->maps.task_filter);

		for (i = 0; i < ntasks; i++) {
			pid = perf_thread_map__pid(ftrace->evlist->core.threads, i);
			bpf_map_update_elem(fd, &pid, &val, BPF_ANY);
		}
	}

	skel->bss->min = INT64_MAX;

	if (func) {
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
	} else {
		struct filter_entry *event;

		event = list_first_entry(&ftrace->event_pair, struct filter_entry, list);

		skel->links.event_begin = bpf_program__attach_raw_tracepoint(skel->progs.event_begin,
									     event->name);
		if (IS_ERR(skel->links.event_begin)) {
			pr_err("Failed to attach first tracepoint program\n");
			err = PTR_ERR(skel->links.event_begin);
			goto out;
		}

		event = list_next_entry(event, list);

		skel->links.event_end = bpf_program__attach_raw_tracepoint(skel->progs.event_end,
									     event->name);
		if (IS_ERR(skel->links.event_end)) {
			pr_err("Failed to attach second tracepoint program\n");
			err = PTR_ERR(skel->links.event_end);
			goto out;
		}
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

int perf_ftrace__latency_read_bpf(struct perf_ftrace *ftrace,
				  int buckets[], struct stats *stats)
{
	int i, fd, err;
	u32 idx;
	u64 *hist;
	int ncpus = cpu__max_cpu().cpu;

	fd = bpf_map__fd(skel->maps.latency);

	hist = calloc(ncpus, sizeof(*hist));
	if (hist == NULL)
		return -ENOMEM;

	for (idx = 0; idx < skel->rodata->bucket_num; idx++) {
		err = bpf_map_lookup_elem(fd, &idx, hist);
		if (err) {
			buckets[idx] = 0;
			continue;
		}

		for (i = 0; i < ncpus; i++)
			buckets[idx] += hist[i];
	}

	if (skel->bss->count) {
		stats->mean = skel->bss->total / skel->bss->count;
		stats->n = skel->bss->count;
		stats->max = skel->bss->max;
		stats->min = skel->bss->min;

		if (!ftrace->use_nsec) {
			stats->mean /= 1000;
			stats->max /= 1000;
			stats->min /= 1000;
		}
	}

	free(hist);
	return 0;
}

int perf_ftrace__latency_cleanup_bpf(struct perf_ftrace *ftrace __maybe_unused)
{
	func_latency_bpf__destroy(skel);
	return 0;
}
