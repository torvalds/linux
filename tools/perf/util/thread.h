#ifndef __PERF_THREAD_H
#define __PERF_THREAD_H

#include <linux/rbtree.h>
#include <unistd.h>
#include "symbol.h"

struct thread {
	struct rb_node		rb_node;
	struct rb_root		maps;
	struct list_head	removed_maps;
	pid_t			pid;
	char			shortname[3];
	char			*comm;
};

int thread__set_comm(struct thread *self, const char *comm);
struct thread *
threads__findnew(pid_t pid, struct rb_root *threads, struct thread **last_match);
struct thread *
threads__findnew_nocomm(pid_t pid, struct rb_root *threads,
			struct thread **last_match);
struct thread *
register_idle_thread(struct rb_root *threads, struct thread **last_match);
void thread__insert_map(struct thread *self, struct map *map);
int thread__fork(struct thread *self, struct thread *parent);
size_t threads__fprintf(FILE *fp, struct rb_root *threads);

void maps__insert(struct rb_root *maps, struct map *map);
struct map *maps__find(struct rb_root *maps, u64 ip);

struct symbol *kernel_maps__find_symbol(const u64 ip, struct map **mapp);
struct map *kernel_maps__find_by_dso_name(const char *name);

static inline struct map *thread__find_map(struct thread *self, u64 ip)
{
	return self ? maps__find(&self->maps, ip) : NULL;
}

#endif	/* __PERF_THREAD_H */
