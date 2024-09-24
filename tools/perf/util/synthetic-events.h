/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_SYNTHETIC_EVENTS_H
#define __PERF_SYNTHETIC_EVENTS_H

#include <stdbool.h>
#include <sys/types.h> // pid_t
#include <linux/compiler.h>
#include <linux/types.h>
#include <perf/cpumap.h>

struct auxtrace_record;
struct build_id;
struct dso;
struct evlist;
struct evsel;
struct machine;
struct perf_counts_values;
struct perf_cpu_map;
struct perf_data;
struct perf_event_attr;
struct perf_event_mmap_page;
struct perf_sample;
struct perf_session;
struct perf_stat_config;
struct perf_thread_map;
struct perf_tool;
struct record_opts;
struct target;

union perf_event;

enum perf_record_synth {
	PERF_SYNTH_TASK		= 1 << 0,
	PERF_SYNTH_MMAP		= 1 << 1,
	PERF_SYNTH_CGROUP	= 1 << 2,

	/* last element */
	PERF_SYNTH_MAX		= 1 << 3,
};
#define PERF_SYNTH_ALL  (PERF_SYNTH_MAX - 1)

int parse_synth_opt(char *str);

typedef int (*perf_event__handler_t)(const struct perf_tool *tool, union perf_event *event,
				     struct perf_sample *sample, struct machine *machine);

int perf_event__synthesize_attrs(const struct perf_tool *tool, struct evlist *evlist, perf_event__handler_t process);
int perf_event__synthesize_attr(const struct perf_tool *tool, struct perf_event_attr *attr, u32 ids, u64 *id, perf_event__handler_t process);
int perf_event__synthesize_build_id(const struct perf_tool *tool,
				    struct perf_sample *sample,
				    struct machine *machine,
				    perf_event__handler_t process,
				    const struct evsel *evsel,
				    __u16 misc,
				    const struct build_id *bid,
				    const char *filename);
int perf_event__synthesize_mmap2_build_id(const struct perf_tool *tool,
					  struct perf_sample *sample,
					  struct machine *machine,
					  perf_event__handler_t process,
					  const struct evsel *evsel,
					  __u16 misc,
					  __u32 pid, __u32 tid,
					  __u64 start, __u64 len, __u64 pgoff,
					  const struct build_id *bid,
					  __u32 prot, __u32 flags,
					  const char *filename);
