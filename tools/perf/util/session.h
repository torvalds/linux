#ifndef __PERF_SESSION_H
#define __PERF_SESSION_H

#include "event.h"
#include "header.h"
#include "thread.h"
#include <linux/rbtree.h>
#include "../../../include/linux/perf_event.h"

struct ip_callchain;
struct thread;
struct symbol;

struct perf_session {
	struct perf_header	header;
	unsigned long		size;
	unsigned long		mmap_window;
	struct map_groups	kmaps;
	struct rb_root		threads;
	struct thread		*last_match;
	struct events_stats	events_stats;
	unsigned long		event_total[PERF_RECORD_MAX];
	struct rb_root		hists;
	u64			sample_type;
	int			fd;
	int			cwdlen;
	char			*cwd;
	char filename[0];
};

typedef int (*event_op)(event_t *self, struct perf_session *session);

struct perf_event_ops {
	event_op	process_sample_event;
	event_op	process_mmap_event;
	event_op	process_comm_event;
	event_op	process_fork_event;
	event_op	process_exit_event;
	event_op	process_lost_event;
	event_op	process_read_event;
	event_op	process_throttle_event;
	event_op	process_unthrottle_event;
	int		(*sample_type_check)(struct perf_session *session);
	unsigned long	total_unknown;
	bool		full_paths;
};

struct perf_session *perf_session__new(const char *filename, int mode, bool force);
void perf_session__delete(struct perf_session *self);

int perf_session__process_events(struct perf_session *self,
				 struct perf_event_ops *event_ops);

struct symbol **perf_session__resolve_callchain(struct perf_session *self,
						struct thread *thread,
						struct ip_callchain *chain,
						struct symbol **parent);

int perf_header__read_build_ids(int input, u64 offset, u64 file_size);

#endif /* __PERF_SESSION_H */
