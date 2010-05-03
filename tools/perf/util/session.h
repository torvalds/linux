#ifndef __PERF_SESSION_H
#define __PERF_SESSION_H

#include "event.h"
#include "header.h"
#include "symbol.h"
#include "thread.h"
#include <linux/rbtree.h>
#include "../../../include/linux/perf_event.h"

struct sample_queue;
struct ip_callchain;
struct thread;

struct ordered_samples {
	u64			last_flush;
	u64			next_flush;
	u64			max_timestamp;
	struct list_head	samples_head;
	struct sample_queue	*last_inserted;
};

struct perf_session {
	struct perf_header	header;
	unsigned long		size;
	unsigned long		mmap_window;
	struct rb_root		threads;
	struct thread		*last_match;
	struct rb_root		machines;
	struct events_stats	events_stats;
	struct rb_root		stats_by_id;
	unsigned long		event_total[PERF_RECORD_MAX];
	unsigned long		unknown_events;
	struct rb_root		hists;
	u64			sample_type;
	int			fd;
	bool			fd_pipe;
	bool			repipe;
	int			cwdlen;
	char			*cwd;
	struct ordered_samples	ordered_samples;
	char filename[0];
};

struct perf_event_ops;

typedef int (*event_op)(event_t *self, struct perf_session *session);
typedef int (*event_op2)(event_t *self, struct perf_session *session,
			 struct perf_event_ops *ops);

struct perf_event_ops {
	event_op	sample,
			mmap,
			comm,
			fork,
			exit,
			lost,
			read,
			throttle,
			unthrottle,
			attr,
			event_type,
			tracing_data,
			build_id;
	event_op2	finished_round;
	bool		ordered_samples;
};

struct perf_session *perf_session__new(const char *filename, int mode, bool force, bool repipe);
void perf_session__delete(struct perf_session *self);

void perf_event_header__bswap(struct perf_event_header *self);

int __perf_session__process_events(struct perf_session *self,
				   u64 data_offset, u64 data_size, u64 size,
				   struct perf_event_ops *ops);
int perf_session__process_events(struct perf_session *self,
				 struct perf_event_ops *event_ops);

struct map_symbol *perf_session__resolve_callchain(struct perf_session *self,
						   struct thread *thread,
						   struct ip_callchain *chain,
						   struct symbol **parent);

bool perf_session__has_traces(struct perf_session *self, const char *msg);

int perf_session__set_kallsyms_ref_reloc_sym(struct map **maps,
					     const char *symbol_name,
					     u64 addr);

void mem_bswap_64(void *src, int byte_size);

int perf_session__create_kernel_maps(struct perf_session *self);

int do_read(int fd, void *buf, size_t size);
void perf_session__update_sample_type(struct perf_session *self);

#ifdef NO_NEWT_SUPPORT
static inline int perf_session__browse_hists(struct rb_root *hists __used,
					      u64 nr_hists __used,
					      u64 session_total __used,
					     const char *helpline __used,
					     const char *input_name __used)
{
	return 0;
}
#else
int perf_session__browse_hists(struct rb_root *hists, u64 nr_hists,
			       u64 session_total, const char *helpline,
			       const char *input_name);
#endif

static inline
struct machine *perf_session__find_host_machine(struct perf_session *self)
{
	return machines__find_host(&self->machines);
}

static inline
struct machine *perf_session__find_machine(struct perf_session *self, pid_t pid)
{
	return machines__find(&self->machines, pid);
}

static inline
struct machine *perf_session__findnew_machine(struct perf_session *self, pid_t pid)
{
	return machines__findnew(&self->machines, pid);
}

static inline
void perf_session__process_machines(struct perf_session *self,
				    machine__process_t process)
{
	return machines__process(&self->machines, process, self);
}

static inline
size_t perf_session__fprintf_dsos(struct perf_session *self, FILE *fp)
{
	return machines__fprintf_dsos(&self->machines, fp);
}

static inline
size_t perf_session__fprintf_dsos_buildid(struct perf_session *self, FILE *fp,
					  bool with_hits)
{
	return machines__fprintf_dsos_buildid(&self->machines, fp, with_hits);
}
#endif /* __PERF_SESSION_H */