int perf_event__synthesize_cpu_map(const struct perf_tool *tool, const struct perf_cpu_map *cpus, perf_event__handler_t process, struct machine *machine);
int perf_event__synthesize_event_update_cpus(const struct perf_tool *tool, struct evsel *evsel, perf_event__handler_t process);
int perf_event__synthesize_event_update_name(const struct perf_tool *tool, struct evsel *evsel, perf_event__handler_t process);
int perf_event__synthesize_event_update_scale(const struct perf_tool *tool, struct evsel *evsel, perf_event__handler_t process);
int perf_event__synthesize_event_update_unit(const struct perf_tool *tool, struct evsel *evsel, perf_event__handler_t process);
int perf_event__synthesize_extra_attr(const struct perf_tool *tool, struct evlist *evsel_list, perf_event__handler_t process, bool is_pipe);
int perf_event__synthesize_extra_kmaps(const struct perf_tool *tool, perf_event__handler_t process, struct machine *machine);
int perf_event__synthesize_features(const struct perf_tool *tool, struct perf_session *session, struct evlist *evlist, perf_event__handler_t process);
int perf_event__synthesize_id_index(const struct perf_tool *tool, perf_event__handler_t process, struct evlist *evlist, struct machine *machine);
int __perf_event__synthesize_id_index(const struct perf_tool *tool, perf_event__handler_t process, struct evlist *evlist, struct machine *machine, size_t from);
int perf_event__synthesize_id_sample(__u64 *array, u64 type, const struct perf_sample *sample);
int perf_event__synthesize_kernel_mmap(const struct perf_tool *tool, perf_event__handler_t process, struct machine *machine);
int perf_event__synthesize_mmap_events(const struct perf_tool *tool, union perf_event *event, pid_t pid, pid_t tgid, perf_event__handler_t process, struct machine *machine, bool mmap_data);
int perf_event__synthesize_modules(const struct perf_tool *tool, perf_event__handler_t process, struct machine *machine);
int perf_event__synthesize_namespaces(const struct perf_tool *tool, union perf_event *event, pid_t pid, pid_t tgid, perf_event__handler_t process, struct machine *machine);
int perf_event__synthesize_cgroups(const struct perf_tool *tool, perf_event__handler_t process, struct machine *machine);
int perf_event__synthesize_sample(union perf_event *event, u64 type, u64 read_format, const struct perf_sample *sample);
int perf_event__synthesize_stat_config(const struct perf_tool *tool, struct perf_stat_config *config, perf_event__handler_t process, struct machine *machine);
int perf_event__synthesize_stat_events(struct perf_stat_config *config, const struct perf_tool *tool, struct evlist *evlist, perf_event__handler_t process, bool attrs);
int perf_event__synthesize_stat_round(const struct perf_tool *tool, u64 time, u64 type, perf_event__handler_t process, struct machine *machine);
int perf_event__synthesize_stat(const struct perf_tool *tool, struct perf_cpu cpu, u32 thread, u64 id, struct perf_counts_values *count, perf_event__handler_t process, struct machine *machine);
int perf_event__synthesize_thread_map2(const struct perf_tool *tool, struct perf_thread_map *threads, perf_event__handler_t process, struct machine *machine);
int perf_event__synthesize_thread_map(const struct perf_tool *tool, struct perf_thread_map *threads, perf_event__handler_t process, struct machine *machine, bool needs_mmap, bool mmap_data);
int perf_event__synthesize_threads(const struct perf_tool *tool, perf_event__handler_t process, struct machine *machine, bool needs_mmap, bool mmap_data, unsigned int nr_threads_synthesize);
int perf_event__synthesize_tracing_data(const struct perf_tool *tool, int fd, struct evlist *evlist, perf_event__handler_t process);
int perf_event__synth_time_conv(const struct perf_event_mmap_page *pc, const struct perf_tool *tool, perf_event__handler_t process, struct machine *machine);
pid_t perf_event__synthesize_comm(const struct perf_tool *tool, union perf_event *event, pid_t pid, perf_event__handler_t process, struct machine *machine);

int perf_tool__process_synth_event(const struct perf_tool *tool, union perf_event *event, struct machine *machine, perf_event__handler_t process);

size_t perf_event__sample_event_size(const struct perf_sample *sample, u64 type, u64 read_format);

int __machine__synthesize_threads(struct machine *machine, const struct perf_tool *tool,
				  struct target *target, struct perf_thread_map *threads,
				  perf_event__handler_t process, bool needs_mmap, bool data_mmap,
				  unsigned int nr_threads_synthesize);
int machine__synthesize_threads(struct machine *machine, struct target *target,
				struct perf_thread_map *threads, bool needs_mmap, bool data_mmap,
				unsigned int nr_threads_synthesize);

#ifdef HAVE_AUXTRACE_SUPPORT
int perf_event__synthesize_auxtrace_info(struct auxtrace_record *itr, const struct perf_tool *tool,
					 struct perf_session *session, perf_event__handler_t process);

#else // HAVE_AUXTRACE_SUPPORT

#include <errno.h>

static inline int
perf_event__synthesize_auxtrace_info(struct auxtrace_record *itr __maybe_unused,
				     const struct perf_tool *tool __maybe_unused,
				     struct perf_session *session __maybe_unused,
				     perf_event__handler_t process __maybe_unused)
{
	return -EINVAL;
}
#endif // HAVE_AUXTRACE_SUPPORT

#ifdef HAVE_LIBBPF_SUPPORT
int perf_event__synthesize_bpf_events(struct perf_session *session, perf_event__handler_t process,
				      struct machine *machine, struct record_opts *opts);
#else // HAVE_LIBBPF_SUPPORT
static inline int perf_event__synthesize_bpf_events(struct perf_session *session __maybe_unused,
						    perf_event__handler_t process __maybe_unused,
						    struct machine *machine __maybe_unused,
						    struct record_opts *opts __maybe_unused)
{
	return 0;
}
#endif // HAVE_LIBBPF_SUPPORT

int perf_event__synthesize_for_pipe(const struct perf_tool *tool,
				    struct perf_session *session,
				    struct perf_data *data,
				    perf_event__handler_t process);

#endif // __PERF_SYNTHETIC_EVENTS_H
