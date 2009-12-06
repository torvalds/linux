#ifndef __PERF_THREAD_H
#define __PERF_THREAD_H

#include <linux/rbtree.h>
#include <unistd.h>
#include "symbol.h"

struct thread {
	struct rb_node		rb_node;
	struct rb_root		maps[MAP__NR_TYPES];
	struct list_head	removed_maps[MAP__NR_TYPES];
	pid_t			pid;
	bool			use_modules;
	char			shortname[3];
	char			*comm;
	int			comm_len;
};

void thread__init(struct thread *self, pid_t pid);
int thread__set_comm(struct thread *self, const char *comm);
int thread__comm_len(struct thread *self);
struct thread *threads__findnew(pid_t pid);
struct thread *register_idle_thread(void);
void thread__insert_map(struct thread *self, struct map *map);
int thread__fork(struct thread *self, struct thread *parent);
size_t thread__fprintf_maps(struct thread *self, FILE *fp);
size_t threads__fprintf(FILE *fp);

void maps__insert(struct rb_root *maps, struct map *map);
struct map *maps__find(struct rb_root *maps, u64 addr);

static inline struct map *thread__find_map(struct thread *self,
					   enum map_type type, u64 addr)
{
	return self ? maps__find(&self->maps[type], addr) : NULL;
}

static inline void __thread__insert_map(struct thread *self, struct map *map)
{
	 maps__insert(&self->maps[map->type], map);
}

void thread__find_addr_location(struct thread *self, u8 cpumode,
				enum map_type type, u64 addr,
				struct addr_location *al,
				symbol_filter_t filter);
struct symbol *thread__find_symbol(struct thread *self,
				   enum map_type type, u64 addr,
				   symbol_filter_t filter);

static inline struct symbol *
thread__find_function(struct thread *self, u64 addr, symbol_filter_t filter)
{
	return thread__find_symbol(self, MAP__FUNCTION, addr, filter);
}
#endif	/* __PERF_THREAD_H */
