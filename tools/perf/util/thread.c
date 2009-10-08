#include "../perf.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "thread.h"
#include "util.h"
#include "debug.h"

static struct thread *thread__new(pid_t pid)
{
	struct thread *self = calloc(1, sizeof(*self));

	if (self != NULL) {
		self->pid = pid;
		self->comm = malloc(32);
		if (self->comm)
			snprintf(self->comm, 32, ":%d", self->pid);
		self->maps = RB_ROOT;
		INIT_LIST_HEAD(&self->removed_maps);
	}

	return self;
}

int thread__set_comm(struct thread *self, const char *comm)
{
	if (self->comm)
		free(self->comm);
	self->comm = strdup(comm);
	return self->comm ? 0 : -ENOMEM;
}

static size_t thread__fprintf(struct thread *self, FILE *fp)
{
	struct rb_node *nd;
	struct map *pos;
	size_t ret = fprintf(fp, "Thread %d %s\nCurrent maps:\n",
			     self->pid, self->comm);

	for (nd = rb_first(&self->maps); nd; nd = rb_next(nd)) {
		pos = rb_entry(nd, struct map, rb_node);
		ret += map__fprintf(pos, fp);
	}

	ret = fprintf(fp, "Removed maps:\n");

	list_for_each_entry(pos, &self->removed_maps, node)
		ret += map__fprintf(pos, fp);

	return ret;
}

struct thread *
threads__findnew(pid_t pid, struct rb_root *threads, struct thread **last_match)
{
	struct rb_node **p = &threads->rb_node;
	struct rb_node *parent = NULL;
	struct thread *th;

	/*
	 * Font-end cache - PID lookups come in blocks,
	 * so most of the time we dont have to look up
	 * the full rbtree:
	 */
	if (*last_match && (*last_match)->pid == pid)
		return *last_match;

	while (*p != NULL) {
		parent = *p;
		th = rb_entry(parent, struct thread, rb_node);

		if (th->pid == pid) {
			*last_match = th;
			return th;
		}

		if (pid < th->pid)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	th = thread__new(pid);
	if (th != NULL) {
		rb_link_node(&th->rb_node, parent, p);
		rb_insert_color(&th->rb_node, threads);
		*last_match = th;
	}

	return th;
}

struct thread *
register_idle_thread(struct rb_root *threads, struct thread **last_match)
{
	struct thread *thread = threads__findnew(0, threads, last_match);

	if (!thread || thread__set_comm(thread, "swapper")) {
		fprintf(stderr, "problem inserting idle task.\n");
		exit(-1);
	}

	return thread;
}

static void thread__remove_overlappings(struct thread *self, struct map *map)
{
	struct rb_node *next = rb_first(&self->maps);

	while (next) {
		struct map *pos = rb_entry(next, struct map, rb_node);
		next = rb_next(&pos->rb_node);

		if (!map__overlap(pos, map))
			continue;

		if (verbose >= 2) {
			printf("overlapping maps:\n");
			map__fprintf(map, stdout);
			map__fprintf(pos, stdout);
		}

		rb_erase(&pos->rb_node, &self->maps);
		/*
		 * We may have references to this map, for instance in some
		 * hist_entry instances, so just move them to a separate
		 * list.
		 */
		list_add_tail(&pos->node, &self->removed_maps);
	}
}

void maps__insert(struct rb_root *maps, struct map *map)
{
	struct rb_node **p = &maps->rb_node;
	struct rb_node *parent = NULL;
	const u64 ip = map->start;
	struct map *m;

	while (*p != NULL) {
		parent = *p;
		m = rb_entry(parent, struct map, rb_node);
		if (ip < m->start)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&map->rb_node, parent, p);
	rb_insert_color(&map->rb_node, maps);
}

struct map *maps__find(struct rb_root *maps, u64 ip)
{
	struct rb_node **p = &maps->rb_node;
	struct rb_node *parent = NULL;
	struct map *m;

	while (*p != NULL) {
		parent = *p;
		m = rb_entry(parent, struct map, rb_node);
		if (ip < m->start)
			p = &(*p)->rb_left;
		else if (ip > m->end)
			p = &(*p)->rb_right;
		else
			return m;
	}

	return NULL;
}

void thread__insert_map(struct thread *self, struct map *map)
{
	thread__remove_overlappings(self, map);
	maps__insert(&self->maps, map);
}

int thread__fork(struct thread *self, struct thread *parent)
{
	struct rb_node *nd;

	if (self->comm)
		free(self->comm);
	self->comm = strdup(parent->comm);
	if (!self->comm)
		return -ENOMEM;

	for (nd = rb_first(&parent->maps); nd; nd = rb_next(nd)) {
		struct map *map = rb_entry(nd, struct map, rb_node);
		struct map *new = map__clone(map);
		if (!new)
			return -ENOMEM;
		thread__insert_map(self, new);
	}

	return 0;
}

size_t threads__fprintf(FILE *fp, struct rb_root *threads)
{
	size_t ret = 0;
	struct rb_node *nd;

	for (nd = rb_first(threads); nd; nd = rb_next(nd)) {
		struct thread *pos = rb_entry(nd, struct thread, rb_node);

		ret += thread__fprintf(pos, fp);
	}

	return ret;
}
