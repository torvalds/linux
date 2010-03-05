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
	struct map_groups	kmaps;
	struct rb_root		threads;
	struct thread		*last_match;
	struct map		*vmlinux_maps[MAP__NR_TYPES];
	struct events_stats	events_stats;
	struct rb_root		stats_by_id;
	unsigned long		event_total[PERF_RECORD_MAX];
	unsigned long		unknown_events;
	struct rb_root		hists;
	u64			sample_type;
	struct ref_reloc_sym	ref_reloc_sym;
	int			fd;
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
		 unthrottle;
};

struct perf_session *perf_session__new(const char *filename, int mode, bool force);
void perf_session__delete(struct perf_session *self);

void perf_event_header__bswap(struct perf_event_header *self);

int __perf_session__process_events(struct perf_session *self,
				   u64 data_offset, u64 data_size, u64 size,
				   struct perf_event_ops *ops);
int perf_session__process_events(struct perf_session *self,
				 struct perf_event_ops *event_ops);

struct symbol **perf_session__resolve_callchain(struct perf_session *self,
						struct thread *thread,
						struct ip_callchain *chain,
						struct symbol **parent);

bool perf_session__has_traces(struct perf_session *self, const char *msg);

int perf_header__read_build_ids(struct perf_header *self, int input,
				u64 offset, u64 file_size);

int perf_session__set_kallsyms_ref_reloc_sym(struct perf_session *self,
					     const char *symbol_name,
					     u64 addr);

void mem_bswap_64(void *src, int byte_size);

static inline int __perf_session__create_kernel_maps(struct perf_session *self,
						struct dso *kernel)
{
	return __map_groups__create_kernel_maps(&self->kmaps,
						self->vmlinux_maps, kernel);
}

static inline struct map *
	perf_session__new_module_map(struct perf_session *self,
				     u64 start, const char *filename)
{
	return map_groups__new_module(&self->kmaps, start, filename);
}
#endif /* __PERF_SESSION_H */
