// SPDX-License-Identifier: LGPL-2.1

#ifndef _PERF_BPF_PID_FILTER_
#define _PERF_BPF_PID_FILTER_

#include <bpf.h>

#define pid_filter(name) pid_map(name, bool)

static int pid_filter__add(struct bpf_map *pids, pid_t pid)
{
	bool value = true;
	return bpf_map_update_elem(pids, &pid, &value, BPF_NOEXIST);
}

static bool pid_filter__has(struct bpf_map *pids, pid_t pid)
{
	return bpf_map_lookup_elem(pids, &pid) != NULL;
}

#endif // _PERF_BPF_PID_FILTER_
