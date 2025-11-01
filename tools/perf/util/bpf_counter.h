/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_BPF_COUNTER_H
#define __PERF_BPF_COUNTER_H 1

struct evsel;
struct target;

#ifdef HAVE_BPF_SKEL

typedef int (*bpf_counter_evsel_op)(struct evsel *evsel);
typedef int (*bpf_counter_evsel_target_op)(struct evsel *evsel,
					   struct target *target);
typedef int (*bpf_counter_evsel_install_pe_op)(struct evsel *evsel,
					       int cpu_map_idx,
					       int fd);

/* Shared ops between bpf_counter, bpf_counter_cgroup, etc. */
struct bpf_counter_ops {
	bpf_counter_evsel_target_op load;
	bpf_counter_evsel_op enable;
	bpf_counter_evsel_op disable;
	bpf_counter_evsel_op read;
	bpf_counter_evsel_op destroy;
	bpf_counter_evsel_install_pe_op install_pe;
};

int bpf_counter__load(struct evsel *evsel, struct target *target);
int bpf_counter__enable(struct evsel *evsel);
int bpf_counter__disable(struct evsel *evsel);
int bpf_counter__read(struct evsel *evsel);
void bpf_counter__destroy(struct evsel *evsel);
int bpf_counter__install_pe(struct evsel *evsel, int cpu_map_idx, int fd);

int bperf_trigger_reading(int prog_fd, int cpu);
void set_max_rlimit(void);

#else /* HAVE_BPF_SKEL */

#include <linux/err.h>

static inline int bpf_counter__load(struct evsel *evsel __maybe_unused,
				    struct target *target __maybe_unused)
{
	return 0;
}

static inline int bpf_counter__enable(struct evsel *evsel __maybe_unused)
{
	return 0;
}

static inline int bpf_counter__disable(struct evsel *evsel __maybe_unused)
{
	return 0;
}

static inline int bpf_counter__read(struct evsel *evsel __maybe_unused)
{
	return -EAGAIN;
}

static inline void bpf_counter__destroy(struct evsel *evsel __maybe_unused)
{
}

static inline int bpf_counter__install_pe(struct evsel *evsel __maybe_unused,
					  int cpu __maybe_unused,
					  int fd __maybe_unused)
{
	return 0;
}

#endif /* HAVE_BPF_SKEL */

#endif /* __PERF_BPF_COUNTER_H */
