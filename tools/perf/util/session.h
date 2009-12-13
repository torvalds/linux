#ifndef __PERF_SESSION_H
#define __PERF_SESSION_H

#include "event.h"
#include "header.h"

struct perf_session {
	struct perf_header	header;
	unsigned long		size;
	int			fd;
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
	int		(*sample_type_check)(u64 sample_type);
	unsigned long	total_unknown;
};

struct perf_session *perf_session__new(const char *filename, int mode,
				       bool force);
void perf_session__delete(struct perf_session *self);

int perf_session__process_events(struct perf_session *self,
				 struct perf_event_ops *event_ops,
				 int full_paths, int *cwdlen, char **cwd);

int perf_header__read_build_ids(int input, u64 offset, u64 file_size);

#endif /* __PERF_SESSION_H */
