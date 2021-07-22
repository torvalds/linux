/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_BPF_COUNTER_H
#define __PERF_BPF_COUNTER_H 1

#include <linux/list.h>
#include <sys/resource.h>
#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <bpf/libbpf.h>

struct evsel;
struct target;
struct bpf_counter;

typedef int (*bpf_counter_evsel_op)(struct evsel *evsel);
typedef int (*bpf_counter_evsel_target_op)(struct evsel *evsel,
					   struct target *target);
typedef int (*bpf_counter_evsel_install_pe_op)(struct evsel *evsel,
					       int cpu,
					       int fd);

struct bpf_counter_ops {
	bpf_counter_evsel_target_op load;
	bpf_counter_evsel_op enable;
	bpf_counter_evsel_op disable;
	bpf_counter_evsel_op read;
	bpf_counter_evsel_op destroy;
	bpf_counter_evsel_install_pe_op install_pe;
};

struct bpf_counter {
	void *skel;
	struct list_head list;
};

#ifdef HAVE_BPF_SKEL

int bpf_counter__load(struct evsel *evsel, struct target *target);
int bpf_counter__enable(struct evsel *evsel);
int bpf_counter__disable(struct evsel *evsel);
int bpf_counter__read(struct evsel *evsel);
void bpf_counter__destroy(struct evsel *evsel);
int bpf_counter__install_pe(struct evsel *evsel, int cpu, int fd);

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

static inline void set_max_rlimit(void)
{
	struct rlimit rinf = { RLIM_INFINITY, RLIM_INFINITY };

	setrlimit(RLIMIT_MEMLOCK, &rinf);
}

static inline __u32 bpf_link_get_id(int fd)
{
	struct bpf_link_info link_info = { .id = 0, };
	__u32 link_info_len = sizeof(link_info);

	bpf_obj_get_info_by_fd(fd, &link_info, &link_info_len);
	return link_info.id;
}

static inline __u32 bpf_link_get_prog_id(int fd)
{
	struct bpf_link_info link_info = { .id = 0, };
	__u32 link_info_len = sizeof(link_info);

	bpf_obj_get_info_by_fd(fd, &link_info, &link_info_len);
	return link_info.prog_id;
}

static inline __u32 bpf_map_get_id(int fd)
{
	struct bpf_map_info map_info = { .id = 0, };
	__u32 map_info_len = sizeof(map_info);

	bpf_obj_get_info_by_fd(fd, &map_info, &map_info_len);
	return map_info.id;
}

/* trigger the leader program on a cpu */
static inline int bperf_trigger_reading(int prog_fd, int cpu)
{
	DECLARE_LIBBPF_OPTS(bpf_test_run_opts, opts,
			    .ctx_in = NULL,
			    .ctx_size_in = 0,
			    .flags = BPF_F_TEST_RUN_ON_CPU,
			    .cpu = cpu,
			    .retval = 0,
		);

	return bpf_prog_test_run_opts(prog_fd, &opts);
}

#endif /* __PERF_BPF_COUNTER_H */
