#ifndef __PERF_THREAD_H
#define __PERF_THREAD_H

#include <linux/rbtree.h>
#include <unistd.h>
#include "symbol.h"

struct thread {
	union {
		struct rb_node	 rb_node;
		struct list_head node;
	};
	struct map_groups	mg;
	pid_t			pid;
	char			shortname[3];
	bool			comm_set;
	char			*comm;
	int			comm_len;
};

struct thread_map {
	int nr;
	int map[];
};

struct perf_session;

void thread__delete(struct thread *self);

struct thread_map *thread_map__new_by_pid(pid_t pid);
struct thread_map *thread_map__new_by_tid(pid_t tid);
struct thread_map *thread_map__new(pid_t pid, pid_t tid);

static inline void thread_map__delete(struct thread_map *threads)
{
	free(threads);
}

int thread__set_comm(struct thread *self, const char *comm);
int thread__comm_len(struct thread *self);
struct thread *perf_session__findnew(struct perf_session *self, pid_t pid);
void thread__insert_map(struct thread *self, struct map *map);
int thread__fork(struct thread *self, struct thread *parent);
size_t perf_session__fprintf(struct perf_session *self, FILE *fp);

static inline struct map *thread__find_map(struct thread *self,
					   enum map_type type, u64 addr)
{
	return self ? map_groups__find(&self->mg, type, addr) : NULL;
}

void thread__find_addr_map(struct thread *self,
			   struct perf_session *session, u8 cpumode,
			   enum map_type type, pid_t pid, u64 addr,
			   struct addr_location *al);

void thread__find_addr_location(struct thread *self,
				struct perf_session *session, u8 cpumode,
				enum map_type type, pid_t pid, u64 addr,
				struct addr_location *al,
				symbol_filter_t filter);
#endif	/* __PERF_THREAD_H */
