#ifndef PERF_UTIL_OFF_CPU_H
#define PERF_UTIL_OFF_CPU_H

#include <linux/perf_event.h>

struct evlist;
struct target;
struct perf_session;
struct record_opts;

#define OFFCPU_EVENT  "offcpu-time"

#define OFFCPU_SAMPLE_TYPES  (PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_IP | \
			      PERF_SAMPLE_TID | PERF_SAMPLE_TIME | \
			      PERF_SAMPLE_ID | PERF_SAMPLE_CPU | \
			      PERF_SAMPLE_PERIOD | PERF_SAMPLE_CALLCHAIN | \
			      PERF_SAMPLE_CGROUP)


#ifdef HAVE_BPF_SKEL
int off_cpu_prepare(struct evlist *evlist, struct target *target,
		    struct record_opts *opts);
int off_cpu_write(struct perf_session *session);
#else
static inline int off_cpu_prepare(struct evlist *evlist __maybe_unused,
				  struct target *target __maybe_unused,
				  struct record_opts *opts __maybe_unused)
{
	return -1;
}

static inline int off_cpu_write(struct perf_session *session __maybe_unused)
{
	return -1;
}
#endif

#endif  /* PERF_UTIL_OFF_CPU_H */
