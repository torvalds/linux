/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_BPF_EVENT_H
#define __PERF_BPF_EVENT_H

#include <linux/compiler.h>
#include <linux/rbtree.h>
#include <pthread.h>
#include <api/fd/array.h>
#include <stdio.h>

struct bpf_prog_info;
struct machine;
union perf_event;
struct perf_env;
struct perf_sample;
struct perf_session;
struct record_opts;
struct evlist;
struct target;

struct bpf_prog_info_node {
	struct bpf_prog_info_linear	*info_linear;
	struct rb_node			rb_node;
};

struct btf_node {
	struct rb_node	rb_node;
	u32		id;
	u32		data_size;
	char		data[];
};

#ifdef HAVE_LIBBPF_SUPPORT
int machine__process_bpf(struct machine *machine, union perf_event *event,
			 struct perf_sample *sample);
int evlist__add_bpf_sb_event(struct evlist *evlist, struct perf_env *env);
void bpf_event__print_bpf_prog_info(struct bpf_prog_info *info,
				    struct perf_env *env,
				    FILE *fp);
#else
static inline int machine__process_bpf(struct machine *machine __maybe_unused,
				       union perf_event *event __maybe_unused,
				       struct perf_sample *sample __maybe_unused)
{
	return 0;
}

static inline int evlist__add_bpf_sb_event(struct evlist *evlist __maybe_unused,
					   struct perf_env *env __maybe_unused)
{
	return 0;
}

static inline void bpf_event__print_bpf_prog_info(struct bpf_prog_info *info __maybe_unused,
						  struct perf_env *env __maybe_unused,
						  FILE *fp __maybe_unused)
{

}
#endif // HAVE_LIBBPF_SUPPORT
#endif
