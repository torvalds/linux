#ifndef __PERF_THREAD_H
#define __PERF_THREAD_H

#include <linux/rbtree.h>
#include <unistd.h>
#include <sys/types.h>
#include "symbol.h"

struct thread {
	union {
		struct rb_node	 rb_node;
		struct list_head node;
	};
	struct map_groups	mg;
	pid_t			tid;
	pid_t			ppid;
	char			shortname[3];
	bool			comm_set;
	bool			dead; /* if set thread has exited */
	char			*comm;
	int			comm_len;

	void			*priv;
};

struct machine;

struct thread *thread__new(pid_t tid);
void thread__delete(struct thread *self);
static inline void thread__exited(struct thread *thread)
{
	thread->dead = true;
}

int thread__set_comm(struct thread *self, const char *comm);
int thread__comm_len(struct thread *self);
void thread__insert_map(struct thread *self, struct map *map);
int thread__fork(struct thread *self, struct thread *parent);
size_t thread__fprintf(struct thread *thread, FILE *fp);

static inline struct map *thread__find_map(struct thread *self,
					   enum map_type type, u64 addr)
{
	return self ? map_groups__find(&self->mg, type, addr) : NULL;
}

void thread__find_addr_map(struct thread *thread, struct machine *machine,
			   u8 cpumode, enum map_type type, u64 addr,
			   struct addr_location *al);

void thread__find_addr_location(struct thread *thread, struct machine *machine,
				u8 cpumode, enum map_type type, u64 addr,
				struct addr_location *al);

static inline void *thread__priv(struct thread *thread)
{
	return thread->priv;
}

static inline void thread__set_priv(struct thread *thread, void *p)
{
	thread->priv = p;
}
#endif	/* __PERF_THREAD_H */
