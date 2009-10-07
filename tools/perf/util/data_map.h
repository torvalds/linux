#ifndef __PERF_DATAMAP_H
#define __PERF_DATAMAP_H

#include "event.h"
#include "header.h"

typedef int (*event_type_handler_t)(event_t *, unsigned long, unsigned long);

struct perf_file_handler {
	event_type_handler_t	process_sample_event;
	event_type_handler_t	process_mmap_event;
	event_type_handler_t	process_comm_event;
	event_type_handler_t	process_fork_event;
	event_type_handler_t	process_exit_event;
	event_type_handler_t	process_lost_event;
	event_type_handler_t	process_read_event;
	event_type_handler_t	process_throttle_event;
	event_type_handler_t	process_unthrottle_event;
	int			(*sample_type_check)(u64 sample_type);
	unsigned long		total_unknown;
};

void register_perf_file_handler(struct perf_file_handler *handler);
int mmap_dispatch_perf_file(struct perf_header **pheader,
			    const char *input_name,
			    int force,
			    int full_paths,
			    int *cwdlen,
			    char **cwd);

#endif
