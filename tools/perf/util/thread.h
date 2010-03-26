#ifndef __PERF_THREAD_H
#define __PERF_THREAD_H

#include <linux/rbtree.h>
#include <unistd.h>
#include "symbol.h"

struct map_groups {
	struct rb_root		maps[MAP__NR_TYPES];
	struct list_head	removed_maps[MAP__NR_TYPES];
};

size_t __map_groups__fprintf_maps(struct map_groups *self,
				  enum map_type type, FILE *fp);

struct thread {
	struct rb_node		rb_node;
	struct map_groups	mg;
	pid_t			pid;
	char			shortname[3];
	bool			comm_set;
	char			*comm;
	int			comm_len;
};

void map_groups__init(struct map_groups *self);
int thread__set_comm(struct thread *self, const char *comm);
int thread__comm_len(struct thread *self);
struct thread *perf_session__findnew(struct perf_session *self, pid_t pid);
void thread__insert_map(struct thread *self, struct map *map);
int thread__fork(struct thread *self, struct thread *parent);
size_t map_groups__fprintf_maps(struct map_groups *self, FILE *fp);
size_t perf_session__fprintf(struct perf_session *self, FILE *fp);

void maps__insert(struct rb_root *maps, struct map *map);
struct map *maps__find(struct rb_root *maps, u64 addr);

static inline void map_groups__insert(struct map_groups *self, struct map *map)
{
	 maps__insert(&self->maps[map->type], map);
}

static inline struct map *map_groups__find(struct map_groups *self,
					   enum map_type type, u64 addr)
{
	return maps__find(&self->maps[type], addr);
}

static inline struct map *thread__find_map(struct thread *self,
					   enum map_type type, u64 addr)
{
	return self ? map_groups__find(&self->mg, type, addr) : NULL;
}

void thread__find_addr_map(struct thread *self,
			   struct perf_session *session, u8 cpumode,
			   enum map_type type, u64 addr,
			   struct addr_location *al);

void thread__find_addr_location(struct thread *self,
				struct perf_session *session, u8 cpumode,
				enum map_type type, u64 addr,
				struct addr_location *al,
				symbol_filter_t filter);
struct symbol *map_groups__find_symbol(struct map_groups *self,
				       enum map_type type, u64 addr,
				       symbol_filter_t filter);

static inline struct symbol *map_groups__find_function(struct map_groups *self,
						       u64 addr,
						       symbol_filter_t filter)
{
	return map_groups__find_symbol(self, MAP__FUNCTION, addr, filter);
}

struct map *map_groups__find_by_name(struct map_groups *self,
				     enum map_type type, const char *name);

int __map_groups__create_kernel_maps(struct map_groups *self,
				     struct map *vmlinux_maps[MAP__NR_TYPES],
				     struct dso *kernel);
int map_groups__create_kernel_maps(struct map_groups *self,
				   struct map *vmlinux_maps[MAP__NR_TYPES]);

struct map *map_groups__new_module(struct map_groups *self, u64 start,
				   const char *filename);
#endif	/* __PERF_THREAD_H */
