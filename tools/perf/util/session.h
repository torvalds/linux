#ifndef __PERF_SESSION_H
#define __PERF_SESSION_H

#include "event.h"
#include "header.h"
#include "symbol.h"
#include "thread.h"
#include <linux/rbtree.h>
#include "../../../include/linux/perf_event.h"

struct ip_callchain;
struct thread;

struct perf_session {
	struct perf_header	header;
	unsigned long		size;
	unsigned long		mmap_window;
	struct rb_root		threads;
	struct thread		*last_match;
	struct rb_root		kerninfo_root;
	struct events_stats	events_stats;
	struct rb_root		stats_by_id;
	unsigned long		event_total[PERF_RECORD_MAX];
	unsigned long		unknown_events;
	struct rb_root		hists;
	u64			sample_type;
	int			fd;
	bool			fd_pipe;
	int			cwdlen;
	char			*cwd;
	char filename[0];
};

typedef int (*event_op)(event_t *self, struct perf_session *session);

struct perf_event_ops {
	event_op sample,
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
};

struct perf_session *perf_session__new(const char *filename, int mode, bool force);
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
#endif /* __PERF_SESSION_H */
